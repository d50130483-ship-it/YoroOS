/* Установщик YoroOS — мастер первого запуска */
#include "ui.h"
#include "gfx.h"
#include "hw.h"
#include "fs.h"
#include "klib.h"

static int page = 0; /* 0 приветствие, 1 лицензия, 2 имя, 3 тема, 4 установка, 5 готово */
static char inst_user[24];
static int inst_user_len = 0;
static int inst_theme = 0;
static int inst_progress = 0;
static int inst_seeded = 0;
static u32 inst_t0 = 0;
static int active = 0;

const char *theme_name(int i);

static const char *license_text[] = {
    "ЛИЦЕНЗИОННОЕ СОГЛАШЕНИЕ YoroOS",
    "",
    "1. Устанавливаясь на виртуальный диск, YoroOS обязуется",
    "   грузиться быстро и не показывать рекламу. Вообще никакой.",
    "",
    "2. Все файлы хранятся на диске образа (YoroFS).",
    "   Не храните здесь рецепты бабушкиного пирога без бэкапа.",
    "",
    "3. В комплект входят: Finder, Терминал, Заметки, Календарь,",
    "   Музыка (PC-динамик, 1 бит чистого ностальжи), Калькулятор.",
    "",
    "4. YoroOS не несёт ответственности за потерю чувства времени.",
    "",
    "5. Все совпадения с настоящими ОС сделаны с любовью."
};
#define LIC_N (int)(sizeof(license_text) / sizeof(license_text[0]))

static void seed_files(void) {
    static const char readme[] =
        "Добро пожаловать в YoroOS!\n"
        "\n"
        "Это настоящая операционная система:\n"
        "загрузчик на ассемблере, ядро на C,\n"
        "своя файловая система YoroFS на диске.\n"
        "\n"
        "Подсказки:\n"
        "  * Меню YoroOS — слева вверху\n"
        "  * Окна: перетаскивай за шапку, край — размер\n"
        "  * Терминал: введи help или neofetch\n"
        "  * Alt переключает RU/EN раскладку\n"
        "  * Музыка играет через PC-динамик\n";
    static const char ideas[] =
        "Идеи для YoroOS:\n\n1. Мышь с ускорением\n2. Ещё темы оформления\n3. Игры на асме\n4. Сеть по ISA NE2000";
    fs_create("readme.txt", readme, sizeof(readme) - 1);
    fs_create("идеи.txt", ideas, sizeof(ideas) - 1);
    fs_create("заметка-1.txt", "Первая заметка!\n\nAlt — переключение раскладки.", 0);
    /* заметка-1 с реальным текстом */
    {
        const char *txt = "Первая заметка!\n\nРедактируй и пиши что хочешь — сохраняется на диск.\n";
        fs_write("заметка-1.txt", txt, strlen(txt));
    }
}

void installer_start(void) {
    page = 0;
    inst_user_len = 0;
    inst_user[0] = 0;
    inst_progress = 0;
    inst_seeded = 0;
    active = 1;
}

int installer_active(void) { return active; }

static void panel_title(const char *t, int step) {
    int w = gfx_w(), h = gfx_h();
    int pw = 460, ph = 330;
    int px = (w - pw) / 2, py = (h - ph) / 2;
    gfx_blend(px + 6, py + 8, pw, ph, 0x000000, 70);
    gfx_round(px, py, pw, ph, 10, theme_accent(g_theme));
    gfx_round(px + 1, py + 1, pw - 2, ph - 2, 9, theme_winbg(g_theme));
    gfx_blend(px + 1, py + 34, pw - 2, 1, 0xffffff, 30);
    gfx_text(px + 14, py + 10, t, theme_text(g_theme), 0);
    /* шаги */
    for (int i = 0; i < 6; i++) {
        u32 c = i < step ? 0x888899 : i == step ? theme_accent(g_theme) : 0x33333c;
        for (int yy = -2; yy <= 2; yy++)
            for (int xx = -2; xx <= 2; xx++)
                if (xx * xx + yy * yy <= 4) gfx_px(px + pw - 16 - i * 10, py + 17 + yy, c);
    }
}

