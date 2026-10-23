/* YoroOS 0.2 - Windows 10/11 Compositing Window Manager with Aero Snap & Acrylic */
#include "kernel.h"

#define TITLE_H  34
#define TASKBAR_H 46

static Window g_wins[MAX_WINDOWS];
static int    g_order[MAX_WINDOWS]; /* z-order bottom..top */
static int    g_norder = 0;
static int    g_next_id = 1;
int wm_dirty = 1;

static Window *g_drag_win = NULL;
static int g_drag_mode = 0; /* 0 none, 1 move, 2 resize-br */
static int g_drag_ox = 0, g_drag_oy = 0;
static u32 g_last_title_click = 0;

int wm_taskbar_height(void) { return TASKBAR_H; }
void wm_invalidate(void) { wm_dirty = 1; }
int wm_window_count(void) { return g_norder; }

Window *wm_window_at_order(int i) {
    if (i < 0 || i >= g_norder) return NULL;
    return &g_wins[g_order[i]];
}

Window *wm_window_by_id(int id) {
    int i;
    for (i = 0; i < g_norder; i++) {
        Window *w = &g_wins[g_order[i]];
        if (w->used && w->id == id) return w;
    }
    return NULL;
}

Window *wm_focused(void) {
    int i;
    for (i = g_norder - 1; i >= 0; i--) {
        Window *w = &g_wins[g_order[i]];
        if (w->used && w->visible && !w->minimized) return w;
    }
    return NULL;
}

void wm_focus(Window *w) {
    int idx = -1, i, slot;
    if (!w || !w->used) return;
    slot = (int)(w - g_wins);
    for (i = 0; i < g_norder; i++) if (g_order[i] == slot) { idx = i; break; }
    if (idx < 0) return;
    for (i = idx; i < g_norder - 1; i++) g_order[i] = g_order[i + 1];
    g_order[g_norder - 1] = slot;
    w->minimized = 0;
    wm_dirty = 1;
}

Rect wm_client(Window *w) {
    if (!w) return (Rect){0, 0, 0, 0};
    if (w->frameless) return w->frame;
    return (Rect){w->frame.x + 1, w->frame.y + TITLE_H, w->frame.w - 2, w->frame.h - TITLE_H - 1};
}

void wm_init(void) {
    memset(g_wins, 0, sizeof(g_wins));
    g_norder = 0;
    g_drag_win = NULL;
    g_drag_mode = 0;
    wm_dirty = 1;
}

Window *wm_open(App *app, const char *title, int x, int y, int w, int h,
                DrawFn draw, EventFn event, CloseFn close) {
    int slot = -1, i;
    Window *win;
    int maxh = (int)fb.height - TASKBAR_H - 8;

    for (i = 0; i < MAX_WINDOWS; i++) if (!g_wins[i].used) { slot = i; break; }
    if (slot < 0) return NULL;

    if (w > (int)fb.width - 20) w = (int)fb.width - 20;
    if (h > maxh) h = maxh;
    if (x < 0 || y < 0) {
        x = ((int)fb.width - w) / 2 + (g_norder % 5) * 22;
        y = (maxh - h) / 2 + (g_norder % 5) * 18;
    }
    if (x + w > (int)fb.width) x = (int)fb.width - w - 8;
    if (y + h > maxh) y = maxh - h;
    if (x < 6) x = 6;
    if (y < 6) y = 6;

    win = &g_wins[slot];
    memset(win, 0, sizeof(*win));
    win->used = 1;
    win->id = g_next_id++;
    strncpy(win->title, title ? title : "Окно", sizeof(win->title) - 1);
    win->frame = (Rect){x, y, w, h};
    win->restore = win->frame;
    win->min_w = 340;
    win->min_h = 220;
    win->visible = 1;
    win->icon = app ? app->icon : ICON_APP;
    win->app = app;
    win->draw = draw;
    win->event = event;
    win->close = close;

    g_order[g_norder++] = slot;
    wm_dirty = 1;
    return win;
}

void wm_close(Window *w) {
    int slot, i, idx = -1;
    if (!w || !w->used) return;
    slot = (int)(w - g_wins);
    if (w->close) w->close(w);
    if (g_drag_win == w) { g_drag_win = NULL; g_drag_mode = 0; }
    w->used = 0;
    for (i = 0; i < g_norder; i++) if (g_order[i] == slot) { idx = i; break; }
    if (idx >= 0) {
        for (i = idx; i < g_norder - 1; i++) g_order[i] = g_order[i + 1];
        g_norder--;
    }
    wm_dirty = 1;
}

void wm_close_all(void) {
    while (g_norder > 0) wm_close(&g_wins[g_order[g_norder - 1]]);
}

Window *wm_at(int x, int y) {
    int i;
    for (i = g_norder - 1; i >= 0; i--) {
        Window *w = &g_wins[g_order[i]];
        if (!w->used || !w->visible || w->minimized) continue;
        if (pt_in(x, y, w->frame)) return w;
    }
    return NULL;
}

