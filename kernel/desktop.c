/* YoroOS 0.2 - Windows 10/11 Desktop, Frosted Acrylic Taskbar, Start Menu, Lock Screen & Context Menu */
#include "kernel.h"

typedef struct {
    int  kind;   /* 0 = This PC, 1 = Recycle Bin, 2 = App shortcut, 3 = FS node on Desktop */
    App *app;
    int  node;
    u32  icon;
    char label[48];
} DeskItem;

#define MAX_DESK_ITEMS 48
static DeskItem g_items[MAX_DESK_ITEMS];
static int g_nitems = 0;
int desktop_selected = -1;
static u32 g_last_desk_click = 0;
static int g_last_desk_idx = -1;

/* Start Menu & Action Center state */
static int g_start_open = 0;
static int g_action_open = 0;
static char g_search[64] = "";
static int  g_search_len = 0, g_search_caret = 0;
static int  g_start_scroll = 0;

/* Universal Popup Context Menu */
#define MAX_CTX_ITEMS 10
static struct {
    int   open;
    Rect  box;
    char  labels[MAX_CTX_ITEMS][56];
    int   count;
    CtxMenuCb cb;
    void *ctx;
} g_ctx;

void ctx_menu_open(int x, int y, const char **labels, int count, CtxMenuCb cb, void *ctx) {
    int i, w = 236, h;
    if (count <= 0) return;
    if (count > MAX_CTX_ITEMS) count = MAX_CTX_ITEMS;
    h = count * 30 + 12;
    if (x + w > (int)fb.width - 8) x = (int)fb.width - w - 8;
    if (y + h > (int)fb.height - wm_taskbar_height() - 6)
        y = (int)fb.height - wm_taskbar_height() - h - 6;
    if (x < 6) x = 6;
    if (y < 6) y = 6;
    g_ctx.open = 1;
    g_ctx.box = (Rect){x, y, w, h};
    g_ctx.count = count;
    g_ctx.cb = cb;
    g_ctx.ctx = ctx;
    for (i = 0; i < count; i++) {
        strncpy(g_ctx.labels[i], labels[i], sizeof(g_ctx.labels[i]) - 1);
        g_ctx.labels[i][sizeof(g_ctx.labels[i]) - 1] = 0;
    }
    wm_invalidate();
}

int ctx_menu_active(void) { return g_ctx.open; }
void ctx_menu_close(void) { g_ctx.open = 0; wm_invalidate(); }

void desktop_rebuild(void) {
    int i, ddir, cnt;
    g_nitems = 0;

    /* 1. This PC */
    g_items[g_nitems].kind = 0;
    g_items[g_nitems].icon = ICON_COMPUTER;
    strcpy(g_items[g_nitems].label, "Этот компьютер");
    g_nitems++;

    /* 2. Recycle Bin */
    g_items[g_nitems].kind = 1;
    g_items[g_nitems].icon = fs_count(fs_trash_dir()) > 0 ? ICON_TRASH : ICON_TRASH_EMPTY;
    strcpy(g_items[g_nitems].label, "Корзина");
    g_nitems++;

    /* 3. Installed Desktop App Shortcuts */
    for (i = 0; i < app_count() && g_nitems < MAX_DESK_ITEMS; i++) {
        App *a = app_by_index(i);
        if (!a || !a->installed || !a->on_desktop) continue;
        g_items[g_nitems].kind = 2;
        g_items[g_nitems].app = a;
        g_items[g_nitems].icon = a->icon;
        strncpy(g_items[g_nitems].label, a->name, sizeof(g_items[g_nitems].label) - 1);
        g_nitems++;
    }

    /* 4. Real Files & Folders in C:\Users\<User>\Desktop */
    ddir = fs_desktop_dir();
    cnt = fs_count(ddir);
    for (i = 0; i < cnt && g_nitems < MAX_DESK_ITEMS; i++) {
        int nd = fs_child(ddir, i);
        if (nd < 0) continue;
        g_items[g_nitems].kind = 3;
        g_items[g_nitems].node = nd;
        g_items[g_nitems].icon = fs_is_dir(nd) ? ICON_FOLDER : fs_icon(nd);
        strncpy(g_items[g_nitems].label, fs_name(nd), sizeof(g_items[g_nitems].label) - 1);
        g_nitems++;
    }
}

