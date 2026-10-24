#!/usr/bin/env python3
"""Build a UEFI-bootable ISO 9660 image for YoroOS.

Layout:
  * An embedded FAT16 EFI System Partition (4 MiB) holding EFI/BOOT/BOOTX64.EFI.
    With ~8100 clusters the volume is FAT16 by the Microsoft FAT rules; firmware decides
    the FAT type from the cluster count, so the table must be 16-bit.
  * ISO 9660 with an El Torito boot catalog. The UEFI entry (platform 0xEF) points at
    the ESP image. There is no legacy BIOS entry.
  * The ISO also carries EFIBOOT.IMG and EFI/BOOT/BOOTX64.EFI as plain files.

Usage: python3 tools/mkiso.py <BOOTX64.EFI> <out.iso>
"""
import datetime
import struct
import sys

SECTOR = 2048
ESP_BYTES = 4 * 1024 * 1024
VOLUME_ID = "YOROOS"


# ---------------------------------------------------------------- FAT16 ESP

def build_esp(efi: bytes) -> bytes:
    total_sec = ESP_BYTES // 512
    reserved = 1
    num_fats = 2
    root_entries = 224
    root_sec = root_entries * 32 // 512

    # Find the smallest FAT size that addresses every data cluster (16-bit entries).
    fat_sec = 1
    while True:
        clusters = total_sec - reserved - num_fats * fat_sec - root_sec
        if (clusters + 2) * 2 <= fat_sec * 512:
            break
        fat_sec += 1

    img = bytearray(ESP_BYTES)
    fat = bytearray(fat_sec * 512)
    data_start = reserved + num_fats * fat_sec + root_sec

    def sector_of(cluster: int) -> int:
        return data_start + (cluster - 2)

    def set_fat(n: int, value: int) -> None:
        struct.pack_into("<H", fat, n * 2, value)

    def write_chain(start: int, nclusters: int) -> None:
        for i in range(nclusters):
            n = start + i
            set_fat(n, 0xFFFF if i == nclusters - 1 else n + 1)

    def dirent(name11: bytes, attr: int, cluster: int, size: int) -> bytes:
        e = bytearray(32)
        e[0:11] = name11
        e[11] = attr
        struct.pack_into("<H", e, 26, cluster)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    dot = bytearray(32); dot[0:11] = b".          "; dot[11] = 0x10
    struct.pack_into("<H", dot, 26, 0)
    dotdot_root = bytearray(32); dotdot_root[0:11] = b"..         "; dotdot_root[11] = 0x10

    # Cluster 2: EFI directory, cluster 3: BOOT directory, clusters 4..: BOOTX64.EFI.
    efi_cluster = 4
    efi_clusters = (len(efi) + 511) // 512
    set_fat(0, 0xFFF8)
    set_fat(1, 0xFFFF)
    set_fat(2, 0xFFFF)
    set_fat(3, 0xFFFF)
    write_chain(efi_cluster, efi_clusters)

    # "." of a subdirectory points at itself. ".." of a first-level subdir points at 0 (root).
    def subdir(self_cluster: int, parent_cluster: int, entries: list) -> bytes:
        d = bytearray(512)
        dself = bytearray(dot); struct.pack_into("<H", dself, 26, self_cluster)
        dpar = bytearray(dotdot_root); struct.pack_into("<H", dpar, 26, parent_cluster)
        d[0:32] = dself
        d[32:64] = dpar
        off = 64
        for e in entries:
            d[off:off + 32] = e
            off += 32
        return bytes(d)

    root = bytearray(root_sec * 512)
    root[0:32] = dirent(b"EFI        ", 0x10, 2, 0)
    efi_dir = subdir(2, 0, [dirent(b"BOOT       ", 0x10, 3, 0)])
    boot_dir = subdir(3, 2, [dirent(b"BOOTX64 EFI", 0x20, efi_cluster, len(efi))])

    img[0:512] = boot_sector(total_sec, fat_sec, root_entries)
    for i in range(num_fats):
        base = (reserved + i * fat_sec) * 512
        img[base:base + len(fat)] = fat
    img[(reserved + num_fats * fat_sec) * 512:(reserved + num_fats * fat_sec) * 512 + len(root)] = root
    for cluster, blob in ((2, efi_dir), (3, boot_dir)):
        s = sector_of(cluster) * 512
        img[s:s + 512] = blob
    s = sector_of(efi_cluster) * 512
    img[s:s + len(efi)] = efi
    return bytes(img)


