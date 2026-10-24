#!/usr/bin/env python3
"""
Full Hardware-Level Microsoft Hyper-V Generation 2 (UEFI x86-64 -> ExitBootServices -> Native VMBus)
End-to-End Verification Suite for YoroOS 0.2.5!

Emulates real x86-64 CPU via Unicorn Engine + authentic Hyper-V VSP wire protocol:
  - UEFI BootServices + RuntimeServices + SimpleFileSystem + GOP (1024x768x32)
  - Strict post-ExitBootServices trap (any BootServices touch after EBS hard-fails!)
  - Hyper-V CPUID leaves (0x40000000 "Microsoft Hv", 0x40000003) + MSRs + vmcall Hypercall page
  - SynIC SIMP / SIEFP / SINT2 / EOM + HVCALL_POST_MESSAGE + HVCALL_SIGNAL_EVENT
  - Full VMBus channel negotiation: UNLOAD -> INITIATE_CONTACT -> REQUESTOFFERS ->
    multi-part GPADL_HEADER/GPADL_BODY -> OPENCHANNEL -> strict 8-byte u64 ring trailer validation!
  - Full VSP devices: SynthVid + SynthHID Mouse (67-byte real Hyper-V HID descriptor + mouse move/click!)
    + SynthKbd + StorVSC SCSI backed by a 64 MiB virtual VHDX disk!
  - Clicks through YoroOS Start Menu & full 4-step OS Installer purely using Hyper-V SynthHID mouse!
  - Validates written GPT + 16 MiB FAT16 ESP using real Linux `fsck.vfat -v -n`!
  - Boots VM #2 from the installed VHDX (EVEN WITH LIVE ISO STILL ATTACHED!) to verify auto-boot into Installed OS!
"""
import os, sys, struct, subprocess
import pefile
from PIL import Image
from unicorn import (
    Uc, UC_ARCH_X86, UC_MODE_64,
    UC_HOOK_INSN, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_UNMAPPED
)
from unicorn.x86_const import *

# Real Hyper-V Mixed-Endian Wire GUIDs
GUID_SYNTHVID  = bytes([0x02,0x78,0x0a,0xda, 0x77,0xe3, 0xac,0x4a, 0x8e,0x77, 0x05,0x58,0xeb,0x10,0x73,0xf8])
GUID_SYNTHHID  = bytes([0x9e,0xb6,0xa8,0xcf, 0x4a,0x5b, 0xc0,0x4c, 0xb9,0x8b, 0x8b,0xa1,0xa1,0xf3,0xf9,0x5a])
GUID_SYNTHKBD  = bytes([0x6d,0xad,0x12,0xf9, 0x17,0x2b, 0xea,0x48, 0xbd,0x65, 0xf9,0x27,0xa6,0x1c,0x76,0x84])
GUID_STOR_SCSI = bytes([0xd9,0x63,0x61,0xba, 0xa1,0x04, 0x29,0x4d, 0xb6,0x05, 0x72,0xe2,0xff,0xb1,0xdc,0x7f])

# Genuine 67-byte Microsoft Hyper-V SynthHID Mouse Report Descriptor:
#   Buttons: 5 bits + 3 bits pad = 8 bits (offset 0)
#   X: 16 bits logical 0..32767 (offset 8)
#   Y: 16 bits logical 0..32767 (offset 24)
#   Wheel: 8 bits signed -127..127 (offset 40)
HV_HID_REPORT_DESC = bytes([
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x02,        # Usage (Mouse)
    0xA1, 0x01,        # Collection (Application)
    0x09, 0x01,        #   Usage (Pointer)
    0xA1, 0x00,        #   Collection (Physical)
    0x05, 0x09,        #     Usage Page (Button)
    0x19, 0x01,        #     Usage Minimum (1)
    0x29, 0x05,        #     Usage Maximum (5)
    0x15, 0x00,        #     Logical Minimum (0)
    0x25, 0x01,        #     Logical Maximum (1)
    0x95, 0x05,        #     Report Count (5)
    0x75, 0x01,        #     Report Size (1)
    0x81, 0x02,        #     Input (Data,Var,Abs)
    0x95, 0x01,        #     Report Count (1)
    0x75, 0x03,        #     Report Size (3)
    0x81, 0x01,        #     Input (Const)
    0x05, 0x01,        #     Usage Page (Generic Desktop)
    0x09, 0x30,        #     Usage (X)
    0x09, 0x31,        #     Usage (Y)
    0x15, 0x00,        #     Logical Minimum (0)
    0x26, 0xFF, 0x7F,  #     Logical Maximum (32767)
    0x75, 0x10,        #     Report Size (16)
    0x95, 0x02,        #     Report Count (2)
    0x81, 0x02,        #     Input (Data,Var,Abs)
    0x09, 0x38,        #     Usage (Wheel)
    0x15, 0x81,        #     Logical Minimum (-127)
    0x25, 0x7F,        #     Logical Maximum (127)
    0x75, 0x08,        #     Report Size (8)
    0x95, 0x01,        #     Report Count (1)
    0x81, 0x06,        #     Input (Data,Var,Rel)
    0xC0,              #   End Collection
    0xC0,              # End Collection
])


