/* YoroOS 0.2 - YoroWeb Browser + Native Matronix Browser 3.0.0 + Chrome + Firefox */
#include "kernel.h"

extern const u8 *fs_builtin_pkg_bytes(const char *id, int *out_len, const char **out_filename);

#define MAX_TABS 4
#define MAX_HIST 16
#define MAX_DLS  8

enum {
    BRAND_YOROWEB  = 0,
    BRAND_MATRONIX = 1, /* Official #000000 Matronix Browser v3.0.0 UI! */
    BRAND_CHROME   = 2,
    BRAND_FIREFOX  = 3
};

typedef struct {
    int  brand;
    int  active_tab;
    int  ntabs;
    char tab_urls[MAX_TABS][128];
    char tab_titles[MAX_TABS][32];
    char url[128];
    int  ulen, ucaret;
    int  url_focused;
    int  search_engine; /* 0=Google, 1=DuckDuckGo, 2=Bing, 3=Яндекс */
    char search_q[96];
    int  qlen, qcaret, q_focused;
    char hist[MAX_HIST][128];
    int  nhist;
    int  last_dl_node;
    char last_dl_name[64];
    int  dl_count;
    char dls[MAX_DLS][64];
} BrowserState;

static const char *const kEngines[4] = { "Google", "DuckDuckGo", "Bing", "Яндекс" };

static void br_navigate(BrowserState *st, const char *target) {
    strncpy(st->url, target, sizeof(st->url) - 1);
    st->url[sizeof(st->url) - 1] = 0;
    st->ulen = (int)strlen(st->url);
    st->ucaret = st->ulen;
    strcpy(st->tab_urls[st->active_tab], st->url);

    if (strcasestr_ascii(st->url, "Matronix_Browser")) strcpy(st->tab_titles[st->active_tab], "GitHub · Matronix 3.0");
    else if (strcasestr_ascii(st->url, "matronix:passwords")) strcpy(st->tab_titles[st->active_tab], "Matronix Пароли");
    else if (strcasestr_ascii(st->url, "matronix:settings"))  strcpy(st->tab_titles[st->active_tab], "Matronix Настройки");
    else if (strcasestr_ascii(st->url, "matronix:history"))   strcpy(st->tab_titles[st->active_tab], "Matronix История");
    else if (strcasestr_ascii(st->url, "matronix:downloads")) strcpy(st->tab_titles[st->active_tab], "Matronix Загрузки");
    else if (strcasestr_ascii(st->url, "chrome"))    strcpy(st->tab_titles[st->active_tab], "Скачать Chrome");
    else if (strcasestr_ascii(st->url, "firefox"))   strcpy(st->tab_titles[st->active_tab], "Скачать Firefox");
    else if (strcasestr_ascii(st->url, "steam"))     strcpy(st->tab_titles[st->active_tab], "Steam Store");
    else if (strcasestr_ascii(st->url, "python"))    strcpy(st->tab_titles[st->active_tab], "Python 3.12");
    else if (strcasestr_ascii(st->url, "visualstudio") || strcasestr_ascii(st->url, "vscode"))
        strcpy(st->tab_titles[st->active_tab], "VS Code");
    else if (strcasestr_ascii(st->url, "search:"))   strcpy(st->tab_titles[st->active_tab], "Результаты поиска");
    else strcpy(st->tab_titles[st->active_tab], st->brand == BRAND_MATRONIX ? "Matronix 3.0.0" : "Новая вкладка");

    if (st->nhist < MAX_HIST) strcpy(st->hist[st->nhist++], st->url);
    wm_invalidate();
}

static void br_trigger_download(BrowserState *st, const char *pkg_id) {
    int len = 0, nd;
    const char *fname = NULL;
    const u8 *bytes = fs_builtin_pkg_bytes(pkg_id, &len, &fname);
    if (!bytes || len <= 0 || !fname) return;
    nd = fs_create_bin(fs_downloads_dir(), fname, bytes, len);
    if (nd >= 0) {
        char msg[128];
        st->last_dl_node = nd;
        strncpy(st->last_dl_name, fname, sizeof(st->last_dl_name) - 1);
        if (st->dl_count < MAX_DLS) strncpy(st->dls[st->dl_count++], fname, 63);
        snprintf(msg, sizeof(msg), "Загружено в Загрузки: %s (%d байт) — нажмите «Запустить»!", fname, len);
        toast(msg);
    }
    wm_invalidate();
}

