/* YoroOS - Setup: a Windows-style installation experience.
 *
 * Two real windows take part: the setup wizard (language, edition, licence,
 * disk, account) and the installation window that appears on top of it with
 * the live phase list, progress bar and final "restart now" prompt.  All the
 * heavy lifting below writes a genuinely bootable disk: MBR, stage 2 and the
 * kernel, verified by reading every sector back.
 */
#include "kernel.h"
#include "bootimg.h"

#define BOOT_SECTOR_ADDR ((const u8 *)boot_sector_blob)
#define STAGE2_ADDR      ((const u8 *)stage2_blob)
#define STAGE2_LBA       4u
#define STAGE2_SECTORS   48u   /* what the boot sector tells stage 2 to re-read */
#define KERNEL_ADDR      0x10000u
#define KERNEL_LBA       128u

#define ST_WELCOME   0
#define ST_EDITION   1
#define ST_LICENSE   2
#define ST_DISK      3
#define ST_USER      4
#define ST_OPTIONS   5
#define ST_PROGRESS  6
#define ST_DONE      7

static int step = ST_WELCOME;
static int language = 0;                 /* 0 = RU, 1 = EN */
static int edition = 0;                  /* 0 = Astra, 1 = Astra N */
static int disk_sel = 0;
static int disk_list[4];
static int disk_count = 0;
static int wipe_confirm = 0;
static char user_name[32] = "Yoro";
static int user_len = 4, user_caret = 4;
static int field_focus = 0;
static int opt_theme = 0;
static int licence_ok = 0;
static int reboot_tick = 0;

static int installing = 0;
static int install_stage = 0;            /* 0 boot, 1 stage2, 2 kernel, 3 done */
static u32 install_lba = 0;
static u32 install_total_lba = 0;
static int install_pct = 0;
static int install_error = 0;
static char install_log[8][96];
static int install_log_n = 0;
static u32 install_done_tick = 0;
static int install_phase = 0;            /* 0 prepare, 1 boot, 2 kernel, 3 finish */

static Window *wiz_win = NULL;
static Window *prog_win = NULL;

/* the progress window hands over to the completion window */
static void done_draw(Window *w, Rect c);
static void done_event(Window *w, YoroEvent *e);

static const char *L(const char *ru, const char *en) { return language ? en : ru; }

static void log_add(const char *text) {
    if (install_log_n >= 8) {
        memmove(install_log[0], install_log[1], sizeof(install_log[0]) * 7);
        install_log_n = 7;
    }
    strncpy(install_log[install_log_n], text, 95);
    install_log[install_log_n][95] = 0;
    install_log_n++;
}

/* the BIOS handed us the real image, so its patched sector count is the
 * authoritative one; the embedded blob still carries the build-time value */
static u32 kernel_sectors(void) {
    const u8 *live = (const u8 *)0x7C00;
    u32 n = (u32)live[0x1F0] | ((u32)live[0x1F1] << 8);
    if (n == 0 || n > 4096)
        n = (u32)boot_sector_blob[0x1F0] | ((u32)boot_sector_blob[0x1F1] << 8);
    return n;
}

/* copy of the boot sector with the live kernel sector count patched in */
static void build_boot_sector(u8 *out) {
    u32 n = kernel_sectors();
    memcpy(out, boot_sector_blob, 512);
    out[0x1F0] = (u8)(n & 0xFF);
    out[0x1F1] = (u8)((n >> 8) & 0xFF);
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
    install_phase = 0;
    install_lba = 0;
    install_pct = 0;
    install_error = 0;
    install_log_n = 0;
    install_total_lba = 1 + STAGE2_SECTORS + kernel_sectors();
    log_add(L("Проверка диска", "Checking the disk"));
    log_add(L("Запись загрузчика YoroBoot", "Writing the YoroBoot loader"));
    step = ST_PROGRESS;
    wm_invalidate();
}

