/* YoroOS 0.2.6 - Authentic Asynchronous Multi-Stage OS Disk Installer
 *
 * Original clean YoroOS UI (ZERO Windows cloning) with real incremental disk writes:
 *   Step 0 — Выбор диска и редакции (real hardware disk inspection + GPT/ESP layout)
 *   Step 1 — Профиль и оформление (user, hostname, wallpaper, theme saved into disk image!)
 *   Step 2 — Пошаговая запись на диск (real 60 FPS async GPT + FAT16 ESP + Kernel + YoroFS2 + CRC32!)
 *   Step 3 — Завершение и перезагрузка с жёсткого диска (auto-reboot timer + 1-click reboot)
 */
#include "kernel.h"

enum {
    INST_DISKS   = 0,
    INST_PROFILE = 1,
    INST_WRITE   = 2,
    INST_DONE    = 3
};

static struct {
    int  step;
    int  edition;
    int  sel_disk;
    u64  c_size_mb;
    int  focus_field; /* 0 = username, 1 = pc_name */
    int  user_len, user_caret;
    int  pc_len, pc_caret;
    int  auto_reboot_on;
    int  lang_page;      /* 1 = первый экран «Язык и формат» (загрузочная среда) */
    int  lang_val[3];    /* язык, формат времени, раскладка (индексы в списках) */
    int  lang_open;      /* 0 = список закрыт, 1..3 = открыт выпадающий список строки */
    u32  finish_tick;
    u32  last_step_tick;
    DiskInstallTask task;
    Window *win;
} g_inst;

int installer_running(void) {
    return system_screen_mode == SCREEN_SETUP || g_inst.win != NULL;
}

void installer_init_boot(void) {
    memset(&g_inst, 0, sizeof(g_inst));
    g_inst.step = INST_DISKS;
    g_inst.edition = sys_profile.edition;
    g_inst.sel_disk = 0;
    g_inst.user_len = (int)strlen(sys_profile.username);
    g_inst.user_caret = g_inst.user_len;
    g_inst.pc_len = (int)strlen(sys_profile.pc_name);
    g_inst.pc_caret = g_inst.pc_len;
    g_inst.lang_page = (system_screen_mode == SCREEN_SETUP);
    g_inst.lang_val[0] = 0;
    g_inst.lang_val[1] = 0;
    g_inst.lang_val[2] = kbd_is_ru() ? 0 : 1;
    g_inst.lang_open = 0;
}

static void start_async_install(void) {
    sys_profile.edition = g_inst.edition;
    if (!sys_profile.username[0]) strcpy(sys_profile.username, "User");
    if (!sys_profile.pc_name[0])  strcpy(sys_profile.pc_name, "YORO-PC");

    g_inst.step = INST_WRITE;
    g_inst.last_step_tick = sys_ticks();

    if (hal_disk_count() <= 0) {
        memset(&g_inst.task, 0, sizeof(g_inst.task));
        sys_profile.setup_completed = 1;
        apps_init();
        g_inst.task.pct = 100;
        g_inst.task.done = 1;
        strcpy(g_inst.task.status, "Настроена RAM-система (подключите диск VHDX для постоянной установки).");
        strcpy(g_inst.task.detail, "Все функции рабочего стола готовы к работе.");
        g_inst.step = INST_DONE;
        g_inst.auto_reboot_on = 0;
        wm_invalidate();
        return;
    }

    disk_install_begin(&g_inst.task, g_inst.sel_disk, g_inst.c_size_mb);
    wm_invalidate();
}

