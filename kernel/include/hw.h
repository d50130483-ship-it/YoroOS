#ifndef HW_H
#define HW_H
#include "klib.h"

/* ---- порты (в симуляторе заменяются заглушками) ---- */
#ifndef YORO_SIM
static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void insw(u16 port, void *addr, u32 cnt) {
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}
static inline void outsw(u16 port, const void *addr, u32 cnt) {
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port) : "memory");
}
static inline void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}
#else
void outb(u16 port, u8 val);
u8 inb(u16 port);
void io_wait(void);
void insw(u16 port, void *addr, u32 cnt);
void outsw(u16 port, const void *addr, u32 cnt);
void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d);
#endif

/* ---- особые клавиши (code), printable -> ascii>0 ---- */
enum { K_UP = 1, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BS, K_ESC, K_DEL, K_TAB };

void hw_init(void);
void isr_dispatch(int n);           /* вызывается из isr.S */
int hw_pump(void);                  /* 1 если были события ввода */
void hw_halt(void);
void hw_reboot(void);
void hw_sleep_wait(void);           /* hlt */

/* время */
extern volatile u32 pit_ticks;      /* тики 100 Гц */
void pit_tick(void);
u32 pit_uptime_s(void);

/* RTC */
void rtc_get(int *sec, int *min, int *hour, int *day, int *mon, int *year);

/* клавиатура */
void kbd_irq_byte(u8 b);
int kbd_pop(int *code, int *ascii); /* 1 = событие */

/* мышь */
void mouse_irq_byte(u8 b);
void mouse_get(int *buttons, int *dx, int *dy);
void mouse_consume(void);
int mouse_moved(void);

/* PC-динамик */
void spk_on(unsigned hz);
void spk_off(void);
void spk_play(unsigned hz, u32 ticks);

/* диск */
int ata_read(u32 lba, u32 count, void *buf);
int ata_write(u32 lba, u32 count, const void *buf);

#endif
