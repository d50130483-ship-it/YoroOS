/* YoroOS 0.2.9 - Native 64-bit UEFI & Bare-Metal Hyper-V Generation 2 HAL
 *
 * Dual-mode architecture for universal compatibility:
 *   Mode A — Real Microsoft Hyper-V Generation 2 ("Microsoft Hv"):
 *     1. Captures self \EFI\BOOT\BOOTX64.EFI + \YOROOS.IMG via SimpleFileSystem into RAM
 *     2. Allocates physical identity-mapped RAM + Hyper-V VMBus DMA arena
 *     3. Cleanly calls BS->GetMemoryMap() + BS->ExitBootServices()
 *     4. Initializes bare-metal 64-bit GDT/IDT + SynIC + VMBus + SynthVid + SynthHID + SynthKbd + StorVSC
 *     -> Seamless zero-capture host mouse cursor in vmconnect.exe + native VHDX I/O!
 *
 *   Mode B — Standard UEFI Systems (QEMU OVMF, VirtualBox EFI, VMware EFI, Physical PCs):
 *     Uses standard UEFI GOP + SimplePointer / AbsolutePointer + SimpleTextInputEx + BlockIo.
 */
#include "kernel.h"
#include "efi.h"
#include "hv_native.h"

int hv_mouse_init(EFI_HANDLE image_handle, EFI_BOOT_SERVICES *bs);
int hv_mouse_poll(u32 *last_seq, int screen_w, int screen_h,
                  int *out_x, int *out_y, int *out_btns, int *out_wheel);

static EFI_GUID gLoadedImageGuid = {
    0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};
static EFI_GUID gSimpleFsGuid = {
    0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
static EFI_GUID gEfiGlobalVarGuid = {
    0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}
};
static EFI_GUID gGopGuid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};
static EFI_GUID gMsGopOverrideGuid = {
    0xBE8EE323, 0x184C, 0x4E24, {0x8E, 0x18, 0x2E, 0x6D, 0xAD, 0xD7, 0x01, 0x60}
};
static EFI_GUID gSimpleTextInExGuid = {
    0xdd9e7534, 0x7762, 0x4698, {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}
};
static EFI_GUID gSimplePointerGuid = {
    0x31878c87, 0x0b75, 0x11d5, {0x9a, 0x4f, 0x00, 90, 0x27, 0x3f, 0xc1, 0x4d}
};
static EFI_GUID gAbsolutePointerGuid = {
    0x8D59D32B, 0xC655, 0x4AE9, {0x9B, 0x15, 0xF2, 0x59, 0x04, 0x99, 0x2A, 0x43}
};
static EFI_GUID gBlockIoGuid = {
    0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

EFI_HANDLE            g_efi_img = NULL;
EFI_SYSTEM_TABLE     *g_st = NULL;
EFI_BOOT_SERVICES    *g_bs = NULL;
EFI_RUNTIME_SERVICES *g_rt = NULL;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;
static int g_post_ebs = 0;

/* Self-contained boot payload buffers captured before ExitBootServices() */
static u8 *g_self_efi_buf = NULL;
static u32 g_self_efi_len = 0;
static u8 *g_bios_img_buf = NULL;
static u32 g_bios_img_len = 0;
static int g_booted_from_installed_disk = 0;

/* Boot wall-clock snapshot for zero-UEFI-call timekeeping after ExitBootServices() */
static EFI_TIME g_boot_time;
static u64      g_boot_hv_100ns = 0;
static int      g_boot_time_valid = 0;

/* Serial COM1 debug helper */
static int g_uart_ok = 0;
static inline void outb64(u16 port, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static inline u8   inb64(u16 port) { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }

static void serial_probe(void) {
    outb64(0x3F8 + 7, 0x5A);
    if (inb64(0x3F8 + 7) == 0x5A) {
        outb64(0x3F8 + 1, 0x00);
        outb64(0x3F8 + 3, 0x80);
        outb64(0x3F8 + 0, 0x01);
        outb64(0x3F8 + 1, 0x00);
        outb64(0x3F8 + 3, 0x03);
        g_uart_ok = 1;
    }
}

void serial_putc(char c) {
    if (!g_uart_ok) return;
    if (c == '\n') outb64(0x3F8, '\r');
    outb64(0x3F8, (u8)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

void klog(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_puts(buf);
    serial_putc('\n');
}

/* ---------------- CPU info & RAM ---------------- */
static char g_cpu_brand[64] = "x86_64 Virtual / Physical Processor";
static u64  g_ram_mb = 2048;

static void detect_cpu(void) {
    u32 ebx, ecx, edx, max_ext = 0;
    __asm__ volatile("cpuid" : "=a"(max_ext), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000U));
    if (max_ext >= 0x80000004U) {
        u32 *p = (u32 *)g_cpu_brand;
        u32 leaf;
        for (leaf = 0x80000002U; leaf <= 0x80000004U; leaf++) {
            __asm__ volatile("cpuid" : "=a"(p[0]), "=b"(p[1]), "=c"(p[2]), "=d"(p[3]) : "a"(leaf));
            p += 4;
        }
        g_cpu_brand[48] = 0;
        {
            char *s = g_cpu_brand;
            while (*s == ' ') s++;
            if (s != g_cpu_brand) memmove(g_cpu_brand, s, strlen(s) + 1);
        }
    }
}

const char *hal_arch_name(void) { return "x86_64 (UEFI Gen 2)"; }
const char *hal_cpu_brand(void) { return g_cpu_brand; }
u64 hal_ram_mb(void) { return g_ram_mb; }
int hal_is_uefi(void) { return 1; }
const u8 *hal_self_efi_bytes(u32 *out_len) { if (out_len) *out_len = g_self_efi_len; return g_self_efi_buf; }
const u8 *hal_bios_img_bytes(u32 *out_len) { if (out_len) *out_len = g_bios_img_len; return g_bios_img_buf; }

/* ---------------- Event Queue ---------------- */
#define EVQ_SIZE 256
static YoroEvent evq[EVQ_SIZE];
static int evq_head = 0, evq_tail = 0;

void input_push(const YoroEvent *e) {
    int nx = (evq_tail + 1) & (EVQ_SIZE - 1);
    if (nx == evq_head) return;
    evq[evq_tail] = *e;
    evq_tail = nx;
}

int input_pop(YoroEvent *e) {
    if (evq_head == evq_tail) return 0;
    *e = evq[evq_head];
    evq_head = (evq_head + 1) & (EVQ_SIZE - 1);
    return 1;
}

/* ---------------- Timer & RTC ---------------- */
static u32 g_ticks = 1;
static EFI_EVENT g_timer_ev = NULL;

static void init_timer(void) {
    if (!g_post_ebs && g_bs) {
        if (!EFI_ERROR(g_bs->CreateEvent(0x80000000U /* EVT_TIMER */, 0, NULL, NULL, &g_timer_ev)))
            g_bs->SetTimer(g_timer_ev, 1 /* TimerPeriodic */, 100000ULL);
    }
}

u32 sys_ticks(void) {
    if (g_post_ebs) {
        u64 elapsed_10ms = (hv_time_ref_100ns() - g_boot_hv_100ns) / 100000ULL;
        return (u32)(elapsed_10ms + 1ULL);
    }
    if (g_timer_ev && g_bs && !EFI_ERROR(g_bs->CheckEvent(g_timer_ev)))
        g_ticks++;
    return g_ticks;
}

void rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year) {
    EFI_TIME t;
    if (!g_post_ebs && g_rt && !EFI_ERROR(g_rt->GetTime(&t, NULL))) {
        int h = (int)t.Hour + sys_profile.tz_offset;
        while (h >= 24) h -= 24;
        while (h < 0) h += 24;
        *hour = h; *min = t.Minute; *sec = t.Second;
        *day = t.Day; *mon = t.Month; *year = t.Year;
        return;
    }
    if (g_boot_time_valid) {
        u64 add_sec = (hv_time_ref_100ns() - g_boot_hv_100ns) / 10000000ULL;
        u64 total_s = (u64)g_boot_time.Hour * 3600ULL + (u64)g_boot_time.Minute * 60ULL
                    + (u64)g_boot_time.Second + add_sec;
        int h = (int)((total_s / 3600ULL) % 24ULL) + sys_profile.tz_offset;
        while (h >= 24) h -= 24;
        while (h < 0) h += 24;
        *hour = h;
        *min  = (int)((total_s / 60ULL) % 60ULL);
        *sec  = (int)(total_s % 60ULL);
        *day  = g_boot_time.Day;
        *mon  = g_boot_time.Month;
        *year = g_boot_time.Year;
        return;
    }
    *hour = 16; *min = 30; *sec = (int)((sys_ticks() / 100U) % 60U);
    *day = 23; *mon = 10; *year = 2026;
}

void hal_beep(u32 freq_hz, u32 dur_ms) {
    UNUSED(freq_hz);
    if (dur_ms > 60) dur_ms = 60;
    if (g_post_ebs) hv_delay_us(dur_ms * 100U);
    else if (g_bs) g_bs->Stall((UINTN)dur_ms * 100ULL);
}

/* ---------------- GOP Video + Bare-Metal SynthVid Present ---------------- */
#define MAX_VMODES 24
static HalVideoMode g_vmodes[MAX_VMODES];
static int g_vmode_n = 0;
static u64 g_gop_fb_gpa = 0;

static void sync_fb_from_gop(void) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = g_gop->Mode->Info;
    fb.width = info->HorizontalResolution;
    fb.height = info->VerticalResolution;
    fb.pitch = info->PixelsPerScanLine * 4U;
    fb.bstride = fb.width * 4U;
    fb.bpp = 32;
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        fb.red_pos = 0; fb.red_size = 8;
        fb.green_pos = 8; fb.green_size = 8;
        fb.blue_pos = 16; fb.blue_size = 8;
    } else {
        fb.red_pos = 16; fb.red_size = 8;
        fb.green_pos = 8; fb.green_size = 8;
        fb.blue_pos = 0; fb.blue_size = 8;
    }
    g_gop_fb_gpa = (u64)g_gop->Mode->FrameBufferBase;
    fb.vram = (u8 *)(uptr)g_gop_fb_gpa;
    fb.dual = 1;
}

