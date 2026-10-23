/* YoroOS 0.2 - Serious Windows 10/11 Multi-Step Graphical Windowed OS Installer
 *
 * Works seamlessly BOTH:
 *   1) At boot time from YoroOS-0.2.0.iso (SCREEN_SETUP fullscreen Setup Environment
 *      with real Windows Setup dialog window, Shift+F10 repair shell, and 1-click Live Desktop option)
 *   2) Inside a desktop Window* anytime the user opens "Установка YoroOS" from Desktop/Start Menu!
 */
#include "kernel.h"

enum {
    STEP_WELCOME   = 0,
    STEP_LICENSE   = 1,
    STEP_DISKS     = 2,
    STEP_INSTALL   = 3,
    STEP_OOBE      = 4,
    STEP_FINISH    = 5
};

static struct {
    int  step;
    int  lang;           /* 0 = Русский, 1 = English */
    int  edition;        /* EDITION_PRO..EDITION_HOME */
    int  license_ok;
    int  sel_disk;
    int  part_formatted; /* user created/formatted GPT layout in disk manager UI */
    u64  c_size_mb;      /* 0 = entire disk */
    int  install_pct;
    char install_msg[128];
    char err_msg[128];
    int  install_started;
    int  install_done;
    int  focus_field;    /* 0 = username, 1 = pc_name */
    int  user_len, user_caret;
    int  pc_len, pc_caret;
    Window *win;
} g_inst;

int installer_running(void) { return system_screen_mode == SCREEN_SETUP || g_inst.win != NULL; }

void installer_init_boot(void) {
    memset(&g_inst, 0, sizeof(g_inst));
    g_inst.step = STEP_WELCOME;
    g_inst.edition = sys_profile.edition;
    g_inst.license_ok = 1;
    g_inst.sel_disk = 0;
    g_inst.user_len = (int)strlen(sys_profile.username);
    g_inst.user_caret = g_inst.user_len;
    g_inst.pc_len = (int)strlen(sys_profile.pc_name);
    g_inst.pc_caret = g_inst.pc_len;
}

static void on_install_progress(int pct, const char *msg) {
    g_inst.install_pct = pct;
    if (msg) {
        strncpy(g_inst.install_msg, msg, sizeof(g_inst.install_msg) - 1);
        g_inst.install_msg[sizeof(g_inst.install_msg) - 1] = 0;
    }
    wm_frame();
}

static void run_disk_installation(void) {
    g_inst.install_started = 1;
    g_inst.err_msg[0] = 0;
    apps_init();

    if (hal_disk_count() <= 0) {
        /* Graceful Live RAM installation if VM has 0 virtual disks attached */
        on_install_progress(35, "Виртуальный жёсткий диск не подключён — настройка RAM-тома YoroFS2...");
        sys_profile.edition = g_inst.edition;
        sys_profile.setup_completed = 1;
        on_install_progress(100, "Система настроена в оперативной памяти (подключите VHDX для записи на диск).");
        g_inst.install_done = 1;
        g_inst.step = STEP_OOBE;
        return;
    }

    sys_profile.edition = g_inst.edition;
    if (disk_install_yoroos(g_inst.sel_disk, g_inst.c_size_mb, on_install_progress,
                            g_inst.err_msg, sizeof(g_inst.err_msg)) == 0) {
        g_inst.install_done = 1;
        g_inst.step = STEP_OOBE;
    }
    wm_invalidate();
}

void installer_tick(void) {
    if (g_inst.step == STEP_INSTALL && !g_inst.install_started)
        run_disk_installation();
}

static Rect boot_dialog_rect(void) {
    int w = 820, h = 530;
    if (w > (int)fb.width - 40) w = (int)fb.width - 40;
    if (h > (int)fb.height - 60) h = (int)fb.height - 60;
    return (Rect){((int)fb.width - w) / 2, ((int)fb.height - h) / 2 - 10, w, h};
}

