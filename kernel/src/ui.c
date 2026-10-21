/* YoroOS UI: меню-бар, док, окна, курсор */
#include "ui.h"
#include "gfx.h"
#include "hw.h"
#include "fs.h"
#include "klib.h"

/* ---- приложения (реализация в apps.c) ---- */
const char *app_name(int id);
void app_anim(void);
void app_open(int id);
void app_key(int id, int code, int ascii);
void app_click(int id, int cx, int cy, int w, int h);
void app_draw(int id, int x, int y, int w, int h);
void app_close_cleanup(int id);
void app_default_size(int id, int *w, int *h);
void draw_app_icon(int id, int x, int y, int s);

/* ---- состояние ---- */
struct win {
    int used, app, x, y, w, h, min, maxed;
    int px, py, pw, ph; /* до максимизации */
    char title[26];
};
static struct win wins[8];
static int focused = -1;
static int mx = 380, my = 260, mbtn = 0, mbtn_prev = 0;
static int drag = -1, drag_dx, drag_dy;
static int resize = -1, rs_w, rs_h;
static int menu_open = 0;
static int dirty = 1;
static int win_serial = 0;
static int shutting_down = 0;

static void scat(char *dst, const char *src) {
    while (*dst) dst++;
    while ((*dst++ = *src++)) {}
}
static void snum(char *dst, int v) {
    char b[12];
    itoa(v, b);
    if (v >= 0 && v < 10) scat(dst, "0");
    scat(dst, b);
}

static const char *menu_items[] = { "О YoroOS", "Терминал", "Настройки", "", "Перезагрузить…", "Выключить…" };
#define MENU_N 6
#define MENU_W 156
#define MENU_IH 17

/* ---- API окон ---- */
int ui_find_win(int app) {
    for (int i = 0; i < 8; i++)
        if (wins[i].used && wins[i].app == app) return i;
    return -1;
}
int ui_win_app(int idx) { return wins[idx].app; }
int ui_focus(void) { return focused; }
int ui_minimized(int idx) { return wins[idx].min; }
void ui_unminimize(int idx) { wins[idx].min = 0; dirty = 1; }
void ui_set_title(int idx, const char *s) { strcpy(wins[idx].title, s, sizeof(wins[idx].title)); dirty = 1; }
int ui_mx(void) { return mx; }
int ui_my(void) { return my; }
int ui_mbuttons(void) { return mbtn; }
void ui_request_redraw(void) { dirty = 1; }
void ui_set_menu_shown(int on) { menu_open = on; dirty = 1; }

static void focus_win(int idx) {
    focused = idx;
    dirty = 1;
}

int ui_open(int app, int w, int h) {
    int i = ui_find_win(app);
    if (i >= 0) {
        wins[i].min = 0;
        focus_win(i);
        return i;
    }
    for (i = 0; i < 8; i++)
        if (!wins[i].used) break;
    if (i == 8) return -1;
    int dw, dh;
    app_default_size(app, &dw, &dh);
    if (w) { dw = w; dh = h; }
    if (dw > gfx_w() - 20) dw = gfx_w() - 20;
    if (dh > gfx_h() - DOCK_H - 40) dh = gfx_h() - DOCK_H - 40;
    wins[i].used = 1;
    wins[i].app = app;
    wins[i].w = dw;
    wins[i].h = dh;
    wins[i].x = 50 + (win_serial * 26) % (gfx_w() - dw - 80 > 0 ? gfx_w() - dw - 80 : 1);
    wins[i].y = 36 + (win_serial * 22) % (gfx_h() - dh - DOCK_H - 60 > 0 ? gfx_h() - dh - DOCK_H - 60 : 1);
    wins[i].min = 0;
    wins[i].maxed = 0;
    strcpy(wins[i].title, app_name(app), sizeof(wins[i].title));
    win_serial++;
    focus_win(i);
    app_open(app);
    return i;
}

void ui_close(int idx) {
    app_close_cleanup(wins[idx].app);
    wins[idx].used = 0;
    if (focused == idx) focused = -1;
    for (int j = 7; j >= 0; j--)
        if (wins[j].used && !wins[j].min) { focus_win(j); break; }
    dirty = 1;
}

void ui_close_app(int app) {
    int i = ui_find_win(app);
    if (i >= 0) ui_close(i);
}

void ui_in_window_content(int idx, int *x, int *y) {
    *x = mx - (wins[idx].x + 1);
    *y = my - (wins[idx].y + 21);
}

