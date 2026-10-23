/* YoroOS - terminal with a real shell over the RAM filesystem */
#include "kernel.h"
#include <stdarg.h>

#define TERM_LINES 320
#define LINE_MAX 200
#define PAD 12

typedef struct {
    char text[TERM_LINES][LINE_MAX];
    u8 color[TERM_LINES];
    int count;
    char input[256];
    int len, caret;
    int cwd;
    int scroll;
    char hist[16][256];
    int hist_n, hist_pos;
} TermState;

static TermState t;

static void term_clear(void) {
    t.count = 0;
    t.scroll = 0;
}

static void push_colored(const char *s, int color) {
    const char *p = s;
    if (t.count >= TERM_LINES) {
        memmove(t.text[0], t.text[1], sizeof(t.text[0]) * (TERM_LINES - 1));
        memmove(t.color, t.color + 1, TERM_LINES - 1);
        t.count = TERM_LINES - 1;
    }
    while (*p && t.count < TERM_LINES) {
        int n = 0;
        while (p[n] && n < LINE_MAX - 1) { t.text[t.count][n] = p[n]; n++; }
        t.text[t.count][n] = 0;
        t.color[t.count] = (u8)color;
        t.count++;
        p += n;
    }
    t.scroll = 0;
}

static void push(const char *s) { push_colored(s, 0); }

