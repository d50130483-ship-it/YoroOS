/* Графика YoroOS: двойная буферизация, обои, текст, blend */
#include "gfx.h"
#include "font.h"

static int W = 800, H = 600;
static u8 back[MAXW * MAXH * 4];
static u8 wall[MAXW * MAXH * 4];
static u8 *fb;
static int fb_pitch;
static int cx, cy, cw, chh; /* клип */

int g_theme = 0;

/* --- темы --- */
static const u32 th_top[5] = { 0x241a52, 0x0a2a4a, 0x4a1030, 0x0c2e20, 0xd7dbe4 };
static const u32 th_bot[5] = { 0x0b0618, 0x02101f, 0x12040c, 0x03110b, 0x9aa3b5 };
static const u32 th_acc[5] = { 0x8e7dff, 0x38bdf8, 0xfb7185, 0x34d399, 0x2563eb };
static const u32 th_win[5] = { 0x16181d, 0x0e1620, 0x1c1016, 0x0e1a14, 0xf2f3f7 };
static const u32 th_txt[5] = { 0xf2f2f7, 0xe8f2fb, 0xfbeef2, 0xe8fbf2, 0x1d1d20 };

u32 theme_top(int t) { return th_top[t % 5]; }
u32 theme_bottom(int t) { return th_bot[t % 5]; }
u32 theme_accent(int t) { return th_acc[t % 5]; }
u32 theme_winbg(int t) { return th_win[t % 5]; }
u32 theme_text(int t) { return th_txt[t % 5]; }

void gfx_init(u8 *fbptr, int w, int h, int pitch) {
    fb = fbptr;
    W = w <= MAXW ? w : MAXW;
    H = h <= MAXH ? h : MAXH;
    fb_pitch = pitch;
    cx = 0; cy = 0; cw = W; chh = H;
}

int gfx_w(void) { return W; }
int gfx_h(void) { return H; }
u8 *gfx_fb_ptr(void) { return fb; }
int gfx_pitch(void) { return fb_pitch; }

static inline void put4(int x, int y, u32 c) {
    u8 *p = back + (y * W + x) * 4;
    p[0] = c & 0xFF;
    p[1] = (c >> 8) & 0xFF;
    p[2] = (c >> 16) & 0xFF;
    p[3] = 0;
}

