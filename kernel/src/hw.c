/* Железо YoroOS: IDT, PIC, PIT, клавиатура PS/2, мышь PS/2, RTC, динамик */
#include "hw.h"
#include "ui.h"

/* ---------- IDT ---------- */
struct idt_entry {
    u16 lo; u16 sel; u8 zero; u8 flags; u16 hi;
} __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));

static struct idt_entry idt[48];
static struct idt_ptr idtp;
extern void *isr_table[48];

void isr_dispatch(int n) {
    if (n >= 40) outb(0xA0, 0x20);
    if (n >= 32) outb(0x20, 0x20);
    if (n == 32) pit_tick();
    else if (n == 33) kbd_irq_byte(inb(0x60));
    else if (n == 44) mouse_irq_byte(inb(0x60));
}

static void idt_set(int i, u32 handler) {
    idt[i].lo = handler & 0xFFFF;
    idt[i].sel = 0x08;
    idt[i].zero = 0;
    idt[i].flags = 0x8E;
    idt[i].hi = handler >> 16;
}

/* ---------- PIT ---------- */
volatile u32 pit_ticks = 0;
static u32 spk_until = 0;
static unsigned spk_hz = 0;

void pit_tick(void) {
    pit_ticks++;
    if (spk_hz && (s32)(pit_ticks - spk_until) >= 0) spk_off();
    ui_anim_tick();
}

u32 pit_uptime_s(void) { return pit_ticks / 100; }

/* ---------- RTC ---------- */
static u8 cmos(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}
static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }

void rtc_get(int *sec, int *min, int *hour, int *day, int *mon, int *year) {
    while (cmos(0x0A) & 0x80) {}
    *sec = bcd(cmos(0));
    *min = bcd(cmos(2));
    *hour = bcd(cmos(4));
    *day = bcd(cmos(7));
    *mon = bcd(cmos(8));
    *year = 2000 + bcd(cmos(9));
}

/* ---------- PC-динамик ---------- */
void spk_on(unsigned hz) {
    if (hz < 20) hz = 20;
    u32 div = 1193182u / hz;
    if (div > 0x10000) div = 0x10000;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 3);
}
void spk_off(void) {
    spk_hz = 0;
    outb(0x61, inb(0x61) & ~3);
}
void spk_play(unsigned hz, u32 ticks) {
    if (hz) {
        spk_on(hz);
        spk_hz = hz;
        spk_until = pit_ticks + ticks;
    } else {
        spk_off();
    }
}

/* ---------- клавиатура PS/2 (сканкоды set 1) ---------- */
static const char kb_map[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
};
static const char kb_map_shift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0
};

#define KB_BUF 64
static struct { int code; int ascii; } kb_queue[KB_BUF];
static volatile int kb_qh = 0, kb_qt = 0;
static int kb_shift = 0, kb_ext = 0;
/* русская раскладка (ЙЦУКЕН), переключение Alt+Shift упрощено до клавиши ScrollLock/правый Alt */
static int kb_ru = 0;
static const char kb_ru_map[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'й', 'ц', 'у', 'к', 'е', 'н', 'г', 'ш', 'щ', 'з', 'х', 'ъ', '\n', 0, 'ф', 'ы',
    'в', 'а', 'п', 'р', 'о', 'л', 'д', 'ж', 'э', 'ё', 0, '\\', 'я', 'ч', 'с', 'м',
    'и', 'т', 'ь', 'б', 'ю', '.', 0, '*', 0, ' ', 0
};
static const char kb_ru_shift[128] = {
    0, 0, '!', '"', '№', ';', '%', ':', '?', '*', '(', ')', '_', '+', '\b', '\t',
    'Й', 'Ц', 'У', 'К', 'Е', 'Н', 'Г', 'Ш', 'Щ', 'З', 'Х', 'Ъ', '\n', 0, 'Ф', 'Ы',
    'В', 'А', 'П', 'Р', 'О', 'Л', 'Д', 'Ж', 'Э', 'Ё', 0, '/', 'Я', 'Ч', 'С', 'М',
    'И', 'Т', 'Ь', 'Б', 'Ю', ',', 0, '*', 0, ' ', 0
};

static void kb_push(int code, int ascii) {
    int nh = (kb_qh + 1) % KB_BUF;
    if (nh == kb_qt) return;
    kb_queue[kb_qh].code = code;
    kb_queue[kb_qh].ascii = ascii;
    kb_qh = nh;
}

int kbd_pop(int *code, int *ascii) {
    if (kb_qt == kb_qh) return 0;
    *code = kb_queue[kb_qt].code;
    *ascii = kb_queue[kb_qt].ascii;
    kb_qt = (kb_qt + 1) % KB_BUF;
    return 1;
}