static int init_gop(void) {
    EFI_HANDLE *handles = NULL;
    UINTN nh = 0, i;
    int best_mode = -1;
    i64 best_score = -1;

    if (!EFI_ERROR(g_bs->HandleProtocol(g_st->ConsoleOutHandle, &gGopGuid, (void **)&g_gop)) && g_gop) {
        /* ok */
    } else if (!EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gGopGuid, NULL, &nh, &handles)) && nh > 0) {
        for (i = 0; i < nh; i++) {
            EFI_GRAPHICS_OUTPUT_PROTOCOL *cand = NULL;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &gGopGuid, (void **)&cand)) && cand && cand->Mode) {
                g_gop = cand;
                if (cand->Mode->FrameBufferBase != 0) break;
            }
        }
        g_bs->FreePool(handles);
    }
    if (!g_gop) (void)g_bs->LocateProtocol(&gGopGuid, NULL, (void **)&g_gop);
    if (!g_gop) (void)g_bs->LocateProtocol(&gMsGopOverrideGuid, NULL, (void **)&g_gop);
    if (!g_gop || !g_gop->Mode) return -1;

    g_vmode_n = 0;
    for (i = 0; i < g_gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *inf = NULL;
        UINTN sz = 0;
        if (EFI_ERROR(g_gop->QueryMode(g_gop, (u32)i, &sz, &inf)) || !inf) continue;
        if (inf->HorizontalResolution >= 800 && inf->VerticalResolution >= 600) {
            if (g_vmode_n < MAX_VMODES) {
                g_vmodes[g_vmode_n].mode_id = (int)i;
                g_vmodes[g_vmode_n].width = inf->HorizontalResolution;
                g_vmodes[g_vmode_n].height = inf->VerticalResolution;
                g_vmodes[g_vmode_n].bpp = 32;
                g_vmode_n++;
            }
            {
                i64 w = inf->HorizontalResolution, h = inf->VerticalResolution;
                i64 score = 0;
                if (w == 1280 && h == 800) score = 1000000;
                else if (w == 1280 && h == 720) score = 960000;
                else if (w == 1024 && h == 768) score = 940000;
                else if (w == 1152 && h == 864) score = 910000;
                else if (w <= 1440 && h <= 900) score = w * h / 4;
                else score = 200000 - (w - 1440);
                if (score > best_score) { best_score = score; best_mode = (int)i; }
            }
        }
        g_bs->FreePool(inf);
    }

    if (best_mode >= 0 && (u32)best_mode != g_gop->Mode->Mode)
        g_gop->SetMode(g_gop, (u32)best_mode);

    sync_fb_from_gop();
    klog("[gop] Video %ux%u pitch=%u vram=0x%llx modes=%d",
         fb.width, fb.height, fb.pitch, (unsigned long long)g_gop_fb_gpa, g_vmode_n);
    return 0;
}

