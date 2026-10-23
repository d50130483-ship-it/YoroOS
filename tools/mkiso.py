#!/usr/bin/env python3
"""YoroOS ISO builder - writes a bootable ISO9660 image with an El Torito
1.44MB floppy-emulation boot entry.  No external tools required."""
import sys, os, time, struct

SEC = 2048


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def rec_datetime(t):
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday, t.tm_hour,
                  t.tm_min, t.tm_sec, 0])


def dir_record(name, lba, size, is_dir, t):
    name = name.encode("ascii")
    flags = 2 if is_dir else 0
    body = bytearray()
    body += bytes([0, 0])                       # len, ext attr len
    body += both32(lba)
    body += both32(size)
    body += rec_datetime(t)
    body += bytes([flags, 0, 0])
    body += both16(1)                           # volume sequence number
    body += bytes([len(name)])
    body += name
    if len(body) % 2:
        body += b"\0"
    body[0] = len(body)
    return bytes(body)


def pad_sec(b):
    return b + b"\0" * (SEC - len(b) % SEC) if len(b) % SEC else b


def main():
    img_p, out_p, label = sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else "YOROOS")
    boot_image = open(img_p, "rb").read()
    extra = {}
    for p in sys.argv[4:]:
        extra[os.path.basename(p).upper()] = open(p, "rb").read()

    now = time.gmtime()
    t = time.struct_time(now)

    # layout
    LBA_PVD, LBA_BOOTREC, LBA_TERM = 16, 17, 18
    LBA_CATALOG, LBA_PATHTBL, LBA_ROOT = 19, 20, 21
    LBA_FILES = 22

    def iso_name(name):
        name = name.upper()
        if "." in name:
            base, ext = name.rsplit(".", 1)
        else:
            base, ext = name, ""
        base = "".join(c for c in base if c.isalnum() or c in "_-")[:8] or "FILE"
        ext = "".join(c for c in ext if c.isalnum())[:3]
        return base + ("." + ext if ext else "") + ";1"

    files = []            # (isoname, lba, size, data)
    cursor = LBA_FILES
    files.append(("YOROOS.IMG;1", cursor, len(boot_image), boot_image))
    cursor += (len(boot_image) + SEC - 1) // SEC
    for name, data in sorted(extra.items()):
        files.append((iso_name(name), cursor, len(data), data))
        cursor += (len(data) + SEC - 1) // SEC
    total_sectors = cursor

    # ---- root directory ----
    root = bytearray()
    root += dir_record("\0", LBA_ROOT, SEC, True, t)
    root += dir_record("\1", LBA_ROOT, SEC, True, t)
    for name, lba, size, _ in files:
        root += dir_record(name, lba, size, False, t)
    assert len(root) <= SEC, "root directory too large"
    root = root + b"\0" * (SEC - len(root))

    # ---- path table ----
    pt = bytearray()
    pt += bytes([1, 0]) + struct.pack("<I", LBA_ROOT) + struct.pack("<H", 1) + b"\0" + b"\0"
    pt = pt + b"\0" * (SEC - len(pt))

    # ---- primary volume descriptor ----
    pvd = bytearray(SEC)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"YOROOS".ljust(32)
    pvd[40:72] = label.upper().encode("ascii", "replace").ljust(32)
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SEC)
    pvd[132:140] = both32(len(pt))
    pvd[140:144] = struct.pack("<I", LBA_PATHTBL)
    pvd[148:152] = struct.pack(">I", LBA_PATHTBL)
    rr = dir_record("\0", LBA_ROOT, SEC, True, t)
    pvd[156:156 + len(rr)] = rr
    pvd[190:318] = b" " * 128
    pvd[318:446] = b"YOROOS PROJECT".ljust(128)
    pvd[446:574] = b"YOROOS PROJECT".ljust(128)
    pvd[574:702] = b"YOROOS 1.0".ljust(128)
    pvd[702:739] = b" " * 37
    pvd[739:776] = b" " * 37
    pvd[776:813] = b" " * 37
    stamp = "%04d%02d%02d%02d%02d%02d00" % (t.tm_year, t.tm_mon, t.tm_mday,
                                            t.tm_hour, t.tm_min, t.tm_sec)
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = stamp.encode() + b"\0"
    pvd[881] = 1

    # ---- el torito boot record ----
    br = bytearray(SEC)
    br[0] = 0
    br[1:6] = b"CD001"
    br[6] = 1
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    br[71:75] = struct.pack("<I", LBA_CATALOG)

    # ---- boot catalog ----
    cat = bytearray(SEC)
    cat[0] = 1                    # validation entry
    cat[1] = 0                    # x86
    cat[4:28] = b"YOROOS BOOT".ljust(24)
    cat[30] = 0x55
    cat[31] = 0xAA
    cat[32] = 0x88
    cat[33] = 2                   # 1.44MB floppy emulation
    cat[38:40] = struct.pack("<H", 1)
    cat[40:44] = struct.pack("<I", LBA_FILES)   # boot image LBA
    s = 0
    for i in range(0, 32, 2):
        s = (s + struct.unpack("<H", cat[i:i + 2])[0]) & 0xFFFF
    cat[28:30] = struct.pack("<H", (0x10000 - s) & 0xFFFF)

    # ---- terminator ----
    term = bytearray(SEC)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    out = bytearray(SEC * 16)          # system area
    out += pvd
    out += br
    out += term
    out += cat
    out += pt
    out += root
    assert len(out) == LBA_FILES * SEC

    for name, lba, size, data in files:
        assert len(out) == lba * SEC, (name, len(out), lba)
        out += data + b"\0" * (((len(data) + SEC - 1) // SEC) * SEC - len(data))

    assert len(out) == total_sectors * SEC, (len(out), total_sectors * SEC)
    open(out_p, "wb").write(out)
    print("iso: %s (%d KiB, %d sectors)" % (out_p, len(out) // 1024, total_sectors))


main()