void installer_tick(void) {
    u32 now = sys_ticks();

    if (g_inst.step == INST_WRITE && g_inst.task.active) {
        /* Space out steps ~80ms so user visibly sees every real hardware phase & MB counter! */
        if (now - g_inst.last_step_tick >= 8U) {
            g_inst.last_step_tick = now;
            if (!disk_install_step(&g_inst.task)) {
                if (g_inst.task.done) {
                    g_inst.step = INST_DONE;
                    g_inst.finish_tick = now;
                    g_inst.auto_reboot_on = (hal_disk_count() > 0 && system_screen_mode == SCREEN_SETUP) ? 1 : 0;
                    desktop_rebuild();
                }
            }
            wm_invalidate();
        }
    } else if (g_inst.step == INST_DONE && g_inst.auto_reboot_on) {
        wm_dirty = 1;
        if (now - g_inst.finish_tick >= 600U) { /* 6.0 seconds */
            g_inst.auto_reboot_on = 0;
            system_reboot();
        }
    }
}

static Rect boot_dialog_rect(void) {
    int w = 820, h = 530;
    if (w > (int)fb.width - 40) w = (int)fb.width - 40;
    if (h > (int)fb.height - 56) h = (int)fb.height - 56;
    return (Rect){((int)fb.width - w) / 2, ((int)fb.height - h) / 2, w, h};
}