int hal_video_mode_count(void) { return g_vmode_n; }

int hal_video_mode_get(int idx, HalVideoMode *out) {
    if (idx < 0 || idx >= g_vmode_n) return -1;
    *out = g_vmodes[idx];
    return 0;
}

int hal_video_set_mode(int mode_id) {
    int i;
    if (g_post_ebs) {
        /* Switch resolution directly through Hyper-V VMBus SynthVid SITUATION_UPDATE! */
        for (i = 0; i < g_vmode_n; i++) {
            if (g_vmodes[i].mode_id == mode_id && hv_video_ready()) {
                u32 nw = g_vmodes[i].width, nh = g_vmodes[i].height;
                if ((u64)nw * (u64)nh * 4ULL <= 8ULL * 1024ULL * 1024ULL) {
                    fb.width = nw;
                    fb.height = nh;
                    fb.pitch = nw * 4U;
                    fb.bstride = nw * 4U;
                    hv_synthvid_set_mode(g_gop_fb_gpa, nw, nh, nw * 4U);
                    gfx_realloc_backbuffer();
                    desktop_set_wall_cache_dirty();
                    wm_invalidate();
                    return 0;
                }
            }
        }
        return -1;
    }
    if (!g_gop) return -1;
    if (EFI_ERROR(g_gop->SetMode(g_gop, (u32)mode_id))) return -1;
    sync_fb_from_gop();
    gfx_realloc_backbuffer();
    desktop_set_wall_cache_dirty();
    {
        YoroEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_RESIZE;
        input_push(&ev);
    }
    wm_invalidate();
    return 0;
}

void hal_present_rect(Rect r) {
    if (!fb.back) return;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > (int)fb.width) r.w = (int)fb.width - r.x;
    if (r.y + r.h > (int)fb.height) r.h = (int)fb.height - r.y;
    if (r.w <= 0 || r.h <= 0) return;

    if (!g_post_ebs && g_gop && g_gop->Blt) {
        EFI_STATUS st = g_gop->Blt(
            g_gop,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)fb.back,
            EfiBltBufferToVideo,
            (UINTN)r.x, (UINTN)r.y,
            (UINTN)r.x, (UINTN)r.y,
            (UINTN)r.w, (UINTN)r.h,
            (UINTN)fb.bstride
        );
        if (!EFI_ERROR(st)) return;
    }
    if (fb.vram) {
        int y;
        for (y = r.y; y < r.y + r.h; y++) {
            memcpy(fb.vram + (size_t)y * fb.pitch + (size_t)r.x * 4U,
                   fb.back + (size_t)y * fb.bstride + (size_t)r.x * 4U,
                   (size_t)r.w * 4U);
        }
        if (g_post_ebs && hv_video_ready())
            hv_synthvid_dirt(r.x, r.y, r.x + r.w, r.y + r.h);
    }
}

void hal_present(void) {
    hal_present_rect((Rect){0, 0, (int)fb.width, (int)fb.height});
}

/* ---------------- Mouse & Keyboard Unified Input ---------------- */
#define MAX_PTR_DEVS 8
static EFI_ABSOLUTE_POINTER_PROTOCOL *g_abs_ptrs[MAX_PTR_DEVS];
static int g_abs_n = 0;
static EFI_SIMPLE_POINTER_PROTOCOL *g_rel_ptrs[MAX_PTR_DEVS];
static int g_rel_n = 0;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *g_key_ex[MAX_PTR_DEVS];
static int g_key_ex_n = 0;

static int g_hv_emcl_ok = 0;
static u32 g_hv_last_seq = 0;
static int g_mx = 512, g_my = 384, g_mbtns = 0;
static int g_ru = 0;
static int g_mousekeys = 0;
static char g_ptr_name[80] = "UEFI Pointer";

const char *hal_pointer_driver_name(void) { return g_ptr_name; }
int mouse_get_x(void) { return g_mx; }
int mouse_get_y(void) { return g_my; }
int mouse_state(void) { return g_mbtns; }
void mouse_set_pos(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)fb.width) x = (int)fb.width - 1;
    if (y >= (int)fb.height) y = (int)fb.height - 1;
    g_mx = x; g_my = y;
}

int kbd_is_ru(void) { return g_ru; }
void kbd_set_ru(int ru) { g_ru = ru ? 1 : 0; wm_invalidate(); }
void kbd_toggle_layout(void) { g_ru = !g_ru; toast(g_ru ? "Раскладка: Русская (RU)" : "Layout: English (EN)"); wm_invalidate(); }

static void init_uefi_input_devices(void) {
    EFI_HANDLE *handles = NULL;
    UINTN nh = 0, i;

    g_mx = (int)fb.width / 2;
    g_my = (int)fb.height / 2;

    g_hv_emcl_ok = hv_mouse_init(g_efi_img, g_bs);

    if (!EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gAbsolutePointerGuid, NULL, &nh, &handles)) && handles) {
        for (i = 0; i < nh && g_abs_n < MAX_PTR_DEVS; i++) {
            EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &gAbsolutePointerGuid, (void **)&ap)) && ap) {
                ap->Reset(ap, 0);
                g_abs_ptrs[g_abs_n++] = ap;
            }
        }
        g_bs->FreePool(handles);
        handles = NULL;
    }

    nh = 0;
    if (!EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gSimplePointerGuid, NULL, &nh, &handles)) && handles) {
        for (i = 0; i < nh && g_rel_n < MAX_PTR_DEVS; i++) {
            EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &gSimplePointerGuid, (void **)&sp)) && sp) {
                sp->Reset(sp, 0);
                g_rel_ptrs[g_rel_n++] = sp;
            }
        }
        g_bs->FreePool(handles);
        handles = NULL;
    }

    nh = 0;
    if (!EFI_ERROR(g_bs->HandleProtocol(g_st->ConsoleInHandle, &gSimpleTextInExGuid, (void **)&g_key_ex[0])) && g_key_ex[0]) {
        g_key_ex_n = 1;
    } else if (!EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gSimpleTextInExGuid, NULL, &nh, &handles)) && handles) {
        for (i = 0; i < nh && g_key_ex_n < MAX_PTR_DEVS; i++) {
            EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *kx = NULL;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &gSimpleTextInExGuid, (void **)&kx)) && kx)
                g_key_ex[g_key_ex_n++] = kx;
        }
        g_bs->FreePool(handles);
    }

    if (g_hv_emcl_ok) strcpy(g_ptr_name, "Hyper-V EMCL Mouse");
    else if (g_abs_n > 0) strcpy(g_ptr_name, "UEFI Absolute HID Tablet");
    else if (g_rel_n > 0) strcpy(g_ptr_name, "UEFI SimplePointer Mouse");
    else strcpy(g_ptr_name, "Keyboard + F8 MouseKeys");
}

