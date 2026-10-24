# YoroOS — new kernel (0.3.0-dev)

The kernel was rewritten from scratch. The previous kernel, its applications and
its boot paths were removed; they remain in git history before this commit.

Current state: step 1.

- UEFI x86_64 application (`src/main.c`).
- Takes the Graphics Output Protocol framebuffer.
- Paints the background white.
- Draws the default pointer (Linux Mint / Bibata Modern Classic style) at the centre.

Not yet: input, desktop, installer, storage, networking. Each comes as a separate step.

## Build

    make            # build/YoroOS-0.3.0-dev.iso (UEFI-bootable)
    make efi        # build/BOOTX64.EFI only
    make cursor     # regenerate src/cursor_data.h and cursor/preview.png
    make check      # render the first frame in the UEFI emulator -> build/check.png

Requirements: Python 3 with `ziglang`, `Pillow`, `unicorn`, `pefile`.

## Layout

- `src/` — kernel sources (`main.c`, `efi.h`, generated `cursor_data.h`).
- `tools/mkcursor.py` — renders the pointer into `src/cursor_data.h`.
- `tools/mkiso.py` — writes a UEFI-only ISO with an embedded FAT16 EFI System Partition.
- `tools/render_test.py` — boots the kernel in a small UEFI emulator and checks the frame.
- `cursor/preview.png` — the pointer at 8x for review.