static void pushf(const char *fmt, ...) {
    char buf[LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    push(buf);
}

static char *prompt(void) {
    static char p[300];
    static char path[256];
    fs_path(t.cwd, path, sizeof(path));
    sprintf(p, "%s>", path);
    return p;
}

static int resolve(const char *arg) {
    if (!arg || !*arg) return t.cwd;
    return fs_parse_path(t.cwd, arg);
}

static void cmd_dir(const char *arg) {
    int dir = resolve(arg);
    int n, i;
    if (dir < 0) { push_colored("Путь не найден", 1); return; }
    if (!fs_is_dir(dir)) { push_colored("Это файл, а не папка", 1); return; }
    pushf(" Содержимое %s", fs_path(dir, (char[256]){0}, 256));
    push(" ------------------------------------------");
    n = fs_count(dir);
    for (i = 0; i < n; i++) {
        int node = fs_child(dir, i);
        char buf[LINE_MAX];
        if (fs_is_dir(node)) sprintf(buf, "  <DIR>        %s", fs_name(node));
        else sprintf(buf, "  %8d Б   %s", fs_size(node), fs_name(node));
        push(buf);
    }
    pushf(" Итого: %d", n);
}

static void cmd_help(void) {
    push_colored("Команды YoroOS Shell:", 2);
    push("  help            список команд");
    push("  dir | ls        содержимое папки");
    push("  cd <путь>       перейти в папку");
    push("  mkdir <имя>     создать папку");
    push("  type <файл>     показать файл");
    push("  echo <текст>    вывести текст");
    push("  write <ф> <т>   записать текст в файл");
    push("  del <имя>       удалить файл или папку (!)");
    push("  format          стереть весь диск C:");
    push("  apps            установленные приложения");
    push("  install <id>    установить приложение из магазина");
    push("  run <id>        запустить приложение");
    push("  theme <0-5>     сменить тему оформления");
    push("  lang            переключить раскладку RU/EN");
    push("  mem             память");
    push("  sysinfo         сведения о системе");
    push("  neofetch        красивая сводка");
    push("  date/time       дата и время");
    push("  cls             очистить экран");
    push("  reboot          перезагрузка");
    push("  shutdown        выключение");
    push("  exit            закрыть терминал");
}

static const char *art[] = {
    "   __   __            ____  ____  ",
    "   \\ \\ / /___  ____  / __ \\/ ___/ ",
    "    \\ V / __ \\/ __ \\/ / / /\\__ \\  ",
    "     | |/ /_/ / /_/ / /_/ /___/ /  ",
    "     |_|\\____/\\____/\\____//____/   ",
    NULL
};

static void cmd_neofetch(void) {
    int i;
    int h, m, s, d, mo, yy;
    char buf[128];
    rtc_read(&h, &m, &s, &d, &mo, &yy);
    for (i = 0; art[i]; i++) push_colored(art[i], 2);
    pushf("   YoroOS 0.1 \"Astra\"  (i386)");
    push("   ------------------------------");
    pushf("   Ядро:      YoroKernel 0.1 (своё)");
    pushf("   Экран:     %dx%d", (int)fb.width, (int)fb.height);
    pushf("   Память:    %d КиБ / %d МиБ", (int)heap_used(), (int)heap_total() / 1024);
    pushf("   Файлов:    %d узлов", 96);
    pushf("   Тема:      %s", theme->name);
    pushf("   Время:     %02d:%02d:%02d  %02d.%02d.%04d", h, m, s, d, mo, yy);
    pushf("   Приложений:%d", app_count());
    UNUSED(buf);
}

static void cmd_sysinfo(void) {
    int h, m, s, d, mo, yy;
    rtc_read(&h, &m, &s, &d, &mo, &yy);
    push_colored("YoroOS 0.1 \"Astra\"  x86 (i386), 32-bit", 2);
    pushf("Ядро:        YoroKernel 0.1, собственная сборка");
    pushf("Загрузчик:   YoroBoot (свой, 2 стадии)");
    pushf("Графика:     VBE %dx%d, %d bpp", (int)fb.width, (int)fb.height, (int)fb.bpp);
    pushf("Память:      %d МиБ всего, %d КиБ занято", (int)heap_total() / 1024, (int)heap_used());
    pushf("Тиков:       %d", (int)sys_ticks());
    pushf("Дата:        %02d.%02d.%04d  %02d:%02d:%02d", d, mo, yy, h, m, s);
    push("Файловая:    YoroFS (RAM, всё удаляемо)");
}

static void exec(char *line) {
    char *cmd = line;
    char *arg;
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;
    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }

    push_colored(prompt(), 2);
    {
        char tmp[256];
        sprintf(tmp, "%s %s", cmd, arg);
        push(tmp);
    }

    if (!strcmp(cmd, "help")) cmd_help();
    else if (!strcmp(cmd, "dir") || !strcmp(cmd, "ls")) cmd_dir(arg);
    else if (!strcmp(cmd, "cd")) {
        int d = resolve(arg);
        if (arg[0] && d < 0) push_colored("Нет такой папки", 1);
        else if (d >= 0 && !fs_is_dir(d)) push_colored("Это файл", 1);
        else if (d >= 0) t.cwd = d;
    }
    else if (!strcmp(cmd, "pwd")) push(fs_path(t.cwd, (char[256]){0}, 256));
    else if (!strcmp(cmd, "mkdir")) {
        if (!*arg) push_colored("Укажите имя папки", 1);
        else if (fs_mkdir(t.cwd, arg) < 0) push_colored("Не удалось создать папку", 1);
        else push_colored("Папка создана", 2);
    }
    else if (!strcmp(cmd, "type") || !strcmp(cmd, "cat")) {
        int n = resolve(arg);
        if (n < 0) push_colored("Файл не найден", 1);
        else if (fs_is_dir(n)) push_colored("Это папка", 1);
        else push(fs_data(n));
    }
    else if (!strcmp(cmd, "echo")) push(arg);
    else if (!strcmp(cmd, "write")) {
        char name[64];
        int i = 0;
        while (arg[i] && arg[i] != ' ' && i < 63) { name[i] = arg[i]; i++; }
        name[i] = 0;
        if (!name[0] || !arg[i]) push_colored("write <файл> <текст>", 1);
        else {
            int node = fs_lookup(t.cwd, name);
            const char *text = arg + i + 1;
            if (node < 0) node = fs_create(t.cwd, name, text);
            else fs_write(node, text, (int)strlen(text));
            push_colored("Записано", 2);
        }
    }
    else if (!strcmp(cmd, "del") || !strcmp(cmd, "rm") || !strcmp(cmd, "rmdir")) {
        int n = resolve(arg);
        if (!*arg) push_colored("del <имя>", 1);
        else if (n <= 0) push_colored("Удаление невозможно или путь не найден", 1);
        else if (fs_remove(n) == 0) push_colored("Удалено. Здесь можно всё.", 2);
        else push_colored("Ошибка удаления", 1);
    }
    else if (!strcmp(cmd, "format")) {
        fs_reset();
        fs_seed();
        t.cwd = fs_root();
        push_colored("Диск C: отформатирован. Система жива — файлы восстановлены.", 2);
    }
    else if (!strcmp(cmd, "apps")) {
        int i;
        push_colored("Приложения:", 2);
        for (i = 0; i < app_count(); i++) {
            App *a = app_by_index(i);
            char buf[160];
            sprintf(buf, "  %-12s %-24s %s", a->id, a->name, a->installed ? "[установлено]" : "[магазин]");
            push(buf);
        }
    }
    else if (!strcmp(cmd, "install")) {
        App *a = app_find(arg);
        if (!a) push_colored("Нет такого приложения (см. apps)", 1);
        else if (a->installed) push_colored("Уже установлено", 1);
        else {
            app_install(a);
            push_colored("Установлено из Магазина YoroOS", 2);
        }
    }
    else if (!strcmp(cmd, "run") || !strcmp(cmd, "start")) {
        App *a = app_find(arg);
        if (!a) push_colored("Нет такого приложения", 1);
        else {
            push_colored("Запуск...", 2);
            app_launch(a);
        }
    }
    else if (!strcmp(cmd, "theme")) {
        int i = atoi(arg);
        if (arg[0]) {
            theme_set(i);
            push_colored("Тема применена", 2);
        } else pushf("Текущая тема: %s", theme->name);
    }
    else if (!strcmp(cmd, "lang")) {
        kbd_toggle_layout();
        push_colored("Раскладка переключена (RU/EN)", 2);
    }
    else if (!strcmp(cmd, "mem")) {
        pushf("Куча: %d КиБ занято из %d КиБ", (int)heap_used() / 1024, (int)heap_total() / 1024);
        pushf("Тиков с загрузки: %d", (int)sys_ticks());
    }
    else if (!strcmp(cmd, "sysinfo") || !strcmp(cmd, "ver")) cmd_sysinfo();
    else if (!strcmp(cmd, "neofetch")) cmd_neofetch();
    else if (!strcmp(cmd, "date") || !strcmp(cmd, "time")) {
        int h, m, s, d, mo, yy;
        char buf[64];
        rtc_read(&h, &m, &s, &d, &mo, &yy);
        sprintf(buf, "%02d.%02d.%04d  %02d:%02d:%02d", d, mo, yy, h, m, s);
        push(buf);
    }
    else if (!strcmp(cmd, "whoami")) push("Yoro (администратор, и вообще тут всё можно)");
    else if (!strcmp(cmd, "cls") || !strcmp(cmd, "clear")) term_clear();
    else if (!strcmp(cmd, "exit")) {
        Window *w = wm_focused();
        if (w) wm_close(w);
    }
    else if (!strcmp(cmd, "reboot")) { push_colored("Перезагрузка...", 2); system_reboot(); }
    else if (!strcmp(cmd, "shutdown")) { push_colored("Выключение...", 2); system_shutdown(); }
    else {
        push_colored("Неизвестная команда. help — список команд.", 1);
    }
}

