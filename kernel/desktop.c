/* YoroOS - desktop, taskbar and start menu */
#include "kernel.h"

#define TASKBAR 46
#define ICON_W 92
#define ICON_H 100

typedef struct {
    const char *id;
    const char *label;
    u32 icon;
    int kind;        /* 0 app, 1 computer, 2 trash */
} DeskIcon;

static DeskIcon icons[] = {
    { "computer", "Этот компьютер", ICON_COMPUTER, 1 },
    { "explorer", "Проводник",      ICON_FOLDER,   0 },
    { "terminal", "Терминал",       ICON_TERMINAL, 0 },
    { "browser",  "YoroNet",        ICON_BROWSER,  0 },
    { "store",    "Магазин",        ICON_STORE,    0 },
    { "settings", "Настройки",      ICON_SETTINGS, 0 },
    { "installer","Установить YoroOS", ICON_SETUP, 0 },
    { "trash",    "Корзина",        ICON_TRASH,    2 }
};
#define NICONS ((int)(sizeof(icons) / sizeof(icons[0])))

int desktop_selected = -1;
static int menu_open = 0;
static int power_open = 0;
static int menu_scroll = 0;
static int last_click_icon = -1;
static u32 last_click_tick = 0;
static char clock_time[8] = "00:00";
static char clock_date[24] = "";
static u32 last_clock_tick = 0;

int startmenu_open(void) { return menu_open; }
void startmenu_toggle(void) { menu_open = !menu_open; power_open = 0; wm_invalidate(); }

void desktop_init(void) {
    menu_open = 0;
    power_open = 0;
    desktop_selected = -1;
}

void desktop_rebuild(void) { wm_invalidate(); }

static Rect icon_rect(int idx) {
    Rect r;
    r.x = 16;
    r.y = 16 + idx * ICON_H;
    r.w = ICON_W;
    r.h = ICON_H - 12;
    return r;
}

static void label_centered(const char *text, int cx, int y, Rect clip) {
    int tw = text_width(text);
    draw_text_clip(cx - tw / 2 + 1, y + 1, text, RGBA(0, 0, 0, 160), clip);
    draw_text_clip(cx - tw / 2, y, text, RGB(250, 250, 253), clip);
}

static void draw_icon(int idx, int selected, int hover) {
    Rect r = icon_rect(idx);
    Rect box = {r.x + (r.w - 52) / 2, r.y + 4, 52, 52};
    const char *lbl = icons[idx].label;
    Rect label = {r.x + r.w / 2 - 64, r.y + 60, 128, 40};
    if (selected || hover)
        fill_round_rect_alpha((Rect){box.x - 4, box.y - 4, box.w + 8, box.h + 8}, 6,
                        selected ? RGBA(255, 255, 255, 60) : RGBA(255, 255, 255, 26));
    draw_shadow(box, 3);
    icon_draw(icons[idx].icon, box.x + 4, box.y + 4, 44);
    {
        int cx = r.x + r.w / 2;
        int tw = text_width(lbl);
        if (tw + 4 <= label.w) {
            label_centered(lbl, cx, label.y + 2, label);
        } else {
            /* split into two balanced lines on the space closest to the middle */
            const char *sp = NULL, *p;
            int best = 1 << 30;
            for (p = lbl; *p; p++) {
                if (*p == ' ') {
                    int left = text_width_n(lbl, (int)(p - lbl));
                    int d = left > tw - left ? left - (tw - left) : (tw - left) - left;
                    if (d < best) { best = d; sp = p; }
                }
            }
            if (sp) {
                char l1[80], l2[80];
                int n1 = (int)(sp - lbl), n2 = (int)strlen(sp + 1);
                if (n1 > 79) n1 = 79;
                if (n2 > 79) n2 = 79;
                memcpy(l1, lbl, (size_t)n1); l1[n1] = 0;
                memcpy(l2, sp + 1, (size_t)n2); l2[n2] = 0;
                label_centered(l1, cx, label.y, label);
                label_centered(l2, cx, label.y + 18, label);
            } else {
                label_centered(lbl, cx, label.y + 2, label);
            }
        }
    }
}