static inline u32 get4(int x, int y) {
    const u8 *p = back + (y * W + x) * 4;
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

static inline u32 mix(u32 a, u32 b, int t) { /* t: 0..255 */
    int ar = a >> 16 & 0xFF, ag = a >> 8 & 0xFF, ab = a & 0xFF;
    int br = b >> 16 & 0xFF, bg = b >> 8 & 0xFF, bb = b & 0xFF;
    int r = ar + (((br - ar) * t) >> 8);
    int g = ag + (((bg - ag) * t) >> 8);
    int bl = ab + (((bb - ab) * t) >> 8);
    return ((u32)r << 16) | ((u32)g << 8) | bl;
}

static inline u32 getwall(int x, int y) {
    const u8 *p = wall + (y * W + x) * 4;
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

/* ---- обои ---- */
static u32 isqrt(u32 v) {
    u32 r = v, s = 1;
    while (s < r) { s = (r + v / r) >> 1; r = s - 1; if (s * s <= v) { r = s; break; } }
    while (r * r > v) r--;
    return r;
}

void gfx_wall_redraw(int theme) {
    u32 top = th_top[theme % 5], bot = th_bot[theme % 5];
    for (int y = 0; y < H; y++) {
        u32 c = mix(top, bot, y * 255 / (H - 1 ? H - 1 : 1));
        u8 *p = wall + y * W * 4;
        for (int x = 0; x < W; x++, p += 4) {
            p[0] = c & 0xFF; p[1] = (c >> 8) & 0xFF; p[2] = (c >> 16) & 0xFF; p[3] = 0;
        }
    }
    /* мягкие световые круги */
    u32 acc = th_acc[theme % 5];
    int circles[3][4] = { { W * 3 / 4, H / 3, W / 3, 26 }, { W / 5, H * 2 / 3, W / 4, 20 }, { W / 2, H * 9 / 10, W / 3, 14 } };
    for (int ci = 0; ci < 3; ci++) {
        int ccx = circles[ci][0], ccy = circles[ci][1], r = circles[ci][2], alpha = circles[ci][3];
        for (int y = ccy - r; y <= ccy + r; y++) {
            if (y < 0 || y >= H) continue;
            for (int x = ccx - r; x <= ccx + r; x++) {
                if (x < 0 || x >= W) continue;
                int dx = x - ccx, dy = y - ccy;
                int d = isqrt(dx * dx + dy * dy);
                if (d > r) continue;
                int a = alpha * (r - d) / r;
                u8 *p = wall + (y * W + x) * 4;
                u32 o = p[0] | (p[1] << 8) | (p[2] << 16);
                u32 m = mix(o, acc, a);
                p[0] = m & 0xFF; p[1] = (m >> 8) & 0xFF; p[2] = (m >> 16) & 0xFF;
            }
        }
    }
}

void gfx_copy_wall(void) { memcpy(back, wall, W * H * 4); }

void gfx_blit(void) {
    u8 *dst = fb;
    for (int y = 0; y < H; y++, dst += fb_pitch)
        memcpy(dst, back + y * W * 4, W * 4);
}

/* ---- примитивы ---- */
void gfx_set_clip(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    cx = x; cy = y; cw = w > 0 ? w : 0; chh = h > 0 ? h : 0;
}

void gfx_clip_win(int x, int y, int w, int h) {
    int x2 = cx + cw, y2 = cy + chh;
    int nx = x > cx ? x : cx;
    int ny = y > cy ? y : cy;
    int nx2 = x + w < x2 ? x + w : x2;
    int ny2 = y + h < y2 ? y + h : y2;
    cx = nx; cy = ny; cw = nx2 - nx; chh = ny2 - ny;
    if (cw < 0) cw = 0;
    if (chh < 0) chh = 0;
}

void gfx_px(int x, int y, u32 c) {
    if (x < cx || y < cy || x >= cx + cw || y >= cy + chh) return;
    put4(x, y, c);
}

void gfx_fill(int x, int y, int w, int h, u32 c) {
    int x2 = x + w, y2 = y + h;
    if (x < cx) x = cx;
    if (y < cy) y = cy;
    if (x2 > cx + cw) x2 = cx + cw;
    if (y2 > cy + chh) y2 = cy + chh;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            put4(xx, yy, c);
}

void gfx_blend(int x, int y, int w, int h, u32 c, int a) {
    int x2 = x + w, y2 = y + h;
    if (x < cx) x = cx;
    if (y < cy) y = cy;
    if (x2 > cx + cw) x2 = cx + cw;
    if (y2 > cy + chh) y2 = cy + chh;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            put4(xx, yy, mix(get4(xx, yy), c, a));
}

void gfx_frame(int x, int y, int w, int h, u32 c) {
    gfx_fill(x, y, w, 1, c);
    gfx_fill(x, y + h - 1, w, 1, c);
    gfx_fill(x, y, 1, h, c);
    gfx_fill(x + w - 1, y, 1, h, c);
}

static void round_clip(int x, int y, int w, int h, int r) {
    /* вырезаем уголки текущим клипом через px-проверку невозможно — рисуем сканлайны */
    (void)x; (void)y; (void)w; (void)h; (void)r;
}

void gfx_round(int x, int y, int w, int h, int r, u32 c) {
    for (int yy = 0; yy < h; yy++) {
        int dy = yy < r ? r - yy : (yy >= h - r ? yy - (h - r - 1) : 0);
        int inset = dy > 0 ? r - isqrt(r * r - dy * dy) : 0;
        if (inset > w / 2) inset = w / 2;
        gfx_fill(x + inset, y + yy, w - inset * 2, 1, c);
    }
}

void gfx_round_blend(int x, int y, int w, int h, int r, u32 c, int a) {
    for (int yy = 0; yy < h; yy++) {
        int dy = yy < r ? r - yy : (yy >= h - r ? yy - (h - r - 1) : 0);
        int inset = dy > 0 ? r - isqrt(r * r - dy * dy) : 0;
        if (inset > w / 2) inset = w / 2;
        gfx_blend(x + inset, y + yy, w - inset * 2, 1, c, a);
    }
}

/* ---- текст ---- */
static const unsigned char *glyph(unsigned cp, int mono) {
    const unsigned char *tbl = mono ? (const unsigned char *)font_mono : (const unsigned char *)font_sans;
    for (unsigned i = 0; i < font_count; i++)
        if (font_cps[i] == cp) return tbl + i * 14;
    return NULL;
}

int gfx_text(int x, int y, const char *s, u32 c, int mono) {
    const unsigned char *g;
    int startx = x;
    while (*s) {
        unsigned cp = utf8_next(&s);
        if (cp == '\n') continue;
        if ((g = glyph(cp, mono))) {
            for (int yy = 0; yy < 14; yy++) {
                u8 bits = g[yy];
                for (int xx = 0; xx < 8; xx++)
                    if (bits & (0x80 >> xx)) gfx_px(x + xx, y + yy, c);
            }
        }
        x += 8;
    }
    return x - startx;
}

int gfx_text_w(const char *s, int mono) {
    (void)mono;
    return strlen(s) * 8;
}

void gfx_text_c(int cxp, int y, const char *s, u32 c, int mono) {
    gfx_text(cxp - gfx_text_w(s, mono) / 2, y, s, c, mono);
}
