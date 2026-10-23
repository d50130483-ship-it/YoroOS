/* YoroOS - Explorer: browse, create, rename and delete anything */
#include "kernel.h"

#define TOOLBAR_H 48
#define SIDEBAR_W 190
#define ROW_H 32
#define HEADER_H 30
#define STATUS_H 26

typedef struct {
    int dir;
    int sel;
    int scroll;
    int back[16];
    int back_n;
    int dlg;            /* 0 none, 1 new folder, 2 rename, 3 new file */
    char buf[64];
    int len, caret;
    u32 last_click;
    int last_node;
} ExplorerState;

static ExplorerState st;

static const char *sidebar_items[] = {
    "Этот компьютер", "Рабочий стол", "Документы", "Загрузки",
    "Изображения", "Диск C:", "Program Files", "Windows", "Корзина"
};
static const char *sidebar_paths[] = {
    "C:\\", "C:\\Users\\Yoro\\Desktop", "C:\\Users\\Yoro\\Documents",
    "C:\\Users\\Yoro\\Downloads", "C:\\Users\\Yoro\\Pictures", "C:\\",
    "C:\\Program Files", "C:\\Windows", "C:\\"
};
#define NSIDEBAR 9

static char *path_buf(int dir) {
    static char b[256];
    fs_path(dir, b, sizeof(b));
    return b;
}

static void navigate(int node) {
    if (node < 0 || !fs_is_dir(node)) return;
    if (node != st.dir) {
        if (st.back_n < 16) st.back[st.back_n++] = st.dir;
        st.dir = node;
        st.sel = -1;
        st.scroll = 0;
    }
}

static int file_icon(int node) {
    const char *name = fs_name(node);
    int l = (int)strlen(name), i;
    const char *ext = NULL;
    if (fs_is_dir(node)) return ICON_FOLDER;
    for (i = l - 1; i > 0; i--) if (name[i] == '.') { ext = name + i + 1; break; }
    if (!ext) return ICON_TEXTFILE;
    if (!strcmp(ext, "zip") || !strcmp(ext, "ZIP")) return ICON_ZIP;
    if (!strcmp(ext, "exe") || !strcmp(ext, "EXE")) return ICON_EXE;
    if (!strcmp(ext, "bmp") || !strcmp(ext, "png") || !strcmp(ext, "jpg")) return ICON_IMAGE;
    return ICON_TEXTFILE;
}

static void open_node(int node) {
    const char *name = fs_name(node);
    if (fs_is_dir(node)) { navigate(node); return; }
    {
        int l = (int)strlen(name);
        const char *ext = NULL;
        int i;
        for (i = l - 1; i > 0; i--) if (name[i] == '.') { ext = name + i + 1; break; }
        if (ext && (!strcmp(ext, "zip") || !strcmp(ext, "ZIP")))
            zip_open_node(node);
        else if (ext && (!strcmp(ext, "exe") || !strcmp(ext, "EXE")))
            exe_open_node(node);
        else if (ext && (!strcmp(ext, "bmp") || !strcmp(ext, "png") || !strcmp(ext, "jpg")))
            image_viewer_open(name);
        else
            notepad_open_file(node);
    }
}

static void delete_selected(void) {
    int n = fs_child(st.dir, st.sel >= 0 ? 0 : 0);
    UNUSED(n);
    if (st.sel < 0) { toast("Выберите файл или папку"); return; }
    {
        int node = fs_child(st.dir, st.sel);
        if (node < 0) return;
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Удалено: %s", fs_name(node));
            if (fs_remove(node) == 0) {
                st.sel = -1;
                toast(msg);
            } else toast("Не удалось удалить (системный корень)");
        }
    }
}

