/* Приложения YoroOS: Терминал, Заметки, Календарь, Музыка, Калькулятор, Finder, Настройки, О системе, Просмотр */
#include "ui.h"
#include "gfx.h"
#include "hw.h"
#include "fs.h"
#include "klib.h"
#include "font.h"

static void scat(char *dst, const char *src) {
    while (*dst) dst++;
    while ((*dst++ = *src++)) {}
}

/* ================= иконки ================= */
static void ic_base(int x, int y, int s, u32 c) {
    gfx_round(x, y, s, s, s / 5, c);
    gfx_round_blend(x, y, s, s / 2, s / 5, 0xffffff, 36);
}

static void ic_terminal(int x, int y, int s) {
    ic_base(x, y, s, 0x1c1f2a);
    int u = s / 16;
    gfx_fill(x + 4 * u, y + 5 * u, 4 * u, 2 * u, 0x6ecf8e);
    gfx_fill(x + 7 * u, y + 5 * u + 2 * u, 2 * u, 2 * u, 0x6ecf8e);
    gfx_fill(x + 4 * u, y + 10 * u, 7 * u, 2 * u, 0xd8e8da);
}

static void ic_notes(int x, int y, int s) {
    ic_base(x, y, s, 0xf6f6f2);
    gfx_round_blend(x, y, s, s / 3, s / 5, 0xf7c948, 255);
    int u = s / 16;
    for (int i = 0; i < 3; i++)
        gfx_fill(x + 3 * u, y + 7 * u + i * 2 * u, 10 * u, u, 0xb9bcc7);
}

static void ic_calendar(int x, int y, int s) {
    ic_base(x, y, s, 0xffffff);
    gfx_round_blend(x, y, s, s / 3, s / 5, 0xff453a, 255);
    int day;
    int sec, min, hour, mon, year;
    rtc_get(&sec, &min, &hour, &day, &mon, &year);
    char b[4];
    itoa(day, b);
    if (s >= 36) gfx_text_c(x + s / 2, y + s / 2 - 4, b, 0x26262b, 1);
}

static void ic_music(int x, int y, int s) {
    ic_base(x, y, s, 0xfa233b);
    int u = s / 16;
    gfx_fill(x + 10 * u, y + 4 * u, u, 7 * u, 0xffffff);
    gfx_fill(x + 11 * u, y + 4 * u, 3 * u, 2 * u, 0xffffff);
    gfx_fill(x + 7 * u, y + 10 * u, 4 * u, 3 * u, 0xffffff);
}

static void ic_calc(int x, int y, int s) {
    ic_base(x, y, s, 0x2e3038);
    int u = s / 16;
    gfx_fill(x + 3 * u, y + 3 * u, 10 * u, 3 * u, 0x1a1b20);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            gfx_fill(x + 3 * u + c * 3 * u, y + 7 * u + r * 2 * u, 2 * u, u, c == 3 ? 0xff9f0a : 0x8e8e93);
}

static void ic_settings(int x, int y, int s) {
    ic_base(x, y, s, 0x8b939f);
    int cx = x + s / 2, cy = y + s / 2, r = s / 4;
    for (int i = 0; i < 8; i++) {
        int dx = (i == 0 || i == 4) ? 0 : (i < 4 ? (i == 1 || i == 2 ? 1 : -1) : (i == 5 || i == 6 ? 1 : -1));
        int dy = (i == 2 || i == 6) ? 0 : (i < 2 || i > 5 ? -1 : 1);
        gfx_fill(cx + dx * r - 2, cy + dy * r - 2, 4, 4, 0xffffff);
    }
    for (int yy = -r; yy <= r; yy++)
        for (int xx = -r; xx <= r; xx++)
            if (xx * xx + yy * yy <= r * r) gfx_px(cx + xx, cy + yy, 0xffffff);
    r = r / 2;
    for (int yy = -r; yy <= r; yy++)
        for (int xx = -r; xx <= r; xx++)
            if (xx * xx + yy * yy <= r * r) gfx_px(cx + xx, cy + yy, 0x6a7280);
}

static void ic_finder(int x, int y, int s) {
    ic_base(x, y, s, 0x3e9bef);
    int u = s / 16;
    gfx_fill(x + 4 * u, y + 5 * u, u, 3 * u, 0x123a5e);
    gfx_fill(x + 11 * u, y + 5 * u, u, 3 * u, 0x123a5e);
    for (int i = 0; i < 2 * u; i++) {
        int w = i < u ? i : (2 * u - i);
        gfx_fill(x + 5 * u + i, y + 10 * u + w, 1, u - w + 1, 0xffffff);
    }
}

static void ic_about(int x, int y, int s) {
    ic_base(x, y, s, 0x6b5cff);
    static const unsigned char yy[7] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
    int k = s / 11;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (yy[r] & (0x10 >> c)) gfx_fill(x + (c + 2) * k + k, y + (r + 2) * k, k, k, 0xffffff);
}

void draw_app_icon(int id, int x, int y, int s) {
    switch (id) {
    case AP_TERMINAL: ic_terminal(x, y, s); break;
    case AP_NOTES: ic_notes(x, y, s); break;
    case AP_CALENDAR: ic_calendar(x, y, s); break;
    case AP_MUSIC: ic_music(x, y, s); break;
    case AP_CALC: ic_calc(x, y, s); break;
    case AP_FINDER: ic_finder(x, y, s); break;
    case AP_SETTINGS: ic_settings(x, y, s); break;
    case AP_ABOUT: ic_about(x, y, s); break;
    }
}

/* ================= имена/размеры ================= */
static const char *app_names[AP_COUNT] = {
    "Терминал", "Заметки", "Календарь", "Музыка", "Калькулятор", "Finder", "Настройки", "О YoroOS", "Просмотр"
};
const char *app_name(int id) { return app_names[id]; }

void app_default_size(int id, int *w, int *h) {
    static const int sz[AP_COUNT][2] = {
        { 480, 300 }, { 500, 300 }, { 400, 340 }, { 380, 240 }, { 224, 270 },
        { 460, 280 }, { 420, 260 }, { 360, 230 }, { 440, 280 }
    };
    *w = sz[id][0];
    *h = sz[id][1];
}

