/* YoroOS 0.2 - Windows 10/11 File Explorer with Unrestricted System Folder Control */
#include "kernel.h"

typedef struct {
    int  cwd;
    int  sel;
    int  clip_node;
    int  hist[24];
    int  nhist;
    int  dialog_mode; /* 0=none, 1=new folder, 2=new file, 3=rename */
    char input[48];
    int  in_len, in_caret;
    u32  last_click;
    int  last_idx;
} ExpState;

static void exp_nav(ExpState *st, int dir) {
    if (dir < 0 || !fs_is_dir(dir)) return;
    if (st->nhist < 24) st->hist[st->nhist++] = st->cwd;
    st->cwd = dir;
    st->sel = -1;
    wm_invalidate();
}

static ExpState *g_exp_ctx = NULL;
static void on_exp_ctx_menu(int item, void *ctx) {
    ExpState *st = (ExpState *)ctx;
    int nd;
    if (!st || st->sel < 0) return;
    nd = fs_child(st->cwd, st->sel);
    if (nd < 0) return;
    switch (item) {
    case 0:
        if (fs_is_dir(nd)) exp_nav(st, nd);
        else fs_open_any_file(nd);
        break;
    case 1:
        if (!fs_is_dir(nd)) notepad_open_file(nd);
        break;
    case 2: {
        char zn[64];
        zip_create_from_node(nd, st->cwd, zn, sizeof(zn));
        break;
    }
    case 3:
        st->clip_node = nd;
        toast("Объект скопирован в буфер Проводника (Ctrl+V — вставить).");
        break;
    case 4:
        st->dialog_mode = 3;
        strncpy(st->input, fs_name(nd), sizeof(st->input) - 1);
        st->in_len = (int)strlen(st->input);
        st->in_caret = st->in_len;
        break;
    case 5:
        fs_move_to_trash(nd);
        st->sel = -1;
        break;
    case 6:
        fs_remove(nd);
        st->sel = -1;
        break;
    }
    wm_invalidate();
}

