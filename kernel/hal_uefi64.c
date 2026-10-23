/* YoroOS 0.2 - Native 64-bit UEFI & Hyper-V Generation 2 Hardware Abstraction Layer */
#include "kernel.h"
#include "efi.h"

int hv_mouse_init(EFI_HANDLE image_handle, EFI_BOOT_SERVICES *bs);
int hv_mouse_poll(u32 *last_seq, int screen_w, int screen_h,
                  int *out_x, int *out_y, int *out_btns, int *out_wheel);

static EFI_GUID gLoadedImageGuid = {
    0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
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
    0x31878c87, 0x0b75, 0x11d5, {0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};
static EFI_GUID gAbsolutePointerGuid = {
    0x8D59D32B, 0xC655, 0x4AE9, {0x9B, 0x15, 0xF2, 0x59, 0x04, 0x99, 0x2A, 0x43}
};
static EFI_GUID gBlockIoGuid = {
    0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

EFI_HANDLE        g_efi_img = NULL;
EFI_SYSTEM_TABLE *g_st = NULL;
EFI_BOOT_SERVICES *g_bs = NULL;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;

/* Serial COM1 safe debug helper (disabled if port floats / no UART in pure Gen 2) */
static int g_uart_ok = 0;
static inline void outb64(u16 port, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static inline u8 inb64(u16 port) { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }

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

const char *hal_arch_name(void) { return "x86_64 (UEFI Generation 2)"; }
const char *hal_cpu_brand(void) { return g_cpu_brand; }
u64 hal_ram_mb(void) { return g_ram_mb; }
int hal_is_uefi(void) { return 1; }

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
    /* 10 ms periodic UEFI timer (100,000 * 100ns = 10ms = 100 Hz) */
    if (!EFI_ERROR(g_bs->CreateEvent(0x80000000U /* EVT_TIMER */, 0, NULL, NULL, &g_timer_ev))) {
        g_bs->SetTimer(g_timer_ev, 1 /* TimerPeriodic */, 100000ULL);
    }
}

u32 sys_ticks(void) {
    if (g_timer_ev && !EFI_ERROR(g_bs->CheckEvent(g_timer_ev))) {
        g_ticks++;
    }
    return g_ticks;
}

void rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year) {
    EFI_TIME t;
    memset(&t, 0, sizeof(t));
    if (g_st && g_st->RuntimeServices && !EFI_ERROR(g_st->RuntimeServices->GetTime(&t, NULL))) {
        int h = (int)t.Hour + sys_profile.tz_offset;
        while (h >= 24) h -= 24;
        while (h < 0) h += 24;
        *hour = h; *min = t.Minute; *sec = t.Second;
        *day = t.Day; *mon = t.Month; *year = t.Year;
        return;
    }
    *hour = 16; *min = 25; *sec = (int)((g_ticks / 100) % 60);
    *day = 23; *mon = 10; *year = 2026;
}

void hal_beep(u32 freq_hz, u32 dur_ms) {
    UNUSED(freq_hz);
    if (g_bs && dur_ms <= 80) g_bs->Stall((UINTN)dur_ms * 100ULL);
}

/* ---------------- GOP Video ---------------- */
#define MAX_VMODES 24
static HalVideoMode g_vmodes[MAX_VMODES];
static int g_vmode_n = 0;

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
    fb.vram = (u8 *)(uptr)g_gop->Mode->FrameBufferBase;
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
    if (!g_gop) {
        (void)g_bs->LocateProtocol(&gGopGuid, NULL, (void **)&g_gop);
    }
    if (!g_gop) {
        (void)g_bs->LocateProtocol(&gMsGopOverrideGuid, NULL, (void **)&g_gop);
    }
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
                else if (w == 1280 && h == 720) score = 950000;
                else if (w == 1024 && h == 768) score = 920000;
                else if (w == 1366 && h == 768) score = 900000;
                else if (w <= 1440 && h <= 900) score = w * h / 4;
                else score = 200000 - (w - 1440);
                if (score > best_score) { best_score = score; best_mode = (int)i; }
            }
        }
        g_bs->FreePool(inf);
    }

    if (best_mode >= 0 && (u32)best_mode != g_gop->Mode->Mode) {
        g_gop->SetMode(g_gop, (u32)best_mode);
    }
    sync_fb_from_gop();
    klog("[gop] Video %ux%u pitch=%u vram=%p modes=%d",
         fb.width, fb.height, fb.pitch, fb.vram, g_vmode_n);
    return 0;
}