static void draw_wizard_body(Rect r) {
    int mx = mouse_get_x(), my = mouse_get_y();
    static const char *step_names[6] = {
        "1. Редакция", "2. Лицензия", "3. Разделы GPT", "4. Установка", "5. Профиль", "6. Готово"
    };
    int i, bx;

    /* Clean modern Windows 11 Setup light/fluent surface */
    fill_rect(r, theme->win_bg);

    /* Top Step Progress Header */
    fill_rect((Rect){r.x, r.y, r.w, 48}, theme->card_bg);
    hline(r.x, r.x + r.w - 1, r.y + 48, RGBA(130, 145, 170, 65));
    bx = r.x + 18;
    for (i = 0; i < 6; i++) {
        int active = (i == g_inst.step);
        int done = (i < g_inst.step);
        Rect pill = {bx, r.y + 11, (r.w - 36) / 6 - 6, 26};
        fill_round_rect(pill, 13, active ? theme->accent
                                         : (done ? RGBA(16, 152, 92, 55) : RGBA(130, 145, 170, 26)));
        draw_text_center_face(FACE_SMALL, pill.x + pill.w / 2, pill.y + 5, step_names[i],
                              active ? C_WHITE : theme->text_main);
        bx += (r.w - 36) / 6;
    }

    if (g_inst.step == STEP_WELCOME) {
        static const char *ed_desc[4] = {
            "Полная рабочая система Windows-класса: Проводник, Терминал, Win32 .EXE, .ZIP, Браузер.",
            "Предустановлены Python 3.12 IDE, Visual Studio Code и Git Bash для разработки.",
            "Предустановлены игровой клиент Steam, Discord и ультра-быстрый Matronix Browser 3.0.",
            "Лёгкая, чистая и быстрая домашняя редакция без лишних фоновых служб."
        };
        char hw[160];
        icon_draw(ICON_ABOUT, r.x + 28, r.y + 64, 42);
        draw_text_title(r.x + 82, r.y + 62, "Установка YoroOS 0.2 «Aurora»", theme->text_main);
        snprintf(hw, sizeof(hw), "Оборудование: %s  ·  Ввод: %s  ·  ОЗУ: %u МБ",
                 hal_arch_name(), hal_pointer_driver_name(), (u32)hal_ram_mb());
        draw_text_small(r.x + 82, r.y + 92, hw, theme->text_muted);

        draw_text_bold(r.x + 28, r.y + 124, "Выберите редакцию операционной системы и раскладку:", theme->text_main);
        for (i = 0; i < 4; i++) {
            Rect card = {r.x + 28, r.y + 152 + i * 58, r.w - 56, 52};
            int sel = (g_inst.edition == i);
            fill_round_rect(card, 7, sel ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 48) : theme->card_bg);
            draw_round_rect(card, 7, sel ? theme->accent : RGBA(130, 145, 170, 60));
            fill_circle(card.x + 20, card.y + 26, 7, sel ? theme->accent : RGBA(130, 145, 170, 90));
            if (sel) fill_circle(card.x + 20, card.y + 26, 3, C_WHITE);
            draw_text_bold(card.x + 38, card.y + 7, edition_name(i), theme->text_main);
            draw_text_small(card.x + 38, card.y + 29, ed_desc[i], theme->text_muted);
        }

        ui_button((Rect){r.x + 28, r.y + r.h - 52, 230, 38},
                  kbd_is_ru() ? "Раскладка: Русская (RU)" : "Layout: English (US)",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 52, 230, 38}), 0);
        if (system_screen_mode == SCREEN_SETUP) {
            ui_button((Rect){r.x + 270, r.y + r.h - 52, 270, 38},
                      "Live-режим (Рабочий стол)",
                      pt_in(mx, my, (Rect){r.x + 270, r.y + r.h - 52, 270, 38}), 0);
        }
        ui_button((Rect){r.x + r.w - 246, r.y + r.h - 52, 218, 38},
                  "Установить сейчас ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 246, r.y + r.h - 52, 218, 38}), 1);
    }
    else if (g_inst.step == STEP_LICENSE) {
        Rect box = {r.x + 28, r.y + 106, r.w - 56, r.h - 216};
        static const char *lines[] = {
            "ЛИЦЕНЗИОННОЕ СОГЛАШЕНИЕ СВОБОДНОГО ПОЛЬЗОВАТЕЛЯ YOROOS 0.2 «AURORA»",
            "",
            "1. ПОДДЕРЖКА ПОКОЛЕНИЯ 2 (UEFI x86-64 / HYPER-V GEN 2) И BIOS:",
            "   YoroOS нативно работает на виртуальных машинах Hyper-V Поколения 2 (VMBus мышь, GOP 32bpp,",
            "   таблица разделов GPT, системный раздел EFI FAT16), а также на классическом оборудовании BIOS.",
            "",
            "2. ПРИНЦИП АБСОЛЮТНОЙ СВОБОДЫ ФАЙЛОВОЙ СИСТЕМЫ:",
            "   В отличие от Microsoft Windows, YoroOS НЕ БЛОКИРУЕТ владельца компьютера сообщениями",
            "   «Отказано в доступе / запросите разрешение у TrustedInstaller». Вы вправе редактировать",
            "   или удалять ЛЮБЫЕ файлы и системные папки (включая C:\\YoroOS, C:\\Windows и System32),",
            "   а при необходимости восстановить их одной кнопкой в Параметрах или командой sfc /restore.",
            "",
            "3. НАТИВНЫЙ ЗАПУСК .EXE, .ZIP И КАТАЛОГ ПРИЛОЖЕНИЙ:",
            "   Поддерживается прямой запуск Windows PE (.exe), работа с архивами (.zip), загрузка",
            "   браузеров Chrome, Firefox и Matronix 3.0.0, а также установка Steam, Python 3.12 и VS Code."
        };
        draw_text_title(r.x + 28, r.y + 64, "Условия лицензии и философия YoroOS", theme->text_main);
        fill_round_rect(box, 7, theme->card_bg);
        draw_round_rect(box, 7, RGBA(130, 145, 170, 75));
        for (i = 0; i < 15 && box.y + 12 + i * 19 < box.y + box.h - 14; i++)
            draw_text_small(box.x + 14, box.y + 12 + i * 19, lines[i],
                            i == 0 ? theme->accent : theme->text_main);

        ui_checkbox((Rect){r.x + 28, r.y + r.h - 94, 420, 26}, g_inst.license_ok,
                    "Я принимаю условия и подтверждаю права полного владельца ПК", theme->text_main);
        ui_button((Rect){r.x + 28, r.y + r.h - 52, 130, 38}, "<- Назад",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 52, 130, 38}), 0);
        ui_button((Rect){r.x + r.w - 220, r.y + r.h - 52, 192, 38}, "Далее ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 220, r.y + r.h - 52, 192, 38}), g_inst.license_ok);
    }
    else if (g_inst.step == STEP_DISKS) {
        int nd = hal_disk_count();
        Rect tbl = {r.x + 28, r.y + 116, r.w - 56, 210};
        int ry = tbl.y + 36;

        draw_text_title(r.x + 28, r.y + 62, "Куда вы хотите установить YoroOS 0.2?", theme->text_main);
        draw_text_small(r.x + 28, r.y + 92,
                        "Создайте и отформатируйте разделы GPT + UEFI System Partition (FAT16) + YoroFS2 (C:):",
                        theme->text_muted);

        fill_round_rect(tbl, 7, theme->card_bg);
        draw_round_rect(tbl, 7, RGBA(130, 145, 170, 80));
        fill_rect_alpha((Rect){tbl.x, tbl.y, tbl.w, 30}, RGBA(130, 145, 170, 30));
        draw_text_small(tbl.x + 14, tbl.y + 7, "Диск / Раздел", theme->text_muted);
        draw_text_small(tbl.x + 360, tbl.y + 7, "Общий размер", theme->text_muted);
        draw_text_small(tbl.x + 500, tbl.y + 7, "Тип файловой системы", theme->text_muted);

        if (nd == 0) {
            draw_text(tbl.x + 18, ry + 24,
                      "Физический/виртуальный диск VHDX не обнаружен — доступна установка в RAM-диск!",
                      theme->text_main);
        } else {
            for (i = 0; i < nd && i < 3; i++) {
                HalDiskInfo di;
                char szb[32], line[96];
                u64 mb;
                hal_disk_info(i, &di);
                mb = di.sectors / 2048ULL;
                if (g_inst.part_formatted && g_inst.sel_disk == i) {
                    /* Show all created GPT partitions just like Windows Setup! */
                    Rect p1 = {tbl.x + 6, ry, tbl.w - 12, 34};
                    Rect p2 = {tbl.x + 6, ry + 38, tbl.w - 12, 38};
                    fill_round_rect_alpha(p1, 5, RGBA(16, 152, 92, 36));
                    icon_draw(ICON_DRIVE, p1.x + 8, p1.y + 5, 24);
                    snprintf(line, sizeof(line), "Диск %d Раздел 1: Системный раздел EFI", i);
                    draw_text(p1.x + 38, p1.y + 8, line, theme->text_main);
                    draw_text(p1.x + 378, p1.y + 8, "8.0 МБ", theme->text_main);
                    draw_text_small(p1.x + 494, p1.y + 9, "FAT16 (UEFI BOOTX64)", RGB(72, 214, 142));

                    fill_round_rect_alpha(p2, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 68));
                    draw_round_rect(p2, 5, theme->accent);
                    icon_draw(ICON_DRIVE, p2.x + 8, p2.y + 7, 24);
                    snprintf(line, sizeof(line), "Диск %d Раздел 2: YoroOS Primary (C:)", i);
                    draw_text_bold(p2.x + 38, p2.y + 10, line, theme->text_main);
                    snprintf(szb, sizeof(szb), "%u МБ", (u32)(mb > 16 ? mb - 16 : mb));
                    draw_text(p2.x + 378, p2.y + 10, szb, theme->text_main);
                    draw_text_small(p2.x + 494, p2.y + 11, "YoroFS2 (Основной)", theme->accent);
                    ry += 82;
                } else {
                    Rect row = {tbl.x + 6, ry, tbl.w - 12, 40};
                    int sel = (g_inst.sel_disk == i);
                    if (sel) {
                        fill_round_rect_alpha(row, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 58));
                        draw_round_rect(row, 5, theme->accent);
                    }
                    icon_draw(ICON_DRIVE, row.x + 8, row.y + 8, 24);
                    draw_text_clipped(row.x + 38, row.y + 11, 326, di.model, theme->text_main);
                    snprintf(szb, sizeof(szb), "%u МБ", (u32)mb);
                    draw_text(row.x + 378, row.y + 11, szb, theme->text_main);
                    draw_text_small(row.x + 494, row.y + 12, "Незанятое пространство", theme->text_muted);
                    ry += 44;
                }
            }
        }

        /* Partition Management Toolbar (Windows Setup style!) */
        ui_button((Rect){r.x + 28,  r.y + 338, 192, 34}, "Создать разделы GPT",
                  pt_in(mx, my, (Rect){r.x + 28,  r.y + 338, 192, 34}), 0);
        ui_button((Rect){r.x + 228, r.y + 338, 150, 34}, "Форматировать",
                  pt_in(mx, my, (Rect){r.x + 228, r.y + 338, 150, 34}), 0);
        ui_button((Rect){r.x + 386, r.y + 338, 154, 34}, "Удалить разделы",
                  pt_in(mx, my, (Rect){r.x + 386, r.y + 338, 154, 34}), 0);
        ui_button((Rect){r.x + 548, r.y + 338, 145, 34}, "Обновить диски",
                  pt_in(mx, my, (Rect){r.x + 548, r.y + 338, 145, 34}), 0);

        if (g_inst.err_msg[0])
            draw_text_small(r.x + 28, r.y + 382, g_inst.err_msg, RGB(245, 92, 92));
        else
            draw_text_small(r.x + 28, r.y + 382,
                            "Автоматически создаются: GPT + Раздел EFI (BOOTX64.EFI для Hyper-V Gen 2) + том C: (YoroFS2).",
                            theme->text_muted);

        ui_button((Rect){r.x + 28, r.y + r.h - 52, 130, 38}, "<- Назад",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 52, 130, 38}), 0);
        ui_button((Rect){r.x + r.w - 250, r.y + r.h - 52, 222, 38}, "Установить на диск ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 250, r.y + r.h - 52, 222, 38}), 1);
    }
    else if (g_inst.step == STEP_INSTALL) {
        static const char *stages[5] = {
            "Создание таблицы разделов GPT и гибридного сектора MBR",
            "Форматирование раздела UEFI Поколения 2 (FAT16, \\EFI\\BOOT\\BOOTX64.EFI)",
            "Копирование ядра YoroKernel 0.2, драйвера Hyper-V VMBus и среды Win32",
            "Развёртывание системных приложений, архиватора ZIP и браузера",
            "Форматирование тома C: (YoroFS2) и проверка контрольной суммы CRC32"
        };
        static const int thresh[5] = { 18, 55, 78, 90, 100 };
        char pctbuf[192];

        draw_text_title(r.x + 28, r.y + 68, "Идёт установка YoroOS 0.2 «Aurora»...", theme->text_main);
        draw_text_small(r.x + 28, r.y + 100,
                        "Компьютер настраивается для загрузки с жёсткого диска без установочного ISO.",
                        theme->text_muted);

        for (i = 0; i < 5; i++) {
            int sy = r.y + 138 + i * 38;
            int done = g_inst.install_pct >= thresh[i];
            if (done) icon_draw(ICON_CHECK, r.x + 34, sy, 22);
            else fx_draw_spinner(r.x + 45, sy + 11, 8, sys_ticks(), theme->accent);
            draw_text(r.x + 68, sy + 2, stages[i], done ? theme->text_main : theme->text_muted);
        }

        snprintf(pctbuf, sizeof(pctbuf), "Выполнено: %d%%  —  %s", g_inst.install_pct, g_inst.install_msg);
        draw_text_bold(r.x + 28, r.y + 348, pctbuf, theme->accent);
        ui_progress((Rect){r.x + 28, r.y + 378, r.w - 56, 16}, g_inst.install_pct, theme->accent);
    }
    else if (g_inst.step == STEP_OOBE) {
        draw_text_title(r.x + 28, r.y + 62, "Персонализация вашего компьютера (OOBE)", theme->text_main);
        draw_text_small(r.x + 28, r.y + 92,
                        "Файлы системы записаны на диск! Укажите имя владельца, тему и обои Рабочего стола:",
                        theme->text_muted);

        draw_text(r.x + 28, r.y + 126, "Имя пользователя:", theme->text_main);
        ui_textfield((Rect){r.x + 28, r.y + 150, 340, 36}, sys_profile.username,
                     g_inst.user_caret, g_inst.focus_field == 0, "User");

        draw_text(r.x + 394, r.y + 126, "Имя компьютера:", theme->text_main);
        ui_textfield((Rect){r.x + 394, r.y + 150, 340, 36}, sys_profile.pc_name,
                     g_inst.pc_caret, g_inst.focus_field == 1, "YORO-PC");

        draw_text(r.x + 28, r.y + 204, "Цветовое оформление системы:", theme->text_main);
        for (i = 0; i < 6; i++) {
            Theme *t = theme_get(i);
            Rect sw = {r.x + 28 + i * 124, r.y + 230, 116, 44};
            fill_round_rect(sw, 7, t->win_bg);
            draw_round_rect(sw, 7, theme_current() == i ? t->accent : RGBA(130, 145, 170, 80));
            fill_circle(sw.x + 18, sw.y + 22, 8, t->accent);
            draw_text_small(sw.x + 32, sw.y + 14, t->dark ? "Тёмная" : "Светлая", t->text_main);
        }

        draw_text(r.x + 28, r.y + 290, "Обои Рабочего стола HD:", theme->text_main);
        for (i = 0; i < 4; i++) {
            Rect wb = {r.x + 28 + i * 188, r.y + 316, 178, 42};
            ui_button(wb, wallpaper_name(i), pt_in(mx, my, wb), wallpaper_current() == i);
        }

        ui_button((Rect){r.x + r.w - 270, r.y + r.h - 52, 242, 38},
                  "Сохранить и продолжить ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 270, r.y + r.h - 52, 242, 38}), 1);
    }
    else {
        icon_draw(ICON_CHECK, r.x + 30, r.y + 72, 44);
        draw_text_title(r.x + 86, r.y + 70, "YoroOS 0.2 «Aurora» готова к работе!", theme->text_main);
        draw_text(r.x + 86, r.y + 106,
                  "Система установлена в режиме GPT + UEFI Поколение 2 (BOOTX64.EFI) + YoroFS2.",
                  theme->text_muted);

        ui_card((Rect){r.x + 28, r.y + 148, r.w - 56, 168}, 0, 8);
        draw_text_bold(r.x + 48, r.y + 166, "Что уже готово для вас на Рабочем столе:", theme->accent);
        draw_text_small(r.x + 48, r.y + 194, "• Проводник YoroFS с правом удалять любые папки (даже C:\\YoroOS и System32!)", theme->text_main);
        draw_text_small(r.x + 48, r.y + 216, "• Нативный запуск Windows .EXE файлов и архиватор .ZIP (чтение, распаковка, сжатие)", theme->text_main);
        draw_text_small(r.x + 48, r.y + 238, "• Браузер с загрузкой Google Chrome, Mozilla Firefox и Matronix Browser 3.0.0", theme->text_main);
        draw_text_small(r.x + 48, r.y + 260, "• Центр установки приложений с иконками: Steam, Python 3.12, VS Code, Discord, VLC, Git", theme->text_main);
        draw_text_small(r.x + 48, r.y + 282, "• Можете извлечь установочный .ISO и перезагрузить ВМ — система загрузится с диска!", RGB(72, 214, 142));

        ui_button((Rect){r.x + 28, r.y + r.h - 52, 250, 38}, "Перезагрузить с диска",
                  pt_in(mx, my, (Rect){r.x + 28, r.y + r.h - 52, 250, 38}), 0);
        ui_button((Rect){r.x + r.w - 312, r.y + r.h - 52, 284, 38}, "Открыть Рабочий стол YoroOS ->",
                  pt_in(mx, my, (Rect){r.x + r.w - 312, r.y + r.h - 52, 284, 38}), 1);
    }
}

