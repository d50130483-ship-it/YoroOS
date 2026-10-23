/* YoroOS 0.2 - 32-bit x86 Protected Mode Hardware Abstraction Layer (BIOS Gen 1) */
#include "kernel.h"

void ata_init(void);
int  ata_disk_count(void);
int  ata_disk_index(int n);
const char *ata_disk_model(int d);
u32  ata_disk_sectors(int d);
int  ata_rw(int d, u32 lba, u32 count, u8 *buf, int write);
int  atapi_boot_media_read(u32 lba512, u32 count512, u8 *buf);

static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static inline u8   inb(u16 port) { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline void outw(u16 port, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(port)); }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }

typedef struct {
    u32 magic, width, height, pitch, bpp;
    u32 framebuffer;
    u32 red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} BootInfo;

static inline volatile void *lowmem_ptr(uptr addr) {
    __asm__ volatile("" : "+r"(addr));
    return (volatile void *)addr;
}
#define BOOTINFO  ((volatile BootInfo *)lowmem_ptr(0x5000))
#define MEMKB     (*(volatile u32 *)lowmem_ptr(0x5030))
#define BOOTDRIVE (*(volatile u8 *)lowmem_ptr(0x600))

struct gdt_entry { u16 limit_low, base_low; u8 base_mid, access, gran, base_high; } __attribute__((packed));
struct gdt_ptr { u16 limit; u32 base; } __attribute__((packed));
struct idt_entry { u16 off_low; u16 sel; u8 zero; u8 flags; u16 off_high; } __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));

static struct gdt_entry gdt[5];
static struct gdt_ptr gdtp;
static struct idt_entry idt[256];
static struct idt_ptr idtp;

static char g_cpu_brand[64] = "x86 32-bit Protected Mode Processor";

static void detect_cpu32(void) {
    u32 max_ext = 0, ebx, ecx, edx;
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

const char *hal_arch_name(void) { return "x86 (BIOS / Generation 1)"; }
const char *hal_cpu_brand(void) { return g_cpu_brand; }
const char *hal_pointer_driver_name(void) { return "PS/2 i8042 Mouse + IRQ12"; }
u64 hal_ram_mb(void) { return MEMKB ? (u64)(MEMKB / 1024U) : 256ULL; }
int hal_is_uefi(void) { return 0; }
int hal_booted_from_disk(void) { return (BOOTDRIVE >= 0x80 && BOOTDRIVE < 0x90) ? 1 : 0; }

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran) {
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access = access;
}

static void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (u32)&gdt;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);
    __asm__ volatile("lgdt %0" :: "m"(gdtp));
    __asm__ volatile("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; "
                     "mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss" ::: "eax");
}

extern void isr0(void), isr1(void), isr2(void), isr3(void), isr4(void), isr5(void);
extern void isr6(void), isr7(void), isr8(void), isr9(void), isr10(void), isr11(void);
extern void isr12(void), isr13(void), isr14(void), isr15(void), isr16(void), isr17(void);
extern void isr18(void), isr19(void), isr20(void), isr21(void), isr22(void), isr23(void);
extern void isr24(void), isr25(void), isr26(void), isr27(void), isr28(void), isr29(void);
extern void isr30(void), isr31(void);
extern void irq0(void), irq1(void), irq2(void), irq3(void), irq4(void), irq5(void);
extern void irq6(void), irq7(void), irq8(void), irq9(void), irq10(void), irq11(void);
extern void irq12(void), irq13(void), irq14(void), irq15(void);

static void idt_set(int i, u32 handler, u8 flags) {
    idt[i].off_low = handler & 0xFFFF;
    idt[i].off_high = (handler >> 16) & 0xFFFF;
    idt[i].sel = 0x08;
    idt[i].zero = 0;
    idt[i].flags = flags;
}

