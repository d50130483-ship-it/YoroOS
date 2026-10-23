/* YoroOS 0.2 - Windows Terminal (CMD + PowerShell + POSIX + winget + PE .EXE + ZIP Shell) */
#include "kernel.h"

#define TERM_BUF_CAP 16384
#define HIST_MAX     16

typedef struct {
    int  cwd;
    char buf[TERM_BUF_CAP];
    int  blen;
    char cmd[192];
    int  clen, caret;
    char hist[HIST_MAX][192];
    int  nhist, hpos;
    int  fg_preset; /* 0 = white/cyan, 1 = matrix green, 2 = amber */
} TermState;

static void tout(TermState *st, const char *s) {
    int n = (int)strlen(s);
    if (st->blen + n >= TERM_BUF_CAP - 1) {
        int drop = TERM_BUF_CAP / 3;
        memmove(st->buf, st->buf + drop, (size_t)(st->blen - drop));
        st->blen -= drop;
    }
    memcpy(st->buf + st->blen, s, (size_t)n);
    st->blen += n;
    st->buf[st->blen] = 0;
}

static void toutf(TermState *st, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tout(st, tmp);
}

static void term_tree(TermState *st, int dir, int depth) {
    int i, cnt = fs_count(dir);
    for (i = 0; i < cnt && depth < 6; i++) {
        int c = fs_child(dir, i), d;
        for (d = 0; d < depth; d++) tout(st, "  │ ");
        toutf(st, "  ├─ %s%s\n", fs_name(c), fs_is_dir(c) ? "\\" : "");
        if (fs_is_dir(c)) term_tree(st, c, depth + 1);
    }
}