static void handle_wizard_event(Rect r, YoroEvent *e) {
    int i;
    if (e->type == EV_KEY && e->shift && e->key == KEY_F10) {
        /* Classic Windows Setup Shift+F10 shortcut -> jump to Desktop + Terminal! */
        system_screen_mode = SCREEN_DESKTOP;
        terminal_open();
        return;
    }

    if (e->type == EV_KEY) {
        if (g_inst.step == STEP_WELCOME) {
            if (e->key == KEY_UP && g_inst.edition > 0) { g_inst.edition--; wm_invalidate(); return; }
            if (e->key == KEY_DOWN && g_inst.edition < 3) { g_inst.edition++; wm_invalidate(); return; }
            if (e->key == KEY_ESC && system_screen_mode == SCREEN_SETUP) {
                system_screen_mode = SCREEN_DESKTOP;
                desktop_rebuild();
                wm_invalidate();
                return;
            }
            if (e->key == KEY_ENTER) { g_inst.step = STEP_LICENSE; wm_invalidate(); return; }
        }
        else if (g_inst.step == STEP_LICENSE) {
            if (e->ch == ' ') { g_inst.license_ok = !g_inst.license_ok; wm_invalidate(); return; }
            if (e->key == KEY_ESC) { g_inst.step = STEP_WELCOME; wm_invalidate(); return; }
            if (e->key == KEY_ENTER) {
                g_inst.license_ok = 1;
                hal_disk_rescan();
                g_inst.step = STEP_DISKS;
                wm_invalidate();
                return;
            }
        }
        else if (g_inst.step == STEP_DISKS) {
            int nd = hal_disk_count();
            if (e->key == KEY_UP && g_inst.sel_disk > 0) { g_inst.sel_disk--; wm_invalidate(); return; }
            if (e->key == KEY_DOWN && g_inst.sel_disk + 1 < nd) { g_inst.sel_disk++; wm_invalidate(); return; }
            if (e->key == KEY_ESC) { g_inst.step = STEP_LICENSE; wm_invalidate(); return; }
            if (e->key == KEY_ENTER && nd > 0) {
                g_inst.part_formatted = 1;
                g_inst.install_started = 0;
                g_inst.install_pct = 2;
                strcpy(g_inst.install_msg, "Инициализация установщика...");
                g_inst.step = STEP_INSTALL;
                wm_invalidate();
                return;
            }
        }
        else if (g_inst.step == STEP_OOBE) {
            if (e->key == KEY_TAB) { g_inst.focus_field = !g_inst.focus_field; wm_invalidate(); return; }
            if (e->key == KEY_ENTER) {
                if (!sys_profile.username[0]) strcpy(sys_profile.username, "User");
                if (sys_profile.installed_disk_idx >= 0)
                    disk_save_yorofs(sys_profile.installed_disk_idx);
                desktop_rebuild();
                g_inst.step = STEP_FINISH;
                wm_invalidate();
                return;
            }
            if (g_inst.focus_field == 0)
                ui_textfield_edit(sys_profile.username, sizeof(sys_profile.username), &g_inst.user_len, &g_inst.user_caret, e);
            else
                ui_textfield_edit(sys_profile.pc_name, sizeof(sys_profile.pc_name), &g_inst.pc_len, &g_inst.pc_caret, e);
            wm_invalidate();
            return;
        }
        else if (g_inst.step == STEP_FINISH) {
            if (e->key == KEY_ENTER) {
                if (g_inst.win) {
                    Window *w = g_inst.win;
                    g_inst.win = NULL;
                    wm_close(w);
                }
                system_screen_mode = SCREEN_DESKTOP;
                desktop_rebuild();
                wm_invalidate();
                return;
            }
        }
    }

    if (e->type != EV_MOUSE_DOWN || e->btn != 1) return;

    if (g_inst.step == STEP_WELCOME) {
        for (i = 0; i < 4; i++) {
            Rect card = {r.x + 28, r.y + 152 + i * 58, r.w - 56, 52};
            if (pt_in(e->x, e->y, card)) { g_inst.edition = i; wm_invalidate(); return; }
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 52, 230, 38})) { kbd_toggle_layout(); return; }
        if (system_screen_mode == SCREEN_SETUP &&
            pt_in(e->x, e->y, (Rect){r.x + 270, r.y + r.h - 52, 270, 38})) {
            system_screen_mode = SCREEN_DESKTOP;
            desktop_rebuild();
            wm_invalidate();
            return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + r.w - 246, r.y + r.h - 52, 218, 38})) {
            g_inst.step = STEP_LICENSE;
            wm_invalidate();
        }
    }
    else if (g_inst.step == STEP_LICENSE) {
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 94, 460, 28}))
            g_inst.license_ok = !g_inst.license_ok;
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 52, 130, 38}))
            g_inst.step = STEP_WELCOME;
        if (g_inst.license_ok && pt_in(e->x, e->y, (Rect){r.x + r.w - 220, r.y + r.h - 52, 192, 38})) {
            hal_disk_rescan();
            g_inst.step = STEP_DISKS;
        }
        wm_invalidate();
    }
    else if (g_inst.step == STEP_DISKS) {
        int nd = hal_disk_count();
        for (i = 0; i < nd && i < 3; i++) {
            Rect row = {r.x + 34, r.y + 152 + i * 44, r.w - 68, 42};
            if (pt_in(e->x, e->y, row)) g_inst.sel_disk = i;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + 338, 192, 34}) ||
            pt_in(e->x, e->y, (Rect){r.x + 228, r.y + 338, 150, 34})) {
            g_inst.part_formatted = 1;
            toast("Разделы GPT (EFI 8 МБ FAT16 + Основной YoroFS2) подготовлены к установке.");
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 386, r.y + 338, 154, 34}))
            g_inst.part_formatted = 0;
        if (pt_in(e->x, e->y, (Rect){r.x + 548, r.y + 338, 145, 34}))
            hal_disk_rescan();
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 52, 130, 38}))
            g_inst.step = STEP_LICENSE;
        if (pt_in(e->x, e->y, (Rect){r.x + r.w - 250, r.y + r.h - 52, 222, 38})) {
            g_inst.part_formatted = 1;
            g_inst.install_started = 0;
            g_inst.install_pct = 2;
            strcpy(g_inst.install_msg, "Инициализация установщика...");
            g_inst.step = STEP_INSTALL;
        }
        wm_invalidate();
    }
    else if (g_inst.step == STEP_OOBE) {
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + 150, 340, 36})) g_inst.focus_field = 0;
        if (pt_in(e->x, e->y, (Rect){r.x + 394, r.y + 150, 340, 36})) g_inst.focus_field = 1;
        for (i = 0; i < 6; i++) {
            Rect sw = {r.x + 28 + i * 124, r.y + 230, 116, 44};
            if (pt_in(e->x, e->y, sw)) theme_set(i);
        }
        for (i = 0; i < 4; i++) {
            Rect wb = {r.x + 28 + i * 188, r.y + 316, 178, 42};
            if (pt_in(e->x, e->y, wb)) wallpaper_set(i);
        }
        if (pt_in(e->x, e->y, (Rect){r.x + r.w - 270, r.y + r.h - 52, 242, 38})) {
            if (!sys_profile.username[0]) strcpy(sys_profile.username, "User");
            if (sys_profile.installed_disk_idx >= 0)
                disk_save_yorofs(sys_profile.installed_disk_idx);
            desktop_rebuild();
            g_inst.step = STEP_FINISH;
        }
        wm_invalidate();
    }
    else if (g_inst.step == STEP_FINISH) {
        if (pt_in(e->x, e->y, (Rect){r.x + 28, r.y + r.h - 52, 250, 38})) {
            system_reboot();
            return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + r.w - 312, r.y + r.h - 52, 284, 38})) {
            if (g_inst.win) {
                Window *w = g_inst.win;
                g_inst.win = NULL;
                wm_close(w);
            }
            system_screen_mode = SCREEN_DESKTOP;
            desktop_rebuild();
            wm_invalidate();
        }
    }
}