void desktop_draw(void) {
    int i, mx = mouse_get_x(), my = mouse_get_y();
    for (i = 0; i < NICONS; i++) {
        Rect r = icon_rect(i);
        int hover = pt_in(mx, my, r) && my < (int)fb.height - TASKBAR;
        draw_icon(i, desktop_selected == i, hover);
    }
}

static void draw_logo(int x, int y, int s) {
    int gap = s / 8;
    int q = (s - gap) / 2;
    fill_rect((Rect){x, y, q, q}, theme->accent);
    fill_rect((Rect){x + q + gap, y, q, q}, theme->accent);
    fill_rect((Rect){x, y + q + gap, q, q}, theme->accent);
    fill_rect((Rect){x + q + gap, y + q + gap, q, q}, theme->accent);
}

static void update_clock(void) {
    if (sys_ticks() - last_clock_tick < 50 && clock_date[0]) return;
    last_clock_tick = sys_ticks();
    {
        int h, m, s, d, mo, yy;
        rtc_read(&h, &m, &s, &d, &mo, &yy);
        sprintf(clock_time, "%02d:%02d", h, m);
        sprintf(clock_date, "%02d.%02d.%04d", d, mo, yy);
    }
}

static int taskbar_y(void) { return (int)fb.height - TASKBAR; }

static void draw_taskbar(void) {
    int y = taskbar_y();
    int i, mx = mouse_get_x(), my = mouse_get_y();
    Rect bar = {0, y, (int)fb.width, TASKBAR};
    fill_rect(bar, RGB(16, 24, 38));
    fill_rect((Rect){0, y, (int)fb.width, 1}, RGBA(255, 255, 255, 26));

    /* start button */
    {
        Rect sb = {0, y, 50, TASKBAR};
        int hov = pt_in(mx, my, sb);
        if (hov || menu_open) fill_rect(sb, RGBA(255, 255, 255, 20));
        draw_logo(sb.x + 14, sb.y + 14, 22);
    }
    /* search */
    {
        Rect sr = {58, y + 7, 210, 32};
        fill_round_rect_alpha(sr, 6, RGBA(255, 255, 255, 18));
        icon_draw(ICON_SEARCH, sr.x + 8, sr.y + 8, 16);
        draw_text(sr.x + 32, sr.y + (sr.h - FONT_H) / 2, "Поиск в YoroOS", RGB(180, 190, 205));
    }

    /* open windows */
    {
        int bx = 280;
        int n = wm_window_count();
        for (i = 0; i < n; i++) {
            Window *w = wm_window_at_order(i);
            Rect b = {bx, y + 4, 46, TASKBAR - 8};
            int hov = pt_in(mx, my, b);
            if (!w || !w->used) continue;
            if (hov) fill_round_rect_alpha(b, 5, RGBA(255, 255, 255, 24));
            if (w == wm_focused()) fill_round_rect((Rect){b.x + 6, b.y + b.h - 3, b.w - 12, 3}, 2, theme->accent);
            else if (w->minimized) fill_round_rect_alpha((Rect){b.x + 16, b.y + b.h - 3, b.w - 32, 3}, 2, RGBA(255, 255, 255, 90));
            icon_draw(w->icon, b.x + 8, b.y + 8, 30);
            bx += 50;
        }
    }

    /* tray */
    {
        int rx = (int)fb.width - 12;
        draw_text_right(rx, y + 6, clock_time, RGB(240, 244, 252));
        draw_text_right(rx, y + 24, clock_date, RGB(170, 180, 198));
        rx -= 78;
        {
            Rect bell = {rx - 18, y + 14, 18, 18};
            Rect vol = {rx - 46, y + 14, 18, 18};
            Rect wifi = {rx - 74, y + 14, 18, 18};
            icon_draw(ICON_WIFI, wifi.x, wifi.y, 18);
            icon_draw(ICON_VOLUME, vol.x, vol.y, 18);
            icon_draw(ICON_BELL, bell.x, bell.y, 18);
        }
    }
}

