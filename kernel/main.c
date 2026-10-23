/* YoroOS 0.2.2 - YoroBoot Graphical Boot Manager, Kernel Loader & Event Loop
 *
 * ZERO Windows 4-square logos anywhere.
 * Provides:
 *   1. Authentic interactive YoroBoot 0.2.2 Boot Menu (keyboard + mouse + 2.5s auto-boot):
 *      - Automatically detects installed YoroFS2 volumes on VHDX / NVMe / SATA disks
 *        and boots directly into the installed OS even if the Live ISO remains attached!
 *      - Cleanly boots OS Installer or Live RAM Desktop on fresh unformatted VMs.
 *   2. Step-by-step progressive kernel module loading bar.
 */
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
    g_toast.expire_tick = sys_ticks() + 420U;
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
    box = (Rect){(int)fb.width - tw - 18, (int)fb.height - wm_taskbar_height() - 68, tw, 48};
    fx_acrylic_panel(box, 10, RGBA(17, 24, 39, 232), theme->accent, 6);
    icon_draw(ICON_BELL, box.x + 14, box.y + 12, 24);
    draw_text_clipped(box.x + 46, box.y + (box.h - font_line_height(FACE_UI)) / 2,
                      box.w - 58, g_toast.text, C_WHITE);
}

/* Draw clean original YoroOS geometric circle-'Y' emblem at any size */
static void draw_yoro_boot_emblem(int cx, int cy, int rad) {
    fill_circle(cx, cy, rad, RGB(59, 130, 246));
    fill_circle(cx, cy, rad - 3, RGB(79, 70, 229));
    /* Crisp minimalist 'Y' stem + arms */
    {
        int d;
        for (d = -3; d <= 3; d++) {
            draw_line(cx - rad * 36 / 100 + d, cy - rad * 34 / 100, cx + d, cy + 2, C_WHITE);
            draw_line(cx + rad * 36 / 100 + d, cy - rad * 34 / 100, cx + d, cy + 2, C_WHITE);
            vline(cx + d, cy, cy + rad * 42 / 100, C_WHITE);
        }
    }
}

void ki_boot_splash(void) {
    static const char *kStages[5] = {
        "Инициализация физического аллокатора и таблиц CPU...",
        "Подключение шин VMBus / UEFI / контроллеров ввода...",
        "Сканирование разделов GPT и накопителей...",
        "Загрузка шрифтового атласа и графической подсистемы...",
        "Запуск окружения YoroOS 0.2.2..."
    };
    int cx = (int)fb.width / 2;
    int cy = (int)fb.height * 40 / 100;
    int i;

    for (i = 0; i < 5; i++) {
        gfx_clear(RGB(11, 15, 25));
        draw_yoro_boot_emblem(cx, cy, 34);
        draw_text_center_face(FACE_TITLE, cx, cy + 54, "YoroOS 0.2.2", C_WHITE);
        draw_text_center_face(FACE_SMALL, cx, cy + 84, hal_arch_name(), RGB(148, 163, 184));
        ui_progress((Rect){cx - 170, cy + 122, 340, 8}, (i + 1) * 20, RGB(56, 189, 248));
        draw_text_center_face(FACE_SMALL, cx, cy + 142, kStages[i], RGB(100, 116, 139));
        gfx_present();
        hal_idle();
    }
}

/* Interactive YoroBoot Menu:
 * Returns: 0 = Boot Installed Disk, 1 = Graphical OS Installer, 2 = Live Desktop
 */
