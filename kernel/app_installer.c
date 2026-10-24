/* YoroOS 0.2.10 - Text-mode installer (console style, like a Linux installer).
 *
 * No windows, no wizard pages and no mouse. Everything is typed:
 *   1. pick the target disk with 1-9, press Enter
 *   2. type a username (Enter keeps "user"), press Enter
 *   3. type "yes" to confirm the erase, press Enter
 *   4. the install runs and logs every stage; press Enter to reboot
 * The install engine itself (diskfmt.c: disk_install_begin/step) is unchanged.
 */
#include "kernel.h"

enum {
    ST_DISK = 0,   /* choose the disk */
    ST_USER,       /* type the username */
    ST_CONFIRM,    /* type "yes" */
    ST_RUN,        /* writing the disk */
    ST_DONE,       /* finished or failed: wait for Enter */
    ST_RAM         /* no disk at all: run from memory */
};

#define LOG_LINES 8
#define LINE_MAX  96
#define INPUT_MAX 32

static struct {
    int  step;
    int  disk_count;
    int  disk_sel;
    char input[INPUT_MAX];
    int  input_len;
    char log[LOG_LINES][LINE_MAX];
    int  log_n;
    char last_status[LINE_MAX];
    u32  last_tick;
    u32  finish_tick;
    int  auto_reboot;
    DiskInstallTask task;
} g_inst;

static void log_add(const char *s) {
    int i;
    if (g_inst.log_n < LOG_LINES) {
        strncpy(g_inst.log[g_inst.log_n], s, LINE_MAX - 1);
        g_inst.log[g_inst.log_n][LINE_MAX - 1] = 0;
        g_inst.log_n++;
        return;
    }
    for (i = 1; i < LOG_LINES; i++)
        memcpy(g_inst.log[i - 1], g_inst.log[i], LINE_MAX);
    strncpy(g_inst.log[LOG_LINES - 1], s, LINE_MAX - 1);
    g_inst.log[LOG_LINES - 1][LINE_MAX - 1] = 0;
}

static void input_clear(void) {
    g_inst.input[0] = 0;
    g_inst.input_len = 0;
}

static void input_char(int ch) {
    if (ch >= 32 && ch < 127 && g_inst.input_len < INPUT_MAX - 1) {
        g_inst.input[g_inst.input_len++] = (char)ch;
        g_inst.input[g_inst.input_len] = 0;
    }
}

static void input_back(void) {
    if (g_inst.input_len > 0) {
        g_inst.input[--g_inst.input_len] = 0;
    }
}

int installer_running(void) {
    return system_screen_mode == SCREEN_SETUP;
}

void installer_init_boot(void) {
    memset(&g_inst, 0, sizeof(g_inst));
    g_inst.disk_count = hal_disk_count();
    g_inst.step = g_inst.disk_count > 0 ? ST_DISK : ST_RAM;
    log_add("YoroOS installer. Follow the prompts.");
}

static void finish_ram(void) {
    sys_profile.setup_completed = 1;
    apps_init();
    system_screen_mode = SCREEN_DESKTOP;
    wm_invalidate();
}

static void start_install(void) {
    if (!sys_profile.username[0]) strcpy(sys_profile.username, "user");
    if (!sys_profile.pc_name[0]) strcpy(sys_profile.pc_name, "YORO-PC");

    g_inst.step = ST_RUN;
    g_inst.last_tick = sys_ticks();
    log_add("Writing partition table, boot partition and system files...");

    disk_install_begin(&g_inst.task, g_inst.disk_sel, 0);
    if (g_inst.task.failed || !g_inst.task.active) {
        g_inst.step = ST_DONE;
        g_inst.finish_tick = sys_ticks();
        log_add(g_inst.task.err[0] ? g_inst.task.err : "Could not start the installation.");
    }
    wm_invalidate();
}

void installer_tick(void) {
    u32 now = sys_ticks();

    if (g_inst.step == ST_RUN) {
        /* One engine step about every 80 ms, so every stage stays visible */
        if (now - g_inst.last_tick >= 8U) {
            int running;
            g_inst.last_tick = now;
            running = disk_install_step(&g_inst.task);
            if (g_inst.task.status[0] && strcmp(g_inst.task.status, g_inst.last_status) != 0) {
                strncpy(g_inst.last_status, g_inst.task.status, LINE_MAX - 1);
                log_add(g_inst.task.status);
            }
            if (!running) {
                g_inst.step = ST_DONE;
                g_inst.finish_tick = now;
                if (g_inst.task.done && !g_inst.task.failed) {
                    log_add("Installation complete.");
                    g_inst.auto_reboot = (system_screen_mode == SCREEN_SETUP);
                    desktop_rebuild();
                } else {
                    log_add(g_inst.task.err[0] ? g_inst.task.err : "Installation failed.");
                }
            }
            wm_invalidate();
        }
    } else if (g_inst.step == ST_DONE && g_inst.auto_reboot) {
        wm_dirty = 1;
        if (now - g_inst.finish_tick >= 600U) { /* 6 seconds, then reboot into the disk */
            g_inst.auto_reboot = 0;
            system_reboot();
        }
    }
}

