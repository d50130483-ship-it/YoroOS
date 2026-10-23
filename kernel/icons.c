/* YoroOS 0.2 - Complete Fluent Vector Icon Suite (45 Icons + User Avatars) */
#include "kernel.h"

u32 icon_id(const char *name) {
    if (!name) return ICON_APP;
    if (!strcasecmp(name, "folder") || !strcasecmp(name, "explorer")) return ICON_FOLDER;
    if (!strcasecmp(name, "terminal") || !strcasecmp(name, "cmd")) return ICON_TERMINAL;
    if (!strcasecmp(name, "settings")) return ICON_SETTINGS;
    if (!strcasecmp(name, "browser")) return ICON_BROWSER;
    if (!strcasecmp(name, "store")) return ICON_STORE;
    if (!strcasecmp(name, "steam")) return ICON_STEAM;
    if (!strcasecmp(name, "python")) return ICON_PYTHON;
    if (!strcasecmp(name, "vscode") || !strcasecmp(name, "code")) return ICON_VSCODE;
    if (!strcasecmp(name, "notepad")) return ICON_NOTEPAD;
    if (!strcasecmp(name, "calc")) return ICON_CALC;
    if (!strcasecmp(name, "computer") || !strcasecmp(name, "thispc")) return ICON_COMPUTER;
    if (!strcasecmp(name, "trash")) return ICON_TRASH;
    if (!strcasecmp(name, "matronix")) return ICON_MATRONIX;
    if (!strcasecmp(name, "chrome")) return ICON_CHROME;
    if (!strcasecmp(name, "firefox")) return ICON_FIREFOX;
    if (!strcasecmp(name, "git")) return ICON_GIT;
    if (!strcasecmp(name, "vlc")) return ICON_VLC;
    if (!strcasecmp(name, "discord")) return ICON_DISCORD;
    if (!strcasecmp(name, "paint")) return ICON_PAINT;
    if (!strcasecmp(name, "setup") || !strcasecmp(name, "installer")) return ICON_SETUP;
    if (!strcasecmp(name, "zip")) return ICON_ZIP;
    if (!strcasecmp(name, "exe")) return ICON_EXE;
    if (!strcasecmp(name, "taskmgr")) return ICON_TASKMGR;
    if (!strcasecmp(name, "regedit")) return ICON_REGEDIT;
    return ICON_APP;
}

void avatar_draw(int avatar_id, int x, int y, int s) {
    static const Color bg[6] = {
        {0, 120, 212, 255}, {0, 168, 142, 255}, {132, 76, 224, 255},
        {218, 78, 42, 255}, {24, 148, 82, 255}, {44, 62, 92, 255}
    };
    int cx = x + s / 2, cy = y + s / 2;
    fill_circle(cx, cy, s / 2, bg[avatar_id % 6]);
    fill_circle(cx, cy - s / 8, s / 5, C_WHITE);
    fill_round_rect((Rect){cx - s * 28 / 100, cy + s / 9, s * 56 / 100, s * 26 / 100}, s / 8, C_WHITE);
}