static void draw_toolbar(Rect c, int hover_btn, int hover_new) {
    Rect bar = {c.x, c.y, c.w, TOOLBAR_H};
    fill_rect(bar, RGB(247, 248, 251));
    fill_rect((Rect){c.x, c.y + TOOLBAR_H - 1, c.w, 1}, RGB(224, 228, 236));
    {
        Rect back = {c.x + 10, c.y + 10, 28, 28};
        Rect up = {c.x + 44, c.y + 10, 28, 28};
        Rect newf = {c.x + 82, c.y + 9, 118, 30};
        Rect newt = {c.x + 206, c.y + 9, 96, 30};
        Rect del = {c.x + 310, c.y + 9, 96, 30};
        Rect addr = {c.x + 418, c.y + 9, c.w - 418 - 14, 30};
        if (st.back_n) ui_icon_button(back, ICON_BACK, hover_btn == 1);
        else icon_draw(ICON_BACK, back.x + 5, back.y + 5, 18);
        ui_icon_button(up, ICON_UP, hover_btn == 2);
        ui_button(newf, "+ Папка", hover_btn == 3, 0);
        ui_button(newt, "+ Файл", hover_btn == 4, 0);
        ui_button(del, "Удалить", hover_btn == 5, 0);
        fill_round_rect(addr, 5, C_WHITE);
        draw_round_rect(addr, 5, RGB(214, 218, 226));
        icon_draw(ICON_FOLDER, addr.x + 7, addr.y + 6, 18);
        draw_text_clip(addr.x + 30, addr.y + (addr.h - FONT_H) / 2, path_buf(st.dir),
                       RGB(40, 46, 60), (Rect){addr.x + 28, addr.y, addr.w - 32, addr.h});
        UNUSED(hover_new);
    }
}

static void draw_sidebar(Rect c, int hover_item) {
    Rect sb = {c.x, c.y + TOOLBAR_H, SIDEBAR_W, c.h - TOOLBAR_H - STATUS_H};
    fill_rect(sb, RGB(242, 244, 248));
    fill_rect((Rect){sb.x + sb.w - 1, sb.y, 1, sb.h}, RGB(224, 228, 236));
    draw_text(sb.x + 16, sb.y + 14, "Быстрый доступ", RGB(120, 128, 144));
    {
        int i;
        for (i = 0; i < NSIDEBAR; i++) {
            Rect it = {sb.x + 8, sb.y + 36 + i * 34, sb.w - 16, 30};
            if (hover_item == i) fill_round_rect(it, 5, RGB(228, 233, 242));
            icon_draw(i == 0 ? ICON_COMPUTER : (i == 8 ? ICON_TRASH : (i == 5 ? ICON_DRIVE : ICON_FOLDER)),
                      it.x + 6, it.y + 6, 18);
            draw_text(it.x + 30, it.y + (it.h - FONT_H) / 2, sidebar_items[i], RGB(40, 46, 60));
        }
    }
}