static int menu_rect_w(void) { return 588; }
static int menu_rect_h(void) { return 540; }
static Rect startmenu_rect(void) {
    Rect r;
    r.w = menu_rect_w();
    r.h = menu_rect_h();
    r.x = 8;
    r.y = taskbar_y() - r.h - 4;
    if (r.y < 4) { r.h += r.y - 4; r.y = 4; }
    return r;
}

static void draw_startmenu(void) {
    Rect m = startmenu_rect();
    int i, n = app_count();
    int mx = mouse_get_x(), my = mouse_get_y();
    int cols = 3, col, row;
    int list_y;
    draw_shadow(m, 14);
    fill_round_rect(m, 12, RGB(18, 26, 42));
    draw_round_rect(m, 12, RGBA(255, 255, 255, 30));

    /* left rail */
    {
        Rect rail = {m.x, m.y, 52, m.h};
        fill_round_rect_alpha(rail, 12, RGBA(255, 255, 255, 8));
        fill_rect((Rect){rail.x + 52, rail.y, 1, rail.h - 24}, RGBA(255, 255, 255, 18));
        {
            Rect ub = {rail.x + 10, rail.y + 12, 32, 32};
            int hov = pt_in(mx, my, ub);
            if (hov) fill_circle(ub.x + 16, ub.y + 16, 18, RGBA(255, 255, 255, 30));
            icon_draw(ICON_USER, ub.x, ub.y, 32);
        }
        {
            Rect pb = {rail.x + 10, m.y + m.h - 46, 32, 32};
            int hov = pt_in(mx, my, pb);
            if (hov || power_open) fill_circle(pb.x + 16, pb.y + 16, 18, RGBA(255, 90, 80, 60));
            icon_draw(ICON_POWER, pb.x, pb.y, 32);
        }
        {
            Rect sb = {rail.x + 10, m.y + m.h - 88, 32, 32};
            int hov = pt_in(mx, my, sb);
            if (hov) fill_circle(sb.x + 16, sb.y + 16, 18, RGBA(255, 255, 255, 30));
            icon_draw(ICON_SETTINGS, sb.x, sb.y, 32);
        }
    }

    /* header */
    draw_text(m.x + 74, m.y + 22, "YoroOS", C_WHITE);
    {
        Rect sr = {m.x + 74, m.y + 50, m.w - 96, 34};
        fill_round_rect_alpha(sr, 6, RGBA(255, 255, 255, 16));
        icon_draw(ICON_SEARCH, sr.x + 9, sr.y + 8, 18);
        draw_text(sr.x + 34, sr.y + (sr.h - FONT_H) / 2, "Найти приложение", RGB(176, 186, 202));
    }

    draw_text(m.x + 74, m.y + 100, "Закреплённые", RGB(190, 200, 216));
    list_y = m.y + 126;

    /* app tiles */
    {
        int idx = 0;
        Rect grid = {m.x + 64, list_y, m.w - 76, m.h - 150};
        gfx_set_clip(grid);
        for (i = 0; i < n; i++) {
            App *a = app_by_index(i);
            Rect tile;
            if (!a || !a->installed || a->pinned < 0) continue;
            col = idx % cols;
            row = idx / cols;
            tile.x = grid.x + col * 168;
            tile.y = grid.y + row * 62 - menu_scroll;
            tile.w = 158;
            tile.h = 54;
            if (tile.y + tile.h > grid.y && tile.y < grid.y + grid.h) {
                int hov = pt_in(mx, my, tile) && pt_in(mx, my, grid);
                if (hov) fill_round_rect_alpha(tile, 6, RGBA(255, 255, 255, 22));
                icon_draw(a->icon, tile.x + 8, tile.y + 9, 36);
                draw_text_clip(tile.x + 52, tile.y + (tile.h - FONT_H) / 2, a->name, RGB(238, 242, 250),
                               (Rect){tile.x + 52, tile.y, tile.w - 54, tile.h});
            }
            idx++;
        }
        gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
        {
            Rect track = {m.x + m.w - 12, grid.y, 6, grid.h - 10};
            int total = ((n / cols) + 1) * 62;
            ui_scrollbar(track, total, grid.h, menu_scroll);
        }
    }

    /* power menu */
    if (power_open) {
        Rect p = {m.x + 56, m.y + m.h - 126, 190, 96};
        draw_shadow(p, 8);
        fill_round_rect(p, 8, RGB(30, 38, 54));
        draw_round_rect(p, 8, RGBA(255, 255, 255, 30));
        {
            Rect it1 = {p.x + 6, p.y + 6, p.w - 12, 28};
            Rect it2 = {p.x + 6, p.y + 34, p.w - 12, 28};
            Rect it3 = {p.x + 6, p.y + 62, p.w - 12, 28};
            if (pt_in(mx, my, it1)) fill_round_rect_alpha(it1, 4, RGBA(255, 255, 255, 24));
            if (pt_in(mx, my, it2)) fill_round_rect_alpha(it2, 4, RGBA(255, 255, 255, 24));
            if (pt_in(mx, my, it3)) fill_round_rect_alpha(it3, 4, RGBA(255, 255, 255, 24));
            draw_text(it1.x + 10, it1.y + 5, "Завершение работы", RGB(240, 244, 252));
            draw_text(it2.x + 10, it2.y + 5, "Перезагрузка", RGB(240, 244, 252));
            draw_text(it3.x + 10, it3.y + 5, "Отмена", RGB(240, 244, 252));
        }
    }
}