void icon_draw(u32 id, int x, int y, int s) {
    int m = s >= 24 ? s / 8 : 2;
    Rect box = {x + m, y + m, s - m * 2, s - m * 2};
    int cx = x + s / 2, cy = y + s / 2;
    int rad = s >= 32 ? 7 : 4;

    switch (id) {
    case ICON_FOLDER: {
        Rect tab = {x + m, y + m + s / 12, s * 42 / 100, s / 5};
        Rect body = {x + m, y + m + s / 5, s - m * 2, s - m * 2 - s / 5};
        fill_round_rect(tab, 3, RGB(214, 152, 22));
        fill_round_rect(body, rad, RGB(252, 198, 52));
        fill_round_rect((Rect){body.x, body.y + body.h / 2, body.w, body.h / 2}, rad, RGB(240, 178, 28));
        if (s >= 28)
            fill_round_rect((Rect){cx - s / 6, body.y + body.h - s / 5, s / 3, s / 8 + 1}, 2, RGB(0, 120, 212));
        break;
    }
    case ICON_TERMINAL: {
        fill_round_rect(box, rad, RGB(24, 28, 36));
        fill_round_rect((Rect){box.x, box.y, box.w / 2, s / 6 + 1}, 3, RGB(72, 82, 102));
        fill_round_rect((Rect){box.x + box.w / 2, box.y, box.w / 2, s / 6 + 1}, 3, RGB(0, 120, 212));
        draw_line(box.x + s / 6, cy - s / 10, cx - s / 14, cy + 1, RGB(96, 224, 255));
        draw_line(cx - s / 14, cy + 1, box.x + s / 6, cy + s / 6, RGB(96, 224, 255));
        fill_rect((Rect){cx + 1, cy + s / 7, s / 4, 2}, C_WHITE);
        break;
    }
    case ICON_SETTINGS: {
        int k;
        fill_round_rect(box, rad, RGB(54, 66, 84));
        for (k = 0; k < 8; k++) {
            static const int dx8[8] = {10, 7, 0, -7, -10, -7, 0, 7};
            static const int dy8[8] = {0, 7, 10, 7, 0, -7, -10, -7};
            fill_circle(cx + dx8[k] * (s * 29 / 100) / 10, cy + dy8[k] * (s * 29 / 100) / 10,
                        s / 12 + 1, RGB(214, 224, 238));
        }
        fill_circle(cx, cy, s * 25 / 100, RGB(232, 240, 250));
        fill_circle(cx, cy, s * 12 / 100, RGB(0, 120, 212));
        break;
    }
    case ICON_BROWSER: {
        fill_circle(cx, cy, s * 39 / 100, RGB(0, 126, 228));
        fill_circle(cx + s / 12, cy - s / 12, s * 27 / 100, RGB(34, 198, 228));
        fill_circle(cx - s / 10, cy + s / 9, s * 21 / 100, RGB(18, 212, 162));
        fill_circle(cx + s / 9, cy + s / 14, s * 12 / 100, RGB(14, 62, 132));
        break;
    }
    case ICON_STORE: {
        Rect bag = {x + m + 1, y + s / 4, s - m * 2 - 2, s * 62 / 100};
        int q = s / 7;
        fill_round_rect(bag, rad, RGB(244, 247, 252));
        draw_round_rect(bag, rad, RGB(185, 196, 214));
        hline(cx - s / 8, cx + s / 8, y + s / 6, RGB(0, 120, 212));
        vline(cx - s / 8, y + s / 6, y + s / 4, RGB(0, 120, 212));
        vline(cx + s / 8, y + s / 6, y + s / 4, RGB(0, 120, 212));
        fill_rect((Rect){cx - q - 1, bag.y + bag.h / 2 - q, q, q}, RGB(242, 80, 34));
        fill_rect((Rect){cx + 1,     bag.y + bag.h / 2 - q, q, q}, RGB(127, 186, 0));
        fill_rect((Rect){cx - q - 1, bag.y + bag.h / 2 + 1, q, q}, RGB(0, 164, 239));
        fill_rect((Rect){cx + 1,     bag.y + bag.h / 2 + 1, q, q}, RGB(255, 185, 0));
        break;
    }
    case ICON_STEAM: {
        fill_circle(cx, cy, s * 39 / 100, RGB(23, 42, 74));
        fill_circle(cx + s / 7, cy - s / 9, s / 6, C_WHITE);
        fill_circle(cx + s / 7, cy - s / 9, s / 12 + 1, RGB(23, 42, 74));
        draw_line(x + m + 2, cy + s / 6, cx + s / 9, cy - s / 12, C_WHITE);
        fill_circle(cx - s / 7, cy + s / 7, s / 8, C_WHITE);
        break;
    }
    case ICON_PYTHON: {
        fill_round_rect(box, rad, RGB(28, 36, 50));
        fill_round_rect((Rect){cx - s / 6, y + s / 5, s / 3, s / 4}, 3, RGB(55, 118, 171));
        fill_round_rect((Rect){x + s / 5, cy - s / 8, s * 2 / 5, s / 4}, 3, RGB(55, 118, 171));
        fill_round_rect((Rect){cx - s / 6, cy + 1, s / 3, s / 4}, 3, RGB(255, 212, 59));
        fill_round_rect((Rect){cx - 2, cy - s / 8, s * 2 / 5, s / 4}, 3, RGB(255, 212, 59));
        break;
    }
    case ICON_VSCODE: {
        fill_round_rect(box, rad, RGB(24, 30, 42));
        draw_line(x + s / 4, y + s / 3, x + s * 3 / 4, y + s * 2 / 3, RGB(0, 152, 255));
        draw_line(x + s / 4, y + s * 2 / 3, x + s * 3 / 4, y + s / 3, RGB(36, 178, 255));
        fill_rect((Rect){x + s * 62 / 100, y + s / 4, s / 7, s / 2}, RGB(0, 122, 204));
        break;
    }
    case ICON_MATRONIX: {
        /* Official Matronix Browser 3.0.0 minimalist #000000 obsidian icon */
        fill_round_rect(box, rad, RGB(8, 8, 12));
        draw_round_rect(box, rad, RGB(104, 118, 146));
        vline(x + s * 28 / 100, y + s * 28 / 100, y + s * 72 / 100, C_WHITE);
        vline(x + s * 28 / 100 + 1, y + s * 28 / 100, y + s * 72 / 100, C_WHITE);
        vline(x + s * 72 / 100, y + s * 28 / 100, y + s * 72 / 100, C_WHITE);
        vline(x + s * 72 / 100 - 1, y + s * 28 / 100, y + s * 72 / 100, C_WHITE);
        draw_line(x + s * 28 / 100, y + s * 28 / 100, cx, cy + 2, RGB(88, 198, 255));
        draw_line(x + s * 72 / 100, y + s * 28 / 100, cx, cy + 2, RGB(88, 198, 255));
        break;
    }
    case ICON_CHROME: {
        fill_circle(cx, cy, s * 38 / 100, RGB(52, 168, 83));
        fill_round_rect((Rect){cx - s * 35 / 100, y + m, s * 70 / 100, s * 33 / 100}, rad, RGB(234, 67, 53));
        fill_circle(cx + s / 8, cy + s / 8, s * 23 / 100, RGB(251, 188, 5));
        fill_circle(cx, cy, s * 17 / 100, C_WHITE);
        fill_circle(cx, cy, s * 13 / 100, RGB(66, 133, 244));
        break;
    }
    case ICON_FIREFOX: {
        fill_circle(cx, cy, s * 38 / 100, RGB(98, 46, 196));
        fill_circle(cx + 2, cy + 2, s * 32 / 100, RGB(255, 113, 57));
        fill_circle(cx + s / 9, cy + s / 9, s * 19 / 100, RGB(255, 196, 46));
        fill_circle(cx - s / 12, cy - s / 12, s * 14 / 100, RGB(114, 58, 212));
        break;
    }
    case ICON_NOTEPAD: {
        int i;
        fill_round_rect(box, rad, RGB(114, 208, 235));
        fill_round_rect((Rect){box.x + 2, box.y + s / 7, box.w - 4, box.h - s / 7 - 2}, 3, C_WHITE);
        for (i = 0; i < 4; i++)
            hline(box.x + 5, box.x + box.w - 6, box.y + s / 4 + i * (s / 8 + 1), RGB(120, 150, 180));
        break;
    }
    case ICON_CALC: {
        int r2, c2;
        fill_round_rect(box, rad, RGB(44, 52, 66));
        fill_round_rect((Rect){box.x + 3, box.y + 3, box.w - 6, s / 4}, 2, RGB(196, 232, 248));
        for (r2 = 0; r2 < 2; r2++)
            for (c2 = 0; c2 < 3; c2++)
                fill_round_rect((Rect){box.x + 3 + c2 * ((box.w - 6) / 3),
                                       box.y + s / 3 + 2 + r2 * (s / 5),
                                       (box.w - 10) / 3, s / 6}, 2,
                                (r2 == 1 && c2 == 2) ? RGB(0, 140, 232) : RGB(84, 94, 112));
        break;
    }
    case ICON_COMPUTER:
    case ICON_DRIVE: {
        Rect mon = {x + m, y + m + 2, s - m * 2, s * 56 / 100};
        fill_round_rect(mon, 4, RGB(34, 42, 54));
        fill_round_rect((Rect){mon.x + 2, mon.y + 2, mon.w - 4, mon.h - 4}, 3, RGB(22, 146, 234));
        fill_rect((Rect){cx - 3, mon.y + mon.h, 6, s / 8}, RGB(160, 170, 186));
        fill_round_rect((Rect){cx - s / 4, mon.y + mon.h + s / 8, s / 2, 3}, 2, RGB(180, 190, 206));
        break;
    }
    case ICON_TRASH:
    case ICON_TRASH_EMPTY: {
        Rect bin = {x + s / 5, y + s / 4, s * 3 / 5, s * 58 / 100};
        fill_round_rect(bin, 4, RGBA(196, 214, 232, 220));
        draw_round_rect(bin, 4, RGB(120, 145, 175));
        fill_round_rect((Rect){bin.x - 2, bin.y - 3, bin.w + 4, 3}, 2, RGB(160, 180, 204));
        if (id == ICON_TRASH)
            fill_round_rect((Rect){bin.x + 3, bin.y + 3, bin.w - 6, bin.h / 2}, 2, RGB(96, 168, 238));
        break;
    }
    case ICON_TEXTFILE:
    case ICON_CODEFILE: {
        Rect doc = {x + s / 5, y + m, s * 3 / 5, s - m * 2};
        fill_round_rect(doc, 3, id == ICON_CODEFILE ? RGB(30, 38, 52) : RGB(245, 248, 252));
        draw_round_rect(doc, 3, RGB(140, 158, 184));
        hline(doc.x + 4, doc.x + doc.w - 5, cy - 3, id == ICON_CODEFILE ? RGB(88, 196, 255) : RGB(130, 146, 170));
        hline(doc.x + 4, doc.x + doc.w - 7, cy + 2, id == ICON_CODEFILE ? RGB(140, 228, 168) : RGB(130, 146, 170));
        break;
    }
    case ICON_IMAGE:
    case ICON_PAINT: {
        fill_round_rect(box, rad, RGB(44, 96, 168));
        fill_circle(x + s * 68 / 100, y + s * 32 / 100, s / 9 + 1, RGB(255, 214, 68));
        {
            int xs[3] = {box.x + 2, cx, box.x + box.w - 2};
            int ys[3] = {box.y + box.h - 2, cy - 2, box.y + box.h - 2};
            fill_poly(xs, ys, 3, RGB(58, 184, 118));
        }
        break;
    }
    case ICON_GIT: {
        int xs[4] = {cx, x + s - m, cx, x + m};
        int ys[4] = {y + m, cy, y + s - m, cy};
        fill_poly(xs, ys, 4, RGB(240, 80, 50));
        draw_line(cx - s / 8, cy - s / 8, cx + s / 9, cy + s / 9, C_WHITE);
        fill_circle(cx, cy, 2, C_WHITE);
        break;
    }
    case ICON_VLC: {
        int xs[4] = {cx - s / 9, cx + s / 9, cx + s * 28 / 100, cx - s * 28 / 100};
        int ys[4] = {y + s / 5, y + s / 5, y + s * 4 / 5, y + s * 4 / 5};
        fill_poly(xs, ys, 4, RGB(255, 136, 0));
        fill_rect((Rect){cx - s / 5, cy - 2, s * 2 / 5, 4}, C_WHITE);
        break;
    }
    case ICON_DISCORD: {
        fill_round_rect(box, rad + 2, RGB(88, 101, 242));
        fill_round_rect((Rect){x + s / 4, y + s / 3, s / 2, s / 3}, 5, C_WHITE);
        fill_circle(cx - s / 10, cy, 2, RGB(88, 101, 242));
        fill_circle(cx + s / 10, cy, 2, RGB(88, 101, 242));
        break;
    }
    case ICON_SETUP:
    case ICON_RESTORE: {
        fill_circle(cx, cy, s * 38 / 100, id == ICON_RESTORE ? RGB(16, 152, 92) : RGB(0, 120, 212));
        fill_circle(cx, cy, s * 26 / 100, C_WHITE);
        fill_circle(cx, cy, s * 11 / 100, id == ICON_RESTORE ? RGB(16, 152, 92) : RGB(0, 120, 212));
        break;
    }
    case ICON_ZIP: {
        int i;
        fill_round_rect(box, rad, RGB(236, 178, 42));
        fill_rect((Rect){cx - 2, box.y, 5, box.h}, RGB(92, 68, 22));
        for (i = 0; i < 4; i++)
            fill_rect((Rect){cx - 2 + (i & 1 ? 2 : 0), box.y + 3 + i * (box.h / 5), 3, 2}, C_WHITE);
        break;
    }
    case ICON_EXE: {
        fill_round_rect(box, rad, RGB(234, 240, 248));
        fill_round_rect((Rect){box.x, box.y, box.w, s / 4}, 3, RGB(0, 120, 212));
        fill_rect((Rect){cx - s / 7, cy, s / 8 + 1, s / 8 + 1}, RGB(0, 164, 239));
        fill_rect((Rect){cx + 2,     cy, s / 8 + 1, s / 8 + 1}, RGB(127, 186, 0));
        fill_rect((Rect){cx - s / 7, cy + s / 7, s / 8 + 1, s / 8 + 1}, RGB(242, 80, 34));
        fill_rect((Rect){cx + 2,     cy + s / 7, s / 8 + 1, s / 8 + 1}, RGB(255, 185, 0));
        break;
    }
    case ICON_TASKMGR: {
        fill_round_rect(box, rad, RGB(22, 34, 48));
        draw_round_rect(box, rad, RGB(48, 178, 224));
        draw_line(box.x + 3, cy + 3, cx - 3, cy + 3, RGB(46, 224, 168));
        draw_line(cx - 3, cy + 3, cx, box.y + 5, RGB(46, 224, 168));
        draw_line(cx, box.y + 5, cx + 4, box.y + box.h - 5, RGB(46, 224, 168));
        draw_line(cx + 4, box.y + box.h - 5, box.x + box.w - 3, cy, RGB(46, 224, 168));
        break;
    }
    case ICON_REGEDIT: {
        int q = s / 4;
        fill_round_rect((Rect){x + m, cy - q / 2, q, q}, 2, RGB(0, 134, 228));
        fill_round_rect((Rect){x + m, cy + q / 2 + 1, q, q}, 2, RGB(0, 108, 198));
        fill_round_rect((Rect){cx, cy + q / 2 + 1, q, q}, 2, RGB(46, 168, 246));
        fill_round_rect((Rect){cx + 2, y + m + 1, q, q}, 2, RGB(88, 198, 255));
        break;
    }
    case ICON_POWER: {
        fill_circle(cx, cy, s / 3, RGBA(232, 62, 74, 42));
        draw_round_rect((Rect){cx - s / 4, cy - s / 4 + 2, s / 2, s / 2}, s / 4, RGB(240, 92, 102));
        vline(cx, y + s / 5, cy, C_WHITE);
        vline(cx + 1, y + s / 5, cy, C_WHITE);
        break;
    }
    case ICON_SEARCH: {
        fill_circle(cx - 2, cy - 2, s / 4, RGBA(255, 255, 255, 210));
        fill_circle(cx - 2, cy - 2, s / 4 - 2, theme->task_bg);
        draw_line(cx + 2, cy + 2, x + s - m - 2, y + s - m - 2, C_WHITE);
        break;
    }
    case ICON_WIFI: {
        int r2;
        for (r2 = 1; r2 <= 3; r2++)
            hline(cx - r2 * (s / 8), cx + r2 * (s / 8), y + s * 3 / 4 - r2 * (s / 7), C_WHITE);
        fill_circle(cx, y + s * 3 / 4, 2, C_WHITE);
        break;
    }
    case ICON_VOLUME: {
        fill_rect((Rect){x + s / 4, cy - 3, 4, 6}, C_WHITE);
        draw_line(x + s / 4 + 4, cy - 3, cx, y + s / 4, C_WHITE);
        draw_line(x + s / 4 + 4, cy + 3, cx, y + s * 3 / 4, C_WHITE);
        vline(cx, y + s / 4, y + s * 3 / 4, C_WHITE);
        vline(cx + 4, cy - 4, cy + 4, RGB(140, 208, 255));
        break;
    }
    case ICON_BELL: {
        fill_round_rect((Rect){cx - s / 5, y + s / 4, s * 2 / 5, s * 2 / 5}, 4, C_WHITE);
        hline(cx - s / 4, cx + s / 4, y + s * 2 / 3, C_WHITE);
        fill_circle(cx, y + s * 3 / 4, 2, C_WHITE);
        break;
    }
    case ICON_UP:
        draw_line(cx, y + s / 4, cx - s / 5, cy, theme->text_main);
        draw_line(cx, y + s / 4, cx + s / 5, cy, theme->text_main);
        vline(cx, y + s / 4, y + s * 3 / 4, theme->text_main);
        break;
    case ICON_BACK:
        draw_line(x + s / 4, cy, cx, cy - s / 5, theme->text_main);
        draw_line(x + s / 4, cy, cx, cy + s / 5, theme->text_main);
        hline(x + s / 4, x + s * 3 / 4, cy, theme->text_main);
        break;
    case ICON_PLUS:
        hline(x + s / 4, x + s * 3 / 4, cy, theme->text_main);
        vline(cx, y + s / 4, y + s * 3 / 4, theme->text_main);
        break;
    case ICON_DOWNLOAD:
    case ICON_ARROW_DOWN:
        vline(cx, y + s / 5, y + s * 7 / 10, RGB(88, 198, 255));
        draw_line(cx - s / 5, cy, cx, y + s * 7 / 10, RGB(88, 198, 255));
        draw_line(cx + s / 5, cy, cx, y + s * 7 / 10, RGB(88, 198, 255));
        hline(x + s / 4, x + s * 3 / 4, y + s * 4 / 5, RGB(88, 198, 255));
        break;
    case ICON_CHECK:
        draw_line(x + s / 5, cy, cx - 2, y + s * 7 / 10, RGB(72, 224, 138));
        draw_line(cx - 2, y + s * 7 / 10, x + s * 4 / 5, y + s * 3 / 10, RGB(72, 224, 138));
        break;
    case ICON_SHIELD:
    case ICON_ABOUT:
    default: {
        /* 4-pane luminous YoroOS 0.2 window mark */
        int q = (s - m * 2 - 3) / 2;
        fill_round_rect((Rect){x + m, y + m, q, q}, 2, RGB(0, 168, 255));
        fill_round_rect((Rect){x + m + q + 3, y + m, q, q}, 2, RGB(48, 206, 255));
        fill_round_rect((Rect){x + m, y + m + q + 3, q, q}, 2, RGB(0, 132, 232));
        fill_round_rect((Rect){x + m + q + 3, y + m + q + 3, q, q}, 2, RGB(0, 214, 184));
        break;
    }
    }
}