static void term_exec(Window *w, TermState *st, const char *raw_line) {
    char line[192], *arg = NULL;
    char cwdp[160];
    int i = 0;

    while (*raw_line == ' ') raw_line++;
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = 0;
    while (line[0] && line[strlen(line) - 1] == ' ') line[strlen(line) - 1] = 0;

    fs_path(st->cwd, cwdp, sizeof(cwdp));
    toutf(st, "%s> %s\n", cwdp, line);
    if (!line[0]) return;

    while (line[i] && line[i] != ' ') i++;
    if (line[i] == ' ') {
        line[i++] = 0;
        while (line[i] == ' ') i++;
        arg = line + i;
    }

    if (!strcasecmp(line, "help") || !strcmp(line, "?")) {
        tout(st,
            "Команды Терминала YoroOS 0.2 «Aurora»:\n"
            "  dir / ls [путь]        — список файлов и папок\n"
            "  cd <путь>              — сменить каталог\n"
            "  type / cat <файл>      — вывести содержимое файла\n"
            "  mkdir <имя>            — создать папку\n"
            "  echo <текст> > <файл>  — записать текст в файл\n"
            "  del / rm [-rf] <путь>  — удалить ЛЮБОЙ файл/системную папку (даже C:\\YoroOS!)\n"
            "  sfc /restore           — мгновенно восстановить системные папки YoroOS\n"
            "  tree [путь]            — дерево каталогов\n"
            "  zip <объект>           — сжать файл/папку в .ZIP архив\n"
            "  unzip <архив.zip>      — распаковать .ZIP архив\n"
            "  winget list            — список пакетов в Центре приложений\n"
            "  winget install <id>    — установить пакет (matronix, steam, python, vscode, chrome, firefox)\n"
            "  python <файл.py>       — выполнить скрипт Python 3.12\n"
            "  <файл.exe> / start <f> — запустить нативный Windows .EXE / любой файл\n"
            "  systeminfo / neofetch  — сведения о системе, UEFI Gen 2, CPU, ОЗУ и дисках\n"
            "  diskpart               — показать физические/виртуальные диски\n"
            "  cls / clear | color 0..2 | reboot | shutdown | exit\n");
    }
    else if (!strcasecmp(line, "cls") || !strcasecmp(line, "clear")) {
        st->blen = 0; st->buf[0] = 0; w->scroll = 0;
    }
    else if (!strcasecmp(line, "exit")) {
        wm_close(w);
        return;
    }
    else if (!strcasecmp(line, "pwd")) {
        toutf(st, "%s\n", cwdp);
    }
    else if (!strcasecmp(line, "ver") || !strcasecmp(line, "uname")) {
        toutf(st, "YoroOS [Version 0.2.3610 «Aurora» — %s]\n", hal_arch_name());
    }
    else if (!strcasecmp(line, "systeminfo") || !strcasecmp(line, "neofetch")) {
        toutf(st,
            "  ██╗   ██╗ ██████╗ ██████╗  ██████╗    %s@%s\n"
            "  ╚██╗ ██╔╝██╔═══██╗██╔══██╗██╔═══██╗   ---------------------------\n"
            "   ╚████╔╝ ██║   ██║██████╔╝██║   ██║   ОС      : %s\n"
            "    ╚██╔╝  ██║   ██║██╔══██╗██║   ██║   Ядро    : %s\n"
            "     ██║   ╚██████╔╝██║  ██║╚██████╔╝   Процессор: %s\n"
            "     ╚═╝    ╚═════╝ ╚═╝  ╚═╝ ╚═════╝    Мышь    : %s\n"
            "                                        Экран   : %ux%u 32bpp Fluent Acrylic\n"
            "                                        Память  : %u КБ исп. / %u МБ куча (%u МБ ОЗУ)\n"
            "                                        Том C:  : %d объектов (%s)\n",
            sys_profile.username, sys_profile.pc_name,
            edition_name(sys_profile.edition), hal_arch_name(), hal_cpu_brand(),
            hal_pointer_driver_name(), fb.width, fb.height,
            (u32)(heap_used() / 1024U), (u32)(heap_total() / (1024U * 1024U)), (u32)hal_ram_mb(),
            fs_total_nodes(), fs_windows_present() ? "Системные файлы целы" : "C:\\YoroOS удалена пользователем!");
    }
    else if (!strcasecmp(line, "dir") || !strcasecmp(line, "ls")) {
        int d = (arg && *arg) ? fs_parse_path(st->cwd, arg) : st->cwd;
        if (d < 0 || !fs_is_dir(d)) tout(st, "Путь не найден.\n");
        else {
            int cnt = fs_count(d), k;
            char dp[160];
            fs_path(d, dp, sizeof(dp));
            toutf(st, " Содержимое папки %s:\n", dp);
            for (k = 0; k < cnt; k++) {
                int c = fs_child(d, k);
                toutf(st, "  %-10s %8d Б   %s\n",
                      fs_is_dir(c) ? "<DIR>" : "<FILE>", fs_size(c), fs_name(c));
            }
            toutf(st, "  Всего объектов: %d\n", cnt);
        }
    }
    else if (!strcasecmp(line, "cd")) {
        if (!arg || !*arg) toutf(st, "%s\n", cwdp);
        else {
            int d = fs_parse_path(st->cwd, arg);
            if (d >= 0 && fs_is_dir(d)) st->cwd = d;
            else tout(st, "Системе не удаётся найти указанный путь.\n");
        }
    }
    else if (!strcasecmp(line, "tree")) {
        int d = (arg && *arg) ? fs_parse_path(st->cwd, arg) : st->cwd;
        if (d >= 0 && fs_is_dir(d)) term_tree(st, d, 0);
    }
    else if (!strcasecmp(line, "type") || !strcasecmp(line, "cat")) {
        int f = arg ? fs_parse_path(st->cwd, arg) : -1;
        if (f < 0 || fs_is_dir(f)) tout(st, "Файл не найден.\n");
        else toutf(st, "%s\n", fs_data(f));
    }
    else if (!strcasecmp(line, "mkdir") || !strcasecmp(line, "md")) {
        if (arg && fs_mkdir_p(st->cwd, arg) >= 0) tout(st, "Каталог создан.\n");
        else tout(st, "Укажите имя каталога.\n");
    }
    else if (!strcasecmp(line, "echo") && arg) {
        char *gt = strchr(arg, '>');
        if (gt) {
            char *fn = gt + 1;
            *gt = 0;
            while (*fn == ' ') fn++;
            fs_create(st->cwd, fn, arg);
            toutf(st, "Записано в %s.\n", fn);
        } else {
            toutf(st, "%s\n", arg);
        }
    }
    else if (!strcasecmp(line, "del") || !strcasecmp(line, "rm") ||
             !strcasecmp(line, "rmdir") || !strcasecmp(line, "rd")) {
        /* Support flags like rm -rf C:\YoroOS or rd /s /q C:\YoroOS! */
        while (arg && (*arg == '-' || *arg == '/')) {
            while (*arg && *arg != ' ') arg++;
            while (*arg == ' ') arg++;
        }
        {
            int n = arg ? fs_parse_path(st->cwd, arg) : -1;
            if (n <= 0) tout(st, "Объект не найден (или указан корень тома C:).\n");
            else {
                if (st->cwd == n) st->cwd = fs_root();
                fs_remove(n);
                tout(st, "[OK] Удалено без каких-либо системных ограничений!\n");
            }
        }
    }
    else if (!strcasecmp(line, "sfc")) {
        fs_restore_system_files();
        tout(st, "[SFC] Все системные папки C:\\YoroOS, C:\\Windows и C:\\Boot успешно восстановлены!\n");
    }
    else if (!strcasecmp(line, "diskpart")) {
        int n = hal_disk_count(), k;
        toutf(st, "Обнаружено дисков: %d\n", n);
        for (k = 0; k < n; k++) {
            HalDiskInfo di;
            hal_disk_info(k, &di);
            toutf(st, "  Диск #%d: %s — %u МБ (%u байт/сектор)\n",
                  k, di.model, (u32)(di.sectors / 2048ULL), di.sector_bytes);
        }
    }
    else if (!strcasecmp(line, "winget")) {
        if (!arg || !strcasecmp(arg, "list")) {
            int k;
            tout(st, "Доступные пакеты YoroOS:\n");
            for (k = 0; k < app_count(); k++) {
                App *a = app_by_index(k);
                toutf(st, "  %-12s  %-22s  [%s]\n",
                      a->id, a->name, a->installed ? "Установлено" : "Доступно");
            }
        } else if (!strncmp(arg, "install ", 8)) {
            App *a = app_find(arg + 8);
            if (a) { app_install(a); toutf(st, "Пакет «%s» успешно установлен!\n", a->name); }
            else tout(st, "Пакет не найден. Введите winget list.\n");
        }
    }
    else if (!strcasecmp(line, "zip") && arg) {
        int n = fs_parse_path(st->cwd, arg);
        char zn[64];
        if (n >= 0 && zip_create_from_node(n, st->cwd, zn, sizeof(zn)) >= 0)
            toutf(st, "Создан архив: %s\n", zn);
        else tout(st, "Объект не найден.\n");
    }
    else if (!strcasecmp(line, "unzip") && arg) {
        int n = fs_parse_path(st->cwd, arg);
        if (n >= 0) {
            int cnt = zip_extract_all_to(fs_raw(n), fs_size(n), st->cwd);
            toutf(st, "Извлечено файлов: %d\n", cnt);
        } else tout(st, "Архив не найден.\n");
    }
    else if (!strcasecmp(line, "python") && arg) {
        int n = fs_parse_path(st->cwd, arg);
        if (n >= 0) {
            char pyout[2048];
            python_run_script(fs_data(n), pyout, sizeof(pyout));
            tout(st, pyout);
        } else tout(st, "Скрипт .py не найден.\n");
    }
    else if (!strcasecmp(line, "color") && arg) {
        st->fg_preset = atoi(arg) % 3;
    }
    else if (!strcasecmp(line, "reboot")) { system_reboot(); }
    else if (!strcasecmp(line, "shutdown")) { system_shutdown(); }
    else if (!strcasecmp(line, "start") && arg) {
        App *a = app_find(arg);
        if (a) app_launch(a);
        else {
            int n = fs_parse_path(st->cwd, arg);
            if (n >= 0) fs_open_any_file(n);
            else tout(st, "Не удалось найти файл или приложение.\n");
        }
    }
    else {
        /* Direct launch of App ID OR native Windows .EXE in cwd/System32! */
        App *a = app_find(line);
        int n = fs_parse_path(st->cwd, line);
        if (n < 0) {
            char with_exe[96];
            snprintf(with_exe, sizeof(with_exe), "%s.exe", line);
            n = fs_parse_path(st->cwd, with_exe);
            if (n < 0) {
                char s32p[128];
                snprintf(s32p, sizeof(s32p), "C:\\YoroOS\\System32\\%s", with_exe);
                n = fs_parse_path(fs_root(), s32p);
            }
        }
        if (n >= 0 && !fs_is_dir(n) && pe_is_pe(fs_raw(n), fs_size(n))) {
            char pelog[2048];
            int ec = 0;
            pe_run(fs_raw(n), fs_size(n), fs_name(n), pelog, sizeof(pelog), &ec);
            tout(st, pelog);
        } else if (n >= 0 && !fs_is_dir(n)) {
            fs_open_any_file(n);
        } else if (a && a->installed) {
            app_launch(a);
        } else {
            toutf(st, "«%s» не является внутренней командой или исполняемым .EXE файлом (введите help).\n", line);
        }
    }
}