void desktop_draw_overlay(void) {
    update_clock();
    if (menu_open) draw_startmenu();
    draw_taskbar();
    toast_draw();
}

void system_shutdown(void) {
    cli();
    {
        Rect full = {0, 0, (int)fb.width, (int)fb.height};
        gfx_set_clip(full);
        fill_grad_v(full, RGB(10, 26, 48), RGB(2, 6, 14));
        draw_text_center((int)fb.width / 2, (int)fb.height / 2 - 30, "Завершение работы YoroOS", RGB(230, 236, 248));
        draw_text_center((int)fb.width / 2, (int)fb.height / 2 + 6, "Теперь питание компьютера можно отключить.", RGB(150, 165, 190));
        gfx_present();
    }
    for (;;) hlt();
}

void system_reboot(void) {
    cli();
    {
        Rect full = {0, 0, (int)fb.width, (int)fb.height};
        gfx_set_clip(full);
        fill_rect(full, RGB(8, 14, 26));
        draw_text_center((int)fb.width / 2, (int)fb.height / 2, "Перезагрузка...", C_WHITE);
        gfx_present();
    }
    outb(0x64, 0xFE);
    for (;;) hlt();
}

/* ---------------- input ---------------- */
static void launch_icon(int idx) {
    DeskIcon *d = &icons[idx];
    if (d->kind == 1) { explorer_open(); }
    else if (d->kind == 2) { toast("Корзина пуста. Удалённые файлы исчезают сразу."); }
    else {
        App *a = app_find(d->id);
        if (a) app_launch(a);
    }
}

static int hit_startmenu(int x, int y) {
    Rect m = startmenu_rect();
    return menu_open && pt_in(x, y, m);
}

