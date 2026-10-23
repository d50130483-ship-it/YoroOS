/* YoroOS - YoroNet browser: home page with Chrome / Firefox / Matronix downloads */
#include "kernel.h"

#define PAGE_HOME 0
#define PAGE_MATRONIX 1
#define PAGE_ABOUT 2
#define PAGE_ERROR 3
#define PAGE_DRIVE 4

static int page = PAGE_HOME;
static char url[160] = "yoro://home";
static int url_len = 10, url_caret = 10, url_focus = 0;
static App *dl_app = NULL;
static u32 dl_start = 0;
static int scroll = 0;

static void go(const char *u) {
    strncpy(url, u, 159);
    url[159] = 0;
    url_len = (int)strlen(url);
    url_caret = url_len;
    url_focus = 0;
    scroll = 0;
    if (!strncmp(u, "yoro://about", 12)) page = PAGE_ABOUT;
    else if (!strncmp(u, "yoro://drive", 12)) page = PAGE_DRIVE;
    else if (!strncmp(u, "yoro://matronix", 15)) page = PAGE_MATRONIX;
    else if (!strncmp(u, "yoro://", 7) || !u[0]) page = PAGE_HOME;
    else if (strstr(u, "matronix")) page = PAGE_MATRONIX;
    else page = PAGE_ERROR;
    wm_invalidate();
}

static int strstr2(const char *h, const char *n) {
    int i, j;
    for (i = 0; h[i]; i++) {
        for (j = 0; n[j] && h[i + j] == n[j]; j++) {}
        if (!n[j]) return 1;
    }
    return 0;
}

static void start_download(App *a) {
    if (!a) return;
    if (a->installed) { app_launch(a); return; }
    dl_app = a;
    dl_start = sys_ticks();
    wm_invalidate();
}

static void dl_tick(void) {
    if (!dl_app) return;
    if (sys_ticks() - dl_start > 220) {
        app_install(dl_app);
        toast("Загрузка завершена — приложение установлено");
        dl_app = NULL;
    }
}

static void draw_tabbar(Rect c) {
    Rect bar = {c.x, c.y, c.w, 38};
    fill_rect(bar, RGB(232, 235, 242));
    fill_rect((Rect){bar.x, bar.y + bar.h - 1, bar.w, 1}, RGB(210, 215, 226));
    {
        Rect tab = {bar.x + 8, bar.y + 6, 220, 32};
        fill_round_rect((Rect){tab.x, tab.y + 4, tab.w, tab.h}, 7, C_WHITE);
        icon_draw(ICON_BROWSER, tab.x + 8, tab.y + 10, 18);
        draw_text_clip(tab.x + 32, tab.y + 11, page == PAGE_HOME ? "Главная — YoroNet" :
                       (page == PAGE_MATRONIX ? "Matronix Browser" : "YoroNet"), RGB(40, 46, 60),
                       (Rect){tab.x + 32, tab.y, tab.w - 40, tab.h});
        if (page == PAGE_HOME || page == PAGE_MATRONIX) draw_line(tab.x + 30, tab.y + 26, tab.x + 180, tab.y + 26, RGB(250, 250, 253));
    }
    {
        Rect plus = {bar.x + 236, bar.y + 8, 26, 26};
        if (pt_in(mouse_get_x(), mouse_get_y(), plus)) fill_round_rect_alpha(plus, 5, RGBA(0, 0, 0, 16));
        icon_draw(ICON_PLUS, plus.x + 5, plus.y + 5, 16);
    }
}

static void draw_toolbar(Rect c) {
    Rect bar = {c.x, c.y + 38, c.w, 46};
    fill_rect(bar, C_WHITE);
    fill_rect((Rect){bar.x, bar.y + bar.h - 1, bar.w, 1}, RGB(228, 232, 240));
    {
        Rect back = {bar.x + 12, bar.y + 9, 28, 28};
        Rect fwd = {bar.x + 44, bar.y + 9, 28, 28};
        Rect home = {bar.x + 76, bar.y + 9, 28, 28};
        Rect go = {bar.x + bar.w - 96, bar.y + 9, 84, 28};
        Rect addr = {bar.x + 112, bar.y + 9, bar.w - 112 - 108, 28};
        int mx = mouse_get_x(), my = mouse_get_y();
        ui_icon_button(back, ICON_BACK, pt_in(mx, my, back));
        ui_icon_button(fwd, ICON_UP, pt_in(mx, my, fwd));
        if (pt_in(mx, my, home)) fill_round_rect(home, 5, RGB(238, 241, 247));
        icon_draw(ICON_BROWSER, home.x + 5, home.y + 5, 18);
        ui_textfield(addr, url, url_caret, url_focus, "Введите адрес или yoro://home");
        ui_button(go, "Перейти", pt_in(mx, my, go), 1);
    }
}