static void emit_pointer_state(int nx, int ny, int nbtns, int wheel) {
    YoroEvent e;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= (int)fb.width) nx = (int)fb.width - 1;
    if (ny >= (int)fb.height) ny = (int)fb.height - 1;

    if (nx != g_mx || ny != g_my) {
        g_mx = nx; g_my = ny;
        memset(&e, 0, sizeof(e));
        e.type = EV_MOUSE_MOVE;
        e.x = g_mx; e.y = g_my;
        input_push(&e);
    }
    if ((nbtns & 1) != (g_mbtns & 1)) {
        memset(&e, 0, sizeof(e));
        e.type = (nbtns & 1) ? EV_MOUSE_DOWN : EV_MOUSE_UP;
        e.btn = 1; e.x = g_mx; e.y = g_my;
        input_push(&e);
    }
    if ((nbtns & 2) != (g_mbtns & 2)) {
        memset(&e, 0, sizeof(e));
        e.type = (nbtns & 2) ? EV_MOUSE_DOWN : EV_MOUSE_UP;
        e.btn = 2; e.x = g_mx; e.y = g_my;
        input_push(&e);
    }
    g_mbtns = nbtns;
    if (wheel != 0) {
        memset(&e, 0, sizeof(e));
        e.type = EV_WHEEL;
        e.wheel = wheel > 0 ? 1 : -1;
        e.x = g_mx; e.y = g_my;
        input_push(&e);
    }
}

static u16 qwerty_to_ru(u16 ch) {
    static const char *en = "qwertyuiop[]asdfghjkl;'zxcvbnm,.`QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>~";
    static const u16 ru[] = {
        0x0439,0x0446,0x0443,0x043A,0x0435,0x043D,0x0433,0x0448,0x0449,0x0437,0x0445,0x044A,
        0x0444,0x044B,0x0432,0x0430,0x043F,0x0440,0x043E,0x043B,0x0434,0x0436,0x044D,
        0x044F,0x0447,0x0441,0x043C,0x0438,0x0442,0x044C,0x0431,0x044E,0x0451,
        0x0419,0x0426,0x0423,0x041A,0x0415,0x041D,0x0413,0x0428,0x0429,0x0417,0x0425,0x042A,
        0x0424,0x042B,0x0412,0x0410,0x041F,0x0420,0x041E,0x041B,0x0414,0x0416,0x042D,
        0x042F,0x0427,0x0421,0x041C,0x0418,0x0422,0x042C,0x0411,0x042E,0x0401
    };
    int i;
    for (i = 0; en[i]; i++)
        if ((u16)(u8)en[i] == ch) return ru[i];
    return ch;
}

static void process_efi_key(u16 scan, u16 uni, u32 shift_state) {
    YoroEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_KEY;
    if (shift_state & EFI_SHIFT_STATE_VALID) {
        if (shift_state & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) e.shift = 1;
        if (shift_state & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) e.ctrl = 1;
        if (shift_state & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) e.alt = 1;
        if ((shift_state & (EFI_LEFT_LOGO_PRESSED | EFI_RIGHT_LOGO_PRESSED)) && scan == 0 && uni == 0) {
            e.key = KEY_WIN;
            input_push(&e);
            return;
        }
        if (scan == 0 && uni == 0 && e.shift && (e.alt || e.ctrl)) {
            kbd_toggle_layout();
            return;
        }
    }

    switch (scan) {
    case 0x01: e.key = KEY_UP; break;
    case 0x02: e.key = KEY_DOWN; break;
    case 0x03: e.key = KEY_RIGHT; break;
    case 0x04: e.key = KEY_LEFT; break;
    case 0x05: e.key = KEY_HOME; break;
    case 0x06: e.key = KEY_END; break;
    case 0x08: e.key = KEY_DEL; break;
    case 0x09: e.key = KEY_PGUP; break;
    case 0x0A: e.key = KEY_PGDN; break;
    case 0x0B: e.key = KEY_F1; break;
    case 0x0C: e.key = KEY_F2; break;
    case 0x0D: e.key = KEY_F3; break;
    case 0x0E: e.key = KEY_F4; break;
    case 0x0F: e.key = KEY_F5; break;
    case 0x10: e.key = KEY_F6; break;
    case 0x11: e.key = KEY_F7; break;
    case 0x12:
        g_mousekeys = !g_mousekeys;
        toast(g_mousekeys ? "Управление курсором со стрелок (F8): ВКЛ" : "Управление курсором со стрелок (F8): ВЫКЛ");
        return;
    case 0x13: e.key = KEY_F9; break;
    case 0x14: e.key = KEY_F10; break;
    case 0x15: e.key = KEY_F11; break;
    case 0x16: e.key = KEY_F12; break;
    case 0x17: e.key = KEY_ESC; break;
    default: break;
    }

    if (g_mousekeys) {
        int step = e.shift ? 32 : 14;
        if (e.key == KEY_UP)    { emit_pointer_state(g_mx, g_my - step, g_mbtns, 0); return; }
        if (e.key == KEY_DOWN)  { emit_pointer_state(g_mx, g_my + step, g_mbtns, 0); return; }
        if (e.key == KEY_LEFT)  { emit_pointer_state(g_mx - step, g_my, g_mbtns, 0); return; }
        if (e.key == KEY_RIGHT) { emit_pointer_state(g_mx + step, g_my, g_mbtns, 0); return; }
        if (uni == '\r' || uni == ' ') {
            emit_pointer_state(g_mx, g_my, 1, 0);
            emit_pointer_state(g_mx, g_my, 0, 0);
            return;
        }
    }

    if (e.key == 0 && uni != 0) {
        if (uni >= 1 && uni <= 26 && uni != 8 && uni != 9 && uni != 13) {
            e.ctrl = 1;
            e.key = 'a' + (uni - 1);
            e.ch = e.key;
        } else if (uni == 8 || uni == 0x7F) {
            e.key = KEY_BACKSP;
        } else if (uni == 9) {
            e.key = KEY_TAB;
        } else if (uni == 13 || uni == 10) {
            e.key = KEY_ENTER;
        } else if (uni == 27) {
            e.key = KEY_ESC;
        } else {
            u16 mapped = (g_ru && uni < 0x80 && !e.ctrl && !e.alt) ? qwerty_to_ru(uni) : uni;
            e.key = (uni < 0x80) ? (int)uni : 0;
            e.ch = (int)mapped;
        }
    }

    if (e.key != 0 || e.ch != 0)
        input_push(&e);
}

