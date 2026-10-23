/* YoroOS - setup wizard: installs the bootloader and kernel onto a real disk */
#include "kernel.h"

#define BOOT_SECTOR_ADDR 0x7C00u
#define STAGE2_ADDR      0x8000u
#define STAGE2_SECTORS   48u
#define KERNEL_ADDR      0x10000u
#define KERNEL_LBA       128u

#define ST_WELCOME   0
#define ST_LANGUAGE  1
#define ST_DISK      2
#define ST_USER      3
#define ST_OPTIONS   4
#define ST_PROGRESS  5
#define ST_DONE      6

static int step = ST_WELCOME;
static int language = 0;              /* 0 = RU, 1 = EN */
static int disk_sel = 0;
static int disk_list[4];
static int disk_count = 0;
static char user_name[32] = "Yoro";
static int user_len = 4, user_caret = 4;
static int field_focus = 0;
static int opt_fast = 1;
static int opt_theme = 0;

static int installing = 0;
static int install_stage = 0;         /* 0 boot, 1 stage2, 2 kernel, 3 done */
static u32 install_lba = 0;
static u32 install_total_lba = 0;
static int install_pct = 0;
static int install_error = 0;
static char install_log[6][96];
static int install_log_n = 0;
static u32 install_done_tick = 0;

static const char *L(const char *ru, const char *en) { return language ? en : ru; }

static void log_add(const char *text) {
    if (install_log_n >= 6) {
        memmove(install_log[0], install_log[1], sizeof(install_log[0]) * 5);
        install_log_n = 5;
    }
    strncpy(install_log[install_log_n], text, 95);
    install_log[install_log_n][95] = 0;
    install_log_n++;
}

static u32 kernel_sectors(void) {
    const u8 *bs = (const u8 *)BOOT_SECTOR_ADDR;
    return (u32)bs[0x1F0] | ((u32)bs[0x1F1] << 8);
}

static void refresh_disks(void) {
    int i;
    disk_count = 0;
    for (i = 0; i < 4 && disk_count < 4; i++) {
        int d = ata_disk_index(i);
        if (d >= 0) disk_list[disk_count++] = d;
    }
    if (disk_sel >= disk_count) disk_sel = 0;
}

/* ---------------- real installation ---------------- */
static void install_start(void) {
    installing = 1;
    install_stage = 0;
    install_lba = 0;
    install_pct = 0;
    install_error = 0;
    install_log_n = 0;
    install_total_lba = 1 + STAGE2_SECTORS + kernel_sectors();
    log_add(L("Подготовка диска...", "Preparing the disk..."));
    step = ST_PROGRESS;
    wm_invalidate();
}

/* writes a few sectors per call so the UI stays alive */
static void install_tick(void) {
    int d, budget = 16;
    if (!installing || install_stage >= 3) return;
    if (disk_sel >= disk_count) { install_error = 1; install_stage = 3; return; }
    d = disk_list[disk_sel];

    while (budget-- > 0) {
        if (install_stage == 0) {
            u8 sector[512];
            memcpy(sector, (const void *)BOOT_SECTOR_ADDR, 512);
            if (ata_write(d, 0, 1, sector) < 0) { install_error = 1; install_stage = 3; break; }
            {
                u8 back[512];
                int ok = ata_read(d, 0, 1, back) == 0 && back[510] == 0x55 && back[511] == 0xAA &&
                         memcmp(back, sector, 512) == 0;
                klog("[installer] verify mbr ok=%d sig=%02x%02x", ok, back[510], back[511]);
                if (!ok) { install_error = 1; install_stage = 3; break; }
            }
            log_add(L("Записан загрузчик YoroBoot (MBR)", "YoroBoot written to the MBR"));
            install_stage = 1;
            install_lba = 0;
        } else if (install_stage == 1) {
            const u8 *src = (const u8 *)STAGE2_ADDR;
            if (ata_write(d, 1 + install_lba, 1, src + install_lba * 512) < 0) {
                install_error = 1; install_stage = 3; break;
            }
            install_lba++;
            if (install_lba >= STAGE2_SECTORS) {
                log_add(L("Вторая стадия загрузчика записана", "Boot stage 2 written"));
                install_stage = 2;
                install_lba = 0;
            }
        } else if (install_stage == 2) {
            const u8 *src = (const u8 *)KERNEL_ADDR;
            u32 n = kernel_sectors();
            if (ata_write(d, KERNEL_LBA + install_lba, 1, src + install_lba * 512) < 0) {
                install_error = 1; install_stage = 3; break;
            }
            install_lba++;
            if (install_lba >= n) {
                log_add(L("Ядро YoroKernel записано", "YoroKernel written"));
                log_add(L("Диск готов к загрузке", "Disk is bootable"));
                install_stage = 3;
                install_done_tick = sys_ticks();
                break;
            }
        }
        {
            u32 done = 1;
            if (install_stage == 1) done += install_lba;
            else if (install_stage == 2) done += STAGE2_SECTORS + install_lba;
            else if (install_stage >= 3) done = install_total_lba;
            install_pct = install_total_lba ? (int)(done * 100 / install_total_lba) : 0;
            if (install_pct > 100) install_pct = 100;
        }
    }
    if (install_stage == 3) {
        install_pct = 100;
        installing = 0;
        if (!install_error) { step = ST_DONE; }
    }
    wm_invalidate();
}