def boot_sector(total_sec: int, fat_sec: int, root_entries: int) -> bytes:
    b = bytearray(512)
    b[0:3] = b"\xEB\x3C\x90"
    b[3:11] = b"YOROOS  "
    struct.pack_into("<H", b, 11, 512)          # bytes per sector
    b[13] = 1                                   # sectors per cluster
    struct.pack_into("<H", b, 14, 1)            # reserved sectors
    b[16] = 2                                   # number of FATs
    struct.pack_into("<H", b, 17, root_entries)
    struct.pack_into("<H", b, 19, total_sec)
    b[21] = 0xF8                                # media descriptor
    struct.pack_into("<H", b, 22, fat_sec)
    struct.pack_into("<H", b, 24, 32)           # sectors per track
    struct.pack_into("<H", b, 26, 64)           # heads
    struct.pack_into("<I", b, 28, 0)            # hidden sectors
    b[36] = 0x80                                # drive number
    b[38] = 0x29                                # extended boot signature
    struct.pack_into("<I", b, 39, 0x59524F53)   # volume serial
    b[43:54] = b"YOROESP    "
    b[54:62] = b"FAT16   "
    b[62:65] = b"\xF4\xEB\xFD"                  # hlt; jmp $-1 (never executed under UEFI)
    b[510:512] = b"\x55\xAA"
    return bytes(b)


# ---------------------------------------------------------------- ISO 9660

def both16(v: int) -> bytes:
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v: int) -> bytes:
    return struct.pack("<I", v) + struct.pack(">I", v)


def dt7(t: datetime.datetime) -> bytes:
    return bytes([t.year - 1900, t.month, t.day, t.hour, t.minute, t.second, 0])


def dir_record(name: bytes, lba: int, size: int, is_dir: bool, t) -> bytes:
    body = bytearray()
    body += b"\x00"                 # length placeholder
    body += b"\x00"                 # extended attribute length
    body += both32(lba)
    body += both32(size)
    body += dt7(t)
    body += bytes([0x02 if is_dir else 0x00])
    body += b"\x00\x00"             # interleave
    body += both16(1)               # volume sequence number
    body += bytes([len(name)])
    body += name
    if len(body) % 2:
        body += b"\x00"
    body[0] = len(body)
    return bytes(body)


def pad_sector(data: bytes) -> bytes:
    rem = (-len(data)) % SECTOR
    return data + b"\x00" * rem


