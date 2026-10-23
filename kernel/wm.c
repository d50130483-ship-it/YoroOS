/* YoroOS - window manager, compositor and input routing */
#include "kernel.h"

#define TITLE_H 34
#define BORDER  1
#define TASKBAR_H 46

extern int mouse_get_x(void);
extern int mouse_get_y(void);

static Window wins[MAX_WINDOWS];
static Window *order[MAX_WINDOWS];
static int order_n = 0;
static int next_id = 1;
static Window *focused_w = NULL;
int wm_dirty = 1;

static Window *drag_w = NULL;
static int drag_mode = 0;      /* 1 move, 2 resize */
static int drag_dx = 0, drag_dy = 0;
static int drag_w0 = 0, drag_h0 = 0;

int wm_taskbar_height(void) { return TASKBAR_H; }
int wm_title_height(void) { return TITLE_H; }

Rect wm_client(Window *w) {
    Rect r;
    r.x = w->frame.x + BORDER;
    r.y = w->frame.y + TITLE_H;
    r.w = w->frame.w - 2 * BORDER;
    r.h = w->frame.h - TITLE_H - BORDER;
    return r;
}

void wm_init(void) {
    memset(wins, 0, sizeof(wins));
    order_n = 0;
    next_id = 1;
    focused_w = NULL;
    wm_dirty = 1;
}

void wm_invalidate(void) { wm_dirty = 1; }

Window *wm_open(App *app, const char *title, int x, int y, int w, int h,
                DrawFn draw, EventFn event, CloseFn close) {
    int i;
    Window *win = NULL;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].used) { win = &wins[i]; break; }
    }
    if (!win) return NULL;
    memset(win, 0, sizeof(Window));
    win->used = 1;
    win->id = next_id++;
    win->app = app;
    win->draw = draw;
    win->event = event;
    win->close = close;
    win->visible = 1;
    win->icon = app ? app->icon : ICON_APP;
    strncpy(win->title, title ? title : "", 63);
    win->title[63] = 0;
    win->min_w = 320;
    win->min_h = 200;

    if (w < 260) w = 260;
    if (h < 160) h = 160;
    if (w > (int)fb.width - 40) w = (int)fb.width - 40;
    if (h > (int)fb.height - TASKBAR_H - 40) h = (int)fb.height - TASKBAR_H - 40;
    if (x < 0) x = 60 + (order_n % 6) * 34;
    if (y < 0) y = 30 + (order_n % 6) * 28;
    if (x + w > (int)fb.width) x = (int)fb.width - w - 8;
    if (y + h > (int)fb.height - TASKBAR_H) y = (int)fb.height - TASKBAR_H - h - 8;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    win->frame.x = x;
    win->frame.y = y;
    win->frame.w = w;
    win->frame.h = h;
    win->restore = win->frame;

    /* top of z-order */
    order[order_n < MAX_WINDOWS ? order_n : MAX_WINDOWS - 1] = win;
    if (order_n < MAX_WINDOWS) order_n++;
    focused_w = win;
    wm_dirty = 1;
    return win;
}

void wm_close(Window *w) {
    int i, j;
    if (!w) return;
    if (w->close) w->close(w);
    for (i = 0; i < order_n; i++) {
        if (order[i] == w) {
            for (j = i; j < order_n - 1; j++) order[j] = order[j + 1];
            order_n--;
            break;
        }
    }
    memset(w, 0, sizeof(Window));
    if (focused_w == w) {
        focused_w = order_n ? order[order_n - 1] : NULL;
    }
    wm_dirty = 1;
}

void wm_focus(Window *w) {
    int i, j;
    if (!w || !w->used) return;
    focused_w = w;
    w->minimized = 0;
    for (i = 0; i < order_n; i++) {
        if (order[i] == w) {
            for (j = i; j < order_n - 1; j++) order[j] = order[j + 1];
            order[order_n - 1] = w;
            break;
        }
    }
    wm_dirty = 1;
}

Window *wm_focused(void) { return focused_w; }

