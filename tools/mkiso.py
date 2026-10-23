#!/usr/bin/env python3
"""YoroOS 0.2 «Aurora» True Quad-Boot Hybrid ISO9660 + El Torito (BIOS + UEFI) + GPT/ESP Builder.

Simultaneously satisfies ALL 4 boot paths in ONE standalone .iso file:
  1. UEFI El Torito (Platform ID 0xEF, Media 0 No-Emulation) -> canonical 8 MiB FAT16 ESP
     containing \\EFI\\BOOT\\BOOTX64.EFI and \\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI
     (Microsoft Hyper-V Generation 2 SCSI DVD, VMware EFI, VirtualBox EFI, QEMU OVMF).
  2. UEFI GPT Hybrid Disk (valid CRC32 Primary + Backup GPT at 512B LBA 1..33 & end-33..end-1)
     pointing Partition 1 (C12A7328-F81F-11D2-BA4B-00A0C93EC93B) directly at the embedded
     8 MiB FAT16 ESP (USB flash drives / Hyper-V / OVMF / hardware UEFI).
  3. Direct ISO9660 \\EFI\\BOOT\\BOOTX64.EFI directory hierarchy inside ISO9660 itself.
  4. Legacy BIOS x86 El Torito No-Emulation (ISO LBA 22 = boot.bin, 23 = stage2.bin, 54 = kernel32.bin)
     + Hybrid MBR boot code at LBA 0 (Hyper-V Gen 1, VirtualBox, QEMU i386/x86_64, v86).
  5. Embedded "YOROISO2" installer manifest at 512B LBA 36 so the YoroOS Windowed Installer
     writes the exact EFI System Partition + BIOS bootloader onto the target GPT VHDX/disk!
"""
import os
import sys
import time
import zlib
import uuid
import struct

SEC2048 = 2048
SEC512  = 512


def both16(v: int) -> bytes:
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v: int) -> bytes:
    return struct.pack("<I", v) + struct.pack(">I", v)


def rec_datetime(t) -> bytes:
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0])


def dir_record(name: str, lba: int, size: int, is_dir: bool, t) -> bytes:
    raw = name.encode("ascii")
    flags = 2 if is_dir else 0
    body = bytearray()
    body += bytes([0, 0])
    body += both32(lba)
    body += both32(size)
    body += rec_datetime(t)
    body += bytes([flags, 0, 0])
    body += both16(1)
    body += bytes([len(raw)])
    body += raw
    if len(body) % 2:
        body += b"\0"
    body[0] = len(body)
    return bytes(body)


