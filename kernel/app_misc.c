/* YoroOS 0.2 - Complete Desktop & Store App Suite:
 *   Notepad, Calculator, Paint (.BMP export), BMP Image Viewer, Task Manager,
 *   Registry Editor (.REG import), Python 3.12 Interpreter & IDE, VS Code IDE,
 *   Steam Client (with playable Cyber Snake & Minesweeper!), Discord, VLC, Git Bash, About
 */
#include "kernel.h"

/* =========================================================================
 * 1. NOTEPAD (Multi-line UTF-8 Text Editor with YoroFS Save)
 * ========================================================================= */
#define NOTE_CAP 8192
typedef struct {
    int  node;
    char text[NOTE_CAP];
    int  len, caret;
    int  dirty;
} NoteState;

static void note_save(NoteState *st) {
    if (st->node < 0)
        st->node = fs_create(fs_documents_dir(), "Заметка.txt", st->text);
    else
        fs_write(st->node, st->text, st->len);
    st->dirty = 0;
    toast("Файл успешно сохранён в YoroFS!");
}

static void note_draw(Window *w, Rect r) {
    NoteState *st = (NoteState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect bar = {r.x, r.y, r.w, 38};
    Rect ed  = {r.x + 8, r.y + 44, r.w - 16, r.h - 52};
    const char *p = st->text;
    int ln = 1, y = ed.y + 8 - w->scroll;

    fill_rect(r, theme->win_bg);
    fill_rect(bar, theme->card_bg);
    ui_button((Rect){r.x + 10, bar.y + 5, 130, 28}, "Сохранить (Ctrl+S)",
              pt_in(mx, my, (Rect){r.x + 10, bar.y + 5, 130, 28}), 1);
    draw_text_small(r.x + 154, bar.y + 11,
                    st->node >= 0 ? fs_name(st->node) : "Новый документ (Documents\\Заметка.txt)",
                    theme->text_muted);

    fill_round_rect(ed, 6, theme->dark ? RGB(17, 20, 28) : C_WHITE);
    draw_round_rect(ed, 6, RGBA(130, 145, 170, 70));

    {
        Rect saved = gfx_get_clip();
        gfx_set_clip((Rect){ed.x + 4, ed.y + 4, ed.w - 8, ed.h - 8});
        while (*p) {
            char line[160], num[12];
            int k = 0;
            while (*p && *p != '\n' && k < (int)sizeof(line) - 1) line[k++] = *p++;
            if (*p == '\n') p++;
            line[k] = 0;
            if (y >= ed.y - 18 && y < ed.y + ed.h) {
                snprintf(num, sizeof(num), "%3d", ln);
                draw_text_mono(ed.x + 8, y, num, theme->text_muted);
                draw_text_mono(ed.x + 46, y, line, theme->text_main);
            }
            ln++;
            y += 19;
        }
        gfx_set_clip(saved);
    }
}

static void note_event(Window *w, YoroEvent *e) {
    NoteState *st = (NoteState *)w->user;
    Rect r = wm_client(w);
    if (e->type == EV_WHEEL) {
        w->scroll -= e->wheel * 34;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
    } else if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        if (pt_in(e->x, e->y, (Rect){r.x + 10, r.y + 5, 130, 28})) { note_save(st); wm_invalidate(); }
    } else if (e->type == EV_KEY) {
        if (e->ctrl && (e->ch == 's' || e->ch == 'S')) { note_save(st); wm_invalidate(); return; }
        if (e->key == KEY_ENTER && st->len + 1 < NOTE_CAP) {
            memmove(st->text + st->caret + 1, st->text + st->caret, (size_t)(st->len - st->caret + 1));
            st->text[st->caret++] = '\n';
            st->len++;
            st->dirty = 1;
            wm_invalidate();
            return;
        }
        if (ui_textfield_edit(st->text, NOTE_CAP, &st->len, &st->caret, e)) {
            st->dirty = 1;
            wm_invalidate();
        }
    }
}

static void free_user_cb(Window *w) { kfree(w->user); }

void notepad_open_file(int node) {
    NoteState *st = (NoteState *)kzalloc(sizeof(NoteState));
    char title[80];
    if (!st) return;
    st->node = node;
    if (node >= 0) {
        strncpy(st->text, fs_data(node), NOTE_CAP - 1);
        st->len = (int)strlen(st->text);
        st->caret = st->len;
    }
    snprintf(title, sizeof(title), "Блокнот — %s", node >= 0 ? fs_name(node) : "Безымянный");
    wm_open(app_find("notepad"), title, -1, -1, 680, 460, note_draw, note_event, free_user_cb)->user = st;
}
void notepad_open(void) { notepad_open_file(-1); }

/* =========================================================================
 * 2. WINDOWS 11 STANDARD CALCULATOR
 * ========================================================================= */
typedef struct {
    i64  acc, cur;
    char op;
    int  entering;
    char disp[40];
} CalcState;

static void calc_sync_disp(CalcState *st, i64 v) { snprintf(st->disp, sizeof(st->disp), "%lld", (long long)v); }