static void draw_download_card(Rect card, App *a, const char *desc) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int hover = pt_in(mx, my, card);
    Rect btn = {card.x + 16, card.y + card.h - 50, card.w - 32, 34};
    ui_card(card, hover, 10);
    icon_draw(a->icon, card.x + (card.w - 54) / 2, card.y + 18, 54);
    draw_text_center(card.x + card.w / 2, card.y + 82, a->name, RGB(28, 34, 46));
    draw_text_center(card.x + card.w / 2, card.y + 106, desc, RGB(132, 140, 156));
    if (dl_app == a) {
        int pct = (int)((sys_ticks() - dl_start) * 100 / 220);
        if (pct > 100) pct = 100;
        fill_round_rect((Rect){card.x + 16, card.y + card.h - 44, card.w - 32, 22}, 6, RGB(232, 235, 242));
        fill_round_rect((Rect){card.x + 16, card.y + card.h - 44, (card.w - 32) * pct / 100, 22}, 6, theme->accent);
        draw_text_center(card.x + card.w / 2, card.y + card.h - 40,
                         a->installed ? "Установлено" : "Загрузка...", C_WHITE);
    } else if (a->installed) {
        ui_button(btn, "Открыть", hover, 1);
    } else {
        ui_button(btn, "Скачать", hover, 1);
    }
}

static void draw_home(Rect p) {
    int w = p.w;
    int cx = p.x + w / 2;
    int cardw = 236, gap = 24;
    int y0 = p.y + 26;
    /* hero */
    {
        Rect hero = {p.x + 24, p.y + 20, p.w - 48, 150};
        fill_round_rect(hero, 14, theme->accent_dark);
        fill_grad_h(hero, theme->accent, theme->accent_dark);
        draw_text_center(cx, hero.y + 34, "YoroNet", C_WHITE);
        draw_text_center(cx, hero.y + 60, "Браузер YoroOS · загрузки приложений", RGB(225, 238, 252));
        {
            Rect badge = {cx - 120, hero.y + 96, 240, 30};
            ui_badge(badge, "yoro://home   ·   интернет-модуль 0.1", RGBA(255, 255, 255, 40), C_WHITE);
        }
        y0 = hero.y + hero.h + 24;
    }
    draw_text(p.x + 24, y0, "Рекомендуемые браузеры", RGB(30, 36, 48));
    {
        int total = cardw * 3 + gap * 2;
        int x = p.x + (w - total) / 2;
        int cy = y0 + 28;
        draw_download_card((Rect){x, cy, cardw, 240}, app_find("chrome"), "Google Chrome");
        draw_download_card((Rect){x + cardw + gap, cy, cardw, 240}, app_find("firefox"), "Mozilla Firefox");
        draw_download_card((Rect){x + 2 * (cardw + gap), cy, cardw, 240}, app_find("matronix"), "YoroOS · свой браузер");
    }
    {
        Rect foot = {p.x + 24, p.y + p.h - 54, p.w - 48, 40};
        fill_round_rect(foot, 8, RGB(240, 243, 249));
        draw_text(foot.x + 14, foot.y + 11, "Все загрузки проходят через Магазин YoroOS — приложения появляются в меню Пуск.",
                  RGB(90, 98, 112));
    }
}