static void idt_init(void) {
    u32 exc[] = { (u32)isr0, (u32)isr1, (u32)isr2, (u32)isr3, (u32)isr4, (u32)isr5,
                  (u32)isr6, (u32)isr7, (u32)isr8, (u32)isr9, (u32)isr10, (u32)isr11,
                  (u32)isr12, (u32)isr13, (u32)isr14, (u32)isr15, (u32)isr16, (u32)isr17,
                  (u32)isr18, (u32)isr19, (u32)isr20, (u32)isr21, (u32)isr22, (u32)isr23,
                  (u32)isr24, (u32)isr25, (u32)isr26, (u32)isr27, (u32)isr28, (u32)isr29,
                  (u32)isr30, (u32)isr31 };
    u32 irqs[] = { (u32)irq0, (u32)irq1, (u32)irq2, (u32)irq3, (u32)irq4, (u32)irq5,
                   (u32)irq6, (u32)irq7, (u32)irq8, (u32)irq9, (u32)irq10, (u32)irq11,
                   (u32)irq12, (u32)irq13, (u32)irq14, (u32)irq15 };
    int i;
    for (i = 0; i < 256; i++) idt_set(i, 0, 0x8E);
    for (i = 0; i < 32; i++) idt_set(i, exc[i], 0x8E);
    for (i = 0; i < 16; i++) idt_set(32 + i, irqs[i], 0x8E);
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u32)&idt;
    __asm__ volatile("lidt %0" :: "m"(idtp));
}

#define PIC1 0x20
#define PIC2 0xA0
static void pic_remap(void) {
    outb(PIC1, 0x11); io_wait();
    outb(PIC2, 0x11); io_wait();
    outb(PIC1 + 1, 0x20); io_wait();
    outb(PIC2 + 1, 0x28); io_wait();
    outb(PIC1 + 1, 0x04); io_wait();
    outb(PIC2 + 1, 0x02); io_wait();
    outb(PIC1 + 1, 0x01); io_wait();
    outb(PIC2 + 1, 0x01); io_wait();
    outb(PIC1 + 1, (u8)~(1 << 0 | 1 << 1 | 1 << 2));
    outb(PIC2 + 1, (u8)~(1 << 4));
}

static void pit_init(u32 hz) {
    u32 div = 1193180U / hz;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
}

static volatile u32 tick_count = 1;
static YoroEvent queue[256];
static volatile int qhead = 0, qtail = 0;

void input_push(const YoroEvent *e) {
    int next = (qtail + 1) & 255;
    if (next == qhead) return;
    queue[qtail] = *e;
    qtail = next;
}

int input_pop(YoroEvent *e) {
    if (qhead == qtail) return 0;
    *e = queue[qhead];
    qhead = (qhead + 1) & 255;
    return 1;
}

static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,
    'q','w','e','r','t','y','u','i','o','p','[',']',13,0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,0,0,' ',0,0
};
static const char keymap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+',8,9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,0,0,' ',0,0
};
static const u16 ru_low[128] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
    0x0439,0x0446,0x0443,0x043A,0x0435,0x043D,0x0433,0x0448,0x0449,0x0437,0x0445,0x044A,0,0,
    0x0444,0x044B,0x0432,0x0430,0x043F,0x0440,0x043E,0x043B,0x0434,0x0436,0x044D,0x0451,0,'\\',
    0x044F,0x0447,0x0441,0x043C,0x0438,0x0442,0x044C,0x0431,0x044E,'.',0,'*',0,' ',0,0
};
static const u16 ru_up[128] = {
    0,0,'!','"',0x2116,';','%',':','?','*','(',')','_','+',0,0,
    0x0419,0x0426,0x0423,0x041A,0x0415,0x041D,0x0413,0x0428,0x0429,0x0417,0x0425,0x042A,0,0,
    0x0424,0x042B,0x0412,0x0410,0x041F,0x0420,0x041E,0x041B,0x0414,0x0416,0x042D,0x0401,0,'/',
    0x042F,0x0427,0x0421,0x041C,0x0418,0x0422,0x042C,0x0411,0x042E,',',0,'*',0,' ',0,0
};

static int kbd_shift = 0, kbd_ctrl = 0, kbd_alt = 0, kbd_e0 = 0, kbd_ru = 0;

int kbd_is_ru(void) { return kbd_ru; }
void kbd_set_ru(int ru) { kbd_ru = ru ? 1 : 0; wm_invalidate(); }
void kbd_toggle_layout(void) { kbd_ru = !kbd_ru; toast(kbd_ru ? "Раскладка: Русская (RU)" : "Layout: English (EN)"); wm_invalidate(); }