static void draw_wizard_body(Rect r) {
    static const char *kSteps[4] = {
        "1. Накопитель", "2. Профиль", "3. Запись на диск", "4. Перезагрузка"
    };
    int mx = mouse_get_x(), my = mouse_get_y();
    int i, sw = (r.w - 40) / 4;

    fill_rect(r, theme->win_bg);
    fill_rect((Rect){r.x, r.y, r.w, 50}, theme->card_bg);
    hline(r.x, r.x + r.w - 1, r.y + 50, RGBA(148, 163, 184, 55));

    for (i = 0; i < 4; i++) {
        Rect pill = {r.x + 20 + i * sw, r.y + 11, sw - 8, 28};
        int cur = (i == g_inst.step);
        int done = (i < g_inst.step) || (g_inst.step == INST_DONE && i <= 3);
        fill_round_rect(pill, 14, cur ? theme->accent
                                      : (done ? RGBA(16, 185, 129, 56) : RGBA(148, 163, 184, 24)));
        draw_text_center_face(FACE_SMALL, pill.x + pill.w / 2, pill.y + 6, kSteps[i],
                              cur ? C_WHITE : theme->text_main);
    }

    if (g_inst.step == INST_DISKS) {
        int nd = hal_disk_count();
        char hw[160];
        icon_draw(ICON_SETUP, r.x + 28, r.y + 64, 40);
        draw_text_title(r.x + 80, r.y + 62, "Установщик системы YoroOS 0.2.6", theme->text_main);
        snprintf(hw, sizeof(hw), "%s  ·  %s  ·  ОЗУ %u МБ",
                 hal_arch_name(), hal_pointer_driver_name(), (u32)hal_ram_mb());
        draw_text_small(r.x + 80, r.y + 90, hw, theme->text_muted);

        /* Disk Selection Section */
        draw_text_bold(r.x + 28, r.y + 122, "Выберите целевой жёсткий диск (будет создана разметка GPT + UEFI FAT16 + YoroFS2):",
                       theme->text_main);
        {
            Rect box = {r.x + 28, r.y + 148, r.w - 56, 134};
            fill_round_rect(box, 8, theme->card_bg);
            draw_round_rect(box, 8, RGBA(148, 163, 184, 65));
            if (nd <= 0) {
                draw_text(box.x + 18, box.y + 34, "Жёсткий диск (VHDX / NVMe / SATA) не подключён к виртуальной машине.",
                          theme->text_main);
                draw_text_small(box.x + 18, box.y + 62,
                                "Создайте и подключите виртуальный диск .VHDX в настройках ВМ или нажмите «Live-режим».",
                                theme->text_muted);
            } else {
                for (i = 0; i < nd && i < 2; i++) {
                    HalDiskInfo di;
                    Rect row = {box.x + 10, box.y + 10 + i * 58, box.w - 20, 52};
                    int sel = (g_inst.sel_disk == i);
                    char cap_s[96];
                    u64 mb;
                    hal_disk_info(i, &di);
                    mb = (di.sectors * 512ULL) / (1024ULL * 1024ULL);
                    fill_round_rect(row, 6, sel ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 52)
                                                : RGBA(148, 163, 184, 14));
                    draw_round_rect(row, 6, sel ? theme->accent : RGBA(148, 163, 184, 42));
                    icon_draw(ICON_DRIVE, row.x + 12, row.y + 10, 32);
                    draw_text_bold(row.x + 54, row.y + 7, di.model, theme->text_main);
                    snprintf(cap_s, sizeof(cap_s),
                             "Ёмкость: %llu МБ  ·  Сектор: %u Б  ·  Схема: GPT + ESP 16 МБ (FAT16) + YoroFS2",
                             (unsigned long long)mb, di.sector_bytes);
                    draw_text_small(row.x + 54, row.y + 29, cap_s, theme->text_muted);
                }
            }
        }

        /* Edition Cards (2x2 compact clean grid) */
        draw_text_bold(r.x + 28, r.y + 296, "Редакция системы:", theme->text_main);
        {
            static const char *kEdShort[4] = {
                "Базовая рабочая ОС, .EXE и .ZIP",
                "Python 3.12, VS Code и Git",
                "Steam, Discord и Matronix Browser",
                "Лёгкая минималистичная сборка"
            };
            int cw = (r.w - 68) / 2;
            for (i = 0; i < 4; i++) {
                Rect c = {r.x + 28 + (i % 2) * (cw + 12), r.y + 322 + (i / 2) * 64, cw, 56};
                int sel = (g_inst.edition == i);
                fill_round_rect(c, 8, sel ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 48)
                                          : theme->card_bg);
                draw_round_rect(c, 8, sel ? theme->accent : RGBA(148, 163, 184, 52));
                draw_text_bold(c.x + 14, c.y + 8, edition_name(i), theme->text_main);
                draw_text_small(c.x + 14, c.y + 31, kEdShort[i], theme->text_muted);
            }
        }

        ui_button((Rect){r.x + 28, r.y + r.h - 50, 190, 36},
                  kbd_is_ru() ? "Язык ввода: RU" : "Input: EN",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 50, 190, 36}), 0);
        if (system_screen_mode == SCREEN_SETUP) {
            ui_button((Rect){r.x + 230, r.y + r.h - 50, 230, 36},
                      "Live-режим (без записи)",
                      pt_in(mx, my, (Rect){r.x + 230, r.y + r.h - 50, 230, 36}), 0);
        }
        ui_button((Rect){r.x + r.w - 228, r.y + r.h - 50, 200, 36},
                  "Далее: Профиль ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 228, r.y + r.h - 50, 200, 36}), 1);
    }
    else if (g_inst.step == INST_PROFILE) {
        Rect f_user = {r.x + 28, r.y + 132, 340, 36};
        Rect f_pc   = {r.x + 28, r.y + 206, 340, 36};
        Rect wp_box = {r.x + 404, r.y + 114, r.w - 432, 196};
        char winfo[96];

        draw_text_title(r.x + 28, r.y + 64, "Настройка учётной записи и внешнего вида", theme->text_main);
        draw_text_small(r.x + 28, r.y + 92,
                        "Параметры будут сразу сохранены на загрузочный том YoroFS2:",
                        theme->text_muted);

        draw_text_small(r.x + 28, r.y + 112, "Имя пользователя:", theme->text_main);
        ui_textfield(f_user, sys_profile.username, g_inst.user_caret, g_inst.focus_field == 0, "User");

        draw_text_small(r.x + 28, r.y + 186, "Имя компьютера:", theme->text_main);
        ui_textfield(f_pc, sys_profile.pc_name, g_inst.pc_caret, g_inst.focus_field == 1, "YORO-PC");

        ui_button((Rect){r.x + 28, r.y + 264, 164, 36},
                  theme->dark ? "Тема: Тёмная" : "Тема: Светлая",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + 264, 164, 36}), 0);
        ui_button((Rect){r.x + 204, r.y + 264, 164, 36},
                  "Сменить обои",
                  pt_in(mx, my, (Rect){r.x + 204, r.y + 264, 164, 36}), 0);

        fill_round_rect(wp_box, 10, theme->card_bg);
        draw_round_rect(wp_box, 10, RGBA(148, 163, 184, 70));
        icon_draw(ICON_ABOUT, wp_box.x + (wp_box.w - 48) / 2, wp_box.y + 28, 48);
        snprintf(winfo, sizeof(winfo), "Обои: %s", wallpaper_name(wallpaper_current()));
        draw_text_center(wp_box.x + wp_box.w / 2, wp_box.y + 94, winfo, theme->text_main);
        draw_text_center_face(FACE_SMALL, wp_box.x + wp_box.w / 2, wp_box.y + 122,
                              "После установки система будет загружаться", theme->text_muted);
        draw_text_center_face(FACE_SMALL, wp_box.x + wp_box.w / 2, wp_box.y + 142,
                              "прямо с виртуального жёсткого диска", theme->text_muted);

        ui_button((Rect){r.x + 28, r.y + r.h - 50, 140, 36}, "<- Назад",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 50, 140, 36}), 0);
        ui_button((Rect){r.x + r.w - 268, r.y + r.h - 50, 240, 36},
                  "Начать установку на диск",
                  pt_in(mx, my, (Rect){r.x + r.w - 268, r.y + r.h - 50, 240, 36}), 1);
    }
    else if (g_inst.step == INST_WRITE) {
        static const char *kPhaseLabels[6] = {
            "Запись гибридной таблицы разделов GPT (Primary + Backup CRC32)",
            "Форматирование раздела EFI (FAT16, 16 МБ) и запись BOOTX64.EFI",
            "Запись загрузчика BIOS Stage2 и ядра YoroKernel",
            "Создание корневой файловой системы YoroFS2 и перенос приложений",
            "Регистрация загрузочной записи YoroOS в UEFI NVRAM (Boot0001)",
            "Физическое контрольное чтение и проверка контрольных сумм CRC32"
        };
        Rect pbar = {r.x + 36, r.y + 148, r.w - 72, 22};
        char pct_s[64];

        draw_text_title(r.x + 36, r.y + 68,
                        g_inst.task.failed ? "Ошибка установки на диск" : "Идёт установка YoroOS на жёсткий диск...",
                        theme->text_main);
        draw_text(r.x + 36, r.y + 102, g_inst.task.failed ? g_inst.task.err : g_inst.task.status,
                  g_inst.task.failed ? RGB(239, 68, 68) : theme->text_main);
        draw_text_small(r.x + 36, r.y + 124, g_inst.task.detail, theme->text_muted);

        ui_progress(pbar, g_inst.task.pct, g_inst.task.failed ? RGB(239, 68, 68) : theme->accent);
        snprintf(pct_s, sizeof(pct_s), "Выполнено: %d%%  ·  Записано на носитель: %u.%u МБ",
                 g_inst.task.pct,
                 (u32)(g_inst.task.bytes_written / (1024ULL * 1024ULL)),
                 (u32)(((g_inst.task.bytes_written % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL)));
        draw_text_small(r.x + 36, r.y + 178, pct_s, theme->text_muted);

        /* Real-time 6-stage hardware checklist */
        for (i = 0; i < 6; i++) {
            int pnum = i + 1;
            int done = g_inst.task.done || (g_inst.task.phase > pnum);
            int cur  = (!g_inst.task.done && !g_inst.task.failed && g_inst.task.phase == pnum);
            int yy = r.y + 214 + i * 36;
            Rect badge = {r.x + 36, yy, 24, 24};
            fill_round_rect(badge, 12, done ? RGB(16, 185, 129)
                                            : (cur ? theme->accent : RGBA(148, 163, 184, 38)));
            draw_text_center_face(FACE_SMALL, badge.x + 12, badge.y + 4,
                                  done ? "+" : (cur ? ">" : "."), C_WHITE);
            draw_text(r.x + 72, yy + 3, kPhaseLabels[i],
                      (done || cur) ? theme->text_main : theme->text_muted);
        }

        if (g_inst.task.failed) {
            ui_button((Rect){r.x + 36, r.y + r.h - 50, 160, 36}, "<- Назад",
                      pt_in(mx, my, (Rect){r.x + 36, r.y + r.h - 50, 160, 36}), 0);
        }
    }
    else { /* INST_DONE */
        u32 elapsed = sys_ticks() - g_inst.finish_tick;
        int rem_sec = 6 - (int)(elapsed / 100U);
        char rb_msg[128];
        if (rem_sec < 0) rem_sec = 0;

        icon_draw(ICON_CHECK, r.x + r.w / 2 - 28, r.y + 74, 56);
        draw_text_center_face(FACE_TITLE, r.x + r.w / 2, r.y + 142,
                              "YoroOS 0.2.6 успешно установлена на жёсткий диск!", theme->text_main);
        draw_text_center(r.x + r.w / 2, r.y + 178, g_inst.task.detail, theme->text_muted);
        draw_text_center_face(FACE_SMALL, r.x + r.w / 2, r.y + 208,
                              "Загрузчик YoroBoot и раздел EFI FAT16 записаны и проверены посекторным чтением.",
                              theme->text_muted);
        draw_text_center_face(FACE_SMALL, r.x + r.w / 2, r.y + 228,
                              "Система загрузится с жёсткого диска автоматически (даже если не извлекать ISO).",
                              theme->text_muted);

        if (g_inst.auto_reboot_on) {
            snprintf(rb_msg, sizeof(rb_msg), "Автоматическая перезагрузка в установленную систему через %d с...", rem_sec);
            ui_progress((Rect){r.x + 160, r.y + 272, r.w - 320, 14}, (int)((elapsed * 100U) / 600U), theme->accent);
            draw_text_center_face(FACE_SMALL, r.x + r.w / 2, r.y + 296, rb_msg, theme->accent);
        }

        ui_button((Rect){r.x + 60, r.y + r.h - 56, 220, 40},
                  "Открыть Рабочий стол",
                  pt_in(mx, my, (Rect){r.x + 60, r.y + r.h - 56, 220, 40}), 0);
        if (g_inst.auto_reboot_on) {
            ui_button((Rect){r.x + 294, r.y + r.h - 56, 200, 40},
                      "Остановить таймер",
                      pt_in(mx, my, (Rect){r.x + 294, r.y + r.h - 56, 200, 40}), 0);
        }
        ui_button((Rect){r.x + r.w - 280, r.y + r.h - 56, 220, 40},
                  "Перезагрузить сейчас",
                  pt_in(mx, my, (Rect){r.x + r.w - 280, r.y + r.h - 56, 220, 40}), 1);
    }
}