void app_launch(int id) { ui_open(id, 0, 0); }

void app_close_app(int app) { ui_close_app(app); }

/* ================= Терминал ================= */
#define T_ROWS 240
static char t_lines[T_ROWS][81];
static int t_head, t_cnt;
static char t_input[81];
static int t_ilen;

static void t_add(const char *s) {
    while (*s) {
        char *ln = t_lines[t_head];
        if (!*ln && t_cnt < T_ROWS) t_cnt++;
        int n = strlen(ln);
        while (*s && n < 80) ln[n++] = *s++;
        ln[n] = 0;
        if (*s) { t_head = (t_head + 1) % T_ROWS; if (t_cnt < T_ROWS) t_cnt++; }
        if (!*s) break;
    }
    t_head = (t_head + 1) % T_ROWS;
    if (t_cnt < T_ROWS) t_cnt++;
}

static void t_print(const char *s) { t_add(s); }

static void term_exec(const char *cmdline) {
    char cmd[40], arg[40];
    int i = 0;
    while (cmdline[i] == ' ') i++;
    int j = 0;
    while (cmdline[i] && cmdline[i] != ' ' && j < 39) cmd[j++] = cmdline[i++];
    cmd[j] = 0;
    while (cmdline[i] == ' ') i++;
    j = 0;
    while (cmdline[i] && j < 39) arg[j++] = cmdline[i++];
    arg[j] = 0;

    char buf[96];
    if (!cmd[0]) return;
    if (!strcmp(cmd, "help")) {
        t_print("Команды: help ls cat echo touch rm clear");
        t_print("  uptime whoami date cpuinfo df neofetch open apps reboot shutdown");
    } else if (!strcmp(cmd, "ls")) {
        if (!fs_valid()) { t_print("ФС не смонтирована"); return; }
        for (int k = 0; k < FS_MAX_FILES; k++)
            if (fs_name(k)[0]) {
                strcpy(buf, fs_name(k), 60);
                int L = strlen(buf);
                strcpy(buf + L, "  (", 8);
                itoa(fs_size(k), buf + strlen(buf));
                strcpy(buf + strlen(buf), " байт)", 8);
                t_print(buf);
            }
    } else if (!strcmp(cmd, "cat")) {
        static char data[1024];
        int n = fs_read(arg, data, sizeof(data) - 1);
        if (n < 0) { t_print("cat: файл не найден"); return; }
        data[n] = 0;
        /* построчно */
        char *p = data;
        while (*p) {
            char *nl = p;
            while (*nl && *nl != '\n') nl++;
            char save = *nl;
            *nl = 0;
            t_print(p);
            *nl = save;
            p = *nl == 0 ? nl : nl + 1;
            if (!save) break;
        }
    } else if (!strcmp(cmd, "echo")) {
        t_print(arg);
    } else if (!strcmp(cmd, "touch")) {
        if (fs_create(arg, "", 0)) t_print("создан");
        else t_print("touch: ошибка");
    } else if (!strcmp(cmd, "rm")) {
        t_print(fs_delete(arg) ? "удалён" : "rm: не найден");
    } else if (!strcmp(cmd, "clear")) {
        t_head = 0; t_cnt = 0;
        for (int k = 0; k < T_ROWS; k++) t_lines[k][0] = 0;
        t_cnt = 0;
    } else if (!strcmp(cmd, "uptime")) {
        itoa((int)pit_uptime_s(), buf);
        strcpy(buf + strlen(buf), " cек", 8);
        t_print(buf);
    } else if (!strcmp(cmd, "whoami")) {
        t_print(g_user);
    } else if (!strcmp(cmd, "date")) {
        int sec, min, hour, day, mon, year;
        rtc_get(&sec, &min, &hour, &day, &mon, &year);
        char b[16];
        itoa(day, buf);
        strcpy(buf + strlen(buf), ".", 4);
        itoa(mon, b); if (mon < 10) strcpy(buf + strlen(buf), "0", 4);
        strcpy(buf + strlen(buf), b, 8);
        strcpy(buf + strlen(buf), ".", 4);
        itoa(year, b); strcpy(buf + strlen(buf), b, 8);
        strcpy(buf + strlen(buf), " ", 4);
        itoa(hour, b); if (hour < 10) strcpy(buf + strlen(buf), "0", 4);
        strcpy(buf + strlen(buf), b, 8);
        strcpy(buf + strlen(buf), ":", 4);
        itoa(min, b); if (min < 10) strcpy(buf + strlen(buf), "0", 4);
        strcpy(buf + strlen(buf), b, 8);
        t_print(buf);
    } else if (!strcmp(cmd, "cpuinfo")) {
        u32 a, b, c, d;
        char v[13];
        cpuid(0, &a, (u32 *)v, (u32 *)(v + 8), (u32 *)(v + 4));
        v[12] = 0;
        strcpy(buf, "ЦП: ", 8);
        strcpy(buf + strlen(buf), v, 16);
        t_print(buf);
    } else if (!strcmp(cmd, "df")) {
        strcpy(buf, "Файлов: ", 12);
        itoa(fs_count(), buf + strlen(buf));
        t_print(buf);
    } else if (!strcmp(cmd, "neofetch")) {
        t_print("YoroOS 1.0 «Натива»");
        t_print("────────────────────────────");
        strcpy(buf, "Пользователь: ", 20);
        strcpy(buf + strlen(buf), g_user, 24);
        t_print(buf);
        strcpy(buf, "Ядро: ykern 1.0 (i386, PM)", 40);
        t_print(buf);
        strcpy(buf, "Оболочка: ysh 1.0", 40);
        t_print(buf);
        strcpy(buf, "Аптайм: ", 12);
        itoa((int)pit_uptime_s(), buf + strlen(buf));
        strcpy(buf + strlen(buf), " с", 4);
        t_print(buf);
        strcpy(buf, "Разрешение: ", 16);
        itoa(gfx_w(), buf + strlen(buf));
        strcpy(buf + strlen(buf), "x", 4);
        itoa(gfx_h(), buf + strlen(buf));
        t_print(buf);
    } else if (!strcmp(cmd, "apps")) {
        for (int k = 0; k < AP_COUNT; k++) t_print(app_name(k));
    } else if (!strcmp(cmd, "open")) {
        int done = 0;
        for (int k = 0; k < AP_COUNT; k++)
            if (!strcmp(arg, app_name(k)) || (k == AP_TERMINAL && !strcmp(arg, "terminal")) ||
                (k == AP_NOTES && !strcmp(arg, "notes")) || (k == AP_MUSIC && !strcmp(arg, "music"))) {
                app_launch(k);
                done = 1;
            }
        if (!done) t_print("open: приложение не найдено");
    } else if (!strcmp(cmd, "reboot")) {
        hw_reboot();
    } else if (!strcmp(cmd, "shutdown")) {
        t_print("Работа завершена.");
        spk_off();
        hw_halt();
    } else {
        strcpy(buf, "ysh: команда не найдена: ", 40);
        strcpy(buf + strlen(buf), cmd, 40);
        t_print(buf);
    }
}