Window *wm_at(int x, int y) {
    int i;
    for (i = order_n - 1; i >= 0; i--) {
        Window *w = order[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if (pt_in(x, y, w->frame)) return w;
    }
    return NULL;
}

Window *wm_window_by_id(int id) {
    int i;
    for (i = 0; i < MAX_WINDOWS; i++)
        if (wins[i].used && wins[i].id == id) return &wins[i];
    return NULL;
}

/* ---------------- drawing ---------------- */
static void win_button(Rect frame, int which, int hover, Color title_bg) {
    /* which: 0 minimize, 1 maximize, 2 close */
    Rect r;
    int x = frame.x + frame.w - 46 * (3 - which);
    r.x = x; r.y = frame.y; r.w = 46; r.h = TITLE_H;
    if (hover) {
        Color hc = which == 2 ? RGB(232, 17, 35) : RGBA(255, 255, 255, 30);
        if (which == 2) fill_rect(r, hc);
        else fill_rect_alpha(r, hc);
    } else {
        fill_rect(r, title_bg);
    }
    {
        Color fg = C_WHITE;
        int cx = r.x + 23, cy = r.y + TITLE_H / 2;
        if (which == 0) hline(cx - 5, cx + 5, cy + 1, fg);
        else if (which == 1) draw_rect((Rect){cx - 5, cy - 4, 10, 9}, fg);
        else {
            draw_line(cx - 5, cy - 4, cx + 5, cy + 4, fg);
            draw_line(cx - 5, cy + 4, cx + 5, cy - 4, fg);
            draw_line(cx - 4, cy - 4, cx + 6, cy + 4, fg);
            draw_line(cx - 4, cy + 4, cx + 6, cy - 4, fg);
        }
    }
}

static void draw_window(Window *w) {
    Rect f = w->frame;
    Rect body;
    Rect client;
    int active;
    Color title_bg;
    if (!w->visible || w->minimized) return;

    active = (w == focused_w);
    draw_shadow(f, 10);

    /* body */
    body = f;
    fill_round_rect(body, 9, RGB(13, 20, 34));
    title_bg = active ? RGB(28, 44, 72) : RGB(48, 52, 62);
    if (active) {
        fill_round_rect((Rect){f.x, f.y, f.w, TITLE_H + 9}, 9, title_bg);
        fill_rect((Rect){f.x, f.y + TITLE_H, f.w, 9}, title_bg);
    } else {
        fill_round_rect((Rect){f.x, f.y, f.w, TITLE_H + 9}, 9, title_bg);
        fill_rect((Rect){f.x, f.y + TITLE_H, f.w, 9}, title_bg);
    }
    if (active) {
        Color line = theme->accent;
        fill_rect_alpha((Rect){f.x + 8, f.y + TITLE_H - 1, f.w - 16, 1}, RGBA(line.r, line.g, line.b, 90));
    }

    /* client area */
    client = wm_client(w);
    fill_rect(client, RGB(248, 249, 252));
    gfx_set_clip(client);
    if (w->draw) w->draw(w, client);
    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});

    /* title bar content */
    icon_draw(w->icon, f.x + 10, f.y + (TITLE_H - 18) / 2, 18);
    draw_text_clip(f.x + 34, f.y + (TITLE_H - FONT_H) / 2, w->title,
                   active ? RGB(240, 244, 250) : RGB(214, 218, 226),
                   (Rect){f.x + 34, f.y, f.w - 34 - 140, TITLE_H});

    /* window buttons */
    {
        int mx = mouse_get_x(), my = mouse_get_y();
        int hov_close = active && mx >= f.x + f.w - 46 && my >= f.y && my < f.y + TITLE_H;
        int hov_max = active && mx >= f.x + f.w - 92 && mx < f.x + f.w - 46 && my >= f.y && my < f.y + TITLE_H;
        int hov_min = active && mx >= f.x + f.w - 138 && mx < f.x + f.w - 92 && my >= f.y && my < f.y + TITLE_H;
        win_button(f, 0, hov_min, title_bg);
        win_button(f, 1, hov_max, title_bg);
        win_button(f, 2, hov_close, title_bg);
    }

    /* 1px frame */
    draw_round_rect(f, 9, active ? RGB(58, 78, 110) : RGB(60, 64, 74));
    UNUSED(body);
}

static void draw_cursor(int mx, int my) {
    static const int ax[7] = {0, 0, 12, 4, 17, 12, 5};
    static const int ay[7] = {0, 18, 18, 20, 20, 8, 8};
    int xs[7], ys[7], i;
    for (i = 0; i < 7; i++) { xs[i] = mx + ax[i]; ys[i] = my + ay[i]; }
    fill_poly(xs, ys, 7, RGB(20, 20, 26));
    for (i = 0; i < 7; i++) { xs[i] = mx + ax[i] + 2; ys[i] = my + ay[i] + 2; }
    fill_poly(xs, ys, 5, RGB(250, 250, 252));
    /* keep black outline by redrawing the contour */
    {
        int ox[8], oy[8];
        int px_[7] = {0, 0, 12, 4, 17, 12, 5};
        int py_[7] = {0, 18, 18, 20, 20, 8, 8};
        for (i = 0; i < 7; i++) { ox[i] = mx + px_[i]; oy[i] = my + py_[i]; }
        for (i = 0; i < 7; i++) {
            int j = (i + 1) % 7;
            draw_line(ox[i], oy[i], ox[j], oy[j], RGB(20, 20, 26));
        }
    }
}

