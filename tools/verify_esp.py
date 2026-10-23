#!/usr/bin/env python3
"""Verify a YoroOS FAT16 EFI System Partition image with pyfatfs.
Usage: verify_esp.py <esp.img> <expected BOOTX64.EFI>
Checks: FAT16 BPB, required EFI directory tree, BOOTX64.EFI byte-equality, INSTALLED.TAG readable."""
import sys
from pyfatfs.PyFatFS import PyFatFS

def main():
    img, ref_path = sys.argv[1], sys.argv[2]
    fs = PyFatFS(img, read_only=True)
    print("FAT type:", getattr(fs.fs, "fat_type", "?"))
    for d in ["/EFI", "/EFI/BOOT", "/EFI/YOROOS"]:
        assert fs.isdir(d), f"missing directory {d}"
        print("dir ok:", d)
    names = [n.upper() for n in fs.listdir("/EFI/BOOT")]
    assert "BOOTX64.EFI" in names, f"BOOTX64.EFI missing in /EFI/BOOT: {names}"
    data = fs.readbytes("/EFI/BOOT/BOOTX64.EFI")
    ref = open(ref_path, "rb").read()
    assert data == ref, f"BOOTX64.EFI mismatch: {len(data)} vs {len(ref)} bytes"
    print(f"BOOTX64.EFI OK: {len(data)} bytes, byte-identical to build artifact")
    tag = fs.readbytes("/EFI/YOROOS/INSTALLED.TAG")
    print("INSTALLED.TAG:", tag.decode(errors="replace").strip().replace("\r\n", " | "))
    fs.close()
    print("ESP VERIFICATION PASSED")

if __name__ == "__main__":
    main()