static void exp_draw(Window *w, Rect r) {
    ExpState *st = (ExpState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect cmdbar = {r.x, r.y, r.w, 42};
    Rect addrbar = {r.x, r.y + 42, r.w, 38};
    Rect side = {r.x, r.y + 80, 192, r.h - 106};
    Rect main_area = {r.x + 192, r.y + 80, r.w - 192, r.h - 106};
    Rect status = {r.x, r.y + r.h - 26, r.w, 26};
    char path[192], statbuf[160];
    int i, cnt, y;

    static const struct { const char *lbl; const char *path; u32 ic; } navs[8] = {
        { "Этот ПК (C:\\)",  "C:\\",          ICON_COMPUTER },
        { "Рабочий стол",    "@desktop",      ICON_FOLDER   },
        { "Загрузки",        "@downloads",    ICON_DOWNLOAD },
        { "Документы",       "@documents",    ICON_TEXTFILE },
        { "Изображения",     "@pictures",     ICON_IMAGE    },
        { "C:\\YoroOS",      "C:\\YoroOS",    ICON_SHIELD   },
        { "Program Files",   "C:\\Program Files", ICON_STORE },
        { "Корзина",         "C:\\Recycle.Bin", ICON_TRASH  },
    };

    if (!fs_is_dir(st->cwd)) st->cwd = fs_root();

    fill_rect(r, theme->win_bg);

    /* Top Windows 11 Command Bar */
    fill_rect(cmdbar, theme->card_bg);
    ui_button((Rect){r.x + 8,   r.y + 6, 126, 30}, "+ Новая папка", pt_in(mx, my, (Rect){r.x + 8,   r.y + 6, 126, 30}), 0);
    ui_button((Rect){r.x + 140, r.y + 6, 122, 30}, "+ Новый файл",  pt_in(mx, my, (Rect){r.x + 140, r.y + 6, 122, 30}), 0);
    ui_button((Rect){r.x + 268, r.y + 6, 122, 30}, "В архив .ZIP",  pt_in(mx, my, (Rect){r.x + 268, r.y + 6, 122, 30}), 0);
    ui_button((Rect){r.x + 396, r.y + 6, 98,  30}, "Открыть",       pt_in(mx, my, (Rect){r.x + 396, r.y + 6, 98,  30}), 1);
    ui_button((Rect){r.x + 500, r.y + 6, 106, 30}, "В Корзину",     pt_in(mx, my, (Rect){r.x + 500, r.y + 6, 106, 30}), 0);
    ui_button_danger((Rect){r.x + 612, r.y + 6, 160, 30}, "Удалить навсегда", pt_in(mx, my, (Rect){r.x + 612, r.y + 6, 160, 30}));
    hline(r.x, r.x + r.w - 1, r.y + 41, RGBA(130, 145, 170, 55));

    /* Address & Navigation Bar */
    fill_rect(addrbar, theme->win_bg);
    ui_icon_button((Rect){r.x + 8,  addrbar.y + 4, 30, 30}, ICON_BACK, pt_in(mx, my, (Rect){r.x + 8,  addrbar.y + 4, 30, 30}));
    ui_icon_button((Rect){r.x + 42, addrbar.y + 4, 30, 30}, ICON_UP,   pt_in(mx, my, (Rect){r.x + 42, addrbar.y + 4, 30, 30}));
    fs_path(st->cwd, path, sizeof(path));
    ui_textfield((Rect){r.x + 80, addrbar.y + 4, r.w - (fs_windows_present() ? 92 : 244), 30}, path, -1, 0, "C:\\");
    if (!fs_windows_present()) {
        ui_button((Rect){r.x + r.w - 156, addrbar.y + 4, 146, 30}, "Восстановить ОС",
                  pt_in(mx, my, (Rect){r.x + r.w - 156, addrbar.y + 4, 146, 30}), 1);
    }
    hline(r.x, r.x + r.w - 1, addrbar.y + 37, RGBA(130, 145, 170, 55));

    /* Left Quick-Access Sidebar */
    fill_rect(side, theme->card_bg);
    vline(side.x + side.w - 1, side.y, side.y + side.h - 1, RGBA(130, 145, 170, 55));
    for (i = 0; i < 8; i++) {
        Rect nr = {side.x + 6, side.y + 8 + i * 34, side.w - 12, 30};
        if (pt_in(mx, my, nr)) fill_round_rect_alpha(nr, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 48));
        icon_draw(navs[i].ic, nr.x + 8, nr.y + 5, 20);
        draw_text_clipped(nr.x + 34, nr.y + 6, nr.w - 40, navs[i].lbl, theme->text_main);
    }

    /* Main File Table Header + Rows */
    fill_rect_alpha((Rect){main_area.x, main_area.y, main_area.w, 26}, RGBA(130, 145, 170, 26));
    draw_text_small(main_area.x + 42, main_area.y + 5, "Имя", theme->text_muted);
    draw_text_small(main_area.x + main_area.w - 230, main_area.y + 5, "Тип", theme->text_muted);
    draw_text_small(main_area.x + main_area.w - 96, main_area.y + 5, "Размер", theme->text_muted);

    cnt = fs_count(st->cwd);
    y = main_area.y + 30 - w->scroll;
    {
        Rect saved = gfx_get_clip();
        gfx_set_clip((Rect){main_area.x, main_area.y + 26, main_area.w, main_area.h - 26});
        if (cnt == 0) {
            draw_text_center(main_area.x + main_area.w / 2, main_area.y + 90,
                             "Эта папка пуста.", theme->text_muted);
        }
        for (i = 0; i < cnt; i++) {
            int nd = fs_child(st->cwd, i);
            Rect row = {main_area.x + 6, y, main_area.w - 16, 32};
            const char *nm = fs_name(nd);
            const char *dot = strrchr(nm, '.');
            char szs[28];
            if (y + 32 >= main_area.y + 26 && y < main_area.y + main_area.h) {
                if (st->sel == i) {
                    fill_round_rect_alpha(row, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 70));
                    draw_round_rect(row, 5, theme->accent);
                } else if (pt_in(mx, my, row)) {
                    fill_round_rect_alpha(row, 5, RGBA(130, 145, 170, 30));
                }
                icon_draw(fs_is_dir(nd) ? ICON_FOLDER : fs_icon(nd), row.x + 8, row.y + 4, 24);
                draw_text_clipped(row.x + 38, row.y + 7, row.w - 275, nm, theme->text_main);
                draw_text_small(main_area.x + main_area.w - 230, row.y + 8,
                                fs_is_dir(nd) ? "Папка с файлами"
                                : (dot && !strcasecmp(dot, ".exe") ? "Приложение Win32 (.EXE)"
                                : (dot && !strcasecmp(dot, ".zip") ? "Сжатый архив (.ZIP)" : "Файл Windows")),
                                theme->text_muted);
                if (!fs_is_dir(nd)) {
                    int b = fs_size(nd);
                    if (b >= 1024) snprintf(szs, sizeof(szs), "%d.%d КБ", b / 1024, (b % 1024) * 10 / 1024);
                    else snprintf(szs, sizeof(szs), "%d Б", b);
                    draw_text_small(main_area.x + main_area.w - 96, row.y + 8, szs, theme->text_muted);
                }
            }
            y += 34;
        }
        gfx_set_clip(saved);
    }

    /* Status Footer */
    fill_rect(status, theme->card_bg);
    snprintf(statbuf, sizeof(statbuf), "Элементов: %d   |   Том C: (YoroFS2 — полный доступ без блокировок системных папок)", cnt);
    draw_text_small(status.x + 12, status.y + 5, statbuf, theme->text_muted);

    if (st->dialog_mode) {
        Rect dlg = {r.x + (r.w - 380) / 2, r.y + (r.h - 140) / 2, 380, 140};
        ui_dialog_input(dlg,
                        st->dialog_mode == 1 ? "Имя новой папки:"
                        : (st->dialog_mode == 2 ? "Имя нового файла:" : "Новое имя объекта:"),
                        st->input, st->in_caret, 1, theme->accent, theme->text_main);
    }
}

