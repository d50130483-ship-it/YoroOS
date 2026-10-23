#!/usr/bin/env python3
"""mkpe.py - turn a flat 32-bit payload (linked at <base>+0x1000) into a real
PE32 console executable with a genuine import table.

YoroOS builds its native Win32 samples with the system linker (ld -m elf_i386),
which cannot emit PE directly: binutils here has no dlltool, no import
libraries and no mingw runtime.  So the linker lays out the code and the
import thunks, and this script wraps that payload in the PE headers plus the
import directory (.idata): descriptors, name tables, hint/name entries.

Result: a normal PE32 image that Windows (and YoroOS's own PE loader) can load.
"""

import argparse
import struct

FILE_ALIGN = 0x200
SECT_ALIGN = 0x1000


def align(v, a):
    return (v + a - 1) // a * a


def read_symbols(path):
    syms = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 3:
                try:
                    syms[parts[2]] = int(parts[0], 16)
                except ValueError:
                    pass
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("payload")
    ap.add_argument("out")
    ap.add_argument("--symbols", required=True, help="nm output for the linked ELF")
    ap.add_argument("--image-start", default="_image_start")
    ap.add_argument("--entry", default="_start")
    ap.add_argument("--base", default="0x400000")
    ap.add_argument("--stack-reserve", default="0x100000")
    ap.add_argument("--import", dest="imports", action="append", default=[],
                    metavar="DLL:func1,func2",
                    help="one import library per flag, e.g. KERNEL32.dll:ExitProcess")
    args = ap.parse_args()

    base = int(args.base, 0)
    stack_reserve = int(args.stack_reserve, 0)
    syms = read_symbols(args.symbols)
    start = syms[args.image_start]
    entry_rva = syms[args.entry] - base

    payload = open(args.payload, "rb").read()

    imports = []
    for spec in args.imports:
        dll, funcs = spec.split(":", 1)
        entries = []
        for fn in funcs.split(","):
            fn = fn.strip()
            if not fn:
                continue
            sym = "__imp_" + fn
            if sym not in syms:
                raise SystemExit("mkpe: no IAT slot for %s (link a thunk!)" % fn)
            entries.append((fn, syms[sym] - base))
        for a, b in zip(entries, entries[1:]):
            if b[1] != a[1] + 4:
                raise SystemExit(
                    "mkpe: IAT slots for %s are not contiguous (%s@0x%x, %s@0x%x); "
                    "group the thunks per DLL in thunks.s"
                    % (dll, a[0], a[1], b[0], b[1]))
        imports.append((dll, entries))

    # --- section layout: .text (payload) then .idata -------------------
    text_rva = SECT_ALIGN
    text_raw = 0x400
    text_raw_size = align(len(payload), FILE_ALIGN)
    idata_rva = text_rva + text_raw_size
    idata_raw = text_raw + text_raw_size

    n_desc = len(imports)
    oft_off = align(20 * (n_desc + 1), 8)
    oft_total = sum(4 * (len(f) + 1) for _, f in imports)
    names_off = align(oft_off + oft_total, 8)

    idata = bytearray(names_off)
    hints = bytearray()

    def push_name(s):
        """hint/name entry: 2-byte hint (0) followed by the ASCII name"""
        rva = idata_rva + names_off + len(hints)
        hints.extend(b"\x00\x00" + s.encode("ascii") + b"\x00")
        return rva

    def push_dll(s):
        """module names are plain NUL-terminated strings, no hint word"""
        rva = idata_rva + names_off + len(hints)
        hints.extend(s.encode("ascii") + b"\x00")
        return rva

    oft_pos = oft_off
    for idx, (dll, funcs) in enumerate(imports):
        dll_rva = push_dll(dll)
        oft_rva = idata_rva + oft_pos
        for k, (fn, iat_rva) in enumerate(funcs):
            hrva = push_name(fn)
            struct.pack_into("<I", idata, oft_pos + 4 * k, hrva)
        struct.pack_into("<I", idata, oft_pos + 4 * len(funcs), 0)
        oft_pos += 4 * (len(funcs) + 1)
        first_thunk = funcs[0][1] if funcs else 0
        struct.pack_into("<IIIII", idata, idx * 20,
                         oft_rva, 0, 0, dll_rva, first_thunk)
    idata.extend(hints)
    idata_raw_size = align(len(idata), FILE_ALIGN)

    size_of_image = align(idata_rva + idata_raw_size, SECT_ALIGN)
    size_of_headers = text_raw

    # --- headers -------------------------------------------------------
    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<H", dos, 0x02, 0x90)          # bytes on last page
    struct.pack_into("<H", dos, 0x04, 3)             # pages
    struct.pack_into("<H", dos, 0x06, 0)             # relocations
    struct.pack_into("<H", dos, 0x08, 4)             # header paragraphs
    struct.pack_into("<H", dos, 0x0A, 0)             # min extra
    struct.pack_into("<H", dos, 0x0C, 0xFFFF)        # max extra
    struct.pack_into("<H", dos, 0x10, 0xFFFE)        # ss
    struct.pack_into("<H", dos, 0x14, 0xFFFE)        # sp
    struct.pack_into("<H", dos, 0x18, 0)             # checksum
    struct.pack_into("<H", dos, 0x1C, 0)             # ip
    struct.pack_into("<H", dos, 0x1E, 0)             # cs
    struct.pack_into("<H", dos, 0x20, 0x40)          # lfarlc
    struct.pack_into("<I", dos, 0x3C, 0x40)          # e_lfanew
    stub = b"\x0e\x1f\xba\x0e\x00\xb4\x09\xcd\x21\xb8\x01\x4c\xcd\x21" \
           b"This program cannot be run in DOS mode.\r\r\n$"
    headers = bytearray(dos) + bytearray(0x40 - len(dos))
    headers += stub
    headers += b"\x00" * (size_of_headers - len(headers))

    coff = struct.pack("<HHIIIHH",
                       0x014C,                       # i386
                       2,                            # sections
                       0, 0, 0,                      # timestamp, symtab, nsyms
                       0xE0,                         # SizeOfOptionalHeader
                       0x0102 | 0x0002)              # executable | 32bit
    opt = struct.pack("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII",
                      0x10B,                        # PE32
                      9, 0,                         # linker version
                      text_raw_size,                # SizeOfCode
                      idata_raw_size,               # SizeOfInitializedData
                      0,                            # SizeOfUninitializedData
                      entry_rva,                    # AddressOfEntryPoint
                      text_rva,                     # BaseOfCode
                      text_rva,                     # BaseOfData
                      base,                         # ImageBase
                      SECT_ALIGN, FILE_ALIGN,
                      6, 0,                         # OS version
                      0, 0,                         # image version
                      5, 1,                         # subsystem version
                      0,                            # Win32VersionValue
                      size_of_image,
                      size_of_headers,
                      0,                            # CheckSum
                      3,                            # console
                      0,                            # DllCharacteristics
                      stack_reserve, 0x1000, 0x1000, 0x10000,
                      0, 16)                        # NumberOfRvaAndSizes
    dd = [(0, 0)] * 16
    dd[1] = (idata_rva, 20 * (n_desc + 1))          # import table
    dd[12] = (0, 0)                                 # IAT lives inside .text thunks
    ddir = b"".join(struct.pack("<II", r, s) for r, s in dd)

    section_table = bytearray()
    section_table += b".text\x00\x00\x00" + struct.pack(
        "<IIIIIIHHI", len(payload), text_rva, text_raw_size, text_raw, 0, 0, 0, 0,
        0x60000020)
    section_table += b".idata\x00\x00" + struct.pack(
        "<IIIIIIHHI", len(idata), idata_rva, idata_raw_size, idata_raw, 0, 0, 0, 0,
        0xC0000040)

    image = bytearray()
    image += headers
    image += b"\x00" * (text_raw - len(image))
    image += payload
    image += b"\x00" * (text_raw_size - len(payload))
    image += idata
    image += b"\x00" * (idata_raw_size - len(idata))

    assert len(image) <= size_of_headers + text_raw_size + idata_raw_size
    # patch PE signature + coff + optional + sections over the header area
    off = 0x40
    image[off:off + 4] = b"PE\x00\x00"
    off += 4
    image[off:off + len(coff)] = coff
    off += len(coff)
    image[off:off + len(opt)] = opt
    off += len(opt)
    image[off:off + len(ddir)] = ddir
    off += len(ddir)
    image[off:off + len(section_table)] = section_table

    open(args.out, "wb").write(image)
    print("mkpe: %s  image=%d bytes base=0x%x entry=0x%x sections=2 imports=%d"
          % (args.out, len(image), base, entry_rva, sum(len(f) for _, f in imports)))


if __name__ == "__main__":
    main()