void wm_frame(void) {
    int i, mx, my;
    if (!wm_dirty) return;
    wm_dirty = 0;

    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
    theme_draw_wallpaper();
    desktop_draw();

    for (i = 0; i < order_n; i++) {
        Window *w = order[i];
        if (w->used && w->visible && !w->minimized) draw_window(w);
    }

    desktop_draw_overlay();

    mx = mouse_get_x();
    my = mouse_get_y();
    draw_cursor(mx, my);
    gfx_present();
}

/* ---------------- input ---------------- */
static void deliver(Window *w, YoroEvent *e) {
    YoroEvent l;
    Rect c;
    if (!w || !w->event) return;
    l = *e;
    c = wm_client(w);
    l.x = e->x - c.x;
    l.y = e->y - c.y;
    w->event(w, &l);
    wm_dirty = 1;
}

static void maximize_toggle(Window *w) {
    if (w->maximized) {
        w->frame = w->restore;
        w->maximized = 0;
    } else {
        w->restore = w->frame;
        w->frame.x = 0;
        w->frame.y = 0;
        w->frame.w = (int)fb.width;
        w->frame.h = (int)fb.height - TASKBAR_H;
        w->maximized = 1;
    }
    wm_dirty = 1;
}

void wm_alt_tab(void) {
    int i;
    for (i = 0; i < order_n; i++) {
        if (order[i] == focused_w) {
            if (i > 0) wm_focus(order[i - 1]);
            else if (order_n > 1) wm_focus(order[order_n - 1]);
            return;
        }
    }
}

int wm_window_count(void) { return order_n; }
Window *wm_window_at_order(int i) { return (i >= 0 && i < order_n) ? order[i] : NULL; }

void wm_handle_event(YoroEvent *e) {
    int mx, my;
    if (!e) return;
    mx = e->x;
    my = e->y;

    /* desktop (start menu, taskbar, icons) has priority when open/hit */
    if (desktop_event(e)) { wm_dirty = 1; return; }

    switch (e->type) {
    case EV_MOUSE_DOWN: {
        Window *w = wm_at(mx, my);
        if (!w) return;
        wm_focus(w);
        {
            Rect f = w->frame;
            if (my < f.y + TITLE_H) {
                if (mx >= f.x + f.w - 46) { wm_close(w); return; }
                if (mx >= f.x + f.w - 92) { maximize_toggle(w); return; }
                if (mx >= f.x + f.w - 138) { w->minimized = 1; wm_dirty = 1; return; }
                drag_w = w;
                drag_mode = 1;
                drag_dx = mx - f.x;
                drag_dy = my - f.y;
                return;
            }
            if (mx >= f.x + f.w - 18 && my >= f.y + f.h - 18) {
                drag_w = w;
                drag_mode = 2;
                drag_dx = mx;
                drag_dy = my;
                drag_w0 = f.w;
                drag_h0 = f.h;
                return;
            }
        }
        deliver(w, e);
        break;
    }
    case EV_MOUSE_MOVE: {
        if (drag_w && drag_mode == 1 && !drag_w->maximized) {
            drag_w->frame.x = mx - drag_dx;
            drag_w->frame.y = my - drag_dy;
            if (drag_w->frame.x < -(drag_w->frame.w - 80)) drag_w->frame.x = -(drag_w->frame.w - 80);
            if (drag_w->frame.y < 0) drag_w->frame.y = 0;
            if (drag_w->frame.y > (int)fb.height - TASKBAR_H - 30)
                drag_w->frame.y = (int)fb.height - TASKBAR_H - 30;
            wm_dirty = 1;
        } else if (drag_w && drag_mode == 2) {
            int nw = drag_w0 + (mx - drag_dx);
            int nh = drag_h0 + (my - drag_dy);
            if (nw < drag_w->min_w) nw = drag_w->min_w;
            if (nh < drag_w->min_h) nh = drag_w->min_h;
            drag_w->frame.w = nw;
            drag_w->frame.h = nh;
            wm_dirty = 1;
        }
        {
            Window *w = wm_at(mx, my);
            if (w) deliver(w, e);
        }
        break;
    }
    case EV_MOUSE_UP: {
        if (drag_w) { drag_w = NULL; drag_mode = 0; wm_dirty = 1; }
        {
            Window *w = wm_at(mx, my);
            if (w) deliver(w, e);
        }
        break;
    }
    case EV_WHEEL: {
        Window *w = wm_at(mx, my);
        if (w) deliver(w, e);
        break;
    }
    case EV_KEY: {
        if (e->alt && e->key == KEY_TAB) { wm_alt_tab(); return; }
        if (e->alt && e->key == KEY_F4 && focused_w) { wm_close(focused_w); return; }
        if (e->key == KEY_F1 && e->ctrl) { /* help */ }
        if (focused_w) deliver(focused_w, e);
        break;
    }
    default: {
        int i;
        for (i = 0; i < order_n; i++)
            if (order[i]->event) deliver(order[i], e);
        break;
    }
    }
}