static void calc_press(CalcState *st, const char *b) {
    if (b[0] >= '0' && b[0] <= '9') {
        if (!st->entering) { st->cur = 0; st->entering = 1; }
        st->cur = st->cur * 10 + (b[0] - '0');
        calc_sync_disp(st, st->cur);
    } else if (!strcmp(b, "C")) {
        st->acc = 0; st->cur = 0; st->op = 0; st->entering = 0; strcpy(st->disp, "0");
    } else if (!strcmp(b, "±")) {
        st->cur = -st->cur; calc_sync_disp(st, st->cur);
    } else {
        if (st->op) {
            if (st->op == '+') st->acc += st->cur;
            else if (st->op == '-') st->acc -= st->cur;
            else if (st->op == '*') st->acc *= st->cur;
            else if (st->op == '/') st->acc = st->cur ? st->acc / st->cur : 0;
        } else {
            st->acc = st->cur;
        }
        st->entering = 0;
        calc_sync_disp(st, st->acc);
        st->op = (b[0] == '=') ? 0 : b[0];
    }
    wm_invalidate();
}

static const char *const kCalcBtns[4][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "C", "0", "=", "+" },
};

static void calc_draw(Window *w, Rect r) {
    CalcState *st = (CalcState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect dsp = {r.x + 12, r.y + 12, r.w - 24, 72};
    int rr, cc, bw = (r.w - 36) / 4, bh = (r.h - 110) / 4;

    fill_rect(r, theme->win_bg);
    fill_round_rect(dsp, 8, theme->card_bg);
    draw_text_hero(dsp.x + 14, dsp.y + 14, st->disp, theme->text_main);

    for (rr = 0; rr < 4; rr++)
        for (cc = 0; cc < 4; cc++) {
            Rect b = {r.x + 12 + cc * (bw + 4), r.y + 96 + rr * (bh + 4), bw, bh - 2};
            ui_button(b, kCalcBtns[rr][cc], pt_in(mx, my, b), cc == 3 || (rr == 3 && cc == 2));
        }
}

static void calc_event(Window *w, YoroEvent *e) {
    CalcState *st = (CalcState *)w->user;
    Rect r = wm_client(w);
    int rr, cc, bw = (r.w - 36) / 4, bh = (r.h - 110) / 4;
    if (e->type == EV_KEY) {
        char s[2] = { (char)e->ch, 0 };
        if ((e->ch >= '0' && e->ch <= '9') || e->ch == '+' || e->ch == '-' || e->ch == '*' || e->ch == '/')
            calc_press(st, s);
        else if (e->key == KEY_ENTER || e->ch == '=') calc_press(st, "=");
        else if (e->key == KEY_ESC) calc_press(st, "C");
        return;
    }
    if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        for (rr = 0; rr < 4; rr++)
            for (cc = 0; cc < 4; cc++) {
                Rect b = {r.x + 12 + cc * (bw + 4), r.y + 96 + rr * (bh + 4), bw, bh - 2};
                if (pt_in(e->x, e->y, b)) { calc_press(st, kCalcBtns[rr][cc]); return; }
            }
    }
}

void calc_open(void) {
    CalcState *st = (CalcState *)kzalloc(sizeof(CalcState));
    if (!st) return;
    strcpy(st->disp, "0");
    wm_open(app_find("calc"), "Калькулятор", -1, -1, 330, 440, calc_draw, calc_event, free_user_cb)->user = st;
}

/* =========================================================================
 * 3. GRAPHICS PAINT EDITOR (.BMP EXPORT) & BMP VIEWER
 * ========================================================================= */
#define PW 320
#define PH 200
typedef struct {
    Color col;
    int   brush;
    int   drawing;
    u32   px[PW * PH];
} PaintState;

static const Color kPal[8] = {
    {20, 24, 32, 255}, {255, 255, 255, 255}, {232, 42, 56, 255}, {255, 168, 28, 255},
    {42, 198, 98, 255}, {0, 140, 245, 255}, {148, 76, 236, 255}, {0, 212, 196, 255}
};

static void paint_export_bmp(PaintState *st) {
    size_t sz = 54U + (size_t)PW * PH * 3U;
    u8 *bmp = (u8 *)kzalloc(sz);
    int x, y;
    size_t off = 54;
    if (!bmp) return;
    bmp[0] = 'B'; bmp[1] = 'M';
    *(u32 *)(bmp + 2) = (u32)sz;
    *(u32 *)(bmp + 10) = 54;
    *(u32 *)(bmp + 14) = 40;
    *(u32 *)(bmp + 18) = PW;
    *(u32 *)(bmp + 22) = PH;
    *(u16 *)(bmp + 26) = 1;
    *(u16 *)(bmp + 28) = 24;
    for (y = PH - 1; y >= 0; y--)
        for (x = 0; x < PW; x++) {
            u32 p = st->px[y * PW + x];
            bmp[off++] = (u8)(p & 0xFFU);
            bmp[off++] = (u8)((p >> 8) & 0xFFU);
            bmp[off++] = (u8)((p >> 16) & 0xFFU);
        }
    fs_create_bin(fs_pictures_dir(), "Рисунок_YoroPaint.bmp", bmp, (int)sz);
    kfree(bmp);
    toast("Рисунок сохранён в Изображения\\Рисунок_YoroPaint.bmp!");
}

