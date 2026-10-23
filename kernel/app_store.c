/* YoroOS - App Store with icons and installation progress */
#include "kernel.h"

#define CARD_W 320
#define CARD_H 132
#define GAP 16

static int scroll = 0;
static int category = 0;
static App *installing = NULL;
static u32 install_start = 0;
static char search[64] = "";
static int search_len = 0, search_caret = 0, search_focus = 0;

static const char *categories[] = { "Все", "Браузеры", "Разработка", "Игры", "Мультимедиа" };

static const char *app_desc(App *a) {
    if (!strcmp(a->id, "steam")) return "Игровая платформа";
    if (!strcmp(a->id, "python")) return "Язык программирования";
    if (!strcmp(a->id, "vscode")) return "Редактор кода";
    if (!strcmp(a->id, "chrome")) return "Браузер Google";
    if (!strcmp(a->id, "firefox")) return "Браузер Mozilla";
    if (!strcmp(a->id, "matronix")) return "Браузер проекта";
    if (!strcmp(a->id, "git")) return "Система контроля версий";
    if (!strcmp(a->id, "vlc")) return "Медиаплеер";
    if (!strcmp(a->id, "discord")) return "Общение и голос";
    if (!strcmp(a->id, "paint")) return "Рисование";
    if (!strcmp(a->id, "explorer")) return "Файловый менеджер";
    if (!strcmp(a->id, "terminal")) return "Командная строка";
    if (!strcmp(a->id, "settings")) return "Настройки системы";
    if (!strcmp(a->id, "browser")) return "Встроенный браузер";
    if (!strcmp(a->id, "store")) return "Магазин приложений";
    if (!strcmp(a->id, "notepad")) return "Текстовый редактор";
    if (!strcmp(a->id, "calc")) return "Калькулятор";
    return "Приложение YoroOS";
}

static int in_category(App *a) {
    if (category == 0) return 1;
    if (category == 1) return !strcmp(a->id, "chrome") || !strcmp(a->id, "firefox") || !strcmp(a->id, "matronix") || !strcmp(a->id, "browser");
    if (category == 2) return !strcmp(a->id, "vscode") || !strcmp(a->id, "python") || !strcmp(a->id, "git") || !strcmp(a->id, "terminal");
    if (category == 3) return !strcmp(a->id, "steam") || !strcmp(a->id, "discord");
    if (category == 4) return !strcmp(a->id, "vlc") || !strcmp(a->id, "paint");
    return 1;
}

static int matches_search(App *a) {
    if (!search[0]) return 1;
    {
        const char *hay = a->name;
        int i, j;
        for (i = 0; hay[i]; i++) {
            for (j = 0; search[j] && hay[i + j] == search[j]; j++) {}
            if (!search[j]) return 1;
        }
    }
    return 0;
}

static void install_poll(void) {
    if (!installing) return;
    if (sys_ticks() - install_start > 170) {
        app_install(installing);
        installing = NULL;
    }
}

static void draw_card(Rect r, App *a) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int hover = pt_in(mx, my, r);
    Rect btn = {r.x + r.w - 122, r.y + r.h - 46, 106, 32};
    ui_card(r, hover, 10);
    icon_draw(a->icon, r.x + 14, r.y + 14, 48);
    draw_text_clip(r.x + 76, r.y + 18, a->name, RGB(28, 34, 46),
                   (Rect){r.x + 76, r.y, r.w - 90, r.h});
    draw_text_clip(r.x + 76, r.y + 40, app_desc(a), RGB(134, 142, 158),
                   (Rect){r.x + 76, r.y, r.w - 90, r.h});
    if (installing == a) {
        int pct = (int)((sys_ticks() - install_start) * 100 / 170);
        if (pct > 100) pct = 100;
        ui_progress((Rect){r.x + 76, r.y + r.h - 54, r.w - 92, 8}, pct, theme->accent);
        draw_text(r.x + 76, r.y + r.h - 40, "Установка...", RGB(120, 128, 144));
    } else {
        draw_text(r.x + 16, r.y + r.h - 42, a->installed ? "Установлено" : "Бесплатно",
                  a->installed ? RGB(60, 170, 110) : RGB(0, 120, 215));
        ui_button(btn, a->installed ? "Открыть" : "Установить", hover, a->installed ? 0 : 1);
    }
}