/* ---- геометрия дока ---- */
#define DOCK_ICONS 7
static const int dock_apps[DOCK_ICONS] = { AP_FINDER, AP_TERMINAL, AP_NOTES, AP_CALENDAR, AP_MUSIC, AP_CALC, AP_SETTINGS };
static int dock_rect(int i, int *x, int *y, int *s) {
    int avail = gfx_w() - 24;
    int n = DOCK_ICONS;
    int cell = avail / n < 56 ? avail / n : 56;
    int total = cell * n;
    int x0 = (gfx_w() - total) / 2;
    int dist = mx - (x0 + i * cell + cell / 2);
    int d = dist < 0 ? -dist : dist;
    *s = 36 + (d < 90 ? (54 - 36) * (90 - d) / 90 : 0);
    *x = x0 + i * cell + cell / 2 - *s / 2;
    *y = gfx_h() - (*s) - 9;
    return mx >= x0 && mx < x0 + total && my >= gfx_h() - DOCK_H;
}

/* ---- клики ---- */
static void dock_click(void) {
    for (int i = 0; i < DOCK_ICONS; i++) {
        int x, y, s;
        dock_rect(i, &x, &y, &s);
        if (mx >= x - 4 && mx < x + s + 4 && my >= y - 4 && my < y + s + 4) {
            int app = dock_apps[i];
            int wi = ui_find_win(app);
            if (wi >= 0) {
                if (!wins[wi].min && focused == wi) wins[wi].min = 1;
                else { wins[wi].min = 0; focus_win(wi); }
            } else app_launch(app);
            dirty = 1;
            return;
        }
    }
}

static void menu_click(void) {
    for (int i = 0; i < MENU_N; i++) {
        if (!menu_items[i][0]) continue;
        int y = MENUBAR_H + 4 + i * MENU_IH;
        if (my >= y && my < y + MENU_IH && mx >= 4 && mx < 4 + MENU_W) {
            menu_open = 0;
            switch (i) {
            case 0: app_launch(AP_ABOUT); break;
            case 1: app_launch(AP_TERMINAL); break;
            case 2: app_launch(AP_SETTINGS); break;
            case 4: hw_reboot(); break;
            case 5:
                shutting_down = 1;
                break;
            }
            dirty = 1;
            return;
        }
    }
    menu_open = 0;
    dirty = 1;
}

static void win_click(int i) {
    struct win *w = &wins[i];
    focus_win(i);
    /* светофор */
    if (my < w->y + 20 && my >= w->y && mx >= w->x + 4 && mx < w->x + 44) {
        int b = (mx - w->x - 4) / 12;
        if (b == 0) { ui_close(i); return; }
        if (b == 1) { w->min = 1; dirty = 1; return; }
        if (b == 2) {
            if (w->maxed) {
                w->x = w->px; w->y = w->py; w->w = w->pw; w->h = w->ph;
                w->maxed = 0;
            } else {
                w->px = w->x; w->py = w->y; w->pw = w->w; w->ph = w->h;
                w->x = 0; w->y = MENUBAR_H;
                w->w = gfx_w(); w->h = gfx_h() - MENUBAR_H - DOCK_H;
                w->maxed = 1;
            }
            dirty = 1;
            return;
        }
    }
    if (my >= w->y + 21) {
        int cxx, cyy;
        ui_in_window_content(i, &cxx, &cyy);
        /* угол ресайза */
        if (mx >= w->x + w->w - 12 && my >= w->y + w->h - 12) {
            resize = i;
            rs_w = mx - (w->x + w->w);
            rs_h = my - (w->y + w->h);
            return;
        }
        app_click(w->app, cxx, cyy, w->w - 2, w->h - 22);
    }
}

static void handle_press(void) {
    if (my < MENUBAR_H) {
        if (mx < 90) { menu_open = !menu_open; dirty = 1; }
        else if (menu_open) { menu_open = 0; dirty = 1; }
        return;
    }
    if (menu_open) {
        menu_click();
        return;
    }
    if (my >= gfx_h() - DOCK_H) { dock_click(); return; }
    /* окна: сверху вниз от фокуса */
    int order[8], n = 0;
    if (focused >= 0 && wins[focused].used && !wins[focused].min) order[n++] = focused;
    for (int j = 7; j >= 0; j--)
        if (wins[j].used && !wins[j].min && j != focused) order[n++] = j;
    for (int k = 0; k < n; k++) {
        int i = order[k];
        struct win *w = &wins[i];
        if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
            if (my < w->y + 21) {
                drag = i;
                drag_dx = mx - w->x;
                drag_dy = my - w->y;
                if (w->maxed) { /* стащить из максимизации */
                    w->maxed = 0;
                    w->x = mx - w->pw / 2;
                    w->y = my - 10;
                    w->w = w->pw; w->h = w->ph;
                    drag_dx = w->pw / 2;
                    drag_dy = 10;
                }
            }
            win_click(i);
            return;
        }
    }
}