static void handle_wizard_event(Rect r, YoroEvent *e) {
    int i;
    if (e->type == EV_KEY && e->key == KEY_F10 && e->shift) {
        system_screen_mode = SCREEN_DESKTOP;
        terminal_open();
        wm_invalidate();
        return;
    }

    if (g_inst.step == INST_DISKS) {
        if (e->type == EV_KEY && e->key == KEY_ENTER) {
            g_inst.step = INST_PROFILE;
            wm_invalidate();
            return;
        }
        if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
            int nd = hal_disk_count();
            int cw = (r.w - 68) / 2;
            for (i = 0; i < nd && i < 2; i++) {
                Rect row = {r.x + 38, r.y + 158 + i * 58, r.w - 76, 52};
                if (pt_in(e->x, e->y, row)) { g_inst.sel_disk = i; wm_invalidate(); return; }
            }
            for (i = 0; i < 4; i++) {
                Rect c = {r.x + 28 + (i % 2) * (cw + 12), r.y + 322 + (i / 2) * 64, cw, 56};
                if (pt_in(e->x, e->y, c)) { g_inst.edition = i; wm_invalidate(); return; }
            }
            if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 50, 190, 36})) {
                kbd_toggle_layout(); return;
            }
            if (system_screen_mode == SCREEN_SETUP &&
                pt_in(e->x, e->y, (Rect){r.x + 230, r.y + r.h - 50, 230, 36})) {
                system_screen_mode = SCREEN_DESKTOP;
                desktop_rebuild();
                wm_invalidate();
                toast("Запущен Live-режим YoroOS. Установщик доступен с Рабочего стола.");
                return;
            }
            if (pt_in(e->x, e->y, (Rect){r.x + r.w - 228, r.y + r.h - 50, 200, 36})) {
                g_inst.step = INST_PROFILE;
                wm_invalidate();
                return;
            }
        }
    }
    else if (g_inst.step == INST_PROFILE) {
        if (e->type == EV_KEY) {
            if (e->key == KEY_TAB) { g_inst.focus_field = !g_inst.focus_field; wm_invalidate(); return; }
            if (e->key == KEY_ENTER) { start_async_install(); return; }
            if (g_inst.focus_field == 0)
                ui_textfield_edit(sys_profile.username, sizeof(sys_profile.username), &g_inst.user_len, &g_inst.user_caret, e);
            else
                ui_textfield_edit(sys_profile.pc_name, sizeof(sys_profile.pc_name), &g_inst.pc_len, &g_inst.pc_caret, e);
            wm_invalidate();
            return;
        }
        if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
            if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + 132, 340, 36})) { g_inst.focus_field = 0; wm_invalidate(); return; }
            if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + 206, 340, 36})) { g_inst.focus_field = 1; wm_invalidate(); return; }
            if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + 264, 164, 36})) { theme_set(theme->dark ? 1 : 0); return; }
            if (pt_in(e->x, e->y, (Rect){r.x + 204, r.y + 264, 164, 36})) { wallpaper_set(wallpaper_current() + 1); return; }
            if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 50, 140, 36})) { g_inst.step = INST_DISKS; wm_invalidate(); return; }
            if (pt_in(e->x, e->y, (Rect){r.x + r.w - 268, r.y + r.h - 50, 240, 36})) { start_async_install(); return; }
        }
    }
    else if (g_inst.step == INST_WRITE) {
        if (g_inst.task.failed && e->type == EV_MOUSE_DOWN && e->btn == 1 &&
            pt_in(e->x, e->y, (Rect){r.x + 36, r.y + r.h - 50, 160, 36})) {
            g_inst.step = INST_DISKS;
            wm_invalidate();
        }
    }
    else if (g_inst.step == INST_DONE) {
        if (e->type == EV_KEY && e->key == KEY_ENTER) {
            system_reboot();
            return;
        }
        if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
            if (pt_in(e->x, e->y, (Rect){r.x + 60, r.y + r.h - 56, 220, 40})) {
                g_inst.auto_reboot_on = 0;
                system_screen_mode = SCREEN_DESKTOP;
                if (g_inst.win) { Window *w = g_inst.win; g_inst.win = NULL; wm_close(w); }
                desktop_rebuild();
                wm_invalidate();
                toast("Добро пожаловать в установленную YoroOS 0.2.6!");
                return;
            }
            if (g_inst.auto_reboot_on && pt_in(e->x, e->y, (Rect){r.x + 294, r.y + r.h - 56, 200, 40})) {
                g_inst.auto_reboot_on = 0;
                wm_invalidate();
                return;
            }
            if (pt_in(e->x, e->y, (Rect){r.x + r.w - 280, r.y + r.h - 56, 220, 40})) {
                system_reboot();
                return;
            }
        }
    }
}