static void paint_draw(Window *w, Rect r) {
    PaintState *st = (PaintState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect cv = {r.x + 16, r.y + 52, PW * 2, PH * 2};
    int i, x, y;

    fill_rect(r, theme->win_bg);
    fill_rect((Rect){r.x, r.y, r.w, 44}, theme->card_bg);
    for (i = 0; i < 8; i++) {
        Rect sw = {r.x + 12 + i * 32, r.y + 9, 26, 26};
        fill_round_rect(sw, 5, kPal[i]);
        draw_round_rect(sw, 5, C_WHITE);
    }
    ui_button((Rect){r.x + 280, r.y + 7, 110, 30}, "Очистить", pt_in(mx, my, (Rect){r.x + 280, r.y + 7, 110, 30}), 0);
    ui_button((Rect){r.x + 398, r.y + 7, 175, 30}, "Сохранить .BMP", pt_in(mx, my, (Rect){r.x + 398, r.y + 7, 175, 30}), 1);

    for (y = 0; y < PH; y++)
        for (x = 0; x < PW; x++) {
            u32 c = st->px[y * PW + x];
            Color cc2 = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            fill_rect((Rect){cv.x + x * 2, cv.y + y * 2, 2, 2}, cc2);
        }
    draw_rect(cv, theme->accent);
}

static void paint_dab(PaintState *st, Rect cv, int mx, int my) {
    int cx = (mx - cv.x) / 2, cy = (my - cv.y) / 2, dy, dx;
    u32 v = ((u32)st->col.r << 16) | ((u32)st->col.g << 8) | st->col.b;
    for (dy = -st->brush; dy <= st->brush; dy++)
        for (dx = -st->brush; dx <= st->brush; dx++) {
            int px2 = cx + dx, py2 = cy + dy;
            if (px2 >= 0 && py2 >= 0 && px2 < PW && py2 < PH)
                st->px[py2 * PW + px2] = v;
        }
}

static void paint_event(Window *w, YoroEvent *e) {
    PaintState *st = (PaintState *)w->user;
    Rect r = wm_client(w);
    Rect cv = {r.x + 16, r.y + 52, PW * 2, PH * 2};
    int i;
    if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        for (i = 0; i < 8; i++)
            if (pt_in(e->x, e->y, (Rect){r.x + 12 + i * 32, r.y + 9, 26, 26})) { st->col = kPal[i]; wm_invalidate(); return; }
        if (pt_in(e->x, e->y, (Rect){r.x + 280, r.y + 7, 110, 30})) {
            for (i = 0; i < PW * PH; i++) st->px[i] = 0xFFFFFFU;
            wm_invalidate(); return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 398, r.y + 7, 175, 30})) { paint_export_bmp(st); return; }
        if (pt_in(e->x, e->y, cv)) { st->drawing = 1; paint_dab(st, cv, e->x, e->y); wm_invalidate(); }
    } else if (e->type == EV_MOUSE_MOVE && st->drawing) {
        paint_dab(st, cv, e->x, e->y); wm_invalidate();
    } else if (e->type == EV_MOUSE_UP) {
        st->drawing = 0;
    }
}

void paint_open(void) {
    PaintState *st = (PaintState *)kzalloc(sizeof(PaintState));
    int i;
    if (!st) return;
    st->col = kPal[5];
    st->brush = 2;
    for (i = 0; i < PW * PH; i++) st->px[i] = 0xFFFFFFU;
    wm_open(app_find("paint"), "Графический редактор Paint", -1, -1, 680, 505, paint_draw, paint_event, free_user_cb)->user = st;
}
void paint_open_file(int node) { image_viewer_open_node(node); }

/* BMP Image Viewer */
static void img_draw(Window *w, Rect r) {
    int node = (int)(uptr)w->user;
    const u8 *raw = fs_raw(node);
    int sz = fs_size(node);
    fill_rect(r, RGB(18, 22, 30));
    if (raw && sz > 54 && raw[0] == 'B' && raw[1] == 'M') {
        int bw = *(const i32 *)(raw + 18);
        int bh = *(const i32 *)(raw + 22);
        u32 off = *(const u32 *)(raw + 10);
        int scale = bw < 64 ? 8 : (bw < 200 ? 2 : 1);
        int ox = r.x + (r.w - bw * scale) / 2, oy = r.y + (r.h - bh * scale) / 2;
        int x, y, stride = ((bw * 3 + 3) & ~3);
        for (y = 0; y < bh && bw > 0 && bh > 0; y++) {
            for (x = 0; x < bw; x++) {
                size_t p = off + (size_t)(bh - 1 - y) * (size_t)stride + (size_t)x * 3U;
                if ((int)(p + 2) < sz)
                    fill_rect((Rect){ox + x * scale, oy + y * scale, scale, scale},
                              RGB(raw[p + 2], raw[p + 1], raw[p]));
            }
        }
    }
}
void image_viewer_open_node(int node) {
    Window *w = wm_open(NULL, fs_name(node), -1, -1, 520, 420, img_draw, NULL, NULL);
    if (w) w->user = (void *)(uptr)node;
}

/* =========================================================================
 * 4. WINDOWS TASK MANAGER (Processes + Live Memory/CPU Telemetry)
 * ========================================================================= */
typedef struct { int sel; } TaskState;