/* ---------------- drawing ---------------- */
static const char *step_titles_ru[5] = { "Приветствие", "Язык", "Диск", "Пользователь", "Установка" };
static const char *step_titles_en[5] = { "Welcome", "Language", "Disk", "User", "Installation" };

static void draw_steps(Rect c) {
    int i, y = c.y + 96;
    draw_text(c.x + 24, c.y + 26, "YoroOS", C_WHITE);
    draw_text(c.x + 24, c.y + 46, L("Установка системы", "System setup"), RGB(168, 196, 232));
    for (i = 0; i < 4; i++) {
        int idx = i == 0 ? 0 : (i == 1 ? 2 : (i == 2 ? 3 : 5));
        int active = (step >= idx);
        Color fg = active ? C_WHITE : RGB(120, 140, 170);
        Rect dot = {c.x + 24, y + i * 44, 24, 24};
        if (active) fill_circle(dot.x + 12, dot.y + 12, 12, theme->accent);
        else draw_round_rect(dot, 12, RGB(90, 110, 140));
        if (step >= idx) icon_draw(ICON_CHECK, dot.x + 3, dot.y + 3, 18);
        draw_text(c.x + 58, dot.y + 3, language ? step_titles_en[i] : step_titles_ru[i], fg);
    }
    draw_text(c.x + 24, c.y + c.h - 60, "ISO · live-режим", RGB(120, 140, 170));
    draw_text(c.x + 24, c.y + c.h - 40, "YoroOS 0.1", RGB(120, 140, 170));
}

static void draw_welcome(Rect a, Window *w) {
    int mx = mouse_get_x(), my = mouse_get_y();
    draw_text(a.x, a.y, L("Добро пожаловать в YoroOS", "Welcome to YoroOS"), RGB(24, 30, 42));
    draw_text_wrap((Rect){a.x, a.y + 34, a.w, 90},
                   L("Установщик запишет загрузчик YoroBoot и ядро YoroKernel на выбранный диск. "
                     "Система загрузится с него без установленного Linux или Windows.",
                     "The installer writes the YoroBoot loader and the YoroKernel to the selected disk. "
                     "The machine will boot from it without Linux or Windows installed."),
                   RGB(90, 98, 112), 24);
    {
        Rect card = {a.x, a.y + 130, a.w, 120};
        ui_card(card, 0, 10);
        draw_text(card.x + 18, card.y + 14, L("Что будет установлено", "What gets installed"), RGB(28, 34, 46));
        draw_text(card.x + 26, card.y + 42, L("свой загрузчик (без GRUB)", "our own boot loader (no GRUB)"), RGB(90, 98, 112));
        draw_text(card.x + 26, card.y + 66, L("своё ядро YoroKernel 0.1", "YoroKernel 0.1"), RGB(90, 98, 112));
        draw_text(card.x + 26, card.y + 90, L("рабочий стол, меню Пуск и 5 приложений", "desktop, start menu and 5 applications"), RGB(90, 98, 112));
    }
    draw_text(a.x, a.y + 268, L("Выберите язык установки:", "Choose installation language:"), RGB(60, 68, 82));
    {
        Rect ru = {a.x, a.y + 296, 150, 40};
        Rect en = {a.x + 164, a.y + 296, 150, 40};
        ui_button(ru, "Русский", language == 0 || pt_in(mx, my, ru), language == 0);
        ui_button(en, "English", language == 1 || pt_in(mx, my, en), language == 1);
    }
    UNUSED(w);
}