/* ---- кадр ---- */
void ui_frame(void) {
    int code, ascii;
    while (kbd_pop(&code, &ascii)) {
        dirty = 1;
        if (shutting_down) continue;
        if (code == K_ESC) { menu_open = 0; continue; }
        if (focused >= 0 && !wins[focused].min)
            app_key(wins[focused].app, code, ascii);
    }
    {
        int b, dx, dy;
        mouse_get(&b, &dx, &dy);
        if (dx || dy) {
            mx += dx; my += dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= gfx_w()) mx = gfx_w() - 1;
            if (my >= gfx_h()) my = gfx_h() - 1;
            dirty = 1;
        }
        {
            int prev = mbtn;
            mbtn = b;
            mouse_consume();
            if ((mbtn & 1) && !(prev & 1)) handle_press();
            if (!(mbtn & 1) && (prev & 1)) { drag = -1; resize = -1; }
        }
        if ((mbtn & 1)) {
            if (drag >= 0) {
                struct win *w = &wins[drag];
                w->x = mx - drag_dx;
                w->y = my - drag_dy;
                if (w->x < -w->w + 60) w->x = -w->w + 60;
                if (w->x > gfx_w() - 60) w->x = gfx_w() - 60;
                if (w->y < MENUBAR_H) w->y = MENUBAR_H;
                if (w->y > gfx_h() - 30) w->y = gfx_h() - 30;
                dirty = 1;
            }
            if (resize >= 0) {
                struct win *w = &wins[resize];
                int nw = mx - rs_w - w->x, nh = my - rs_h - w->y;
                if (nw < 190) nw = 190;
                if (nh < 140) nh = 140;
                w->w = nw; w->h = nh;
                dirty = 1;
            }
        }
    }
    if (dirty) {
        ui_redraw();
        dirty = 0;
    }
}

/* ---- отрисовка ---- */
static void draw_cursor(void) {
    static const char *rows[] = {
        "X........", "XX.......", "X#X......", "X##X.....", "X###X....",
        "X####X...", "X#####X..", "X######X.", "X###XXXX.", "X#X#X....", "XX.X#X...", "....X...."
    };
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 9; x++) {
            char c = rows[y][x];
            if (c == 'X') gfx_px(mx + x, my + y, 0x101010);
            else if (c == '#') gfx_px(mx + x, my + y, 0xffffff);
        }
}

static void draw_logo(int x, int y, int s) {
    gfx_round(x, y, s, s, s / 4, 0x6b5cff);
    int m = s / 2;
    int t = s >= 18 ? 2 : 1;
    for (int i = 0; i < t; i++) {
        gfx_fill(x + s / 4, y + s / 4 + i, m / 2, 1, 0xffffff);        /* левая палка Y */
        gfx_fill(x + m / 2 + s / 4, y + s / 4 + i, m / 2, 1, 0xffffff); /* правая */
    }
    int vx = x + s / 4 + m / 2 - t / 2;
    gfx_fill(vx, y + s / 4, t, s / 2, 0xffffff);
}

static void draw_menubar(void) {
    gfx_blend(0, 0, gfx_w(), MENUBAR_H, 0x000000, 110);
    gfx_fill(0, MENUBAR_H - 1, gfx_w(), 1, 0x000000);
    int tx = theme_text(g_theme);
    draw_logo(5, 3, 16);
    /* активное приложение */
    const char *an = "Finder";
    if (focused >= 0 && !wins[focused].min) an = app_name(wins[focused].app);
    gfx_text(26, 4, "YoroOS", tx, 0);
    gfx_text(82, 4, an, theme_accent(g_theme), 0);
    /* часы справа */
    int sec, min, hour, day, mon, year;
    rtc_get(&sec, &min, &hour, &day, &mon, &year);
    (void)sec;
    char buf[40];
    itoa(day, buf);
    scat(buf, ".");
    snum(buf, mon);
    scat(buf, " ");
    snum(buf, hour);
    scat(buf, ":");
    snum(buf, min);
    gfx_text(gfx_w() - gfx_text_w(buf, 0) - 10, 4, buf, tx, 0);
}