static void draw_list(Rect c, int hover_row) {
    Rect list = {c.x + SIDEBAR_W, c.y + TOOLBAR_H, c.w - SIDEBAR_W, c.h - TOOLBAR_H - STATUS_H};
    int n = fs_count(st.dir), i;
    fill_rect(list, C_WHITE);
    /* header */
    fill_rect((Rect){list.x, list.y, list.w, HEADER_H}, RGB(250, 251, 253));
    fill_rect((Rect){list.x, list.y + HEADER_H - 1, list.w, 1}, RGB(230, 233, 240));
    draw_text(list.x + 14, list.y + 6, "Имя", RGB(110, 118, 132));
    draw_text(list.x + list.w / 2, list.y + 6, "Тип", RGB(110, 118, 132));
    draw_text(list.x + list.w - 190, list.y + 6, "Размер", RGB(110, 118, 132));

    gfx_set_clip((Rect){list.x, list.y + HEADER_H, list.w, list.h - HEADER_H});
    for (i = 0; i < n; i++) {
        int node = fs_child(st.dir, i);
        Rect row = {list.x, list.y + HEADER_H + i * ROW_H - st.scroll, list.w, ROW_H};
        char sizebuf[32];
        if (row.y + ROW_H < list.y + HEADER_H || row.y > list.y + list.h) continue;
        {
            int sel = (st.sel == i);
            Color fg_main = sel ? C_WHITE : RGB(30, 36, 48);
            Color fg_dim = sel ? RGB(226, 238, 252) : RGB(110, 118, 132);
            if (sel) fill_rect(row, theme->accent);
            else if (hover_row == i) fill_rect(row, RGB(244, 247, 252));
            icon_draw(file_icon(node), row.x + 12, row.y + 6, 20);
            draw_text_clip(row.x + 42, row.y + (ROW_H - FONT_H) / 2, fs_name(node), fg_main,
                           (Rect){row.x + 42, row.y, list.w / 2 - 60, ROW_H});
            draw_text(list.x + list.w / 2, row.y + (ROW_H - FONT_H) / 2,
                      fs_is_dir(node) ? "Папка" : "Файл", fg_dim);
            if (fs_is_dir(node)) strcpy(sizebuf, "—");
            else { u32 sz = (u32)fs_size(node); char t[16]; utoa(sz, t); sprintf(sizebuf, "%s Б", t); }
            draw_text(list.x + list.w - 190, row.y + (ROW_H - FONT_H) / 2, sizebuf, fg_dim);
        }
    }
    gfx_set_clip(c);
    if (n == 0)
        draw_text_center(list.x + list.w / 2, list.y + list.h / 2, "Эта папка пуста", RGB(150, 158, 172));
    {
        Rect track = {list.x + list.w - 10, list.y + HEADER_H + 4, 6, list.h - HEADER_H - 8};
        ui_scrollbar(track, n * ROW_H, list.h - HEADER_H, st.scroll);
    }
}

static void draw_status(Rect c) {
    Rect s = {c.x, c.y + c.h - STATUS_H, c.w, STATUS_H};
    char buf[64];
    fill_rect(s, RGB(246, 248, 251));
    fill_rect((Rect){s.x, s.y, s.w, 1}, RGB(228, 232, 240));
    sprintf(buf, "Элементов: %d", fs_count(st.dir));
    draw_text(s.x + 14, s.y + 4, buf, RGB(110, 118, 132));
    draw_text_right(s.x + s.w - 14, s.y + 4, "YoroFS", RGB(140, 148, 162));
}

static void draw_dialog(Rect c) {
    Rect d;
    if (!st.dlg) return;
    d.w = 360; d.h = 150;
    d.x = c.x + (c.w - d.w) / 2;
    d.y = c.y + (c.h - d.h) / 2;
    fill_rect_alpha(c, RGBA(10, 16, 28, 90));
    draw_shadow(d, 8);
    fill_round_rect(d, 10, C_WHITE);
    draw_round_rect(d, 10, RGB(200, 206, 216));
    draw_text(d.x + 20, d.y + 16,
              st.dlg == 1 ? "Новая папка" : (st.dlg == 3 ? "Новый файл" : "Переименовать"),
              RGB(30, 36, 48));
    ui_textfield((Rect){d.x + 20, d.y + 46, d.w - 40, 32}, st.buf, st.caret, 1, "Имя");
    ui_button((Rect){d.x + d.w - 200, d.y + 96, 84, 32}, "Отмена", 0, 0);
    ui_button((Rect){d.x + d.w - 108, d.y + 96, 88, 32}, "OK", 1, 1);
    draw_text(d.x + 20, d.y + 104, "Enter — сохранить, Esc — отмена", RGB(150, 158, 172));
}