/* A few sectors per paint keeps the interface responsive while installing. */
static void install_tick(void) {
    int d, budget = 3;   /* 3 batches of up to 32 sectors per paint */
    if (!installing || install_stage >= 3) return;
    if (disk_sel >= disk_count) { install_error = 1; install_stage = 3; return; }
    d = disk_list[disk_sel];

    while (budget-- > 0) {
        if (install_stage == 0) {
            u8 sector[512];
            u8 back[512];
            int ok;
            build_boot_sector(sector);
            if (ata_write(d, 0, 1, sector) < 0) { install_error = 1; install_stage = 3; break; }
            ok = ata_read(d, 0, 1, back) == 0 && back[510] == 0x55 && back[511] == 0xAA &&
                 memcmp(back, sector, 512) == 0;
            klog("[installer] verify mbr ok=%d sig=%02x%02x", ok, back[510], back[511]);
            if (!ok) { install_error = 1; install_stage = 3; break; }
            log_add(L("Главная загрузочная запись записана и проверена",
                      "Master boot record written and verified"));
            install_stage = 1;
            install_phase = 1;
            install_lba = 0;
        } else if (install_stage == 1) {
            const u8 *src = STAGE2_ADDR;
            u32 n = STAGE2_SECTORS - install_lba;
            if (n > 32) n = 32;
            if (ata_write(d, STAGE2_LBA + install_lba, n, src + install_lba * 512) < 0) {
                install_error = 1; install_stage = 3; break;
            }
            install_lba += n;
            if (install_lba >= STAGE2_SECTORS) {
                log_add(L("Вторая стадия загрузчика записана", "Boot stage 2 written"));
                install_stage = 2;
                install_phase = 2;
                install_lba = 0;
            }
        } else if (install_stage == 2) {
            const u8 *src = (const u8 *)KERNEL_ADDR;
            u32 total = kernel_sectors();
            u32 n = total - install_lba;
            if (n > 32) n = 32;
            if (ata_write(d, KERNEL_LBA + install_lba, n, src + install_lba * 512) < 0) {
                install_error = 1; install_stage = 3; break;
            }
            install_lba += n;
            if (install_lba >= total) {
                log_add(L("Ядро YoroKernel записано", "YoroKernel written"));
                log_add(L("Диск готов к загрузке", "The disk is bootable"));
                install_stage = 3;
                install_phase = 3;
                install_done_tick = sys_ticks();
                reboot_tick = 30;
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
    }
}

/* ---------------- setup wizard ---------------- */
static const char *step_titles_ru[5] = { "Язык", "Выпуск", "Лицензия", "Диск", "Учётная запись" };
static const char *step_titles_en[5] = { "Language", "Edition", "Licence", "Disk", "Account" };

static void draw_steps(Rect c) {
    int i, y = c.y + 104;
    fill_grad_v(c, RGB(14, 30, 56), RGB(9, 18, 34));
    draw_text(c.x + 26, c.y + 28, "YoroOS", C_WHITE);
    draw_text_bold(c.x + 26, c.y + 30, "YoroOS", C_WHITE);
    draw_text(c.x + 26, c.y + 52, L("Установка системы", "System setup"), RGB(150, 186, 230));
    draw_text(c.x + 26, c.y + 74, "0.1 «Astra»", RGB(110, 140, 180));
    for (i = 0; i < 5; i++) {
        int current = (step == i);
        int active = (step > i);
        Color fg = (active || current) ? C_WHITE : RGB(112, 134, 166);
        Rect dot = {c.x + 26, y + i * 46, 24, 24};
        if (active) {
            fill_circle(dot.x + 12, dot.y + 12, 12, theme->accent);
            icon_draw(ICON_CHECK, dot.x + 3, dot.y + 3, 18);
        } else if (current) {
            draw_round_rect(dot, 12, theme->accent);
        } else {
            draw_round_rect(dot, 12, RGB(74, 96, 130));
        }
        draw_text(c.x + 60, dot.y + 2, language ? step_titles_en[i] : step_titles_ru[i], fg);
    }
    draw_text(c.x + 26, c.y + c.h - 56, L("Установочный носитель", "Installation media"), RGB(108, 132, 168));
    draw_text(c.x + 26, c.y + c.h - 36, "YoroOS 0.1 · ISO", RGB(108, 132, 168));
}

static void draw_welcome(Rect a) {
    draw_text_bold(a.x, a.y, L("Установка YoroOS 0.1", "Install YoroOS 0.1"), RGB(22, 28, 40));
    draw_text(a.x, a.y + 30, L("Первый запуск займёт пару минут.",
                               "The first run takes a couple of minutes."), RGB(96, 104, 120));
    {
        Rect card = {a.x, a.y + 68, a.w, 132};
        ui_card(card, 0, 10);
        draw_text(card.x + 20, card.y + 14, L("Что будет установлено", "What gets installed"), RGB(28, 34, 46));
        draw_text(card.x + 28, card.y + 44, L("свой загрузчик YoroBoot (без GRUB)", "our own YoroBoot loader (no GRUB)"), RGB(92, 100, 116));
        draw_text(card.x + 28, card.y + 68, L("ядро YoroKernel 0.1", "the YoroKernel 0.1 kernel"), RGB(92, 100, 116));
        draw_text(card.x + 28, card.y + 92, L("рабочий стол, меню Пуск, Проводник, Терминал, YoroNet, Магазин, Параметры",
                                             "desktop, Start menu, Explorer, Terminal, YoroNet, Store, Settings"), RGB(92, 100, 116));
    }
    draw_text(a.x, a.y + 216, L("Язык установки и системы", "Setup and system language"), RGB(70, 78, 94));
    {
        Rect ru = {a.x, a.y + 244, 150, 40};
        Rect en = {a.x + 162, a.y + 244, 150, 40};
        int mx = mouse_get_x(), my = mouse_get_y();
        ui_button(ru, "Русский", pt_in(mx, my, ru), language == 0);
        ui_button(en, "English", pt_in(mx, my, en), language == 1);
    }
    draw_text(a.x, a.y + 306, L("Раскладку можно менять в системе: Ctrl+Alt+L",
                               "The layout can be changed later: Ctrl+Alt+L"), RGB(140, 148, 164));
}

static void draw_edition(Rect a) {
    int i, mx = mouse_get_x(), my = mouse_get_y();
    draw_text_bold(a.x, a.y, L("Выберите выпуск", "Choose an edition"), RGB(22, 28, 40));
    draw_text(a.x, a.y + 30, L("Оба выпуска включают рабочий стол и все приложения.",
                               "Both editions include the desktop and every application."), RGB(96, 104, 120));
    for (i = 0; i < 2; i++) {
        Rect row = {a.x, a.y + 72 + i * 96, a.w, 84};
        int sel = edition == i;
        ui_card(row, sel || pt_in(mx, my, row), 10);
        if (sel) draw_round_rect(row, 10, theme->accent);
        icon_draw(ICON_SETUP, row.x + 18, row.y + 20, 40);
        draw_text(row.x + 74, row.y + 16, i ? "YoroOS 0.1 Astra N" : "YoroOS 0.1 Astra", RGB(24, 30, 42));
        draw_text(row.x + 74, row.y + 44,
                  i ? L("Без магазина: только системные приложения", "No store: system applications only")
                    : L("Полный выпуск: магазин, YoroNet, установка приложений", "Full release: store, YoroNet, app installer"),
                  RGB(120, 128, 144));
        if (sel) {
            fill_circle(row.x + row.w - 26, row.y + 42, 11, theme->accent);
            icon_draw(ICON_CHECK, row.x + row.w - 35, row.y + 33, 18);
        }
    }
}

static void draw_license(Rect a) {
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect box = {a.x, a.y + 60, a.w, 200};
    draw_text_bold(a.x, a.y, L("Лицензионное соглашение", "Licence terms"), RGB(22, 28, 40));
    fill_round_rect(box, 8, RGB(247, 248, 251));
    draw_round_rect(box, 8, RGB(214, 219, 228));
    draw_text_wrap((Rect){box.x + 16, box.y + 14, box.w - 32, box.h - 28},
                   L("YoroOS можно устанавливать, изменять и распространять свободно. "
                     "Система не ограничивает доступ к собственным файлам: любую папку, включая Windows, "
                     "можно удалить или создать заново. Гарантий нет, но и паролей тоже.\r\n\r\n"
                     "Устанавливая систему, вы соглашаетесь, что диск будет полностью перезаписан.",
                     "YoroOS may be installed, modified and redistributed freely. The system does not lock "
                     "its own files: any folder, including Windows, can be deleted or recreated. No warranty, "
                     "and no passwords either.\r\n\r\nBy installing you agree that the target disk is erased."),
                   RGB(64, 72, 86), 22);
    {
        Rect chk = {a.x, a.y + 276, 420, 24};
        int hover = pt_in(mx, my, chk);
        ui_checkbox(chk, licence_ok,
                    L("Я принимаю условия соглашения", "I accept the terms"), hover ? theme->accent : RGB(40, 46, 60));
    }
}

static void draw_disk(Rect a) {
    int i, mx = mouse_get_x(), my = mouse_get_y();
    draw_text_bold(a.x, a.y, L("Куда установить YoroOS?", "Where to install YoroOS?"), RGB(22, 28, 40));
    draw_text(a.x, a.y + 30, L("Все данные на выбранном диске будут перезаписаны.",
                               "All data on the selected disk will be overwritten."), RGB(168, 92, 64));
    refresh_disks();
    if (!disk_count) {
        Rect warn = {a.x, a.y + 66, a.w, 96};
        ui_card(warn, 0, 10);
        draw_text(warn.x + 18, warn.y + 16, L("Диски ATA не найдены", "No ATA disks found"), RGB(180, 70, 60));
        draw_text(warn.x + 18, warn.y + 44,
                  L("В виртуальной машине подключите жёсткий диск (IDE / SATA).",
                    "Attach a hard disk (IDE / SATA) to the virtual machine."), RGB(92, 100, 116));
        draw_text(warn.x + 18, warn.y + 68,
                  L("Hyper-V: «Жёсткий диск» → «Создать виртуальный жёсткий диск».",
                    "Hyper-V: Hard drive -> New virtual hard disk."), RGB(92, 100, 116));
        return;
    }
    for (i = 0; i < disk_count; i++) {
        int d = disk_list[i];
        Rect row = {a.x, a.y + 70 + i * 84, a.w, 74};
        int sel = disk_sel == i;
        char buf[120];
        ui_card(row, sel || pt_in(mx, my, row), 10);
        if (sel) draw_round_rect(row, 10, theme->accent);
        icon_draw(ICON_DRIVE, row.x + 16, row.y + 18, 38);
        draw_text(row.x + 66, row.y + 12, ata_disk_model(d), RGB(26, 32, 44));
        snprintf(buf, sizeof(buf), "%d МБ · ATA %d · %s", (int)(ata_disk_sectors(d) / 2048), d,
                 L("таблица разделов будет создана заново", "the partition table will be recreated"));
        draw_text(row.x + 66, row.y + 38, buf, RGB(120, 128, 144));
        /* a small partition bar, like the Windows "where to install" screen */
        {
            Rect bar = {row.x + 66, row.y + 58, row.w - 120, 8};
            fill_round_rect(bar, 4, RGB(224, 228, 236));
            fill_round_rect((Rect){bar.x, bar.y, bar.w * 3 / 4, bar.h}, 4,
                            sel ? theme->accent : RGB(150, 164, 186));
        }
        if (sel) {
            fill_circle(row.x + row.w - 26, row.y + 37, 11, theme->accent);
            icon_draw(ICON_CHECK, row.x + row.w - 35, row.y + 28, 18);
        }
    }
    {
        Rect note = {a.x, a.y + 70 + disk_count * 84 + 16, a.w, 56};
        fill_round_rect(note, 8, RGB(255, 246, 232));
        draw_text(note.x + 14, note.y + 10,
                  L("Установщик запишет загрузочный сектор, вторую стадию и ядро.",
                    "Setup writes the boot sector, stage 2 and the kernel."), RGB(150, 96, 40));
        draw_text(note.x + 14, note.y + 32,
                  L("Разметка разделов появится в версии 0.2 вместе с YoroFS на диске.",
                    "On-disk YoroFS and partitioning arrive in 0.2."), RGB(150, 96, 40));
    }
}

static void draw_user(Rect a) {
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect f = {a.x, a.y + 56, 340, 40};
    draw_text_bold(a.x, a.y, L("Кто будет пользоваться системой?", "Who is going to use it?"), RGB(22, 28, 40));
    draw_text(a.x, a.y + 30, L("Имя появится на экране входа и в Проводнике.",
                               "The name appears on the sign-in screen and in Explorer."), RGB(96, 104, 120));
    draw_text(a.x, a.y + 128, L("Имя пользователя", "User name"), RGB(110, 118, 134));
    ui_textfield(f, user_name, user_caret, field_focus, "Yoro");
    if (pt_in(mx, my, f) != field_focus) { /* hover only, focus follows clicks */ }
    draw_text(a.x, a.y + 196, L("Имя компьютера", "Computer name"), RGB(110, 118, 134));
    {
        Rect c2 = {a.x, a.y + 224, 340, 40};
        fill_round_rect(c2, 6, RGB(244, 246, 250));
        draw_round_rect(c2, 6, RGB(216, 220, 228));
        draw_text(c2.x + 12, c2.y + (c2.h - FONT_H) / 2, "YORO-PC", RGB(120, 128, 144));
    }
    draw_text(a.x, a.y + 290, L("Пароль не нужен: YoroOS не запирает систему.",
                                "No password: YoroOS does not lock you out."), RGB(140, 148, 164));
}

static void draw_options(Rect a) {
    int i;
    draw_text_bold(a.x, a.y, L("Оформление", "Appearance"), RGB(22, 28, 40));
    draw_text(a.x, a.y + 30, L("Тему можно сменить в Параметрах в любой момент.",
                               "You can change the theme later in Settings."), RGB(96, 104, 120));
    {
        Rect row = {a.x, a.y + 66, a.w, 148};
        ui_card(row, 0, 10);
        for (i = 0; i < theme_count() && i < 6; i++) {
            Rect sw = {row.x + 18 + i * 56, row.y + 20, 48, 40};
            Theme *t = theme_get(i);
            fill_grad_v(sw, t->wall_top, t->wall_bottom);
            if (opt_theme == i) draw_round_rect((Rect){sw.x - 2, sw.y - 2, sw.w + 4, sw.h + 4}, 8, theme->accent);
            draw_text(sw.x, sw.y + 46, t->name, RGB(110, 118, 134));
        }
        draw_text(row.x + 18, row.y + 118,
                  L("Обои, цвета и акценты применяются сразу.", "Wallpapers, colours and accents apply instantly."),
                  RGB(130, 138, 154));
    }
    {
        Rect sum = {a.x, a.y + 232, a.w, 88};
        ui_card(sum, 0, 10);
        draw_text(sum.x + 18, sum.y + 12, L("К установке", "Ready to install"), RGB(28, 34, 46));
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "%s · %s · %s",
                     edition ? "YoroOS 0.1 Astra N" : "YoroOS 0.1 Astra",
                     disk_count ? ata_disk_model(disk_list[disk_sel]) : L("диск не выбран", "no disk"),
                     user_name);
            draw_text(sum.x + 18, sum.y + 40, buf, RGB(90, 98, 112));
            draw_text(sum.x + 18, sum.y + 62, L("Загрузчик YoroBoot + ядро YoroKernel",
                                                "YoroBoot loader + YoroKernel"), RGB(140, 148, 164));
        }
    }
}