static void draw_menu_dropdown(void) {
    if (!menu_open) return;
    int tx = theme_text(g_theme);
    gfx_round(4, MENUBAR_H + 2, MENU_W, MENU_IH * MENU_N + 6, 6, 0x3a3a44);
    gfx_round(5, MENUBAR_H + 3, MENU_W - 2, MENU_IH * MENU_N + 4, 5, 0x101014);
    for (int i = 0; i < MENU_N; i++) {
        if (!menu_items[i][0]) continue;
        int y = MENUBAR_H + 4 + i * MENU_IH;
        int hover = my >= y && my < y + MENU_IH && mx >= 4 && mx < 4 + MENU_W;
        if (hover) gfx_blend(6, y, MENU_W - 4, MENU_IH, theme_accent(g_theme), 220);
        gfx_text(12, y + 2, menu_items[i], hover ? 0xffffff : tx, 0);
    }
}

static void draw_dock(void) {
    int avail = gfx_w() - 24;
    int cell = avail / DOCK_ICONS < 56 ? avail / DOCK_ICONS : 56;
    int total = cell * DOCK_ICONS;
    int x0 = (gfx_w() - total) / 2;
    gfx_round_blend(x0 - 6, gfx_h() - DOCK_H + 2, total + 12, DOCK_H - 4, 14, 0xffffff, 26);
    gfx_round_blend(x0 - 6, gfx_h() - DOCK_H + 2, total + 12, DOCK_H - 4, 14, 0x101018, 60);
    for (int i = 0; i < DOCK_ICONS; i++) {
        int x, y, s;
        dock_rect(i, &x, &y, &s);
        draw_app_icon(dock_apps[i], x, y, s);
        if (ui_find_win(dock_apps[i]) >= 0)
            gfx_fill(x + s / 2 - 2, gfx_h() - 6, 4, 2, 0xffffff);
    }
}

void ui_redraw(void) {
    gfx_copy_wall();
    gfx_set_clip(0, 0, gfx_w(), gfx_h());
    draw_menubar();
    /* окна: не-фокусированные, затем фокус */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 8; i++) {
            if (!wins[i].used || wins[i].min) continue;
            int is_f = (i == focused);
            if ((pass == 1) != is_f) continue;
            struct win *w = &wins[i];
            gfx_blend(w->x + 4, w->y + 5, w->w, w->h, 0x000000, 60); /* тень */
            gfx_round(w->x, w->y, w->w, w->h, 8, is_f ? theme_accent(g_theme) : 0x30333a);
            gfx_round(w->x + 1, w->y + 1, w->w - 2, w->h - 2, 7, theme_winbg(g_theme));
            gfx_blend(w->x + 1, w->y + 20, w->w - 2, 1, 0xffffff, 30);
            /* светофор */
            u32 dotc[3] = { 0xff5f57, 0xfebc2e, 0x28c840 };
            for (int b = 0; b < 3; b++) {
                int bx = w->x + 6 + b * 12;
                for (int yy = -3; yy <= 3; yy++)
                    for (int xx = -3; xx <= 3; xx++)
                        if (xx * xx + yy * yy <= 9) gfx_px(bx + 4 + xx, w->y + 10 + yy, dotc[b]);
            }
            gfx_text_c(w->x + w->w / 2, w->y + 4, w->title, is_f ? theme_text(g_theme) : 0x8a8f9a, 0);
            /* контент */
            gfx_set_clip(w->x + 1, w->y + 21, w->w - 2, w->h - 22);
            app_draw(w->app, w->x + 1, w->y + 21, w->w - 2, w->h - 22);
            gfx_set_clip(0, 0, gfx_w(), gfx_h());
        }
    draw_menu_dropdown();
    draw_dock();
    if (shutting_down) {
        gfx_fill(0, 0, gfx_w(), gfx_h(), 0x000000);
        gfx_text_c(gfx_w() / 2, gfx_h() / 2 - 8, "Работа YoroOS завершена.", 0x888899, 0);
        gfx_text_c(gfx_w() / 2, gfx_h() / 2 + 8, "Питание можно отключить (закрыть QEMU).", 0x555566, 0);
    }
    if (!shutting_down) draw_cursor();
    gfx_blit();
}

void ui_anim_tick(void) {
    if (shutting_down) return;
    app_anim();
    static u32 last = 0;
    if (pit_ticks - last >= 25) {
        last = pit_ticks;
        dirty = 1;
    }
}

void ui_init(void) {
    for (int i = 0; i < 8; i++) wins[i].used = 0;
    focused = -1;
    dirty = 1;
}