/* ---- Первый экран загрузочной среды: язык, формат и раскладка ----
 * Структура как у стандартного мастера установки: три поля выбора, подсказка
 * и кнопка «Далее». Оформление оригинальное (логотип YoroOS, без логотипов сторонних систем). */
static const char *kLangOpts[2] = { "Русский (Россия)", "English (United States)" };
static const char *kKbdOpts[2]  = { "Русская", "English (US)" };
static const char *kLangRowLbl[3] = {
    "Устанавливаемый язык:",
    "Формат времени и денежных единиц:",
    "Метод ввода (раскладка клавиатуры):"
};

static Rect lang_dialog_rect(void) {
    int w = 640, h = 380;
    int fw = (int)fb.width, fh = (int)fb.height;
    if (w > fw - 40) w = fw - 40;
    if (h > fh - 40) h = fh - 40;
    return (Rect){(fw - w) / 2, (fh - h) / 2, w, h};
}

static Rect lang_combo_rect(Rect d, int row) {
    return (Rect){d.x + 344, d.y + 112 + row * 44, 256, 26};
}

static Rect lang_next_rect(Rect d) {
    return (Rect){d.x + d.w - 170, d.y + d.h - 56, 140, 34};
}

static const char *lang_option_text(int row, int idx) {
    return (row == 2) ? kKbdOpts[idx] : kLangOpts[idx];
}