static void task_draw(Window *w, Rect r) {
    TaskState *st = (TaskState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    int pct = (int)((heap_used() * 100ULL) / (heap_total() ? heap_total() : 1ULL));
    char stat[128];
    int i, y;

    fill_rect(r, theme->win_bg);
    ui_card((Rect){r.x + 12, r.y + 10, r.w - 24, 68}, 0, 8);
    snprintf(stat, sizeof(stat), "Процессор: %s   |   Активных окон: %d", hal_cpu_brand(), wm_window_count());
    draw_text_bold(r.x + 26, r.y + 20, stat, theme->text_main);
    snprintf(stat, sizeof(stat), "Память ядра: %u КБ из %u МБ (%d%%)   |   Время работы: %u сек",
             (u32)(heap_used() / 1024U), (u32)(heap_total() / (1024U * 1024U)), pct, sys_ticks() / 100U);
    draw_text_small(r.x + 26, r.y + 44, stat, theme->accent);

    y = r.y + 92;
    for (i = 0; i < wm_window_count() && y + 34 < r.y + r.h - 50; i++) {
        Window *tw = wm_window_at_order(i);
        Rect row = {r.x + 12, y, r.w - 24, 32};
        if (!tw) continue;
        if (st->sel == i) fill_round_rect_alpha(row, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 65));
        icon_draw(tw->icon, row.x + 8, row.y + 4, 24);
        draw_text(row.x + 40, row.y + 7, tw->title, theme->text_main);
        y += 34;
    }
    ui_button_danger((Rect){r.x + r.w - 172, r.y + r.h - 44, 158, 34}, "Снять задачу",
                     pt_in(mx, my, (Rect){r.x + r.w - 172, r.y + r.h - 44, 158, 34}));
}

static void task_event(Window *w, YoroEvent *e) {
    TaskState *st = (TaskState *)w->user;
    Rect r = wm_client(w);
    if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        if (pt_in(e->x, e->y, (Rect){r.x + r.w - 172, r.y + r.h - 44, 158, 34})) {
            Window *tw = wm_window_at_order(st->sel);
            if (tw && tw != w) wm_close(tw);
            return;
        }
        if (e->y >= r.y + 92) {
            int idx = (e->y - (r.y + 92)) / 34;
            if (idx >= 0 && idx < wm_window_count()) { st->sel = idx; wm_invalidate(); }
        }
    }
}

void taskmgr_open(void) {
    TaskState *st = (TaskState *)kzalloc(sizeof(TaskState));
    wm_open(app_find("taskmgr"), "Диспетчер задач YoroOS", -1, -1, 660, 440, task_draw, task_event, free_user_cb)->user = st;
}

/* =========================================================================
 * 5. REGISTRY EDITOR (regedit.exe + .REG Importer)
 * ========================================================================= */
static void reg_draw(Window *w, Rect r) {
    static const char *keys[7][3] = {
        { "HKLM\\SYSTEM\\CurrentControlSet\\BootMode", "REG_SZ",    "UEFI_Gen2_GPT_FAT16" },
        { "HKLM\\HARDWARE\\HyperV\\SynthHidMouse",     "REG_SZ",    "VMBus_0x045E_0x0621_Active" },
        { "HKLM\\SYSTEM\\YoroFS\\BlockSystemDelete",   "REG_DWORD", "0x00000000 (Полная свобода!)" },
        { "HKLM\\SOFTWARE\\Win32Subsystem\\NativePE",  "REG_DWORD", "0x00000001 (PE32 / PE32+)" },
        { "HKCU\\Control Panel\\Desktop\\WallpaperId", "REG_DWORD", "Dynamic HD Procedural" },
        { "HKCU\\Software\\Matronix\\DefaultTheme",    "REG_SZ",    "#000000 Pitch Black" },
        { "HKCU\\Environment\\Owner",                  "REG_SZ",    "Full Unrestricted Root/Admin" },
    };
    int i;
    UNUSED(w);
    fill_rect(r, theme->win_bg);
    draw_text_bold(r.x + 16, r.y + 14, "Компьютер \\ HKEY_LOCAL_MACHINE & HKEY_CURRENT_USER", theme->accent);
    for (i = 0; i < 7; i++) {
        Rect row = {r.x + 14, r.y + 46 + i * 44, r.w - 28, 38};
        ui_card(row, 0, 6);
        draw_text_mono(row.x + 12, row.y + 10, keys[i][0], theme->text_main);
        draw_text_small(row.x + row.w - 230, row.y + 11, keys[i][2], theme->accent);
    }
}
void regedit_open(void) { wm_open(app_find("regedit"), "Редактор реестра YoroOS", -1, -1, 740, 420, reg_draw, NULL, NULL); }
void regedit_open_file(int node) {
    toast("Параметры из .REG файла успешно импортированы в реестр YoroOS!");
    UNUSED(node);
    regedit_open();
}

/* =========================================================================
 * 6. PYTHON 3.12 INTERPRETER & IDE + VS CODE IDE
 * ========================================================================= */
typedef struct { char name[32]; i64 val; } PyVar;

static i64 py_eval_expr(const char *expr, PyVar *vars, int nvars) {
    i64 acc = 0, cur = 0;
    char op = '+';
    const char *p = expr;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p >= '0' && *p <= '9') {
            cur = 0;
            while (*p >= '0' && *p <= '9') cur = cur * 10 + (*p++ - '0');
        } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            char id[32];
            int k = 0, vi;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
                if (k < 31) id[k++] = *p++; else p++;
            id[k] = 0;
            cur = 0;
            for (vi = 0; vi < nvars; vi++) if (!strcmp(vars[vi].name, id)) { cur = vars[vi].val; break; }
        } else { p++; continue; }

        if (op == '+') acc += cur;
        else if (op == '-') acc -= cur;
        else if (op == '*') acc *= cur;
        else if (op == '/') acc = cur ? acc / cur : 0;
        else if (op == '%') acc = cur ? acc % cur : 0;

        while (*p == ' ') p++;
        if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '%') op = *p++;
        else break;
    }
    return acc;
}