def fat_dirent(name83: bytes, attr: int, cluster: int, size: int) -> bytes:
    assert len(name83) == 11
    out = bytearray(32)
    out[0:11] = name83
    out[11] = attr
    # Valid FAT date/time: 2026-10-23 12:00:00
    fdate = ((2026 - 1980) << 9) | (10 << 5) | 23
    ftime = (12 << 11) | (0 << 5) | 0
    struct.pack_into("<H", out, 14, ftime)
    struct.pack_into("<H", out, 16, fdate)
    struct.pack_into("<H", out, 18, fdate)
    struct.pack_into("<H", out, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", out, 22, ftime)
    struct.pack_into("<H", out, 24, fdate)
    struct.pack_into("<H", out, 26, cluster & 0xFFFF)
    struct.pack_into("<I", out, 28, size)
    return bytes(out)


def build_fat16_esp(efi_bytes: bytes) -> bytes:
    """Build a 100% Microsoft-canonical 8 MiB (16,384 x 512B = 8,142 clusters) FAT16 ESP image
    containing both \\EFI\\BOOT\\BOOTX64.EFI and \\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI."""
    total_sec   = 16384          # 8 MiB exact
    spc         = 2              # 1024 bytes per cluster
    rsvd_sec    = 4
    num_fats    = 2
    root_ents   = 512            # 32 sectors
    fat_secs    = 32             # 8,192 16-bit entries -> covers all 8,142 clusters cleanly!
    root_sec0   = rsvd_sec + num_fats * fat_secs          # sector 68
    data_sec0   = root_sec0 + (root_ents * 32) // SEC512  # sector 100

    img = bytearray(total_sec * SEC512)

    # Boot sector / FAT16 BPB
    img[0:3]   = b"\xEB\x3C\x90"
    img[3:11]  = b"MSWIN4.1"
    struct.pack_into("<H", img, 11, SEC512)
    img[13]    = spc
    struct.pack_into("<H", img, 14, rsvd_sec)
    img[16]    = num_fats
    struct.pack_into("<H", img, 17, root_ents)
    struct.pack_into("<H", img, 19, total_sec)
    img[21]    = 0xF8
    struct.pack_into("<H", img, 22, fat_secs)
    struct.pack_into("<H", img, 24, 63)
    struct.pack_into("<H", img, 26, 255)
    struct.pack_into("<I", img, 28, 0)
    struct.pack_into("<I", img, 32, 0)
    img[36]    = 0x80
    img[38]    = 0x29
    struct.pack_into("<I", img, 39, 0x594F524F)
    img[43:54] = b"YOROOS_ESP "
    img[54:62] = b"FAT16   "
    img[510]   = 0x55
    img[511]   = 0xAA

    fat = [0] * (fat_secs * (SEC512 // 2))
    fat[0] = 0xFFF8
    fat[1] = 0xFFFF
    next_clus = [2]

    def clus_off(c: int) -> int:
        return (data_sec0 + (c - 2) * spc) * SEC512

    def alloc_chain(data: bytes) -> int:
        clus_sz = spc * SEC512
        n = max(1, (len(data) + clus_sz - 1) // clus_sz)
        first = next_clus[0]
        next_clus[0] += n
        for i in range(n):
            c = first + i
            fat[c] = 0xFFFF if i == n - 1 else (c + 1)
            chunk = data[i * clus_sz:(i + 1) * clus_sz]
            off = clus_off(c)
            img[off:off + len(chunk)] = chunk
        return first

    # Allocate clusters for \EFI, \EFI\BOOT, \EFI\MICROSOFT, \EFI\MICROSOFT\BOOT, and both EFI binaries
    c_efi    = alloc_chain(b"")
    c_boot   = alloc_chain(b"")
    c_ms     = alloc_chain(b"")
    c_msboot = alloc_chain(b"")
    c_x64    = alloc_chain(efi_bytes)
    c_mgfw   = alloc_chain(efi_bytes)

    # Root directory: volume label + EFI directory
    root_off = root_sec0 * SEC512
    img[root_off + 0:root_off + 32]  = fat_dirent(b"YOROOS_ESP ", 0x08, 0, 0)
    img[root_off + 32:root_off + 64] = fat_dirent(b"EFI        ", 0x10, c_efi, 0)

    # \EFI directory: ., .., BOOT, MICROSOF
    o = clus_off(c_efi)
    img[o + 0:o + 32]   = fat_dirent(b".          ", 0x10, c_efi, 0)
    img[o + 32:o + 64]  = fat_dirent(b"..         ", 0x10, 0, 0)
    img[o + 64:o + 96]  = fat_dirent(b"BOOT       ", 0x10, c_boot, 0)
    img[o + 96:o + 128] = fat_dirent(b"MICROSOFT"[:8].ljust(8) + b"   ", 0x10, c_ms, 0)
    # Wait: "MICROSOFT" is 9 chars! Look how crucial checking 8.3 vs LFN is in EDK2 Fat.efi!!
    # Let's emit BOTH a genuine VFAT Long File Name entry for "Microsoft" AND standard 8.3!
    ms_lfn = bytearray(32)
    name_utf16 = [ord(ch) for ch in "Microsoft"] + [0x0000] + [0xFFFF] * 3
    ms_short = b"MICROSOF   "
    chk = 0
    for b in ms_short:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
    ms_lfn[0] = 0x41  # last LFN entry, ordinal 1
    for idx, ch in enumerate(name_utf16[:5]):
        struct.pack_into("<H", ms_lfn, 1 + idx * 2, ch)
    ms_lfn[11] = 0x0F  # ATTR_LONG_NAME
    ms_lfn[12] = 0
    ms_lfn[13] = chk
    for idx, ch in enumerate(name_utf16[5:11]):
        struct.pack_into("<H", ms_lfn, 14 + idx * 2, ch)
    struct.pack_into("<H", ms_lfn, 26, 0)
    for idx, ch in enumerate(name_utf16[11:13]):
        struct.pack_into("<H", ms_lfn, 28 + idx * 2, ch)
    img[o + 96:o + 128]  = bytes(ms_lfn)
    img[o + 128:o + 160] = fat_dirent(ms_short, 0x10, c_ms, 0)

    # \EFI\BOOT directory: ., .., BOOTX64.EFI
    o = clus_off(c_boot)
    img[o + 0:o + 32]  = fat_dirent(b".          ", 0x10, c_boot, 0)
    img[o + 32:o + 64] = fat_dirent(b"..         ", 0x10, c_efi, 0)
    img[o + 64:o + 96] = fat_dirent(b"BOOTX64 EFI", 0x20, c_x64, len(efi_bytes))

    # \EFI\Microsoft directory: ., .., Boot
    o = clus_off(c_ms)
    img[o + 0:o + 32]  = fat_dirent(b".          ", 0x10, c_ms, 0)
    img[o + 32:o + 64] = fat_dirent(b"..         ", 0x10, c_efi, 0)
    img[o + 64:o + 96] = fat_dirent(b"BOOT       ", 0x10, c_msboot, 0)

    # \EFI\Microsoft\Boot directory: ., .., bootmgfw.efi
    o = clus_off(c_msboot)
    img[o + 0:o + 32]  = fat_dirent(b".          ", 0x10, c_msboot, 0)
    img[o + 32:o + 64] = fat_dirent(b"..         ", 0x10, c_ms, 0)
    img[o + 64:o + 96] = fat_dirent(b"BOOTMGFWEFI", 0x20, c_mgfw, len(efi_bytes))

    # Write both FAT copies
    fat_bytes = b"".join(struct.pack("<H", v) for v in fat)
    for f in range(num_fats):
        foff = (rsvd_sec + f * fat_secs) * SEC512
        img[foff:foff + len(fat_bytes)] = fat_bytes

    return bytes(img)


def write_gpt_and_hybrid_mbr(iso: bytearray, boot_bin: bytes, esp_lba512: int, esp_sec512: int):
    """Write Hybrid MBR at LBA 0 + CRC32-verified Primary & Backup GPT tables around the ISO."""
    total_lba = len(iso) // SEC512

    # 1. LBA 0: BIOS boot.bin code + Protective GPT MBR entry (0xEE)
    mbr = bytearray(boot_bin[:512])
    struct.pack_into("<I", mbr, 0x1B8, 0x594F5232)
    mbr[0x1BE:0x1FE] = b"\x00" * 64
    # Partition 1: Standard Protective GPT entry (0xEE) covering entire image (UEFI 2.10 Sec 5.2.3)
    mbr[0x1BE + 0] = 0x00
    mbr[0x1BE + 1:0x1BE + 4] = bytes([0x00, 0x02, 0x00])
    mbr[0x1BE + 4] = 0xEE
    mbr[0x1BE + 5:0x1BE + 8] = bytes([0xFE, 0xFF, 0xFF])
    struct.pack_into("<I", mbr, 0x1BE + 8, 1)
    struct.pack_into("<I", mbr, 0x1BE + 12, total_lba - 1)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    iso[0:512] = mbr

    # 2. GPT Partition Array (128 x 128B = 16,384B = 32 sectors)
    ents = bytearray(128 * 128)
    esp_type_guid = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").bytes_le
    esp_uniq_guid = uuid.UUID("594f524f-0002-4000-8000-000000000001").bytes_le
    ents[0:16]   = esp_type_guid
    ents[16:32]  = esp_uniq_guid
    struct.pack_into("<Q", ents, 32, esp_lba512)
    struct.pack_into("<Q", ents, 40, esp_lba512 + esp_sec512 - 1)
    struct.pack_into("<Q", ents, 48, 0)
    name_utf16 = "YoroOS EFI System Partition".encode("utf-16-le")[:72]
    ents[56:56 + len(name_utf16)] = name_utf16
    ents_crc = zlib.crc32(ents) & 0xFFFFFFFF

    disk_guid = uuid.UUID("594f524f-0002-4000-9000-202610230002").bytes_le

    def make_hdr(my_lba: int, alt_lba: int, part_lba: int) -> bytes:
        h = bytearray(SEC512)
        h[0:8] = b"EFI PART"
        struct.pack_into("<I", h, 8,  0x00010000)
        struct.pack_into("<I", h, 12, 92)
        struct.pack_into("<I", h, 16, 0)
        struct.pack_into("<Q", h, 24, my_lba)
        struct.pack_into("<Q", h, 32, alt_lba)
        struct.pack_into("<Q", h, 40, 34)
        struct.pack_into("<Q", h, 48, total_lba - 34)
        h[56:72] = disk_guid
        struct.pack_into("<Q", h, 72, part_lba)
        struct.pack_into("<I", h, 80, 128)
        struct.pack_into("<I", h, 84, 128)
        struct.pack_into("<I", h, 88, ents_crc)
        hcrc = zlib.crc32(bytes(h[:92])) & 0xFFFFFFFF
        struct.pack_into("<I", h, 16, hcrc)
        return bytes(h)

    # Primary GPT at LBA 1 + LBA 2..33
    iso[512:1024]        = make_hdr(1, total_lba - 1, 2)
    iso[1024:1024 + len(ents)] = ents

    # Backup GPT at LBA total_lba-33..total_lba-2 + LBA total_lba-1
    bpart_off = (total_lba - 33) * SEC512
    bhdr_off  = (total_lba - 1) * SEC512
    iso[bpart_off:bpart_off + len(ents)] = ents
    iso[bhdr_off:bhdr_off + 512] = make_hdr(total_lba - 1, 1, total_lba - 33)


def main():
    # Usage: mkiso.py <bios_yoroos.img> <BOOTX64.EFI> <boot.bin> <kernel32.bin> <out.iso> [LABEL] [extra files...]
    bios_img_p, efi_p, boot_bin_p, k32_p, out_p = sys.argv[1:6]
    label = sys.argv[6] if len(sys.argv) > 6 else "YOROOS_0_2"
    extra_paths = sys.argv[7:]

    bios_img  = open(bios_img_p, "rb").read()
    efi_bytes = open(efi_p, "rb").read()
    boot_bin  = open(boot_bin_p, "rb").read()
    k32_bytes = open(k32_p, "rb").read()
    esp_bytes = build_fat16_esp(efi_bytes)

    # Also save build/efiboot.img for inspection
    os.makedirs("build", exist_ok=True)
    open("build/efiboot.img", "wb").write(esp_bytes)

    t = time.struct_time((2026, 10, 23, 12, 0, 0, 4, 296, 0))

    # Fixed ISO9660 layout so boot/boot.asm's CD No-Emulation offsets match to the exact sector!
    #   ISO LBA 16 = Primary Volume Descriptor
    #   ISO LBA 17 = El Torito Boot Record Volume Descriptor
    #   ISO LBA 18 = Volume Descriptor Set Terminator
    #   ISO LBA 19 = Dual BIOS + UEFI El Torito Boot Catalog
    #   ISO LBA 20 = Type-L Path Table
    #   ISO LBA 21 = Root Directory (\)
    #   ISO LBA 22..N = YOROOS.IMG (MUST start at ISO LBA 22 so boot=22, stage2=23, kernel32=54!)
    LBA_PVD, LBA_BOOTREC, LBA_TERM = 16, 17, 18
    LBA_CATALOG, LBA_PATHTBL, LBA_ROOT = 19, 20, 21
    LBA_YOROOS_IMG = 22

    cursor = LBA_YOROOS_IMG
    yoroos_lba = cursor
    cursor += (len(bios_img) + SEC2048 - 1) // SEC2048

    # Directories \EFI and \EFI\BOOT inside ISO9660 too!
    lba_dir_efi  = cursor; cursor += 1
    lba_dir_boot = cursor; cursor += 1

    # Direct BOOTX64.EFI file inside ISO9660 \EFI\BOOT\BOOTX64.EFI
    lba_efi_file = cursor
    cursor += (len(efi_bytes) + SEC2048 - 1) // SEC2048

    # Canonical 8 MiB FAT16 EFI System Partition image (aligned on 2048B = 4x512B boundary!)
    lba_esp = cursor
    cursor += len(esp_bytes) // SEC2048

    extras = []
    for p in extra_paths:
        if os.path.isfile(p):
            raw = open(p, "rb").read()
            nm = os.path.basename(p).upper()
            base, _, ext = nm.partition(".")
            base = "".join(c for c in base if c.isalnum() or c == "_")[:8] or "README"
            ext = "".join(c for c in ext if c.isalnum())[:3]
            isonm = f"{base}.{ext};1" if ext else f"{base};1"
            extras.append((isonm, cursor, len(raw), raw))
            cursor += (len(raw) + SEC2048 - 1) // SEC2048

    # Reserve 9 x 2048B sectors (36 x 512B sectors) at the end for Backup GPT array + header!
    total_iso_sectors = cursor + 9

    # Build \EFI and \EFI\BOOT ISO9660 directories
    dir_efi = bytearray()
    dir_efi += dir_record("\0", lba_dir_efi, SEC2048, True, t)
    dir_efi += dir_record("\1", LBA_ROOT, SEC2048, True, t)
    dir_efi += dir_record("BOOT", lba_dir_boot, SEC2048, True, t)
    dir_efi += b"\0" * (SEC2048 - len(dir_efi))

    dir_boot = bytearray()
    dir_boot += dir_record("\0", lba_dir_boot, SEC2048, True, t)
    dir_boot += dir_record("\1", lba_dir_efi, SEC2048, True, t)
    dir_boot += dir_record("BOOTX64.EFI;1", lba_efi_file, len(efi_bytes), False, t)
    dir_boot += b"\0" * (SEC2048 - len(dir_boot))

    # Build Root ISO9660 directory
    root = bytearray()
    root += dir_record("\0", LBA_ROOT, SEC2048, True, t)
    root += dir_record("\1", LBA_ROOT, SEC2048, True, t)
    root += dir_record("EFI", lba_dir_efi, SEC2048, True, t)
    root += dir_record("EFIBOOT.IMG;1", lba_esp, len(esp_bytes), False, t)
    root += dir_record("BOOTX64.EFI;1", lba_efi_file, len(efi_bytes), False, t)
    root += dir_record("YOROOS.IMG;1", yoroos_lba, len(bios_img), False, t)
    for isonm, flba, fsz, _ in extras:
        root += dir_record(isonm, flba, fsz, False, t)
    assert len(root) <= SEC2048
    root += b"\0" * (SEC2048 - len(root))

    # Path Table (Root=1, EFI=2, EFI/BOOT=3)
    pt = bytearray()
    pt += bytes([1, 0]) + struct.pack("<I", LBA_ROOT) + struct.pack("<H", 1) + b"\0\0"
    pt += bytes([3, 0]) + struct.pack("<I", lba_dir_efi) + struct.pack("<H", 1) + b"EFI\0"
    pt += bytes([4, 0]) + struct.pack("<I", lba_dir_boot) + struct.pack("<H", 2) + b"BOOT"
    pt_len = len(pt)
    pt += b"\0" * (SEC2048 - len(pt))

    # Primary Volume Descriptor (LBA 16)
    pvd = bytearray(SEC2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"YOROOS 0.2 AURORA".ljust(32)
    pvd[40:72] = label.upper().encode("ascii", "replace")[:32].ljust(32)
    pvd[80:88] = both32(total_iso_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SEC2048)
    pvd[132:140] = both32(pt_len)
    struct.pack_into("<I", pvd, 140, LBA_PATHTBL)
    rs = dir_record("\0", LBA_ROOT, SEC2048, True, t)
    pvd[156:156 + len(rs)] = rs
    pvd[318:446] = b"D50130483-SHIP-IT / YOROOS PROJECT".ljust(128)
    pvd[446:574] = b"YOROOS MKISO HYBRID BUILDER".ljust(128)
    pvd[574:702] = b"YOROOS 0.2 PRE-RELEASE (UEFI GEN2 + BIOS)".ljust(128)
    stamp = b"2026102312000000\0"
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = stamp
    pvd[881] = 1

    # El Torito Boot Record (LBA 17)
    br = bytearray(SEC2048)
    br[0] = 0
    br[1:6] = b"CD001"
    br[6] = 1
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    struct.pack_into("<I", br, 71, LBA_CATALOG)

    # Volume Descriptor Terminator (LBA 18)
    term = bytearray(SEC2048)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    # Dual BIOS + UEFI El Torito Boot Catalog (LBA 19)
    #   [0..31]   Validation Entry (Platform 0x00 = x86)
    #   [32..63]  Initial/Default Entry: BIOS No-Emulation at ISO LBA 22 (4 x 512B virtual sectors)
    #   [64..95]  Section Header Entry: HeaderID 0x91 (Final Section), PlatformID 0xEF (UEFI), 1 entry
    #   [96..127] Section Entry: Bootable 0x88, Media 0 (No-Emulation), SectorCount = 0 (or 1), LoadRBA = lba_esp
    #             NOTE ON HYPER-V GEN 2 EDK2 PartitionDxe ElTorito.c:
    #             Look up exact EDK2 PartitionInstallElToritoChildHandles behavior!
    #             In EDK2 MdeModulePkg/Universal/Disk/PartitionDxe/ElTorito.c:
    #               if (Catalog->Boot.SectorCount == 0 || Catalog->Boot.SectorCount == 1) ->
    #               EDK2 reads the FAT BPB inside LoadRBA to get the FULL 16,384-sector FAT16 size!
    #               Whereas if SectorCount > 1 (e.g. 4), EDK2 sets LastBlock = 4 sectors = 2 KiB and TRUNCATES THE ESP!!
    #             SETTING SectorCount = 0 (OR 1) IS THE FAMOUS EDK2 / HYPER-V GEN 2 SECRET!!
    cat = bytearray(SEC2048)
    cat[0] = 0x01
    cat[1] = 0x00
    cat[4:28] = b"YOROOS 0.2 HYBRID".ljust(24)
    cat[30] = 0x55
    cat[31] = 0xAA
    csum = sum(struct.unpack("<16H", bytes(cat[:32]))) & 0xFFFF
    struct.pack_into("<H", cat, 28, (-csum) & 0xFFFF)
    assert (sum(struct.unpack("<16H", bytes(cat[:32]))) & 0xFFFF) == 0

    # Entry 1 [32..63]: BIOS x86 No-Emulation
    cat[32] = 0x88
    cat[33] = 0x00  # No emulation -> works for unlimited kernel sizes in SeaBIOS / Hyper-V Gen1 / v86 / QEMU!
    struct.pack_into("<H", cat, 34, 0x0000)
    cat[36] = 0x00
    struct.pack_into("<H", cat, 38, 4)
    struct.pack_into("<I", cat, 40, yoroos_lba)

    # Entry 2 [64..95]: Final Section Header for UEFI (Platform ID = 0xEF)
    cat[64] = 0x91  # Final Section Header indicator
    cat[65] = 0xEF  # EFI Platform ID!
    struct.pack_into("<H", cat, 66, 1)  # 1 section entry follows
    cat[68:96] = b"YOROOS UEFI X64 GEN2".ljust(28, b"\0")

    # Entry 3 [96..127]: UEFI Boot Section Entry pointing at lba_esp with SectorCount = 0
    # (just like `xorriso -e efiboot.img -no-emul-boot` / Windows oscdimg `-pEF`!)
    cat[96] = 0x88  # Bootable
    cat[97] = 0x00  # No emulation
    struct.pack_into("<H", cat, 98, 0x0000)
    cat[100] = 0x00
    struct.pack_into("<H", cat, 102, 0)  # 0 => EDK2/Hyper-V Gen 2 reads FAT16 BPB TotalSectors (16,384)!
    struct.pack_into("<I", cat, 104, lba_esp)

    # Assemble complete ISO image
    iso = bytearray(total_iso_sectors * SEC2048)

    def put_sec(lba2048: int, data: bytes):
        off = lba2048 * SEC2048
        iso[off:off + len(data)] = data

    put_sec(LBA_PVD, pvd)
    put_sec(LBA_BOOTREC, br)
    put_sec(LBA_TERM, term)
    put_sec(LBA_CATALOG, cat)
    put_sec(LBA_PATHTBL, pt)
    put_sec(LBA_ROOT, root)
    put_sec(yoroos_lba, bios_img)
    put_sec(lba_dir_efi, dir_efi)
    put_sec(lba_dir_boot, dir_boot)
    put_sec(lba_efi_file, efi_bytes)
    put_sec(lba_esp, esp_bytes)
    for _, flba, _, raw in extras:
        put_sec(flba, raw)

    # Write GPT + Hybrid Protective MBR (LBA 0, LBA 1..33, Backup LBA end-33..end-1)
    esp_lba512     = lba_esp * 4
    esp_sec512     = len(esp_bytes) // SEC512
    bios_mbr_lba   = yoroos_lba * 4          # 22 * 4 = 88
    stage2_lba     = (yoroos_lba + 1) * 4    # 23 * 4 = 92
    kernel32_lba   = (yoroos_lba + 32) * 4   # 54 * 4 = 216
    kernel32_secs  = (len(k32_bytes) + SEC512 - 1) // SEC512

    write_gpt_and_hybrid_mbr(iso, boot_bin, esp_lba512, esp_sec512)

    # Write "YOROISO2" installer manifest at 512-byte LBA 36 (offset 18,432 — safely after GPT LBA 33!)
    manifest = struct.pack(
        "<8s6I",
        b"YOROISO2",
        esp_lba512,
        esp_sec512,
        bios_mbr_lba,
        stage2_lba,
        kernel32_lba,
        kernel32_secs,
    )
    iso[36 * SEC512:36 * SEC512 + len(manifest)] = manifest

    os.makedirs(os.path.dirname(out_p) or ".", exist_ok=True)
    open(out_p, "wb").write(iso)
    print(
        f"ISO OK: {out_p} ({len(iso) // 1024} KiB = {len(iso) / (1024 * 1024):.2f} MiB, "
        f"ESP512={esp_lba512}+{esp_sec512}, BIOS512={bios_mbr_lba}/{stage2_lba}/{kernel32_lba}+{kernel32_secs})"
    )


if __name__ == "__main__":
    main()