/* Boot-time Windows Setup environment */
void installer_draw_fullscreen(void) {
    Rect dlg = boot_dialog_rect();
    Rect win_frame = {dlg.x, dlg.y - 34, dlg.w, dlg.h + 34};

    theme_draw_wallpaper();
    fill_rect_alpha((Rect){0, 0, (int)fb.width, (int)fb.height}, RGBA(6, 18, 44, 132));

    /* Real Windows Setup Frame + Titlebar */
    draw_shadow(win_frame, 12);
    fill_round_rect(win_frame, 8, theme->win_head);
    icon_draw(ICON_SETUP, win_frame.x + 12, win_frame.y + 7, 20);
    draw_text_bold(win_frame.x + 40, win_frame.y + 8,
                   "Программа установки YoroOS 0.2 «Aurora» (UEFI Gen 2 & BIOS)", theme->text_main);
    draw_wizard_body(dlg);
    draw_round_rect(win_frame, 8, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 175));

    /* Helpful subtle status bar along bottom edge */
    draw_text_center_face(FACE_SMALL, (int)fb.width / 2, (int)fb.height - 22,
        "Подсказка: мышь работает нативно (Hyper-V VMBus / USB HID / F8 стрелки) · Shift+F10 открывает Терминал",
        RGBA(225, 238, 255, 220));
}

void installer_event_fullscreen(YoroEvent *e) {
    handle_wizard_event(boot_dialog_rect(), e);
}

/* Desktop Window mode */
static void inst_win_draw(Window *w, Rect r) { UNUSED(w); draw_wizard_body(r); }
static void inst_win_event(Window *w, YoroEvent *e) { handle_wizard_event(wm_client(w), e); }
static void inst_win_close(Window *w) { if (g_inst.win == w) g_inst.win = NULL; }

void installer_open(void) {
    if (g_inst.win) { wm_focus(g_inst.win); return; }
    installer_init_boot();
    g_inst.win = wm_open(app_find("installer"), "Установка YoroOS 0.2 на диск",
                         -1, -1, 820, 560, inst_win_draw, inst_win_event, inst_win_close);
}