static void draw_download_hero_page(Rect page, u32 ic, Color accent, const char *title,
                                    const char *sub, const char *bullets[3],
                                    const char *btn1, const char *btn2, int mx, int my) {
    int cx = page.x + page.w / 2;
    Rect card = {page.x + 36, page.y + 24, page.w - 72, page.h - 48};
    int i;

    fill_round_rect(card, 12, RGBA(accent.r, accent.g, accent.b, 24));
    draw_round_rect(card, 12, accent);
    icon_draw(ic, cx - 34, card.y + 22, 68);
    draw_text_center_face(FACE_TITLE, cx, card.y + 100, title, C_WHITE);
    draw_text_center(cx, card.y + 132, sub, RGB(196, 210, 232));

    for (i = 0; i < 3; i++)
        draw_text_center_face(FACE_SMALL, cx, card.y + 166 + i * 22, bullets[i], RGB(224, 234, 248));

    ui_button((Rect){cx - (btn2 ? 250 : 140), card.y + 244, 240, 42}, btn1,
              pt_in(mx, my, (Rect){cx - (btn2 ? 250 : 140), card.y + 244, 240, 42}), 1);
    if (btn2) {
        ui_button((Rect){cx + 10, card.y + 244, 240, 42}, btn2,
                  pt_in(mx, my, (Rect){cx + 10, card.y + 244, 240, 42}), 0);
    }
}

