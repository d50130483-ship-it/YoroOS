#!/usr/bin/env python3
"""Сборщик загрузочного ISO YoroOS (ISO9660 + El Torito, эмуляция жёсткого диска).

BIOS видит CD как диск, содержимое которого = вложенный образ (boot+loader+kernel),
загружает его MBR-сектор и дальше наш загрузчик читает ядро через int 13h AH=42h.

Использование: mkiso.py boot.bin loader.bin kernel.bin out.iso [размер_вложенного_образа_МБ]
"""
MKISO_VERSION = "v5-pathtable-fix"
import struct
import sys
import datetime

BLOCK = 2048
SYSTEM_AREA_BLOCKS = 16
PVD_BLOCK = 16
BRVD_BLOCK = 17
TERM_BLOCK = 18
ROOTDIR_BLOCK = 19
PATHTBL_L_BLOCK = 20
PATHTBL_M_BLOCK = 21
CATALOG_BLOCK = 22
IMG_BLOCK = 23

A_FN = "YOROOS.IMG"


def be32(v):
    return struct.pack(">I", v)


def le32(v):
    return struct.pack("<I", v)


def both32(v):
    return le32(v) + be32(v)


def be16(v):
    return struct.pack(">H", v)


def le16(v):
    return struct.pack("<H", v)


def both16(v):
    return le16(v) + be16(v)


def sp(s, n):
    return s.ljust(n)[:n].encode("ascii")


def dir_record(name: bytes, extent: int, size: int, flags: int, when: bytes) -> bytes:
    nmlen = len(name)
    rec_len = 33 + nmlen + (1 if nmlen % 2 == 0 else 0)
    r = bytearray(rec_len)
    r[0] = rec_len
    r[1] = 0                       # ext attr len
    r[2:10] = both32(extent)
    r[10:18] = both32(size)
    r[18:25] = when                # дата/время
    r[25] = flags                  # 0=файл, 2=каталог
    r[26] = 0                      # file unit size
    r[27] = 0                      # interleave gap
    r[28:32] = both16(1)           # номер тома (both-endian u16, 4 байта)
    r[32] = nmlen
    r[33:33 + nmlen] = name
    return bytes(r)


def root_record(flags: int, name_byte: int, when: bytes) -> bytes:
    return dir_record(bytes([name_byte]), ROOTDIR_BLOCK, BLOCK, flags, when)


def pvd(total_blocks: int, ptsize: int, when: bytes) -> bytes:
    b = bytearray(BLOCK)
    b[0] = 1
    b[1:6] = b"CD001"
    b[6] = 1
    b[7] = 0                       # флаги: без записей
    b[8:40] = sp("YOROOS", 32)     # system id
    b[40:72] = sp("YOROOS", 32)    # volume id
    b[80:88] = both32(total_blocks)
    b[120:124] = both16(1)         # volume set size
    b[124:128] = both16(1)         # volume sequence
    b[128:130] = le16(BLOCK)       # размер блока (both-endian u16)
    b[130:132] = be16(BLOCK)
    b[132:140] = both32(ptsize)    # размер path table
    b[140:144] = le32(PATHTBL_L_BLOCK)
    b[144:148] = le32(0)           # optional L
    b[148:152] = be32(PATHTBL_M_BLOCK)
    b[152:156] = be32(0)           # optional M
    b[156:190] = root_record(2, 0, when)
    for off in (190, 318, 446):    # volume set / publisher / preparer
        b[off:off + 128] = sp(" ", 128)
    b[574:611] = sp(" ", 37)
    b[611:648] = sp(" ", 37)
    b[648:685] = sp(" ", 37)
    def date17():
        # формат ISO9660: 16 ASCII-цифр ГГГГММДДЧЧММССFF + нулевое смещение
        return b"%04d%02d%02d%02d%02d%02d00\x00" % (
            when[0] + 1900, when[1], when[2], when[3], when[4], when[5])
    b[813 - 74:813 - 57] = date17()  # 739: creation
    b[813 - 37:813 - 20] = date17()  # 776: modification
    b[813:830] = date17()            # 813: expiry
    b[830:847] = date17()            # 830: effective
    b[881] = 1                       # file structure version
    return bytes(b)


def boot_record_vd(catalog_block: int) -> bytes:
    b = bytearray(BLOCK)
    b[0] = 0
    b[1:6] = b"CD001"
    b[6] = 1
    b[7:39] = sp("EL TORITO SPECIFICATION", 32)
    b[39:71] = b"\x00" * 32
    b[71:75] = le32(catalog_block)
    return bytes(b)


def vd_terminator() -> bytes:
    b = bytearray(BLOCK)
    b[0] = 255
    b[1:6] = b"CD001"
    b[6] = 1
    return bytes(b)


def path_table(m: int, root_extent: int) -> bytes:
    # одна запись: корень
    rec = bytearray(10)            # 8 байт заголовка + имя (1) + паддинг (1)
    rec[0] = 8                     # длина заголовка записи
    rec[1] = 0
    rec[2:6] = (be32(root_extent) if m else le32(root_extent))
    rec[6:8] = struct.pack(">H" if m else "<H", 1)   # номер родителя
    rec[8] = 1                     # длина имени
    rec[9] = 0                     # имя корня: \x00
    return bytes(rec)