static void kbd_irq(void) {
    u8 sc = inb(0x60);
    YoroEvent e;
    if (sc == 0xE0) { kbd_e0 = 1; return; }
    if (sc & 0x80) {
        u8 rel = sc & 0x7F;
        if (rel == 0x2A || rel == 0x36) {
            if (kbd_alt || kbd_ctrl) kbd_toggle_layout();
            kbd_shift = 0;
        }
        if (rel == 0x1D) kbd_ctrl = 0;
        if (rel == 0x38) kbd_alt = 0;
        kbd_e0 = 0;
        return;
    }
    memset(&e, 0, sizeof(e));
    e.type = EV_KEY;
    e.shift = kbd_shift; e.ctrl = kbd_ctrl; e.alt = kbd_alt;
    if (kbd_e0 && (sc == 0x5B || sc == 0x5C)) { kbd_e0 = 0; e.key = KEY_WIN; input_push(&e); return; }
    kbd_e0 = 0;
    switch (sc) {
    case 0x01: e.key = KEY_ESC; break;
    case 0x0E: e.key = KEY_BACKSP; break;
    case 0x0F: e.key = KEY_TAB; break;
    case 0x1C: e.key = KEY_ENTER; break;
    case 0x2A: case 0x36: kbd_shift = 1; return;
    case 0x1D: kbd_ctrl = 1; return;
    case 0x38: kbd_alt = 1; return;
    case 0x3B: e.key = KEY_F1; break;
    case 0x3C: e.key = KEY_F2; break;
    case 0x3D: e.key = KEY_F3; break;
    case 0x3E: e.key = KEY_F4; break;
    case 0x3F: e.key = KEY_F5; break;
    case 0x40: e.key = KEY_F6; break;
    case 0x41: e.key = KEY_F7; break;
    case 0x42: e.key = KEY_F8; break;
    case 0x43: e.key = KEY_F9; break;
    case 0x44: e.key = KEY_F10; break;
    case 0x57: e.key = KEY_F11; break;
    case 0x58: e.key = KEY_F12; break;
    case 0x48: e.key = KEY_UP; break;
    case 0x50: e.key = KEY_DOWN; break;
    case 0x4B: e.key = KEY_LEFT; break;
    case 0x4D: e.key = KEY_RIGHT; break;
    case 0x49: e.key = KEY_PGUP; break;
    case 0x51: e.key = KEY_PGDN; break;
    case 0x53: e.key = KEY_DEL; break;
    case 0x47: e.key = KEY_HOME; break;
    case 0x4F: e.key = KEY_END; break;
    default:
        if (sc < 128) {
            if (kbd_ru && !kbd_ctrl && !kbd_alt && ru_low[sc]) {
                int cp = kbd_shift ? ru_up[sc] : ru_low[sc];
                e.key = cp; e.ch = cp;
            } else {
                char c = kbd_shift ? keymap_shift[sc] : keymap[sc];
                if (!c) return;
                e.key = (u8)c; e.ch = (u8)c;
            }
            break;
        }
        return;
    }
    input_push(&e);
}

/* ---------------- PS/2 Mouse ---------------- */
static volatile int mouse_x = 512, mouse_y = 384, mouse_btn_state = 0;
static u8 mpkt[4];
static int mcycle = 0, mwheel_ext = 0;

static void ps2_wait(int w) {
    int t = 100000;
    if (w) while (t-- && (inb(0x64) & 2)) {}
    else   while (t-- && !(inb(0x64) & 1)) {}
}
static void mouse_write(u8 v) { ps2_wait(1); outb(0x64, 0xD4); ps2_wait(1); outb(0x60, v); }
static u8 mouse_read(void) { ps2_wait(0); return inb(0x60); }

