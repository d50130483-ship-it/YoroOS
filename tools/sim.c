/* Хост-симулятор YoroOS: гоняет сценарии ввода и рендерит кадры в PNG */
#include <stdio.h>
#include <stdlib.h>
#include "klib.h"
#include "hw.h"
#include "gfx.h"
#include "ui.h"
#include "fs.h"

/* ---- заглушки железа ---- */
volatile u32 pit_ticks = 0;
void outb(u16 p, u8 v) { (void)p; (void)v; }
u8 inb(u16 p) { (void)p; return 0; }
void io_wait(void) {}
void insw(u16 p, void *a, u32 c) { (void)p; (void)a; (void)c; }
void outsw(u16 p, const void *a, u32 c) { (void)p; (void)a; (void)c; }
void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    if (leaf == 0) {
        *a = 1;
        memcpy(b, "Yoro", 4);
        memcpy(c, "CPU!", 4);
        memcpy(d, "Engn", 4);
    } else { *a = *b = *c = *d = 0; }
}

static int m_buttons = 0, m_dx = 0, m_dy = 0;
void mouse_get(int *buttons, int *dx, int *dy) { *buttons = m_buttons; *dx = m_dx; *dy = m_dy; }
void mouse_consume(void) { m_dx = m_dy = 0; }
int mouse_moved(void) { return 0; }
static int cur_mx = 380, cur_my = 260;
void sim_mouse(int dx, int dy, int buttons) { m_dx += dx; m_dy += dy; m_buttons = buttons; cur_mx += dx; cur_my += dy; pit_ticks += 3; }
static void sim_mouse_abs(int x, int y, int buttons) { sim_mouse(x - cur_mx, y - cur_my, buttons); }

unsigned last_hz;
void spk_on(unsigned hz) { (void)last_hz; last_hz = hz; }
void spk_off(void) { last_hz = 0; }
void spk_play(unsigned hz, u32 ticks) { (void)ticks; last_hz = hz; }

void rtc_get(int *sec, int *min, int *hour, int *day, int *mon, int *year) {
    *sec = 30; *min = 25; *hour = 21; *day = 21; *mon = 10; *year = 2026;
}

/* ---- клавиатура ---- */
#define KBQ 256
static struct { int code, ascii; } kbq[KBQ];
static int kbqh, kbqt;
int kbd_pop(int *code, int *ascii) {
    if (kbqh == kbqt) return 0;
    *code = kbq[kbqt].code;
    *ascii = kbq[kbqt].ascii;
    kbqt = (kbqt + 1) % KBQ;
    return 1;
}
void hw_init(void) {}
int hw_pump(void) { return 0; }
void hw_halt(void) { printf("[HALT]\n"); exit(0); }
void hw_sleep_wait(void) {}
void hw_reboot(void) { printf("[REBOOT]\n"); exit(2); }
void pit_tick(void) {}
u32 pit_uptime_s(void) { return pit_ticks / 100; }

char *yoro_strcpy(char *dst, const char *src, int max) {
    int n = (int)strlen(src);
    if (n > max - 1) n = max - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return dst;
}

int sim_dbg = 0;
int sim_dbg2 = 0;

/* ---- утилиты сценария ---- */
static void key(int code, int ascii) {
    kbq[kbqh].code = code;
    kbq[kbqh].ascii = ascii;
    kbqh = (kbqh + 1) % KBQ;
}
static void typestr(const char *utf8) {
    const char *p = utf8;
    unsigned cp;
    while ((cp = utf8_next(&p)) != 0) {
        if (cp == '\n') key(K_ENTER, 0);
        else key(0, (int)cp);
    }
}
static void frames(int n);

static void drag(int x0, int y0, int x1, int y1) {
    sim_mouse(x0 - 380, y0 - 260, 1);
    frames(2);
    sim_mouse((x1 - x0) / 2, (y1 - y0) / 2, 1);
    frames(1);
    sim_mouse(x1 - (x1 + x0) / 2, y1 - (y1 + y0) / 2, 1);
    frames(1);
    sim_mouse(0, 0, 0);
    frames(1);
}