static void draw_window(Window *w, Rect c) {
    int mx = mouse_get_x() - c.x, my = mouse_get_y() - c.y;
    int hover_btn = 0, hover_item = -1, hover_row = -1;
    int i, n = fs_count(st.dir);
    UNUSED(hover_btn);
    {
        Rect btns[6] = {
            {c.x + 10, c.y + 10, 28, 28}, {c.x + 44, c.y + 10, 28, 28},
            {c.x + 82, c.y + 9, 118, 30}, {c.x + 206, c.y + 9, 96, 30},
            {c.x + 310, c.y + 9, 96, 30}, {0, 0, 0, 0}
        };
        for (i = 0; i < 5; i++) if (pt_in(mx, my, btns[i])) hover_btn = i + 1;
    }
    {
        Rect sb = {c.x, c.y + TOOLBAR_H, SIDEBAR_W, c.h - TOOLBAR_H - STATUS_H};
        for (i = 0; i < NSIDEBAR; i++) {
            Rect it = {sb.x + 8, sb.y + 36 + i * 34, sb.w - 16, 30};
            if (pt_in(mx, my, it)) hover_item = i;
        }
    }
    {
        Rect list = {c.x + SIDEBAR_W, c.y + TOOLBAR_H + HEADER_H, c.w - SIDEBAR_W, c.h - TOOLBAR_H - HEADER_H - STATUS_H};
        if (pt_in(mx, my, list)) {
            int row = (my - list.y + st.scroll) / ROW_H;
            if (row >= 0 && row < n) hover_row = row;
        }
    }
    draw_toolbar(c, hover_btn, 0);
    draw_sidebar(c, hover_item);
    draw_list(c, hover_row);
    draw_status(c);
    draw_dialog(c);
    w->scroll = st.scroll;
}

static void dialog_commit(void) {
    if (st.dlg == 1) {
        if (fs_mkdir(st.dir, st.buf) >= 0) toast("Папка создана");
        else toast("Такая папка уже есть");
    } else if (st.dlg == 3) {
        if (fs_create(st.dir, st.buf, "") >= 0) toast("Файл создан");
        else toast("Не удалось создать файл");
    } else if (st.dlg == 2) {
        int node = fs_child(st.dir, st.sel);
        if (node > 0) { fs_rename(node, st.buf); toast("Переименовано"); }
    }
    st.dlg = 0;
    wm_invalidate();
}