static void draw(Window *w, Rect c) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int i, n = app_count();
    Rect head = {c.x, c.y, c.w, 96};
    Rect content = {c.x + 20, c.y + 104, c.w - 40, c.h - 108};

    install_poll();

    fill_grad_v(head, RGB(16, 32, 58), RGB(20, 48, 88));
    draw_text(head.x + 24, head.y + 18, "Магазин YoroOS", C_WHITE);
    draw_text(head.x + 24, head.y + 42, "Приложения с иконками · установка в один клик", RGB(170, 195, 230));
    {
        Rect sr = {head.x + head.w - 300, head.y + 30, 276, 34};
        ui_textfield(sr, search, search_caret, search_focus, "Поиск приложений");
    }
    /* categories */
    {
        int x = c.x + 20;
        for (i = 0; i < 5; i++) {
            Rect chip = {x, c.y + 62, 0, 30};
            chip.w = text_width(categories[i]) + 28;
            if (category == i) fill_round_rect(chip, 15, theme->accent);
            else fill_round_rect(chip, 15, RGB(236, 240, 247));
            draw_text_center(chip.x + chip.w / 2, chip.y + (chip.h - FONT_H) / 2, categories[i],
                             category == i ? C_WHITE : RGB(70, 78, 92));
            x += chip.w + 8;
        }
    }

    gfx_set_clip(content);
    {
        int idx = 0, cols = 2;
        for (i = 0; i < n; i++) {
            App *a = app_by_index(i);
            int col, row;
            Rect card;
            if (!a->installed && !in_category(a)) continue;
            if (!matches_search(a)) continue;
            col = idx % cols;
            row = idx / cols;
            card.x = content.x + col * (CARD_W + GAP);
            card.y = content.y + row * (CARD_H + GAP) - scroll;
            card.w = CARD_W;
            card.h = CARD_H;
            if (card.y + CARD_H > content.y - 40 && card.y < content.y + content.h + 40)
                draw_card(card, a);
            idx++;
        }
        gfx_set_clip(c);
        {
            int rows = (idx + 1) / 2;
            Rect track = {c.x + c.w - 12, content.y, 6, content.h};
            ui_scrollbar(track, rows * (CARD_H + GAP), content.h, scroll);
        }
    }
    UNUSED(mx); UNUSED(my);
    UNUSED(w);
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    Rect content = {c.x + 20, c.y + 104, c.w - 40, c.h - 108};
    int i;

    if (e->type == EV_KEY && search_focus) {
        if (e->key == KEY_ENTER) search_focus = 0;
        else if (ui_textfield_edit(search, 64, &search_len, &search_caret, e)) {
            scroll = 0;
            wm_invalidate();
        }
        return;
    }
    if (e->type == EV_WHEEL) {
        int rows = (app_count() + 1) / 2;
        int maxs = rows * (CARD_H + GAP) - content.h;
        scroll -= e->wheel * 60;
        if (scroll < 0) scroll = 0;
        if (scroll > maxs) scroll = maxs > 0 ? maxs : 0;
        wm_invalidate();
        return;
    }
    if (e->type != EV_MOUSE_DOWN) return;

    {
        Rect sr = {c.x + c.w - 300, c.y + 30, 276, 34};
        if (pt_in(mx, my, sr)) { search_focus = 1; search_caret = search_len; wm_invalidate(); return; }
        search_focus = 0;
    }
    {
        int x = c.x + 20;
        for (i = 0; i < 5; i++) {
            Rect chip = {x, c.y + 62, text_width(categories[i]) + 28, 30};
            if (pt_in(mx, my, chip)) { category = i; scroll = 0; wm_invalidate(); return; }
            x += chip.w + 8;
        }
    }
    {
        int idx = 0, n = app_count();
        for (i = 0; i < n; i++) {
            App *a = app_by_index(i);
            int col, row;
            Rect card;
            if (!a->installed && !in_category(a)) continue;
            if (!matches_search(a)) continue;
            col = idx % 2;
            row = idx / 2;
            card.x = content.x + col * (CARD_W + GAP);
            card.y = content.y + row * (CARD_H + GAP) - scroll;
            card.w = CARD_W;
            card.h = CARD_H;
            if (pt_in(mx, my, (Rect){card.x + card.w - 122, card.y + card.h - 46, 106, 32})) {
                if (installing) return;
                if (a->installed) app_launch(a);
                else {
                    installing = a;
                    install_start = sys_ticks();
                    wm_invalidate();
                }
                return;
            }
            idx++;
        }
    }
    UNUSED(w);
}

void store_open(void) {
    wm_open(app_find("store"), "Магазин YoroOS", -1, -1, 880, 620, draw, on_event, NULL);
    wm_invalidate();
}