static void mouse_init_hw(void) {
    u8 status;
    mouse_x = (int)fb.width / 2;
    mouse_y = (int)fb.height / 2;
    ps2_wait(1); outb(0x64, 0xA8);
    ps2_wait(1); outb(0x64, 0xAE);
    ps2_wait(1); outb(0x64, 0x20);
    status = (u8)((mouse_read() | 0x03) & ~0x30);
    ps2_wait(1); outb(0x64, 0x60);
    ps2_wait(1); outb(0x60, status);
    mouse_write(0xF6); (void)mouse_read();
    /* Enable IntelliMouse scroll wheel (200 -> 100 -> 80 sample sequence) */
    mouse_write(0xF3); (void)mouse_read(); mouse_write(200); (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read(); mouse_write(100); (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read(); mouse_write(80);  (void)mouse_read();
    mouse_write(0xF2); (void)mouse_read();
    mwheel_ext = (mouse_read() == 3) ? 1 : 0;
    mouse_write(0xF4); (void)mouse_read();
    ps2_wait(1); outb(0x64, 0xAE);
    while (inb(0x64) & 1) (void)inb(0x60);
}

static void mouse_irq(void) {
    u8 b = inb(0x60);
    int nbytes = mwheel_ext ? 4 : 3;
    if (mcycle == 0 && !(b & 0x08)) return;
    mpkt[mcycle++] = b;
    if (mcycle < nbytes) return;
    mcycle = 0;
    {
        int dx = (i8)mpkt[1];
        int dy = -(i8)mpkt[2];
        int nb = mpkt[0] & 3;
        int wh = mwheel_ext ? -(i8)mpkt[3] : 0;
        YoroEvent e;
        mouse_x += dx; mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int)fb.width) mouse_x = (int)fb.width - 1;
        if (mouse_y >= (int)fb.height) mouse_y = (int)fb.height - 1;
        if (dx || dy) {
            memset(&e, 0, sizeof(e));
            e.type = EV_MOUSE_MOVE; e.x = mouse_x; e.y = mouse_y;
            input_push(&e);
        }
        if ((nb & 1) != (mouse_btn_state & 1)) {
            memset(&e, 0, sizeof(e));
            e.type = (nb & 1) ? EV_MOUSE_DOWN : EV_MOUSE_UP;
            e.btn = 1; e.x = mouse_x; e.y = mouse_y;
            input_push(&e);
        }
        if ((nb & 2) != (mouse_btn_state & 2)) {
            memset(&e, 0, sizeof(e));
            e.type = (nb & 2) ? EV_MOUSE_DOWN : EV_MOUSE_UP;
            e.btn = 2; e.x = mouse_x; e.y = mouse_y;
            input_push(&e);
        }
        mouse_btn_state = nb;
        if (wh) {
            memset(&e, 0, sizeof(e));
            e.type = EV_WHEEL; e.wheel = wh > 0 ? 1 : -1; e.x = mouse_x; e.y = mouse_y;
            input_push(&e);
        }
    }
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_state(void) { return mouse_btn_state; }
void mouse_set_pos(int x, int y) { mouse_x = x; mouse_y = y; }

void hal_poll_input(void) {
    int guard = 32;
    cli();
    while (guard-- > 0) {
        u8 st = inb(0x64);
        if (!(st & 0x01)) break;
        if (st & 0x20) mouse_irq();
        else kbd_irq();
    }
    sti();
}
void hal_idle(void) { hlt(); }

void hal_beep(u32 freq_hz, u32 dur_ms) {
    u32 div, end;
    if (!freq_hz) return;
    div = 1193180U / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));
    outb(0x61, (u8)(inb(0x61) | 3));
    end = tick_count + (dur_ms + 9) / 10;
    while (tick_count < end) hlt();
    outb(0x61, (u8)(inb(0x61) & 0xFC));
}

/* ---------------- Video Presentation (8/15/16/24/32 bpp VBE) ---------------- */
static int palette_ready = 0;

static void palette_install(void) {
    int i;
    outb(0x3C8, 0);
    for (i = 0; i < 256; i++) {
        outb(0x3C9, (u8)(((i >> 5) & 7) * 63 / 7));
        outb(0x3C9, (u8)(((i >> 2) & 7) * 63 / 7));
        outb(0x3C9, (u8)((i & 3) * 63 / 3));
    }
    palette_ready = 1;
}

void hal_present_rect(Rect r) {
    u32 x, y;
    u32 rp = fb.red_pos, gp = fb.green_pos, bp = fb.blue_pos;
    u32 rs = fb.red_size ? 8U - fb.red_size : 0;
    u32 gs = fb.green_size ? 8U - fb.green_size : 0;
    u32 bs = fb.blue_size ? 8U - fb.blue_size : 0;
    u8 bpp = fb.bpp;

    if (!fb.vram || !fb.back) return;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > (int)fb.width) r.w = (int)fb.width - r.x;
    if (r.y + r.h > (int)fb.height) r.h = (int)fb.height - r.y;
    if (r.w <= 0 || r.h <= 0) return;

    if (bpp == 8 && !palette_ready) palette_install();

    for (y = (u32)r.y; y < (u32)(r.y + r.h); y++) {
        const u32 *src = (const u32 *)(fb.back + y * fb.bstride);
        u8 *dst = fb.vram + y * fb.pitch;
        if (bpp == 32 && rp == 16 && gp == 8 && bp == 0) {
            memcpy(dst + (u32)r.x * 4U, src + r.x, (size_t)r.w * 4U);
            continue;
        }
        for (x = (u32)r.x; x < (u32)(r.x + r.w); x++) {
            u32 v = src[x];
            if (bpp == 8) {
                dst[x] = (u8)(((v >> 16) & 0xE0) | (((v >> 8) & 0xE0) >> 3) | ((v & 0xC0) >> 6));
            } else if (bpp == 15 || bpp == 16) {
                u32 o = ((((v >> 16) & 0xFF) >> rs) << rp) | ((((v >> 8) & 0xFF) >> gs) << gp) | (((v & 0xFF) >> bs) << bp);
                *(u16 *)(dst + x * 2) = (u16)o;
            } else if (bpp == 24) {
                u32 o = ((((v >> 16) & 0xFF) >> rs) << rp) | ((((v >> 8) & 0xFF) >> gs) << gp) | (((v & 0xFF) >> bs) << bp);
                dst[x * 3] = (u8)o; dst[x * 3 + 1] = (u8)(o >> 8); dst[x * 3 + 2] = (u8)(o >> 16);
            } else {
                u32 o = ((((v >> 16) & 0xFF) >> rs) << rp) | ((((v >> 8) & 0xFF) >> gs) << gp) | (((v & 0xFF) >> bs) << bp);
                *(u32 *)(dst + x * 4) = o;
            }
        }
    }
}