static void open_dialog(int kind) {
    st.dlg = kind;
    st.buf[0] = 0;
    st.len = 0;
    st.caret = 0;
    if (kind == 2) {
        int node = fs_child(st.dir, st.sel);
        if (node > 0) {
            strncpy(st.buf, fs_name(node), 63);
            st.len = (int)strlen(st.buf);
            st.caret = st.len;
        }
    }
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;   /* absolute */
    if (st.dlg) {
        if (e->type == EV_KEY) {
            if (e->key == KEY_ESC) { st.dlg = 0; wm_invalidate(); return; }
            if (e->key == KEY_ENTER) { dialog_commit(); return; }
            if (ui_textfield_edit(st.buf, 64, &st.len, &st.caret, e)) wm_invalidate();
            return;
        }
        if (e->type == EV_MOUSE_DOWN) {
            Rect d;
            d.w = 360; d.h = 150;
            d.x = c.x + (c.w - d.w) / 2;
            d.y = c.y + (c.h - d.h) / 2;
            if (pt_in(mx, my, (Rect){d.x + d.w - 108, d.y + 96, 88, 32})) { dialog_commit(); return; }
            if (pt_in(mx, my, (Rect){d.x + d.w - 200, d.y + 96, 84, 32})) { st.dlg = 0; wm_invalidate(); return; }
        }
        return;
    }
    if (e->type == EV_WHEEL) {
        int n = fs_count(st.dir);
        int view = c.h - TOOLBAR_H - HEADER_H - STATUS_H;
        int maxs = n * ROW_H - view;
        st.scroll -= e->wheel * (ROW_H * 2);
        if (st.scroll > maxs) st.scroll = maxs > 0 ? maxs : 0;
        if (st.scroll < 0) st.scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type == EV_KEY) {
        if (e->key == KEY_DEL) { delete_selected(); wm_invalidate(); return; }
        if (e->key == KEY_F2 && st.sel >= 0) { open_dialog(2); wm_invalidate(); return; }
        if (e->key == KEY_ENTER && st.sel >= 0) {
            int node = fs_child(st.dir, st.sel);
            if (node >= 0) { open_node(node); wm_invalidate(); }
            return;
        }
        if (e->key == KEY_UP && st.sel > 0) { st.sel--; wm_invalidate(); return; }
        if (e->key == KEY_DOWN && st.sel < fs_count(st.dir) - 1) { st.sel++; wm_invalidate(); return; }
        return;
    }
    if (e->type == EV_MOUSE_DOWN) {
        /* back / up */
        if (pt_in(mx, my, (Rect){c.x + 10, c.y + 10, 28, 28})) {
            if (st.back_n) { st.dir = st.back[--st.back_n]; st.sel = -1; st.scroll = 0; wm_invalidate(); }
            return;
        }
        if (pt_in(mx, my, (Rect){c.x + 44, c.y + 10, 28, 28})) {
            char path[256];
            int len, i3;
            fs_path(st.dir, path, sizeof(path));
            len = (int)strlen(path);
            if (len > 3) {
                for (i3 = len - 1; i3 > 2 && path[i3] != '\\'; i3--) {}
                if (i3 > 2) path[i3] = 0;
                else path[3] = 0;
                {
                    int up = fs_parse_path(fs_root(), path);
                    if (up >= 0) navigate(up);
                }
            }
            wm_invalidate();
            return;
        }
        if (pt_in(mx, my, (Rect){c.x + 82, c.y + 9, 118, 30})) { open_dialog(1); wm_invalidate(); return; }
        if (pt_in(mx, my, (Rect){c.x + 206, c.y + 9, 96, 30})) { open_dialog(3); wm_invalidate(); return; }
        if (pt_in(mx, my, (Rect){c.x + 310, c.y + 9, 96, 30})) { delete_selected(); wm_invalidate(); return; }

        /* sidebar */
        {
            Rect sb = {c.x, c.y + TOOLBAR_H, SIDEBAR_W, c.h - TOOLBAR_H - STATUS_H};
            int i;
            for (i = 0; i < NSIDEBAR; i++) {
                Rect it = {sb.x + 8, sb.y + 36 + i * 34, sb.w - 16, 30};
                if (pt_in(mx, my, it)) {
                    if (i == 8) { toast("Корзина пуста"); return; }
                    {
                        int node = fs_parse_path(fs_root(), sidebar_paths[i]);
                        if (node >= 0) navigate(node);
                    }
                    wm_invalidate();
                    return;
                }
            }
        }
        /* list rows */
        {
            Rect list = {c.x + SIDEBAR_W, c.y + TOOLBAR_H + HEADER_H, c.w - SIDEBAR_W,
                         c.h - TOOLBAR_H - HEADER_H - STATUS_H};
            if (my >= list.y && my < list.y + list.h && mx >= list.x) {
                int row = (my - list.y + st.scroll) / ROW_H;
                if (row >= 0 && row < fs_count(st.dir)) {
                    int node = fs_child(st.dir, row);
                    if (st.sel == row && st.last_node == node && sys_ticks() - st.last_click < 45) {
                        open_node(node);
                        st.last_click = 0;
                    } else {
                        st.sel = row;
                        st.last_node = node;
                        st.last_click = sys_ticks();
                    }
                    wm_invalidate();
                    return;
                }
                st.sel = -1;
                wm_invalidate();
            }
        }
    }
}

void explorer_open(void) {
    Window *w;
    if (st.dir < 0) st.dir = fs_root();
    w = wm_open(app_find("explorer"), "Проводник", -1, -1, 900, 600, draw_window, on_event, NULL);
    if (w) { w->min_w = 620; w->min_h = 380; w->icon = ICON_FOLDER; }
    wm_invalidate();
}

void explorer_open_path(const char *path) {
    int node = fs_parse_path(fs_root(), path);
    if (node >= 0) { st.dir = node; st.sel = -1; st.scroll = 0; }
    explorer_open();
}