static void draw_disk(Rect a) {
    int i;
    draw_text(a.x, a.y, L("Куда установить YoroOS?", "Where should YoroOS be installed?"), RGB(24, 30, 42));
    draw_text(a.x, a.y + 26, L("Все данные на выбранном диске будут перезаписаны.",
                               "All data on the selected disk will be overwritten."), RGB(160, 90, 60));
    refresh_disks();
    if (!disk_count) {
        Rect warn = {a.x, a.y + 60, a.w, 90};
        ui_card(warn, 0, 10);
        draw_text(warn.x + 18, warn.y + 16, L("Диски ATA не найдены", "No ATA disks found"), RGB(180, 70, 60));
        draw_text(warn.x + 18, warn.y + 44, L("В виртуальной машине подключите жёсткий диск (hda).",
                                              "Attach a hard disk (hda) to the virtual machine."), RGB(90, 98, 112));
        return;
    }
    for (i = 0; i < disk_count; i++) {
        int d = disk_list[i];
        Rect row = {a.x, a.y + 64 + i * 76, a.w, 66};
        int sel = disk_sel == i;
        ui_card(row, sel, 10);
        if (sel) draw_round_rect(row, 10, theme->accent);
        icon_draw(ICON_DRIVE, row.x + 14, row.y + 14, 36);
        draw_text(row.x + 62, row.y + 12, ata_disk_model(d), RGB(28, 34, 46));
        {
            char buf[100];
            snprintf(buf, sizeof(buf), "%d МБ · ATA %d", (int)(ata_disk_sectors(d) / 2048), d);
            draw_text(row.x + 62, row.y + 36, buf, RGB(120, 128, 144));
        }
        if (sel) fill_circle(row.x + row.w - 28, row.y + 33, 11, theme->accent), icon_draw(ICON_CHECK, row.x + row.w - 37, row.y + 24, 18);
    }
}

static void draw_user(Rect a) {
    draw_text(a.x, a.y, L("Учётная запись", "User account"), RGB(24, 30, 42));
    draw_text(a.x, a.y + 26, L("Имя пользователя для рабочего стола",
                               "User name for the desktop"), RGB(120, 128, 144));
    ui_textfield((Rect){a.x, a.y + 56, 340, 40}, user_name, user_caret, 1, "Yoro");
    draw_text(a.x, a.y + 116, L("Имя компьютера", "Computer name"), RGB(120, 128, 144));
    {
        Rect f = {a.x, a.y + 144, 340, 40};
        fill_round_rect(f, 6, RGB(242, 244, 248));
        draw_round_rect(f, 6, RGB(214, 218, 226));
        draw_text(f.x + 12, f.y + (f.h - FONT_H) / 2, "YORO-PC", RGB(120, 128, 144));
    }
    draw_text(a.x, a.y + 206, L("Пароль не требуется: в YoroOS всё можно менять.",
                                "No password: everything in YoroOS is editable."), RGB(150, 158, 172));
}

static void draw_options(Rect a) {
    int mx = mouse_get_x(), my = mouse_get_y();
    draw_text(a.x, a.y, L("Параметры установки", "Installation options"), RGB(24, 30, 42));
    {
        Rect row = {a.x, a.y + 36, a.w, 60};
        ui_card(row, 0, 10);
        ui_checkbox((Rect){row.x + 18, row.y + 20, 20, 20}, opt_fast,
                    L("Быстрая установка (только загрузчик и ядро)", "Quick install (bootloader and kernel only)"),
                    RGB(40, 46, 60));
    }
    {
        Rect row = {a.x, a.y + 106, a.w, 150};
        ui_card(row, 0, 10);
        draw_text(row.x + 18, row.y + 14, L("Оформление по умолчанию", "Default theme"), RGB(40, 46, 60));
        {
            int i;
            for (i = 0; i < theme_count() && i < 6; i++) {
                Rect sw = {row.x + 18 + i * 52, row.y + 46, 44, 40};
                Theme *t = theme_get(i);
                fill_round_rect(sw, 6, t->wall_top);
                fill_grad_v(sw, t->wall_top, t->wall_bottom);
                if (opt_theme == i) draw_round_rect((Rect){sw.x - 2, sw.y - 2, sw.w + 4, sw.h + 4}, 8, theme->accent);
            }
        }
        draw_text(row.x + 18, row.y + 104,
                  L("Тему можно сменить в Параметрах в любой момент.", "You can change the theme later in Settings."),
                  RGB(130, 138, 154));
    }
    UNUSED(mx); UNUSED(my);
}