void desktop_init(void) {
    g_start_open = 0;
    g_action_open = 0;
    g_ctx.open = 0;
    desktop_rebuild();
}

static Rect desk_cell_rect(int idx) {
    int rows = ((int)fb.height - wm_taskbar_height() - 24) / 92;
    int col, row;
    if (rows < 4) rows = 4;
    col = idx / rows;
    row = idx % rows;
    return (Rect){14 + col * 104, 14 + row * 92, 96, 86};
}

static void launch_desk_item(int idx) {
    DeskItem *it;
    if (idx < 0 || idx >= g_nitems) return;
    it = &g_items[idx];
    if (it->kind == 0) explorer_open_path("C:\\");
    else if (it->kind == 1) explorer_open_path("C:\\Recycle.Bin");
    else if (it->kind == 2 && it->app) app_launch(it->app);
    else if (it->kind == 3) fs_open_any_file(it->node);
}

static void fit_utf8_label(const char *src, int max_px, char *out, size_t cap) {
    size_t pos = 0;
    const char *p = src;
    out[0] = 0;
    if (text_width_face(FACE_SMALL, src) <= max_px) {
        strncpy(out, src, cap - 1);
        out[cap - 1] = 0;
        return;
    }
    while (*p && pos + 6 < cap) {
        int adv = 0;
        str_utf8_decode(p, &adv);
        if (adv <= 0) break;
        memcpy(out + pos, p, (size_t)adv);
        out[pos + adv] = '.';
        out[pos + adv + 1] = '.';
        out[pos + adv + 2] = 0;
        if (text_width_face(FACE_SMALL, out) > max_px) {
            out[pos] = '.';
            out[pos + 1] = '.';
            out[pos + 2] = 0;
            return;
        }
        pos += (size_t)adv;
        p += adv;
    }
}

void desktop_draw(void) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int i;
    theme_draw_wallpaper();

    /* Draw Windows 10/11 Desktop Icon Grid */
    for (i = 0; i < g_nitems; i++) {
        Rect cell = desk_cell_rect(i);
        int hov = pt_in(mx, my, cell);
        if (desktop_selected == i) {
            fill_round_rect_alpha(cell, 6, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 88));
            draw_round_rect(cell, 6, RGBA(255, 255, 255, 110));
        } else if (hov) {
            fill_round_rect_alpha(cell, 6, RGBA(255, 255, 255, 34));
        }
        icon_draw(g_items[i].icon, cell.x + (cell.w - 40) / 2, cell.y + 6, 40);

        /* Crisp pixel-measured UTF-8 label with subtle drop shadow */
        {
            char lbl[64];
            fit_utf8_label(g_items[i].label, cell.w - 4, lbl, sizeof(lbl));
            draw_text_center_face(FACE_SMALL, cell.x + cell.w / 2 + 1, cell.y + 53, lbl, RGBA(0, 0, 0, 210));
            draw_text_center_face(FACE_SMALL, cell.x + cell.w / 2,     cell.y + 52, lbl, C_WHITE);
        }
    }
}

int startmenu_open(void) { return g_start_open; }
void startmenu_toggle(void) {
    g_start_open = !g_start_open;
    g_action_open = 0;
    g_ctx.open = 0;
    wm_invalidate();
}

static Rect start_rect(void) {
    int h = 510;
    if (h > (int)fb.height - wm_taskbar_height() - 24)
        h = (int)fb.height - wm_taskbar_height() - 24;
    return (Rect){10, (int)fb.height - wm_taskbar_height() - h - 8, 610, h};
}