static void toggle_maximize(Window *w) {
    if (!w) return;
    if (w->maximized) {
        w->frame = w->restore;
        w->maximized = 0;
    } else {
        w->restore = w->frame;
        w->frame = (Rect){0, 0, (int)fb.width, (int)fb.height - TASKBAR_H};
        w->maximized = 1;
    }
    wm_dirty = 1;
}

void wm_alt_tab(void) {
    int slot;
    if (g_norder < 2) return;
    slot = g_order[g_norder - 1];
    memmove(&g_order[1], &g_order[0], (size_t)(g_norder - 1) * sizeof(int));
    g_order[0] = slot;
    g_wins[g_order[g_norder - 1]].minimized = 0;
    wm_dirty = 1;
}

static void draw_one_window(Window *w, int focused, int mx, int my) {
    Rect f = w->frame;
    Rect cl = wm_client(w);
    int bw = 44;
    Rect rc = {f.x + f.w - bw,     f.y + 1, bw - 1, TITLE_H - 1};
    Rect rx = {f.x + f.w - bw * 2, f.y + 1, bw,     TITLE_H - 1};
    Rect rm = {f.x + f.w - bw * 3, f.y + 1, bw,     TITLE_H - 1};

    if (!w->maximized) draw_shadow(f, focused ? 10 : 5);
    fill_round_rect(f, w->maximized ? 0 : 8, theme->win_bg);

    if (!w->frameless) {
        Color tb = focused ? theme->win_head : RGBA(theme->win_head.r, theme->win_head.g, theme->win_head.b, 230);
        fill_round_rect((Rect){f.x, f.y, f.w, TITLE_H}, w->maximized ? 0 : 8, tb);
        fill_rect((Rect){f.x, f.y + TITLE_H - 8, f.w, 8}, tb);
        if (focused)
            hline(f.x + 8, f.x + f.w - 8, f.y, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 140));
        icon_draw(w->icon, f.x + 10, f.y + 7, 20);
        draw_text_clipped(f.x + 36, f.y + (TITLE_H - font_line_height(FACE_UI)) / 2,
                          f.w - bw * 3 - 44, w->title,
                          focused ? theme->text_main : theme->text_muted);
        draw_win_buttons(f, pt_in(mx, my, rc), pt_in(mx, my, rm), pt_in(mx, my, rx));
        hline(f.x, f.x + f.w - 1, f.y + TITLE_H - 1, RGBA(130, 145, 170, 50));
    }

    if (w->draw) {
        gfx_set_clip(cl);
        w->draw(w, cl);
        gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
    }

    /* Resize grip on bottom-right corner */
    if (!w->maximized && !w->frameless) {
        draw_line(f.x + f.w - 11, f.y + f.h - 3, f.x + f.w - 3, f.y + f.h - 11, RGBA(150, 165, 190, 120));
        draw_line(f.x + f.w - 7,  f.y + f.h - 3, f.x + f.w - 3, f.y + f.h - 7,  RGBA(150, 165, 190, 120));
    }
    draw_round_rect(f, w->maximized ? 0 : 8,
                    focused ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 165)
                            : RGBA(120, 135, 160, 75));
}

void wm_frame(void) {
    int mx = mouse_get_x(), my = mouse_get_y();
    Window *top;
    int i;

    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});

    if (system_screen_mode == SCREEN_SETUP) {
        installer_draw_fullscreen();
        toast_draw();
        fx_draw_cursor(mx, my, mouse_state() & 1);
        gfx_present();
        wm_dirty = 0;
        return;
    }

    if (system_screen_mode == SCREEN_LOCK) {
        lockscreen_draw();
        fx_draw_cursor(mx, my, mouse_state() & 1);
        gfx_present();
        wm_dirty = 0;
        return;
    }

    desktop_draw();
    top = wm_focused();
    for (i = 0; i < g_norder; i++) {
        Window *w = &g_wins[g_order[i]];
        if (!w->used || !w->visible || w->minimized) continue;
        draw_one_window(w, w == top, mx, my);
    }

    /* Windows Aero Snap translucent preview ghost */
    if (g_drag_win && g_drag_mode == 1) {
        if (my <= 4)
            fill_round_rect_alpha((Rect){6, 6, (int)fb.width - 12, (int)fb.height - TASKBAR_H - 12}, 8,
                                  RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 70));
        else if (mx <= 6)
            fill_round_rect_alpha((Rect){6, 6, (int)fb.width / 2 - 9, (int)fb.height - TASKBAR_H - 12}, 8,
                                  RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 70));
        else if (mx >= (int)fb.width - 7)
            fill_round_rect_alpha((Rect){(int)fb.width / 2 + 3, 6, (int)fb.width / 2 - 9, (int)fb.height - TASKBAR_H - 12}, 8,
                                  RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 70));
    }

    desktop_draw_overlay();
    toast_draw();
    fx_draw_cursor(mx, my, mouse_state() & 1);
    gfx_present();
    wm_dirty = 0;
}

void wm_tick(void) {
    int i;
    YoroEvent te;
    memset(&te, 0, sizeof(te));
    te.type = EV_TICK;
    for (i = 0; i < g_norder; i++) {
        Window *w = &g_wins[g_order[i]];
        if (w->used && w->visible && !w->minimized && w->event)
            w->event(w, &te);
    }
}

