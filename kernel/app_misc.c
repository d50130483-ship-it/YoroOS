/* YoroOS - Notepad, Calculator, About and Image Viewer */
#include "kernel.h"

/* ==================== Notepad ==================== */
typedef struct {
    char text[8192];
    int len;
    int cur;
    int scroll;
    int node;
    char name[64];
    int picker;
    int picker_files[32];
    int picker_n;
} NotepadState;

static NotepadState np;
static Window *np_win = NULL;

static void np_title(void) {
    if (np_win && np_win->used)
        snprintf(np_win->title, sizeof(np_win->title), "Блокнот — %s", np.name);
}

static void np_new(void) {
    np.len = 0;
    np.text[0] = 0;
    np.cur = 0;
    np.scroll = 0;
    np.node = -1;
    strcpy(np.name, "Безымянный.txt");
    np_title();
}

static void np_open_file(int node) {
    if (node < 0) return;
    strncpy(np.text, fs_data(node), sizeof(np.text) - 1);
    np.text[sizeof(np.text) - 1] = 0;
    np.len = (int)strlen(np.text);
    np.cur = np.len;
    np.scroll = 0;
    np.node = node;
    strncpy(np.name, fs_name(node), 63);
    np_title();
}

static void np_save(void) {
    if (np.node >= 0) {
        fs_write(np.node, np.text, np.len);
        toast("Файл сохранён");
    } else {
        int docs = fs_parse_path(fs_root(), "C:\\Users\\Yoro\\Documents");
        np.node = fs_create(docs >= 0 ? docs : fs_root(), np.name, np.text);
        if (np.node >= 0) toast("Файл сохранён в Documents");
        else toast("Не удалось сохранить");
    }
    wm_invalidate();
}

static void np_collect(int dir, int depth) {
    int i, n = fs_count(dir);
    if (depth > 3 || np.picker_n >= 32) return;
    for (i = 0; i < n && np.picker_n < 32; i++) {
        int node = fs_child(dir, i);
        if (fs_is_dir(node)) np_collect(node, depth + 1);
        else np.picker_files[np.picker_n++] = node;
    }
}

/* wraps the text into display lines; returns the number of lines */
static int np_layout(int width, int *starts, int *lens, int max) {
    int pos = 0, line = 0;
    while (pos < np.len && line < max - 1) {
        int start = pos, x = 0;
        while (pos < np.len) {
            int adv = 1;
            int cp = str_utf8_decode(np.text + pos, &adv);
            if (cp == '\n') { pos++; break; }
            if (cp == '\r') { pos++; continue; }
            {
                const Glyph *g = font_lookup((u16)cp);
                int w = g ? g->adv : 6;
                if (x + w > width && x > 0) break;
                x += w;
            }
            pos += adv;
        }
        starts[line] = start;
        lens[line] = pos - start;
        line++;
    }
    if (line == 0) { starts[0] = 0; lens[0] = 0; line = 1; }
    lens[line] = 0;
    starts[line] = np.len;
    return line;
}

static int np_line_at(int count, const int *starts, const int *lens) {
    int i;
    for (i = 0; i < count; i++) {
        if (lens[i] == 0) continue;
        if (np.cur >= starts[i] && np.cur < starts[i] + lens[i]) return i;
        if (np.cur == starts[i] + lens[i] && i + 1 < count && np.cur < starts[i + 1]) return i;
    }
    return count > 0 ? count - 1 : 0;
}