int hal_video_mode_count(void) { return g_vmode_n; }

int hal_video_mode_get(int idx, HalVideoMode *out) {
    if (idx < 0 || idx >= g_vmode_n) return -1;
    *out = g_vmodes[idx];
    return 0;
}

int hal_video_set_mode(int mode_id) {
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
    if (!fb.back || !g_gop) return;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > (int)fb.width) r.w = (int)fb.width - r.x;
    if (r.y + r.h > (int)fb.height) r.h = (int)fb.height - r.y;
    if (r.w <= 0 || r.h <= 0) return;

    if (g_gop->Blt) {
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

static int g_hv_mouse_ok = 0;
static u32 g_hv_last_seq = 0;
static int g_mx = 512, g_my = 384, g_mbtns = 0;
static int g_ru = 0;
static int g_mousekeys = 0;
static char g_ptr_name[64] = "UEFI Pointer";

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

static void init_input_devices(void) {
    EFI_HANDLE *handles = NULL;
    UINTN nh = 0, i;

    g_mx = (int)fb.width / 2;
    g_my = (int)fb.height / 2;

    /* 1) Native Microsoft Hyper-V Gen 2 VMBus Synthetic Mouse */
    g_hv_mouse_ok = hv_mouse_init(g_efi_img, g_bs);

    /* 2) All UEFI Absolute Pointer devices (OVMF usb-tablet, VMware, touchscreens) */
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

    /* 3) All UEFI Simple Pointer devices (USB mouse, PS/2 mouse in UEFI) */
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

    /* 4) Extended keyboard protocol handles */
    nh = 0;
    if (!EFI_ERROR(g_bs->HandleProtocol(g_st->ConsoleInHandle, &gSimpleTextInExGuid, (void **)&g_key_ex[0])) && g_key_ex[0]) {
        g_key_ex_n = 1;
    } else if (!EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gSimpleTextInExGuid, NULL, &nh, &handles)) && handles) {
        for (i = 0; i < nh && g_key_ex_n < MAX_PTR_DEVS; i++) {
            EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *kx = NULL;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &gSimpleTextInExGuid, (void **)&kx)) && kx) {
                g_key_ex[g_key_ex_n++] = kx;
            }
        }
        g_bs->FreePool(handles);
    }

    if (g_hv_mouse_ok) strcpy(g_ptr_name, "Hyper-V VMBus SynthHid Mouse");
    else if (g_abs_n > 0) strcpy(g_ptr_name, "UEFI Absolute HID Tablet/Mouse");
    else if (g_rel_n > 0) strcpy(g_ptr_name, "UEFI USB/PS2 SimplePointer");
    else strcpy(g_ptr_name, "Keyboard Navigation + F8 MouseKeys");

    klog("[input] hv=%d abs=%d rel=%d kbd_ex=%d (%s)",
         g_hv_mouse_ok, g_abs_n, g_rel_n, g_key_ex_n, g_ptr_name);
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

/* QWERTY ASCII -> Russian ЙЦУКЕН Unicode codepoint when in RU mode */
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
    for (i = 0; en[i]; i++) {
        if ((u16)(u8)en[i] == ch) return ru[i];
    }
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
        /* Alt+Shift or Ctrl+Shift with no character switches RU/EN! */
        if (scan == 0 && uni == 0 && e.shift && (e.alt || e.ctrl)) {
            kbd_toggle_layout();
            return;
        }
    }

    /* UEFI Scan Codes (UEFI Spec Table 107) */
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

    /* Optional F8 arrow-cursor mode if someone runs without any mouse device */
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
        /* Ctrl+A..Z control codes 1..26 */
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

    sys_ticks();

    /* 1) Hyper-V Gen 2 VMBus Synthetic Mouse (drain full lossless 64-report FIFO) */
    while (hv_mouse_poll(&g_hv_last_seq, (int)fb.width, (int)fb.height, &hx, &hy, &hb, &hw)) {
        emit_pointer_state(hx, hy, hb, hw);
    }

    /* 2) UEFI Absolute Pointer devices */
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

    /* 3) UEFI Relative Simple Pointer devices */
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

    /* 4) Keyboard input (SimpleTextInputEx preferred, fallback to ConIn) */
    if (g_key_ex_n > 0) {
        EFI_KEY_DATA kd;
        while (!EFI_ERROR(g_key_ex[0]->ReadKeyStrokeEx(g_key_ex[0], &kd))) {
            process_efi_key(kd.Key.ScanCode, kd.Key.UnicodeChar, kd.KeyState.KeyShiftState);
        }
    } else if (g_st && g_st->ConIn) {
        EFI_INPUT_KEY k;
        while (!EFI_ERROR(g_st->ConIn->ReadKeyStroke(g_st->ConIn, &k))) {
            process_efi_key(k.ScanCode, k.UnicodeChar, 0);
        }
    }
}