static void draw_start_menu(int mx, int my) {
    Rect sm = start_rect();
    Rect search_r = {sm.x + 18, sm.y + 16, sm.w - 36, 36};
    Rect left_list = {sm.x + 16, sm.y + 64, 246, sm.h - 122};
    int right_x = sm.x + 278, right_w = sm.w - 294;
    int i, y, tile_idx = 0;

    fx_acrylic_panel(sm, 10, theme->menu_bg, RGBA(140, 160, 190, 90), 12);
    ui_textfield(search_r, g_search, g_search_caret, 1, "Поиск приложений, настроек и команд...");

    /* Left Column: All Applications */
    draw_text_small(left_list.x + 6, left_list.y - 2, "Все приложения", theme->text_muted);
    y = left_list.y + 18 - g_start_scroll;
    {
        Rect saved = gfx_get_clip();
        gfx_set_clip((Rect){left_list.x, left_list.y + 16, left_list.w, left_list.h - 16});
        for (i = 0; i < app_count(); i++) {
            App *a = app_by_index(i);
            Rect row = {left_list.x, y, left_list.w - 6, 34};
            if (!a) continue;
            if (g_search[0] && !strcasestr_ascii(a->name, g_search) && !strcasestr_ascii(a->id, g_search))
                continue;
            if (y + 34 >= left_list.y + 16 && y < left_list.y + left_list.h) {
                if (pt_in(mx, my, row))
                    fill_round_rect_alpha(row, 6, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 56));
                icon_draw(a->icon, row.x + 8, row.y + 5, 24);
                draw_text_clipped(row.x + 40, row.y + 8, row.w - 72, a->name, theme->text_main);
                if (!a->installed)
                    ui_badge((Rect){row.x + row.w - 58, row.y + 8, 52, 18}, "Скачать",
                             RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 48), theme->accent);
            }
            y += 36;
        }
        gfx_set_clip(saved);
    }

    /* Right Column: Pinned Tiles Grid (3 columns) */
    draw_text_small(right_x, left_list.y - 2, "Закреплённые и рекомендуемые", theme->text_muted);
    for (i = 0; i < app_count() && tile_idx < 12; i++) {
        App *a = app_by_index(i);
        int tc, tr, tw = (right_w - 16) / 3, th = 76;
        Rect tile;
        if (!a || !a->pinned) continue;
        tc = tile_idx % 3;
        tr = tile_idx / 3;
        tile = (Rect){right_x + tc * (tw + 8), left_list.y + 20 + tr * (th + 8), tw, th};
        if (tile.y + tile.h <= sm.y + sm.h - 58) {
            int hov = pt_in(mx, my, tile);
            fill_round_rect(tile, 7, hov
                ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 70)
                : theme->card_bg);
            draw_round_rect(tile, 7, hov ? theme->accent : RGBA(130, 145, 170, 52));
            icon_draw(a->icon, tile.x + (tile.w - 30) / 2, tile.y + 9, 30);
            {
                char tlbl[64];
                fit_utf8_label(a->name, tile.w - 10, tlbl, sizeof(tlbl));
                draw_text_center_face(FACE_SMALL, tile.x + tile.w / 2, tile.y + 46, tlbl, theme->text_main);
            }
            if (!a->installed)
                fill_circle(tile.x + tile.w - 10, tile.y + 10, 4, theme->accent);
        }
        tile_idx++;
    }

    /* Bottom Footer Bar: Avatar + Username + Lock / Reboot / Shutdown */
    hline(sm.x + 14, sm.x + sm.w - 14, sm.y + sm.h - 50, RGBA(130, 145, 170, 58));
    avatar_draw(sys_profile.avatar_id, sm.x + 20, sm.y + sm.h - 41, 30);
    draw_text_bold(sm.x + 58, sm.y + sm.h - 40, sys_profile.username, theme->text_main);
    draw_text_small(sm.x + 58, sm.y + sm.h - 22, edition_name(sys_profile.edition), theme->text_muted);

    ui_button((Rect){sm.x + sm.w - 306, sm.y + sm.h - 40, 88, 30}, "Блокир.", pt_in(mx, my, (Rect){sm.x + sm.w - 306, sm.y + sm.h - 40, 88, 30}), 0);
    ui_button((Rect){sm.x + sm.w - 210, sm.y + sm.h - 40, 96, 30}, "Перезагр.", pt_in(mx, my, (Rect){sm.x + sm.w - 210, sm.y + sm.h - 40, 96, 30}), 0);
    ui_button_danger((Rect){sm.x + sm.w - 106, sm.y + sm.h - 40, 90, 30}, "Выключить", pt_in(mx, my, (Rect){sm.x + sm.w - 106, sm.y + sm.h - 40, 90, 30}));
}