static void br_draw(Window *w, Rect r) {
    BrowserState *st = (BrowserState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    int is_matronix = (st->brand == BRAND_MATRONIX);
    Color chrome_top = is_matronix ? RGB(0, 0, 0)
                     : (st->brand == BRAND_FIREFOX ? RGB(28, 22, 46)
                     : (st->brand == BRAND_CHROME ? RGB(32, 35, 42) : theme->card_bg));
    Color nav_bg = is_matronix ? RGB(10, 10, 14) : theme->win_bg;
    Color page_bg = is_matronix ? RGB(0, 0, 0) : RGB(15, 20, 30);
    int dl_h = st->last_dl_node >= 0 ? 44 : 0;
    Rect page = {r.x, r.y + 102, r.w, r.h - 102 - dl_h};
    static const struct { const char *lbl; const char *url; } bmarks[7] = {
        { "Портал",          "about:home" },
        { "Matronix GitHub", "https://github.com/d50130483-ship-it/Matronix_Browser" },
        { "Google Chrome",   "https://www.google.com/chrome" },
        { "Mozilla Firefox", "https://www.mozilla.org/firefox" },
        { "Steam",           "https://store.steampowered.com" },
        { "Python 3.12",     "https://www.python.org/downloads" },
        { "VS Code",         "https://code.visualstudio.com" },
    };
    int i, bx;

    /* 1. Multi-Tab Strip */
    fill_rect((Rect){r.x, r.y, r.w, 34}, chrome_top);
    for (i = 0; i < st->ntabs; i++) {
        Rect tab = {r.x + 8 + i * 168, r.y + 5, 162, 29};
        int act = (i == st->active_tab);
        fill_round_rect(tab, 6, act ? (is_matronix ? RGB(22, 22, 28) : nav_bg) : RGBA(255, 255, 255, 18));
        if (act && is_matronix) draw_round_rect(tab, 6, RGB(72, 180, 255));
        draw_text_clipped(tab.x + 10, tab.y + 7, 140, st->tab_titles[i], C_WHITE);
    }
    if (st->ntabs < MAX_TABS)
        ui_icon_button((Rect){r.x + 12 + st->ntabs * 168, r.y + 5, 28, 26}, ICON_PLUS,
                       pt_in(mx, my, (Rect){r.x + 12 + st->ntabs * 168, r.y + 5, 28, 26}));

    /* 2. Address Bar + Search Engine Selector */
    fill_rect((Rect){r.x, r.y + 34, r.w, 40}, nav_bg);
    ui_icon_button((Rect){r.x + 8,  r.y + 39, 30, 30}, ICON_BACK, pt_in(mx, my, (Rect){r.x + 8, r.y + 39, 30, 30}));
    ui_textfield((Rect){r.x + 44, r.y + 39, r.w - 230, 30}, st->url,
                 st->url_focused ? st->ucaret : -1, st->url_focused, "Введите адрес сайта или поисковый запрос...");
    ui_button((Rect){r.x + r.w - 180, r.y + 39, 98, 30}, kEngines[st->search_engine],
              pt_in(mx, my, (Rect){r.x + r.w - 180, r.y + 39, 98, 30}), 0);
    ui_button((Rect){r.x + r.w - 76, r.y + 39, 68, 30}, "Перейти",
              pt_in(mx, my, (Rect){r.x + r.w - 76, r.y + 39, 68, 30}), 1);

    /* 3. Bookmarks Bar (or Matronix Quick Bar on Matronix 3.0.0!) */
    fill_rect((Rect){r.x, r.y + 74, r.w, 28}, is_matronix ? RGB(6, 6, 10) : theme->card_bg);
    bx = r.x + 8;
    if (is_matronix) {
        static const struct { const char *l; const char *u; } mlinks[6] = {
            { "Главная #000000", "matronix:home" },
            { "Пароли (Vault)",  "matronix:passwords" },
            { "Загрузки",        "matronix:downloads" },
            { "История",         "matronix:history" },
            { "Настройки 3.0",   "matronix:settings" },
            { "GitHub Релиз",    "https://github.com/d50130483-ship-it/Matronix_Browser" },
        };
        for (i = 0; i < 6; i++) {
            int w2 = text_width_face(FACE_SMALL, mlinks[i].l) + 18;
            Rect br = {bx, r.y + 77, w2, 22};
            if (pt_in(mx, my, br)) fill_round_rect_alpha(br, 4, RGBA(88, 198, 255, 48));
            draw_text_small(br.x + 9, br.y + 4, mlinks[i].l, RGB(180, 214, 255));
            bx += w2 + 6;
        }
    } else {
        for (i = 0; i < 7; i++) {
            int w2 = text_width_face(FACE_SMALL, bmarks[i].lbl) + 16;
            Rect br = {bx, r.y + 77, w2, 22};
            if (pt_in(mx, my, br)) fill_round_rect_alpha(br, 4, RGBA(130, 155, 190, 45));
            draw_text_small(br.x + 8, br.y + 4, bmarks[i].lbl, theme->text_main);
            bx += w2 + 5;
        }
    }
    hline(r.x, r.x + r.w - 1, r.y + 101, RGBA(130, 145, 170, 55));

    /* 4. Web Page Viewport */
    fill_rect(page, page_bg);

    if (strcasestr_ascii(st->url, "Matronix_Browser")) {
        static const char *b[3] = {
            "• Угольно-чёрный минималистичный дизайн (#000000) с мгновенным откликом вкладок",
            "• Быстрое переключение Google / DuckDuckGo / Bing / Яндекс + Менеджер паролей Vault",
            "• Нативные пакеты для YoroOS 0.2: MatronixSetup.exe и архив Matronix-3.0.0-win64.zip"
        };
        draw_download_hero_page(page, ICON_MATRONIX, RGB(72, 184, 255),
            "d50130483-ship-it / Matronix_Browser  —  Release v3.0.0",
            "Официальная страница загрузки Matronix Browser 3.0.0 для Windows и YoroOS",
            b, "Скачать MatronixSetup.exe", "Скачать Matronix-3.0.0.zip", mx, my);
    }
    else if (strcasestr_ascii(st->url, "google.com/chrome")) {
        static const char *b[3] = {
            "• Современный движок Blink + синхронизация вкладок и быстрая омнибокс-строка",
            "• Прямая загрузка установщика ChromeSetup.exe в папку Загрузки (YoroFS)",
            "• Создаёт ярлык Google Chrome на Рабочем столе и в меню «Пуск» YoroOS"
        };
        draw_download_hero_page(page, ICON_CHROME, RGB(66, 133, 244),
            "Google Chrome — Создан для скорости и безопасности",
            "Версия 131.0 (64-разрядная / Windows & YoroOS 0.2 Compatible)",
            b, "Скачать ChromeSetup.exe", "Установить и открыть Chrome", mx, my);
    }
    else if (strcasestr_ascii(st->url, "firefox")) {
        static const char *b[3] = {
            "• Полная защита приватности Enhanced Tracking Protection по умолчанию",
            "• Независимый браузер Mozilla Foundation с низким потреблением памяти",
            "• Установщик FirefoxSetup.exe готов к запуску в подсистеме Win32 YoroOS"
        };
        draw_download_hero_page(page, ICON_FIREFOX, RGB(255, 113, 57),
            "Mozilla Firefox Quantum — Свободный интернет без слежки",
            "Выпуск 133.0 (Русская версия для YoroOS / Windows)",
            b, "Скачать FirefoxSetup.exe", "Установить и открыть Firefox", mx, my);
    }
    else if (strcasestr_ascii(st->url, "steam")) {
        static const char *b[3] = {
            "• Игровая библиотека, магазин и встроенные мини-игры прямо внутри YoroOS 0.2",
            "• Поддержка полноскоростного рендеринга 32bpp GOP в Hyper-V Поколения 2",
            "• Скачайте SteamSetup.exe или установите в один клик"
        };
        draw_download_hero_page(page, ICON_STEAM, RGB(42, 136, 218),
            "Steam — Главная игровая платформа на ПК",
            "Клиент Steam для YoroOS 0.2 «Aurora»",
            b, "Скачать SteamSetup.exe", "Установить и открыть Steam", mx, my);
    }
    else if (strcasestr_ascii(st->url, "python")) {
        static const char *b[3] = {
            "• Интерактивная среда разработки Python 3.12 REPL + запуск .py файлов",
            "• Поддержка циклов, переменных, списков, вычислений и вывода в консоль",
            "• Интеграция с Терминалом YoroOS и редактором Visual Studio Code"
        };
        draw_download_hero_page(page, ICON_PYTHON, RGB(255, 208, 62),
            "Python 3.12.7 — Official Release for YoroOS & Windows",
            "Настоящий интерпретатор и интерактивная IDE для скриптов Python",
            b, "Скачать PythonSetup-3.12.exe", "Установить Python 3.12 сейчас", mx, my);
    }
    else if (strcasestr_ascii(st->url, "visualstudio") || strcasestr_ascii(st->url, "vscode")) {
        static const char *b[3] = {
            "• Умный редактор кода с деревом проекта, подсветкой синтаксиса и терминалом",
            "• Запуск Python (.py), пакетных файлов (.bat) и Win32 (.exe) одной кнопкой F5",
            "• Мгновенное сохранение кода в файловую систему YoroFS2 на диск"
        };
        draw_download_hero_page(page, ICON_VSCODE, RGB(0, 148, 255),
            "Visual Studio Code — Code Editing. Redefined.",
            "Версия 1.95 x64 User Installer для YoroOS 0.2",
            b, "Скачать VSCodeSetup.exe", "Установить VS Code сейчас", mx, my);
    }
    else if (strcasestr_ascii(st->url, "matronix:passwords")) {
        draw_text_title(page.x + 34, page.y + 26, "Matronix 3.0.0 — Зашифрованное хранилище паролей (AES-256)", C_WHITE);
        draw_text_small(page.x + 34, page.y + 58, "Локальный сейф учётных записей Matronix Browser (без облачной передачи данных):", RGB(160, 178, 205));
        for (i = 0; i < 3; i++) {
            static const char *sites[3] = { "github.com/d50130483-ship-it", "store.steampowered.com", "account.yoroos.local" };
            static const char *users[3] = { "ship-it-dev  ·  ••••••••••••", "yoro_gamer   ·  ••••••••••", "Owner (Admin) ·  ••••••••••••••" };
            Rect row = {page.x + 34, page.y + 92 + i * 54, page.w - 68, 46};
            fill_round_rect(row, 8, RGB(14, 16, 22));
            draw_round_rect(row, 8, RGB(52, 64, 88));
            draw_text_bold(row.x + 16, row.y + 13, sites[i], RGB(88, 198, 255));
            draw_text_mono(row.x + 320, row.y + 14, users[i], C_WHITE);
        }
    }
    else if (strcasestr_ascii(st->url, "matronix:history") || strcasestr_ascii(st->url, "matronix:downloads") ||
             strcasestr_ascii(st->url, "matronix:settings")) {
        draw_text_title(page.x + 34, page.y + 24,
                        strcasestr_ascii(st->url, "history") ? "История просмотров Matronix 3.0.0"
                        : (strcasestr_ascii(st->url, "downloads") ? "Менеджер загрузок Matronix 3.0.0"
                                                                  : "Настройки Matronix Browser 3.0.0"), C_WHITE);
        for (i = 0; i < st->nhist && i < 8; i++)
            draw_text_mono(page.x + 38, page.y + 68 + i * 24, st->hist[i], RGB(180, 214, 255));
        draw_text_small(page.x + 34, page.y + page.h - 46,
                        "Тема: Pure Pitch Black (#000000)   |   Блокировщик трекеров: Активен   |   Поисковик: переключается в панели выше",
                        RGB(120, 220, 160));
    }
    else if (!strncmp(st->url, "search:", 7)) {
        const char *q = st->url + 7;
        char hdr[160];
        snprintf(hdr, sizeof(hdr), "Поиск в %s по запросу: «%s»", kEngines[st->search_engine], q);
        draw_text_title(page.x + 32, page.y + 20, hdr, C_WHITE);
        for (i = 0; i < 4; i++) {
            static const char *titles[4] = {
                "Matronix Browser v3.0.0 — Скачать официальный установщик (.EXE / .ZIP)",
                "Google Chrome — Быстрый браузер для Windows и YoroOS",
                "Mozilla Firefox — Свободный браузер с защитой приватности",
                "Центр приложений YoroOS 0.2 — Steam, Python 3.12, VS Code"
            };
            Rect res = {page.x + 32, page.y + 62 + i * 64, page.w - 64, 54};
            fill_round_rect(res, 7, RGB(20, 26, 38));
            draw_round_rect(res, 7, pt_in(mx, my, res) ? theme->accent : RGB(44, 54, 74));
            draw_text_bold(res.x + 14, res.y + 9, titles[i], RGB(104, 188, 255));
            draw_text_small(res.x + 14, res.y + 31, "Нажмите, чтобы перейти на страницу загрузки и установить в YoroOS 0.2", RGB(168, 182, 204));
        }
    }
    else {
        /* Clean Start Portal: Matronix #000000 OR YoroWeb / Chrome / Firefox Home! */
        int cx = page.x + page.w / 2;
        static const struct { const char *name; const char *url; u32 ic; } tiles[6] = {
            { "Matronix 3.0",   "https://github.com/d50130483-ship-it/Matronix_Browser", ICON_MATRONIX },
            { "Google Chrome",  "https://www.google.com/chrome",   ICON_CHROME   },
            { "Firefox",        "https://www.mozilla.org/firefox", ICON_FIREFOX  },
            { "Steam",          "https://store.steampowered.com",  ICON_STEAM    },
            { "Python 3.12",    "https://www.python.org/downloads", ICON_PYTHON  },
            { "VS Code",        "https://code.visualstudio.com",   ICON_VSCODE   },
        };
        draw_text_center_face(FACE_HERO, cx, page.y + 24,
            is_matronix ? "MATRONIX 3.0"
            : (st->brand == BRAND_CHROME ? "Google Chrome"
            : (st->brand == BRAND_FIREFOX ? "Firefox" : "YoroWeb")), C_WHITE);
        draw_text_center_face(FACE_SMALL, cx, page.y + 72,
            is_matronix ? "Минималистичный угольно-чёрный браузер (#000000) · Поиск и быстрые загрузки"
                        : "Выберите браузер или приложение для мгновенного скачивания (.EXE / .ZIP) в YoroOS:",
            RGB(162, 178, 204));

        ui_textfield((Rect){cx - 250, page.y + 98, 404, 38}, st->search_q,
                     st->q_focused ? st->qcaret : -1, st->q_focused,
                     "Найти в интернете (Chrome, Matronix, Steam, Python...)");
        ui_button((Rect){cx + 162, page.y + 98, 92, 38}, "Найти",
                  pt_in(mx, my, (Rect){cx + 162, page.y + 98, 92, 38}), 1);

        for (i = 0; i < 6; i++) {
            int tw = 118, gap = 12;
            int total_w = 6 * tw + 5 * gap;
            Rect card = {cx - total_w / 2 + i * (tw + gap), page.y + 160, tw, 98};
            int hov = pt_in(mx, my, card);
            fill_round_rect(card, 9, hov ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 64)
                                         : RGB(20, 25, 36));
            draw_round_rect(card, 9, hov ? theme->accent : RGB(52, 62, 82));
            icon_draw(tiles[i].ic, card.x + (tw - 38) / 2, card.y + 14, 38);
            draw_text_center_face(FACE_SMALL, card.x + tw / 2, card.y + 64, tiles[i].name, C_WHITE);
        }
    }

    /* 5. Bottom Download Bar (when any .EXE or .ZIP has been downloaded!) */
    if (st->last_dl_node >= 0) {
        Rect dbar = {r.x, r.y + r.h - 44, r.w, 44};
        fill_rect(dbar, RGB(20, 26, 36));
        hline(dbar.x, dbar.x + dbar.w - 1, dbar.y, theme->accent);
        icon_draw(ICON_DOWNLOAD, dbar.x + 12, dbar.y + 10, 24);
        draw_text_bold(dbar.x + 44, dbar.y + 12, st->last_dl_name, C_WHITE);
        ui_button((Rect){dbar.x + dbar.w - 332, dbar.y + 7, 168, 30}, "Запустить / Открыть",
                  pt_in(mx, my, (Rect){dbar.x + dbar.w - 332, dbar.y + 7, 168, 30}), 1);
        ui_button((Rect){dbar.x + dbar.w - 156, dbar.y + 7, 144, 30}, "Папка «Загрузки»",
                  pt_in(mx, my, (Rect){dbar.x + dbar.w - 156, dbar.y + 7, 144, 30}), 0);
    }
}