static void draw_progress(Rect a) {
    int i;
    draw_text(a.x, a.y, L("Установка YoroOS...", "Installing YoroOS..."), RGB(24, 30, 42));
    ui_progress((Rect){a.x, a.y + 40, a.w, 22}, install_pct, theme->accent);
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "%d%%", install_pct);
        draw_text_right(a.x + a.w, a.y + 70, buf, RGB(110, 118, 134));
    }
    {
        Rect logbox = {a.x, a.y + 100, a.w, 190};
        fill_round_rect(logbox, 8, RGB(16, 22, 34));
        for (i = 0; i < install_log_n; i++)
            draw_text(logbox.x + 14, logbox.y + 14 + i * 22, install_log[i], RGB(150, 220, 180));
        if (install_error)
            draw_text(logbox.x + 14, logbox.y + 14 + i * 22,
                      L("Ошибка записи на диск", "Disk write error"), RGB(255, 120, 110));
    }
}

static void draw_done(Rect a) {
    int cx = a.x + a.w / 2;
    fill_circle(cx, a.y + 60, 46, theme->accent);
    icon_draw(ICON_CHECK, cx - 24, a.y + 36, 48);
    draw_text_center(cx, a.y + 124, L("YoroOS установлена!", "YoroOS is installed!"), RGB(24, 30, 42));
    draw_text_center(cx, a.y + 152, L("Извлеките ISO и загрузитесь с диска.",
                                      "Remove the ISO and boot from the disk."), RGB(110, 118, 134));
    {
        char buf[120];
        snprintf(buf, sizeof(buf), "%s · %d МБ",
                 disk_count ? ata_disk_model(disk_list[disk_sel]) : "",
                 disk_count ? (int)(ata_disk_sectors(disk_list[disk_sel]) / 2048) : 0);
        draw_text_center(cx, a.y + 182, buf, RGB(150, 158, 172));
    }
}