static Rect action_rect(void) {
    return (Rect){(int)fb.width - 332, (int)fb.height - wm_taskbar_height() - 298, 322, 290};
}

static void draw_action_center(int mx, int my) {
    Rect ac = action_rect();
    char info[96];
    fx_acrylic_panel(ac, 10, theme->menu_bg, RGBA(140, 160, 190, 90), 10);
    draw_text_bold(ac.x + 16, ac.y + 14, "Быстрые параметры YoroOS 0.2", theme->text_main);
    snprintf(info, sizeof(info), "Мышь: %s", hal_pointer_driver_name());
    draw_text_small(ac.x + 16, ac.y + 38, info, theme->text_muted);

    ui_button((Rect){ac.x + 16, ac.y + 68, 138, 38}, theme->dark ? "Тёмная тема: ВКЛ" : "Светлая тема",
              pt_in(mx, my, (Rect){ac.x + 16, ac.y + 68, 138, 38}), theme->dark);
    ui_button((Rect){ac.x + 166, ac.y + 68, 140, 38}, sys_profile.night_light ? "Ночной свет: ВКЛ" : "Ночной свет: ВЫКЛ",
              pt_in(mx, my, (Rect){ac.x + 166, ac.y + 68, 140, 38}), sys_profile.night_light);
    ui_button((Rect){ac.x + 16, ac.y + 116, 138, 38}, "Сменить обои",
              pt_in(mx, my, (Rect){ac.x + 16, ac.y + 116, 138, 38}), 0);
    ui_button((Rect){ac.x + 166, ac.y + 116, 140, 38}, "Диспетчер задач",
              pt_in(mx, my, (Rect){ac.x + 166, ac.y + 116, 140, 38}), 0);
    ui_button((Rect){ac.x + 16, ac.y + 164, 290, 36}, "Восстановить системные файлы YoroOS",
              pt_in(mx, my, (Rect){ac.x + 16, ac.y + 164, 290, 36}), 0);
    ui_button((Rect){ac.x + 16, ac.y + 210, 290, 36}, "Все параметры YoroOS ->",
              pt_in(mx, my, (Rect){ac.x + 16, ac.y + 210, 290, 36}), 1);
    snprintf(info, sizeof(info), "ОЗУ ядра: %u КБ / %u МБ  ·  Файлов: %d",
             (u32)(heap_used() / 1024U), (u32)(heap_total() / (1024U * 1024U)), fs_total_nodes());
    draw_text_small(ac.x + 16, ac.y + 260, info, theme->text_muted);
}