static void notepad_draw(Window *w, Rect c) {
    Rect bar = {c.x, c.y, c.w, 42};
    Rect area = {c.x, c.y + 42, c.w, c.h - 42};
    int mx = mouse_get_x(), my = mouse_get_y();
    static int starts[400], lens[400];
    int n, i;
    int lineh = 22;
    int visible = (area.h - 24) / lineh;
    int wrapw = area.w - 40;

    fill_rect(bar, RGB(246, 248, 251));
    fill_rect((Rect){bar.x, bar.y + 41, bar.w, 1}, RGB(226, 230, 238));
    ui_button((Rect){bar.x + 10, bar.y + 7, 88, 28}, "Новый", 0, 0);
    ui_button((Rect){bar.x + 104, bar.y + 7, 96, 28}, "Открыть", 0, 0);
    ui_button((Rect){bar.x + 206, bar.y + 7, 106, 28}, "Сохранить", 0, 1);
    draw_text_right(bar.x + bar.w - 14, bar.y + 12, np.name, RGB(120, 128, 144));

    fill_rect(area, C_WHITE);
    if (wrapw < 60) wrapw = 60;
    n = np_layout(wrapw, starts, lens, 400);
    if (visible < 1) visible = 1;
    {
        int curline = np_line_at(n, starts, lens);
        if (curline < np.scroll) np.scroll = curline;
        if (curline >= np.scroll + visible) np.scroll = curline - visible + 1;
        if (np.scroll < 0) np.scroll = 0;

        gfx_set_clip(area);
        for (i = np.scroll; i < n && i < np.scroll + visible; i++) {
            char buf[400];
            int l = lens[i];
            if (l > 399) l = 399;
            memcpy(buf, np.text + starts[i], (size_t)l);
            buf[l] = 0;
            if (l && buf[l - 1] == '\n') buf[l - 1] = 0;
            draw_text(area.x + 20, area.y + 12 + (i - np.scroll) * lineh, buf, RGB(30, 36, 48));
        }
        {
            int cy = area.y + 12 + (curline - np.scroll) * lineh;
            int cx = area.x + 20;
            char tmp[400];
            int l = np.cur - starts[curline];
            if (l < 0) l = 0;
            if (l > 399) l = 399;
            memcpy(tmp, np.text + starts[curline], (size_t)l);
            tmp[l] = 0;
            cx += text_width(tmp);
            if ((sys_ticks() / 30) % 2 == 0) fill_rect((Rect){cx, cy, 1, FONT_H}, RGB(40, 46, 60));
        }
        gfx_set_clip(c);
    }

    if (np.picker) {
        Rect p = {c.x + (c.w - 420) / 2, c.y + 60, 420, 380};
        fill_rect_alpha(c, RGBA(10, 16, 28, 80));
        draw_shadow(p, 10);
        fill_round_rect(p, 10, C_WHITE);
        draw_round_rect(p, 10, RGB(206, 212, 222));
        draw_text(p.x + 18, p.y + 14, "Открыть файл", RGB(28, 34, 46));
        {
            int k;
            for (k = 0; k < np.picker_n && k < 11; k++) {
                Rect row = {p.x + 10, p.y + 44 + k * 28, p.w - 20, 26};
                int node = np.picker_files[k];
                if (pt_in(mx, my, row)) fill_round_rect(row, 4, RGB(238, 242, 248));
                icon_draw(ICON_TEXTFILE, row.x + 6, row.y + 5, 16);
                draw_text_clip(row.x + 28, row.y + 4, fs_name(node), RGB(40, 46, 60),
                               (Rect){row.x + 28, row.y, row.w - 40, row.h});
            }
        }
        ui_button((Rect){p.x + p.w - 110, p.y + p.h - 44, 94, 30}, "Закрыть", 0, 0);
    }
    UNUSED(w);
}

static void np_insert_char(int cp) {
    char enc[4];
    int n, k;
    if (cp < 32) return;
    if (np.len > (int)sizeof(np.text) - 8) return;
    n = utf8_encode((u16)cp, enc);
    memmove(np.text + np.cur + n, np.text + np.cur, (size_t)(np.len - np.cur + 1));
    for (k = 0; k < n; k++) np.text[np.cur + k] = enc[k];
    np.cur += n;
    np.len += n;
}

static void np_backspace(void) {
    int p;
    if (np.cur <= 0) return;
    p = np.cur - 1;
    while (p > 0 && ((unsigned char)np.text[p] & 0xC0) == 0x80) p--;
    memmove(np.text + p, np.text + np.cur, (size_t)(np.len - np.cur + 1));
    np.len -= np.cur - p;
    np.cur = p;
}