static void br_submit_url(BrowserState *st) {
    if (!strncmp(st->url, "http", 4) || !strncmp(st->url, "about:", 6) || !strncmp(st->url, "matronix:", 9))
        br_navigate(st, st->url);
    else {
        char q[128];
        snprintf(q, sizeof(q), "search:%s", st->url);
        br_navigate(st, q);
    }
}

static void br_event(Window *w, YoroEvent *e) {
    BrowserState *st = (BrowserState *)w->user;
    Rect r = wm_client(w);
    int is_matronix = (st->brand == BRAND_MATRONIX);
    int dl_h = st->last_dl_node >= 0 ? 44 : 0;
    Rect page = {r.x, r.y + 102, r.w, r.h - 102 - dl_h};
    int i, cx = page.x + page.w / 2;

    if (e->type == EV_KEY) {
        if (st->url_focused) {
            if (e->key == KEY_ENTER) { st->url_focused = 0; br_submit_url(st); return; }
            if (ui_textfield_edit(st->url, sizeof(st->url), &st->ulen, &st->ucaret, e)) wm_invalidate();
            return;
        }
        if (st->q_focused) {
            if (e->key == KEY_ENTER && st->search_q[0]) {
                char q[128];
                snprintf(q, sizeof(q), "search:%s", st->search_q);
                br_navigate(st, q);
                return;
            }
            if (ui_textfield_edit(st->search_q, sizeof(st->search_q), &st->qlen, &st->qcaret, e)) wm_invalidate();
        }
        return;
    }

    if (e->type != EV_MOUSE_DOWN || e->btn != 1) return;

    /* Tabs */
    for (i = 0; i < st->ntabs; i++) {
        if (pt_in(e->x, e->y, (Rect){r.x + 8 + i * 168, r.y + 5, 162, 29})) {
            st->active_tab = i;
            strcpy(st->url, st->tab_urls[i]);
            st->ulen = (int)strlen(st->url); st->ucaret = st->ulen;
            wm_invalidate();
            return;
        }
    }
    if (st->ntabs < MAX_TABS && pt_in(e->x, e->y, (Rect){r.x + 12 + st->ntabs * 168, r.y + 5, 28, 26})) {
        st->active_tab = st->ntabs++;
        br_navigate(st, is_matronix ? "matronix:home" : "about:home");
        return;
    }

    /* Nav bar */
    if (pt_in(e->x, e->y, (Rect){r.x + 8, r.y + 39, 30, 30})) {
        br_navigate(st, is_matronix ? "matronix:home" : "about:home");
        return;
    }
    if (pt_in(e->x, e->y, (Rect){r.x + 44, r.y + 39, r.w - 230, 30})) {
        st->url_focused = 1; st->q_focused = 0; wm_invalidate(); return;
    }
    st->url_focused = 0;
    if (pt_in(e->x, e->y, (Rect){r.x + r.w - 180, r.y + 39, 98, 30})) {
        st->search_engine = (st->search_engine + 1) & 3;
        wm_invalidate();
        return;
    }
    if (pt_in(e->x, e->y, (Rect){r.x + r.w - 76, r.y + 39, 68, 30})) {
        br_submit_url(st);
        return;
    }

    /* Bookmarks bar */
    if (e->y >= r.y + 74 && e->y <= r.y + 101) {
        int bx = r.x + 8;
        if (is_matronix) {
            static const struct { const char *l; const char *u; } mlinks[6] = {
                { "Главная #000000", "matronix:home" },
                { "Пароли (Vault)",  "matronix:passwords" },
                { "Загрузки",        "matronix:downloads" },
                { "История",         "matronix:history" },
                { "Настройки 3.0",   "matronix:settings" },
                { "GitHub Релиз",    "https://github.com/d50130483-ship-it/Matronix_Browser" },
            };
            for (i = 0; i < 6; i++) {
                int w2 = text_width_face(FACE_SMALL, mlinks[i].l) + 18;
                if (pt_in(e->x, e->y, (Rect){bx, r.y + 77, w2, 22})) { br_navigate(st, mlinks[i].u); return; }
                bx += w2 + 6;
            }
        } else {
            static const struct { const char *lbl; const char *url; } bmarks[7] = {
                { "Портал",          "about:home" },
                { "Matronix GitHub", "https://github.com/d50130483-ship-it/Matronix_Browser" },
                { "Google Chrome",   "https://www.google.com/chrome" },
                { "Mozilla Firefox", "https://www.mozilla.org/firefox" },
                { "Steam",           "https://store.steampowered.com" },
                { "Python 3.12",     "https://www.python.org/downloads" },
                { "VS Code",         "https://code.visualstudio.com" },
            };
            for (i = 0; i < 7; i++) {
                int w2 = text_width_face(FACE_SMALL, bmarks[i].lbl) + 16;
                if (pt_in(e->x, e->y, (Rect){bx, r.y + 77, w2, 22})) { br_navigate(st, bmarks[i].url); return; }
                bx += w2 + 5;
            }
        }
    }

    /* Download shelf buttons */
    if (st->last_dl_node >= 0 && e->y >= r.y + r.h - 44) {
        Rect dbar = {r.x, r.y + r.h - 44, r.w, 44};
        if (pt_in(e->x, e->y, (Rect){dbar.x + dbar.w - 332, dbar.y + 7, 168, 30})) {
            fs_open_any_file(st->last_dl_node);
            return;
        }
        if (pt_in(e->x, e->y, (Rect){dbar.x + dbar.w - 156, dbar.y + 7, 144, 30})) {
            char dp[160];
            fs_path(fs_downloads_dir(), dp, sizeof(dp));
            explorer_open_path(dp);
            return;
        }
    }

    /* Download hero action buttons */
    {
        Rect b1 = {cx - 250, page.y + 268, 240, 42};
        Rect b2 = {cx + 10,  page.y + 268, 240, 42};
        if (strcasestr_ascii(st->url, "Matronix_Browser")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "matronix"); return; }
            if (pt_in(e->x, e->y, b2)) { br_trigger_download(st, "matronix_zip"); return; }
        } else if (strcasestr_ascii(st->url, "google.com/chrome")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "chrome"); return; }
            if (pt_in(e->x, e->y, b2)) { app_install(app_find("chrome")); chrome_open(); return; }
        } else if (strcasestr_ascii(st->url, "firefox")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "firefox"); return; }
            if (pt_in(e->x, e->y, b2)) { app_install(app_find("firefox")); firefox_open(); return; }
        } else if (strcasestr_ascii(st->url, "steam")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "steam"); return; }
            if (pt_in(e->x, e->y, b2)) { app_install(app_find("steam")); steam_open(); return; }
        } else if (strcasestr_ascii(st->url, "python")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "python"); return; }
            if (pt_in(e->x, e->y, b2)) { app_install(app_find("python")); python_app_open(); return; }
        } else if (strcasestr_ascii(st->url, "visualstudio") || strcasestr_ascii(st->url, "vscode")) {
            if (pt_in(e->x, e->y, b1)) { br_trigger_download(st, "vscode"); return; }
            if (pt_in(e->x, e->y, b2)) { app_install(app_find("vscode")); vscode_open(); return; }
        } else if (!strncmp(st->url, "search:", 7)) {
            static const char *urls[4] = {
                "https://github.com/d50130483-ship-it/Matronix_Browser",
                "https://www.google.com/chrome",
                "https://www.mozilla.org/firefox",
                "about:home"
            };
            for (i = 0; i < 4; i++) {
                Rect res = {page.x + 32, page.y + 62 + i * 64, page.w - 64, 54};
                if (pt_in(e->x, e->y, res)) {
                    if (i == 3) store_open();
                    else br_navigate(st, urls[i]);
                    return;
                }
            }
        } else {
            static const char *turls[6] = {
                "https://github.com/d50130483-ship-it/Matronix_Browser",
                "https://www.google.com/chrome",
                "https://www.mozilla.org/firefox",
                "https://store.steampowered.com",
                "https://www.python.org/downloads",
                "https://code.visualstudio.com"
            };
            int tw = 118, gap = 12, total_w = 6 * tw + 5 * gap;
            if (pt_in(e->x, e->y, (Rect){cx - 250, page.y + 98, 404, 38})) {
                st->q_focused = 1; wm_invalidate(); return;
            }
            if (pt_in(e->x, e->y, (Rect){cx + 162, page.y + 98, 92, 38}) && st->search_q[0]) {
                char q[128];
                snprintf(q, sizeof(q), "search:%s", st->search_q);
                br_navigate(st, q);
                return;
            }
            for (i = 0; i < 6; i++) {
                Rect card = {cx - total_w / 2 + i * (tw + gap), page.y + 160, tw, 98};
                if (pt_in(e->x, e->y, card)) { br_navigate(st, turls[i]); return; }
            }
        }
    }
}

static void br_close(Window *w) { kfree(w->user); }

static void open_browser_brand(int brand, const char *app_id, const char *win_title, const char *start_url) {
    BrowserState *st = (BrowserState *)kzalloc(sizeof(BrowserState));
    if (!st) return;
    st->brand = brand;
    st->ntabs = 1;
    st->last_dl_node = -1;
    st->q_focused = 1;
    br_navigate(st, start_url);
    wm_open(app_find(app_id), win_title, -1, -1, 860, 545, br_draw, br_event, br_close)->user = st;
}

void browser_open_url(const char *url) { open_browser_brand(BRAND_YOROWEB, "browser", "Браузер YoroWeb", url ? url : "about:home"); }
void browser_open(void) { browser_open_url("about:home"); }
void matronix_open(void) { open_browser_brand(BRAND_MATRONIX, "matronix", "Matronix Browser v3.0.0 (#000000 Edition)", "matronix:home"); }
void chrome_open(void) { open_browser_brand(BRAND_CHROME, "chrome", "Google Chrome", "about:home"); }
void firefox_open(void) { open_browser_brand(BRAND_FIREFOX, "firefox", "Mozilla Firefox", "about:home"); }