void desktop_draw_overlay(void) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int th = wm_taskbar_height();
    Rect tb = {0, (int)fb.height - th, (int)fb.width, th};
    Rect start_btn = {6, tb.y + 4, 42, th - 8};
    Rect search_pill = {54, tb.y + 6, 196, th - 12};
    int x, i, hh, mm, ss, dd, mo, yy;
    char tbuf[32], dbuf[32];
    static const char *pins[] = { "explorer", "browser", "store", "terminal", "settings" };

    /* 1. Translucent Frosted Acrylic Windows 10/11 Taskbar */
    fx_blur_rect(tb, 8, theme->task_bg);
    hline(0, (int)fb.width - 1, tb.y, RGBA(255, 255, 255, 36));

    /* Start Button */
    if (pt_in(mx, my, start_btn) || g_start_open)
        fill_round_rect_alpha(start_btn, 6, RGBA(255, 255, 255, 34));
    icon_draw(ICON_ABOUT, start_btn.x + 9, start_btn.y + 7, 24);

    /* Search Pill */
    fill_round_rect_alpha(search_pill, 16, RGBA(255, 255, 255, 24));
    draw_round_rect(search_pill, 16, RGBA(255, 255, 255, 42));
    icon_draw(ICON_SEARCH, search_pill.x + 10, search_pill.y + 7, 18);
    draw_text_small(search_pill.x + 34, search_pill.y + (search_pill.h - font_line_height(FACE_SMALL)) / 2,
                    "Поиск в YoroOS...", RGBA(230, 238, 250, 205));

    /* Pinned Quick-Launch Icons */
    x = search_pill.x + search_pill.w + 10;
    for (i = 0; i < 5; i++) {
        App *a = app_find(pins[i]);
        Rect b = {x, tb.y + 4, 40, th - 8};
        if (!a) continue;
        if (pt_in(mx, my, b)) fill_round_rect_alpha(b, 6, RGBA(255, 255, 255, 30));
        icon_draw(a->icon, b.x + 7, b.y + 6, 26);
        x += 44;
    }

    vline(x + 2, tb.y + 10, tb.y + th - 10, RGBA(255, 255, 255, 40));
    x += 10;

    /* Running Window Task Buttons */
    for (i = 0; i < wm_window_count(); i++) {
        Window *w = wm_window_at_order(i);
        int focused = (w == wm_focused());
        Rect wb = {x, tb.y + 5, 148, th - 10};
        if (!w || !w->used || x + 150 > (int)fb.width - 210) continue;
        fill_round_rect_alpha(wb, 6, focused ? RGBA(255, 255, 255, 44) : RGBA(255, 255, 255, 16));
        icon_draw(w->icon, wb.x + 8, wb.y + 8, 20);
        draw_text_clipped(wb.x + 34, wb.y + (wb.h - font_line_height(FACE_SMALL)) / 2, 106, w->title, C_WHITE);
        fill_round_rect((Rect){wb.x + (focused ? 24 : 52), wb.y + wb.h - 3, focused ? wb.w - 48 : 44, 3}, 1, theme->accent);
        x += 154;
    }

    /* Right System Tray: Layout (RU/EN) + Wifi/Volume/Action Center + Date/Time + Show Desktop */
    {
        Rect lang_r = {(int)fb.width - 196, tb.y + 6, 38, th - 12};
        Rect tray_r = {(int)fb.width - 154, tb.y + 6, 68, th - 12};
        Rect clk_r  = {(int)fb.width - 82,  tb.y + 4, 72, th - 8};
        if (pt_in(mx, my, lang_r)) fill_round_rect_alpha(lang_r, 5, RGBA(255, 255, 255, 32));
        draw_text_center_face(FACE_SMALL, lang_r.x + lang_r.w / 2,
                              lang_r.y + (lang_r.h - font_line_height(FACE_SMALL)) / 2,
                              kbd_is_ru() ? "РУС" : "ENG", C_WHITE);

        if (pt_in(mx, my, tray_r)) fill_round_rect_alpha(tray_r, 5, RGBA(255, 255, 255, 32));
        icon_draw(ICON_WIFI,   tray_r.x + 6,  tray_r.y + 8, 18);
        icon_draw(ICON_VOLUME, tray_r.x + 26, tray_r.y + 8, 18);
        icon_draw(ICON_BELL,   tray_r.x + 46, tray_r.y + 8, 18);

        rtc_read(&hh, &mm, &ss, &dd, &mo, &yy);
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", hh, mm);
        snprintf(dbuf, sizeof(dbuf), "%02d.%02d.%04d", dd, mo, yy);
        draw_text_center_face(FACE_SMALL, clk_r.x + clk_r.w / 2, tb.y + 7, tbuf, C_WHITE);
        draw_text_center_face(FACE_SMALL, clk_r.x + clk_r.w / 2, tb.y + 23, dbuf, RGBA(220, 232, 248, 220));
        vline((int)fb.width - 5, tb.y + 6, tb.y + th - 6, RGBA(255, 255, 255, 80));
    }

    if (g_start_open) draw_start_menu(mx, my);
    if (g_action_open) draw_action_center(mx, my);

    /* Popup Right-Click Context Menu */
    if (g_ctx.open) {
        fx_acrylic_panel(g_ctx.box, 8, theme->menu_bg, RGBA(140, 160, 190, 105), 8);
        for (i = 0; i < g_ctx.count; i++) {
            Rect ir = {g_ctx.box.x + 5, g_ctx.box.y + 6 + i * 30, g_ctx.box.w - 10, 28};
            if (pt_in(mx, my, ir))
                fill_round_rect(ir, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 85));
            draw_text(ir.x + 12, ir.y + (ir.h - font_line_height(FACE_UI)) / 2, g_ctx.labels[i], theme->text_main);
        }
    }
}