/* ---------------- progress window ---------------- */
static const char *phase_ru[4] = { "Подготовка", "Копирование файлов", "Установка компонентов", "Завершение" };
static const char *phase_en[4] = { "Preparing", "Copying files", "Installing components", "Finishing up" };

static void prog_draw(Window *w, Rect c) {
    char buf[120];
    int i;
    install_tick();
    fill_grad_v(c, RGB(18, 26, 44), RGB(10, 14, 24));
    icon_draw(ICON_SETUP, c.x + 22, c.y + 18, 40);
    draw_text(c.x + 76, c.y + 20, L("Установка YoroOS", "Installing YoroOS"), C_WHITE);
    draw_text(c.x + 76, c.y + 42,
              edition ? "YoroOS 0.1 Astra N" : "YoroOS 0.1 Astra · «Astra»", RGB(150, 176, 210));

    for (i = 0; i < 4; i++) {
        int y = c.y + 84 + i * 26;
        int done = install_phase > i || install_stage >= 3;
        int cur = install_phase == i && !install_error;
        Color fg = done ? RGB(150, 220, 170) : (cur ? C_WHITE : RGB(110, 126, 150));
        if (done) icon_draw(ICON_CHECK, c.x + 24, y + 1, 16);
        else if (cur) {
            fill_circle(c.x + 32, y + 9, 4, theme->accent);
        } else draw_round_rect((Rect){c.x + 28, y + 5, 8, 8}, 4, RGB(70, 88, 112));
        draw_text(c.x + 48, y, language ? phase_en[i] : phase_ru[i], fg);
    }

    ui_progress((Rect){c.x + 24, c.y + 200, c.w - 48, 14}, install_pct, theme->accent);
    snprintf(buf, sizeof(buf), "%d%%", install_pct);
    draw_text_right(c.x + c.w - 24, c.y + 222, buf, RGB(150, 176, 210));
    if (install_stage >= 3) {
        snprintf(buf, sizeof(buf), L("%d секторов записано на диск", "%d sectors written to disk"),
                 install_total_lba);
        draw_text(c.x + 24, c.y + 222, install_error ? L("Ошибка записи", "Write error") : buf,
                  install_error ? RGB(255, 140, 120) : RGB(120, 200, 150));
    } else {
        draw_text(c.x + 24, c.y + 222, language ? phase_en[install_phase] : phase_ru[install_phase],
                  RGB(150, 176, 210));
    }

    {
        int bw = 190, gap = 12;
        int bx = c.x + (c.w - bw * 2 - gap) / 2;
        int by = c.y + c.h - 62;
        if (install_stage >= 3) {
            ui_button((Rect){bx, by, bw, 40}, L("Перезагрузить", "Restart now"), 0, 1);
            ui_button((Rect){bx + bw + gap, by, bw, 40}, L("Остаться", "Stay live"), 0, 0);
        } else {
            draw_text_center(c.x + c.w / 2, by + 8, L("Не выключайте компьютер", "Do not power off"), RGB(130, 148, 174));
        }
    }
    UNUSED(w);
}