/* ================= Заметки ================= */
static char n_buf[3000];
static int n_len, n_cur;
static int n_file = -1;   /* индекс файла в ФС */
static int n_sel = 0;     /* выбранный в списке */

static void notes_save(void) {
    if (n_file >= 0 && fs_name(n_file)[0])
        fs_write(fs_name(n_file), n_buf, n_len);
}

static void notes_load(int idx) {
    notes_save();
    n_file = idx;
    n_len = fs_read(fs_name(idx), n_buf, sizeof(n_buf) - 1);
    if (n_len < 0) { n_len = 0; n_buf[0] = 0; }
    n_cur = 0;
}

static int notes_file_at(int list_i) {
    int k = -1;
    for (int f = 0; f < FS_MAX_FILES; f++) {
        const char *nm = fs_name(f);
        if (nm[0] && !strcmp(nm + strlen(nm) - 4, ".txt")) {
            k++;
            if (k == list_i) return f;
        }
    }
    return -1;
}
static int notes_file_count(void) {
    int n = 0;
    for (int f = 0; f < FS_MAX_FILES; f++) {
        const char *nm = fs_name(f);
        if (nm[0] && !strcmp(nm + strlen(nm) - 4, ".txt")) n++;
    }
    return n;
}

/* ================= Календарь ================= */
struct cal_ev { char date[11]; char text[36]; };
static struct cal_ev c_evs[64];
static int c_n = 0;
static int cal_y, cal_m, cal_day;
static char cal_input[36];
static int cal_input_dirty;

static void cal_load(void) {
    static char data[2048];
    c_n = 0;
    int n = fs_read("calendar.txt", data, sizeof(data) - 1);
    if (n < 0) return;
    data[n] = 0;
    char *p = data;
    while (*p && c_n < 64) {
        struct cal_ev *e = &c_evs[c_n];
        int i = 0;
        while (*p && *p != ' ' && i < 10) e->date[i++] = *p++;
        e->date[i] = 0;
        if (*p == ' ') p++;
        i = 0;
        while (*p && *p != '\n' && i < 35) e->text[i++] = *p++;
        e->text[i] = 0;
        if (*p == '\n') p++;
        if (e->date[0]) c_n++;
    }
}
static void cal_save(void) {
    static char data[4096];
    int p = 0;
    for (int i = 0; i < c_n; i++) {
        strcpy(data + p, c_evs[i].date, 12);
        p += strlen(c_evs[i].date);
        data[p++] = ' ';
        strcpy(data + p, c_evs[i].text, 40);
        p += strlen(c_evs[i].text);
        data[p++] = '\n';
    }
    fs_write("calendar.txt", data, p);
}
static int cal_ev_count(int y, int m, int d) {
    char key[11];
    char b[6];
    itoa(y, key);
    strcpy(key + strlen(key), "-", 4);
    itoa(m, b); if (m < 10) strcpy(key + strlen(key), "0", 4);
    strcpy(key + strlen(key), b, 4);
    strcpy(key + strlen(key), "-", 4);
    itoa(d, b); if (d < 10) strcpy(key + strlen(key), "0", 4);
    strcpy(key + strlen(key), b, 4);
    int n = 0;
    for (int i = 0; i < c_n; i++)
        if (!strcmp(c_evs[i].date, key)) n++;
    return n;
}

/* ================= Музыка ================= */
static const short trk_notes[][2] = {
    /* «Неоновый драйв» — басовая пульсация */
    { 33, 2 }, { 0, 2 }, { 33, 2 }, { 45, 2 }, { 33, 2 }, { 0, 2 }, { 36, 2 }, { 40, 2 },
    { 31, 2 }, { 0, 2 }, { 31, 2 }, { 43, 2 }, { 31, 2 }, { 0, 2 }, { 34, 2 }, { 38, 2 },
    { 29, 2 }, { 0, 2 }, { 29, 2 }, { 41, 2 }, { 29, 2 }, { 0, 2 }, { 33, 2 }, { 36, 2 },
    { 31, 2 }, { 0, 2 }, { 31, 2 }, { 43, 2 }, { 38, 2 }, { 36, 2 }, { 34, 2 }, { 31, 2 },
    /* конец первого трека — маркер */
    { -1, 0 },
    /* «Дождь за окном» — медленные арпеджио */
    { 50, 6 }, { 53, 6 }, { 57, 6 }, { 60, 6 }, { 57, 6 }, { 53, 6 },
    { 48, 6 }, { 52, 6 }, { 55, 6 }, { 60, 6 }, { 55, 6 }, { 52, 6 },
    { 46, 6 }, { 50, 6 }, { 53, 6 }, { 58, 6 }, { 53, 6 }, { 50, 6 },
    { 45, 6 }, { 48, 6 }, { 52, 6 }, { 57, 6 }, { 55, 6 }, { 52, 6 },
    { -1, 0 },
    /* «Пиксельный вальс» — 3/4 чиптюн */
    { 72, 2 }, { 76, 2 }, { 79, 2 }, { 76, 2 }, { 72, 2 }, { 76, 2 },
    { 74, 2 }, { 77, 2 }, { 81, 2 }, { 77, 2 }, { 74, 2 }, { 77, 2 },
    { 72, 2 }, { 76, 2 }, { 79, 2 }, { 84, 2 }, { 79, 2 }, { 76, 2 },
    { 71, 2 }, { 74, 2 }, { 79, 2 }, { 74, 2 }, { 71, 2 }, { 67, 2 },
    { -1, 0 }
};
static const char *trk_names[] = { "Неоновый драйв", "Дождь за окном", "Пиксельный вальс" };
static const char *theme_names[5] = { "Ночь", "Океан", "Закат", "Лес", "Светлая" };
const char *theme_name(int i) { return theme_names[i]; }
static const int trk_start[3] = { 0, 32, 56 };
static const int trk_len[3] = { 32, 24, 24 };
static const int trk_tps[3] = { 14, 22, 12 }; /* тиков на шаг */