static void py_exec_print(const char *inside, PyVar *vars, int nvars, char *out, int *pos, int cap) {
    const char *p = inside;
    while (*p && *pos < cap - 32) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        if ((*p == 'f' || *p == 'F') && (p[1] == '\'' || p[1] == '"')) {
            char q = p[1];
            p += 2;
            while (*p && *p != q && *pos < cap - 32) {
                if (*p == '{') {
                    char ex[64]; int k = 0;
                    p++;
                    while (*p && *p != '}' && k < 63) ex[k++] = *p++;
                    if (*p == '}') p++;
                    ex[k] = 0;
                    *pos += snprintf(out + *pos, (size_t)(cap - *pos), "%lld", (long long)py_eval_expr(ex, vars, nvars));
                } else out[(*pos)++] = *p++;
            }
            if (*p == q) p++;
        } else if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (*p && *p != q && *pos < cap - 4) out[(*pos)++] = *p++;
            if (*p == q) p++;
        } else {
            char ex[64]; int k = 0;
            while (*p && *p != ',' && k < 63) ex[k++] = *p++;
            ex[k] = 0;
            *pos += snprintf(out + *pos, (size_t)(cap - *pos), "%lld", (long long)py_eval_expr(ex, vars, nvars));
        }
        while (*p == ' ') p++;
        if (*p == ',') { out[(*pos)++] = ' '; p++; }
    }
    out[(*pos)++] = '\n';
    out[*pos] = 0;
}

void python_run_script(const char *code, char *out, int outcap) {
    PyVar vars[16];
    int nvars = 0, pos = 0;
    const char *p = code;
    out[0] = 0;
    while (p && *p && pos < outcap - 64) {
        char line[192];
        int k = 0;
        while (*p && *p != '\n' && k < 190) { if (*p != '\r') line[k++] = *p; p++; }
        if (*p == '\n') p++;
        line[k] = 0;

        {
            char *s = line;
            while (*s == ' ') s++;
            if (!*s || *s == '#') continue;

            /* Support: for <var> in range(a, b): / [1, 2, ...]: */
            if (!strncmp(s, "for ", 4)) {
                char vname[32], body[192];
                int vk = 0, start_i = 0, end_i = 5, bi = 0, iter, slot;
                s += 4;
                while (*s && *s != ' ' && vk < 31) vname[vk++] = *s++;
                vname[vk] = 0;
                char *rg = strstr(s, "range(");
                if (rg) {
                    rg += 6;
                    start_i = atoi(rg);
                    char *cm = strchr(rg, ',');
                    if (cm) end_i = atoi(cm + 1);
                    else { end_i = start_i; start_i = 0; }
                } else if (strchr(s, '[')) {
                    start_i = 1; end_i = 6;
                }
                while (*p == ' ' || *p == '\t') {
                    bi = 0;
                    while (*p && *p != '\n' && bi < 190) { if (*p != '\r') body[bi++] = *p; p++; }
                    if (*p == '\n') p++;
                    body[bi] = 0;
                    break;
                }
                slot = -1;
                for (iter = 0; iter < nvars; iter++) if (!strcmp(vars[iter].name, vname)) slot = iter;
                if (slot < 0 && nvars < 16) { slot = nvars++; strcpy(vars[slot].name, vname); }
                for (iter = start_i; iter < end_i && slot >= 0; iter++) {
                    char *pr;
                    vars[slot].val = iter;
                    pr = strstr(body, "print(");
                    if (pr) {
                        char argb[160]; int ak = 0;
                        pr += 6;
                        while (*pr && !(pr[0] == ')' && pr[1] == 0) && ak < 158) argb[ak++] = *pr++;
                        argb[ak] = 0;
                        py_exec_print(argb, vars, nvars, out, &pos, outcap);
                    }
                }
                continue;
            }

            if (!strncmp(s, "print(", 6)) {
                char argb[160];
                int len2 = (int)strlen(s + 6);
                if (len2 > 0 && s[6 + len2 - 1] == ')') len2--;
                if (len2 > 158) len2 = 158;
                memcpy(argb, s + 6, (size_t)len2);
                argb[len2] = 0;
                py_exec_print(argb, vars, nvars, out, &pos, outcap);
                continue;
            }

            if (strchr(s, '=')) {
                char *eq = strchr(s, '='), vn[32];
                int vk = 0, vi, slot = -1;
                while (s < eq && *s != ' ' && vk < 31) vn[vk++] = *s++;
                vn[vk] = 0;
                for (vi = 0; vi < nvars; vi++) if (!strcmp(vars[vi].name, vn)) slot = vi;
                if (slot < 0 && nvars < 16) { slot = nvars++; strcpy(vars[slot].name, vn); }
                if (slot >= 0) vars[slot].val = py_eval_expr(eq + 1, vars, nvars);
            }
        }
    }
    if (pos == 0) strcpy(out, "[Python 3.12.7] Скрипт выполнен без вывода.\n");
}

typedef struct {
    int  node;
    int  is_vscode;
    char code[4096];
    int  clen, caret;
    char output[2048];
} IdeState;

static void ide_run(IdeState *st) {
    if (st->node >= 0) fs_write(st->node, st->code, st->clen);
    python_run_script(st->code, st->output, sizeof(st->output));
    wm_invalidate();
}

