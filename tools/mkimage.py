#!/usr/bin/env python3
"""Build the 1.44MB floppy image that is embedded in the YoroOS ISO
(El Torito floppy emulation boot)."""
import sys, os

FLOPPY = 1474560
KERNEL_LBA = 128
KERNEL_DEST = 0x10000
MAX_KERNEL = 0x8F000 - KERNEL_DEST


def main():
    boot_p, stage2_p, kernel_p, out_p = sys.argv[1:5]
    boot = open(boot_p, "rb").read()
    stage2 = open(stage2_p, "rb").read()
    kernel = open(kernel_p, "rb").read()

    assert len(boot) == 512, "boot sector must be 512 bytes, got %d" % len(boot)
    assert len(stage2) <= 48 * 512, "stage2 too big (%d bytes)" % len(stage2)
    assert len(kernel) <= MAX_KERNEL, "kernel too big (%d > %d bytes)" % (len(kernel), MAX_KERNEL)
    assert KERNEL_LBA * 512 + len(kernel) <= FLOPPY

    sectors = (len(kernel) + 511) // 512
    boot = bytearray(boot)
    boot[0x1F0] = sectors & 0xFF
    boot[0x1F1] = (sectors >> 8) & 0xFF

    img = bytearray(FLOPPY)
    img[0:512] = boot
    img[512:512 + len(stage2)] = stage2
    off = KERNEL_LBA * 512
    img[off:off + len(kernel)] = kernel

    open(out_p, "wb").write(img)
    print("image: %s (%d KiB)  kernel %d bytes / %d sectors" %
          (out_p, len(img) // 1024, len(kernel), sectors))


main()