void hal_poll_input(void) {
    int i, hx, hy, hb, hw;

    if (g_post_ebs) {
        hv_poll();
        return;
    }

    sys_ticks();

    while (hv_mouse_poll(&g_hv_last_seq, (int)fb.width, (int)fb.height, &hx, &hy, &hb, &hw))
        emit_pointer_state(hx, hy, hb, hw);

    for (i = 0; i < g_abs_n; i++) {
        EFI_ABSOLUTE_POINTER_STATE st;
        EFI_ABSOLUTE_POINTER_PROTOCOL *ap = g_abs_ptrs[i];
        if (!EFI_ERROR(ap->GetState(ap, &st))) {
            u64 maxx = ap->Mode->AbsoluteMaxX > ap->Mode->AbsoluteMinX ? (ap->Mode->AbsoluteMaxX - ap->Mode->AbsoluteMinX) : 32767ULL;
            u64 maxy = ap->Mode->AbsoluteMaxY > ap->Mode->AbsoluteMinY ? (ap->Mode->AbsoluteMaxY - ap->Mode->AbsoluteMinY) : 32767ULL;
            int nx = (int)(((st.CurrentX - ap->Mode->AbsoluteMinX) * (u64)(fb.width - 1)) / maxx);
            int ny = (int)(((st.CurrentY - ap->Mode->AbsoluteMinY) * (u64)(fb.height - 1)) / maxy);
            int nb = ((st.ActiveButtons & 1) ? 1 : 0) | ((st.ActiveButtons & 2) ? 2 : 0);
            emit_pointer_state(nx, ny, nb, 0);
        }
    }

    for (i = 0; i < g_rel_n; i++) {
        EFI_SIMPLE_POINTER_STATE st;
        EFI_SIMPLE_POINTER_PROTOCOL *sp = g_rel_ptrs[i];
        if (!EFI_ERROR(sp->GetState(sp, &st))) {
            i64 rx = (i64)sp->Mode->ResolutionX ? (i64)sp->Mode->ResolutionX : 8192;
            i64 ry = (i64)sp->Mode->ResolutionY ? (i64)sp->Mode->ResolutionY : 8192;
            int dx = (int)((st.RelativeMovementX * 4) / (rx > 256 ? (rx / 64) : 4));
            int dy = (int)((st.RelativeMovementY * 4) / (ry > 256 ? (ry / 64) : 4));
            int nb = (st.LeftButton ? 1 : 0) | (st.RightButton ? 2 : 0);
            if (dx == 0 && st.RelativeMovementX != 0) dx = st.RelativeMovementX > 0 ? 2 : -2;
            if (dy == 0 && st.RelativeMovementY != 0) dy = st.RelativeMovementY > 0 ? 2 : -2;
            emit_pointer_state(g_mx + dx, g_my + dy, nb, st.RelativeMovementZ);
        }
    }

    if (g_key_ex_n > 0) {
        EFI_KEY_DATA kd;
        while (!EFI_ERROR(g_key_ex[0]->ReadKeyStrokeEx(g_key_ex[0], &kd)))
            process_efi_key(kd.Key.ScanCode, kd.Key.UnicodeChar, kd.KeyState.KeyShiftState);
    } else if (g_st && g_st->ConIn) {
        EFI_INPUT_KEY k;
        while (!EFI_ERROR(g_st->ConIn->ReadKeyStroke(g_st->ConIn, &k)))
            process_efi_key(k.ScanCode, k.UnicodeChar, 0);
    }
}

void hal_idle(void) {
    if (g_post_ebs) hv_delay_us(2000);
    else if (g_bs) g_bs->Stall(2500);
}

/* ---------------- Boot Media Snapshot + UEFI NVRAM Boot Entry Registration ---------------- */
static u8 *read_efi_file_from_root(EFI_FILE_PROTOCOL *root, CHAR16 *path, u32 max_bytes, u32 *out_len) {
    EFI_FILE_PROTOCOL *f = NULL;
    u8 *buf;
    UINTN sz = max_bytes;
    *out_len = 0;
    if (!root || EFI_ERROR(root->Open(root, &f, path, EFI_FILE_MODE_READ, 0)) || !f)
        return NULL;
    buf = (u8 *)kmalloc(max_bytes);
    if (!buf) { f->Close(f); return NULL; }
    if (EFI_ERROR(f->Read(f, &sz, buf)) || sz == 0) {
        kfree(buf);
        f->Close(f);
        return NULL;
    }
    f->Close(f);
    *out_len = (u32)sz;
    return buf;
}

static void capture_boot_payloads_before_ebs(void) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_HANDLE *fs_handles = NULL;
    UINTN nfs = 0, i;

    /* 1. Try boot volume first */
    if (!EFI_ERROR(g_bs->HandleProtocol(g_efi_img, &gLoadedImageGuid, (void **)&loaded)) &&
        loaded && loaded->DeviceHandle) {
        if (!EFI_ERROR(g_bs->HandleProtocol(loaded->DeviceHandle, &gSimpleFsGuid, (void **)&sfs)) && sfs) {
            if (!EFI_ERROR(sfs->OpenVolume(sfs, &root)) && root) {
                u32 tag_len = 0;
                u8 *tag = read_efi_file_from_root(root, (CHAR16 *)L"\\EFI\\YOROOS\\INSTALLED.TAG", 256, &tag_len);
                if (tag) {
                    g_booted_from_installed_disk = 1;
                    kfree(tag);
                }
                g_self_efi_buf = read_efi_file_from_root(root, (CHAR16 *)L"\\EFI\\BOOT\\BOOTX64.EFI",
                                                         6U * 1024U * 1024U, &g_self_efi_len);
                g_bios_img_buf = read_efi_file_from_root(root, (CHAR16 *)L"\\YOROOS.IMG",
                                                         6U * 1024U * 1024U, &g_bios_img_len);
                root->Close(root);
            }
        }
    }

    /* 2. Scan all other SimpleFileSystem volumes if YOROOS.IMG or BOOTX64.EFI wasn't on boot partition */
    if ((!g_self_efi_buf || !g_bios_img_buf) &&
        !EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gSimpleFsGuid, NULL, &nfs, &fs_handles)) && fs_handles) {
        for (i = 0; i < nfs; i++) {
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs2 = NULL;
            EFI_FILE_PROTOCOL *r2 = NULL;
            if (EFI_ERROR(g_bs->HandleProtocol(fs_handles[i], &gSimpleFsGuid, (void **)&fs2)) || !fs2) continue;
            if (EFI_ERROR(fs2->OpenVolume(fs2, &r2)) || !r2) continue;
            if (!g_self_efi_buf)
                g_self_efi_buf = read_efi_file_from_root(r2, (CHAR16 *)L"\\EFI\\BOOT\\BOOTX64.EFI",
                                                         6U * 1024U * 1024U, &g_self_efi_len);
            if (!g_bios_img_buf)
                g_bios_img_buf = read_efi_file_from_root(r2, (CHAR16 *)L"\\YOROOS.IMG",
                                                         6U * 1024U * 1024U, &g_bios_img_len);
            r2->Close(r2);
        }
        g_bs->FreePool(fs_handles);
    }

    klog("[boot] Self EFI=%u bytes, BIOS img=%u bytes, installed_tag=%d",
         g_self_efi_len, g_bios_img_len, g_booted_from_installed_disk);
}