static void click(int x, int y) {
    sim_mouse_abs(x, y, 1);
    frames(2);
    sim_mouse(0, 0, 0);
    frames(1);
}
static void frames(int n) {
    for (int i = 0; i < n; i++) {
        pit_ticks++;
        ui_anim_tick();
        if (installer_active()) installer_frame();
        else ui_frame();
    }
}
static void dump(const char *name) {
    char path[128];
    snprintf(path, sizeof(path), "out/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror("out/"); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", gfx_w(), gfx_h());
    extern u8 *gfx_fb_ptr(void);
    extern int gfx_pitch(void);
    for (int y = 0; y < gfx_h(); y++) {
        const u8 *row = gfx_fb_ptr() + (size_t)y * gfx_pitch();
        for (int x = 0; x < gfx_w(); x++) {
            const u8 *p = row + x * 4;
            u8 rgb[3] = { p[2], p[1], p[0] };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("кадр: out/%s.ppm\n", name);
}

extern void ata_sim_load(const char *path);
extern void ata_sim_save(const char *path);

static u8 fbbuf[MAXW * MAXH * 4];

static void sys_boot(void) {
    gfx_init(fbbuf, 800, 600, 800 * 4);
    hw_init();
    fs_init();
    int fresh = !fs_valid();
    if (!fresh) settings_load();
    gfx_wall_redraw(g_theme);
    ui_init();
    if (fresh) installer_start();
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "all";
    system("mkdir -p out");
    ata_sim_load("/tmp/yoro_disk.bin");
    sys_boot();

    if (!strcmp(scenario, "all") || !strcmp(scenario, "install")) {
        frames(30);
        dump("inst-1-welcome");
        key(K_ENTER, 0); frames(3);
        dump("inst-2-license");
        key(K_ENTER, 0); frames(3);
        typestr("Ёжик");
        frames(3);
        dump("inst-3-user");
        key(K_ENTER, 0); frames(3);
        key(0, 0); /* ничего */
        key(K_RIGHT, 0); key(K_RIGHT, 0); frames(3);
        dump("inst-4-theme");
        key(K_ENTER, 0); frames(20);
        dump("inst-5-progress");
        frames(400);
        dump("inst-6-done");
        key(K_ENTER, 0);
        frames(40);
        dump("desktop");
        printf("[debug] g_theme=%d\n", g_theme);
    }
    if (!strcmp(scenario, "all") || !strcmp(scenario, "apps")) {
        /* терминал */
        app_launch(AP_TERMINAL);
        frames(5);
        typestr("neofetch\n");
        frames(3);
        typestr("ls\n");
        frames(3);
        typestr("cpuinfo\n");
        frames(3);
        dump("app-terminal");
        /* заметки */
        app_launch(AP_NOTES);
        frames(3);
        typestr("Привет из YoroOS! Это заметка на настоящем ядре.\n");
        frames(3);
        dump("app-notes");
        /* музыка */
        app_launch(AP_MUSIC);
        frames(2);
        key(K_ENTER, 0);
        frames(60);
        dump("app-music");
        /* калькулятор */
        app_launch(AP_CALC);
        sim_dbg = 1;
        frames(2);
        typestr("128*64=");
        frames(3);
        dump("app-calc");
        sim_dbg = 0;
        /* календарь */
        app_launch(AP_CALENDAR);
        frames(2);
        typestr("Встреча с Ядром\n");
        frames(3);
        dump("app-calendar");
        /* finder */
        app_launch(AP_FINDER);
        frames(3);
        dump("app-finder");
        /* настройки */
        app_launch(AP_SETTINGS);
        frames(3);
        dump("app-settings");
        /* меню YoroOS */
        app_close_app(AP_SETTINGS);
        app_close_app(AP_FINDER);
        app_close_app(AP_CALENDAR);
        app_close_app(AP_CALC);
        app_close_app(AP_MUSIC);
        app_close_app(AP_NOTES);
        app_close_app(AP_TERMINAL);
        sim_mouse_abs(12, 11, 0);
        frames(2);
        sim_mouse(0, 0, 1);
        frames(2);
        sim_mouse(0, 0, 0);
        frames(3);
        dump("menu-open");
    }
    if (!strcmp(scenario, "all") || !strcmp(scenario, "mouse")) {
        /* drag окна за шапку */
        app_launch(AP_FINDER);
        frames(4);
        int wx = 50, wy = 36; /* каскад №0 */
        drag(wx + 200, wy + 10, wx + 330, wy + 120);
        frames(2);
        dump("mouse-drag");
        /* maximize зелёной кнопкой */
        {
            /* окно теперь в nx=wx+130, ny=wy+110; зелёная кнопка: центр (nx+28, ny+10) */
            int nx = wx + 130, ny = wy + 110;
            sim_mouse_abs(nx + 28, ny + 10, 0);
            frames(1);
            sim_mouse(0, 0, 1);
            frames(1);
            sim_mouse(0, 0, 0);
            frames(2);
        }
        dump("mouse-max");
        /* клик по иконке Терминала в доке (абсолютные координаты) */
        {
            int x0 = (800 - 7 * 56) / 2;
            int dx = x0 + 1 * 56 + 28;
            sim_mouse_abs(dx, 570, 0);
            frames(1);
            sim_mouse(0, 0, 1);
            frames(2);
            sim_mouse(0, 0, 0);
            frames(3);
        }
        dump("mouse-dock");
    }
    ata_sim_save("/tmp/yoro_disk.bin");
    printf("сценарий '%s' завершён, файлов в ФС: %d\n", scenario, fs_count());
    return 0;
}