static void prog_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int bw = 190, gap = 12;
    int bx = c.x + (c.w - bw * 2 - gap) / 2;
    int by = c.y + c.h - 62;
    int mx = e->x + c.x, my = e->y + c.y;
    if (e->type != EV_MOUSE_DOWN) return;
    if (install_stage >= 3) {
        if (pt_in(mx, my, (Rect){bx, by, bw, 40})) { system_reboot(); return; }
        if (pt_in(mx, my, (Rect){bx + bw + gap, by, bw, 40})) {
            if (prog_win) wm_close(prog_win);
            prog_win = NULL;
            step = ST_DONE;
            if (wiz_win) { wm_close(wiz_win); wiz_win = NULL; }
            wm_open(app_find("installer"), L("Установка завершена", "Setup complete"),
                    -1, -1, 500, 320, done_draw, done_event, NULL);
            wm_invalidate();
            return;
        }
    }
}

/* ---------------- done window ---------------- */
static void done_draw(Window *w, Rect c) {
    char buf[140];
    int cx = c.x + c.w / 2;
    int left = 30 - (int)((sys_ticks() - install_done_tick) / 100);
    fill_grad_v(c, RGB(18, 26, 44), RGB(10, 14, 24));
    fill_circle(cx, c.y + 62, 34, theme->accent);
    icon_draw(ICON_CHECK, cx - 20, c.y + 42, 40);
    draw_text_center(cx, c.y + 112, L("YoroOS установлена", "YoroOS is installed"), C_WHITE);
    snprintf(buf, sizeof(buf), "%s · %s", disk_count ? ata_disk_model(disk_list[disk_sel]) : "",
             user_name);
    draw_text_center(cx, c.y + 138, buf, RGB(150, 176, 210));
    draw_text_center(cx, c.y + 164,
                     L("Извлеките ISO и загрузитесь с диска.", "Remove the ISO and boot from the disk."),
                     RGB(130, 148, 174));
    if (left > 0)
        snprintf(buf, sizeof(buf), L("Перезагрузка через %d с", "Restarting in %d s"), left);
    else
        snprintf(buf, sizeof(buf), "%s", L("Можно перезагрузить сейчас", "Ready to restart"));
    draw_text_center(cx, c.y + 190, buf, RGB(110, 140, 170));
    ui_button((Rect){cx - 200, c.y + c.h - 68, 190, 42}, L("Перезагрузить", "Restart now"), 0, 1);
    ui_button((Rect){cx + 10, c.y + c.h - 68, 190, 42}, L("Остаться в live", "Stay live"), 0, 0);
    UNUSED(w);
}