void hal_idle(void) {
    if (g_bs) g_bs->Stall(2500); /* 2.5 ms polite sleep (~400 Hz responsive loop, low CPU) */
}

/* ---------------- Block I/O Disk Layer ---------------- */
#define MAX_DISKS 8
typedef struct {
    EFI_BLOCK_IO_PROTOCOL *bio;
    EFI_HANDLE handle;
    HalDiskInfo info;
} UefiDisk;

static UefiDisk g_disks[MAX_DISKS];
static int g_disk_n = 0;
static EFI_BLOCK_IO_PROTOCOL *g_iso_bio = NULL;
static int g_booted_from_installed_disk = 0;
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
    if (g_iso_bio && bio_read_sectors512(g_iso_bio, lba512, count512, buf) == 0)
        return 0;
    if (g_disk_n > 0)
        return hal_disk_read(0, lba512, count512, buf);
    return -1;
}

void hal_disk_rescan(void) {
    EFI_HANDLE *handles = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_HANDLE boot_dev = NULL;
    UINTN nh = 0, i;
    u8 probe[512];

    g_disk_n = 0;
    g_iso_bio = NULL;
    if (!g_io_bounce) {
        EFI_PHYSICAL_ADDRESS pa = 0;
        if (!EFI_ERROR(g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 8, &pa)))
            g_io_bounce = (u8 *)(uptr)pa; /* 32 KiB 4K-page-aligned DMA/IoAlign buffer! */
        else
            g_io_bounce = (u8 *)kmalloc(32768);
    }

    if (!EFI_ERROR(g_bs->HandleProtocol(g_efi_img, &gLoadedImageGuid, (void **)&loaded)) && loaded)
        boot_dev = loaded->DeviceHandle;

    /* Check if the boot device is a partition on an installed GPT disk vs read-only CD/ISO */
    if (boot_dev) {
        EFI_BLOCK_IO_PROTOCOL *bbio = NULL;
        if (!EFI_ERROR(g_bs->HandleProtocol(boot_dev, &gBlockIoGuid, (void **)&bbio)) && bbio && bbio->Media) {
            if (!bbio->Media->ReadOnly && bbio->Media->BlockSize == 512)
                g_booted_from_installed_disk = 1;
        }
    }

    if (EFI_ERROR(g_bs->LocateHandleBuffer(ByProtocol, &gBlockIoGuid, NULL, &nh, &handles)) || !handles)
        return;

    for (i = 0; i < nh && g_disk_n < MAX_DISKS; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        EFI_BLOCK_IO_MEDIA *m;
        u64 total_bytes;
        if (EFI_ERROR(g_bs->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio)) || !bio || !bio->Media)
            continue;
        m = bio->Media;
        if (!m->MediaPresent)
            continue;
        if (m->BlockSize < 512 || (m->BlockSize & (m->BlockSize - 1)) != 0)
            continue;
        memset(probe, 0, sizeof(probe));
        if (!m->LogicalPartition && bio_read_sectors512(bio, 36, 1, probe) == 0 && memcmp(probe, "YOROISO2", 8) == 0) {
            g_iso_bio = bio;
            continue;
        }
        if (m->LogicalPartition || m->ReadOnly)
            continue;
        /* Ignore tiny floppy/ElTorito 8 MiB ESP image if exposed as raw block io (< 24 MiB) */
        total_bytes = (m->LastBlock + 1ULL) * (u64)m->BlockSize;
        if (total_bytes < 24ULL * 1024ULL * 1024ULL)
            continue;

        g_disks[g_disk_n].bio = bio;
        g_disks[g_disk_n].handle = handles[i];
        g_disks[g_disk_n].info.id = g_disk_n;
        g_disks[g_disk_n].info.sector_bytes = m->BlockSize;
        g_disks[g_disk_n].info.sectors = total_bytes / 512ULL;
        g_disks[g_disk_n].info.removable = m->RemovableMedia ? 1 : 0;
        g_disks[g_disk_n].info.is_boot_disk = (handles[i] == boot_dev) ? 1 : 0;
        snprintf(g_disks[g_disk_n].info.model, sizeof(g_disks[g_disk_n].info.model),
                 "%s Virtual/NVMe/SCSI Disk #%d (%llu МБ)",
                 g_hv_mouse_ok ? "Hyper-V StorVSC" : "UEFI BlockIO",
                 g_disk_n, (unsigned long long)(total_bytes / (1024ULL * 1024ULL)));
        klog("[disk] #%d: %s sectors512=%llu bsz=%u",
             g_disk_n, g_disks[g_disk_n].info.model,
             (unsigned long long)g_disks[g_disk_n].info.sectors, m->BlockSize);
        g_disk_n++;
    }
    g_bs->FreePool(handles);
}