static void notepad_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    static int starts[400], lens[400];
    if (np.picker) {
        if (e->type == EV_MOUSE_DOWN) {
            Rect p = {c.x + (c.w - 420) / 2, c.y + 60, 420, 380};
            int k;
            for (k = 0; k < np.picker_n && k < 11; k++) {
                Rect row = {p.x + 10, p.y + 44 + k * 28, p.w - 20, 26};
                if (pt_in(mx, my, row)) {
                    np_open_file(np.picker_files[k]);
                    np.picker = 0;
                    wm_invalidate();
                    return;
                }
            }
            if (pt_in(mx, my, (Rect){p.x + p.w - 110, p.y + p.h - 44, 94, 30})) {
                np.picker = 0;
                wm_invalidate();
            }
        }
        if (e->type == EV_KEY && e->key == KEY_ESC) { np.picker = 0; wm_invalidate(); }
        return;
    }
    if (e->type == EV_MOUSE_DOWN) {
        if (pt_in(mx, my, (Rect){c.x + 10, c.y + 7, 88, 28})) { np_new(); wm_invalidate(); return; }
        if (pt_in(mx, my, (Rect){c.x + 104, c.y + 7, 96, 28})) {
            np.picker_n = 0;
            np_collect(fs_root(), 0);
            np.picker = 1;
            wm_invalidate();
            return;
        }
        if (pt_in(mx, my, (Rect){c.x + 206, c.y + 7, 106, 28})) { np_save(); return; }
        if (my > c.y + 42 && mx > c.x) {
            int i, n;
            int lineh = 22;
            int wrapw = c.w - 40;
            n = np_layout(wrapw, starts, lens, 400);
            i = (my - (c.y + 42) - 12) / lineh + np.scroll;
            if (i < 0) i = 0;
            if (i >= n) i = n - 1;
            np.cur = starts[i] + (lens[i] > 0 ? lens[i] - 1 : 0);
            wm_invalidate();
        }
        return;
    }
    if (e->type == EV_WHEEL) {
        np.scroll -= e->wheel * 2;
        if (np.scroll < 0) np.scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type != EV_KEY) return;
    if (e->ctrl && (e->key == 's' || e->key == 'S')) { np_save(); return; }
    {
        int n = np_layout(c.w - 40, starts, lens, 400);
        int ln = np_line_at(n, starts, lens);
        switch (e->key) {
        case KEY_ENTER: np_insert_char('\n'); break;
        case KEY_BACKSP: np_backspace(); break;
        case KEY_DEL:
            if (np.cur < np.len) {
                int adv = 1;
                str_utf8_decode(np.text + np.cur, &adv);
                memmove(np.text + np.cur, np.text + np.cur + adv, (size_t)(np.len - np.cur - adv + 1));
                np.len -= adv;
            }
            break;
        case KEY_LEFT:
            if (np.cur > 0) {
                int p = np.cur - 1;
                while (p > 0 && ((unsigned char)np.text[p] & 0xC0) == 0x80) p--;
                np.cur = p;
            }
            break;
        case KEY_RIGHT:
            if (np.cur < np.len) {
                int adv = 1;
                str_utf8_decode(np.text + np.cur, &adv);
                np.cur += adv;
            }
            break;
        case KEY_UP:
            if (ln > 0) {
                int col = np.cur - starts[ln];
                int t = ln - 1;
                if (col > lens[t]) col = lens[t];
                if (col > 0 && np.text[starts[t] + col - 1] == '\n') col--;
                np.cur = starts[t] + (col < 0 ? 0 : col);
            }
            break;
        case KEY_DOWN:
            if (ln + 1 < n) {
                int col = np.cur - starts[ln];
                int t = ln + 1;
                if (col > lens[t]) col = lens[t];
                if (col > 0 && np.text[starts[t] + col - 1] == '\n') col--;
                np.cur = starts[t] + (col < 0 ? 0 : col);
            }
            break;
        case KEY_HOME:
            np.cur = starts[ln];
            break;
        case KEY_END:
            np.cur = starts[ln] + lens[ln];
            if (np.cur > 0 && np.text[np.cur - 1] == '\n') np.cur--;
            break;
        default:
            if (e->ch >= 32) np_insert_char(e->ch);
            break;
        }
    }
    wm_invalidate();
}

