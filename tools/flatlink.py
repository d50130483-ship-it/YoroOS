#!/usr/bin/env python3
"""Плоский линкер YoroOS: ELF32 i386 .o -> плоский бинарник (без ld elf_i386).
Поддерживает R_386_32 / PC32 / PLT32 / 16 / PC16 / 8, __bss_start/__bss_end."""
import sys, struct, os

SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_REL, SHT_NOBITS = 1, 2, 3, 9, 8
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 0x1, 0x2, 0x4
R32, RPC32, RPLT32, R16, RPC16, R8 = 1, 2, 4, 20, 21, 22


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def load_obj(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1, f"{path}: не ELF32"
    fmt = "<HHIIIIIHHHHHH"
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(fmt, d, 16)
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        vals = struct.unpack_from("<IIIIIIIIII", d, off)
        s = dict(zip("name type flags addr offset size link info align entsize".split(), vals))
        s["align"] = max(1, s["align"])
        s["data"] = bytearray(d[s["offset"]:s["offset"] + s["size"]]) if s["type"] != SHT_NOBITS else bytearray()
        secs.append(s)
    shstr = secs[e_shstrndx]["data"]
    for s in secs:
        n = s["name"]
        s["sname"] = shstr[n:shstr.index(b"\0", n)].decode()
    syms = []
    for s in secs:
        if s["type"] == SHT_SYMTAB:
            strtab = secs[s["link"]]["data"]
            for i in range(s["size"] // 16):
                o = s["offset"] + i * 16
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", d, o)
                nm = strtab[st_name:strtab.index(b"\0", st_name)].decode() if st_name else ""
                syms.append(dict(name=nm, value=st_value, size=st_size, shndx=st_shndx,
                                 info=st_info, symidx=i, symtab=s))
    rels = [s for s in secs if s["type"] == SHT_REL]
    return dict(path=path, secs=secs, syms=syms, rels=rels)


def build(objs, base, entry_first):
    # порядок объектов: entry-объект первым
    objs = sorted(objs, key=lambda o: 0 if entry_first in o["path"] else 1)
    cur = base
    alloc = []
    # ФАЗЫ: сначала весь код (.text), затем данные (.rodata/.data/.eh_frame), в конце BSS
    collected = []
    for obj in objs:
        for s in obj["secs"]:
            if s["flags"] & SHF_ALLOC and s["type"] in (SHT_PROGBITS, SHT_NOBITS):
                execs = bool(s["flags"] & SHF_EXEC)
                phase = 0 if execs else (1 if s["type"] == SHT_PROGBITS else 2)
                collected.append((phase, obj, s))
    for phase, obj, s in sorted(collected, key=lambda t: t[0]):
        a = max(s["align"], 4) if s["type"] == SHT_NOBITS else s["align"]
        s["vaddr"] = align(cur, a)
        cur = s["vaddr"] + s["size"]
        alloc.append((obj, s))
    # символы
    symval = {}
    bss_start = bss_end = None
    bss_secs = [s for _, s in alloc if s["type"] == SHT_NOBITS]
    if bss_secs:
        bss_start = min(s["vaddr"] for s in bss_secs)
        bss_end = max(s["vaddr"] + s["size"] for s in bss_secs)
    for obj in objs:
        for sym in obj["syms"]:
            if sym["shndx"] == 0xFFF2:  # COMMON -> в bss
                symval[(obj["path"], sym["symidx"])] = bss_end
                bss_end += align(sym["size"], 4)
                continue
            if sym["shndx"] == 0 or sym["shndx"] >= 0xFF00:  # UNDEF/ABS/RESERVED
                symval[(obj["path"], sym["symidx"])] = sym["value"]
            else:
                sec = obj["secs"][sym["shndx"]]
                symval[(obj["path"], sym["symidx"])] = sec["vaddr"] + sym["value"]
    # глобальные имена -> адреса
    glob = {}
    for obj in objs:
        for sym in obj["syms"]:
            if (sym.get("info", 0) & 0x10) and sym["name"] and sym["shndx"] not in (0, 0xFFF2):
                glob[sym["name"]] = symval[(obj["path"], sym["symidx"])]
    for special in ("__bss_start", "__bss_end"):
        if special not in glob:
            glob[special] = {"__bss_start": bss_start or cur, "__bss_end": bss_end or cur}[special]

    # проверка: все UNDEF должны резолвиться
    for obj in objs:
        for sym in obj["syms"]:
            if sym["shndx"] == 0 and sym["name"] and sym["name"] not in glob:
                sys.exit(f"{obj['path']}: неразрешённый символ {sym['name']}")

    def resolve(obj, symi, symtab_sec):
        # символ из конкретной таблицы символов
        for sym in obj["syms"]:
            if sym["symtab"] is symtab_sec and sym["symidx"] == symi:
                if sym["shndx"] == 0 and sym["name"] in glob:
                    return glob[sym["name"]]
                return symval[(obj["path"], sym["symidx"])]
        raise KeyError(f"symbol {symi} in {obj['path']}")

    # релокации
    for obj in objs:
        for rel in obj["rels"]:
            target = obj["secs"][rel["info"]]
            symtab = obj["secs"][rel["link"]]
            n = rel["size"] // 8
            for i in range(n):
                o = i * 8
                r_offset, r_info = struct.unpack_from("<II", bytes(rel["data"]), o)
                t, symi = r_info & 0xFF, r_info >> 8
                S = resolve(obj, symi, symtab)
                P = target["vaddr"] + r_offset
                if t == R32:
                    A = struct.unpack_from("<i", bytes(target["data"]), r_offset)[0]
                    V = (S + A) & 0xFFFFFFFF
                    struct.pack_into("<I", target["data"], r_offset, V)
                elif t in (RPC32, RPLT32):
                    A = struct.unpack_from("<i", bytes(target["data"]), r_offset)[0]
                    V = S + A - P
                    if V >= 1 << 31:
                        V -= 1 << 32
                    if V < -(1 << 31):
                        V += 1 << 32
                    struct.pack_into("<i", target["data"], r_offset, V)
                elif t == R16:
                    A = struct.unpack_from("<H", bytes(target["data"]), r_offset)[0]
                    struct.pack_into("<H", target["data"], r_offset, (S + A) & 0xFFFF)
                elif t == RPC16:
                    A = struct.unpack_from("<h", bytes(target["data"]), r_offset)[0]
                    struct.pack_into("<h", target["data"], r_offset, (S + A - P) & 0xFFFF)
                elif t == R8:
                    A = struct.unpack_from("<B", bytes(target["data"]), r_offset)[0]
                    struct.pack_into("<B", target["data"], r_offset, (S + A) & 0xFF)
                else:
                    sys.exit(f"{obj['path']}: неподдерживаемый тип релокации {t}")
    # образ
    prog = [(s["vaddr"], s["data"]) for _, s in alloc if s["type"] == SHT_PROGBITS and s["data"]]
    if not prog:
        sys.exit("нет PROGBITS секций")
    lo = min(a for a, _ in prog)
    hi = max(a + len(d) for a, d in prog)
    img = bytearray(hi - base)
    for a, d in prog:
        img[a - base:a - base + len(d)] = d
    return bytes(img), glob, (lo, hi)


def main():
    args = sys.argv[1:]
    base = 0x10000
    entry_first = "entry"
    outp, mapf = None, None
    objs = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--base":
            base = int(args[i + 1], 0); i += 2
        elif a == "--entry":
            entry_first = args[i + 1]; i += 2
        elif a == "-o":
            outp = args[i + 1]; i += 2
        elif a == "-M":
            mapf = args[i + 1]; i += 2
        else:
            objs.append(load_obj(a)); i += 1
    img, glob, span = build(objs, base, entry_first)
    open(outp, "wb").write(img)
    if mapf:
        with open(mapf, "w") as f:
            for name, addr in sorted(glob.items(), key=lambda kv: kv[1]):
                if addr:
                    f.write(f"{addr:08x} {name}\n")
    print(f"flatlink: {outp} base={base:#x} size={len(img)} ({span[0]:#x}..{span[1]:#x}), символов: {len(glob)}")


if __name__ == "__main__":
    main()