static int mu_track = 0, mu_playing = 0, mu_pos = 0, mu_i = 0, mu_next = 0;

static void mu_stop_note(void) { spk_off(); }

static void mu_advance(void) {
    if (!mu_playing) return;
    if (mu_pos >= mu_next) {
        if (mu_i >= trk_len[mu_track]) { mu_i = 0; mu_pos = 0; mu_next = 0; }
        else {
            int idx = trk_start[mu_track] + mu_i;
            int midi = trk_notes[idx][0];
            int dur = trk_notes[idx][1] * trk_tps[mu_track];
            if (midi) spk_play(midi_hz[midi], dur - 1);
            else mu_stop_note();
            mu_next += dur;
            mu_i++;
        }
    }
    mu_pos++;
}

/* ================= Калькулятор ================= */
static char ca_disp[16] = "0";
static int ca_acc = 0, ca_op = 0, ca_fresh = 1;

static int ca_val(void) { return stoi(ca_disp); }
static void ca_show(int v) {
    if (v > 999999999) { strcpy(ca_disp, ">1e9", 8); return; }
    itoa(v, ca_disp);
}
static void ca_press(const char *k) {
    if (k[0] >= '0' && k[0] <= '9') {
        if (ca_fresh || !strcmp(ca_disp, "0")) { strcpy(ca_disp, k, 3); ca_fresh = 0; }
        else if (strlen(ca_disp) < 9) { ca_disp[strlen(ca_disp)] = k[0]; ca_disp[strlen(ca_disp)] = 0; }
        return;
    }
    switch (k[0]) {
    case 'C': strcpy(ca_disp, "0", 3); ca_acc = 0; ca_op = 0; ca_fresh = 1; break;
    case '%': ca_show(ca_val() / 100); ca_fresh = 1; break;
    case '+': case '-': case '*': case '/':
        if (ca_op && !ca_fresh) {
            int b = ca_val();
            if (ca_op == 1) ca_acc += b;
            if (ca_op == 2) ca_acc -= b;
            if (ca_op == 3) ca_acc *= b;
            if (ca_op == 4) ca_acc = b ? ca_acc / b : 0;
            ca_show(ca_acc);
        } else ca_acc = ca_val();
        ca_op = k[0] == '+' ? 1 : k[0] == '-' ? 2 : k[0] == '*' ? 3 : 4;
        ca_fresh = 1;
        break;
    case '=':
        if (ca_op) {
            int b = ca_val();
            int r = ca_acc;
            if (ca_op == 1) r += b;
            if (ca_op == 2) r -= b;
            if (ca_op == 3) r *= b;
            if (ca_op == 4) r = b ? r / b : 0;
            ca_show(r);
            ca_op = 0;
            ca_fresh = 1;
        }
        break;
    case 'n': ca_show(-ca_val()); break;
    }
}

/* ================= Finder ================= */
static int fi_sel = 0, fi_scroll = 0;
static int vi_file = -1;

/* ================= Настройки ================= */

/* ================= общие хелперы ================= */
static int btn(int x, int y, int w, int h, const char *label, int mxp, int myp, int pressed) {
    int hot = mxp >= x && mxp < x + w && myp >= y && myp < y + h;
    u32 bg = hot ? theme_accent(g_theme) : 0x2a2d36;
    gfx_round(x, y, w, h, 5, bg);
    if (!hot && pressed >= 0) gfx_round_blend(x, y, w, h, 5, 0xffffff, 14);
    gfx_text_c(x + w / 2, y + h / 2 - 7, label, hot ? 0xffffff : theme_text(g_theme), 0);
    return hot && pressed;
}

static void wrap_text(const char *s, int maxc, void (*out)(const char *)) {
    static char line[96];
    (void)out;
    while (*s) {
        int n = 0;
        while (*s && n < maxc) line[n++] = *s++;
        line[n] = 0;
    }
}

/* ================= события приложений ================= */
void app_open(int id) {
    if (id == AP_TERMINAL) {
        t_head = 0; t_cnt = 0; t_ilen = 0; t_input[0] = 0;
        for (int k = 0; k < T_ROWS; k++) t_lines[k][0] = 0;
        t_cnt = 0;
        t_print("YoroOS ysh 1.0 — help: список команд");
    } else if (id == AP_NOTES) {
        if (notes_file_count() > 0) notes_load(notes_file_at(0));
        else { n_file = -1; n_len = 0; n_buf[0] = 0; }
    } else if (id == AP_CALENDAR) {
        int sec, min, hour, day, mon, year;
        rtc_get(&sec, &min, &hour, &day, &mon, &year);
        cal_y = year; cal_m = mon; cal_day = day;
        cal_input[0] = 0;
        cal_load();
    } else if (id == AP_MUSIC) {
        mu_track = 0; mu_playing = 0; mu_pos = 0; mu_i = 0; mu_next = 0;
    } else if (id == AP_FINDER) {
        fi_sel = 0; fi_scroll = 0;
    }
}

void app_close_cleanup(int id) {
    if (id == AP_NOTES) notes_save();
    if (id == AP_MUSIC) { mu_playing = 0; mu_stop_note(); }
}

static void term_key(int code, int ascii) {
    if (code == K_ENTER) {
        char line[96];
        strcpy(line, g_user, 24);
        int L = strlen(line);
        strcpy(line + L, "@yoroos:~$ ", 16);
        strcpy(line + strlen(line), t_input, 84);
        t_print(line);
        term_exec(t_input);
        t_ilen = 0;
        t_input[0] = 0;
        return;
    }
    if (code == K_BS) {
        if (t_ilen > 0) { t_ilen--; while (t_ilen > 0 && (t_input[t_ilen] & 0xC0) == 0x80) t_ilen--; t_input[t_ilen] = 0; }
        return;
    }
    if (ascii >= 32 && t_ilen < 76) { t_ilen += utf8_put(t_input + t_ilen, (unsigned)ascii); t_input[t_ilen] = 0; }
}