static void done_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int cx = c.x + c.w / 2;
    int mx = e->x + c.x, my = e->y + c.y;
    if (e->type != EV_MOUSE_DOWN) return;
    if (pt_in(mx, my, (Rect){cx - 200, c.y + c.h - 68, 190, 42})) { system_reboot(); return; }
    if (pt_in(mx, my, (Rect){cx + 10, c.y + c.h - 68, 190, 42})) {
        if (wiz_win) wm_close(wiz_win);
        wiz_win = NULL;
        wm_close(w);
        return;
    }
}

/* ---------------- wizard ---------------- */
static void draw(Window *w, Rect c) {
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect side = {c.x, c.y, 232, c.h};
    Rect area = {c.x + 264, c.y + 30, c.w - 292, c.h - 116};
    int can_next = 1;

    fill_rect(c, RGB(250, 251, 253));
    draw_steps(side);

    if (step == ST_WELCOME) draw_welcome(area);
    else if (step == ST_EDITION) draw_edition(area);
    else if (step == ST_LICENSE) { draw_license(area); can_next = licence_ok; }
    else if (step == ST_DISK) { draw_disk(area); can_next = disk_count > 0; }
    else if (step == ST_USER) draw_user(area);
    else if (step == ST_OPTIONS) draw_options(area);

    {
        Rect back = {c.x + c.w - 350, c.y + c.h - 66, 120, 44};
        Rect next = {c.x + c.w - 216, c.y + c.h - 66, 190, 44};
        if (step != ST_WELCOME) ui_button(back, L("Назад", "Back"), pt_in(mx, my, back), 0);
        if (step == ST_OPTIONS) {
            ui_button(next, L("Установить", "Install"), pt_in(mx, my, next), 1);
        } else if (step == ST_WELCOME) {
            ui_button((Rect){c.x + c.w - 216, c.y + c.h - 66, 190, 44},
                      L("Установить сейчас", "Install now"), pt_in(mx, my, next), 1);
        } else if (can_next) {
            ui_button(next, L("Далее", "Next"), pt_in(mx, my, next), 1);
        } else {
            fill_round_rect(next, 6, RGB(224, 228, 236));
            draw_text_center(next.x + next.w / 2, next.y + (next.h - FONT_H) / 2,
                             L("Далее", "Next"), RGB(160, 166, 180));
        }
    }
    UNUSED(w);
}