static void draw_matronix(Rect p) {
    Rect hero = {p.x + 24, p.y + 20, p.w - 48, 190};
    fill_round_rect(hero, 14, RGB(36, 26, 72));
    fill_grad_h(hero, RGB(96, 62, 196), RGB(60, 34, 140));
    icon_draw(ICON_MATRONIX, hero.x + 34, hero.y + 46, 96);
    draw_text(hero.x + 154, hero.y + 46, "Matronix Browser", C_WHITE);
    draw_text(hero.x + 154, hero.y + 74, "Авторский браузер проекта YoroOS", RGB(226, 216, 255));
    draw_text(hero.x + 154, hero.y + 100, "github.com/d50130483-ship-it/Matronix_Browser", RGB(196, 182, 255));
    {
        Rect btn = {hero.x + 154, hero.y + 130, 220, 36};
        App *a = app_find("matronix");
        if (dl_app == a) {
            int pct = (int)((sys_ticks() - dl_start) * 100 / 220);
            if (pct > 100) pct = 100;
            ui_progress((Rect){btn.x, btn.y, 220, 20}, pct, theme->accent);
            draw_text(btn.x + 232, btn.y, "Загрузка...", RGB(226, 216, 255));
        } else {
            ui_button(btn, a->installed ? "Открыть Matronix" : "Установить", pt_in(mouse_get_x(), mouse_get_y(), btn), 1);
        }
    }
    {
        Rect card = {p.x + 24, hero.y + hero.h + 24, p.w - 48, 150};
        ui_card(card, 0, 12);
        draw_text(card.x + 20, card.y + 16, "О браузере", RGB(28, 34, 46));
        draw_text(card.x + 20, card.y + 44, "Matronix — лёгкий браузер с собственным движком интерфейса.", RGB(90, 98, 112));
        draw_text(card.x + 20, card.y + 68, "В YoroOS 0.1 установлен демонстрационный режим.", RGB(90, 98, 112));
        draw_text(card.x + 20, card.y + 92, "Полная версия доступна в репозитории проекта.", RGB(90, 98, 112));
    }
}

static void draw_about(Rect p) {
    Rect card = {p.x + 40, p.y + 40, p.w - 80, 260};
    ui_card(card, 0, 12);
    draw_text(card.x + 24, card.y + 20, "О браузере YoroNet", RGB(28, 34, 46));
    draw_text(card.x + 24, card.y + 52, "Версия 0.1 · часть YoroOS", RGB(90, 98, 112));
    draw_text(card.x + 24, card.y + 84, "· вкладки, адресная строка, страницы yoro://", RGB(90, 98, 112));
    draw_text(card.x + 24, card.y + 108, "· установка браузеров прямо со стартовой страницы", RGB(90, 98, 112));
    draw_text(card.x + 24, card.y + 132, "· встроенная страница с Matronix Browser", RGB(90, 98, 112));
    draw_text(card.x + 24, card.y + 168, "Сетевой стек появится в следующих версиях YoroOS.", RGB(150, 158, 172));
    ui_button((Rect){card.x + 24, card.y + 200, 160, 34}, "На главную", 0, 1);
}

static void draw_error(Rect p) {
    int cx = p.x + p.w / 2;
    draw_text_center(cx, p.y + 90, "Нет подключения к интернету", RGB(40, 46, 60));
    draw_text_center(cx, p.y + 122, "YoroOS 0.1 не содержит сетевого стека.", RGB(130, 138, 152));
    draw_text_center(cx, p.y + 146, "Но локальные страницы и Магазин работают.", RGB(130, 138, 152));
    ui_button((Rect){cx - 90, p.y + 190, 180, 36}, "На главную", 0, 1);
}

static void draw_drive(Rect p) {
    Rect card = {p.x + 40, p.y + 40, p.w - 80, 240};
    char buf[160];
    int n = fs_count(fs_root());
    ui_card(card, 0, 12);
    draw_text(card.x + 24, card.y + 20, "Диск C: — YoroFS", RGB(28, 34, 46));
    sprintf(buf, "Объектов в корне: %d", n);
    draw_text(card.x + 24, card.y + 52, buf, RGB(90, 98, 112));
    draw_text(card.x + 24, card.y + 80, "Файловая система живёт в ОЗУ: всё можно удалить и создать заново.", RGB(90, 98, 112));
    ui_progress((Rect){card.x + 24, card.y + 120, card.w - 48, 18}, 34, theme->accent);
    draw_text(card.x + 24, card.y + 150, "Использовано 34% демонстрационного объёма", RGB(150, 158, 172));
    ui_button((Rect){card.x + 24, card.y + 186, 190, 34}, "Открыть Проводник", 0, 1);
}

