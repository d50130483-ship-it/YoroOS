/* YoroOS - kernel entry, GDT/IDT/PIC/PIT, PS/2 keyboard + mouse, wall clock */
#include "kernel.h"

/* ---------------- boot info handed over by stage2 ---------------- */
typedef struct {
    u32 magic, width, height, pitch, bpp;
    u32 framebuffer;
    u32 red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} BootInfo;

#define BOOTINFO ((volatile BootInfo *)0x5000)
#define MEMKB    (*(volatile u32 *)0x5030)
#define BOOT_MAGIC 0x4F524F59

extern u32 boot_info __attribute__((weak));

/* ---------------- GDT ---------------- */
struct gdt_entry { u16 limit_low, base_low; u8 base_mid, access, gran, base_high; } __attribute__((packed));
struct gdt_ptr { u16 limit; u32 base; } __attribute__((packed));
struct idt_entry { u16 off_low; u16 sel; u8 zero; u8 flags; u16 off_high; } __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));

static struct gdt_entry gdt[5];
static struct gdt_ptr gdtp;
static struct idt_entry idt[256];
static struct idt_ptr idtp;

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
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);   /* code 0x08 */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);   /* data 0x10 */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);   /* user code */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);   /* user data */
    __asm__ volatile("lgdt %0" :: "m"(gdtp));
    __asm__ volatile("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; "
                     "mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss" ::: "eax");
}

/* ---------------- IDT ---------------- */
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

/* ---------------- PIC ---------------- */
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
    /* unmask: timer, keyboard, cascade, mouse */
    outb(PIC1 + 1, (u8)~(1 << 0 | 1 << 1 | 1 << 2));
    outb(PIC2 + 1, (u8)~(1 << 4));
}

/* ---------------- input queues ---------------- */
extern void yoro_event(YoroEvent *e);

volatile u32 tick_count = 0;

static YoroEvent queue[128];
static volatile int qhead = 0, qtail = 0;

void input_push(const YoroEvent *e) {
    int next = (qtail + 1) % 128;
    if (next == qhead) return;
    queue[qtail] = *e;
    qtail = next;
}

int input_pop(YoroEvent *e) {
    if (qhead == qtail) return 0;
    *e = queue[qhead];
    qhead = (qhead + 1) % 128;
    return 1;
}

/* ---------------- keyboard ---------------- */
static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,
    'q','w','e','r','t','y','u','i','o','p','[',']',13,0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,0,0,' ',0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char keymap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+',8,9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,0,0,' ',0,0
};

static int kbd_shift = 0, kbd_ctrl = 0, kbd_alt = 0;
static int kbd_e0 = 0;
static int kbd_ru = 0;

/* Russian ЙЦУКЕН layout: scancode -> Unicode codepoint */
static const u16 ru_low[128] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
    0x0439,0x0446,0x0443,0x043A,0x0435,0x043D,0x0433,0x0448,0x0449,0x0437,0x0445,0x044A,0,0,
    0x0444,0x044B,0x0432,0x0430,0x043F,0x0440,0x043E,0x043B,0x0434,0x0436,0x044D,0,0x0451,
    0x044F,0x0447,0x0441,0x043C,0x0438,0x0442,0x044C,0x0431,0x044E,'.',0,0,0,' ',0,0
};
static const u16 ru_up[128] = {
    0,0,'!','"',0x2116,';','%',':','?','*','(',')','_','+',0,0,
    0x0419,0x0426,0x0423,0x041A,0x0415,0x041D,0x0413,0x0428,0x0429,0x0417,0x0425,0x042A,0,0,
    0x0424,0x042B,0x0412,0x0410,0x041F,0x0420,0x041E,0x041B,0x0414,0x0416,0x042D,0,0x0401,
    0x042F,0x0427,0x0421,0x041C,0x0418,0x0422,0x042C,0x0411,0x042E,',',0,0,0,' ',0,0
};