static int startmenu_event(YoroEvent *e) {
    Rect m = startmenu_rect();
    if (!menu_open) return 0;
    if (e->type == EV_MOUSE_DOWN && !pt_in(e->x, e->y, m)) {
        menu_open = 0;
        power_open = 0;
        wm_invalidate();
        return 0;   /* let the click continue to the desktop */
    }
    if (e->type == EV_WHEEL && pt_in(e->x, e->y, m)) {
        menu_scroll -= e->wheel * 46;
        {
            int n = 0, i;
            for (i = 0; i < app_count(); i++) {
                App *a = app_by_index(i);
                if (a && a->installed && a->pinned >= 0) n++;
            }
            {
                int content = ((n / 3) + 1) * 62;
                int view = menu_rect_h() - 150;
                int maxs = content - view;
                if (maxs < 0) maxs = 0;
                if (menu_scroll > maxs) menu_scroll = maxs;
                if (menu_scroll < 0) menu_scroll = 0;
            }
        }
        wm_invalidate();
        return 1;
    }
    if (e->type != EV_MOUSE_DOWN) {
        return pt_in(e->x, e->y, m);
    }

    /* power menu */
    if (power_open) {
        Rect p = {m.x + 56, m.y + m.h - 126, 190, 96};
        Rect it1 = {p.x + 6, p.y + 6, p.w - 12, 28};
        Rect it2 = {p.x + 6, p.y + 34, p.w - 12, 28};
        Rect it3 = {p.x + 6, p.y + 62, p.w - 12, 28};
        if (pt_in(e->x, e->y, it1)) { system_shutdown(); return 1; }
        if (pt_in(e->x, e->y, it2)) { system_reboot(); return 1; }
        if (pt_in(e->x, e->y, it3)) { power_open = 0; wm_invalidate(); return 1; }
    }

    /* power & settings buttons in the rail */
    {
        Rect pb = {m.x + 10, m.y + m.h - 46, 32, 32};
        Rect sb = {m.x + 10, m.y + m.h - 88, 32, 32};
        if (pt_in(e->x, e->y, pb)) { power_open = !power_open; wm_invalidate(); return 1; }
        if (pt_in(e->x, e->y, sb)) { menu_open = 0; settings_open(); wm_invalidate(); return 1; }
    }

    /* app tiles */
    {
        int i, idx = 0, n = app_count();
        Rect grid = {m.x + 64, m.y + 126, m.w - 76, m.h - 150};
        for (i = 0; i < n; i++) {
            App *a = app_by_index(i);
            Rect tile;
            int col, row;
            if (!a || !a->installed || a->pinned < 0) continue;
            col = idx % 3; row = idx / 3;
            tile.x = grid.x + col * 168;
            tile.y = grid.y + row * 62 - menu_scroll;
            tile.w = 158; tile.h = 54;
            if (pt_in(e->x, e->y, tile) && pt_in(e->x, e->y, grid)) {
                menu_open = 0;
                app_launch(a);
                return 1;
            }
            idx++;
        }
    }
    return 1;
}

int desktop_event(YoroEvent *e) {
    int ty = taskbar_y();
    int mx = e->x, my = e->y;

    if (startmenu_event(e)) return 1;

    if (e->type == EV_MOUSE_DOWN) {
        /* taskbar */
        if (my >= ty) {
            if (mx < 50) { menu_open = !menu_open; power_open = 0; wm_invalidate(); return 1; }
            if (mx >= 58 && mx < 268) { menu_open = 1; wm_invalidate(); return 1; }
            {
                int bx = 280, i, n = wm_window_count();
                for (i = 0; i < n; i++) {
                    Window *w = wm_window_at_order(i);
                    if (!w || !w->used) continue;
                    if (mx >= bx && mx < bx + 46) {
                        if (w == wm_focused() && !w->minimized) w->minimized = 1;
                        else wm_focus(w);
                        wm_invalidate();
                        return 1;
                    }
                    bx += 50;
                }
            }
            if (mx > (int)fb.width - 90) { menu_open = 0; wm_invalidate(); return 1; }
            return 1;
        }
        /* desktop icons */
        {
            int i;
            for (i = 0; i < NICONS; i++) {
                Rect r = icon_rect(i);
                if (pt_in(mx, my, r)) {
                    desktop_selected = i;
                    if (last_click_icon == i && sys_ticks() - last_click_tick < 45) {
                        launch_icon(i);
                        last_click_icon = -1;
                    } else {
                        last_click_icon = i;
                        last_click_tick = sys_ticks();
                    }
                    wm_invalidate();
                    return 1;
                }
            }
        }
        if (desktop_selected != -1) {
            desktop_selected = -1;
            wm_invalidate();
        }
    }
    if (e->type == EV_KEY && e->key == KEY_ESC && menu_open) {
        menu_open = 0;
        wm_invalidate();
        return 1;
    }
    return 0;
}