static void notes_key(int code, int ascii) {
    if (n_file < 0) return;
    if (code == K_BS) {
        if (n_cur > 0) {
            memmove(n_buf + n_cur - 1, n_buf + n_cur, n_len - n_cur + 1);
            n_cur--;
            n_len--;
            notes_save();
        }
        return;
    }
    if (code == K_LEFT && n_cur > 0) { n_cur--; return; }
    if (code == K_RIGHT && n_cur < n_len) { n_cur++; return; }
    if (code == K_ENTER) ascii = '\n';
    if (ascii >= 32 && n_len < (int)sizeof(n_buf) - 4) {
        char tmp[4];
        int k = utf8_put(tmp, (unsigned)ascii);
        memmove(n_buf + n_cur + k, n_buf + n_cur, n_len - n_cur + 1);
        for (int b = 0; b < k; b++) n_buf[n_cur++] = tmp[b];
        n_len += k;
        n_buf[n_len] = 0;
        notes_save();
    }
}

static void cal_key(int code, int ascii) {
    if (code == K_LEFT) { cal_day--; if (cal_day < 1) cal_day = 28; }
    else if (code == K_RIGHT) { cal_day++; if (cal_day > 28) cal_day = 1; }
    else if (code == K_UP) { cal_m--; if (cal_m < 1) { cal_m = 12; cal_y--; } }
    else if (code == K_DOWN) { cal_m++; if (cal_m > 12) { cal_m = 1; cal_y++; } }
    else if (code == K_BS) { if (cal_input[0]) cal_input[strlen(cal_input) - 1] = 0; }
    else if (code == K_ENTER) {
        if (cal_input[0]) {
            char key[11], b[6];
            itoa(cal_y, key);
            strcpy(key + strlen(key), "-", 4);
            itoa(cal_m, b); if (cal_m < 10) strcpy(key + strlen(key), "0", 4);
            strcpy(key + strlen(key), b, 4);
            strcpy(key + strlen(key), "-", 4);
            itoa(cal_day, b); if (cal_day < 10) strcpy(key + strlen(key), "0", 4);
            strcpy(key + strlen(key), b, 4);
            if (c_n < 64) {
                strcpy(c_evs[c_n].date, key, 12);
                strcpy(c_evs[c_n].text, cal_input, 36);
                c_n++;
                cal_save();
            }
            cal_input[0] = 0;
        }
    } else if (ascii >= 32 && strlen(cal_input) < 28) {
        int L = strlen(cal_input);
        L += utf8_put(cal_input + L, (unsigned)ascii);
        cal_input[L] = 0;
    }
}

static void music_key(int code, int ascii) {
    (void)ascii;
    if (code == K_UP) { mu_playing = 0; mu_stop_note(); if (mu_track > 0) mu_track--; mu_pos = mu_i = mu_next = 0; }
    else if (code == K_DOWN) { mu_playing = 0; mu_stop_note(); if (mu_track < 2) mu_track++; mu_pos = mu_i = mu_next = 0; }
    else if (code == K_ENTER) {
        mu_playing = !mu_playing;
        if (mu_playing) { mu_pos = 0; mu_i = 0; mu_next = 0; }
        else mu_stop_note();
    }
}

static void calc_key(int code, int ascii) {
    if (code == K_ENTER) ca_press("=");
    else if (code == K_BS) ca_press("C");
    else if (ascii >= '0' && ascii <= '9') {
        char k[2] = { (char)ascii, 0 };
        ca_press(k);
    } else if (ascii == '+') ca_press("+");
    else if (ascii == '-') ca_press("-");
    else if (ascii == '*') ca_press("*");
    else if (ascii == '/') ca_press("/");
}

static void finder_key(int code, int ascii) {
    int cnt = fs_count();
    if (code == K_UP && fi_sel > 0) fi_sel--;
    if (code == K_DOWN && fi_sel < cnt - 1) fi_sel++;
    if (code == K_ENTER) {
        int f = fi_sel;
        const char *nm = fs_name(f);
        if (nm[0] && !strcmp(nm + strlen(nm) - 4, ".txt")) {
            notes_load(f);
            ui_open(AP_NOTES, 0, 0);
        } else {
            vi_file = f;
            ui_open(AP_VIEWER, 0, 0);
        }
    }
    if (code == K_DEL) {
        int f = fi_sel;
        if (fs_name(f)[0] && strcmp(fs_name(f), "settings.dat")) {
            fs_delete(fs_name(f));
            if (fi_sel >= fs_count()) fi_sel = fs_count() - 1;
            if (fi_sel < 0) fi_sel = 0;
        }
    }
    (void)ascii;
}

void app_key(int id, int code, int ascii) {
    switch (id) {
    case AP_TERMINAL: term_key(code, ascii); break;
    case AP_NOTES: notes_key(code, ascii); break;
    case AP_CALENDAR: cal_key(code, ascii); break;
    case AP_MUSIC: music_key(code, ascii); break;
    case AP_CALC: calc_key(code, ascii); break;
    case AP_FINDER: finder_key(code, ascii); break;
    case AP_SETTINGS:
        if (code == K_LEFT && g_theme > 0) g_theme--;
        if (code == K_RIGHT && g_theme < 4) g_theme++;
        if (code == K_LEFT || code == K_RIGHT) { gfx_wall_redraw(g_theme); settings_save(); }
        break;
    default: break;
    }
}