static void on_desk_bg_ctx(int idx, void *ctx) {
    UNUSED(ctx);
    switch (idx) {
    case 0: desktop_rebuild(); wm_invalidate(); break;
    case 1: wallpaper_set(wallpaper_current() + 1); break;
    case 2: theme_set(theme->dark ? 1 : 0); break;
    case 3: fs_create(fs_desktop_dir(), "Заметка.txt", "Новая заметка на Рабочем столе YoroOS 0.2\n"); desktop_rebuild(); break;
    case 4: fs_mkdir(fs_desktop_dir(), "Новая папка"); desktop_rebuild(); break;
    case 5: terminal_open(); break;
    case 6: settings_open(); break;
    }
}

static void on_desk_item_ctx(int idx, void *ctx) {
    int item_i = (int)(uptr)ctx;
    if (item_i < 0 || item_i >= g_nitems) return;
    if (idx == 0) {
        launch_desk_item(item_i);
    } else if (idx == 1 && g_items[item_i].kind == 3) {
        char zname[64];
        zip_create_from_node(g_items[item_i].node, fs_desktop_dir(), zname, sizeof(zname));
        desktop_rebuild();
    } else if (idx == 2) {
        if (g_items[item_i].kind == 3) fs_move_to_trash(g_items[item_i].node);
        else if (g_items[item_i].kind == 2 && g_items[item_i].app) {
            g_items[item_i].app->on_desktop = 0;
            desktop_rebuild();
        }
    } else if (idx == 3 && g_items[item_i].kind == 3) {
        fs_remove(g_items[item_i].node);
    }
}

