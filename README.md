# YoroOS — new kernel (0.3.0-dev)

The kernel was rewritten from scratch. The previous kernel, its applications and
its boot paths were removed; they remain in git history before this commit.

Current state: step 2 (mouse).

- UEFI x86_64 application (`src/main.c`).
- Takes the Graphics Output Protocol framebuffer and paints the background white.
- Draws the default pointer (Linux Mint / Bibata Modern Classic style).
- On Hyper-V Generation 2 the pointer follows the synthetic mouse. The driver talks
  to VMBus directly (`src/hv_mouse.c`) and reads the SynthHID input reports
  (`src/hid.c` parses the report descriptor). Interrupts stay masked; the driver
  polls the message page and the channel ring every 10 ms.
- Elsewhere the pointer stays in the centre of the screen.

Not yet: keyboard, buttons in the UI, desktop, installer, storage, networking.

## Build

    make            # build/YoroOS-0.3.0-dev.iso (UEFI-bootable)
    make efi        # build/BOOTX64.EFI only
    make cursor     # regenerate src/cursor_data.h and cursor/preview.png
    make check      # render the first frame in the UEFI emulator -> build/check.png
    make test       # host tests for the HID descriptor parser

Requirements: Python 3 with `ziglang`, `Pillow`, `unicorn`, `pefile`.

The EFI image is linked with `lld-link` (shipped with `ziglang`), not `zig cc`:
`zig cc` ignores `/base`, and the image needs a non-zero ImageBase.

## Layout

- `src/` — kernel sources: `main.c`, `hv_mouse.c`/`.h` (Hyper-V synthetic mouse),
  `hid.c`/`.h` (HID descriptor parser), `efi.h`, generated `cursor_data.h`.
- `tools/mkcursor.py` — renders the pointer into `src/cursor_data.h`.
- `tools/mkiso.py` — writes a UEFI-only ISO with an embedded FAT16 EFI System Partition.
- `tools/render_test.py` — boots the kernel in a small UEFI emulator and checks the frame.
- `tools/test_hid.c` — host test for the HID parser.
- `cursor/preview.png` — the pointer at 8x for review.

## Status of the mouse driver

Checked here: the HID parser (`make test`), and the EFI image structure. The emulator
does not run the Hyper-V path, since it has no hypervisor. The VMBus part (offers,
GPADL, channel open, SynthHID handshake) is ported from the earlier kernel's
`hv_native.c`, and was not run against a real Hyper-V host from this sandbox.