static void key_press(int code) {
    YoroEvent e;
    char c;
    memset(&e, 0, sizeof(e));
    e.type = EV_KEY;
    e.shift = kbd_shift;
    e.ctrl = kbd_ctrl;
    e.alt = kbd_alt;

    switch (code) {
    case 0x01: e.key = KEY_ESC; break;
    case 0x0E: e.key = KEY_BACKSP; break;
    case 0x0F: e.key = KEY_TAB; break;
    case 0x1C: e.key = KEY_ENTER; break;
    case 0x2A: case 0x36: kbd_shift = 1; return;
    case 0x1D: kbd_ctrl = 1; return;
    case 0x38: kbd_alt = 1; return;
    case 0x3A: return;
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
    case 0x45: return;                     /* numlock */
    case 0x48: e.key = KEY_UP; break;
    case 0x50: e.key = KEY_DOWN; break;
    case 0x4B: e.key = KEY_LEFT; break;
    case 0x4D: e.key = KEY_RIGHT; break;
    case 0x53: e.key = KEY_DEL; break;
    case 0x47: e.key = KEY_HOME; break;
    case 0x4F: e.key = KEY_END; break;
    default:
        if (code > 0 && code < 128) {
            if (kbd_ru && ru_low[code]) {
                int cp = kbd_shift ? ru_up[code] : ru_low[code];
                e.key = cp;
                e.ch = cp;
            } else {
                c = kbd_shift ? keymap_shift[code] : keymap[code];
                if (!c) return;
                e.key = (int)(unsigned char)c;
                e.ch = (unsigned char)c;
            }
            break;
        }
        return;
    }
    input_push(&e);
}

void kbd_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0xE0) { kbd_e0 = 1; return; }
    if (sc & 0x80) {
        u8 make = sc & 0x7F;
        if (make == 0x2A || make == 0x36) kbd_shift = 0;
        if (make == 0x1D) kbd_ctrl = 0;
        if (make == 0x38) kbd_alt = 0;
        kbd_e0 = 0;
        return;
    }
    if (kbd_e0) {
        kbd_e0 = 0;
        YoroEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_KEY;
        e.shift = kbd_shift; e.ctrl = kbd_ctrl; e.alt = kbd_alt;
        if (sc == 0x4B) { e.key = KEY_LEFT; input_push(&e); }
        if (sc == 0x4D) { e.key = KEY_RIGHT; input_push(&e); }
        if (sc == 0x48) { e.key = KEY_UP; input_push(&e); }
        if (sc == 0x50) { e.key = KEY_DOWN; input_push(&e); }
        if (sc == 0x53) { e.key = KEY_DEL; input_push(&e); }
        if (sc == 0x47) { e.key = KEY_HOME; input_push(&e); }
        if (sc == 0x4F) { e.key = KEY_END; input_push(&e); }
        return;
    }
    key_press(sc);
}

void kbd_toggle_layout(void) { kbd_ru = !kbd_ru; }

/* ---------------- mouse ---------------- */
static int mouse_x = 512, mouse_y = 384;
static int mouse_pkt[3];
static int mouse_idx = 0;
static int mouse_btn_state = 0;

static void mouse_wait(u8 type) {
    u32 t = 200000;
    while (t--) {
        u8 s = inb(0x64);
        if (type == 0) { if ((s & 2) == 0) return; }
        else { if (s & 1) return; }
    }
}

static void mouse_write(u8 v) {
    mouse_wait(0);
    outb(0x64, 0xD4);      /* next byte is addressed to the aux device */
    mouse_wait(0);
    outb(0x60, v);
    mouse_wait(1);
    inb(0x60);             /* acknowledge */
}

