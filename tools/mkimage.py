#!/usr/bin/env python3
"""Builds the raw 512B-sector BIOS boot image (boot.bin + stage2.bin + kernel.bin)."""
import sys, struct

boot_p, stg2_p, kern_p, out_p = sys.argv[1:5]
boot = bytearray(open(boot_p, "rb").read())
stg2 = open(stg2_p, "rb").read()
kern = open(kern_p, "rb").read()

assert len(boot) == 512, len(boot)
assert len(stg2) <= 2048, len(stg2)

ksecs = (len(kern) + 511) // 512
boot[0x1B0:0x1B4] = b"YBT2"
boot[0x1B4:0x1B8] = struct.pack("<I", ksecs)

img = bytearray(128 * 512 + ksecs * 512)
img[0:512] = boot
img[4 * 512 : 4 * 512 + len(stg2)] = stg2
img[128 * 512 : 128 * 512 + len(kern)] = kern

# Pad to at least 1.44 MiB if smaller so floppy-emulation BIOSes see a full floppy,
# otherwise pad to 2048B ISO sector multiple
min_size = 1474560
if len(img) < min_size:
    img += b"\0" * (min_size - len(img))
elif len(img) % 2048:
    img += b"\0" * (2048 - len(img) % 2048)

open(out_p, "wb").write(img)
print("mkimage: %s (%d KiB, kernel=%d sectors)" % (out_p, len(img) // 1024, ksecs))