int hal_booted_from_disk(void) { return g_booted_from_installed_disk; }
int hal_disk_count(void) { return g_disk_n; }

int hal_disk_info(int idx, HalDiskInfo *out) {
    if (idx < 0 || idx >= g_disk_n) return -1;
    *out = g_disks[idx].info;
    return 0;
}

int hal_disk_read(int idx, u64 lba512, u32 count512, void *buf) {
    EFI_BLOCK_IO_PROTOCOL *bio;
    u32 bsz, spb;
    u8 *dst = (u8 *)buf;
    if (idx < 0 || idx >= g_disk_n || !g_io_bounce) return -1;
    bio = g_disks[idx].bio;
    bsz = bio->Media->BlockSize;
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

int hal_disk_write(int idx, u64 lba512, u32 count512, const void *buf) {
    EFI_BLOCK_IO_PROTOCOL *bio;
    u32 bsz, spb;
    const u8 *src = (const u8 *)buf;
    if (idx < 0 || idx >= g_disk_n || !g_io_bounce) return -1;
    bio = g_disks[idx].bio;
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
    if (g_st && g_st->RuntimeServices && g_st->RuntimeServices->ResetSystem)
        g_st->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    for (;;) __asm__ volatile("hlt");
}

void system_shutdown(void) {
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    if (g_st && g_st->RuntimeServices && g_st->RuntimeServices->ResetSystem)
        g_st->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    for (;;) __asm__ volatile("hlt");
}

/* ---------------- UEFI 64-bit Kernel Entry Point (\EFI\BOOT\BOOTX64.EFI) ---------------- */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    return efi_main(ImageHandle, SystemTable);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    void *heap_mem = NULL;
    size_t heap_sizes[3] = { 48U * 1024U * 1024U, 24U * 1024U * 1024U, 12U * 1024U * 1024U };
    int i;

    g_efi_img = ImageHandle;
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;

    /* Disable UEFI's default 5-minute boot watchdog so YoroOS runs indefinitely! */
    g_bs->SetWatchdogTimer(0, 0, 0, NULL);

    if (g_st->ConOut) g_st->ConOut->EnableCursor(g_st->ConOut, 0);

    serial_probe();
    klog("=== YoroOS 0.2 'Aurora' x86_64 UEFI / Hyper-V Generation 2 Kernel ===");
    detect_cpu();

    /* Connect all UEFI controllers so USB HID mice/keyboards, NVMe, AHCI, VMBus attach */
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
        if (!EFI_ERROR(g_bs->AllocatePool(EfiLoaderData, heap_sizes[i], &heap_mem)) && heap_mem) {
            heap_init(heap_mem, heap_sizes[i]);
            g_ram_mb = (heap_sizes[i] / (1024U * 1024U)) * 32U;
            break;
        }
    }
    if (!heap_mem) return EFI_OUT_OF_RESOURCES;

    if (init_gop() < 0) return EFI_UNSUPPORTED;

    init_timer();
    init_input_devices();
    hal_disk_rescan();
    gfx_init();

    kernel_boot_common();
    main_loop();
    return EFI_SUCCESS;
}