int desktop_event(YoroEvent *e) {
    int th = wm_taskbar_height();
    Rect tb = {0, (int)fb.height - th, (int)fb.width, th};
    int i;

    /* 1. Active Context Menu */
    if (g_ctx.open) {
        if (e->type == EV_MOUSE_DOWN) {
            if (pt_in(e->x, e->y, g_ctx.box)) {
                int idx = (e->y - g_ctx.box.y - 6) / 30;
                CtxMenuCb cb = g_ctx.cb;
                void *c = g_ctx.ctx;
                g_ctx.open = 0;
                if (idx >= 0 && idx < g_ctx.count && cb) cb(idx, c);
                wm_invalidate();
                return 1;
            }
            g_ctx.open = 0;
            wm_invalidate();
        }
        if (e->type == EV_KEY && e->key == KEY_ESC) { g_ctx.open = 0; wm_invalidate(); return 1; }
    }

    /* 2. Open Start Menu */
    if (g_start_open) {
        Rect sm = start_rect();
        if (e->type == EV_WHEEL && pt_in(e->x, e->y, sm)) {
            g_start_scroll -= e->wheel * 28;
            if (g_start_scroll < 0) g_start_scroll = 0;
            wm_invalidate();
            return 1;
        }
        if (e->type == EV_KEY) {
            if (e->key == KEY_ESC) { g_start_open = 0; wm_invalidate(); return 1; }
            if (e->key == KEY_ENTER) {
                for (i = 0; i < app_count(); i++) {
                    App *a = app_by_index(i);
                    if (a && (!g_search[0] || strcasestr_ascii(a->name, g_search) || strcasestr_ascii(a->id, g_search))) {
                        g_start_open = 0;
                        if (!a->installed) app_install(a);
                        app_launch(a);
                        return 1;
                    }
                }
            }
            if (ui_textfield_edit(g_search, sizeof(g_search), &g_search_len, &g_search_caret, e)) {
                wm_invalidate();
                return 1;
            }
            return 1;
        }
        if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
            if (pt_in(e->x, e->y, sm)) {
                Rect left_list = {sm.x + 16, sm.y + 64, 246, sm.h - 122};
                int right_x = sm.x + 278, right_w = sm.w - 294, y = left_list.y + 18 - g_start_scroll, tidx = 0;

                /* Power / Lock buttons */
                if (pt_in(e->x, e->y, (Rect){sm.x + sm.w - 306, sm.y + sm.h - 40, 88, 30})) {
                    g_start_open = 0; lockscreen_lock(); return 1;
                }
                if (pt_in(e->x, e->y, (Rect){sm.x + sm.w - 210, sm.y + sm.h - 40, 96, 30})) {
                    system_reboot(); return 1;
                }
                if (pt_in(e->x, e->y, (Rect){sm.x + sm.w - 106, sm.y + sm.h - 40, 90, 30})) {
                    system_shutdown(); return 1;
                }
                /* App list clicks */
                for (i = 0; i < app_count(); i++) {
                    App *a = app_by_index(i);
                    Rect row = {left_list.x, y, left_list.w - 6, 34};
                    if (!a) continue;
                    if (g_search[0] && !strcasestr_ascii(a->name, g_search) && !strcasestr_ascii(a->id, g_search))
                        continue;
                    if (pt_in(e->x, e->y, row)) {
                        g_start_open = 0;
                        if (!a->installed) app_install(a);
                        app_launch(a);
                        return 1;
                    }
                    y += 36;
                }
                /* Tile clicks */
                for (i = 0; i < app_count() && tidx < 12; i++) {
                    App *a = app_by_index(i);
                    int tc, tr, tw = (right_w - 16) / 3, th2 = 76;
                    Rect tile;
                    if (!a || !a->pinned) continue;
                    tc = tidx % 3; tr = tidx / 3;
                    tile = (Rect){right_x + tc * (tw + 8), left_list.y + 20 + tr * (th2 + 8), tw, th2};
                    if (pt_in(e->x, e->y, tile)) {
                        g_start_open = 0;
                        if (!a->installed) app_install(a);
                        app_launch(a);
                        return 1;
                    }
                    tidx++;
                }
                return 1;
            }
            if (!pt_in(e->x, e->y, (Rect){6, tb.y + 4, 42, th - 8})) {
                g_start_open = 0;
                wm_invalidate();
            }
        }
    }

    /* 3. Open Action Center */
    if (g_action_open && e->type == EV_MOUSE_DOWN && e->btn == 1) {
        Rect ac = action_rect();
        if (pt_in(e->x, e->y, ac)) {
            if (pt_in(e->x, e->y, (Rect){ac.x + 16, ac.y + 68, 138, 38})) theme_set(theme->dark ? 1 : 0);
            else if (pt_in(e->x, e->y, (Rect){ac.x + 166, ac.y + 68, 140, 38})) { sys_profile.night_light = !sys_profile.night_light; wm_invalidate(); }
            else if (pt_in(e->x, e->y, (Rect){ac.x + 16, ac.y + 116, 138, 38})) wallpaper_set(wallpaper_current() + 1);
            else if (pt_in(e->x, e->y, (Rect){ac.x + 166, ac.y + 116, 140, 38})) { g_action_open = 0; taskmgr_open(); }
            else if (pt_in(e->x, e->y, (Rect){ac.x + 16, ac.y + 164, 290, 36})) { fs_restore_system_files(); }
            else if (pt_in(e->x, e->y, (Rect){ac.x + 16, ac.y + 210, 290, 36})) { g_action_open = 0; settings_open(); }
            return 1;
        }
        g_action_open = 0;
        wm_invalidate();
    }

    /* 4. Taskbar Clicks */
    if (e->type == EV_MOUSE_DOWN && pt_in(e->x, e->y, tb)) {
        static const char *pins[] = { "explorer", "browser", "store", "terminal", "settings" };
        int x;
        if (pt_in(e->x, e->y, (Rect){6, tb.y + 4, 42, th - 8}) ||
            pt_in(e->x, e->y, (Rect){54, tb.y + 6, 196, th - 12})) {
            startmenu_toggle();
            return 1;
        }
        x = 54 + 196 + 10;
        for (i = 0; i < 5; i++) {
            Rect b = {x, tb.y + 4, 40, th - 8};
            if (pt_in(e->x, e->y, b)) { app_launch(app_find(pins[i])); return 1; }
            x += 44;
        }
        x += 10;
        for (i = 0; i < wm_window_count(); i++) {
            Window *w = wm_window_at_order(i);
            Rect wb = {x, tb.y + 5, 148, th - 10};
            if (!w || !w->used || x + 150 > (int)fb.width - 210) continue;
            if (pt_in(e->x, e->y, wb)) {
                if (w == wm_focused() && !w->minimized) { w->minimized = 1; wm_invalidate(); }
                else wm_focus(w);
                return 1;
            }
            x += 154;
        }
        if (pt_in(e->x, e->y, (Rect){(int)fb.width - 196, tb.y + 6, 38, th - 12})) {
            kbd_toggle_layout();
            return 1;
        }
        if (pt_in(e->x, e->y, (Rect){(int)fb.width - 154, tb.y + 6, 80, th - 12})) {
            g_action_open = !g_action_open;
            g_start_open = 0;
            wm_invalidate();
            return 1;
        }
        if (e->x >= (int)fb.width - 12) {
            for (i = 0; i < wm_window_count(); i++) {
                Window *w = wm_window_at_order(i);
                if (w) w->minimized = 1;
            }
            wm_invalidate();
            return 1;
        }
        return 1;
    }

    /* 5. Desktop Icon & Background Clicks (only if not over an open window) */
    if ((e->type == EV_MOUSE_DOWN) && !wm_at(e->x, e->y)) {
        for (i = 0; i < g_nitems; i++) {
            if (pt_in(e->x, e->y, desk_cell_rect(i))) {
                desktop_selected = i;
                if (e->btn == 1) {
                    u32 now = sys_ticks();
                    if (g_last_desk_idx == i && now - g_last_desk_click < 45) {
                        g_last_desk_click = 0;
                        launch_desk_item(i);
                    } else {
                        g_last_desk_idx = i;
                        g_last_desk_click = now;
                    }
                } else if (e->btn == 2) {
                    static const char *ilabels[] = {
                        "Открыть / Запустить",
                        "Сжать в архив .ZIP",
                        "Переместить в Корзину",
                        "Удалить безвозвратно"
                    };
                    ctx_menu_open(e->x, e->y, ilabels, 4, on_desk_item_ctx, (void *)(uptr)i);
                }
                wm_invalidate();
                return 1;
            }
        }
        desktop_selected = -1;
        if (e->btn == 2) {
            static const char *bglabels[] = {
                "Обновить Рабочий стол",
                "Следующие обои HD",
                "Переключить тему (Тёмная/Светлая)",
                "Создать текстовый документ",
                "Создать папку",
                "Открыть Терминал YoroOS",
                "Параметры и Персонализация"
            };
            ctx_menu_open(e->x, e->y, bglabels, 7, on_desk_bg_ctx, NULL);
        }
        wm_invalidate();
        return 1;
    }
    return 0;
}