int hal_register_nvram_boot(const u8 part_guid[16], u64 esp_start_lba, u64 esp_sectors) {
    /* Build standard UEFI 2.10 EFI_LOAD_OPTION ("Boot0001" = "YoroOS") + prepend BootOrder:
     *   u32 Attributes = 0x00000001 (LOAD_OPTION_ACTIVE)
     *   u16 FilePathListLength = 42 (HardDrive DP) + 52 (FilePath DP) + 4 (End DP) = 98
     *   CHAR16 Description[] = L"YoroOS\0" (14 bytes)
     *   HardDrive Media Device Path (Type 4, SubType 1, Length 42):
     *     u32 PartNum=1, u64 StartLBA, u64 SizeLBA, u8 Signature[16], u8 MBRType=2(GPT), u8 SigType=2(GUID)
     *   FilePath Media Device Path (Type 4, SubType 4, Length 52):
     *     CHAR16 PathName[] = L"\\EFI\\BOOT\\BOOTX64.EFI\0" (24 chars = 48 bytes + 4 header = 52 bytes)
     *   End Entire Device Path (Type 0x7F, SubType 0xFF, Length 4)
     */
    static const CHAR16 kBootPath[24] = {
        '\\','E','F','I','\\','B','O','O','T','\\','B','O','O','T','X','6','4','.','E','F','I', 0, 0, 0
    };
    u8 opt[256];
    u8 *p = opt;
    u16 fp_len = 42U + 48U + 4U; /* 94 bytes */
    u16 order[16];
    UINTN ord_bytes = sizeof(order);
    u32 attrs = 0x00000007U; /* NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS */
    int n_ord = 0, i, out_n = 0;
    u16 new_order[17];

    if (!g_rt || !g_rt->SetVariable) return -1;

    memset(opt, 0, sizeof(opt));
    *(u32 *)p = 0x00000001U; p += 4;
    *(u16 *)p = fp_len;      p += 2;
    /* Description: L"YoroOS\0" */
    *(u16 *)p = 'Y'; p += 2; *(u16 *)p = 'o'; p += 2; *(u16 *)p = 'r'; p += 2;
    *(u16 *)p = 'o'; p += 2; *(u16 *)p = 'O'; p += 2; *(u16 *)p = 'S'; p += 2;
    *(u16 *)p = 0;   p += 2;

    /* Node 1: HardDrive Media Device Path (42 bytes) */
    p[0] = 4; p[1] = 1; *(u16 *)(p + 2) = 42;
    *(u32 *)(p + 4) = 1U;
    *(u64 *)(p + 8) = esp_start_lba;
    *(u64 *)(p + 16) = esp_sectors;
    memcpy(p + 24, part_guid, 16);
    p[40] = 2; /* GPT */
    p[41] = 2; /* GUID signature */
    p += 42;

    /* Node 2: FilePath Media Device Path (4 + 22*2 = 48 bytes) */
    p[0] = 4; p[1] = 4; *(u16 *)(p + 2) = 48;
    memcpy(p + 4, kBootPath, 44);
    p += 48;

    /* Node 3: End of Hardware Device Path (4 bytes) */
    p[0] = 0x7F; p[1] = 0xFF; *(u16 *)(p + 2) = 4;
    p += 4;

    if (EFI_ERROR(g_rt->SetVariable((CHAR16 *)L"Boot0001", &gEfiGlobalVarGuid, attrs, (UINTN)(p - opt), opt)))
        return -1;

    if (!EFI_ERROR(g_rt->GetVariable((CHAR16 *)L"BootOrder", &gEfiGlobalVarGuid, NULL, &ord_bytes, order)))
        n_ord = (int)(ord_bytes / 2U);

    new_order[out_n++] = 0x0001U;
    for (i = 0; i < n_ord && out_n < 16; i++) {
        if (order[i] != 0x0001U)
            new_order[out_n++] = order[i];
    }
    (void)g_rt->SetVariable((CHAR16 *)L"BootOrder", &gEfiGlobalVarGuid, attrs, (UINTN)(out_n * 2), new_order);
    return 0;
}

/* ---------------- Storage Layer (Native Hyper-V StorVSC OR UEFI BlockIo) ---------------- */
#define MAX_DISKS 8
typedef struct {
    EFI_BLOCK_IO_PROTOCOL *bio;
    int hv_idx;
    HalDiskInfo info;
} UefiDisk;

static UefiDisk g_disks[MAX_DISKS];
static int g_disk_n = 0;
static EFI_BLOCK_IO_PROTOCOL *g_iso_bio = NULL;
static int g_hv_iso_idx = -1;
static u8 *g_io_bounce = NULL;