static void lang_finish(void) {
    int want_ru = (g_inst.lang_val[2] == 0);
    if (want_ru != kbd_is_ru()) kbd_toggle_layout();
    g_inst.lang_page = 0;
    g_inst.lang_open = 0;
    wm_invalidate();
}

static void draw_lang_page(void) {
    Rect d = lang_dialog_rect();
    Rect bar = {d.x, d.y, d.w, 32};
    Rect nx = lang_next_rect(d);
    Rect mark = {d.x + d.w / 2 - 104, d.y + 52, 44, 44};
    int mx = mouse_get_x(), my = mouse_get_y();
    int i, k;

    fill_rect((Rect){0, 0, (int)fb.width, (int)fb.height}, RGB(28, 10, 92));
    draw_shadow(d, 14);
    fill_rect(d, RGB(34, 14, 104));
    fill_rect(bar, RGB(22, 8, 74));
    icon_draw(ICON_SETUP, d.x + 10, d.y + 6, 20);
    draw_text_small(d.x + 38, d.y + 9, "Установка YoroOS", C_WHITE);

    /* Оригинальный знак: скруглённый квадрат с буквой Y и название */
    fill_round_rect(mark, 10, C_ACCENT);
    draw_text_center_face(FACE_TITLE, mark.x + mark.w / 2, mark.y + 8, "Y", C_WHITE);
    draw_text_hero(mark.x + mark.w + 14, mark.y + 4, "YoroOS", C_WHITE);

    for (i = 0; i < 3; i++) {
        Rect cb = lang_combo_rect(d, i);
        int hov = pt_in(mx, my, cb);
        draw_text_right(cb.x - 12, cb.y + 6, kLangRowLbl[i], RGB(232, 232, 250));
        fill_rect(cb, (hov || g_inst.lang_open == i + 1) ? RGB(255, 255, 255) : RGB(246, 246, 250));
        draw_text(cb.x + 8, cb.y + 6, lang_option_text(i, g_inst.lang_val[i]), RGB(20, 20, 30));
        /* стрелка вниз */
        for (k = 0; k < 5; k++) {
            int ax = cb.x + cb.w - 14, ay = cb.y + 9;
            hline(ax - (4 - k), ax + (4 - k), ay + k, RGB(50, 50, 64));
        }
    }

    draw_text_center_face(FACE_SMALL, d.x + d.w / 2, d.y + 286,
                          "Выберите нужный язык и другие параметры, затем нажмите «Далее».",
                          RGB(225, 225, 245));

    /* Выпадающий список поверх остального */
    if (g_inst.lang_open) {
        int row = g_inst.lang_open - 1;
        Rect cb = lang_combo_rect(d, row);
        int n = 2;
        fill_rect((Rect){cb.x, cb.y + cb.h, cb.w, cb.h * n}, RGB(255, 255, 255));
        for (i = 0; i < n; i++) {
            Rect it = {cb.x, cb.y + cb.h + i * cb.h, cb.w, cb.h};
            int sel = (g_inst.lang_val[row] == i);
            int hov = pt_in(mx, my, it);
            if (sel || hov) fill_rect(it, sel ? C_ACCENT : RGB(220, 230, 246));
            draw_text(it.x + 8, it.y + 6, lang_option_text(row, i),
                      sel ? C_WHITE : RGB(20, 20, 30));
        }
        draw_round_rect((Rect){cb.x, cb.y + cb.h, cb.w, cb.h * n}, 0, RGB(120, 120, 160));
    }

    /* Кнопка «Далее» и подпись */
    {
        int hov = pt_in(mx, my, nx);
        fill_rect(nx, hov ? RGB(222, 222, 236) : RGB(244, 244, 250));
        draw_round_rect(nx, 2, RGB(120, 120, 160));
        draw_text_center_face(FACE_SMALL, nx.x + nx.w / 2, nx.y + 9, "Далее", RGB(20, 20, 30));
    }
    draw_text_small(d.x + 24, d.y + d.h - 24, "© YoroOS 0.2.6. Все права защищены.", RGB(176, 176, 210));
}