static void big_logo(int cx, int cy, int s) {
    extern void draw_app_icon(int id, int x, int y, int s);
    draw_app_icon(AP_ABOUT, cx - s / 2, cy - s / 2, s);
}

static void draw_installer(void) {
    int w = gfx_w(), h = gfx_h();
    gfx_copy_wall();
    int tx = theme_text(g_theme);
    if (page == 0) {
        big_logo(w / 2, h / 2 - 60, 72);
        gfx_text_c(w / 2, h / 2 + 12, "Установка YoroOS 1.0 «Натива»", tx, 0);
        gfx_text_c(w / 2, h / 2 + 34, "Ядро загрузится с диска, файлы будут записаны в YoroFS.", 0x9aa0ab, 0);
        if ((pit_ticks / 40) % 2)
            gfx_text_c(w / 2, h / 2 + 70, "Нажми Enter, чтобы начать", theme_accent(g_theme), 0);
    } else if (page == 1) {
        panel_title("Лицензионное соглашение", 1);
        int px = (w - 460) / 2, py = (h - 330) / 2;
        for (int i = 0; i < LIC_N; i++)
            gfx_text(px + 18, py + 48 + i * 16, license_text[i], i == 0 ? theme_accent(g_theme) : tx, 0);
        gfx_text_c(px + 230, py + 300, "Enter — принимаю и продолжаю", theme_accent(g_theme), 0);
    } else if (page == 2) {
        panel_title("Создай учётную запись", 2);
        int px = (w - 460) / 2, py = (h - 330) / 2;
        big_logo(px + 90, py + 130, 64);
        gfx_text(px + 40, py + 190, "Имя пользователя:", tx, 0);
        gfx_round(px + 40, py + 210, 300, 26, 6, 0x000000);
        gfx_round(px + 40, py + 210, 300, 26, 6, theme_accent(g_theme));
        gfx_text(px + 50, py + 216, inst_user, tx, 0);
        if ((pit_ticks / 40) % 2)
            gfx_fill(px + 52 + inst_user_len * 8, py + 219, 7, 11, tx);
        gfx_text_c(px + 230, py + 270, "Enter — далее (Alt — RU/EN)", 0x9aa0ab, 0);
    } else if (page == 3) {
        panel_title("Выбери оформление", 3);
        int px = (w - 460) / 2, py = (h - 330) / 2;
        gfx_text(px + 24, py + 60, "Тема:", tx, 0);
        for (int i = 0; i < 5; i++) {
            int sx = px + 24 + i * 84;
            gfx_round(sx, py + 80, 72, 90, 8, i == inst_theme ? theme_accent(g_theme) : 0x000000);
            gfx_round(sx + 2, py + 82, 68, 86, 6, theme_top(i));
            gfx_text_c(sx + 36, py + 180, theme_name(i), i == inst_theme ? theme_accent(g_theme) : tx, 0);
        }
        gfx_text_c(px + 230, py + 290, "< > — выбор, Enter — установить", 0x9aa0ab, 0);
    } else if (page == 4) {
        panel_title("Установка YoroOS…", 4);
        int px = (w - 460) / 2, py = (h - 330) / 2;
        char pct[8];
        itoa(inst_progress, pct);
        int L = strlen(pct);
        pct[L] = '%';
        pct[L + 1] = 0;
        char bigp[8];
        strcpy(bigp, pct, 8);
        gfx_text_c(px + 230, py + 90, bigp, tx, 1);
        gfx_round(px + 40, py + 130, 380, 10, 5, 0x000000);
        gfx_round(px + 41, py + 131, 378 * inst_progress / 100, 8, 4, theme_accent(g_theme));
        const char *ph = inst_progress < 25 ? "Форматирование YoroFS…" :
                         inst_progress < 55 ? "Копирование system.core…" :
                         inst_progress < 80 ? "Установка приложений…" :
                         inst_progress < 100 ? "Запись пользовательских файлов…" : "Готово!";
        gfx_text_c(px + 230, py + 160, ph, 0x9aa0ab, 0);
        gfx_text_c(px + 230, py + 210, "Не выключай питание (настояще!) :)", 0x666670, 0);
    } else if (page == 5) {
        panel_title("Установка завершена", 5);
        int px = (w - 460) / 2, py = (h - 330) / 2;
        int ccx = px + 230, ccy = py + 104, r = 12;
        for (int yy = -r; yy <= r; yy++)
            for (int xx = -r; xx <= r; xx++)
                if (xx * xx + yy * yy <= r * r) gfx_px(ccx + xx, ccy + yy, 0x2e7d4f);
        for (int i = 0; i < 7; i++) {
            gfx_fill(ccx - 6 + i, ccy + i, 2, 2, 0xffffff);
            gfx_fill(ccx + 0 + i, ccy + 6 - i, 2, 2, 0xffffff);
        }
        gfx_text_c(px + 230, py + 140, "YoroOS установлена на диск!", tx, 0);
        gfx_text_c(px + 230, py + 164, "Настройки сохранены в settings.dat (YoroFS).", 0x9aa0ab, 0);
        if ((pit_ticks / 40) % 2)
            gfx_text_c(px + 230, py + 230, "Enter — войти в систему", theme_accent(g_theme), 0);
    }
    gfx_blit();
}