static void draw(Window *w, Rect c) {
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect side = {c.x, c.y, 240, c.h};
    Rect area = {c.x + 268, c.y + 26, c.w - 292, c.h - 100};
    int i;

    install_tick();

    fill_rect(c, RGB(250, 251, 253));
    fill_grad_v(side, RGB(16, 34, 62), RGB(10, 20, 38));
    draw_steps(side);

    if (step == ST_WELCOME || step == ST_LANGUAGE) draw_welcome(area, w);
    else if (step == ST_DISK) draw_disk(area);
    else if (step == ST_USER) draw_user(area);
    else if (step == ST_OPTIONS) draw_options(area);
    else if (step == ST_PROGRESS) draw_progress(area);
    else draw_done(area);

    /* bottom navigation */
    if (step != ST_PROGRESS && step != ST_DONE) {
        Rect back = {c.x + c.w - 340, c.y + c.h - 62, 130, 46};
        Rect next = {c.x + c.w - 200, c.y + c.h - 62, 180, 46};
        if (step != ST_WELCOME) ui_button(back, L("Назад", "Back"), pt_in(mx, my, back), 0);
        if (step == ST_OPTIONS) ui_button(next, L("Установить", "Install"), pt_in(mx, my, next), 1);
        else if (step == ST_WELCOME) ui_button((Rect){c.x + c.w - 344, c.y + c.h - 62, 320, 46},
                                               L("Начать установку", "Start setup"), pt_in(mx, my, next), 1);
        else ui_button(next, L("Далее", "Next"), pt_in(mx, my, next), 1);
    } else if (step == ST_DONE) {
        Rect r = {c.x + c.w - 340, c.y + c.h - 62, 320, 46};
        ui_button((Rect){r.x, r.y, 150, 46}, L("Перезагрузить", "Reboot"), pt_in(mx, my, r), 1);
        ui_button((Rect){r.x + 160, r.y, 160, 46}, L("Остаться в live", "Stay live"), pt_in(mx, my, (Rect){r.x + 160, r.y, 160, 46}), 0);
    }
    /* language switch buttons live in the welcome area */
    if (step == ST_WELCOME) {
        Rect ru = {area.x, area.y + 296, 150, 40};
        Rect en = {area.x + 164, area.y + 296, 150, 40};
        if (pt_in(mx, my, ru)) draw_round_rect(ru, 6, theme->accent);
        if (pt_in(mx, my, en)) draw_round_rect(en, 6, theme->accent);
        for (i = 0; i < 2; i++) {}
    }
    UNUSED(w);
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    Rect area = {c.x + 268, c.y + 26, c.w - 292, c.h - 100};

    if (step == ST_PROGRESS) return;

    if (e->type == EV_KEY) {
        if (step == ST_USER) {
            if (ui_textfield_edit(user_name, 32, &user_len, &user_caret, e)) wm_invalidate();
            return;
        }
        if (e->key == KEY_ENTER) {
            /* fall through to the next button */
            e->type = EV_MOUSE_DOWN;
            e->x = c.w - 110;
            e->y = c.h - 38;
        }
    }

    if (e->type != EV_MOUSE_DOWN) return;

    /* navigation */
    if (step != ST_DONE) {
        Rect back = {c.x + c.w - 340, c.y + c.h - 62, 130, 46};
        Rect next = {c.x + c.w - 200, c.y + c.h - 62, 180, 46};
        if (step != ST_WELCOME && pt_in(mx, my, back)) {
            step = step == ST_PROGRESS ? ST_OPTIONS : step - 1;
            wm_invalidate();
            return;
        }
        if (pt_in(mx, my, next) ||
            (step == ST_WELCOME && pt_in(mx, my, (Rect){c.x + c.w - 344, c.y + c.h - 62, 320, 46}))) {
            if (step == ST_OPTIONS) { install_start(); return; }
            step++;
            if (step == ST_DISK) refresh_disks();
            wm_invalidate();
            return;
        }
    } else {
        Rect r = {c.x + c.w - 340, c.y + c.h - 62, 320, 46};
        if (pt_in(mx, my, (Rect){r.x, r.y, 150, 46})) { system_reboot(); return; }
        if (pt_in(mx, my, (Rect){r.x + 160, r.y, 160, 46})) { wm_close(w); return; }
    }

    if (step == ST_WELCOME) {
        Rect ru = {area.x, area.y + 296, 150, 40};
        Rect en = {area.x + 164, area.y + 296, 150, 40};
        if (pt_in(mx, my, ru)) { language = 0; wm_invalidate(); return; }
        if (pt_in(mx, my, en)) { language = 1; wm_invalidate(); return; }
    }
    if (step == ST_DISK) {
        int i;
        for (i = 0; i < disk_count; i++) {
            Rect row = {area.x, area.y + 64 + i * 76, area.w, 66};
            if (pt_in(mx, my, row)) { disk_sel = i; wm_invalidate(); return; }
        }
    }
    if (step == ST_OPTIONS) {
        Rect row1 = {area.x, area.y + 36, area.w, 60};
        if (pt_in(mx, my, row1)) { opt_fast = !opt_fast; wm_invalidate(); return; }
        {
            int i;
            for (i = 0; i < theme_count() && i < 6; i++) {
                Rect sw = {area.x + 18 + i * 52, area.y + 106 + 46, 44, 40};
                if (pt_in(mx, my, sw)) { opt_theme = i; theme_set(i); wm_invalidate(); return; }
            }
        }
    }
    if (step == ST_USER) {
        Rect f = {area.x, area.y + 56, 340, 40};
        field_focus = pt_in(mx, my, f);
    }
    UNUSED(w);
}

void installer_open(void) {
    refresh_disks();
    ata_init();
    refresh_disks();
    step = ST_WELCOME;
    {
        Window *w = wm_open(app_find("installer"), L("Установка YoroOS", "YoroOS Setup"),
                            0, 0, (int)fb.width, (int)fb.height - wm_taskbar_height(),
                            draw, on_event, NULL);
        if (w) {
            w->frame.x = 0;
            w->frame.y = 0;
            w->frame.w = (int)fb.width;
            w->frame.h = (int)fb.height - wm_taskbar_height();
            w->restore = w->frame;
            w->maximized = 1;
            w->min_w = 720;
            w->min_h = 520;
        }
    }
    wm_invalidate();
}

int installer_running(void) { return step == ST_PROGRESS && installing; }