static void term_draw(Window *w, Rect r) {
    TermState *st = (TermState *)w->user;
    static const Color kCols[3] = {
        {226, 236, 250, 255}, {92, 242, 148, 255}, {255, 202, 88, 255}
    };
    Color fg = kCols[st->fg_preset % 3];
    int lh = 19, total_lines = 1, vis_lines, start_line, cur_line = 0;
    const char *p = st->buf;
    char prompt[280], cwdp[128];
    int y;

    fill_rect(r, RGB(12, 15, 22));
    while (*p) { if (*p++ == '\n') total_lines++; }

    vis_lines = (r.h - 20) / lh;
    if (vis_lines < 3) vis_lines = 3;
    start_line = total_lines - vis_lines + 1 - w->scroll;
    if (start_line < 0) start_line = 0;

    y = r.y + 8;
    p = st->buf;
    while (*p) {
        char ln[180];
        int k = 0;
        while (*p && *p != '\n' && k < (int)sizeof(ln) - 1) ln[k++] = *p++;
        if (*p == '\n') p++;
        ln[k] = 0;
        if (cur_line >= start_line && y + lh <= r.y + r.h - 24) {
            draw_text_mono(r.x + 12, y, ln, fg);
            y += lh;
        }
        cur_line++;
    }

    fs_path(st->cwd, cwdp, sizeof(cwdp));
    snprintf(prompt, sizeof(prompt), "%s> %s", cwdp, st->cmd);
    draw_text_mono(r.x + 12, y, prompt, RGB(98, 208, 255));
    ui_caret(r.x + 12 + text_width_face(FACE_MONO, prompt), y + 1, 16, C_WHITE);
}

