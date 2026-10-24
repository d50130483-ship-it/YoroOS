#!/usr/bin/env python3
"""Render the kernel's first frame in a small UEFI emulator (Unicorn).

Loads build/BOOTX64.EFI, provides a Graphics Output Protocol with a 1024x768 BGR
framebuffer and the two boot services the kernel uses (LocateProtocol, Stall). The
kernel runs until its first Stall call, then the framebuffer is saved and checked.

Usage: python3 tools/render_test.py <BOOTX64.EFI> <out.png>
"""
import struct
import sys

import pefile
from PIL import Image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_RAX, UC_X86_REG_R8

IMG_BASE = 0x00400000
STACK_TOP = 0x01F00000
TABLES = 0x00300000            # system table, boot services, GOP, mode, info
THUNK_BASE = 0xF0080000
FB_BASE = 0xE0000000
W, H = 1024, 768
PITCH_PX = 1024
PIXEL_FORMAT_BGR = 1

FUNCS = {"LocateProtocol": 320, "Stall": 248}   # BootServices offsets from the UEFI spec


class Emu:
    def __init__(self, efi_path: str):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.stall_calls = 0
        self.locate_calls = 0
        self.uc.mem_map(0x00000000, 0x02000000, UC_PROT_ALL)   # 32 MiB RAM: image, tables, stack

        pe = pefile.PE(efi_path)
        pe.relocate_image(IMG_BASE)
        for sec in pe.sections:
            data = sec.get_data()[: sec.Misc_VirtualSize]
            self.uc.mem_write(IMG_BASE + sec.VirtualAddress, data)
        self.entry = IMG_BASE + pe.OPTIONAL_HEADER.AddressOfEntryPoint

        self.uc.mem_map(THUNK_BASE, 0x1000, UC_PROT_ALL)
        self.uc.mem_map(FB_BASE, 0x00400000, UC_PROT_ALL)     # 4 MiB framebuffer window

        self.setup_tables()

        self.uc.hook_add(UC_HOOK_CODE, self.on_thunk, None, THUNK_BASE, THUNK_BASE + 0xFFF)

    # --- firmware tables ------------------------------------------------------

    def setup_tables(self):
        st = TABLES                  # EFI_SYSTEM_TABLE
        bs = TABLES + 0x1000         # EFI_BOOT_SERVICES
        gop = TABLES + 0x2000        # EFI_GRAPHICS_OUTPUT_PROTOCOL
        mode = TABLES + 0x2100       # EFI_GOP_MODE
        info = TABLES + 0x2200       # EFI_GOP_MODE_INFO

        # Stub addresses for the services the kernel calls.
        self.stub_name = {}
        for i, name in enumerate(FUNCS):
            addr = THUNK_BASE + i * 16
            self.stub_name[addr] = name
            self.uc.mem_write(addr, b"\x90" * 16)
        for name, off in FUNCS.items():
            addr = [a for a, n in self.stub_name.items() if n == name][0]
            self.uc.mem_write(bs + off, struct.pack("<Q", addr))

        self.uc.mem_write(st + 96, struct.pack("<Q", bs))              # BootServices
        self.uc.mem_write(info, struct.pack("<IIIII", 0, W, H, PIXEL_FORMAT_BGR, 0))
        self.uc.mem_write(info + 32, struct.pack("<I", PITCH_PX))      # PixelsPerScanLine
        self.uc.mem_write(mode, struct.pack("<II", 1, 0))            # MaxMode, Mode
        self.uc.mem_write(mode + 8, struct.pack("<Q", info))
        self.uc.mem_write(mode + 24, struct.pack("<Q", FB_BASE))
        self.uc.mem_write(mode + 32, struct.pack("<Q", W * H * 4))
        self.uc.mem_write(gop + 24, struct.pack("<Q", mode))           # Mode
        self.gop = gop
        self.st = st

    # --- thunk dispatch -------------------------------------------------------

    def on_thunk(self, uc, address, size, user_data):
        name = self.stub_name.get(address & ~0xF)
        if name is None:
            return
        if name == "LocateProtocol":
            # (ProtocolGuid, Registration, Interface): return the GOP for any request.
            self.locate_calls += 1
            out_ptr = uc.reg_read(UC_X86_REG_R8)              # 5th arg: Interface
            uc.mem_write(out_ptr, struct.pack("<Q", self.gop))
            uc.reg_write(UC_X86_REG_RAX, 0)
        elif name == "Stall":
            self.stall_calls += 1
            uc.reg_write(UC_X86_REG_RAX, 0)
            uc.emu_stop()   # first frame is complete; stop here
            return
        ret = struct.unpack("<Q", uc.mem_read(uc.reg_read(UC_X86_REG_RSP), 8))[0]
        uc.reg_write(UC_X86_REG_RSP, uc.reg_read(UC_X86_REG_RSP) + 8)
        uc.reg_write(UC_X86_REG_RIP, ret)

    # --- run ------------------------------------------------------------------

    def run(self):
        rsp = STACK_TOP - 0x28
        self.uc.mem_write(rsp, struct.pack("<Q", 0))     # fake return address (never used)
        self.uc.reg_write(UC_X86_REG_RSP, rsp)
        self.uc.reg_write(UC_X86_REG_RCX, 0x1234)        # ImageHandle
        self.uc.reg_write(UC_X86_REG_RDX, self.st)       # SystemTable
        self.uc.emu_start(self.entry, 0, timeout=120 * 1000 * 1000, count=400_000_000)

    def framebuffer(self) -> Image.Image:
        raw = bytes(self.uc.mem_read(FB_BASE, W * H * 4))
        img = Image.frombuffer("RGBA", (W, H), raw, "raw", "BGRA", PITCH_PX * 4, 1)
        return img.convert("RGB")


def check(img: Image.Image) -> None:
    cursor_x = (W - 24) // 2
    cursor_y = (H - 24) // 2
    assert img.getpixel((0, 0)) == (255, 255, 255), "background corner is not white"
    assert img.getpixel((W - 1, H - 1)) == (255, 255, 255), "background corner is not white"
    # Inside the arrow's black fill near the tip.
    assert img.getpixel((cursor_x + 6, cursor_y + 10)) == (0, 0, 0), "cursor fill is not black"
    # Outside the cursor the screen stays white.
    assert img.getpixel((cursor_x - 20, cursor_y + 10)) == (255, 255, 255), "stray pixels outside the cursor"


def main() -> None:
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    emu = Emu(sys.argv[1])
    emu.run()
    assert emu.locate_calls >= 1, "kernel did not ask for the GOP"
    assert emu.stall_calls >= 1, "kernel did not reach its idle loop"
    img = emu.framebuffer()
    check(img)
    img.save(sys.argv[2])
    print(f"ok: GOP located, first frame rendered ({W}x{H}); saved {sys.argv[2]}")


if __name__ == "__main__":
    main()
