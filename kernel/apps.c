/* YoroOS - application registry */
#include "kernel.h"

static App apps[] = {
    { "explorer", "Проводник",        ICON_FOLDER,   1, 1, 1, 900, 600, explorer_open },
    { "terminal", "Терминал",         ICON_TERMINAL, 1, 1, 1, 800, 480, terminal_open },
    { "settings", "Параметры",        ICON_SETTINGS, 1, 1, 1, 860, 620, settings_open },
    { "browser",  "YoroNet",          ICON_BROWSER,  1, 1, 1, 960, 620, browser_open },
    { "store",    "Магазин YoroOS",   ICON_STORE,    1, 1, 1, 880, 600, store_open },
    { "notepad",  "Блокнот",          ICON_NOTEPAD,  1, 1, 0, 720, 520, notepad_open },
    { "calc",     "Калькулятор",      ICON_CALC,     1, 1, 0, 380, 520, calc_open },
    { "about",    "О системе",        ICON_ABOUT,    1, 1, 0, 620, 480, about_open },
    { "installer", "Установка YoroOS", ICON_SETUP,   1, 1, 1, 840, 600, installer_open },
    { "zip",      "Архив ZIP",        ICON_ZIP,      2, 1, -1, 720, 540, NULL },
    { "exe",      "Запуск EXE",       ICON_EXE,      2, 1, -1, 560, 460, NULL },
    /* store catalogue */
    { "steam",    "Steam",            ICON_STEAM,    0, 0, 0, 900, 600, NULL },
    { "python",   "Python 3.12",      ICON_PYTHON,   0, 0, 0, 800, 500, NULL },
    { "vscode",   "Visual Studio Code", ICON_VSCODE, 0, 0, 0, 980, 640, NULL },
    { "chrome",   "Google Chrome",    ICON_CHROME,   0, 0, 0, 980, 640, NULL },
    { "firefox",  "Mozilla Firefox",  ICON_FIREFOX,  0, 0, 0, 980, 640, NULL },
    { "matronix", "Matronix Browser", ICON_MATRONIX, 0, 0, 0, 980, 640, NULL },
    { "git",      "Git",              ICON_GIT,      0, 0, 0, 800, 500, NULL },
    { "vlc",      "VLC Media Player", ICON_VLC,      0, 0, 0, 900, 560, NULL },
    { "discord",  "Discord",          ICON_DISCORD,  0, 0, 0, 900, 600, NULL },
    { "paint",    "Paint",            ICON_PAINT,    0, 0, 0, 900, 600, NULL }
};

int app_count(void) { return (int)(sizeof(apps) / sizeof(apps[0])); }

App *app_by_index(int i) {
    if (i < 0 || i >= app_count()) return NULL;
    return &apps[i];
}

App *app_find(const char *id) {
    int i;
    for (i = 0; i < app_count(); i++)
        if (!strcmp(apps[i].id, id)) return &apps[i];
    return NULL;
}

void apps_init(void) {
    int i;
    for (i = 0; i < app_count(); i++) {
        if (apps[i].builtin) apps[i].installed = 1;
        apps[i].pinned = 0;
    }
    app_find("explorer")->pinned = 1;
    app_find("terminal")->pinned = 1;
    app_find("browser")->pinned = 1;
    app_find("store")->pinned = 1;
    app_find("settings")->pinned = 1;
}

void app_launch(App *a) {
    char msg[160];
    if (!a) return;
    if (!a->installed) {
        snprintf(msg, sizeof(msg), "%s не установлен — откройте Магазин YoroOS", a->name);
        toast(msg);
        store_open();
        return;
    }
    if (a->open) { a->open(); return; }
    /* store apps launch a generic demo window */
    {
        char title[80];
        snprintf(title, sizeof(title), "%s", a->name);
        {
            static App *demo_app;
            demo_app = a;
            wm_open(a, title, -1, -1, a->width, a->height, NULL, NULL, NULL);
            toast("Запущено (демо-окно)");
        }
    }
}

void app_install(App *a) {
    char msg[160];
    if (!a || a->installed) return;
    a->installed = 1;
    snprintf(msg, sizeof(msg), "%s установлен", a->name);
    toast(msg);
    wm_invalidate();
}

void app_desktop_rebuild(void) { wm_invalidate(); }