static void term_event(Window *w, YoroEvent *e) {
    TermState *st = (TermState *)w->user;
    if (e->type == EV_WHEEL) {
        w->scroll += e->wheel * 3;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type != EV_KEY) return;

    if (e->ctrl && (e->ch == 'l' || e->ch == 'L')) {
        st->blen = 0; st->buf[0] = 0; wm_invalidate(); return;
    }
    if (e->key == KEY_UP && st->nhist > 0) {
        if (st->hpos > 0) st->hpos--;
        strcpy(st->cmd, st->hist[st->hpos]);
        st->clen = (int)strlen(st->cmd); st->caret = st->clen;
        wm_invalidate();
        return;
    }
    if (e->key == KEY_DOWN && st->nhist > 0) {
        if (st->hpos < st->nhist - 1) {
            st->hpos++;
            strcpy(st->cmd, st->hist[st->hpos]);
        } else st->cmd[0] = 0;
        st->clen = (int)strlen(st->cmd); st->caret = st->clen;
        wm_invalidate();
        return;
    }
    if (e->key == KEY_ENTER) {
        char runbuf[192];
        strcpy(runbuf, st->cmd);
        if (runbuf[0] && st->nhist < HIST_MAX)
            strcpy(st->hist[st->nhist++], runbuf);
        st->hpos = st->nhist;
        st->cmd[0] = 0; st->clen = 0; st->caret = 0; w->scroll = 0;
        term_exec(w, st, runbuf);
        wm_invalidate();
        return;
    }
    if (ui_textfield_edit(st->cmd, sizeof(st->cmd), &st->clen, &st->caret, e))
        wm_invalidate();
}

static void term_close(Window *w) { kfree(w->user); }

void terminal_open_cmd(const char *cmd) {
    TermState *st = (TermState *)kzalloc(sizeof(TermState));
    Window *w;
    if (!st) return;
    st->cwd = fs_root();
    tout(st, "YoroOS 0.2 «Aurora» Terminal [Win32 PE + YoroFS2 + POSIX Shell]\n"
             "(c) 2026 YoroOS Project. Введите «help» или «neofetch» для справки.\n\n");
    w = wm_open(app_find("terminal"), "Терминал YoroOS 0.2", -1, -1, 760, 470, term_draw, term_event, term_close);
    w->user = st;
    if (cmd && *cmd) term_exec(w, st, cmd);
}

void terminal_open(void) { terminal_open_cmd(NULL); }