/* ---------------- Windows 10/11 Lock Screen ---------------- */
void lockscreen_lock(void) { system_screen_mode = SCREEN_LOCK; wm_invalidate(); }

void lockscreen_draw(void) {
    int hh, mm, ss, dd, mo, yy;
    char tbuf[32], dbuf[48];
    int cx = (int)fb.width / 2, cy = (int)fb.height / 2;
    Rect btn = {cx - 110, cy + 96, 220, 40};

    theme_draw_wallpaper();
    fill_rect_alpha((Rect){0, 0, (int)fb.width, (int)fb.height}, RGBA(8, 12, 22, 128));

    rtc_read(&hh, &mm, &ss, &dd, &mo, &yy);
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", hh, mm);
    snprintf(dbuf, sizeof(dbuf), "%02d.%02d.%04d  ·  YoroOS 0.2 «Aurora»", dd, mo, yy);
    draw_text_center_face(FACE_HERO, cx, (int)fb.height / 6, tbuf, C_WHITE);
    draw_text_center_face(FACE_TITLE, cx, (int)fb.height / 6 + 52, dbuf, RGB(220, 234, 252));

    avatar_draw(sys_profile.avatar_id, cx - 42, cy - 34, 84);
    draw_text_center_face(FACE_TITLE, cx, cy + 60, sys_profile.username, C_WHITE);
    ui_button(btn, "Войти в систему", pt_in(mouse_get_x(), mouse_get_y(), btn), 1);
}

void lockscreen_event(YoroEvent *e) {
    if (e->type == EV_KEY || (e->type == EV_MOUSE_DOWN && e->btn == 1)) {
        system_screen_mode = SCREEN_DESKTOP;
        wm_invalidate();
    }
}