void notepad_open(void) {
    if (!np_win || !np_win->used) {
        np_new();
        np_win = wm_open(app_find("notepad"), "Блокнот — Безымянный.txt", -1, -1, 760, 560,
                         notepad_draw, notepad_event, NULL);
    } else {
        wm_focus(np_win);
    }
    wm_invalidate();
}

void notepad_open_file(int node) {
    if (!np_win || !np_win->used)
        np_win = wm_open(app_find("notepad"), "Блокнот", -1, -1, 780, 560,
                         notepad_draw, notepad_event, NULL);
    np_open_file(node);
    wm_focus(np_win);
    wm_invalidate();
}

/* ==================== Calculator ==================== */
typedef struct {
    int acc;        /* scaled by 100 */
    int cur;        /* scaled by 100 */
    char entry[24];
    int entry_len;
    int op;
    int fresh;
    char display[32];
} CalcState;

static CalcState calc;

static void calc_reset(void) {
    calc.acc = 0;
    calc.cur = 0;
    calc.op = 0;
    calc.fresh = 1;
    calc.entry[0] = '0';
    calc.entry[1] = 0;
    calc.entry_len = 1;
}

static void calc_update_display(void) {
    int v = calc.cur;
    int neg = v < 0;
    int a;
    if (neg) v = -v;
    a = v / 100;
    if (v % 100)
        sprintf(calc.display, "%s%d.%02d", neg ? "-" : "", a, v % 100);
    else
        sprintf(calc.display, "%s%d", neg ? "-" : "", a);
}

static int parse_entry(void) {
    int i = 0, sign = 1, whole = 0, frac = 0, fracdig = 0;
    if (calc.entry[0] == '-') { sign = -1; i = 1; }
    while (calc.entry[i] >= '0' && calc.entry[i] <= '9') { whole = whole * 10 + (calc.entry[i] - '0'); i++; }
    if (calc.entry[i] == '.') {
        i++;
        while (calc.entry[i] >= '0' && calc.entry[i] <= '9') {
            if (fracdig < 2) { frac = frac * 10 + (calc.entry[i] - '0'); fracdig++; }
            i++;
        }
    }
    while (fracdig < 2) { frac *= 10; fracdig++; }
    return sign * (whole * 100 + frac);
}

static int mul_div100(int a, int b) {
    return (a / 100) * b + ((a % 100) * b) / 100;
}

static void calc_apply(void) {
    int v = parse_entry();
    switch (calc.op) {
    case 1: calc.acc += v; break;
    case 2: calc.acc -= v; break;
    case 3: calc.acc = mul_div100(calc.acc, v); break;
    case 4: if (v) calc.acc = (int)((long)calc.acc * 100 / v); break;
    default: calc.acc = v; break;
    }
    calc.cur = calc.acc;
}

static void calc_set_entry(int v) {
    int neg = v < 0;
    if (neg) v = -v;
    if (v % 100) sprintf(calc.entry, "%s%d.%02d", neg ? "-" : "", v / 100, v % 100);
    else sprintf(calc.entry, "%s%d", neg ? "-" : "", v / 100);
    calc.entry_len = (int)strlen(calc.entry);
}

