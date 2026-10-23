/* YoroOS - procedurally drawn application icons (no image assets needed) */
#include "kernel.h"

static const int sin_tab[91] = {
0,17,35,52,70,87,105,122,139,156,174,191,208,225,242,259,276,292,309,326,342,358,
375,391,407,423,438,454,469,485,500,515,530,545,559,574,588,602,616,629,643,656,
669,682,695,707,719,731,743,755,766,777,788,799,809,819,829,839,848,857,866,875,
883,891,899,906,914,921,927,934,940,946,951,956,961,966,970,974,978,982,985,988,
990,993,995,996,998,999,999,1000,1000
};

static int isin(int deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    if (deg <= 90) return sin_tab[deg];
    if (deg <= 180) return sin_tab[180 - deg];
    if (deg <= 270) return -sin_tab[deg - 180];
    return -sin_tab[360 - deg];
}
static int icos(int deg) { return isin(deg + 90); }

typedef struct { int x, y, s; } Ic;

static int S(Ic *i, int v) { return (v * i->s + 16) / 32; }
static int IX(Ic *i, int v) { return i->x + S(i, v); }
static int IY(Ic *i, int v) { return i->y + S(i, v); }

static void tile(Ic *i, int r, Color a, Color b) {
    Rect rc = {i->x, i->y, i->s, i->s};
    if (r >= 32) { fill_round_rect(rc, r, b); return; }
    fill_round_rect(rc, r, a);
    if (b.a) {
        /* soft top highlight */
        Rect hl = {i->x + 1, i->y + 1, i->s - 2, i->s / 2};
        fill_rect_alpha(hl, b);
    }
}

static void pie(Ic *i, int cx, int cy, int rad, int a0, int a1, Color c) {
    int xs[80], ys[80], n = 0, a;
    int cx2 = IX(i, cx), cy2 = IY(i, cy);
    int r2 = S(i, rad);
    xs[n] = cx2; ys[n] = cy2; n++;
    for (a = a0; a <= a1 && n < 78; a += 6) {
        xs[n] = cx2 + r2 * icos(a) / 1000;
        ys[n] = cy2 + r2 * isin(a) / 1000;
        n++;
    }
    xs[n] = cx2 + r2 * icos(a1) / 1000;
    ys[n] = cy2 + r2 * isin(a1) / 1000;
    n++;
    fill_poly(xs, ys, n, c);
}

static void ring(Ic *i, int cx, int cy, int rad, int thick, Color c, int a0, int a1) {
    int a;
    int w = S(i, thick) < 1 ? 1 : S(i, thick);
    for (a = a0; a <= a1; a += 4) {
        int x = IX(i, cx) + S(i, rad) * icos(a) / 1000;
        int y = IY(i, cy) + S(i, rad) * isin(a) / 1000;
        fill_circle(x, y, w, c);
    }
}

static void glyph_chevron(Ic *i, Color c) {
    int y0 = 11, y1 = 16, y2 = 21;
    int x0 = 8, x1 = 14;
    draw_line(IX(i, x0), IY(i, y0), IX(i, x1), IY(i, y1), c);
    draw_line(IX(i, x1), IY(i, y1), IX(i, x0), IY(i, y2), c);
    draw_line(IX(i, x0), IY(i, y0) + 1, IX(i, x1), IY(i, y1) + 1, c);
    draw_line(IX(i, x1), IY(i, y1) + 1, IX(i, x0), IY(i, y2) + 1, c);
    fill_rect((Rect){IX(i, 17), IY(i, 22), S(i, 8), S(i, 2) > 1 ? S(i, 2) : 1}, c);
}