void hal_present(void) { hal_present_rect((Rect){0, 0, (int)fb.width, (int)fb.height}); }
int hal_video_mode_count(void) { return 1; }
int hal_video_mode_get(int idx, HalVideoMode *out) {
    if (idx != 0) return -1;
    out->mode_id = 0; out->width = fb.width; out->height = fb.height; out->bpp = fb.bpp;
    return 0;
}
int hal_video_set_mode(int mode_id) { UNUSED(mode_id); return -1; }

/* ---------------- Disk HAL over ATA PIO ---------------- */
void hal_disk_rescan(void) { ata_init(); }
int hal_disk_count(void) { return ata_disk_count(); }

int hal_disk_info(int idx, HalDiskInfo *out) {
    int d = ata_disk_index(idx);
    if (d < 0) return -1;
    memset(out, 0, sizeof(*out));
    out->id = idx;
    out->sectors = ata_disk_sectors(d);
    out->sector_bytes = 512;
    snprintf(out->model, sizeof(out->model), "%s (%u МБ)",
             ata_disk_model(d)[0] ? ata_disk_model(d) : "IDE/ATA Disk",
             (u32)(out->sectors / 2048ULL));
    return 0;
}

int hal_disk_read(int idx, u64 lba512, u32 count512, void *buf) {
    int d = ata_disk_index(idx);
    if (d < 0 || lba512 > 0x0FFFFFFFULL) return -1;
    return ata_rw(d, (u32)lba512, count512, (u8 *)buf, 0);
}

int hal_disk_write(int idx, u64 lba512, u32 count512, const void *buf) {
    int d = ata_disk_index(idx);
    if (d < 0 || lba512 > 0x0FFFFFFFULL) return -1;
    return ata_rw(d, (u32)lba512, count512, (u8 *)buf, 1);
}

int hal_boot_media_read(u64 lba512, u32 count512, void *buf) {
    int i, n;
    u8 probe[512];
    if (lba512 > 0x0FFFFFFFULL) return -1;
    if (atapi_boot_media_read((u32)lba512, count512, (u8 *)buf) == 0)
        return 0;
    n = hal_disk_count();
    for (i = 0; i < n; i++) {
        if (hal_disk_read(i, 36, 1, probe) == 0 && memcmp(probe, "YOROISO2", 8) == 0)
            return hal_disk_read(i, lba512, count512, buf);
    }
    return -1;
}

/* ---------------- RTC, Serial, IRQ & Power ---------------- */
static u8 cmos_read(u8 reg) { outb(0x70, reg); return inb(0x71); }
static int bcd(u8 v) { return (v & 0x0F) + ((v >> 4) * 10); }

void rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year) {
    u8 s, m, h, d, mo, y, fmt;
    int hh;
    while (cmos_read(0x0A) & 0x80) {}
    s = cmos_read(0x00); m = cmos_read(0x02); h = cmos_read(0x04);
    d = cmos_read(0x07); mo = cmos_read(0x08); y = cmos_read(0x09);
    fmt = cmos_read(0x0B);
    if (!(fmt & 0x04)) {
        s = (u8)bcd(s); m = (u8)bcd(m); d = (u8)bcd(d); mo = (u8)bcd(mo); y = (u8)bcd(y);
        h = (u8)(((h & 0x0F) + (((h >> 4) & 7) * 10)) | (h & 0x80));
    }
    if (!(fmt & 0x02) && (h & 0x80)) h = (u8)(((h & 0x7F) + 12) % 24);
    hh = (int)h + sys_profile.tz_offset;
    while (hh >= 24) hh -= 24;
    while (hh < 0) hh += 24;
    *hour = hh; *min = m; *sec = s; *day = d; *mon = mo; *year = 2000 + y;
}