static void inst_seed_disk(void) {
    fs_format();
    strcpy(g_user, inst_user, sizeof(g_user));
    {
        char sern[16] = "YORO-";
        u32 r = pit_ticks ^ 0xBEEF;
        for (int i = 0; i < 6; i++) {
            r = r * 1103515245 + 12345;
            sern[5 + i] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"[(r >> 16) & 31];
        }
        sern[11] = 0;
        strcpy(g_serial, sern, sizeof(g_serial));
    }
    seed_files();
    inst_theme_save();
}

void inst_theme_save(void) {
    g_theme = inst_theme;
    gfx_wall_redraw(g_theme);
    settings_save();
}

void installer_key(int code, int ascii) {
    if (!active) return;
    if (page == 0 && code == K_ENTER) { page = 1; return; }
    if (page == 1 && code == K_ENTER) { page = 2; return; }
    if (page == 2) {
        if (code == K_BS) { if (inst_user_len > 0) { inst_user_len--; inst_user[inst_user_len] = 0; } return; }
        if (code == K_ENTER && inst_user_len > 0) { page = 3; return; }
        if (ascii >= 32 && inst_user_len < 18) {
            inst_user_len += utf8_put(inst_user + inst_user_len, (unsigned)ascii);
            inst_user[inst_user_len] = 0;
        }
        return;
    }
    if (page == 3) {
        if (code == K_LEFT && inst_theme > 0) inst_theme--;
        if (code == K_RIGHT && inst_theme < 4) inst_theme++;
        if (code == K_ENTER) {
            inst_seed_disk();
            inst_t0 = pit_ticks;
            page = 4;
        }
        return;
    }
    if (page == 5 && code == K_ENTER) {
        active = 0;
    }
}

/* возвращает 1 пока установщик активен */
int installer_frame(void) {
    int code, ascii;
    while (kbd_pop(&code, &ascii)) installer_key(code, ascii);
    if (page == 4) {
        u32 el = pit_ticks - inst_t0;
        int target = (int)(el * 100 / 350);
        if (target > 100) target = 100;
        inst_progress = target;
        if (inst_progress >= 100) {
            if (!inst_seeded) { inst_seeded = 1; }
            page = 5;
        }
    }
    draw_installer();
    return active;
}