void installer_event_fullscreen(YoroEvent *e) {
    if (e->type != EV_KEY) return;

    switch (g_inst.step) {
    case ST_DISK:
        if (e->ch >= '1' && e->ch <= '9' && (e->ch - '1') < g_inst.disk_count)
            g_inst.disk_sel = e->ch - '1';
        if (e->key == KEY_ENTER) {
            input_clear();
            g_inst.step = ST_USER;
        }
        break;

    case ST_USER:
        if (e->key == KEY_ESC) {
            g_inst.step = ST_DISK;
        } else if (e->key == KEY_BACKSP) {
            input_back();
        } else if (e->key == KEY_ENTER) {
            if (g_inst.input[0]) {
                strncpy(sys_profile.username, g_inst.input, sizeof(sys_profile.username) - 1);
                sys_profile.username[sizeof(sys_profile.username) - 1] = 0;
            } else {
                strcpy(sys_profile.username, "user");
            }
            input_clear();
            g_inst.step = ST_CONFIRM;
        } else {
            input_char(e->ch);
        }
        break;

    case ST_CONFIRM:
        if (e->key == KEY_ESC) {
            input_clear();
            g_inst.step = ST_USER;
        } else if (e->key == KEY_BACKSP) {
            input_back();
        } else if (e->key == KEY_ENTER) {
            if (strcmp(g_inst.input, "yes") == 0) {
                start_install();
            } else {
                log_add("Type yes (lowercase) to confirm, or Esc to go back.");
                input_clear();
            }
        } else {
            input_char(e->ch);
        }
        break;

    case ST_RAM:
        if (e->key == KEY_ENTER) finish_ram();
        break;

    case ST_DONE:
        if (e->key == KEY_ENTER) system_reboot();
        break;

    default:
        break;   /* ST_RUN: nothing to type while writing */
    }
    wm_invalidate();
}

static int put_line(int y, const char *s, Color c) {
    draw_text(40, y, s, c);
    return y + font_line_height(FACE_UI) + 6;
}

static int put_log(int y) {
    int i;
    y = put_line(y, "", RGB(150, 150, 150));
    for (i = 0; i < g_inst.log_n; i++)
        y = put_line(y, g_inst.log[i], RGB(170, 170, 170));
    return y;
}

void installer_draw_fullscreen(void) {
    char buf[160];
    char bar[32];
    Color white = RGB(235, 235, 235);
    Color grey = RGB(150, 150, 150);
    Color red = RGB(239, 68, 68);
    int y = 40, i, filled;

    fill_rect((Rect){0, 0, (int)fb.width, (int)fb.height}, RGB(0, 0, 0));
    y = put_line(y, "YoroOS 0.2.10 installer", white);
    y = put_line(y, "--------------------------------------------------", grey);

    switch (g_inst.step) {
    case ST_RAM:
        y = put_line(y, "No disks were found.", white);
        y = put_line(y, "Press Enter to run YoroOS from memory. Nothing will be installed.", grey);
        break;

    case ST_DISK:
        y = put_line(y, "Available disks:", white);
        for (i = 0; i < g_inst.disk_count; i++) {
            HalDiskInfo info;
            memset(&info, 0, sizeof(info));
            hal_disk_info(i, &info);
            snprintf(buf, sizeof(buf), "%s [%d]  %s  %u MiB",
                     (i == g_inst.disk_sel) ? ">" : " ", i + 1, info.model,
                     (unsigned)(info.sectors * info.sector_bytes / 1048576ULL));
            y = put_line(y, buf, (i == g_inst.disk_sel) ? white : grey);
        }
        y = put_line(y, "", grey);
        y = put_line(y, "Type the disk number (1-9), then press Enter.", grey);
        break;

    case ST_USER:
        y = put_line(y, "Username for the new system (Enter keeps \"user\"):", white);
        snprintf(buf, sizeof(buf), "> %s_", g_inst.input);
        y = put_line(y, buf, white);
        y = put_line(y, "Esc goes back to the disk list.", grey);
        break;

    case ST_CONFIRM:
        snprintf(buf, sizeof(buf), "WARNING: all data on disk [%d] will be erased.", g_inst.disk_sel + 1);
        y = put_line(y, buf, red);
        y = put_line(y, "Type yes and press Enter to start the installation:", white);
        snprintf(buf, sizeof(buf), "> %s_", g_inst.input);
        y = put_line(y, buf, white);
        y = put_line(y, "Esc goes back to the username.", grey);
        break;

    case ST_RUN:
        snprintf(buf, sizeof(buf), "Installing to disk [%d]... %d%%", g_inst.disk_sel + 1, g_inst.task.pct);
        y = put_line(y, buf, white);
        filled = g_inst.task.pct / 5;
        if (filled > 20) filled = 20;
        bar[0] = '[';
        for (i = 0; i < 20; i++) bar[1 + i] = i < filled ? '#' : '.';
        bar[21] = ']';
        bar[22] = 0;
        y = put_line(y, bar, white);
        y = put_log(y);
        break;

    case ST_DONE:
        if (g_inst.task.failed || !g_inst.task.done) {
            y = put_line(y, "Installation FAILED.", red);
            y = put_log(y);
            y = put_line(y, "Press Enter to reboot.", grey);
        } else {
            y = put_line(y, "Installation complete.", white);
            y = put_log(y);
            y = put_line(y, g_inst.auto_reboot ? "Rebooting in a few seconds. Press Enter to reboot now."
                                               : "Press Enter to reboot.", grey);
        }
        break;

    default:
        break;
    }
}