/* ================= клики ================= */
void app_click(int id, int cx, int cy, int w, int h) {
    if (id == AP_MUSIC) {
        /* кнопки play/prev/next внизу */
        int by = h - 30;
        if (cy >= by && cy < by + 22) {
            if (cx >= 8 && cx < 30) { music_key(K_UP, 0); }
            else if (cx >= 34 && cx < 62) music_key(K_ENTER, 0);
            else if (cx >= 66 && cx < 88) { mu_playing = 0; mu_stop_note(); mu_track = (mu_track + 1) % 3; mu_pos = mu_i = mu_next = 0; }
        } else {
            for (int i = 0; i < 3; i++)
                if (cy >= 8 + i * 22 && cy < 8 + (i + 1) * 22) {
                    mu_playing = 0; mu_stop_note();
                    mu_track = i;
                    mu_pos = mu_i = mu_next = 0;
                    mu_playing = 1;
                }
        }
    } else if (id == AP_CALC) {
        static const char *keys[5][4] = {
            { "C", "n", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "0", "", ".", "=" }
        };
        int bw = (w - 10) / 4, bh = 22;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 4; c++) {
                if (!keys[r][c][0]) continue;
                int bx = 4 + c * bw, by = 40 + r * (bh + 3);
                if (cx >= bx && cx < bx + bw && cy >= by && cy < by + bh) ca_press(keys[r][c]);
            }
    } else if (id == AP_FINDER) {
        int row = cy / 16 - 1;
        if (row >= 0 && row < fs_count()) {
            fi_sel = fi_scroll + row;
            if (fi_sel >= fs_count()) fi_sel = fs_count() - 1;
        }
    } else if (id == AP_NOTES) {
        if (cx < 120) {
            int row = cy / 16 - 2;
            if (row == -2 || row == -1) {
                /* кнопка +: создать */
                char nm[24];
                strcpy(nm, "заметка-", 12);
                itoa(notes_file_count() + 1, nm + strlen(nm));
                strcpy(nm + strlen(nm), ".txt", 8);
                fs_create(nm, "", 0);
                notes_load(fs_find(nm));
                n_sel = 0;
            } else if (row >= 0 && row < notes_file_count()) {
                n_sel = row;
                notes_load(notes_file_at(row));
            }
        }
    } else if (id == AP_CALENDAR) {
        if (cy < 24) {
            if (cx < 24) { cal_m--; if (cal_m < 1) { cal_m = 12; cal_y--; } }
            else if (cx > w - 24) { cal_m++; if (cal_m > 12) { cal_m = 1; cal_y++; } }
        } else if (cy >= 24 && cy < 24 + 6 * 26 && cx < 7 * 34) {
            int row = (cy - 24) / 26, col = cx / 34;
            int d = row * 7 + col + 1;
            if (d <= 31) cal_day = d;
        }
    } else if (id == AP_SETTINGS) {
        for (int i = 0; i < 5; i++) {
            int sx = 14 + i * 54;
            if (cx >= sx && cx < sx + 44 && cy >= 60 && cy < 100) {
                g_theme = i;
                gfx_wall_redraw(g_theme);
                settings_save();
            }
        }
        if (cx >= 14 && cx < 200 && cy >= h - 34 && cy < h - 12) {
            /* сброс */
            extern void fs_wipe(void);
            fs_wipe();
            hw_reboot();
        }
    }
}

/* ================= отрисовка ================= */
static void draw_terminal(int x, int y, int w, int h) {
    (void)w;
    int rows = h / 14;
    int start = t_cnt > rows - 1 ? t_cnt - (rows - 1) : 0;
    int li = start;
    for (int r = 0; r < rows - 1 && li < t_cnt; r++, li++) {
        const char *ln = t_lines[(t_head - t_cnt + li + T_ROWS * 2) % T_ROWS];
        gfx_text(x + 6, y + r * 14, ln, 0xc4e8c8, 1);
    }
    /* строка ввода */
    char line[96];
    strcpy(line, g_user, 24);
    strcpy(line + strlen(line), "@yoroos:~$ ", 16);
    strcpy(line + strlen(line), t_input, 84);
    gfx_text(x + 6, y + (rows - 1) * 14, line, 0x8e7dff, 1);
    if ((pit_ticks / 40) % 2)
        gfx_fill(x + 6 + strlen(line) * 8 + 1, y + (rows - 1) * 14 + 2, 7, 11, 0xc4e8c8);
}

static void draw_notes(int x, int y, int w, int h) {
    /* список слева */
    gfx_fill(x, y, 120, h, 0x000000);
    gfx_blend(x, y, 120, h, 0xffffff, 8);
    btn(x + 4, y + 4, 22, 16, "+", -1, -1, -1);
    int nf = notes_file_count();
    for (int i = 0; i < nf && i < (h - 24) / 16; i++) {
        int f = notes_file_at(i);
        if (i == n_sel) gfx_blend(x + 2, y + 24 + i * 16, 116, 16, theme_accent(g_theme), 150);
        gfx_text(x + 6, y + 25 + i * 16, fs_name(f), i == n_sel ? 0xffffff : theme_text(g_theme), 0);
    }
    /* редактор */
    int ex = x + 124, ew = w - 128;
    int maxc = (ew - 12) / 8;
    if (maxc > 90) maxc = 90;
    int row = 0;
    int cur_drawn = 0;
    static char line[96];
    int i = 0;
    while (i <= n_len && row < (h - 12) / 14) {
        int n = 0;
        while (i + n < n_len && n_buf[i + n] != '\n' && n < maxc) n++;
        memcpy(line, n_buf + i, n);
        line[n] = 0;
        gfx_text(ex + 6, y + 6 + row * 14, line, theme_text(g_theme), 0);
        if (!cur_drawn && n_cur >= i && n_cur <= i + n) {
            int cxp = ex + 6 + (n_cur - i) * 8;
            if ((pit_ticks / 40) % 2) gfx_fill(cxp, y + 6 + row * 14 + 2, 7, 11, theme_text(g_theme));
            cur_drawn = 1;
        }
        i += n;
        if (n_buf[i] == '\n') i++;
        row++;
    }
    if (n_file < 0)
        gfx_text(ex + 10, y + 10, "Нет заметок — нажми +", 0x888899, 0);
}

static const char *cal_months[12] = { "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь" };