void icon_draw(u32 id, int x, int y, int size) {
    Ic ic = {x, y, size};
    Ic *i = &ic;
    if (size <= 0) return;
    switch (id) {
    case ICON_FOLDER: {
        fill_round_rect((Rect){IX(i, 3), IY(i, 8), S(i, 14), S(i, 12)}, 2, RGB(214, 146, 22));
        {
            Rect r = {IX(i, 3), IY(i, 12), S(i, 26), S(i, 15)};
            fill_round_rect(r, 2, RGB(255, 196, 66));
            fill_rect((Rect){r.x, r.y, r.w, S(i, 4)}, RGB(255, 210, 100));
            fill_rect((Rect){r.x, r.y + r.h - S(i, 2), r.w, S(i, 2)}, RGB(232, 164, 30));
        }
        break;
    }
    case ICON_TERMINAL:
        tile(i, 6, RGB(24, 32, 48), RGBA(255, 255, 255, 12));
        fill_rect((Rect){IX(i, 3), IY(i, 3), S(i, 26), S(i, 5)}, RGB(40, 52, 74));
        fill_circle(IX(i, 6), IY(i, 6), S(i, 1) > 0 ? S(i, 1) : 1, RGB(255, 95, 86));
        fill_circle(IX(i, 9), IY(i, 6), S(i, 1) > 0 ? S(i, 1) : 1, RGB(255, 189, 46));
        fill_circle(IX(i, 12), IY(i, 6), S(i, 1) > 0 ? S(i, 1) : 1, RGB(39, 201, 63));
        {
            Color fg = RGB(140, 230, 190);
            draw_line(IX(i, 7), IY(i, 13), IX(i, 11), IY(i, 17), fg);
            draw_line(IX(i, 11), IY(i, 17), IX(i, 7), IY(i, 21), fg);
            draw_line(IX(i, 7), IY(i, 14), IX(i, 11), IY(i, 17), fg);
            draw_line(IX(i, 11), IY(i, 17), IX(i, 7), IY(i, 20), fg);
            fill_rect((Rect){IX(i, 14), IY(i, 21), S(i, 9), S(i, 2)}, RGB(120, 200, 255));
        }
        break;
    case ICON_SETTINGS: {
        int a;
        tile(i, 7, RGB(64, 108, 180), RGBA(255, 255, 255, 20));
        for (a = 0; a < 360; a += 45) {
            int cx = IX(i, 16) + S(i, 11) * icos(a) / 1000;
            int cy = IY(i, 16) + S(i, 11) * isin(a) / 1000;
            fill_circle(cx, cy, S(i, 3) > 0 ? S(i, 3) : 1, C_WHITE);
        }
        fill_circle(IX(i, 16), IY(i, 16), S(i, 9), C_WHITE);
        fill_circle(IX(i, 16), IY(i, 16), S(i, 4), RGB(64, 108, 180));
        break;
    }
    case ICON_BROWSER:
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(0, 120, 215));
        fill_ellipse(IX(i, 16), IY(i, 16), S(i, 6), S(i, 14), C_WHITE);
        fill_ellipse(IX(i, 16), IY(i, 16), S(i, 4), S(i, 14), RGB(0, 120, 215));
        fill_ellipse(IX(i, 16), IY(i, 16), S(i, 14), S(i, 5), C_WHITE);
        fill_ellipse(IX(i, 16), IY(i, 16), S(i, 14), S(i, 3), RGB(0, 120, 215));
        break;
    case ICON_STORE: {
        fill_round_rect((Rect){IX(i, 5), IY(i, 11), S(i, 22), S(i, 17)}, 3, RGB(0, 120, 215));
        fill_round_rect((Rect){IX(i, 5), IY(i, 11), S(i, 22), S(i, 6)}, 3, RGB(0, 150, 240));
        ring(i, 16, 11, 6, 2, C_WHITE, 180, 360);
        fill_circle(IX(i, 16), IY(i, 19), S(i, 4), C_WHITE);
        fill_circle(IX(i, 16), IY(i, 19), S(i, 2), RGB(0, 120, 215));
        break;
    }
    case ICON_STEAM: {
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(23, 26, 33));
        fill_circle(IX(i, 20), IY(i, 12), S(i, 5), RGB(230, 236, 244));
        fill_circle(IX(i, 12), IY(i, 20), S(i, 6), RGB(180, 190, 205));
        fill_circle(IX(i, 20), IY(i, 12), S(i, 2), RGB(23, 26, 33));
        break;
    }
    case ICON_PYTHON:
        fill_round_rect((Rect){IX(i, 7), IY(i, 3), S(i, 18), S(i, 13)}, 5, RGB(55, 118, 171));
        fill_round_rect((Rect){IX(i, 7), IY(i, 16), S(i, 18), S(i, 13)}, 5, RGB(255, 212, 59));
        fill_circle(IX(i, 18), IY(i, 8), S(i, 2), C_WHITE);
        fill_circle(IX(i, 14), IY(i, 24), S(i, 2), RGB(55, 118, 171));
        break;
    case ICON_VSCODE:
        tile(i, 5, RGB(0, 122, 204), RGBA(255, 255, 255, 20));
        {
            Color w = C_WHITE;
            draw_line(IX(i, 8), IY(i, 10), IX(i, 13), IY(i, 16), w);
            draw_line(IX(i, 13), IY(i, 16), IX(i, 8), IY(i, 22), w);
            draw_line(IX(i, 8), IY(i, 11), IX(i, 13), IY(i, 16), w);
            draw_line(IX(i, 13), IY(i, 16), IX(i, 8), IY(i, 21), w);
            fill_rect((Rect){IX(i, 19), IY(i, 7), S(i, 3), S(i, 18)}, w);
        }
        break;
    case ICON_NOTEPAD: {
        fill_round_rect((Rect){IX(i, 6), IY(i, 3), S(i, 20), S(i, 26)}, 2, C_WHITE);
        fill_rect((Rect){IX(i, 6), IY(i, 3), S(i, 20), S(i, 6)}, RGB(0, 120, 215));
        {
            int k;
            for (k = 0; k < 5; k++)
                fill_rect((Rect){IX(i, 9), IY(i, 13 + k * 3), S(i, 14), S(i, 1)}, RGB(190, 196, 208));
        }
        break;
    }
    case ICON_CALC: {
        tile(i, 5, RGB(38, 44, 58), RGBA(255, 255, 255, 16));
        fill_round_rect((Rect){IX(i, 6), IY(i, 5), S(i, 20), S(i, 6)}, 2, RGB(120, 210, 170));
        {
            int cx, cy;
            for (cy = 0; cy < 3; cy++)
                for (cx = 0; cx < 3; cx++)
                    fill_round_rect((Rect){IX(i, 6 + cx * 7), IY(i, 14 + cy * 6), S(i, 5), S(i, 4)}, 1, RGB(220, 226, 238));
        }
        break;
    }
    case ICON_COMPUTER:
        fill_round_rect((Rect){IX(i, 3), IY(i, 5), S(i, 26), S(i, 17)}, 2, RGB(210, 216, 226));
        fill_rect((Rect){IX(i, 5), IY(i, 7), S(i, 22), S(i, 12)}, RGB(0, 120, 215));
        fill_rect((Rect){IX(i, 5), IY(i, 7), S(i, 22), S(i, 5)}, RGB(90, 180, 240));
        fill_rect((Rect){IX(i, 13), IY(i, 22), S(i, 6), S(i, 3)}, RGB(170, 176, 188));
        fill_round_rect((Rect){IX(i, 8), IY(i, 25), S(i, 16), S(i, 3)}, 1, RGB(210, 216, 226));
        break;
    case ICON_TRASH:
    case ICON_TRASH_EMPTY:
        fill_round_rect((Rect){IX(i, 8), IY(i, 9), S(i, 16), S(i, 18)}, 2, RGB(190, 198, 210));
        fill_round_rect((Rect){IX(i, 6), IY(i, 6), S(i, 20), S(i, 4)}, 2, RGB(150, 158, 172));
        fill_rect((Rect){IX(i, 13), IY(i, 3), S(i, 6), S(i, 3)}, RGB(150, 158, 172));
        fill_rect((Rect){IX(i, 12), IY(i, 12), S(i, 2), S(i, 12)}, RGB(230, 234, 242));
        fill_rect((Rect){IX(i, 18), IY(i, 12), S(i, 2), S(i, 12)}, RGB(230, 234, 242));
        break;
    case ICON_TEXTFILE:
        fill_round_rect((Rect){IX(i, 6), IY(i, 3), S(i, 20), S(i, 26)}, 2, C_WHITE);
        fill_poly((const int[]){IX(i, 19), IX(i, 26), IX(i, 26)},
                  (const int[]){IY(i, 3), IY(i, 3), IY(i, 10)}, 3, RGB(222, 226, 236));
        {
            int k;
            for (k = 0; k < 6; k++)
                fill_rect((Rect){IX(i, 9), IY(i, 12 + k * 3), S(i, 14 - (k % 3) * 2), S(i, 1)}, RGB(0, 120, 215));
        }
        break;
    case ICON_IMAGE:
        fill_round_rect((Rect){IX(i, 3), IY(i, 5), S(i, 26), S(i, 22)}, 2, C_WHITE);
        fill_rect((Rect){IX(i, 5), IY(i, 7), S(i, 22), S(i, 18)}, RGB(160, 210, 250));
        fill_circle(IX(i, 11), IY(i, 12), S(i, 3), RGB(255, 220, 120));
        fill_poly((const int[]){IX(i, 5), IX(i, 13), IX(i, 19), IX(i, 22), IX(i, 27), IX(i, 27)},
                  (const int[]){IY(i, 25), IY(i, 13), IY(i, 19), IY(i, 15), IY(i, 25), IY(i, 25)}, 6, RGB(90, 160, 110));
        break;
    case ICON_MATRONIX: {
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(108, 76, 220));
        fill_circle(IX(i, 16), IY(i, 15), S(i, 11), RGB(150, 110, 245));
        {
            Color w = C_WHITE;
            draw_line(IX(i, 10), IY(i, 22), IX(i, 10), IY(i, 10), w);
            draw_line(IX(i, 10), IY(i, 10), IX(i, 16), IY(i, 18), w);
            draw_line(IX(i, 16), IY(i, 18), IX(i, 22), IY(i, 10), w);
            draw_line(IX(i, 22), IY(i, 10), IX(i, 22), IY(i, 22), w);
            draw_line(IX(i, 10), IY(i, 22), IX(i, 10), IY(i, 10), w);
        }
        break;
    }
    case ICON_CHROME: {
        int cx = IX(i, 16), cy = IY(i, 16), r = S(i, 14);
        fill_circle(cx, cy, r, RGB(219, 68, 55));
        pie(i, 16, 16, 14, -30, 90, RGB(255, 205, 60));
        pie(i, 16, 16, 14, 150, 270, RGB(15, 157, 88));
        fill_circle(cx, cy, S(i, 6), C_WHITE);
        fill_circle(cx, cy, S(i, 5), RGB(66, 133, 244));
        break;
    }
    case ICON_FIREFOX: {
        int cx = IX(i, 16), cy = IY(i, 16), r = S(i, 14);
        fill_circle(cx, cy, r, RGB(255, 148, 0));
        fill_circle(cx, cy - S(i, 2), S(i, 10), RGB(255, 200, 60));
        fill_circle(cx, cy + S(i, 3), S(i, 8), RGB(255, 90, 30));
        fill_circle(cx, cy + S(i, 4), S(i, 6), RGB(255, 220, 120));
        break;
    }
    case ICON_DOWNLOAD:
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(0, 120, 215));
        fill_rect((Rect){IX(i, 14), IY(i, 8), S(i, 4), S(i, 11)}, C_WHITE);
        fill_poly((const int[]){IX(i, 9), IX(i, 23), IX(i, 16)},
                  (const int[]){IY(i, 17), IY(i, 17), IY(i, 25)}, 3, C_WHITE);
        fill_rect((Rect){IX(i, 9), IY(i, 26), S(i, 14), S(i, 2)}, C_WHITE);
        break;
    case ICON_ABOUT:
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(0, 120, 215));
        fill_circle(IX(i, 16), IY(i, 9), S(i, 2) > 0 ? S(i, 2) : 1, C_WHITE);
        fill_rect((Rect){IX(i, 14), IY(i, 13), S(i, 4), S(i, 11)}, C_WHITE);
        break;
    case ICON_POWER:
        ring(i, 16, 17, 9, 2, C_WHITE, 120, 420);
        fill_rect((Rect){IX(i, 15), IY(i, 6), S(i, 2), S(i, 9)}, C_WHITE);
        break;
    case ICON_USER:
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(0, 120, 215));
        fill_circle(IX(i, 16), IY(i, 11), S(i, 5), C_WHITE);
        fill_ellipse(IX(i, 16), IY(i, 26), S(i, 9), S(i, 7), C_WHITE);
        break;
    case ICON_SEARCH:
        ring(i, 14, 14, 8, 2, RGB(90, 98, 112), 0, 360);
        draw_line(IX(i, 20), IY(i, 20), IX(i, 27), IY(i, 27), RGB(90, 98, 112));
        draw_line(IX(i, 20), IY(i, 21), IX(i, 27), IY(i, 28), RGB(90, 98, 112));
        break;
    case ICON_GIT:
        tile(i, 5, RGB(240, 80, 51), RGBA(255, 255, 255, 20));
        {
            Color w = C_WHITE;
            fill_circle(IX(i, 11), IY(i, 9), S(i, 3), w);
            fill_circle(IX(i, 11), IY(i, 23), S(i, 3), w);
            fill_circle(IX(i, 21), IY(i, 14), S(i, 3), w);
            draw_line(IX(i, 11), IY(i, 12), IX(i, 11), IY(i, 20), w);
            draw_line(IX(i, 11), IY(i, 17), IX(i, 21), IY(i, 17), w);
            draw_line(IX(i, 21), IY(i, 17), IX(i, 21), IY(i, 11), w);
        }
        break;
    case ICON_VLC:
        fill_poly((const int[]){IX(i, 16), IX(i, 4), IX(i, 28)},
                  (const int[]){IY(i, 4), IY(i, 26), IY(i, 26)}, 3, RGB(255, 136, 0));
        fill_poly((const int[]){IX(i, 16), IX(i, 9), IX(i, 23)},
                  (const int[]){IY(i, 9), IY(i, 24), IY(i, 24)}, 3, C_WHITE);
        fill_rect((Rect){IX(i, 6), IY(i, 25), S(i, 20), S(i, 3)}, RGB(240, 240, 240));
        break;
    case ICON_DISCORD:
        tile(i, 6, RGB(88, 101, 242), RGBA(255, 255, 255, 18));
        fill_ellipse(IX(i, 16), IY(i, 17), S(i, 10), S(i, 7), C_WHITE);
        fill_circle(IX(i, 12), IY(i, 17), S(i, 2), RGB(88, 101, 242));
        fill_circle(IX(i, 20), IY(i, 17), S(i, 2), RGB(88, 101, 242));
        break;
    case ICON_PAINT:
        fill_circle(IX(i, 16), IY(i, 16), S(i, 14), RGB(240, 244, 250));
        fill_circle(IX(i, 12), IY(i, 11), S(i, 3), RGB(230, 60, 60));
        fill_circle(IX(i, 20), IY(i, 11), S(i, 3), RGB(60, 140, 230));
        fill_circle(IX(i, 11), IY(i, 20), S(i, 3), RGB(60, 190, 100));
        fill_circle(IX(i, 21), IY(i, 20), S(i, 3), RGB(250, 190, 40));
        break;
    case ICON_DRIVE:
        fill_round_rect((Rect){IX(i, 3), IY(i, 9), S(i, 26), S(i, 15)}, 3, RGB(120, 130, 148));
        fill_rect((Rect){IX(i, 5), IY(i, 18), S(i, 22), S(i, 4)}, RGB(90, 98, 114));
        fill_circle(IX(i, 23), IY(i, 13), S(i, 2), RGB(90, 230, 140));
        break;
    case ICON_UP:
        fill_poly((const int[]){IX(i, 16), IX(i, 6), IX(i, 26)},
                  (const int[]){IY(i, 8), IY(i, 20), IY(i, 20)}, 3, RGB(70, 80, 96));
        fill_rect((Rect){IX(i, 13), IY(i, 19), S(i, 6), S(i, 8)}, RGB(70, 80, 96));
        break;
    case ICON_BACK:
        draw_line(IX(i, 8), IY(i, 16), IX(i, 24), IY(i, 16), RGB(70, 80, 96));
        draw_line(IX(i, 9), IY(i, 16), IX(i, 16), IY(i, 9), RGB(70, 80, 96));
        draw_line(IX(i, 9), IY(i, 16), IX(i, 16), IY(i, 23), RGB(70, 80, 96));
        draw_line(IX(i, 8), IY(i, 17), IX(i, 24), IY(i, 17), RGB(70, 80, 96));
        break;
    case ICON_PLUS:
        fill_rect((Rect){IX(i, 14), IY(i, 6), S(i, 4), S(i, 20)}, RGB(70, 80, 96));
        fill_rect((Rect){IX(i, 6), IY(i, 14), S(i, 20), S(i, 4)}, RGB(70, 80, 96));
        break;
    case ICON_WIFI:
        ring(i, 16, 24, 13, 2, C_WHITE, 225, 315);
        ring(i, 16, 24, 8, 2, C_WHITE, 225, 315);
        fill_circle(IX(i, 16), IY(i, 24), S(i, 2), C_WHITE);
        break;
    case ICON_VOLUME:
        fill_poly((const int[]){IX(i, 5), IX(i, 5), IX(i, 10), IX(i, 16), IX(i, 16), IX(i, 10)},
                  (const int[]){IY(i, 13), IY(i, 20), IY(i, 20), IY(i, 26), IY(i, 7), IY(i, 13)}, 6, C_WHITE);
        ring(i, 16, 16, 8, 1, C_WHITE, 300, 420);
        break;
    case ICON_BELL:
        fill_round_rect((Rect){IX(i, 9), IY(i, 8), S(i, 14), S(i, 13)}, 4, C_WHITE);
        fill_rect((Rect){IX(i, 7), IY(i, 20), S(i, 18), S(i, 2)}, C_WHITE);
        fill_circle(IX(i, 16), IY(i, 23), S(i, 2), C_WHITE);
        break;
    case ICON_ARROW_DOWN:
        fill_poly((const int[]){IX(i, 6), IX(i, 26), IX(i, 16)},
                  (const int[]){IY(i, 8), IY(i, 8), IY(i, 24)}, 3, RGB(70, 80, 96));
        break;
    case ICON_CHECK:
        draw_line(IX(i, 6), IY(i, 17), IX(i, 13), IY(i, 24), RGB(60, 180, 110));
        draw_line(IX(i, 13), IY(i, 24), IX(i, 26), IY(i, 8), RGB(60, 180, 110));
        break;
    case ICON_SETUP: {
        fill_round_rect((Rect){IX(i, 5), IY(i, 6), S(i, 22), S(i, 21)}, 3, RGB(0, 120, 215));
        fill_round_rect((Rect){IX(i, 5), IY(i, 6), S(i, 22), S(i, 5)}, 3, RGB(70, 170, 245));
        fill_circle(IX(i, 16), IY(i, 17), S(i, 5), C_WHITE);
        fill_circle(IX(i, 16), IY(i, 17), S(i, 2), RGB(0, 120, 215));
        fill_rect((Rect){IX(i, 14), IY(i, 24), S(i, 4), S(i, 5)}, C_WHITE);
        break;
    }
    case ICON_ZIP: {
        fill_round_rect((Rect){IX(i, 4), IY(i, 7), S(i, 24), S(i, 20)}, 3, RGB(248, 200, 80));
        fill_rect((Rect){IX(i, 4), IY(i, 7), S(i, 10), S(i, 5)}, RGB(214, 160, 40));
        fill_rect((Rect){IX(i, 14), IY(i, 7), S(i, 4), S(i, 20)}, RGB(90, 70, 20));
        {
            int k;
            for (k = 0; k < 5; k++)
                fill_rect((Rect){IX(i, 15), IY(i, 9 + k * 3), S(i, 2), S(i, 1)}, C_WHITE);
        }
        break;
    }
    case ICON_EXE: {
        fill_round_rect((Rect){IX(i, 4), IY(i, 6), S(i, 24), S(i, 20)}, 3, RGB(70, 110, 180));
        fill_rect((Rect){IX(i, 4), IY(i, 6), S(i, 24), S(i, 4)}, RGB(110, 160, 230));
        draw_text(IX(i, 8), IY(i, 11), "EXE", C_WHITE);
        break;
    }
    default:
        tile(i, 6, RGB(90, 140, 220), RGBA(255, 255, 255, 18));
        fill_circle(IX(i, 16), IY(i, 12), S(i, 4), C_WHITE);
        fill_round_rect((Rect){IX(i, 9), IY(i, 18), S(i, 14), S(i, 8)}, 3, C_WHITE);
        break;
    }
}

u32 icon_id(const char *name) {
    if (!strcmp(name, "folder")) return ICON_FOLDER;
    if (!strcmp(name, "terminal")) return ICON_TERMINAL;
    if (!strcmp(name, "settings")) return ICON_SETTINGS;
    if (!strcmp(name, "browser")) return ICON_BROWSER;
    if (!strcmp(name, "store")) return ICON_STORE;
    if (!strcmp(name, "steam")) return ICON_STEAM;
    if (!strcmp(name, "python")) return ICON_PYTHON;
    if (!strcmp(name, "vscode")) return ICON_VSCODE;
    if (!strcmp(name, "notepad")) return ICON_NOTEPAD;
    if (!strcmp(name, "calc")) return ICON_CALC;
    if (!strcmp(name, "computer")) return ICON_COMPUTER;
    if (!strcmp(name, "trash")) return ICON_TRASH;
    if (!strcmp(name, "image")) return ICON_IMAGE;
    if (!strcmp(name, "browser")) return ICON_BROWSER;
    return ICON_APP;
}