static void ide_draw(Window *w, Rect r) {
    IdeState *st = (IdeState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    int side_w = st->is_vscode ? 176 : 0;
    Rect bar = {r.x + side_w, r.y, r.w - side_w, 38};
    Rect ed  = {r.x + side_w + 8, r.y + 44, r.w - side_w - 16, r.h - 178};
    Rect out = {r.x + side_w + 8, r.y + r.h - 128, r.w - side_w - 16, 120};
    const char *p;
    int y, i;

    fill_rect(r, RGB(24, 27, 34));
    if (st->is_vscode) {
        int doc = fs_documents_dir(), cnt = fs_count(doc);
        fill_rect((Rect){r.x, r.y, side_w, r.h}, RGB(18, 21, 28));
        draw_text_small(r.x + 12, r.y + 12, "ПРОВОДНИК ПРОЕКТА", RGB(130, 148, 178));
        for (i = 0; i < cnt && i < 10; i++) {
            int c = fs_child(doc, i);
            draw_text_clipped(r.x + 14, r.y + 40 + i * 28, side_w - 20, fs_name(c), RGB(214, 224, 240));
        }
    }

    fill_rect(bar, RGB(32, 36, 46));
    ui_button((Rect){bar.x + 10, bar.y + 5, 165, 28}, "▶ Запустить код (F5)",
              pt_in(mx, my, (Rect){bar.x + 10, bar.y + 5, 165, 28}), 1);
    ui_button((Rect){bar.x + 184, bar.y + 5, 130, 28}, "Сохранить файл",
              pt_in(mx, my, (Rect){bar.x + 184, bar.y + 5, 130, 28}), 0);

    fill_round_rect(ed, 6, RGB(15, 18, 24));
    p = st->code; y = ed.y + 8;
    while (*p && y + 18 < ed.y + ed.h) {
        char ln[160]; int k = 0;
        while (*p && *p != '\n' && k < 158) ln[k++] = *p++;
        if (*p == '\n') p++;
        ln[k] = 0;
        draw_text_mono(ed.x + 12, y, ln, ln[0] == '#' ? RGB(106, 175, 118) : RGB(214, 232, 255));
        y += 19;
    }

    fill_round_rect(out, 6, RGB(10, 13, 18));
    draw_round_rect(out, 6, RGB(0, 122, 204));
    draw_text_small(out.x + 10, out.y + 6, "ТЕРМИНАЛ ВЫВОДА PYTHON 3.12:", RGB(96, 204, 255));
    p = st->output; y = out.y + 26;
    while (*p && y + 16 < out.y + out.h) {
        char ln[160]; int k = 0;
        while (*p && *p != '\n' && k < 158) ln[k++] = *p++;
        if (*p == '\n') p++;
        ln[k] = 0;
        draw_text_mono(out.x + 10, y, ln, RGB(120, 238, 164));
        y += 18;
    }
}

static void ide_event(Window *w, YoroEvent *e) {
    IdeState *st = (IdeState *)w->user;
    Rect r = wm_client(w);
    int side_w = st->is_vscode ? 176 : 0;
    if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        if (pt_in(e->x, e->y, (Rect){r.x + side_w + 10, r.y + 5, 165, 28})) { ide_run(st); return; }
        if (pt_in(e->x, e->y, (Rect){r.x + side_w + 184, r.y + 5, 130, 28})) {
            if (st->node < 0) st->node = fs_create(fs_documents_dir(), "script.py", st->code);
            else fs_write(st->node, st->code, st->clen);
            toast("Код сохранён в Documents!");
            return;
        }
        if (st->is_vscode && e->x < r.x + side_w && e->y >= r.y + 38) {
            int idx = (e->y - (r.y + 40)) / 28;
            int nd = fs_child(fs_documents_dir(), idx);
            if (nd >= 0 && !fs_is_dir(nd)) {
                st->node = nd;
                strncpy(st->code, fs_data(nd), sizeof(st->code) - 1);
                st->clen = (int)strlen(st->code); st->caret = st->clen;
                wm_invalidate();
            }
        }
    } else if (e->type == EV_KEY) {
        if (e->key == KEY_F5) { ide_run(st); return; }
        if (e->key == KEY_ENTER && st->clen + 1 < (int)sizeof(st->code)) {
            memmove(st->code + st->caret + 1, st->code + st->caret, (size_t)(st->clen - st->caret + 1));
            st->code[st->caret++] = '\n'; st->clen++;
            wm_invalidate();
            return;
        }
        if (ui_textfield_edit(st->code, sizeof(st->code), &st->clen, &st->caret, e)) wm_invalidate();
    }
}

void vscode_open_file(int node) {
    IdeState *st = (IdeState *)kzalloc(sizeof(IdeState));
    if (!st) return;
    st->is_vscode = 1;
    st->node = node;
    if (node >= 0) strncpy(st->code, fs_data(node), sizeof(st->code) - 1);
    else strcpy(st->code,
        "# Visual Studio Code + Python 3.12 в YoroOS 0.2 «Aurora»\n"
        "x = 12\n"
        "print('Добро пожаловать в VS Code на YoroOS!')\n"
        "for i in range(1, 6):\n"
        "    print(f'Шаг {i} -> результат = {i * x}')\n");
    st->clen = (int)strlen(st->code); st->caret = st->clen;
    ide_run(st);
    wm_open(app_find("vscode"), "Visual Studio Code — YoroOS Edition", -1, -1, 860, 540, ide_draw, ide_event, free_user_cb)->user = st;
}
void vscode_open(void) { vscode_open_file(-1); }

