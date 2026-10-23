/* YoroOS 0.2 - Unified Boot Splash, Toast Engine,Boot Mode Selection & Event Loop */
#include "kernel.h"

int system_screen_mode = SCREEN_SETUP;

typedef struct {
    char text[160];
    u32  expire_tick;
    int  active;
} ToastItem;

static ToastItem g_toast;

void toast(const char *text) {
    if (!text || !*text) return;
    strncpy(g_toast.text, text, sizeof(g_toast.text) - 1);
    g_toast.text[sizeof(g_toast.text) - 1] = 0;
    g_toast.expire_tick = sys_ticks() + 420U; /* ~4.2 seconds */
    g_toast.active = 1;
    wm_invalidate();
}

void toast_draw(void) {
    Rect box;
    int tw;
    if (!g_toast.active) return;
    if (sys_ticks() > g_toast.expire_tick) { g_toast.active = 0; return; }
    tw = text_width(g_toast.text) + 58;
    if (tw < 280) tw = 280;
    if (tw > (int)fb.width - 40) tw = (int)fb.width - 40;
    box = (Rect){(int)fb.width - tw - 18, (int)fb.height - wm_taskbar_height() - 68, tw, 50};
    fx_acrylic_panel(box, 9, RGBA(20, 26, 38, 224), theme->accent, 6);
    icon_draw(ICON_BELL, box.x + 14, box.y + 13, 24);
    draw_text_clipped(box.x + 46, box.y + (box.h - font_line_height(FACE_UI)) / 2,
                      box.w - 58, g_toast.text, C_WHITE);
}

/* Crisp Windows 10/11 UEFI GOP / VBE Boot Splash */
void ki_boot_splash(void) {
    int cx = (int)fb.width / 2;
    int cy = (int)fb.height * 42 / 100;
    int s = 76, q = 34, i;

    for (i = 0; i < 10; i++) {
        gfx_clear(RGB(0, 0, 0));
        fill_round_rect((Rect){cx - s / 2,         cy - s / 2,         q, q}, 4, RGB(0, 168, 255));
        fill_round_rect((Rect){cx - s / 2 + q + 8, cy - s / 2,         q, q}, 4, RGB(48, 206, 255));
        fill_round_rect((Rect){cx - s / 2,         cy - s / 2 + q + 8, q, q}, 4, RGB(0, 132, 232));
        fill_round_rect((Rect){cx - s / 2 + q + 8, cy - s / 2 + q + 8, q, q}, 4, RGB(0, 214, 184));
        draw_text_center_face(FACE_TITLE, cx, cy + 62, "YoroOS 0.2 «Aurora»", C_WHITE);
        draw_text_center_face(FACE_SMALL, cx, cy + 94, hal_arch_name(), RGB(130, 152, 180));
        fx_draw_spinner(cx, cy + 146, 17, (u32)(i * 9), RGB(0, 178, 255));
        gfx_present();
        hal_idle();
    }
}

void kernel_boot_common(void) {
    int disk_installed;
    theme_init();
    ki_boot_splash();

    fs_init();
    apps_init();
    fs_seed();

    /* Check if an installed YoroOS 0.2 volume (YOROFS2) exists on disk! */
    disk_installed = disk_probe_installed_yorofs();
    if (hal_booted_from_disk() && disk_installed >= 0) {
        disk_load_yorofs(disk_installed);
        system_screen_mode = SCREEN_DESKTOP;
    } else if (disk_installed >= 0 && disk_load_yorofs(disk_installed) == 0 && sys_profile.setup_completed) {
        system_screen_mode = SCREEN_DESKTOP;
    } else {
        /* Booted from Live/Install ISO on fresh VM -> open serious Windows Setup Wizard! */
        system_screen_mode = SCREEN_SETUP;
    }

    desktop_init();
    wm_init();
    installer_init_boot();
    wm_invalidate();
}

void main_loop(void) {
    u32 last_clock_sec = 0;
    u32 last_tick_step = 0;
    wm_frame();
    for (;;) {
        YoroEvent e;
        int got = 0;
        u32 cur_ticks;
        hal_poll_input();
        while (input_pop(&e)) {
            got = 1;
            if (e.type == EV_TICK) {
                wm_tick();
            } else {
                wm_handle_event(&e);
            }
        }
        cur_ticks = sys_ticks();
        if (cur_ticks != last_tick_step) {
            last_tick_step = cur_ticks;
            wm_tick();
        }
        /* Refresh clock every second & expire toast */
        if (cur_ticks / 100U != last_clock_sec) {
            last_clock_sec = cur_ticks / 100U;
            wm_dirty = 1;
        }
        if (wm_dirty)
            wm_frame();
        installer_tick();
        if (wm_dirty)
            wm_frame();
        if (!got)
            hal_idle();
    }
}