static void commit_exp_dialog(ExpState *st) {
    if (st->input[0]) {
        if (st->dialog_mode == 1) fs_mkdir(st->cwd, st->input);
        else if (st->dialog_mode == 2) fs_create(st->cwd, st->input, "");
        else if (st->dialog_mode == 3 && st->sel >= 0) {
            int nd = fs_child(st->cwd, st->sel);
            if (nd >= 0) fs_rename(nd, st->input);
        }
        desktop_rebuild();
    }
    st->dialog_mode = 0;
    wm_invalidate();
}

static void exp_event(Window *w, YoroEvent *e) {
    ExpState *st = (ExpState *)w->user;
    Rect r = wm_client(w);
    Rect side = {r.x, r.y + 80, 192, r.h - 106};
    Rect main_area = {r.x + 192, r.y + 80, r.w - 192, r.h - 106};
    int i;

    if (st->dialog_mode) {
        Rect dlg = {r.x + (r.w - 380) / 2, r.y + (r.h - 140) / 2, 380, 140};
        if (e->type == EV_KEY) {
            if (e->key == KEY_ESC) { st->dialog_mode = 0; wm_invalidate(); return; }
            if (e->key == KEY_ENTER) { commit_exp_dialog(st); return; }
            if (ui_textfield_edit(st->input, sizeof(st->input), &st->in_len, &st->in_caret, e))
                wm_invalidate();
        } else if (e->type == EV_MOUSE_DOWN && pt_in(e->x, e->y, (Rect){dlg.x + dlg.w - 118, dlg.y + dlg.h - 44, 100, 30})) {
            commit_exp_dialog(st);
        }
        return;
    }

    if (e->type == EV_WHEEL) {
        w->scroll -= e->wheel * 34;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
        return;
    }

    if (e->type == EV_KEY) {
        int nd = st->sel >= 0 ? fs_child(st->cwd, st->sel) : -1;
        if (e->key == KEY_BACKSP && fs_parent(st->cwd) >= 0) exp_nav(st, fs_parent(st->cwd));
        else if (e->key == KEY_ENTER && nd >= 0) {
            if (fs_is_dir(nd)) exp_nav(st, nd);
            else fs_open_any_file(nd);
        } else if (e->key == KEY_DEL && nd >= 0) {
            if (e->shift) fs_remove(nd);
            else fs_move_to_trash(nd);
            st->sel = -1;
            wm_invalidate();
        } else if (e->key == KEY_F2 && nd >= 0) {
            st->dialog_mode = 3;
            strncpy(st->input, fs_name(nd), sizeof(st->input) - 1);
            st->in_len = (int)strlen(st->input);
            st->in_caret = st->in_len;
            wm_invalidate();
        } else if (e->ctrl && (e->ch == 'c' || e->ch == 'C') && nd >= 0) {
            st->clip_node = nd;
        } else if (e->ctrl && (e->ch == 'v' || e->ch == 'V') && st->clip_node >= 0) {
            fs_copy_node(st->clip_node, st->cwd);
            wm_invalidate();
        }
        return;
    }

    if (e->type == EV_MOUSE_DOWN) {
        int nd = st->sel >= 0 ? fs_child(st->cwd, st->sel) : -1;
        /* Command Bar Buttons */
        if (pt_in(e->x, e->y, (Rect){r.x + 8, r.y + 6, 126, 30})) {
            st->dialog_mode = 1; strcpy(st->input, "Новая папка");
            st->in_len = (int)strlen(st->input); st->in_caret = st->in_len; wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 140, r.y + 6, 122, 30})) {
            st->dialog_mode = 2; strcpy(st->input, "заметка.txt");
            st->in_len = (int)strlen(st->input); st->in_caret = st->in_len; wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 268, r.y + 6, 122, 30}) && nd >= 0) {
            char zn[64];
            zip_create_from_node(nd, st->cwd, zn, sizeof(zn));
            wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 396, r.y + 6, 98, 30}) && nd >= 0) {
            if (fs_is_dir(nd)) exp_nav(st, nd);
            else fs_open_any_file(nd);
            return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 500, r.y + 6, 106, 30}) && nd >= 0) {
            fs_move_to_trash(nd); st->sel = -1; wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 612, r.y + 6, 160, 30}) && nd >= 0) {
            fs_remove(nd); st->sel = -1; wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 8, r.y + 46, 30, 30}) && st->nhist > 0) {
            st->cwd = st->hist[--st->nhist]; st->sel = -1; wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 42, r.y + 46, 30, 30}) && fs_parent(st->cwd) >= 0) {
            exp_nav(st, fs_parent(st->cwd)); return;
        }
        if (!fs_windows_present() && pt_in(e->x, e->y, (Rect){r.x + r.w - 156, r.y + 46, 146, 30})) {
            fs_restore_system_files(); return;
        }

        /* Sidebar clicks */
        for (i = 0; i < 8; i++) {
            Rect nr = {side.x + 6, side.y + 8 + i * 34, side.w - 12, 30};
            if (pt_in(e->x, e->y, nr)) {
                int target = -1;
                if (i == 0) target = fs_root();
                else if (i == 1) target = fs_desktop_dir();
                else if (i == 2) target = fs_downloads_dir();
                else if (i == 3) target = fs_documents_dir();
                else if (i == 4) target = fs_pictures_dir();
                else if (i == 5) target = fs_parse_path(fs_root(), "C:\\YoroOS");
                else if (i == 6) target = fs_parse_path(fs_root(), "C:\\Program Files");
                else if (i == 7) target = fs_trash_dir();
                if (target >= 0) exp_nav(st, target);
                else toast("Эта папка была удалена вами! Нажмите «Восстановить ОС» для возврата.");
                return;
            }
        }

        /* File rows */
        if (pt_in(e->x, e->y, (Rect){main_area.x, main_area.y + 26, main_area.w, main_area.h - 26})) {
            int idx = (e->y - (main_area.y + 30 - w->scroll)) / 34;
            int cnt = fs_count(st->cwd);
            if (idx >= 0 && idx < cnt) {
                st->sel = idx;
                if (e->btn == 1) {
                    u32 now = sys_ticks();
                    if (st->last_idx == idx && now - st->last_click < 45) {
                        int item = fs_child(st->cwd, idx);
                        st->last_click = 0;
                        if (fs_is_dir(item)) exp_nav(st, item);
                        else fs_open_any_file(item);
                    } else {
                        st->last_idx = idx;
                        st->last_click = now;
                    }
                } else if (e->btn == 2) {
                    static const char *menu[] = {
                        "Открыть / Запустить",
                        "Открыть в Блокноте",
                        "Сжать в .ZIP архив",
                        "Копировать",
                        "Переименовать",
                        "Переместить в Корзину",
                        "Удалить безвозвратно"
                    };
                    g_exp_ctx = st;
                    (void)g_exp_ctx;
                    ctx_menu_open(e->x, e->y, menu, 7, on_exp_ctx_menu, st);
                }
            } else {
                st->sel = -1;
            }
            wm_invalidate();
        }
    }
}

static void exp_close(Window *w) { kfree(w->user); }

void explorer_open_path(const char *path) {
    ExpState *st = (ExpState *)kzalloc(sizeof(ExpState));
    int target;
    if (!st) return;
    target = path ? fs_parse_path(fs_root(), path) : fs_root();
    st->cwd = (target >= 0 && fs_is_dir(target)) ? target : fs_root();
    st->sel = -1;
    st->clip_node = -1;
    wm_open(app_find("explorer"), "Проводник — YoroFS", -1, -1, 830, 525, exp_draw, exp_event, exp_close)->user = st;
}

void explorer_open(void) { explorer_open_path("C:\\"); }