static int run_yoroboot_menu(int disk_installed) {
    int n_items = (disk_installed >= 0) ? 3 : 2;
    int sel = 0; /* 0 = Boot Installed OS if present, else OS Installer! */
    int timer_paused = 0;
    u32 start_tick = sys_ticks();
    u32 timeout_ticks = 220U; /* 2.2 seconds crisp auto-boot */

    for (;;) {
        YoroEvent e;
        int i, mw = 640, mh = 370;
        Rect card;
        u32 now;
        HalDiskInfo di;
        char labels[3][96];
        char subs[3][112];
        char hw_line[160];

        if (mw > (int)fb.width - 40) mw = (int)fb.width - 40;
        card = (Rect){((int)fb.width - mw) / 2, ((int)fb.height - mh) / 2, mw, mh};

        if (disk_installed >= 0) {
            memset(&di, 0, sizeof(di));
            hal_disk_info(disk_installed, &di);
            snprintf(labels[0], sizeof(labels[0]), "Загрузить YoroOS 0.2.2 с жёсткого диска");
            snprintf(subs[0], sizeof(subs[0]), "Установленная система на %s (том YoroFS2)", di.model);
            snprintf(labels[1], sizeof(labels[1]), "Установщик / Восстановление YoroOS");
            snprintf(subs[1], sizeof(subs[1]), "Переразметка GPT, переустановка загрузчика или смена редакции");
            snprintf(labels[2], sizeof(labels[2]), "Запустить чистую Live-сессию в ОЗУ");
            snprintf(subs[2], sizeof(subs[2]), "Работа без монтирования и изменения жёсткого диска");
        } else {
            snprintf(labels[0], sizeof(labels[0]), "Установить YoroOS 0.2.2 на жёсткий диск");
            snprintf(subs[0], sizeof(subs[0]), "Разметка GPT, создание системного раздела UEFI FAT16 и запись ОС");
            snprintf(labels[1], sizeof(labels[1]), "Запустить YoroOS 0.2.2 Live (без установки)");
            snprintf(subs[1], sizeof(subs[1]), "Мгновенный запуск рабочего стола в оперативной памяти");
        }

        hal_poll_input();
        while (input_pop(&e)) {
            if (e.type == EV_KEY) {
                timer_paused = 1;
                if (e.key == KEY_UP) { sel = (sel + n_items - 1) % n_items; }
                else if (e.key == KEY_DOWN || e.key == KEY_TAB) { sel = (sel + 1) % n_items; }
                else if (e.ch == '1') { sel = 0; goto chosen; }
                else if (e.ch == '2' && n_items >= 2) { sel = 1; goto chosen; }
                else if (e.ch == '3' && n_items >= 3) { sel = 2; goto chosen; }
                else if (e.key == KEY_ENTER) { goto chosen; }
            } else if (e.type == EV_MOUSE_MOVE) {
                for (i = 0; i < n_items; i++) {
                    Rect row = {card.x + 28, card.y + 114 + i * 66, card.w - 56, 58};
                    if (pt_in(e.x, e.y, row)) sel = i;
                }
            } else if (e.type == EV_MOUSE_DOWN && e.btn == 1) {
                timer_paused = 1;
                for (i = 0; i < n_items; i++) {
                    Rect row = {card.x + 28, card.y + 114 + i * 66, card.w - 56, 58};
                    if (pt_in(e.x, e.y, row)) { sel = i; goto chosen; }
                }
            }
        }

        now = sys_ticks();
        if (!timer_paused && (now - start_tick >= timeout_ticks))
            break;

        /* Render crisp dark minimalist YoroBoot UI */
        gfx_clear(RGB(11, 15, 25));
        fill_round_rect(card, 12, RGB(17, 24, 39));
        draw_round_rect(card, 12, RGBA(148, 163, 184, 55));

        draw_yoro_boot_emblem(card.x + 48, card.y + 52, 22);
        draw_text_title(card.x + 84, card.y + 30, "YoroBoot — Диспетчер загрузки YoroOS", C_WHITE);
        snprintf(hw_line, sizeof(hw_line), "%s  ·  %s  ·  Дисков: %d",
                 hal_arch_name(), hal_pointer_driver_name(), hal_disk_count());
        draw_text_small(card.x + 84, card.y + 60, hw_line, RGB(148, 163, 184));

        for (i = 0; i < n_items; i++) {
            Rect row = {card.x + 28, card.y + 114 + i * 66, card.w - 56, 58};
            int active = (sel == i);
            fill_round_rect(row, 8, active ? RGBA(59, 130, 246, 56) : RGB(24, 33, 50));
            draw_round_rect(row, 8, active ? RGB(56, 189, 248) : RGBA(148, 163, 184, 38));
            draw_text_bold(row.x + 18, row.y + 9, labels[i], C_WHITE);
            draw_text_small(row.x + 18, row.y + 32, subs[i], RGB(148, 163, 184));
        }

        if (!timer_paused) {
            u32 elapsed = now - start_tick;
            int rem = (int)((timeout_ticks - elapsed + 99U) / 100U);
            char tmsg[128];
            snprintf(tmsg, sizeof(tmsg),
                     "Автоматическая загрузка через %d с  (Стрелки / Мышь — выбор, Enter — запуск)", rem);
            ui_progress((Rect){card.x + 28, card.y + card.h - 46, card.w - 56, 6},
                        (int)((elapsed * 100U) / timeout_ticks), RGB(56, 189, 248));
            draw_text_center_face(FACE_SMALL, card.x + card.w / 2, card.y + card.h - 30,
                                  tmsg, RGB(148, 163, 184));
        } else {
            draw_text_center_face(FACE_SMALL, card.x + card.w / 2, card.y + card.h - 32,
                                  "Нажмите Enter или кликните мышью для загрузки выбранного режима",
                                  RGB(56, 189, 248));
        }

        fx_draw_cursor(mouse_get_x(), mouse_get_y(), mouse_state() & 1);
        gfx_present();
        hal_idle();
    }

chosen:
    if (disk_installed >= 0) {
        if (sel == 0) return 0; /* Boot Installed OS */
        if (sel == 1) return 1; /* Installer */
        return 2;               /* Live Desktop */
    }
    return (sel == 0) ? 1 : 2;
}

void kernel_boot_common(void) {
    int disk_installed, mode_choice;

    theme_init();
    fs_init();
    apps_init();
    fs_seed();

    disk_installed = disk_probe_installed_yorofs();
    mode_choice = run_yoroboot_menu(disk_installed);
    ki_boot_splash();

    if (mode_choice == 0 && disk_installed >= 0 && disk_load_yorofs(disk_installed) == 0) {
        system_screen_mode = SCREEN_DESKTOP;
    } else if (mode_choice == 2) {
        system_screen_mode = SCREEN_DESKTOP;
    } else {
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