static void draw(Window *w, Rect c) {
    int i, y, maxlines, lineh = 20;
    int visible = (c.h - PAD * 2 - 24) / lineh;
    fill_rect(c, RGB(10, 16, 28));
    fill_grad_v(c, RGB(12, 20, 34), RGB(9, 13, 24));
    fill_rect((Rect){c.x, c.y, c.w, 3}, theme->accent);

    maxlines = visible;
    y = c.y + PAD;
    {
        int start = t.count - maxlines + t.scroll;
        if (start < 0) start = 0;
        for (i = start; i < t.count && y < c.y + c.h - 30; i++) {
            Color col = RGB(214, 222, 236);
            if (t.color[i] == 1) col = RGB(255, 120, 110);
            else if (t.color[i] == 2) col = RGB(120, 210, 255);
            draw_text_clip(c.x + PAD, y, t.text[i], col,
                           (Rect){c.x + PAD, c.y, c.w - PAD * 2, c.h});
            y += lineh;
        }
    }
    /* prompt line */
    {
        int py = c.y + c.h - 28;
        int px = c.x + PAD;
        int pw;
        char p[300];
        sprintf(p, "%s ", prompt());
        draw_text(px, py, p, RGB(120, 210, 255));
        pw = text_width(p);
        draw_text_clip(px + pw, py, t.input, RGB(240, 244, 252),
                       (Rect){px + pw, c.y, c.w - pw - PAD, c.h});
        if ((sys_ticks() / 30) % 2 == 0)
            fill_rect((Rect){px + pw + text_width_n(t.input, t.caret), py + 2, 2, FONT_H - 2},
                      RGB(200, 220, 240));
    }
    UNUSED(w);
}

