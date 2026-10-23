/* YoroOS 0.2 - Graphical App Installer & Package Store with Live Icons */
#include "kernel.h"

typedef struct {
    int  cat; /* 0 = Все, 1 = Браузеры, 2 = Разработка, 3 = Игры и Общение */
    char query[64];
    int  qlen, qcaret;
} StoreState;

typedef struct {
    const char *id;
    int cat;
    const char *ver;
    const char *desc;
} StoreEntry;

static const StoreEntry kCatalog[] = {
    { "matronix", 1, "v3.0.0",  "Ультра-быстрый минималистичный браузер #000000 (вкладки, Vault паролей, 4 поисковика)" },
    { "steam",    3, "v2.10",   "Официальный игровой клиент Steam с библиотекой игр и встроенными мини-играми" },
    { "python",   2, "v3.12.7", "Интерактивная среда разработки Python 3.12 REPL и интерпретатор .py скриптов" },
    { "vscode",   2, "v1.95",   "Редактор кода Visual Studio Code с проводником проекта, подсветкой и запуском кода" },
    { "chrome",   1, "v131.0",  "Браузер Google Chrome с поддержкой загрузок .EXE и .ZIP пакетов в YoroOS" },
    { "firefox",  1, "v133.0",  "Свободный приватный браузер Mozilla Firefox с защитой от отслеживания" },
    { "discord",  3, "v1.0.9",  "Голосовые и текстовые серверы сообществ разработчиков и геймеров YoroOS" },
    { "vlc",      3, "v3.0.21", "Медиаплеер VLC с визуальным спектроанализатором и синтезатором мелодий" },
    { "git",      2, "v2.47",   "Система контроля версий Git Bash с локальными коммитами и историей веток" },
};
#define N_CAT ((int)(sizeof(kCatalog) / sizeof(kCatalog[0])))

static void store_draw(Window *w, Rect r) {
    StoreState *st = (StoreState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    static const char *cats[4] = { "Все приложения", "Браузеры", "Разработка", "Игры и Медиа" };
    int i, y;

    fill_rect(r, theme->win_bg);

    /* Featured Hero Banner */
    {
        Rect hero = {r.x + 16, r.y + 12, r.w - 32, 92};
        fill_grad_h(hero, RGB(12, 46, 108), RGB(0, 128, 172));
        draw_round_rect(hero, 10, theme->accent);
        icon_draw(ICON_STORE, hero.x + 18, hero.y + 18, 56);
        draw_text_title(hero.x + 90, hero.y + 16, "Центр установки приложений YoroOS 0.2", C_WHITE);
        draw_text_small(hero.x + 90, hero.y + 48,
                        "Устанавливайте Steam, Python 3.12, VS Code, Matronix 3.0, Chrome и Firefox в 1 клик с иконками!",
                        RGB(220, 240, 255));
    }

    /* Filter Tabs + Search Box */
    for (i = 0; i < 4; i++) {
        Rect cb = {r.x + 16 + i * 138, r.y + 114, 130, 32};
        ui_button(cb, cats[i], pt_in(mx, my, cb), st->cat == i);
    }
    ui_textfield((Rect){r.x + r.w - 252, r.y + 114, 236, 32}, st->query, st->qcaret, 1, "Поиск пакета...");

    /* Scrollable Rich App Cards with Big Vector Icons */
    y = r.y + 158 - w->scroll;
    {
        Rect saved = gfx_get_clip();
        gfx_set_clip((Rect){r.x, r.y + 154, r.w, r.h - 156});
        for (i = 0; i < N_CAT; i++) {
            App *a = app_find(kCatalog[i].id);
            Rect card = {r.x + 16, y, r.w - 32, 68};
            if (!a) continue;
            if (st->cat != 0 && kCatalog[i].cat != st->cat) continue;
            if (st->query[0] && !strcasestr_ascii(a->name, st->query) && !strcasestr_ascii(a->id, st->query))
                continue;

            if (y + 68 >= r.y + 154 && y < r.y + r.h) {
                ui_card(card, pt_in(mx, my, card), 8);
                icon_draw(a->icon, card.x + 14, card.y + 10, 48);
                draw_text_bold(card.x + 76, card.y + 12, a->name, theme->text_main);
                ui_badge((Rect){card.x + 76 + text_width(a->name) + 10, card.y + 11, 58, 20},
                         kCatalog[i].ver, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 44), theme->accent);
                draw_text_clipped(card.x + 76, card.y + 38, card.w - 290, kCatalog[i].desc, theme->text_muted);

                if (a->installed) {
                    ui_button((Rect){card.x + card.w - 204, card.y + 17, 100, 34}, "Открыть",
                              pt_in(mx, my, (Rect){card.x + card.w - 204, card.y + 17, 100, 34}), 1);
                    ui_button_danger((Rect){card.x + card.w - 96, card.y + 17, 84, 34}, "Удалить",
                                     pt_in(mx, my, (Rect){card.x + card.w - 96, card.y + 17, 84, 34}));
                } else {
                    ui_button((Rect){card.x + card.w - 166, card.y + 17, 152, 34}, "Установить",
                              pt_in(mx, my, (Rect){card.x + card.w - 166, card.y + 17, 152, 34}), 1);
                }
            }
            y += 76;
        }
        gfx_set_clip(saved);
    }
}

static void store_event(Window *w, YoroEvent *e) {
    StoreState *st = (StoreState *)w->user;
    Rect r = wm_client(w);
    int i, y;

    if (e->type == EV_WHEEL) {
        w->scroll -= e->wheel * 38;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type == EV_KEY) {
        if (ui_textfield_edit(st->query, sizeof(st->query), &st->qlen, &st->qcaret, e)) {
            w->scroll = 0;
            wm_invalidate();
        }
        return;
    }
    if (e->type != EV_MOUSE_DOWN || e->btn != 1) return;

    for (i = 0; i < 4; i++) {
        if (pt_in(e->x, e->y, (Rect){r.x + 16 + i * 138, r.y + 114, 130, 32})) {
            st->cat = i; w->scroll = 0; wm_invalidate(); return;
        }
    }

    y = r.y + 158 - w->scroll;
    for (i = 0; i < N_CAT; i++) {
        App *a = app_find(kCatalog[i].id);
        Rect card = {r.x + 16, y, r.w - 32, 68};
        if (!a) continue;
        if (st->cat != 0 && kCatalog[i].cat != st->cat) continue;
        if (st->query[0] && !strcasestr_ascii(a->name, st->query) && !strcasestr_ascii(a->id, st->query))
            continue;
        if (e->y >= r.y + 154) {
            if (a->installed) {
                if (pt_in(e->x, e->y, (Rect){card.x + card.w - 204, card.y + 17, 100, 34})) { app_launch(a); return; }
                if (pt_in(e->x, e->y, (Rect){card.x + card.w - 96, card.y + 17, 84, 34})) { app_uninstall(a); return; }
            } else {
                if (pt_in(e->x, e->y, (Rect){card.x + card.w - 166, card.y + 17, 152, 34})) {
                    app_install(a);
                    return;
                }
            }
        }
        y += 76;
    }
}

static void store_close(Window *w) { kfree(w->user); }

void store_open(void) {
    StoreState *st = (StoreState *)kzalloc(sizeof(StoreState));
    if (!st) return;
    wm_open(app_find("store"), "Центр приложений YoroOS 0.2", -1, -1, 840, 535, store_draw, store_event, store_close)->user = st;
}