void wm_handle_event(YoroEvent *e) {
    Window *w;

    if (system_screen_mode == SCREEN_SETUP) {
        if (e->type == EV_MOUSE_MOVE) wm_dirty = 1;
        installer_event_fullscreen(e);
        return;
    }
    if (system_screen_mode == SCREEN_LOCK) {
        if (e->type == EV_MOUSE_MOVE) wm_dirty = 1;
        lockscreen_event(e);
        return;
    }

    if (e->type == EV_MOUSE_MOVE) wm_dirty = 1;

    if (e->type == EV_KEY) {
        if (e->key == KEY_WIN || (e->ctrl && e->key == KEY_ESC)) { startmenu_toggle(); return; }
        if (e->alt && e->key == KEY_TAB) { wm_alt_tab(); return; }
        if (e->alt && e->key == KEY_F4)  { if (wm_focused()) wm_close(wm_focused()); return; }
    }

    /* Active window dragging / resizing */
    if (g_drag_win && g_drag_mode != 0) {
        if (e->type == EV_MOUSE_MOVE) {
            if (g_drag_mode == 1) {
                if (g_drag_win->maximized) {
                    int rw = g_drag_win->restore.w;
                    g_drag_win->maximized = 0;
                    g_drag_win->frame.w = rw;
                    g_drag_win->frame.h = g_drag_win->restore.h;
                    g_drag_ox = rw / 2;
                }
                g_drag_win->frame.x = e->x - g_drag_ox;
                g_drag_win->frame.y = e->y - g_drag_oy;
                if (g_drag_win->frame.y < 0) g_drag_win->frame.y = 0;
            } else if (g_drag_mode == 2) {
                int nw = e->x - g_drag_win->frame.x;
                int nh = e->y - g_drag_win->frame.y;
                if (nw < g_drag_win->min_w) nw = g_drag_win->min_w;
                if (nh < g_drag_win->min_h) nh = g_drag_win->min_h;
                g_drag_win->frame.w = nw;
                g_drag_win->frame.h = nh;
            }
            wm_dirty = 1;
            return;
        }
        if (e->type == EV_MOUSE_UP && e->btn == 1) {
            if (g_drag_mode == 1) {
                /* Aero Snap: top -> maximize, left -> left half, right -> right half */
                if (e->y <= 4) toggle_maximize(g_drag_win);
                else if (e->x <= 6) {
                    g_drag_win->restore = g_drag_win->frame;
                    g_drag_win->frame = (Rect){0, 0, (int)fb.width / 2, (int)fb.height - TASKBAR_H};
                } else if (e->x >= (int)fb.width - 7) {
                    g_drag_win->restore = g_drag_win->frame;
                    g_drag_win->frame = (Rect){(int)fb.width / 2, 0, (int)fb.width / 2, (int)fb.height - TASKBAR_H};
                }
            }
            g_drag_win = NULL;
            g_drag_mode = 0;
            wm_dirty = 1;
            return;
        }
    }

    /* Overlay panels (Start Menu, Context Menu, Taskbar) get priority cleanly */
    if (desktop_event(e)) return;

    if (e->type == EV_MOUSE_DOWN || e->type == EV_MOUSE_UP || e->type == EV_MOUSE_MOVE || e->type == EV_WHEEL) {
        w = wm_at(e->x, e->y);
        if (!w) return;
        if (e->type == EV_MOUSE_DOWN) wm_focus(w);

        if (e->type == EV_MOUSE_DOWN && e->btn == 1 && !w->frameless) {
            Rect f = w->frame;
            int bw = 44;
            Rect rc = {f.x + f.w - bw,     f.y + 1, bw - 1, TITLE_H - 1};
            Rect rx = {f.x + f.w - bw * 2, f.y + 1, bw,     TITLE_H - 1};
            Rect rm = {f.x + f.w - bw * 3, f.y + 1, bw,     TITLE_H - 1};
            if (pt_in(e->x, e->y, rc)) { wm_close(w); return; }
            if (pt_in(e->x, e->y, rx)) { toggle_maximize(w); return; }
            if (pt_in(e->x, e->y, rm)) { w->minimized = 1; wm_dirty = 1; return; }
            if (!w->maximized && e->x >= f.x + f.w - 16 && e->y >= f.y + f.h - 16) {
                g_drag_win = w; g_drag_mode = 2; return;
            }
            if (e->y < f.y + TITLE_H) {
                u32 now = sys_ticks();
                if (now - g_last_title_click < 35) { g_last_title_click = 0; toggle_maximize(w); return; }
                g_last_title_click = now;
                g_drag_win = w; g_drag_mode = 1;
                g_drag_ox = e->x - f.x; g_drag_oy = e->y - f.y;
                return;
            }
        }
        if (w->event) w->event(w, e);
        return;
    }

    if (e->type == EV_KEY) {
        w = wm_focused();
        if (w && w->event) w->event(w, e);
    }
}