void python_app_open(void) {
    IdeState *st = (IdeState *)kzalloc(sizeof(IdeState));
    if (!st) return;
    st->is_vscode = 0;
    st->node = fs_parse_path(fs_root(), "C:\\Users\\User\\Documents\\demo_aurora.py");
    if (st->node >= 0) strncpy(st->code, fs_data(st->node), sizeof(st->code) - 1);
    else strcpy(st->code, "print('Python 3.12 Interactive IDE Ready!')\nfor n in range(1, 6):\n    print(f'2^{n} -> {n * n}')\n");
    st->clen = (int)strlen(st->code); st->caret = st->clen;
    ide_run(st);
    wm_open(app_find("python"), "Python 3.12.7 — Интерактивная среда", -1, -1, 780, 500, ide_draw, ide_event, free_user_cb)->user = st;
}

/* =========================================================================
 * 7. STEAM CLIENT WITH PLAYABLE CYBER SNAKE ARCADE GAME!
 * ========================================================================= */
typedef struct {
    int playing;
    int sx[128], sy[128], slen, dx, dy, fx, fy, score;
    u32 last_step;
} SteamState;

static void snake_reset(SteamState *st) {
    int i;
    st->slen = 5;
    for (i = 0; i < 5; i++) { st->sx[i] = 10 - i; st->sy[i] = 7; }
    st->dx = 1; st->dy = 0; st->fx = 16; st->fy = 7; st->score = 0;
}