void mouse_init(void) {
    u8 status;
    int tries;

    /* enable the auxiliary (mouse) port */
    mouse_wait(0);
    outb(0x64, 0xA8);
    mouse_wait(0);

    /* read the controller configuration byte and enable both IRQs */
    outb(0x64, 0x20);
    mouse_wait(1);
    status = inb(0x60);
    status = (u8)((status & ~0x23) | 0x03);   /* kbd irq + aux irq, aux port enabled */
    mouse_wait(0);
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);

    /* reset the mouse: expect 0xFA (ack), 0xAA (self test), 0x00 (id) */
    mouse_write(0xFF);
    for (tries = 0; tries < 3; tries++) { mouse_wait(1); inb(0x60); }

    mouse_write(0xF6);                 /* set defaults */
    mouse_write(0xF3); mouse_write(80);/* sample rate */
    mouse_write(0xE8); mouse_write(3); /* resolution 8 counts/mm */
    mouse_write(0xF4);                 /* enable data reporting */
    mouse_idx = 0;
    {
        /* read back the controller config byte to verify the aux IRQ bit */
        mouse_wait(0);
        outb(0x64, 0x20);
        mouse_wait(1);
        status = inb(0x60);
    }
    UNUSED(status);
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }

void mouse_irq(void) {
    u8 status = inb(0x64);
    if (!(status & 0x20)) return;
    u8 data = inb(0x60);
    mouse_pkt[mouse_idx++] = data;
    if (mouse_idx < 3) return;
    mouse_idx = 0;

    int flags = mouse_pkt[0];
    if (flags & 0xC0) return;             /* overflow */
    int dx = (signed char)mouse_pkt[1];
    int dy = (signed char)mouse_pkt[2];
    int btn = flags & 7;

    if (dx || dy) {
        mouse_x += dx;
        mouse_y -= dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > (int)fb.width - 1) mouse_x = (int)fb.width - 1;
        if (mouse_y > (int)fb.height - 1) mouse_y = (int)fb.height - 1;
        YoroEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_MOUSE_MOVE;
        e.x = mouse_x; e.y = mouse_y;
        input_push(&e);
    }
    if (btn != mouse_btn_state) {
        int pressed = btn & ~mouse_btn_state;
        int released = mouse_btn_state & ~btn;
        YoroEvent e;
        memset(&e, 0, sizeof(e));
        e.x = mouse_x; e.y = mouse_y;
        if (pressed) {
            e.type = EV_MOUSE_DOWN;
            e.btn = 1;
            if (pressed & 2) e.btn = 2;
            input_push(&e);
        }
        if (released) {
            e.type = EV_MOUSE_UP;
            e.btn = 1;
            if (released & 2) e.btn = 2;
            input_push(&e);
        }
        mouse_btn_state = btn;
    }
}

/* ---------------- PIT ---------------- */
static void pit_init(u32 hz) {
    u32 div = 1193180 / hz;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
}

/* ---------------- CMOS clock ---------------- */
static u8 cmos_read(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}
static int bcd(u8 v) { return (v & 0x0F) + ((v >> 4) * 10); }

void rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year) {
    while (cmos_read(0x0A) & 0x80) {}
    u8 s = cmos_read(0x00), m = cmos_read(0x02), h = cmos_read(0x04);
    u8 d = cmos_read(0x07), mo = cmos_read(0x08), y = cmos_read(0x09);
    u8 fmt = cmos_read(0x0B);
    if (!(fmt & 0x04)) {
        s = (u8)bcd(s); m = (u8)bcd(m); d = (u8)bcd(d);
        mo = (u8)bcd(mo); y = (u8)bcd(y);
        h = (u8)(((h & 0x0F) + (((h >> 4) & 7) * 10)) | (h & 0x80));
    }
    if (!(fmt & 0x02) && (h & 0x80)) h = (u8)(((h & 0x7F) + 12) % 24);
    *hour = h; *min = m; *sec = s;
    *day = d; *mon = mo; *year = 2000 + y;
}

/* ---------------- IRQ / exception dispatch ---------------- */
/* Layout must match the stack frame built by kernel/isr.asm:
   gs, fs, es, ds, pusha(edi..eax), vector, err, eip, cs, eflags */
typedef struct {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 vector, err, eip, cs, eflags;
} Regs;

u32 sys_ticks(void) { return tick_count; }
int mouse_state(void) { return mouse_btn_state; }