static int bio_read_sectors512(EFI_BLOCK_IO_PROTOCOL *bio, u64 lba512, u32 count512, void *buf) {
    u32 bsz, spb;
    u8 *dst = (u8 *)buf;
    if (!bio || !bio->Media || !g_io_bounce) return -1;
    bsz = bio->Media->BlockSize;
    if (bsz < 512) return -1;
    spb = bsz / 512U;
    while (count512 > 0) {
        if (bsz == 512) {
            u32 chunk = count512 > 64 ? 64 : count512;
            if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, lba512, (UINTN)chunk * 512U, g_io_bounce)))
                return -1;
            memcpy(dst, g_io_bounce, (size_t)chunk * 512U);
            dst += (size_t)chunk * 512U;
            lba512 += chunk;
            count512 -= chunk;
        } else {
            u64 phys_lba = lba512 / spb;
            u32 sub = (u32)(lba512 % spb);
            u32 avail = spb - sub;
            u32 take = count512 < avail ? count512 : avail;
            if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, phys_lba, bsz, g_io_bounce)))
                return -1;
            memcpy(dst, g_io_bounce + (size_t)sub * 512U, (size_t)take * 512U);
            dst += (size_t)take * 512U;
            lba512 += take;
            count512 -= take;
        }
    }
    return 0;
}

int hal_boot_media_read(u64 lba512, u32 count512, void *buf) {
    if (g_post_ebs && g_hv_iso_idx >= 0)
        return hv_stor_read512(g_hv_iso_idx, lba512, count512, buf);
    if (!g_post_ebs && g_iso_bio && bio_read_sectors512(g_iso_bio, lba512, count512, buf) == 0)
        return 0;
    if (g_disk_n > 0)
        return hal_disk_read(0, lba512, count512, buf);
    return -1;
}

void hal_disk_rescan(void) {
    int i;
    g_disk_n = 0;

    if (g_post_ebs && hv_native_active()) {
        int n = hv_stor_count();
        g_hv_iso_idx = -1;
        for (i = 0; i < n && g_disk_n < MAX_DISKS; i++) {
            const HvScsiDisk *hd = hv_stor_get(i);
            u64 total_bytes;
            if (!hd || !hd->active) continue;
            if (hd->dev_type == 0x05U || hd->read_only) {
                if (g_hv_iso_idx < 0) g_hv_iso_idx = i;
                continue;
            }
            total_bytes = hd->total_blocks * (u64)hd->block_size;
            if (total_bytes < 24ULL * 1024ULL * 1024ULL) continue;
            memset(&g_disks[g_disk_n], 0, sizeof(UefiDisk));
            g_disks[g_disk_n].hv_idx = i;
            g_disks[g_disk_n].info.id = g_disk_n;
            g_disks[g_disk_n].info.sector_bytes = hd->block_size;
            g_disks[g_disk_n].info.sectors = total_bytes / 512ULL;
            g_disks[g_disk_n].info.removable = 0;
            g_disks[g_disk_n].info.is_boot_disk = g_booted_from_installed_disk ? 1 : 0;
            snprintf(g_disks[g_disk_n].info.model, sizeof(g_disks[g_disk_n].info.model),
                     "Hyper-V StorVSC %s (%llu МБ)",
                     hd->model[0] ? hd->model : "Virtual Disk",
                     (unsigned long long)(total_bytes / (1024ULL * 1024ULL)));
            g_disk_n++;
        }
        return;
    }

    if (!g_bs) return;
    {
        EFI_HANDLE *handles = NULL;
        UINTN nh = 0;
        u8 probe[512];

        g_iso_bio = NULL;
        if (!g_io_bounce) {
            EFI_PHYSICAL_ADDRESS pa = 0;
            if (!EFI_ERROR(g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 8, &pa)))
                g_io_bounce = (u8 *)(uptr)pa;
            else
                g_io_bounce = (u8 *)kmalloc(32768);
        }

        if (EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gBlockIoGuid, NULL, &nh, &handles)) || !handles)
            return;

        for (i = 0; i < (int)nh && g_disk_n < MAX_DISKS; i++) {
            EFI_BLOCK_IO_PROTOCOL *bio = NULL;
            EFI_BLOCK_IO_MEDIA *m;
            u64 total_bytes;
            if (EFI_ERROR(g_bs->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio)) || !bio || !bio->Media)
                continue;
            m = bio->Media;
            if (!m->MediaPresent || m->BlockSize < 512 || (m->BlockSize & (m->BlockSize - 1)) != 0)
                continue;
            memset(probe, 0, sizeof(probe));
            if (!m->LogicalPartition && bio_read_sectors512(bio, 36, 1, probe) == 0 && memcmp(probe, "YOROISO2", 8) == 0) {
                g_iso_bio = bio;
                continue;
            }
            if (m->LogicalPartition || m->ReadOnly)
                continue;
            total_bytes = (m->LastBlock + 1ULL) * (u64)m->BlockSize;
            if (total_bytes < 24ULL * 1024ULL * 1024ULL)
                continue;

            memset(&g_disks[g_disk_n], 0, sizeof(UefiDisk));
            g_disks[g_disk_n].bio = bio;
            g_disks[g_disk_n].hv_idx = -1;
            g_disks[g_disk_n].info.id = g_disk_n;
            g_disks[g_disk_n].info.sector_bytes = m->BlockSize;
            g_disks[g_disk_n].info.sectors = total_bytes / 512ULL;
            g_disks[g_disk_n].info.removable = m->RemovableMedia ? 1 : 0;
            g_disks[g_disk_n].info.is_boot_disk = g_booted_from_installed_disk ? 1 : 0;
            snprintf(g_disks[g_disk_n].info.model, sizeof(g_disks[g_disk_n].info.model),
                     "UEFI NVMe/SCSI Disk #%d (%llu МБ)",
                     g_disk_n, (unsigned long long)(total_bytes / (1024ULL * 1024ULL)));
            g_disk_n++;
        }
        g_bs->FreePool(handles);
    }
}

int hal_booted_from_disk(void) { return g_booted_from_installed_disk; }
int hal_disk_count(void) { return g_disk_n; }

int hal_disk_info(int idx, HalDiskInfo *out) {
    if (idx < 0 || idx >= g_disk_n) return -1;
    *out = g_disks[idx].info;
    return 0;
}

int hal_disk_read(int idx, u64 lba512, u32 count512, void *buf) {
    if (idx < 0 || idx >= g_disk_n) return -1;
    if (g_post_ebs && g_disks[idx].hv_idx >= 0)
        return hv_stor_read512(g_disks[idx].hv_idx, lba512, count512, buf);
    return bio_read_sectors512(g_disks[idx].bio, lba512, count512, buf);
}