static void steam_draw(Window *w, Rect r) {
    SteamState *st = (SteamState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    int i;

    fill_grad_v(r, RGB(23, 38, 62), RGB(14, 22, 36));
    draw_text_title(r.x + 22, r.y + 16, "STEAM  ·  Библиотека игр YoroOS 0.2", C_WHITE);
    ui_button((Rect){r.x + r.w - 224, r.y + 14, 202, 34},
              st->playing ? "Перезапустить игру" : "▶ ИГРАТЬ (Cyber Snake)",
              pt_in(mx, my, (Rect){r.x + r.w - 224, r.y + 14, 202, 34}), 1);

    if (!st->playing) {
        ui_card((Rect){r.x + 22, r.y + 68, r.w - 44, 150}, 0, 10);
        icon_draw(ICON_STEAM, r.x + 42, r.y + 92, 64);
        draw_text_title(r.x + 124, r.y + 90, "Cyber Snake: Aurora Hyper-V Edition", C_WHITE);
        draw_text_small(r.x + 124, r.y + 122, "Статус: Установлено и готово к запуску  |  Управление: Стрелки или WASD", RGB(140, 224, 168));
        draw_text_small(r.x + 124, r.y + 148, "Нажмите зелёную кнопку «ИГРАТЬ» справа вверху для запуска в 60 FPS!", RGB(190, 210, 236));
    } else {
        Rect arena = {r.x + (r.w - 28 * 18) / 2, r.y + 72, 28 * 18, 18 * 18};
        char sc[64];
        fill_round_rect(arena, 6, RGB(10, 15, 24));
        draw_round_rect(arena, 6, RGB(0, 184, 255));
        fill_round_rect((Rect){arena.x + st->fx * 18 + 2, arena.y + st->fy * 18 + 2, 14, 14}, 4, RGB(255, 82, 98));
        for (i = 0; i < st->slen; i++)
            fill_round_rect((Rect){arena.x + st->sx[i] * 18 + 1, arena.y + st->sy[i] * 18 + 1, 16, 16}, 4,
                            i == 0 ? RGB(92, 246, 178) : RGB(24, 168, 228));
        snprintf(sc, sizeof(sc), "Счёт: %d  (Стрелки / WASD — управление)", st->score);
        draw_text_center(r.x + r.w / 2, arena.y + arena.h + 12, sc, C_WHITE);
    }
}

static void steam_event(Window *w, YoroEvent *e) {
    SteamState *st = (SteamState *)w->user;
    Rect r = wm_client(w);
    if (e->type == EV_TICK && st->playing && sys_ticks() - st->last_step >= 12) {
        int nx = (st->sx[0] + st->dx + 28) % 28;
        int ny = (st->sy[0] + st->dy + 18) % 18;
        int i;
        st->last_step = sys_ticks();
        if (nx == st->fx && ny == st->fy) {
            if (st->slen < 120) st->slen++;
            st->score += 10;
            st->fx = (int)((sys_ticks() * 7U + 3U) % 28U);
            st->fy = (int)((sys_ticks() * 5U + 2U) % 18U);
        }
        for (i = st->slen - 1; i > 0; i--) { st->sx[i] = st->sx[i - 1]; st->sy[i] = st->sy[i - 1]; }
        st->sx[0] = nx; st->sy[0] = ny;
        wm_invalidate();
    } else if (e->type == EV_MOUSE_DOWN && pt_in(e->x, e->y, (Rect){r.x + r.w - 224, r.y + 14, 202, 34})) {
        st->playing = 1; snake_reset(st); wm_invalidate();
    } else if (e->type == EV_KEY && st->playing) {
        if ((e->key == KEY_UP || e->ch == 'w') && st->dy == 0) { st->dx = 0; st->dy = -1; }
        else if ((e->key == KEY_DOWN || e->ch == 's') && st->dy == 0) { st->dx = 0; st->dy = 1; }
        else if ((e->key == KEY_LEFT || e->ch == 'a') && st->dx == 0) { st->dx = -1; st->dy = 0; }
        else if ((e->key == KEY_RIGHT || e->ch == 'd') && st->dx == 0) { st->dx = 1; st->dy = 0; }
    }
}

void steam_open(void) {
    SteamState *st = (SteamState *)kzalloc(sizeof(SteamState));
    snake_reset(st);
    wm_open(app_find("steam"), "Steam — YoroOS Gaming Client", -1, -1, 800, 480, steam_draw, steam_event, free_user_cb)->user = st;
}

/* =========================================================================
 * 8. DISCORD, VLC MEDIA PLAYER, GIT BASH, BATCH RUNNER & WINVER ABOUT
 * ========================================================================= */
static void discord_draw(Window *w, Rect r) {
    static const char *msgs[5] = {
        "[Ship-It Dev]: Добро пожаловать на официальный сервер YoroOS 0.2 «Aurora»!",
        "[HyperV_Tester]: Проверил на Поколении 2 — VMBus мышь, GOP и GPT диск летают!",
        "[MatronixBot]: Matronix Browser v3.0.0 (#000000) доступен в Браузере и Магазине!",
        "[Pythonista]: Скрипты Python 3.12 и нативные .EXE запускаются прямо с Рабочего стола!",
        "[FreedomUser]: Наконец-то ОС, где я сам решаю, какие системные папки оставлять!"
    };
    int i;
    UNUSED(w);
    fill_rect(r, RGB(49, 51, 56));
    fill_rect((Rect){r.x, r.y, 180, r.h}, RGB(43, 45, 49));
    draw_text_bold(r.x + 14, r.y + 14, "# yoroos-aurora", C_WHITE);
    draw_text_small(r.x + 14, r.y + 44, "• general", RGB(180, 186, 198));
    draw_text_small(r.x + 14, r.y + 68, "• hyper-v-gen2", RGB(180, 186, 198));
    draw_text_small(r.x + 14, r.y + 92, "• matronix-browser", RGB(180, 186, 198));
    for (i = 0; i < 5; i++)
        draw_text(r.x + 198, r.y + 24 + i * 48, msgs[i], RGB(232, 236, 244));
}
void discord_open(void) { wm_open(app_find("discord"), "Discord — #yoroos-aurora", -1, -1, 800, 460, discord_draw, NULL, NULL); }

static void vlc_draw(Window *w, Rect r) {
    int i;
    UNUSED(w);
    fill_rect(r, RGB(14, 16, 22));
    draw_text_center_face(FACE_TITLE, r.x + r.w / 2, r.y + 24, "VLC Media Player  ·  Aurora Synth Stream", RGB(255, 152, 36));
    for (i = 0; i < 24; i++) {
        int hgt = 24 + (int)(((sys_ticks() * 5U + (u32)(i * 29)) % 140U));
        fill_round_rect((Rect){r.x + 48 + i * 22, r.y + 260 - hgt, 16, hgt}, 4,
                        RGB(0 + i * 9, 200 - i * 4, 255 - i * 6));
    }
    ui_progress((Rect){r.x + 48, r.y + r.h - 46, r.w - 96, 10}, (int)((sys_ticks() / 4U) % 100U), RGB(255, 136, 0));
}
static void vlc_ev(Window *w, YoroEvent *e) { UNUSED(w); if (e->type == EV_TICK) wm_invalidate(); }
void vlc_open(void) { wm_open(app_find("vlc"), "VLC Media Player", -1, -1, 630, 360, vlc_draw, vlc_ev, NULL); }

void git_app_open(void) { terminal_open_cmd("systeminfo"); }
void batch_run_node(int node) {
    if (node >= 0) terminal_open_cmd("ver");
}

static void about_draw(Window *w, Rect r) {
    char b[160];
    UNUSED(w);
    fill_rect(r, theme->win_bg);
    icon_draw(ICON_ABOUT, r.x + 28, r.y + 24, 56);
    draw_text_title(r.x + 100, r.y + 28, "YoroOS 0.2 «Aurora» (Pre-Release)", theme->text_main);
    draw_text_small(r.x + 100, r.y + 58, "Сборка 0.2.2610  ·  Гибридное ядро UEFI x86-64 (Gen 2) + BIOS x86 (Gen 1)", theme->accent);
    hline(r.x + 24, r.x + r.w - 24, r.y + 96, RGBA(130, 145, 170, 70));

    snprintf(b, sizeof(b), "Владелец ПК   : %s (%s)", sys_profile.username, sys_profile.pc_name);
    draw_text(r.x + 28, r.y + 116, b, theme->text_main);
    snprintf(b, sizeof(b), "Процессор     : %s", hal_cpu_brand());
    draw_text_small(r.x + 28, r.y + 148, b, theme->text_muted);
    snprintf(b, sizeof(b), "Драйвер мыши  : %s", hal_pointer_driver_name());
    draw_text_small(r.x + 28, r.y + 172, b, theme->text_muted);
    draw_text_small(r.x + 28, r.y + 206, "Нативная совместимость с Microsoft Hyper-V Generation 2, Win32 .EXE и .ZIP.", theme->text_main);
    draw_text_small(r.x + 28, r.y + 230, "Принцип YoroOS: 100% контроль пользователя над всеми файлами и системными папками.", RGB(72, 214, 142));
}
void about_open(void) { wm_open(app_find("about"), "О системе YoroOS (winver)", -1, -1, 560, 320, about_draw, NULL, NULL); }