static void calc_press(int key) {
    if (key >= '0' && key <= '9') {
        if (calc.fresh) { calc.entry[0] = 0; calc.entry_len = 0; calc.fresh = 0; }
        if (calc.entry_len < 12) {
            calc.entry[calc.entry_len++] = (char)key;
            calc.entry[calc.entry_len] = 0;
        }
        calc.cur = parse_entry();
    } else if (key == '.') {
        if (calc.fresh) { strcpy(calc.entry, "0"); calc.entry_len = 1; calc.fresh = 0; }
        if (!strstr(calc.entry, ".") && calc.entry_len < 11) {
            calc.entry[calc.entry_len++] = '.';
            calc.entry[calc.entry_len] = 0;
        }
        calc.cur = parse_entry();
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc.cur = parse_entry();
        calc_apply();
        calc.op = key == '+' ? 1 : key == '-' ? 2 : key == '*' ? 3 : 4;
        calc.fresh = 1;
        calc_set_entry(calc.acc);
    } else if (key == '=') {
        calc.cur = parse_entry();
        calc_apply();
        calc.op = 0;
        calc.fresh = 1;
        calc_set_entry(calc.acc);
    } else if (key == 'c') {
        calc_reset();
        calc_set_entry(0);
    } else if (key == 'n') {
        calc.cur = -calc.cur;
        calc_set_entry(calc.cur);
        calc.fresh = 0;
    } else if (key == 'p') {
        calc.cur = calc.cur / 100;
        calc_set_entry(calc.cur);
        calc.fresh = 0;
    }
    calc_update_display();
    wm_invalidate();
}

static const char *calc_keys[20] = {
    "C", "+/-", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "=", ""
};
static const char calc_map[20] = { 'c', 'n', 'p', '/', '7', '8', '9', '*', '4', '5', '6', '-',
                                   '1', '2', '3', '+', '0', '.', '=', 0 };

static void calc_draw(Window *w, Rect c) {
    int i;
    int mx = mouse_get_x(), my = mouse_get_y();
    calc_update_display();
    fill_rect(c, RGB(28, 32, 42));
    {
        Rect disp = {c.x + 12, c.y + 12, c.w - 24, 90};
        fill_round_rect(disp, 8, RGB(18, 22, 30));
        draw_text_right(disp.x + disp.w - 16, disp.y + 52, calc.display, C_WHITE);
    }
    {
        int gw = (c.w - 24 - 3 * 10) / 4;
        int gh = (c.h - 120 - 4 * 10 - 20) / 5;
        for (i = 0; i < 19; i++) {
            int col = i % 4, row = i / 4;
            Rect b = {c.x + 12 + col * (gw + 10), c.y + 116 + row * (gh + 10), gw, gh};
            int hov = pt_in(mx, my, b);
            Color bg = RGB(58, 64, 78);
            if (i == 0) bg = RGB(200, 70, 60);
            else if (i == 2 || i == 3 || i == 7 || i == 11 || i == 15) bg = RGB(48, 54, 68);
            else if (i == 18) bg = theme->accent;
            if (hov) { bg.r = (u8)(bg.r + 18); bg.g = (u8)(bg.g + 18); bg.b = (u8)(bg.b + 18); }
            fill_round_rect(b, 8, bg);
            draw_text_center(b.x + b.w / 2, b.y + (b.h - FONT_H) / 2, calc_keys[i], C_WHITE);
        }
    }
    UNUSED(w);
}

static void calc_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    int gw, gh, i;
    if (e->type == EV_KEY) {
        if (e->key == KEY_ENTER) calc_press('=');
        else if (e->key == KEY_BACKSP) {
            if (calc.entry_len > 1) { calc.entry[--calc.entry_len] = 0; calc.cur = parse_entry(); }
            else { calc.entry[0] = '0'; calc.entry[1] = 0; calc.entry_len = 1; calc.cur = 0; }
            calc_update_display();
        } else if (e->ch == 'c' || e->ch == 'C') calc_press('c');
        else if (e->ch > 0 && strstr("0123456789.+-*/=", (char[]){ (char)e->ch, 0 }))
            calc_press(e->ch);
        wm_invalidate();
        return;
    }
    if (e->type != EV_MOUSE_DOWN) return;
    gw = (c.w - 24 - 3 * 10) / 4;
    gh = (c.h - 120 - 4 * 10 - 20) / 5;
    for (i = 0; i < 19; i++) {
        int col = i % 4, row = i / 4;
        Rect b = {c.x + 12 + col * (gw + 10), c.y + 116 + row * (gh + 10), gw, gh};
        if (pt_in(mx, my, b)) { calc_press(calc_map[i]); return; }
    }
    UNUSED(w);
}