int hal_disk_write(int idx, u64 lba512, u32 count512, const void *buf) {
    EFI_BLOCK_IO_PROTOCOL *bio;
    u32 bsz, spb;
    const u8 *src = (const u8 *)buf;
    if (idx < 0 || idx >= g_disk_n) return -1;
    if (g_post_ebs && g_disks[idx].hv_idx >= 0)
        return hv_stor_write512(g_disks[idx].hv_idx, lba512, count512, buf);

    bio = g_disks[idx].bio;
    if (!bio || !g_io_bounce) return -1;
    bsz = bio->Media->BlockSize;
    spb = bsz / 512U;

    while (count512 > 0) {
        if (bsz == 512) {
            u32 chunk = count512 > 64 ? 64 : count512;
            memcpy(g_io_bounce, src, (size_t)chunk * 512U);
            if (EFI_ERROR(bio->WriteBlocks(bio, bio->Media->MediaId, lba512, (UINTN)chunk * 512U, g_io_bounce)))
                return -1;
            src += (size_t)chunk * 512U;
            lba512 += chunk;
            count512 -= chunk;
        } else {
            u64 phys_lba = lba512 / spb;
            u32 sub = (u32)(lba512 % spb);
            u32 avail = spb - sub;
            u32 take = count512 < avail ? count512 : avail;
            if (take < spb) {
                if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, phys_lba, bsz, g_io_bounce)))
                    return -1;
            }
            memcpy(g_io_bounce + (size_t)sub * 512U, src, (size_t)take * 512U);
            if (EFI_ERROR(bio->WriteBlocks(bio, bio->Media->MediaId, phys_lba, bsz, g_io_bounce)))
                return -1;
            src += (size_t)take * 512U;
            lba512 += take;
            count512 -= take;
        }
    }
    if (bio->FlushBlocks) bio->FlushBlocks(bio);
    return 0;
}

void system_reboot(void) {
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    if (g_rt && g_rt->ResetSystem)
        g_rt->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    hv_reboot_hypervisor();
    outb64(0xCF9, 0x06);
    for (;;) __asm__ volatile("hlt");
}

void system_shutdown(void) {
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    if (g_rt && g_rt->ResetSystem)
        g_rt->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    for (;;) __asm__ volatile("hlt");
}

static int exit_boot_services_cleanly(void) {
    static u8 mmap_buf[32768];
    int attempt;
    for (attempt = 0; attempt < 4; attempt++) {
        UINTN mmap_size = sizeof(mmap_buf);
        UINTN map_key = 0, desc_size = 0;
        u32 desc_ver = 0;
        if (EFI_ERROR(g_bs->GetMemoryMap(&mmap_size, mmap_buf, &map_key, &desc_size, &desc_ver)))
            continue;
        if (!EFI_ERROR(g_bs->ExitBootServices(g_efi_img, map_key))) {
            g_bs = NULL;
            g_post_ebs = 1;
            return 0;
        }
    }
    return -1;
}

/* ---------------- UEFI 64-bit Kernel Entry Point (\EFI\BOOT\BOOTX64.EFI) ---------------- */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    return efi_main(ImageHandle, SystemTable);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    void *heap_mem = NULL;
    void *hv_pool = NULL;
    size_t heap_sizes[3] = { 64U * 1024U * 1024U, 32U * 1024U * 1024U, 16U * 1024U * 1024U };
    size_t hv_pool_bytes = 8U * 1024U * 1024U;
    int i;

    g_efi_img = ImageHandle;
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;
    g_rt = SystemTable->RuntimeServices;

    g_bs->SetWatchdogTimer(0, 0, 0, NULL);
    if (g_st->ConOut) g_st->ConOut->EnableCursor(g_st->ConOut, 0);

    serial_probe();
    klog("=== YoroOS 0.2.9 x86_64 UEFI / Bare-Metal Hyper-V Gen 2 Kernel ===");
    detect_cpu();

    /* Connect UEFI controllers on non-Hyper-V or for SimpleFileSystem boot volume */
    {
        EFI_HANDLE *all_h = NULL;
        UINTN nh = 0, k;
        if (!EFI_ERROR(g_bs->LocateHandleBuffer(AllHandles, NULL, NULL, &nh, &all_h)) && all_h) {
            for (k = 0; k < nh; k++)
                g_bs->ConnectController(all_h[k], NULL, NULL, 1);
            g_bs->FreePool(all_h);
        }
    }

    for (i = 0; i < 3; i++) {
        EFI_PHYSICAL_ADDRESS pa = 0xFFFFFFFFULL;
        UINTN pages = heap_sizes[i] / 4096U;
        if (!EFI_ERROR(g_bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &pa)) ||
            !EFI_ERROR(g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &pa))) {
            heap_mem = (void *)(uptr)pa;
            heap_init(heap_mem, heap_sizes[i]);
            g_ram_mb = (heap_sizes[i] / (1024U * 1024U)) * 32U;
            break;
        }
    }
    if (!heap_mem) return EFI_OUT_OF_RESOURCES;

    if (init_gop() < 0) return EFI_UNSUPPORTED;

    /* Snapshot our own BOOTX64.EFI + YOROOS.IMG into RAM BEFORE ExitBootServices()! */
    capture_boot_payloads_before_ebs();

    if (g_rt && !EFI_ERROR(g_rt->GetTime(&g_boot_time, NULL)))
        g_boot_time_valid = 1;

    /* Real Microsoft Hyper-V Generation 2 Path: ExitBootServices -> Native VMBus! */
    if (hv_detect_hypervisor()) {
        EFI_PHYSICAL_ADDRESS hv_pa = 0xFFFFFFFFULL;
        if (!EFI_ERROR(g_bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, hv_pool_bytes / 4096U, &hv_pa)) ||
            !EFI_ERROR(g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, hv_pool_bytes / 4096U, &hv_pa))) {
            hv_pool = (void *)(uptr)hv_pa;
        }
        if (hv_pool && exit_boot_services_cleanly() == 0) {
            hv_cpu_post_exit_boot_services();
            g_mx = (int)fb.width / 2;
            g_my = (int)fb.height / 2;
            hv_native_init(hv_pool, hv_pool_bytes, g_gop_fb_gpa, fb.width, fb.height, fb.pitch);
            g_boot_hv_100ns = hv_time_ref_100ns();
            strncpy(g_ptr_name, hv_status_summary(), sizeof(g_ptr_name) - 1);
            hal_disk_rescan();
            gfx_init();
            kernel_boot_common();
            main_loop();
            return EFI_SUCCESS;
        }
    }

    /* Non-Hyper-V UEFI Systems (QEMU OVMF, VirtualBox EFI, VMware EFI, Physical UEFI) */
    init_timer();
    init_uefi_input_devices();
    hal_disk_rescan();
    gfx_init();

    kernel_boot_common();
    main_loop();
    return EFI_SUCCESS;
}