static void draw(Window *w, Rect c) {
    Rect page_r = {c.x, c.y + 84, c.w, c.h - 84};
    dl_tick();
    draw_tabbar(c);
    draw_toolbar(c);
    fill_rect(page_r, RGB(247, 249, 253));
    gfx_set_clip(page_r);
    if (page == PAGE_HOME) draw_home(page_r);
    else if (page == PAGE_MATRONIX) draw_matronix(page_r);
    else if (page == PAGE_ABOUT) draw_about(page_r);
    else if (page == PAGE_DRIVE) draw_drive(page_r);
    else draw_error(page_r);
    gfx_set_clip(c);
    UNUSED(w);
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    Rect page_r = {c.x, c.y + 84, c.w, c.h - 84};

    if (e->type == EV_KEY) {
        if (url_focus) {
            if (e->key == KEY_ENTER) { url[url_len] = 0; go(url); }
            else if (e->key == KEY_ESC) url_focus = 0;
            else if (ui_textfield_edit(url, 160, &url_len, &url_caret, e)) wm_invalidate();
            return;
        }
        if (e->ch == '/') { url_focus = 1; }
        return;
    }
    if (e->type == EV_MOUSE_DOWN) {
        Rect addr = {c.x + 112, c.y + 38 + 9, c.w - 112 - 108, 28};
        Rect gob = {c.x + c.w - 96, c.y + 38 + 9, 84, 28};
        Rect homeb = {c.x + 76, c.y + 47, 28, 28};
        Rect back = {c.x + 12, c.y + 47, 28, 28};
        if (pt_in(mx, my, addr)) { url_focus = 1; url_caret = url_len; wm_invalidate(); return; }
        url_focus = 0;
        if (pt_in(mx, my, gob)) { url[url_len] = 0; go(url); return; }
        if (pt_in(mx, my, homeb)) { go("yoro://home"); return; }
        if (pt_in(mx, my, back)) { go("yoro://home"); return; }
        /* content links */
        if (page == PAGE_HOME) {
            int cardw = 236, gap = 24, total = cardw * 3 + gap * 2;
            int x = page_r.x + (page_r.w - total) / 2;
            int cy = page_r.y + 26 + 150 + 24 + 28;
            Rect c1 = {x, cy, cardw, 240};
            Rect c2 = {x + cardw + gap, cy, cardw, 240};
            Rect c3 = {x + 2 * (cardw + gap), cy, cardw, 240};
            Rect b1 = {c1.x + 16, c1.y + 190, cardw - 32, 34};
            Rect b2 = {c2.x + 16, c2.y + 190, cardw - 32, 34};
            Rect b3 = {c3.x + 16, c3.y + 190, cardw - 32, 34};
            if (pt_in(mx, my, b1)) { start_download(app_find("chrome")); return; }
            if (pt_in(mx, my, b2)) { start_download(app_find("firefox")); return; }
            if (pt_in(mx, my, b3)) { start_download(app_find("matronix")); return; }
        }
        if (page == PAGE_MATRONIX) {
            Rect hero = {page_r.x + 24, page_r.y + 20, page_r.w - 48, 190};
            Rect btn = {hero.x + 154, hero.y + 130, 220, 36};
            if (pt_in(mx, my, btn)) { start_download(app_find("matronix")); return; }
        }
        if (page == PAGE_ERROR || page == PAGE_ABOUT) {
            Rect b = {page_r.x + page_r.w / 2 - 90, page_r.y + 190, 180, 36};
            if (pt_in(mx, my, b)) { go("yoro://home"); return; }
        }
        if (page == PAGE_DRIVE) {
            Rect card = {page_r.x + 40, page_r.y + 40, page_r.w - 80, 240};
            Rect b = {card.x + 24, card.y + 186, 190, 34};
            if (pt_in(mx, my, b)) { explorer_open(); return; }
        }
        wm_invalidate();
    }
    UNUSED(w);
}

void browser_open(void) {
    if (!url[0]) go("yoro://home");
    wm_open(app_find("browser"), "YoroNet — браузер", -1, -1, 980, 640, draw, on_event, NULL);
    wm_invalidate();
}