u32 sys_ticks(void) { return tick_count; }

typedef struct {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 vector, err, eip, cs, eflags;
} Regs;

void isr_handler_c(Regs *r) {
    char buf[128];
    cli();
    klog("!!! CPU EXCEPTION vec=0x%x eip=0x%x err=0x%x esp=0x%x", r->vector, r->eip, r->err, r->esp);
    fill_rect((Rect){0, 0, (int)fb.width, (int)fb.height}, RGB(0, 120, 215));
    draw_text_hero(60, 60, ":(", C_WHITE);
    draw_text_title(60, 130, "На вашем ПК возникла проблема — YoroKernel 0.2 остановил выполнение", C_WHITE);
    snprintf(buf, sizeof(buf), "Код исключения CPU: 0x%02X   EIP: 0x%08X   ERR: 0x%08X", r->vector, r->eip, r->err);
    draw_text(60, 180, buf, RGB(230, 244, 255));
    gfx_present();
    for (;;) hlt();
}

void irq_handler_c(Regs *r) {
    int irq = (int)r->vector - 32;
    if (irq == 0) {
        tick_count++;
        if ((tick_count & 1) == 0) {
            YoroEvent e;
            memset(&e, 0, sizeof(e));
            e.type = EV_TICK;
            input_push(&e);
        }
    } else if (irq == 1) kbd_irq();
    else if (irq == 12) mouse_irq();
    if (irq >= 8) outb(PIC2, 0x20);
    outb(PIC1, 0x20);
}

static void serial_init_hw(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
void serial_putc(char c) {
    int guard = 100000;
    while (!(inb(0x3F8 + 5) & 0x20) && guard--) {}
    outb(0x3F8, (u8)c);
}
void serial_puts(const char *s) { while (*s) { if (*s == '\n') serial_putc('\r'); serial_putc(*s++); } }
void klog(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_puts(buf);
    serial_puts("\n");
}

void system_reboot(void) {
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    cli();
    outb(0x64, 0xFE);
    outb(0xCF9, 0x06);
    for (;;) hlt();
}

void system_shutdown(void) {
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    outw(0x604, 0x2000);   /* QEMU / Bochs */
    outw(0xB004, 0x2000);  /* Old QEMU */
    outw(0x4004, 0x3400);  /* VirtualBox */
    cli();
    fill_rect((Rect){0, 0, (int)fb.width, (int)fb.height}, RGB(8, 12, 22));
    draw_text_center_face(FACE_TITLE, (int)fb.width / 2, (int)fb.height / 2 - 14,
                          "Теперь питание компьютера можно отключить", C_WHITE);
    gfx_present();
    for (;;) hlt();
}

void kmain(void) {
    volatile BootInfo *bi = BOOTINFO;
    size_t hsz;
    cli();
    serial_init_hw();
    klog("=== YoroOS 0.2 'Aurora' x86 BIOS Kernel ===");
    detect_cpu32();
    gdt_init();
    idt_init();
    pic_remap();
    pit_init(100);

    fb.width = bi->width;
    fb.height = bi->height;
    fb.pitch = bi->pitch;
    fb.bpp = (u8)bi->bpp;
    fb.red_pos = (u8)bi->red_pos; fb.red_size = (u8)bi->red_size;
    fb.green_pos = (u8)bi->green_pos; fb.green_size = (u8)bi->green_size;
    fb.blue_pos = (u8)bi->blue_pos; fb.blue_size = (u8)bi->blue_size;
    fb.vram = (u8 *)(uptr)bi->framebuffer;
    fb.bstride = fb.width * 4U;
    fb.dual = 1;
    klog("[vbe] %ux%ux%u pitch=%u fb=0x%x memkb=%u", fb.width, fb.height, fb.bpp, fb.pitch, bi->framebuffer, MEMKB);

    /* Kernel lives at 1 MiB, PE arena at 8..12 MiB, heap starts at 16 MiB */
    hsz = (MEMKB > 32768U) ? (size_t)(MEMKB - 16384U) * 1024U : 32U * 1024U * 1024U;
    if (hsz > 96U * 1024U * 1024U) hsz = 96U * 1024U * 1024U;
    heap_init((void *)0x01000000U, hsz);

    gfx_init();
    mouse_init_hw();
    ata_init();

    sti();
    kernel_boot_common();
    main_loop();
}