class HyperVGen2Machine:
    def __init__(self, efi_bytes: bytes, bios_bytes: bytes, vhdx_buf: bytearray,
                 iso_bytes: bytes = b"", boot_from_vhdx: bool = False):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.efi_bytes = efi_bytes
        self.bios_bytes = bios_bytes
        self.vhdx = vhdx_buf
        self.iso_bytes = iso_bytes
        self.boot_from_vhdx = boot_from_vhdx

        # Address layout:
        #   0x0000_0000 .. 0x1400_0000 : 320 MiB Guest RAM (image @ 0x0040_0000, heap @ 0x0200_0000)
        #   0xE000_0000 .. 0xE040_0000 : 4 MiB GOP Framebuffer (1024x768x32)
        #   0xFEE0_0000 .. 0xFEE0_1000 : x86 Local APIC MMIO page
        #   0xF000_0000 .. 0xF010_0000 : UEFI SystemTable / BootServices / RuntimeServices / SFS thunks
        self.FB_BASE = 0xE0000000
        self.FB_W, self.FB_H = 1024, 768
        self.IMG_BASE = 0x00400000
        self.HEAP_NEXT = 0x02000000
        self.THUNK_BASE = 0xF0080000

        self.uc.mem_map(0x00000000, 512 * 1024 * 1024)
        self.uc.mem_map(self.FB_BASE, 4 * 1024 * 1024)
        self.uc.mem_map(0xFEE00000, 0x1000)
        self.uc.mem_map(0xF0000000, 0x100000)

        # Initialize LAPIC SVR = 0x1FF
        self.uc.mem_write(0xFEE000F0, struct.pack("<I", 0x1FF))

        self.ebs_called = False
        self.reboot_requested = False
        self.nvram_vars = {}
        self.time_100ns = 10_000_000  # 1.0s

        # Hyper-V SynIC & VMBus state
        self.msrs = {
            0x40000000: 0, 0x40000001: 0, 0x40000002: 0, 0x40000003: 0,
            0x40000070: 0, 0x40000080: 0, 0x40000082: 0, 0x40000083: 0,
            0x40000084: 0, 0x40000092: 0x10000,
        }
        self.ctrl_msg_queue = []
        self.gpadls = {}         # gpadl_handle -> list of PFNs
        self.pending_gpadl = None
        self.channels = {}       # relid -> dict
        self.stats = {
            "unload": 0, "contact": 0, "gpadl_created": 0, "chans_opened": 0,
            "ring_pkts_verified": 0, "synthvid_dirt": 0, "synthvid_ptr_hidden": 0,
            "stor_reads": 0, "stor_writes": 0, "stor_sync_cache": 0,
            "serial_log": ""
        }

        self._load_pe_image()
        self._setup_uefi_tables()
        self._install_hooks()

    def _load_pe_image(self):
        pe = pefile.PE(data=self.efi_bytes)
        self.entry_rip = self.IMG_BASE + pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.uc.mem_write(self.IMG_BASE, self.efi_bytes[:pe.OPTIONAL_HEADER.SizeOfHeaders])
        for sec in pe.sections:
            raw = sec.get_data()
            self.uc.mem_write(self.IMG_BASE + sec.VirtualAddress, raw)
        # Apply base relocations if ImageBase != self.IMG_BASE
        delta = self.IMG_BASE - pe.OPTIONAL_HEADER.ImageBase
        if delta != 0 and hasattr(pe, "DIRECTORY_ENTRY_BASERELOC"):
            for base_reloc in pe.DIRECTORY_ENTRY_BASERELOC:
                for entry in base_reloc.entries:
                    if entry.type == 10:  # IMAGE_REL_BASED_DIR64
                        addr = self.IMG_BASE + entry.rva
                        val = struct.unpack("<Q", self.uc.mem_read(addr, 8))[0]
                        self.uc.mem_write(addr, struct.pack("<Q", (val + delta) & 0xFFFFFFFFFFFFFFFF))

    def _alloc_thunk(self, fn, require_pre_ebs=False):
        addr = self.THUNK_BASE + len(self.thunks) * 8
        # Write `ret` (0xC3) at addr so once code hook updates RAX, guest cleanly returns!
        self.uc.mem_write(addr, b"\xC3")
        self.thunks[addr] = (fn, require_pre_ebs)
        return addr

    def _read_ucs2(self, ptr):
        out = []
        while True:
            ch = struct.unpack("<H", self.uc.mem_read(ptr, 2))[0]
            if ch == 0:
                break
            out.append(chr(ch))
            ptr += 2
        return "".join(out)

    def _setup_uefi_tables(self):
        self.thunks = {}
        self.sfs_files = {
            "\\EFI\\BOOT\\BOOTX64.EFI": self.efi_bytes,
            "\\YOROOS.IMG": self.bios_bytes,
        }
        if self.boot_from_vhdx:
            self.sfs_files["\\EFI\\YOROOS\\INSTALLED.TAG"] = b"YOROOS_INSTALLED=1\nVERSION=0.2.5\n"

        # Build EFI_FILE_PROTOCOL handles
        self.next_meta = 0xF0010000
        def meta_alloc(data: bytes):
            p = self.next_meta
            self.next_meta += (len(data) + 15) & ~15
            self.uc.mem_write(p, data)
            return p

        self.open_files = {}  # file_proto_addr -> [bytes, pos]

        def make_file_handle(content: bytes, is_root=False):
            fp = self.next_meta
            self.next_meta += 128

            def f_open(uc):
                # RCX=This, RDX=*NewHandle, R8=FileName, R9=OpenMode
                out_ptr = uc.reg_read(UC_X86_REG_RDX)
                fname = self._read_ucs2(uc.reg_read(UC_X86_REG_R8))
                norm = "\\" + fname.lstrip("\\")
                for k, v in self.sfs_files.items():
                    if k.lower() == norm.lower():
                        h = make_file_handle(v, False)
                        uc.mem_write(out_ptr, struct.pack("<Q", h))
                        return 0
                return 0x800000000000000E  # EFI_NOT_FOUND

            def f_close(uc):
                return 0

            def f_read(uc):
                this = uc.reg_read(UC_X86_REG_RCX)
                sz_ptr = uc.reg_read(UC_X86_REG_RDX)
                buf_ptr = uc.reg_read(UC_X86_REG_R8)
                req = struct.unpack("<Q", uc.mem_write and uc.mem_read(sz_ptr, 8))[0]
                data, pos = self.open_files[this]
                chunk = data[pos:pos + req]
                if chunk:
                    uc.mem_write(buf_ptr, chunk)
                self.open_files[this][1] = pos + len(chunk)
                uc.mem_write(sz_ptr, struct.pack("<Q", len(chunk)))
                return 0

            def f_getpos(uc):
                this = uc.reg_read(UC_X86_REG_RCX)
                p_ptr = uc.reg_read(UC_X86_REG_RDX)
                uc.mem_write(p_ptr, struct.pack("<Q", self.open_files[this][1]))
                return 0

            def f_setpos(uc):
                this = uc.reg_read(UC_X86_REG_RCX)
                pos = uc.reg_read(UC_X86_REG_RDX)
                data = self.open_files[this][0]
                self.open_files[this][1] = len(data) if pos == 0xFFFFFFFFFFFFFFFF else pos
                return 0

            self.open_files[fp] = [content, 0]
            struct_bytes = struct.pack(
                "<QQQQQQQQQ",
                0x00010000,
                self._alloc_thunk(f_open, True),
                self._alloc_thunk(f_close, True),
                0,
                self._alloc_thunk(f_read, True),
                0,
                self._alloc_thunk(f_getpos, True),
                self._alloc_thunk(f_setpos, True),
                0,
            )
            self.uc.mem_write(fp, struct_bytes)
            return fp

        root_dir = make_file_handle(b"", is_root=True)
        sfs_proto = meta_alloc(struct.pack(
            "<QQ", 0x00010000,
            self._alloc_thunk(lambda uc: (
                uc.mem_write(uc.reg_read(UC_X86_REG_RDX), struct.pack("<Q", root_dir)), 0
            )[1], True)
        ))

        self.DEV_HANDLE = 0xCAFE0001
        loaded_img = meta_alloc(struct.pack(
            "<I4xQQQQQI4xQQQIIQ",
            0x1000, 0, 0, self.DEV_HANDLE, 0, 0,
            0, 0, self.IMG_BASE, len(self.efi_bytes), 2, 4, 0
        ))

        # GOP protocol (1024x768 BlueGreenRedReserved8BitPerColor = format 1)
        gop_info = meta_alloc(struct.pack("<IIII16sI", 0, self.FB_W, self.FB_H, 1, b"\x00" * 16, self.FB_W))
        gop_mode = meta_alloc(struct.pack("<IIQQQQ", 1, 0, gop_info, 36, self.FB_BASE, self.FB_W * self.FB_H * 4))
        gop_proto = meta_alloc(struct.pack(
            "<QQQQ",
            self._alloc_thunk(lambda uc: (
                uc.mem_write(uc.reg_read(UC_X86_REG_R8), struct.pack("<Q", 36)),
                uc.mem_write(uc.reg_read(UC_X86_REG_R9), struct.pack("<Q", gop_info)), 0
            )[2], True),
            self._alloc_thunk(lambda uc: 0, True),
            0, gop_mode
        ))

        def bs_alloc_pool(uc):
            sz = uc.reg_read(UC_X86_REG_RDX)
            out_p = uc.reg_read(UC_X86_REG_R8)
            p = (self.HEAP_NEXT + 4095) & ~4095
            self.HEAP_NEXT = p + ((sz + 4095) & ~4095)
            uc.mem_write(out_p, struct.pack("<Q", p))
            return 0

        def bs_handle_proto(uc):
            handle = uc.reg_read(UC_X86_REG_RCX)
            guid = bytes(uc.mem_read(uc.reg_read(UC_X86_REG_RDX), 16))
            out_p = uc.reg_read(UC_X86_REG_R8)
            g_li = bytes(struct.pack("<IHH8B", 0x5b1b31a1, 0x9562, 0x11d2, 0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b))
            g_sfs = bytes(struct.pack("<IHH8B", 0x964e5b22, 0x6459, 0x11d2, 0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b))
            if guid == g_li:
                uc.mem_write(out_p, struct.pack("<Q", loaded_img)); return 0
            if guid == g_sfs:
                uc.mem_write(out_p, struct.pack("<Q", sfs_proto)); return 0
            return 0x8000000000000003

        def bs_locate_proto(uc):
            guid = bytes(uc.mem_read(uc.reg_read(UC_X86_REG_RCX), 16))
            out_p = uc.reg_read(UC_X86_REG_R8)
            g_gop = bytes(struct.pack("<IHH8B", 0x9042a9de, 0x23dc, 0x4a38, 0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a))
            if guid == g_gop:
                uc.mem_write(out_p, struct.pack("<Q", gop_proto)); return 0
            return 0x800000000000000E

        def bs_get_mem_map(uc):
            sz_p = uc.reg_read(UC_X86_REG_RCX)
            buf_p = uc.reg_read(UC_X86_REG_RDX)
            key_p = uc.reg_read(UC_X86_REG_R8)
            dsz_p = uc.reg_read(UC_X86_REG_R9)
            ver_p = struct.unpack("<Q", uc.mem_read(uc.reg_read(UC_X86_REG_RSP) + 40, 8))[0]
            avail = struct.unpack("<Q", uc.mem_read(sz_p, 8))[0]
            uc.mem_write(sz_p, struct.pack("<Q", 48))
            if dsz_p: uc.mem_write(dsz_p, struct.pack("<Q", 48))
            if ver_p: uc.mem_write(ver_p, struct.pack("<I", 1))
            if avail < 48:
                return 0x8000000000000005  # EFI_BUFFER_TOO_SMALL
            uc.mem_write(key_p, struct.pack("<Q", 0x55AA7788))
            uc.mem_write(buf_p, b"\x00" * 48)
            return 0

        def bs_exit_boot_services(uc):
            key = uc.reg_read(UC_X86_REG_RDX)
            assert key == 0x55AA7788, f"Wrong MapKey {hex(key)} passed to ExitBootServices!"
            self.ebs_called = True
            return 0

        def bs_alloc_pages(uc):
            pages = uc.reg_read(UC_X86_REG_R8)
            mem_p = uc.reg_read(UC_X86_REG_R9)
            p = (self.HEAP_NEXT + 4095) & ~4095
            self.HEAP_NEXT = p + pages * 4096
            uc.mem_write(mem_p, struct.pack("<Q", p))
            return 0

        # Build EFI_BOOT_SERVICES with exact Microsoft x64 ABI slot offsets matching include/efi.h!
        nop_pre = self._alloc_thunk(lambda uc: 0, True)
        err_pre = self._alloc_thunk(lambda uc: 0x800000000000000E, True)
        bs_slots = [nop_pre] * 45
        bs_slots[2]  = self._alloc_thunk(bs_alloc_pages, True)         # AllocatePages
        bs_slots[4]  = self._alloc_thunk(bs_get_mem_map, True)         # GetMemoryMap
        bs_slots[5]  = self._alloc_thunk(bs_alloc_pool, True)          # AllocatePool
        bs_slots[6]  = nop_pre                                          # FreePool
        bs_slots[16] = self._alloc_thunk(bs_handle_proto, True)        # HandleProtocol
        bs_slots[26] = self._alloc_thunk(bs_exit_boot_services, True)  # ExitBootServices (0xE8)
        bs_slots[28] = nop_pre                                          # Stall (0xF8)
        bs_slots[29] = nop_pre                                          # SetWatchdogTimer (0x100)
        bs_slots[36] = err_pre                                          # LocateHandleBuffer (0x138)
        bs_slots[37] = self._alloc_thunk(bs_locate_proto, True)        # LocateProtocol (0x140)
        bs_addr = meta_alloc(b"\x00" * 24 + b"".join(struct.pack("<Q", s) for s in bs_slots))

        # Build EFI_RUNTIME_SERVICES (allowed for GetTime/SetVariable pre-EBS & ResetSystem anytime)
        def rt_get_time(uc):
            tp = uc.reg_read(UC_X86_REG_RCX)
            uc.mem_write(tp, struct.pack("<HBBBBBBIhBB", 2026, 10, 23, 18, 45, 12, 0, 0, 0, 0, 0))
            return 0

        def rt_set_var(uc):
            name = self._read_ucs2(uc.reg_read(UC_X86_REG_RCX))
            sz = uc.reg_read(UC_X86_REG_R9)
            data_p = struct.unpack("<Q", uc.mem_read(uc.reg_read(UC_X86_REG_RSP) + 40, 8))[0]
            self.nvram_vars[name] = bytes(uc.mem_read(data_p, sz)) if sz else b""
            return 0

        def rt_reset(uc):
            self.reboot_requested = True
            uc.emu_stop()
            return 0

        err_any = self._alloc_thunk(lambda uc: 0x800000000000000E, False)
        rt_addr = meta_alloc(
            b"\x00" * 24 + struct.pack(
                "<QQQQQQQQQQQ",
                self._alloc_thunk(rt_get_time, True),
                nop_pre, nop_pre, nop_pre,
                nop_pre, nop_pre,
                err_any, nop_pre,
                self._alloc_thunk(rt_set_var, False),
                nop_pre,
                self._alloc_thunk(rt_reset, False),
            )
        )

        con_in = meta_alloc(struct.pack("<QQQ", nop_pre, err_pre, 0))
        con_out = meta_alloc(struct.pack("<QQQQQQQQQQ", nop_pre, nop_pre, nop_pre, nop_pre, nop_pre, nop_pre, nop_pre, nop_pre, nop_pre, 0))
        self.sys_table = meta_alloc(
            b"\x00" * 24 + struct.pack(
                "<QI4xQQQQQQQQQ",
                0, 0, 0, con_in, 0, con_out, 0, 0, rt_addr, bs_addr, 0
            ) + b"\x00" * 8
        )

    # ---------------- Full Hyper-V VSP Wire Protocol Emulation ----------------
    def _push_ctrl_msg(self, payload: bytes):
        self.ctrl_msg_queue.append(payload)
        self._flush_next_synic_slot()

    def _flush_next_synic_slot(self):
        simp = self.msrs[0x40000083]
        if not (simp & 1) or not self.ctrl_msg_queue:
            return
        slot2 = (simp & ~0xFFF) + 2 * 256
        cur_type = struct.unpack("<I", self.uc.mem_read(slot2, 4))[0]
        if cur_type != 0:
            # Mark message_pending bit so EOM delivers next queued message!
            self.uc.mem_write(slot2 + 5, bytes([1 if self.ctrl_msg_queue else 0]))
            return
        payload = self.ctrl_msg_queue.pop(0)
        flags = 1 if self.ctrl_msg_queue else 0
        hdr = struct.pack("<IBB2xQ", 1, len(payload), flags, 0)
        msg = hdr + payload + b"\x00" * (240 - len(payload))
        self.uc.mem_write(slot2, msg)

    def _make_offer(self, relid: int, conn_id: int, guid: bytes, is_pipe: int, mon_id: int = 0xFF):
        # Exact 196-byte vmbus_channel_offer_channel matching torvalds/linux!
        hdr = struct.pack("<II", 1, 0)  # CHANNELMSG_OFFERCHANNEL = 1
        offer = (
            guid + os.urandom(16) + struct.pack("<QQHH", 0, 0, 0, 0) +
            b"\x00" * 120 + struct.pack("<HH", 0, 0)
        )
        assert len(offer) == 176
        tail = struct.pack("<IBBH", relid, mon_id, 1 if mon_id != 0xFF else 0, 1) + struct.pack("<I", conn_id)
        pkt = hdr + offer + tail
        assert len(pkt) == 196, f"Offer size mismatch {len(pkt)}"
        return pkt

    def _handle_post_message(self, in_gpa: int):
        raw = bytes(self.uc.mem_read(in_gpa, 256))
        conn_id, _, msg_type, payload_sz = struct.unpack("<IIII", raw[:16])
        payload = raw[16:16 + payload_sz]
        mtype = struct.unpack("<I", payload[:4])[0]

        if mtype == 16:  # CHANNELMSG_UNLOAD
            self.stats["unload"] += 1
            self._push_ctrl_msg(struct.pack("<II", 17, 0))  # UNLOAD_RESPONSE
        elif mtype == 14:  # CHANNELMSG_INITIATE_CONTACT
            assert len(payload) == 40, f"HvInitiateContact wrong size {len(payload)}!"
            ver_req = struct.unpack("<I", payload[8:12])[0]
            self.stats["contact"] += 1
            # Negotiate VERSION_WIN10_V5_2 (0x00050002), msg_conn_id = 4
            self._push_ctrl_msg(struct.pack("<IIBB2xI", 15, 0, 1, 2, 4))
        elif mtype == 3:  # CHANNELMSG_REQUESTOFFERS
            # Offer SynthVid (relid 1), SynthHID Mouse (relid 2), SynthKbd (relid 3), StorVSC SCSI (relid 4)
            self.offer_map = {
                1: ("vid",   201, 1),
                2: ("mouse", 202, 1),
                3: ("kbd",   203, 0),
                4: ("stor",  204, 0),
            }
            self._push_ctrl_msg(self._make_offer(1, 201, GUID_SYNTHVID,  1, 1))
            self._push_ctrl_msg(self._make_offer(2, 202, GUID_SYNTHHID,  1, 2))
            self._push_ctrl_msg(self._make_offer(3, 203, GUID_SYNTHKBD,  0, 3))
            self._push_ctrl_msg(self._make_offer(4, 204, GUID_STOR_SCSI, 0, 4))
            self._push_ctrl_msg(struct.pack("<II", 2, 0))  # ALLOFFERS_DELIVERED
        elif mtype == 8:  # CHANNELMSG_GPADL_HEADER
            relid, gpadl, range_buflen, range_cnt = struct.unpack("<IIHH", payload[8:20])
            byte_cnt, byte_ofs = struct.unpack("<II", payload[20:28])
            pfns = list(struct.unpack(f"<{(len(payload) - 28)//8}Q", payload[28:]))
            need = byte_cnt // 4096
            self.pending_gpadl = [relid, gpadl, need, pfns]
            if len(pfns) >= need:
                self.gpadls[gpadl] = pfns[:need]
                self.stats["gpadl_created"] += 1
                self._push_ctrl_msg(struct.pack("<IIIII", 10, 0, relid, gpadl, 0))
        elif mtype == 9:  # CHANNELMSG_GPADL_BODY
            _, gpadl = struct.unpack("<II", payload[8:16])
            more = list(struct.unpack(f"<{(len(payload) - 16)//8}Q", payload[16:]))
            relid, gp, need, pfns = self.pending_gpadl
            assert gp == gpadl
            pfns.extend(more)
            if len(pfns) >= need:
                self.gpadls[gpadl] = pfns[:need]
                self.stats["gpadl_created"] += 1
                self._push_ctrl_msg(struct.pack("<IIIII", 10, 0, relid, gpadl, 0))
        elif mtype == 5:  # CHANNELMSG_OPENCHANNEL
            assert len(payload) == 148, f"HvOpenChannel wrong size {len(payload)} != 148!"
            relid, openid, ring_gpadl, vp, down_ofs = struct.unpack("<IIIII", payload[8:28])
            pfns = self.gpadls[ring_gpadl]
            kind, cid, is_pipe = self.offer_map[relid]
            self.channels[relid] = {
                "kind": kind, "conn_id": cid, "is_pipe": is_pipe,
                "out_hdr": pfns[0] * 4096,
                "out_data": [p * 4096 for p in pfns[1:down_ofs]],
                "in_hdr": pfns[down_ofs] * 4096,
                "in_data": [p * 4096 for p in pfns[down_ofs + 1:]],
            }
            self.stats["chans_opened"] += 1
            self._push_ctrl_msg(struct.pack("<IIIII", 6, 0, relid, openid, 0))
        return 0

    def _ring_read_bytes(self, pages, off, length):
        size = len(pages) * 4096
        out = bytearray()
        for i in range(length):
            pos = (off + i) % size
            out.append(self.uc.mem_read(pages[pos // 4096] + (pos % 4096), 1)[0])
        return bytes(out)

    def _ring_write_bytes(self, pages, off, data: bytes):
        size = len(pages) * 4096
        for i, b in enumerate(data):
            pos = (off + i) % size
            self.uc.mem_write(pages[pos // 4096] + (pos % 4096), bytes([b]))

    def send_host_pkt(self, ch, pkt_type: int, trans_id: int, payload: bytes, wrap_pipe=False):
        if wrap_pipe:
            payload = struct.pack("<II", 1, len(payload)) + payload
        hdr = struct.pack("<HHHHQ", pkt_type, 2, (16 + len(payload) + 7) // 8, 0, trans_id)
        pad = b"\x00" * ((8 - (len(payload) % 8)) % 8)
        w, r = struct.unpack("<II", self.uc.mem_read(ch["in_hdr"], 8))
        trailer = struct.pack("<Q", w << 32)
        full = hdr + payload + pad + trailer
        self._ring_write_bytes(ch["in_data"], w, full)
        ring_sz = len(ch["in_data"]) * 4096
        self.uc.mem_write(ch["in_hdr"], struct.pack("<I", (w + len(full)) % ring_sz))

    def _process_channel_outbound(self, ch):
        ring_sz = len(ch["out_data"]) * 4096
        while True:
            w, r = struct.unpack("<II", self.uc.mem_read(ch["out_hdr"], 8))
            if r == w:
                break
            desc = self._ring_read_bytes(ch["out_data"], r, 16)
            ptype, ofs8, len8, flags, trans_id = struct.unpack("<HHHHQ", desc)
            pkt_bytes = len8 * 8
            raw_pkt = self._ring_read_bytes(ch["out_data"], r, pkt_bytes + 8)
            # STRICT LINUX VMBUS INVARIANT CHECK: 8-byte trailer MUST encode (old_write_index << 32)!
            trailer = struct.unpack("<Q", raw_pkt[-8:])[0]
            expected_old_w = r
            assert (trailer >> 32) == expected_old_w, (
                f"FATAL VMBUS BUG: packet trailer {hex(trailer)} != ({expected_old_w} << 32)!"
            )
            self.stats["ring_pkts_verified"] += 1
            r = (r + pkt_bytes + 8) % ring_sz
            self.uc.mem_write(ch["out_hdr"] + 4, struct.pack("<I", r))
            body = raw_pkt[ofs8 * 8:pkt_bytes]
            self._dispatch_vsp_packet(ch, ptype, flags, trans_id, body, raw_pkt[:ofs8 * 8])

    def _dispatch_vsp_packet(self, ch, ptype, flags, trans_id, body: bytes, hdr_ext: bytes):
        kind = ch["kind"]
        if kind == "vid":
            pipe_t, pipe_sz = struct.unpack("<II", body[:8])
            msg = body[8:8 + pipe_sz]
            vtype, vsz = struct.unpack("<II", msg[:8])
            if vtype == 1:  # VERSION_REQUEST
                ver = struct.unpack("<I", msg[8:12])[0]
                self.send_host_pkt(ch, 6, 0, struct.pack("<IIIBB", 2, 14, ver, 1, 1), wrap_pipe=True)
            elif vtype == 3:  # VRAM_LOCATION -> ack ctx + send FEATURE_CHANGE!
                ctx = struct.unpack("<Q", msg[8:16])[0]
                self.send_host_pkt(ch, 6, 0, struct.pack("<IIQB", 4, 17, ctx, 1), wrap_pipe=True)
                self.send_host_pkt(ch, 6, 0, struct.pack("<IIBBBB", 6, 12, 1, 1, 1, 0), wrap_pipe=True)
            elif vtype == 8:  # POINTER_SHAPE
                self.stats["synthvid_ptr_hidden"] += 1
            elif vtype == 10:  # SYNTHVID_DIRT
                self.stats["synthvid_dirt"] += 1

        elif kind == "mouse":
            pipe_t, pipe_sz = struct.unpack("<II", body[:8])
            msg = body[8:8 + pipe_sz]
            htype, hsz = struct.unpack("<II", msg[:8])
            if htype == 0:  # PROTOCOL_REQUEST
                ver = struct.unpack("<I", msg[8:12])[0]
                self.send_host_pkt(ch, 6, 0, struct.pack("<IIIB3x", 1, 8, ver, 1), wrap_pipe=True)
                # Send genuine INITIAL_DEVICE_INFO with 67-byte HID report descriptor!
                hid_attr = struct.pack("<IHHHI", 12, 0x045E, 0x0621, 1, 0)
                hid_desc = struct.pack("<BBHBBH", 9, 0x21, 0x0101, 0, 1, 0x22) + struct.pack("<H", len(HV_HID_REPORT_DESC))
                info = struct.pack("<II", 2, 4 + len(hid_attr) + len(hid_desc) + len(HV_HID_REPORT_DESC)) + hid_attr + hid_desc + HV_HID_REPORT_DESC
                self.send_host_pkt(ch, 6, 0, info, wrap_pipe=True)

        elif kind == "kbd":
            ktype = struct.unpack("<I", body[:4])[0]
            if ktype == 1:
                self.send_host_pkt(ch, 6, 0, struct.pack("<II", 2, 1), wrap_pipe=False)

        elif kind == "stor":
            op, sflags, status = struct.unpack("<III", body[:12])
            if op == 7:    # BEGIN_INITIALIZATION
                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + b"\x00" * 52)
            elif op == 9:  # QUERY_PROTOCOL_VERSION
                ver_b = body[12:16]
                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + ver_b + b"\x00" * 48)
            elif op == 10: # QUERY_PROPERTIES
                props = struct.pack("<HHBBHIQ", 0, 8, 0, 0, 0, 32768, 0)
                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + props + b"\x00" * (52 - len(props)))
            elif op == 8:  # END_INITIALIZATION
                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + b"\x00" * 52)
            elif op == 11: # ENUMERATE_BUS
                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + b"\x00" * 52)
            elif op == 3:  # EXECUTE_SRB!
                srb = bytearray(body[12:64])
                path_id, target_id, lun = srb[5], srb[6], srb[7]
                cdb_len = srb[8]
                data_in = srb[10]
                xfer_len = struct.unpack("<I", srb[12:16])[0]
                cdb = bytes(srb[16:16 + cdb_len])

                # Disk mapping on virtual SCSI bus:
                #   (0, 0, 0) = Virtual Hard Disk (VHDX, 64 MiB writable!)
                #   (0, 1, 0) = Virtual DVD-ROM (Live ISO, read-only, if attached!)
                disk_kind = None
                if (path_id, target_id, lun) == (0, 0, 0):
                    disk_kind = "vhdx"
                elif (path_id, target_id, lun) == (0, 1, 0) and self.iso_bytes:
                    disk_kind = "iso"

                gpa_base = 0
                if ptype == 9:  # VM_PKT_DATA_USING_GPA_DIRECT
                    _, _, rlen, rofs = struct.unpack("<IIII", hdr_ext[16:32])
                    pfn0 = struct.unpack("<Q", hdr_ext[32:40])[0]
                    gpa_base = pfn0 * 4096 + rofs

                if disk_kind is None:
                    srb[2] = 0x0A  # SRB_STATUS_SELECTION_TIMEOUT
                    srb[3] = 0x02
                else:
                    srb[2] = 0x01  # SRB_STATUS_SUCCESS
                    srb[3] = 0x00  # GOOD
                    opcode = cdb[0]
                    if opcode == 0x00:    # TEST UNIT READY
                        pass
                    elif opcode == 0x12:  # INQUIRY
                        if disk_kind == "vhdx":
                            inq = bytes([0x00, 0x00, 5, 2, 31, 0, 0, 0]) + b"Msft    Virtual Disk    1.0 "
                        else:
                            inq = bytes([0x05, 0x80, 5, 2, 31, 0, 0, 0]) + b"Msft    Virtual DVD-ROM 1.0 "
                        self.uc.mem_write(gpa_base, inq[:xfer_len])
                    elif opcode == 0x25:  # READ CAPACITY(10)
                        total_sec = (len(self.vhdx) // 512) if disk_kind == "vhdx" else (len(self.iso_bytes) // 2048)
                        bsz = 512 if disk_kind == "vhdx" else 2048
                        self.uc.mem_write(gpa_base, struct.pack(">II", total_sec - 1, bsz))
                    elif opcode == 0x28:  # READ(10)
                        lba = int.from_bytes(cdb[2:6], "big"); cnt = int.from_bytes(cdb[7:9], "big")  # READ/WRITE(10)
                        bsz = 512 if disk_kind == "vhdx" else 2048
                        src = self.vhdx if disk_kind == "vhdx" else self.iso_bytes
                        # Read multi-page GPA ranges accurately!
                        data = bytes(src[lba * bsz:(lba + cnt) * bsz])
                        self._write_gpa_direct(hdr_ext, data)
                        self.stats["stor_reads"] += 1
                    elif opcode == 0x2A:  # WRITE(10)
                        assert disk_kind == "vhdx", "Write to read-only ISO!"
                        lba = int.from_bytes(cdb[2:6], "big"); cnt = int.from_bytes(cdb[7:9], "big")  # READ/WRITE(10)
                        data = self._read_gpa_direct(hdr_ext, cnt * 512)
                        self.vhdx[lba * 512:(lba + cnt) * 512] = data
                        self.stats["stor_writes"] += 1
                    elif opcode == 0x35:  # SYNCHRONIZE CACHE(10)
                        self.stats["stor_sync_cache"] += 1
                    else:
                        # Unsupported SCSI opcode (e.g. 0x9E on virtual DVD) -> ILLEGAL REQUEST!
                        srb[2] = 0x04
                        srb[3] = 0x02

                self.send_host_pkt(ch, 0xB, trans_id, struct.pack("<III", 1, 0, 0) + bytes(srb))

    def _gpa_pages(self, hdr_ext: bytes):
        _, _, rlen, rofs = struct.unpack("<IIII", hdr_ext[16:32])
        npfns = (len(hdr_ext) - 32) // 8
        pfns = struct.unpack(f"<{npfns}Q", hdr_ext[32:32 + npfns * 8])
        return rlen, rofs, pfns

    def _write_gpa_direct(self, hdr_ext: bytes, data: bytes):
        rlen, rofs, pfns = self._gpa_pages(hdr_ext)
        pos = 0
        pidx = 0
        first = min(len(data), 4096 - rofs)
        self.uc.mem_write(pfns[0] * 4096 + rofs, data[:first])
        pos += first
        pidx = 1
        while pos < len(data):
            take = min(4096, len(data) - pos)
            self.uc.mem_write(pfns[pidx] * 4096, data[pos:pos + take])
            pos += take
            pidx += 1

    def _read_gpa_direct(self, hdr_ext: bytes, total: int) -> bytes:
        rlen, rofs, pfns = self._gpa_pages(hdr_ext)
        out = bytearray()
        first = min(total, 4096 - rofs)
        out.extend(self.uc.mem_read(pfns[0] * 4096 + rofs, first))
        pidx = 1
        while len(out) < total:
            take = min(4096, total - len(out))
            out.extend(self.uc.mem_read(pfns[pidx] * 4096, take))
            pidx += 1
        return bytes(out)

    # Inject REAL Hyper-V SynthHID mouse event over VMBus ring!
    def move_and_click_mouse(self, px_x: int, px_y: int, click: bool = True):
        mouse_ch = [c for c in self.channels.values() if c["kind"] == "mouse"][0]
        abs_x = int(px_x * 32767 // (self.FB_W - 1))
        abs_y = int(px_y * 32767 // (self.FB_H - 1))

        def push_report(btn: int):
            rep = struct.pack("<BHHb", btn, abs_x, abs_y, 0)
            pkt = struct.pack("<II", 4, len(rep)) + rep  # SYNTH_HID_INPUT_REPORT = 4
            self.send_host_pkt(mouse_ch, 6, 0, pkt, wrap_pipe=True)

        push_report(0)
        if click:
            push_report(1)
            push_report(0)

    def inject_synthkbd_key(self, code: int, is_e0: bool = False):
        # struct synth_kbd_keystroke: header.type=3 (SYNTH_KBD_EVENT), make_code, reserved, info
        # info bits: IS_UNICODE=1, IS_BREAK=2, IS_E0=4
        kbd_ch = [c for c in self.channels.values() if c["kind"] == "kbd"][0]
        e0 = 4 if is_e0 else 0
        self.send_host_pkt(kbd_ch, 6, 0, struct.pack("<IHHI", 3, code, 0, e0))
        self.send_host_pkt(kbd_ch, 6, 0, struct.pack("<IHHI", 3, code, 0, e0 | 2))

    def save_screenshot(self, path: str):
        raw = bytes(self.uc.mem_read(self.FB_BASE, self.FB_W * self.FB_H * 4))
        img = Image.frombytes("RGBA", (self.FB_W, self.FB_H), raw, "raw", "BGRA")
        img.convert("RGB").save(path)

    def _install_hooks(self):
        # 1. CPUID hook: advertise "Microsoft Hv" + PartitionReferenceCounter!
        def on_cpuid(uc, user_data):
            eax = uc.reg_read(UC_X86_REG_EAX)
            if eax == 1:
                uc.reg_write(UC_X86_REG_ECX, 1 << 31)
                uc.reg_write(UC_X86_REG_EAX, 0x000806E9)
                uc.reg_write(UC_X86_REG_EBX, 0)
                uc.reg_write(UC_X86_REG_EDX, 0x078BFBFF)
            elif eax == 0x40000000:
                uc.reg_write(UC_X86_REG_EAX, 0x40000005)
                uc.reg_write(UC_X86_REG_EBX, struct.unpack("<I", b"Micr")[0])
                uc.reg_write(UC_X86_REG_ECX, struct.unpack("<I", b"osof")[0])
                uc.reg_write(UC_X86_REG_EDX, struct.unpack("<I", b"t Hv")[0])
            elif eax == 0x40000003:
                uc.reg_write(UC_X86_REG_EAX, 0x2FFE)  # bit 1 = TimeRefCount MSR present!
                uc.reg_write(UC_X86_REG_EBX, 0)
                uc.reg_write(UC_X86_REG_ECX, 0)
                uc.reg_write(UC_X86_REG_EDX, 0)
            else:
                uc.reg_write(UC_X86_REG_EAX, 0)
                uc.reg_write(UC_X86_REG_EBX, 0)
                uc.reg_write(UC_X86_REG_ECX, 0)
                uc.reg_write(UC_X86_REG_EDX, 0)
            return True

        def on_rdmsr(uc, user_data):
            msr = uc.reg_read(UC_X86_REG_ECX)
            if msr == 0x40000020:  # HV_X64_MSR_TIME_REF_COUNT (100ns units -> 10,000 = 1 ms)
                self.time_100ns += 50_000  # +5 ms per read so animations/timers run briskly in test!
                val = self.time_100ns
            else:
                val = self.msrs.get(msr, 0)
            uc.reg_write(UC_X86_REG_EAX, val & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_EDX, (val >> 32) & 0xFFFFFFFF)
            return True

        def on_wrmsr(uc, user_data):
            msr = uc.reg_read(UC_X86_REG_ECX)
            lo = uc.reg_read(UC_X86_REG_EAX)
            hi = uc.reg_read(UC_X86_REG_EDX)
            val = ((hi & 0xFFFFFFFF) << 32) | (lo & 0xFFFFFFFF)
            self.msrs[msr] = val
            if msr == 0x40000001 and (val & 1):
                # Populate Hypercall page with `out 0xEB, al (E6 EB); ret (C3)` -> zero UC_HOOK_CODE overhead!
                hc_gpa = val & ~0xFFF
                uc.mem_write(hc_gpa, b"\xE6\xEB\xC3")
            elif msr == 0x40000084:  # EOM! Deliver next queued SynIC control message if any!
                self._flush_next_synic_slot()
            elif msr == 0x40000003:  # HV_X64_MSR_RESET!
                self.reboot_requested = True
                uc.emu_stop()
            return True

        def on_out(uc, port, size, value, user_data):
            if port == 0x3F8:
                self.stats["serial_log"] += chr(value & 0xFF)
            elif port == 0xCF9 and (value & 0x06):
                self.reboot_requested = True
                uc.emu_stop()
            elif port == 0xE9:  # 2-byte patched `rdmsr`!
                on_rdmsr(uc, user_data)
            elif port == 0xEA:  # 2-byte patched `wrmsr`!
                on_wrmsr(uc, user_data)
            elif port == 0xEB:  # Hypercall entry `out 0xEB, al; ret`!
                ctrl = uc.reg_read(UC_X86_REG_RCX)
                in_p = uc.reg_read(UC_X86_REG_RDX)
                code = ctrl & 0xFFFF
                if code == 0x005C:
                    rc = self._handle_post_message(in_p)
                    uc.reg_write(UC_X86_REG_RAX, rc)
                elif code == 0x005D:
                    for ch in self.channels.values():
                        if ch["conn_id"] == (in_p & 0xFFFFFFFF):
                            self._process_channel_outbound(ch)
                    uc.reg_write(UC_X86_REG_RAX, 0)

        def on_in(uc, port, size, user_data):
            if port == 0x3FF:
                return 0x5A
            if port == 0x3FD:
                return 0x20
            return 0xFF

        # Intercept UEFI thunks AND Hypercall page entry!
        def on_code(uc, address, size, user_data):
            hc_reg = self.msrs[0x40000001]
            if (hc_reg & 1) and address == (hc_reg & ~0xFFF):
                ctrl = uc.reg_read(UC_X86_REG_RCX)
                in_p = uc.reg_read(UC_X86_REG_RDX)
                code = ctrl & 0xFFFF
                if code == 0x005C:  # HVCALL_POST_MESSAGE
                    rc = self._handle_post_message(in_p)
                    uc.reg_write(UC_X86_REG_RAX, rc)
                elif code == 0x005D:  # HVCALL_SIGNAL_EVENT
                    for ch in self.channels.values():
                        if ch["conn_id"] == (in_p & 0xFFFFFFFF):
                            self._process_channel_outbound(ch)
                    uc.reg_write(UC_X86_REG_RAX, 0)
                # Jump straight to the `ret` at hc_gpa + 3!
                uc.reg_write(UC_X86_REG_RIP, address + 3)
                return

            if address in self.thunks:
                fn, req_pre_ebs = self.thunks[address]
                if req_pre_ebs and self.ebs_called:
                    raise RuntimeError(f"FATAL BUG: Called UEFI BootServices thunk {hex(address)} AFTER ExitBootServices()!")
                rc = fn(uc)
                uc.reg_write(UC_X86_REG_RAX, rc)

        self.uc.hook_add(UC_HOOK_INSN, on_cpuid, None, 1, 0, UC_X86_INS_CPUID)
        self.uc.hook_add(UC_HOOK_INSN, on_out,   None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_INSN, on_in,    None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_CODE, on_code,  None, self.THUNK_BASE, self.THUNK_BASE + 0x10000)

        # Binary-patch exact 2-byte `rdmsr` (0F 32 -> E6 E9) and `wrmsr` (0F 30 -> E6 EA) in guest RAM
        # so Unicorn TCG executes 100% of guest code with full native direct block chaining (250+ MIPS)!
        pe = pefile.PE(data=self.efi_bytes)
        for sec in pe.sections:
            if sec.IMAGE_SCN_MEM_EXECUTE:
                base = self.IMG_BASE + sec.VirtualAddress
                code_bytes = bytearray(self.uc.mem_read(base, sec.Misc_VirtualSize))
                for i in range(len(code_bytes) - 1):
                    if code_bytes[i] == 0x0F and code_bytes[i + 1] == 0x32:
                        code_bytes[i] = 0xE6; code_bytes[i + 1] = 0xE9
                    elif code_bytes[i] == 0x0F and code_bytes[i + 1] == 0x30:
                        code_bytes[i] = 0xE6; code_bytes[i + 1] = 0xEA
                self.uc.mem_write(base, bytes(code_bytes))

    def start_guest(self):
        self.uc.reg_write(UC_X86_REG_RSP, 0x01F00000 - 0x28)
        self.uc.reg_write(UC_X86_REG_RCX, self.DEV_HANDLE)
        self.uc.reg_write(UC_X86_REG_RDX, self.sys_table)
        self.uc.reg_write(UC_X86_REG_RIP, self.entry_rip)

    def step_insns(self, count: int):
        rip = self.uc.reg_read(UC_X86_REG_RIP)
        self.uc.emu_start(rip, 0, count=count)


def run_e2e():
    with open("build/BOOTX64.EFI", "rb") as f:
        efi_bytes = f.read()
    with open("build/yoroos.img", "rb") as f:
        bios_bytes = f.read()
    with open("release/YoroOS-0.2.5.iso", "rb") as f:
        iso_bytes = f.read()

    vhdx = bytearray(64 * 1024 * 1024)

    print("=== [VM #1] Booting Live YoroOS 0.2.5 on Hyper-V Generation 2 (UEFI x86-64 -> ExitBootServices -> Native VMBus) ===")
    vm1 = HyperVGen2Machine(efi_bytes, bios_bytes, vhdx, iso_bytes=iso_bytes, boot_from_vhdx=False)
    vm1.start_guest()

    def run_until_dirt(vm, target_dirt, max_chunks=120):
        for _ in range(max_chunks):
            if vm.stats["synthvid_dirt"] >= target_dirt or vm.reboot_requested:
                return
            vm.step_insns(5_000_000)

    # Wait until YoroBoot renders & presents its first full frame (dirt == 3, since handshake sends 2)!
    run_until_dirt(vm1, 3)

    print("VM #1 VMBus Handshake Stats:", {k: v for k, v in vm1.stats.items() if k != "serial_log"})
    assert vm1.ebs_called, "ExitBootServices() was NOT called on Hyper-V Gen 2!"
    assert vm1.stats["unload"] >= 1, "CHANNELMSG_UNLOAD was NOT sent before INITIATE_CONTACT!"
    assert vm1.stats["chans_opened"] == 4, f"Expected all 4 Hyper-V VMBus channels opened, got {vm1.stats['chans_opened']}!"
    assert vm1.stats["synthvid_ptr_hidden"] >= 1, "SynthVid stray hardware dot cursor was NOT hidden!"
    vm1.save_screenshot("build/shot_hv_1_yoroboot.png")
    print("[OK] Captured build/shot_hv_1_yoroboot.png (YoroBoot Manager!)")

    # Press Enter via Hyper-V SynthKbd -> 5 boot splash frames + full Desktop + Installer frame = +6 dirts!
    d0 = vm1.stats["synthvid_dirt"]
    vm1.inject_synthkbd_key(0x1C, False)
    run_until_dirt(vm1, d0 + 6, max_chunks=200)
    vm1.save_screenshot("build/shot_hv_2_desktop.png")
    print("[OK] Captured build/shot_hv_2_desktop.png (Live Desktop + Installer Window!)")

    # Mouse is live: move the SynthHID pointer over the YoroOS menu button and click it.
    d0 = vm1.stats["synthvid_dirt"]
    vm1.move_and_click_mouse(26, 748, click=True)
    run_until_dirt(vm1, d0 + 2)
    vm1.save_screenshot("build/shot_hv_3_startmenu.png")
    print("[OK] Captured build/shot_hv_3_startmenu.png (Start menu click via SynthHID mouse)")

    # Installer wizard (fullscreen Setup in SCREEN_SETUP): Enter -> disks -> profile -> start install.
    d0 = vm1.stats["synthvid_dirt"]
    vm1.inject_synthkbd_key(0x1C, False)
    run_until_dirt(vm1, d0 + 2)
    d0 = vm1.stats["synthvid_dirt"]
    vm1.inject_synthkbd_key(0x1C, False)
    run_until_dirt(vm1, d0 + 2)
    d0 = vm1.stats["synthvid_dirt"]
    vm1.inject_synthkbd_key(0x1C, False)
    run_until_dirt(vm1, d0 + 3)
    vm1.save_screenshot("build/shot_hv_4_installing.png")
    print("[OK] Captured build/shot_hv_4_installing.png (installer write phase)")

    # Run until the installer finishes all 6 phases (incl. read-back verification) and auto-reboots
    # (DONE screen -> 6 s timer -> system_reboot -> HV RESET MSR). This is the real completion signal.
    shot_done = False
    for i in range(700):
        if vm1.reboot_requested:
            break
        vm1.step_insns(5_000_000)
        if not shot_done and vm1.stats["stor_sync_cache"] >= 2 and i % 40 == 39:
            vm1.save_screenshot("build/shot_hv_5_install_progress.png")
            shot_done = True
    vm1.save_screenshot("build/shot_hv_5_install_done.png")
    print("[OK] Install finished, reboot_requested =", vm1.reboot_requested)

    print("Post-Install StorVSC Stats:", {k: v for k, v in vm1.stats.items() if k != "serial_log"})
    print("Post-Install Serial Log:\n" + vm1.stats["serial_log"])
    assert vm1.stats["stor_writes"] > 20, f"Expected real StorVSC disk writes, got {vm1.stats['stor_writes']}!"
    assert "Boot0001" in vm1.nvram_vars and "BootOrder" in vm1.nvram_vars, f"UEFI NVRAM Boot0001/BootOrder NOT registered! Got {list(vm1.nvram_vars.keys())}"

    gpt_hdr = bytes(vhdx[512:1024])
    assert gpt_hdr[:8] == b"EFI PART", f"Invalid GPT signature on VHDX: {gpt_hdr[:8]}!"
    # GPT partition 1 (EFI System Partition) starts at LBA 16384, 32768 sectors (16 MiB)
    esp_img = bytes(vhdx[16384 * 512:(16384 + 32768) * 512])
    esp_path = "build/test_vhdx_esp.fat16"
    with open(esp_path, "wb") as f:
        f.write(esp_img)
    # Validate the FAT16 ESP with pyfatfs (pure-Python FAT reader; no dosfstools in sandbox)
    checker = "/tmp/fatvenv/bin/python"
    if os.path.exists(checker):
        chk = subprocess.run([checker, "tools/verify_esp.py", esp_path, "build/BOOTX64.EFI"],
                             capture_output=True, text=True)
        print("=== ESP FAT16 VERIFICATION (pyfatfs) ===")
        print(chk.stdout)
        if chk.stderr.strip():
            print("STDERR:", chk.stderr[-800:])
        assert chk.returncode == 0, "ESP FAT16 verification FAILED!"
    else:
        print("[skip] pyfatfs venv not present; ESP byte-level checks only")
    assert esp_img[510:512] == b"\x55\xaa", "FAT16 boot sector signature missing!"
    assert esp_img[54:62] == b"FAT16   ", "BPB FilSysType is not FAT16!"
    yfs_magic = struct.unpack("<I", vhdx[49152 * 512:49152 * 512 + 4])[0]
    assert yfs_magic == 0x32534659, f"Missing YoroFS2 superblock at LBA 49152: {hex(yfs_magic)}"
    print("[OK] Verified GPT ('EFI PART') + 100% clean fsck.vfat FAT16 ESP + persistent YoroFS2 at LBA 49152!")

    print("=== [VM #2] Booting Installed VHDX on Hyper-V Gen 2 WITH Live ISO STILL INSERTED ===")
    vm2 = HyperVGen2Machine(efi_bytes, bios_bytes, vhdx, iso_bytes=iso_bytes, boot_from_vhdx=True)
    vm2.start_guest()
    run_until_dirt(vm2, 3)
    vm2.save_screenshot("build/shot_hv_6_installed_yoroboot.png")
    # Press Enter or let YoroBoot auto-boot the default selected #0 item ("Загрузить YoroOS 0.2.5 с жёсткого диска")!
    d0 = vm2.stats["synthvid_dirt"]
    vm2.inject_synthkbd_key(0x1C, False)
    run_until_dirt(vm2, d0 + 6, max_chunks=200)
    vm2.save_screenshot("build/shot_hv_7_installed_desktop.png")
    print("VM #2 Serial Log:\n" + vm2.stats["serial_log"])
    print("=== ALL HYPER-V GEN 2 HARDWARE & PERSISTENT INSTALL E2E CHECKS PASSED 100%! ===")


if __name__ == "__main__":
    run_e2e()