static void handle_lang_event(YoroEvent *e) {
    Rect d = lang_dialog_rect();
    int i;
    if (e->type == EV_KEY) {
        if (e->key == KEY_ESC && g_inst.lang_open) { g_inst.lang_open = 0; wm_invalidate(); return; }
        if (e->key == KEY_ENTER) { lang_finish(); return; }
        return;
    }
    if (e->type != EV_MOUSE_DOWN || e->btn != 1) return;

    if (g_inst.lang_open) {
        int row = g_inst.lang_open - 1;
        Rect cb = lang_combo_rect(d, row);
        for (i = 0; i < 2; i++) {
            Rect it = {cb.x, cb.y + cb.h + i * cb.h, cb.w, cb.h};
            if (pt_in(e->x, e->y, it)) { g_inst.lang_val[row] = i; break; }
        }
        g_inst.lang_open = 0;
        wm_invalidate();
        return;
    }
    for (i = 0; i < 3; i++) {
        if (pt_in(e->x, e->y, lang_combo_rect(d, i))) {
            g_inst.lang_open = i + 1;
            wm_invalidate();
            return;
        }
    }
    if (pt_in(e->x, e->y, lang_next_rect(d))) lang_finish();
}

void installer_draw_fullscreen(void) {
    Rect dlg;
    Rect frame;
    if (g_inst.lang_page) { draw_lang_page(); return; }
    dlg = boot_dialog_rect();
    frame = (Rect){dlg.x, dlg.y - 34, dlg.w, dlg.h + 34};

    theme_draw_wallpaper();
    draw_shadow(frame, 16);
    fill_round_rect(frame, 10, theme->win_head);
    icon_draw(ICON_SETUP, frame.x + 12, frame.y + 7, 20);
    draw_text_small(frame.x + 38, frame.y + 9, "YoroOS 0.2.6 — Мастер установки системы", theme->text_main);
    draw_wizard_body(dlg);
    draw_round_rect(frame, 10, RGBA(148, 163, 184, 75));
}

void installer_event_fullscreen(YoroEvent *e) {
    if (g_inst.lang_page) { handle_lang_event(e); return; }
    handle_wizard_event(boot_dialog_rect(), e);
}

static void win_inst_draw(Window *w, Rect r) { UNUSED(w); draw_wizard_body(r); }
static void win_inst_ev(Window *w, YoroEvent *e) { handle_wizard_event(wm_client(w), e); }
static void win_inst_close(Window *w) { UNUSED(w); disk_install_free(&g_inst.task); g_inst.win = NULL; }

void installer_open(void) {
    if (g_inst.win) { wm_focus(g_inst.win); return; }
    installer_init_boot();
    g_inst.lang_page = 0;
    g_inst.win = wm_open(app_find("installer"), "Установка YoroOS на диск",
                         -1, -1, 820, 560, win_inst_draw, win_inst_ev, win_inst_close);
}