void calc_open(void) {
    calc_reset();
    calc_set_entry(0);
    calc_update_display();
    wm_open(app_find("calc"), "Калькулятор", -1, -1, 380, 540, calc_draw, calc_event, NULL);
    wm_invalidate();
}

/* ==================== About ==================== */
static void about_draw(Window *w, Rect c) {
    char buf[200];
    fill_grad_v(c, RGB(12, 26, 48), RGB(6, 12, 26));
    draw_text_center(c.x + c.w / 2, c.y + 40, "YoroOS", C_WHITE);
    draw_text_center(c.x + c.w / 2, c.y + 68, "0.1 «Astra»", RGB(160, 200, 250));
    {
        int rad = 54;
        int cx = c.x + c.w / 2, cy = c.y + 150;
        fill_circle(cx, cy, rad, theme->accent);
        fill_circle(cx, cy, rad - 6, theme->accent_dark);
        draw_text_center(cx, cy - 18, "Yoro", C_WHITE);
        draw_text_center(cx, cy + 2, "OS", C_WHITE);
    }
    sprintf(buf, "Своё ядро YoroKernel 0.1 · загрузчик YoroBoot · ISO собрана из исходников");
    draw_text_center(c.x + c.w / 2, c.y + 224, buf, RGB(160, 176, 200));
    sprintf(buf, "Экран %dx%d · память %d МиБ · тиков %d", (int)fb.width, (int)fb.height,
            (int)heap_total() / 1024, (int)sys_ticks());
    draw_text_center(c.x + c.w / 2, c.y + 250, buf, RGB(120, 138, 166));
    draw_text_center(c.x + c.w / 2, c.y + 292, "Всё в системе можно удалить и создать заново —", RGB(200, 210, 230));
    draw_text_center(c.x + c.w / 2, c.y + 314, "включая папку Windows. Это ваш компьютер.", RGB(200, 210, 230));
    UNUSED(w);
}

void about_open(void) {
    wm_open(app_find("about"), "О системе YoroOS", -1, -1, 640, 420, about_draw, NULL, NULL);
    wm_invalidate();
}

/* ==================== Image viewer ==================== */
static char img_name[64] = "wallpaper.bmp";

static void image_draw(Window *w, Rect c) {
    Rect img;
    fill_rect(c, RGB(24, 26, 32));
    img.x = c.x + 24;
    img.y = c.y + 24;
    img.w = c.w - 48;
    img.h = c.h - 80;
    fill_round_rect(img, 6, RGB(12, 18, 30));
    gfx_set_clip(img);
    fill_grad_v(img, theme->wall_top, theme->wall_bottom);
    fill_circle(img.x + img.w / 3, img.y + img.h / 2, img.h / 4, RGBA(80, 170, 255, 120));
    fill_circle(img.x + img.w * 2 / 3, img.y + img.h / 3, img.h / 5, RGBA(255, 170, 90, 110));
    draw_text_center(img.x + img.w / 2, img.y + img.h / 2, "YoroOS", RGBA(255, 255, 255, 200));
    gfx_set_clip(c);
    draw_text(c.x + 24, c.y + c.h - 42, img_name, RGB(210, 216, 230));
    draw_text_right(c.x + c.w - 24, c.y + c.h - 42, "Просмотр изображений YoroOS", RGB(130, 140, 158));
    UNUSED(w);
}

void image_viewer_open(const char *name) {
    strncpy(img_name, name, 63);
    img_name[63] = 0;
    wm_open(app_find("notepad"), "Изображение", -1, -1, 760, 560, image_draw, NULL, NULL);
    wm_invalidate();
}