def boot_catalog(img_blocks: int, img_extent: int) -> bytes:
    b = bytearray(BLOCK)
    # --- validation entry ---
    v = bytearray(32)
    v[0] = 0x01                    # header id
    v[1] = 0x00                    # платформа x86
    v[4:28] = sp("YOROOS CD BOOT", 24)
    v[30:32] = b"\x55\xaa"         # ключ (учитывается в сумме!)
    s = 0
    for i in range(0, 32, 2):
        s += struct.unpack_from("<H", v, i)[0]
    v[28:30] = le16((-s) & 0xFFFF)
    b[0:32] = v
    # --- default entry: эмуляция жёсткого диска ---
    sectors = min(img_blocks * (BLOCK // 512), 0xFFFF)
    d = bytearray(32)
    d[0] = 0x88                    # bootable
    d[1] = 0x04                    # media: hard disk emulation
    d[2:4] = le16(0)               # load segment (для эмуляции BIOS сам грузит на 0x7C00)
    d[4] = 0x06                    # system type
    d[5] = 0
    d[6:8] = le16(sectors)         # размер эмулируемого диска (вирт. секторы 512Б)
    d[8:12] = le32(img_extent)     # где лежит образ
    d[12:24] = b"\x00" * 12
    d[24:26] = le16(sectors)       # дублируем размер (некоторые BIOS читают отсюда)
    b[32:64] = d
    return bytes(b)


def build(boot_bin, loader_bin, kernel_bin, out_iso, img_mb=4):
    when = bytearray(7)
    now = datetime.datetime.now(datetime.timezone.utc)
    when[0] = now.year - 1900
    when[1] = now.month
    when[2] = now.day
    when[3] = now.hour
    when[4] = now.minute
    when[5] = now.second
    when[6] = 0
    when = bytes(when)

    # вложенный диск: тот же layout, что у yoroos.img (boot@0, loader@1, kernel@9)
    disk = bytearray(img_mb * 1024 * 1024)
    disk[0:512] = boot_bin
    disk[512:512 + len(loader_bin)] = loader_bin
    disk[9 * 512:9 * 512 + len(kernel_bin)] = kernel_bin
    assert disk[510:512] == b"\x55\xaa", "нет сигнатуры MBR"

    img_blocks = (len(disk) + BLOCK - 1) // BLOCK
    sectors = min(img_blocks * (BLOCK // 512), 0xFFFF)
    total = IMG_BLOCK + img_blocks

    rootdir = bytearray(BLOCK)
    recs = [
        root_record(2, 0, when),                                  # .
        root_record(2, 1, when),                                  # ..
        dir_record(b"BOOT.CAT;1", CATALOG_BLOCK, BLOCK, 0, when),
        dir_record(A_FN.encode(), IMG_BLOCK, len(disk), 0, when),
    ]
    pos = 0
    for r in recs:
        rootdir[pos:pos + len(r)] = r
        pos += len(r)

    pt = path_table(False, ROOTDIR_BLOCK)
    ptm = path_table(True, ROOTDIR_BLOCK)

    out = bytearray(total * BLOCK)
    out[CATALOG_BLOCK * BLOCK:CATALOG_BLOCK * BLOCK + BLOCK] = boot_catalog(img_blocks, IMG_BLOCK)
    out[PVD_BLOCK * BLOCK:PVD_BLOCK * BLOCK + BLOCK] = pvd(total, len(pt), when)
    out[BRVD_BLOCK * BLOCK:BRVD_BLOCK * BLOCK + BLOCK] = boot_record_vd(CATALOG_BLOCK)
    out[TERM_BLOCK * BLOCK:TERM_BLOCK * BLOCK + BLOCK] = vd_terminator()
    out[ROOTDIR_BLOCK * BLOCK:ROOTDIR_BLOCK * BLOCK + BLOCK] = bytes(rootdir)
    out[PATHTBL_L_BLOCK * BLOCK:PATHTBL_L_BLOCK * BLOCK + len(pt)] = pt
    out[PATHTBL_M_BLOCK * BLOCK:PATHTBL_M_BLOCK * BLOCK + len(ptm)] = ptm
    out[IMG_BLOCK * BLOCK:IMG_BLOCK * BLOCK + len(disk)] = disk
    open(out_iso, "wb").write(out)
    print(f"yoroos.iso: {len(out)} байт ({total} блоков), образ внутри: {len(disk)} байт, {sectors} вирт. секторов")
    return out


def self_check(d):
    """Проверим ISO по байтам в памяти (перечитывание файла в песочнице ненадёжно)."""
    assert d[0x8001:0x8006] == b"CD001" and d[0x8000] == 1, "PVD сломан"
    assert d[0x8801:0x8806] == b"CD001" and d[0x8800] == 0, "Boot Record VD сломан"
    assert d[0x8807:0x881E].startswith(b"EL TORITO"), "нет сигнатуры El Torito"
    cat = struct.unpack_from("<I", d, 0x8847)[0]
    assert d[0x9001:0x9006] == b"CD001" and d[0x9000] == 255, "терминатор сломан"
    # каталог
    coff = cat * BLOCK
    v = d[coff:coff + 32]
    s = sum(struct.unpack_from("<H", v, i)[0] for i in range(0, 32, 2)) & 0xFFFF
    assert s == 0, f"контрольная сумма validation entry: {s}"
    assert v[30:32] == b"\x55\xaa", "нет ключа 0x55AA"
    e = d[coff + 32:coff + 64]
    assert e[0] == 0x88 and e[1] == 0x04, "default entry не bootable HD"
    img_ext = struct.unpack_from("<I", e, 8)[0]
    # образ на месте и содержит MBR + ядро
    off = img_ext * BLOCK
    assert d[off + 510:off + 512] == b"\x55\xaa", "в образе нет MBR-сигнатуры"
    assert d[off + 512:off + 514] != b"\x00\x00", "лоадер пуст"
    print("самопроверка: PVD OK, El Torito OK, контрольная сумма OK, образ на месте")


if __name__ == "__main__":
    boot, loader, kernel, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    mb = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    img = build(open(boot, "rb").read(), open(loader, "rb").read(), open(kernel, "rb").read(), out, mb)
    self_check(img)