void kbd_irq_byte(u8 b) {
    if (b == 0xE0) { kb_ext = 1; return; }
    int rel = b & 0x80;
    u8 c = b & 0x7F;
    if (kb_ext) {
        kb_ext = 0;
        if (rel) return;
        switch (c) {
        case 0x48: kb_push(K_UP, 0); break;
        case 0x50: kb_push(K_DOWN, 0); break;
        case 0x4B: kb_push(K_LEFT, 0); break;
        case 0x4D: kb_push(K_RIGHT, 0); break;
        case 0x53: kb_push(K_DEL, 0); break;
        }
        return;
    }
    if (c == 0x2A || c == 0x36) { kb_shift |= 1; return; }
    if (c == 0xAA || c == 0xB6) { kb_shift &= ~1; return; }
    if (c == 0x38) { if (!rel) kb_ru ^= 1; return; } /* Alt переключает раскладку */
    if (c == 0x1D || c == 0x5C) return; /* Ctrl */
    if (rel) return;
    if (c == 0x1C) { kb_push(K_ENTER, 0); return; }
    if (c == 0x0E) { kb_push(K_BS, 0); return; }
    if (c == 0x01) { kb_push(K_ESC, 0); return; }
    if (c == 0x0F) { kb_push(K_TAB, 0); return; }
    if (c < 128) {
        if (kb_ru && kb_ru_map[c]) {
            int cp = kb_ru_map[c];
            if (kb_shift) {
                if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
                else if (cp == 0x451) cp = 0x401;
            }
            kb_push(0, cp);
        } else {
            const char *m = kb_shift ? kb_map_shift : kb_map;
            if (m[c]) kb_push(0, m[c]);
        }
    }
}

/* ---------- мышь PS/2 ---------- */
static int m_buttons = 0, m_dx = 0, m_dy = 0;
static int m_changed = 0;
static volatile int m_state = 0;
static u8 m_pkt[3];

void mouse_irq_byte(u8 b) {
    switch (m_state) {
    case 0:
        if (b & 0x08) { m_pkt[0] = b; m_state = 1; }
        break;
    case 1:
        m_pkt[1] = b; m_state = 2;
        break;
    case 2:
        m_pkt[2] = b; m_state = 0;
        if (m_pkt[0] & 0xC0) return; /* overflow */
        {
            int dx = m_pkt[1], dy = m_pkt[2];
            if (m_pkt[0] & 0x10) dx |= 0xFFFFFF00;
            if (m_pkt[0] & 0x20) dy |= 0xFFFFFF00;
            m_buttons = m_pkt[0] & 7;
            m_dx += dx;
            m_dy -= dy;
            m_changed = 1;
        }
        break;
    }
}

void mouse_get(int *buttons, int *dx, int *dy) {
    *buttons = m_buttons;
    *dx = m_dx;
    *dy = m_dy;
}
void mouse_consume(void) { m_dx = m_dy = 0; m_changed = 0; }
int mouse_moved(void) { return m_changed; }

static int mouse_ack_timeout(void) {
    int t = 100000;
    while (t--) if (inb(0x64) & 1) return inb(0x60);
    return -1;
}
static void mouse_cmd(int v) {
    outb(0x64, 0xD4);
    outb(0x60, (u8)v);
    mouse_ack_timeout();
}

/* ---------- init / pump ---------- */
static int had_input = 0;

void hw_init(void) {
    int i;
    for (i = 0; i < 48; i++) idt_set(i, (u32)isr_table[i]);
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u32)idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));

    /* PIC */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xF8); /* IRQ0,1,2 */
    outb(0xA1, 0xEF); /* IRQ12 */

    /* PIT 100 Гц */
    outb(0x43, 0x36);
    outb(0x40, 11932 & 0xFF);
    outb(0x40, 11932 >> 8);

    /* мышь */
    outb(0x64, 0xA8);
    {
        u8 cfg;
        outb(0x64, 0x20);
        cfg = inb(0x60) | 2;   /* IRQ12 */
        cfg &= ~0x20;          /* тактирование мыши */
        outb(0x64, 0x60);
        outb(0x60, cfg);
    }
    mouse_ack_timeout();
    mouse_cmd(0xF6);
    mouse_cmd(0xF4);

    __asm__ volatile("sti");
}

int hw_pump(void) {
    int any = had_input || mouse_moved();
    had_input = 0;
    return any;
}
void hw_note_input(void) { had_input = 1; }

void hw_halt(void) { for (;;) __asm__ volatile("hlt"); }
void hw_sleep_wait(void) { __asm__ volatile("hlt"); }

void hw_reboot(void) {
    spk_off();
    int t = 500000;
    while (inb(0x64) & 2 && t--) {}
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}