void isr_handler_c(Regs *r) {
    /* Unrecoverable CPU exception: show a kernel panic screen */
    cli();
    Rect full = {0, 0, (int)fb.width, (int)fb.height};
    fill_rect(full, RGB(12, 18, 40));
    char buf[128];
    sprintf(buf, "KERNEL EXCEPTION 0x%x", r->vector);
    draw_text(40, 40, buf, RGB(255, 90, 90));
    draw_text(40, 70, "YoroOS has stopped to protect your files.", RGB(220, 220, 230));
    sprintf(buf, "eip=0x%x  err=0x%x  cs=0x%x", r->eip, r->err, r->cs);
    draw_text(40, 96, buf, RGB(170, 180, 200));
    draw_text(40, 122, "Press the reset button of the machine.", RGB(170, 180, 200));
    gfx_present();
    for (;;) hlt();
}

void irq_handler_c(Regs *r) {
    int irq = (int)r->vector - 32;
    if (irq == 0) {
        tick_count++;
        YoroEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_TICK;
        input_push(&e);
    } else if (irq == 1) {
        kbd_irq();
    } else if (irq == 12) {
        mouse_irq();
    }
    if (irq >= 8) outb(PIC2, 0x20);
    outb(PIC1, 0x20);
}

/* ---------------- debug serial (COM1) ---------------- */
void serial_init(void) {
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
void serial_puts(const char *s) {
    while (*s) { if (*s == '\n') serial_putc('\r'); serial_putc(*s++); }
}
void klog(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_puts(buf);
    serial_puts("\n");
}

/* ---------------- graphics init ---------------- */
void gfx_init(void) {
    volatile BootInfo *bi = BOOTINFO;
    fb.width = bi->width;
    fb.height = bi->height;
    fb.pitch = bi->pitch;
    fb.bpp = (u8)bi->bpp;
    fb.red_pos = (u8)bi->red_pos; fb.red_size = (u8)bi->red_size;
    fb.green_pos = (u8)bi->green_pos; fb.green_size = (u8)bi->green_size;
    fb.blue_pos = (u8)bi->blue_pos; fb.blue_size = (u8)bi->blue_size;
    fb.vram = (u8 *)bi->framebuffer;
    fb.bstride = fb.width * 4;
    fb.dual = 0;
    {
        u32 need = fb.bstride * fb.height;
        u8 *buf = (u8 *)kmalloc(need);
        if (buf) {
            fb.back = buf;
            memset(buf, 0, need);
            fb.dual = 1;
        } else {
            /* no memory for a back buffer: draw straight into video memory
             * (only sane for 32 bpp; the compositor still works, just slowly) */
            fb.back = fb.vram;
            fb.bstride = fb.pitch;
        }
    }
}

/* ---------------- boot ---------------- */
void kmain(void) {
    cli();
    serial_init();
    serial_puts("\n[YoroOS] kmain\n");
    gdt_init();
    idt_init();
    pic_remap();
    pit_init(100);
    mouse_init();

    klog("[YoroOS] gdt/idt/pic/pit ready, memkb=%d", (int)MEMKB);
    heap_init(0x100000, (MEMKB > 4096 ? MEMKB - 2048 : 2048) * 1024);
    klog("[YoroOS] heap %d KiB", (int)heap_total() / 1024);
    gfx_init();
    klog("[YoroOS] fb %dx%d bpp=%d dual=%d rgbpos=%d,%d,%d rgbmask=%d,%d,%d", (int)fb.width, (int)fb.height, (int)fb.bpp, fb.dual, (int)fb.red_pos, (int)fb.green_pos, (int)fb.blue_pos, (int)fb.red_size, (int)fb.green_size, (int)fb.blue_size);
    ki_boot_splash();
    klog("[YoroOS] splash done");
    ata_init();
    fs_init();
    fs_seed();
    theme_init();
    apps_init();
    desktop_init();
    wm_init();
    klog("[YoroOS] subsystems ready -> loop");

    sti();
    main_loop();
}