static void on_event(Window *w, YoroEvent *e) {
    if (e->type == EV_WHEEL) {
        t.scroll += e->wheel;
        if (t.scroll > t.count) t.scroll = t.count;
        if (t.scroll < 0) t.scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type != EV_KEY) return;
    if (e->key == KEY_ENTER) {
        t.input[t.len] = 0;
        if (t.len) {
            if (t.hist_n < 16) strcpy(t.hist[t.hist_n++], t.input);
            else {
                memmove(t.hist[0], t.hist[1], sizeof(t.hist[0]) * 15);
                strcpy(t.hist[15], t.input);
            }
            t.hist_pos = t.hist_n;
            exec(t.input);
        } else {
            push(prompt());
        }
        t.len = 0;
        t.caret = 0;
        t.input[0] = 0;
        wm_invalidate();
        return;
    }
    if (e->key == KEY_UP) {
        if (t.hist_pos > 0) {
            t.hist_pos--;
            strcpy(t.input, t.hist[t.hist_pos]);
            t.len = (int)strlen(t.input);
            t.caret = t.len;
            wm_invalidate();
        }
        return;
    }
    if (e->key == KEY_DOWN) {
        if (t.hist_pos < t.hist_n - 1) {
            t.hist_pos++;
            strcpy(t.input, t.hist[t.hist_pos]);
        } else {
            t.hist_pos = t.hist_n;
            t.input[0] = 0;
        }
        t.len = (int)strlen(t.input);
        t.caret = t.len;
        wm_invalidate();
        return;
    }
    if (e->key == KEY_BACKSP) {
        if (t.caret > 0) {
            memmove(t.input + t.caret - 1, t.input + t.caret, (size_t)(t.len - t.caret + 1));
            t.caret--; t.len--;
        }
        wm_invalidate();
        return;
    }
    if (e->key == KEY_LEFT) { if (t.caret > 0) t.caret--; wm_invalidate(); return; }
    if (e->key == KEY_RIGHT) { if (t.caret < t.len) t.caret++; wm_invalidate(); return; }
    if (e->ch >= 32 && t.len < 190) {
        char enc[4];
        int n = utf8_encode((u16)e->ch, enc), k;
        if (t.caret < t.len)
            memmove(t.input + t.caret + n, t.input + t.caret, (size_t)(t.len - t.caret + 1));
        for (k = 0; k < n; k++) t.input[t.caret + k] = enc[k];
        t.caret += n;
        t.len += n;
        t.input[t.len] = 0;
        (void)w;
        wm_invalidate();
    }
}

void terminal_open(void) {
    if (t.cwd <= 0) t.cwd = fs_root();
    if (t.count == 0) {
        push_colored("YoroOS Shell 0.1   (help — список команд)", 2);
        push("Добро пожаловать! В этой системе можно удалить что угодно,");
        push("включая папку Windows. Попробуйте: dir, sysinfo, neofetch.");
        push("");
    }
    wm_open(app_find("terminal"), "Терминал — YoroOS Shell", -1, -1, 820, 500, draw, on_event, NULL);
    wm_invalidate();
}