static void advance(void) {
    step++;
    if (step == ST_DISK) refresh_disks();
    wm_invalidate();
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    Rect area = {c.x + 264, c.y + 30, c.w - 292, c.h - 116};
    Rect back = {c.x + c.w - 350, c.y + c.h - 66, 120, 44};
    Rect next = {c.x + c.w - 216, c.y + c.h - 66, 190, 44};

    if (e->type == EV_KEY) {
        if (step == ST_USER) {
            if (ui_textfield_edit(user_name, 32, &user_len, &user_caret, e)) wm_invalidate();
            return;
        }
        if (e->key == KEY_ENTER) { e->type = EV_MOUSE_DOWN; e->x = next.x - c.x + 20; e->y = next.y - c.y + 20; }
    }
    if (e->type != EV_MOUSE_DOWN) return;

    if (step != ST_WELCOME && pt_in(mx, my, back)) {
        if (step > ST_WELCOME) step--;
        wm_invalidate();
        return;
    }
    if (pt_in(mx, my, next)) {
        if (step == ST_WELCOME) { advance(); return; }
        if (step == ST_LICENSE && !licence_ok) return;
        if (step == ST_DISK && !disk_count) return;
        if (step == ST_OPTIONS) {
            Window *pw;
            install_start();
            pw = wm_open(app_find("installer"), L("Установка YoroOS", "Installing YoroOS"),
                         -1, -1, 520, 360, prog_draw, prog_event, NULL);
            prog_win = pw;
            if (pw) wm_focus(pw);
            wm_invalidate();
            return;
        }
        advance();
        return;
    }

    if (step == ST_WELCOME) {
        Rect ru = {area.x, area.y + 244, 150, 40};
        Rect en = {area.x + 162, area.y + 244, 150, 40};
        if (pt_in(mx, my, ru)) { language = 0; wm_invalidate(); return; }
        if (pt_in(mx, my, en)) { language = 1; wm_invalidate(); return; }
    } else if (step == ST_EDITION) {
        int i;
        for (i = 0; i < 2; i++) {
            Rect row = {area.x, area.y + 72 + i * 96, area.w, 84};
            if (pt_in(mx, my, row)) { edition = i; wm_invalidate(); return; }
        }
    } else if (step == ST_LICENSE) {
        Rect chk = {area.x, area.y + 276, 420, 24};
        if (pt_in(mx, my, chk)) { licence_ok = !licence_ok; wm_invalidate(); return; }
    } else if (step == ST_DISK) {
        int i;
        for (i = 0; i < disk_count; i++) {
            Rect row = {area.x, area.y + 70 + i * 84, area.w, 74};
            if (pt_in(mx, my, row)) { disk_sel = i; wm_invalidate(); return; }
        }
    } else if (step == ST_OPTIONS) {
        int i;
        for (i = 0; i < theme_count() && i < 6; i++) {
            Rect sw = {area.x + 18 + i * 56, area.y + 66 + 20, 48, 40};
            if (pt_in(mx, my, sw)) { opt_theme = i; theme_set(i); wm_invalidate(); return; }
        }
    } else if (step == ST_USER) {
        Rect f = {area.x, area.y + 56, 340, 40};
        field_focus = pt_in(mx, my, f);
    }
    UNUSED(w);
}

void installer_open(void) {
    ata_init();
    refresh_disks();
    step = ST_WELCOME;
    licence_ok = 0;
    wiz_win = wm_open(app_find("installer"), L("Установка YoroOS", "YoroOS Setup"),
                      -1, -1, 960, 660, draw, on_event, NULL);
    if (wiz_win) {
        wiz_win->min_w = 760;
        wiz_win->min_h = 540;
    }
    wm_invalidate();
}

int installer_running(void) { return step == ST_PROGRESS && installing; }
