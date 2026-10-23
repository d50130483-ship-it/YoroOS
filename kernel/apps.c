/* YoroOS 0.2 - System & Store Application Registry */
#include "kernel.h"

extern const u8 *fs_builtin_pkg_bytes(const char *id, int *out_len, const char **out_filename);

static App g_apps[] = {
    /* Core 5 System Applications + Essential Windows Tools (pre-installed) */
    { "explorer",  "Проводник",          ICON_FOLDER,   1, 1, 1, 0, 820, 520, explorer_open },
    { "browser",   "Браузер YoroWeb",    ICON_BROWSER,  1, 1, 1, 1, 860, 540, browser_open },
    { "store",     "Центр приложений",   ICON_STORE,    1, 1, 1, 1, 840, 530, store_open },
    { "terminal",  "Терминал YoroOS",    ICON_TERMINAL, 1, 1, 1, 1, 760, 470, terminal_open },
    { "settings",  "Параметры YoroOS",   ICON_SETTINGS, 1, 1, 1, 1, 820, 520, settings_open },
    { "installer", "Установка YoroOS",   ICON_SETUP,    1, 1, 1, 1, 820, 540, installer_open },
    { "notepad",   "Блокнот",            ICON_NOTEPAD,  1, 1, 1, 0, 680, 450, notepad_open },
    { "calc",      "Калькулятор",        ICON_CALC,     1, 1, 1, 0, 340, 450, calc_open },
    { "paint",     "Графический Paint",  ICON_PAINT,    1, 1, 1, 0, 740, 490, paint_open },
    { "taskmgr",   "Диспетчер задач",    ICON_TASKMGR,  1, 1, 1, 0, 680, 450, taskmgr_open },
    { "regedit",   "Редактор реестра",   ICON_REGEDIT,  1, 1, 0, 0, 720, 450, regedit_open },
    { "about",     "О системе YoroOS",   ICON_ABOUT,    1, 1, 0, 0, 540, 390, about_open },

    /* Installable Store / Browser Applications (with full icons, Desktop shortcuts & real environments!) */
    { "matronix",  "Matronix Browser",   ICON_MATRONIX, 0, 0, 1, 1, 860, 540, matronix_open },
    { "chrome",    "Google Chrome",      ICON_CHROME,   0, 0, 1, 1, 860, 540, chrome_open },
    { "firefox",   "Mozilla Firefox",    ICON_FIREFOX,  0, 0, 1, 1, 860, 540, firefox_open },
    { "steam",     "Steam",              ICON_STEAM,    0, 0, 1, 1, 840, 520, steam_open },
    { "python",    "Python 3.12 IDE",    ICON_PYTHON,   0, 0, 1, 1, 780, 500, python_app_open },
    { "vscode",    "Visual Studio Code", ICON_VSCODE,   0, 0, 1, 1, 860, 540, vscode_open },
    { "discord",   "Discord",            ICON_DISCORD,  0, 0, 1, 1, 800, 500, discord_open },
    { "vlc",       "VLC Media Player",   ICON_VLC,      0, 0, 1, 1, 660, 440, vlc_open },
    { "git",       "Git Bash",           ICON_GIT,      0, 0, 1, 1, 740, 460, git_app_open },
};

#define N_APPS ((int)(sizeof(g_apps) / sizeof(g_apps[0])))

void apps_init(void) {
    /* If Developer or Gaming edition selected during setup, pre-install edition bundle! */
    if (sys_profile.edition == EDITION_DEV) {
        app_find("python")->installed = 1;
        app_find("vscode")->installed = 1;
        app_find("git")->installed = 1;
    } else if (sys_profile.edition == EDITION_GAMING) {
        app_find("steam")->installed = 1;
        app_find("discord")->installed = 1;
        app_find("matronix")->installed = 1;
    }
}

int app_count(void) { return N_APPS; }
App *app_by_index(int i) { return (i >= 0 && i < N_APPS) ? &g_apps[i] : NULL; }

App *app_find(const char *id) {
    int i;
    if (!id) return NULL;
    for (i = 0; i < N_APPS; i++)
        if (!strcasecmp(g_apps[i].id, id) || !strcasecmp(g_apps[i].name, id))
            return &g_apps[i];
    return NULL;
}

void app_launch(App *a) {
    if (!a) return;
    if (!a->installed) {
        toast("Приложение ещё не установлено — откройте Центр приложений или Браузер.");
        return;
    }
    if (a->open) a->open();
}

void app_install(App *a) {
    char pdir_path[128], msg[128];
    int pdir, exe_len = 0;
    const char *exe_name = NULL;
    const u8 *exe_bytes;
    if (!a) return;
    a->installed = 1;
    a->on_desktop = 1;
    a->pinned = 1;

    /* Create real directory & real Windows PE executable inside C:\Program Files\<App>! */
    snprintf(pdir_path, sizeof(pdir_path), "C:\\Program Files\\%s", a->name);
    pdir = fs_mkdir_p(fs_root(), pdir_path);
    if (pdir >= 0) {
        exe_bytes = fs_builtin_pkg_bytes(a->id, &exe_len, &exe_name);
        if (exe_bytes && exe_len > 0) {
            char binname[48];
            snprintf(binname, sizeof(binname), "%s.exe", a->id);
            fs_create_bin(pdir, binname, exe_bytes, exe_len);
        }
        fs_create(pdir, "app.manifest",
                  "Installed via YoroOS 0.2 Package Manager (Desktop + Start Menu registered)\n");
    }

    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    desktop_rebuild();
    snprintf(msg, sizeof(msg), "Установлено: %s (ярлык добавлен на Рабочий стол и в «Пуск»)", a->name);
    toast(msg);
    wm_invalidate();
}

void app_uninstall(App *a) {
    char pdir_path[128], msg[128];
    int pdir;
    if (!a || a->builtin) return;
    a->installed = 0;
    snprintf(pdir_path, sizeof(pdir_path), "C:\\Program Files\\%s", a->name);
    pdir = fs_parse_path(fs_root(), pdir_path);
    if (pdir >= 0) fs_remove(pdir);
    if (sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
    desktop_rebuild();
    snprintf(msg, sizeof(msg), "Приложение «%s» удалено с компьютера.", a->name);
    toast(msg);
    wm_invalidate();
}

void app_desktop_rebuild(void) { desktop_rebuild(); }