static void draw_calendar(int x, int y, int w, int h) {
    (void)h;
    /* шапка */
    {
        char hdr[24];
        char yb[8];
        strcpy(hdr, cal_months[cal_m - 1], 20);
        scat(hdr, " ");
        itoa(cal_y, yb);
        scat(hdr, yb);
        gfx_text_c(x + w / 2, y + 5, hdr, theme_text(g_theme), 0);
    }
    gfx_text(x + 8, y + 5, "<", theme_accent(g_theme), 0);
    gfx_text(x + w - 16, y + 5, ">", theme_accent(g_theme), 0);
    static const char *wd[7] = { "пн", "вт", "ср", "чт", "пт", "сб", "вс" };
    for (int i = 0; i < 7; i++)
        gfx_text(x + 10 + i * 34, y + 22, wd[i], 0x888899, 0);
    /* сетка: первый день месяца */
    /* вычисление дня недели 1-го числа (алгоритм Целлера, упрощ.) */
    int q = 1, mth = cal_m, yr = cal_y;
    if (mth < 3) { mth += 12; yr--; }
    int K = yr % 100, J = yr / 100;
    int h1 = (q + 13 * (mth + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7; /* 0=сб */
    int dow = (h1 + 5) % 7; /* 0=пн */
    int dim = 31;
    if (cal_m == 4 || cal_m == 6 || cal_m == 9 || cal_m == 11) dim = 30;
    if (cal_m == 2) dim = (cal_y % 4 == 0 && (cal_y % 100 != 0 || cal_y % 400 == 0)) ? 29 : 28;
    for (int d = 1; d <= dim; d++) {
        int idx = dow + d - 1;
        int r = idx / 7, c = idx % 7;
        int cx = x + 4 + c * 34, cy = y + 34 + r * 26;
        int sel = d == cal_day;
        int ev = cal_ev_count(cal_y, cal_m, d);
        if (sel) gfx_round(cx, cy, 30, 22, 4, theme_accent(g_theme));
        char b[4];
        itoa(d, b);
        gfx_text_c(cx + 15, cy + 4, b, sel ? 0xffffff : theme_text(g_theme), 0);
        if (ev) gfx_fill(cx + 13, cy + 17, 4, 3, sel ? 0xffffff : 0xff9f0a);
    }
    /* панель событий */
    int py = y + 34 + 5 * 26 + 6;
    gfx_blend(x + 4, py, w - 8, h - (py - y) - 4, 0x000000, 40);
    int shown = 0;
    for (int i = 0; i < c_n && shown < 2; i++) {
        /* событие выбранного дня? */
        char key[11], b[6];
        itoa(cal_y, key);
        strcpy(key + strlen(key), "-", 4);
        itoa(cal_m, b); if (cal_m < 10) strcpy(key + strlen(key), "0", 4);
        strcpy(key + strlen(key), b, 4);
        strcpy(key + strlen(key), "-", 4);
        itoa(cal_day, b); if (cal_day < 10) strcpy(key + strlen(key), "0", 4);
        strcpy(key + strlen(key), b, 4);
        if (!strcmp(c_evs[i].date, key)) {
            gfx_text(x + 10, py + 5 + shown * 15, c_evs[i].text, theme_text(g_theme), 0);
            shown++;
        }
    }
    char inp[44];
    strcpy(inp, "+ ", 4);
    strcpy(inp + strlen(inp), cal_input, 40);
    gfx_text(x + 10, py + h - (py - y) - 16, inp, theme_accent(g_theme), 0);
    if ((pit_ticks / 40) % 2)
        gfx_fill(x + 26 + strlen(cal_input) * 8, py + h - (py - y) - 16 + 2, 7, 11, theme_text(g_theme));
}

static void draw_music(int x, int y, int w, int h) {
    for (int i = 0; i < 3; i++) {
        int sel = i == mu_track;
        if (sel) gfx_blend(x + 4, y + 6 + i * 22, w - 8, 20, theme_accent(g_theme), 140);
        char nb[4];
        itoa(i + 1, nb);
        gfx_text(x + 10, y + 10 + i * 22, nb, 0x888899, 0);
        gfx_text(x + 30, y + 10 + i * 22, trk_names[i], sel ? 0xffffff : theme_text(g_theme), 0);
        if (sel && mu_playing) gfx_text(x + w - 20, y + 10 + i * 22, ">", 0xffffff, 0);
    }
    /* визуализатор */
    int vx = x + 8, vy = y + 8 + 3 * 22 + 6, vh = h - (vy - y) - 34;
    for (int i = 0; i < 24; i++) {
        int bh2 = mu_playing ? 3 + ((pit_ticks * 13 + i * 29) % (vh > 8 ? vh - 6 : 4)) : 2;
        gfx_fill(vx + i * ((w - 16) / 24), vy + vh - bh2, (w - 16) / 24 - 2, bh2, theme_accent(g_theme));
    }
    /* прогресс */
    int total = trk_len[mu_track] * trk_tps[mu_track];
    int pw = w - 104;
    gfx_blend(x + 8, h - 18 + y - y, 0, 0, 0, 0);
    int py = y + h - 16;
    gfx_fill(x + 70, py, pw, 4, 0x33333c);
    int frac = total ? (mu_pos % total) * pw / total : 0;
    gfx_fill(x + 70, py, frac, 4, theme_accent(g_theme));
    /* кнопки */
    btn(x + 8, y + h - 26, 22, 20, "|<", -1, -1, -1);
    btn(x + 34, y + h - 26, 28, 20, mu_playing ? "||" : ">", -1, -1, -1);
    btn(x + 66, y + h - 26, 22, 20, ">|", -1, -1, -1);
}

static void draw_calc(int x, int y, int w, int h) {
    (void)h;
    gfx_fill(x, y, w, 34, 0x14151a);
    char buf[20];
    strcpy(buf, ca_disp, 16);
    int tw = gfx_text_w(buf, 1);
    gfx_text(x + w - tw - 10, y + 10, buf, 0xffffff, 1);
    static const char *keys[5][4] = {
        { "C", "n", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "0", "", ".", "=" }
    };
    int bw = (w - 8) / 4, bh = 22;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            if (!keys[r][c][0]) continue;
            int bx = 4 + c * bw, by = 40 + r * (bh + 3);
            u32 bg = (c == 3) ? 0xff9f0a : r == 0 ? 0x3a3d46 : 0x54575f;
            gfx_round(x + bx + 1, y + by, bw - 4, bh, 5, bg);
            gfx_text_c(x + bx + 1 + (bw - 4) / 2, y + by + 4, keys[r][c], c == 3 || r == 0 ? 0x1b1b1f : 0xffffff, 0);
        }
}

static void draw_finder(int x, int y, int w, int h) {
    gfx_text(x + 8, y + 6, "Файл", 0x888899, 0);
    gfx_text(x + w - 90, y + 6, "Размер", 0x888899, 0);
    gfx_fill(x + 4, y + 20, w - 8, 1, 0x33333c);
    int rows = (h - 26) / 16;
    if (fi_sel < fi_scroll) fi_scroll = fi_sel;
    if (fi_sel >= fi_scroll + rows) fi_scroll = fi_sel - rows + 1;
    for (int i = 0; i < rows; i++) {
        int f = fi_scroll + i;
        if (f >= fs_count()) break;
        const char *nm = fs_name(f);
        int sel = f == fi_sel;
        if (sel) gfx_blend(x + 2, y + 24 + i * 16, w - 4, 16, theme_accent(g_theme), 150);
        gfx_text(x + 8, y + 25 + i * 16, nm, sel ? 0xffffff : theme_text(g_theme), 0);
        char b[12];
        itoa(fs_size(f), b);
        gfx_text(x + w - 8 - gfx_text_w(b, 0) - 8, y + 25 + i * 16, b, sel ? 0xdddddd : 0x888899, 0);
    }
    if (!fs_count())
        gfx_text_c(x + w / 2, y + 40, "Диск пуст", 0x888899, 0);
}

static void draw_viewer(int x, int y, int w, int h) {
    if (vi_file < 0 || !fs_name(vi_file)[0]) {
        gfx_text(x + 10, y + 10, "Файл не найден", 0x888899, 0);
        return;
    }
    static char data[2048];
    int n = fs_read(fs_name(vi_file), data, sizeof(data) - 1);
    if (n < 0) n = 0;
    data[n] = 0;
    int maxc = (w - 16) / 8;
    int row = 0, i = 0;
    static char line[96];
    while (i < n && row < (h - 10) / 14) {
        int m = 0;
        while (i + m < n && data[i + m] != '\n' && m < maxc) m++;
        memcpy(line, data + i, m);
        line[m] = 0;
        gfx_text(x + 8, y + 6 + row * 14, line, theme_text(g_theme), 0);
        i += m;
        if (data[i] == '\n') i++;
        row++;
    }
}

static void draw_settings(int x, int y, int w, int h) {
    gfx_text(x + 12, y + 10, "Оформление — тема:", theme_text(g_theme), 0);
    for (int i = 0; i < 5; i++) {
        int sx = 14 + i * 54;
        gfx_round(x + sx, y + 60, 44, 40, 6, i == g_theme ? theme_accent(g_theme) : 0x000000);
        gfx_round(x + sx + 2, y + 62, 40, 36, 5, theme_top(i));
        if (i == g_theme) gfx_text_c(x + sx + 22, y + 76, "•", 0xffffff, 0);
    }
    char buf[80];
    strcpy(buf, "Пользователь: ", 20);
    strcpy(buf + strlen(buf), g_user, 24);
    gfx_text(x + 12, y + 112, buf, theme_text(g_theme), 0);
    strcpy(buf, "Серийный номер: ", 20);
    strcpy(buf + strlen(buf), g_serial, 16);
    gfx_text(x + 12, y + 128, buf, theme_text(g_theme), 0);
    strcpy(buf, "Файлов на диске: ", 24);
    itoa(fs_count(), buf + strlen(buf));
    gfx_text(x + 12, y + 144, buf, theme_text(g_theme), 0);
    /* опасная зона */
    int bx = 14, by = h - 34;
    gfx_round(x + bx, y + by, 190, 22, 6, 0xc0392b);
    gfx_text_c(x + bx + 95, y + by + 4, "Сбросить YoroOS", 0xffffff, 0);
}

static void draw_about(int x, int y, int w, int h) {
    ic_about(x + w / 2 - 24, y + 8, 48);
    gfx_text_c(x + w / 2, y + 62, "YoroOS 1.0 «Натива»", theme_text(g_theme), 0);
    gfx_text_c(x + w / 2, y + 78, "Настоящая ОС: ASM + C", 0x888899, 0);
    char buf[80];
    u32 va, vb, vc, vd;
    char v[13];
    cpuid(0, &va, (u32 *)v, (u32 *)(v + 8), (u32 *)(v + 4));
    v[12] = 0;
    strcpy(buf, "ЦП: ", 8);
    strcpy(buf + strlen(buf), v, 16);
    gfx_text(x + 24, y + 102, buf, theme_text(g_theme), 0);
    strcpy(buf, "Видео: VBE ", 16);
    itoa(gfx_w(), buf + strlen(buf));
    strcpy(buf + strlen(buf), "x", 4);
    itoa(gfx_h(), buf + strlen(buf));
    gfx_text(x + 24, y + 118, buf, theme_text(g_theme), 0);
    strcpy(buf, "Серийный: ", 16);
    strcpy(buf + strlen(buf), g_serial, 16);
    gfx_text(x + 24, y + 134, buf, theme_text(g_theme), 0);
    strcpy(buf, "Аптайм: ", 12);
    itoa((int)pit_uptime_s(), buf + strlen(buf));
    strcpy(buf + strlen(buf), " с", 4);
    gfx_text(x + 24, y + 150, buf, theme_text(g_theme), 0);
}

void app_draw(int id, int x, int y, int w, int h) {
    switch (id) {
    case AP_TERMINAL: draw_terminal(x, y, w, h); break;
    case AP_NOTES: draw_notes(x, y, w, h); break;
    case AP_CALENDAR: draw_calendar(x, y, w, h); break;
    case AP_MUSIC: draw_music(x, y, w, h); break;
    case AP_CALC: draw_calc(x, y, w, h); break;
    case AP_FINDER: draw_finder(x, y, w, h); break;
    case AP_VIEWER: draw_viewer(x, y, w, h); break;
    case AP_SETTINGS: draw_settings(x, y, w, h); break;
    case AP_ABOUT: draw_about(x, y, w, h); break;
    }
}

void app_anim(void) {
    mu_advance();
}