def build_iso(efi: bytes, out_path: str) -> None:
    now = datetime.datetime.now(datetime.timezone.utc)
    esp = build_esp(efi)

    # Sector plan.
    PVD, BRVD, TERM, PATH_L, PATH_M, CATALOG, ROOT, EFI_DIR, BOOT_DIR = range(16, 25)
    ESP_LBA = 25
    esp_sectors = len(esp) // SECTOR
    EFI_LBA = ESP_LBA + esp_sectors
    efi_sectors = (len(efi) + SECTOR - 1) // SECTOR
    total = EFI_LBA + efi_sectors

    # Directory extents (one sector each).
    def dir_block(entries: list, self_lba: int, parent_lba: int) -> bytes:
        blk = bytearray()
        blk += dir_record(b"\x00", self_lba, SECTOR, True, now)
        blk += dir_record(b"\x01", parent_lba, SECTOR, True, now)
        for e in entries:
            blk += e
        return pad_sector(bytes(blk))

    root_entries = [
        dir_record(b"EFIBOOT.IMG;1", ESP_LBA, len(esp), False, now),
        dir_record(b"EFI", EFI_DIR, SECTOR, True, now),
    ]
    root_blk = dir_block(root_entries, ROOT, ROOT)
    efi_blk = dir_block([dir_record(b"BOOT", BOOT_DIR, SECTOR, True, now)], EFI_DIR, ROOT)
    boot_blk = dir_block([dir_record(b"BOOTX64.EFI;1", EFI_LBA, len(efi), False, now)], BOOT_DIR, EFI_DIR)

    root_record = dir_record(b"\x00", ROOT, SECTOR, True, now)  # for the PVD root entry (34 bytes)

    # Path tables: one entry for the root directory.
    def path_entry(extent: int, big_endian: bool) -> bytes:
        e = bytearray()
        e += bytes([1, 0])
        e += (struct.pack(">I", extent) if big_endian else struct.pack("<I", extent))
        e += (struct.pack(">H", 1) if big_endian else struct.pack("<H", 1))
        e += b"\x00"
        if len(e) % 2:
            e += b"\x00"
        return bytes(e)

    path_l = pad_sector(path_entry(ROOT, False))
    path_m = pad_sector(path_entry(ROOT, True))
    path_size = len(path_entry(ROOT, False))

    # Primary volume descriptor.
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"YOROOS".ljust(32, b" ")
    pvd[40:72] = VOLUME_ID.encode().ljust(32, b" ")
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(path_size)
    struct.pack_into("<I", pvd, 140, PATH_L)
    struct.pack_into("<I", pvd, 148, 0)
    struct.pack_into(">I", pvd, 148, PATH_M)
    pvd[156:190] = root_record[:34].ljust(34, b"\x00")
    pvd[190:318] = b" " * 128
    pvd[318:446] = b" " * 128
    pvd[446:574] = b" " * 128
    pvd[574:702] = b" " * 128
    pvd[702:739] = b" " * 37
    pvd[739:776] = b" " * 37
    pvd[776:813] = b" " * 37
    stamp = now.strftime("%Y%m%d%H%M%S00") + "\x00"
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = stamp.encode("ascii")
    pvd[881] = 1

    # Boot record volume descriptor (El Torito).
    brvd = bytearray(SECTOR)
    brvd[0] = 0
    brvd[1:6] = b"CD001"
    brvd[6] = 1
    brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", brvd, 71, CATALOG)

    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    # Boot catalog: validation entry, default entry (no BIOS boot), UEFI section.
    cat = bytearray(SECTOR)
    val = bytearray(32)
    val[0] = 1
    val[1] = 0x00                       # platform of the default entry (x86, unused here)
    val[4:28] = b"YOROOS UEFI".ljust(24, b"\x00")
    val[30] = 0x55
    val[31] = 0xAA
    s = sum(struct.unpack_from("<16H", val, 0)) & 0xFFFF
    struct.pack_into("<H", val, 28, (-s) & 0xFFFF)
    cat[0:32] = val
    default = bytearray(32)            # not bootable: BIOS is not supported
    cat[32:64] = default
    section_header = bytearray(32)
    section_header[0] = 0x91           # final section header
    section_header[1] = 0xEF           # platform: EFI
    struct.pack_into("<H", section_header, 2, 1)
    cat[64:96] = section_header
    entry = bytearray(32)
    entry[0] = 0x88                    # bootable
    entry[1] = 0x00                    # no emulation
    struct.pack_into("<H", entry, 6, len(esp) // 512)
    struct.pack_into("<I", entry, 8, ESP_LBA)
    cat[96:128] = entry

    layout = {
        PVD: bytes(pvd), BRVD: bytes(brvd), TERM: bytes(term),
        PATH_L: path_l, PATH_M: path_m, CATALOG: bytes(cat),
        ROOT: root_blk, EFI_DIR: efi_blk, BOOT_DIR: boot_blk,
    }
    out = bytearray(total * SECTOR)
    for lba, data in layout.items():
        out[lba * SECTOR:lba * SECTOR + len(data)] = data
    out[ESP_LBA * SECTOR:ESP_LBA * SECTOR + len(esp)] = esp
    out[EFI_LBA * SECTOR:EFI_LBA * SECTOR + len(efi)] = efi

    with open(out_path, "wb") as f:
        f.write(out)
    print(f"wrote {out_path}: {len(out)} bytes, ESP {len(esp)} bytes, EFI {len(efi)} bytes")


def main() -> None:
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], "rb") as f:
        efi = f.read()
    build_iso(efi, sys.argv[2])


if __name__ == "__main__":
    main()
