/* YoroOS 0.2 - C++20 Fluent Acrylic Frosted Glass, HD Procedural Wallpapers & Cursor Engine */
#include "kernel.h"

namespace yoro::fx {

/* Fixed-point integer sin/cos lookup (quarter wave, 1024 scale) */
static constexpr int kSin64[65] = {
    0, 25, 50, 75, 100, 125, 150, 174, 199, 223, 247, 271, 294, 318, 341, 363,
    386, 408, 430, 451, 472, 492, 512, 532, 551, 570, 588, 606, 623, 640, 656, 672,
    687, 702, 716, 729, 742, 754, 766, 777, 788, 798, 807, 816, 824, 831, 838, 844,
    850, 855, 859, 863, 866, 868, 870, 872, 873, 873, 874, 874, 874, 874, 874, 874, 874
};

static inline int fast_sin256(int ang256) {
    int a = ang256 & 255;
    int neg = 0;
    if (a >= 128) { a -= 128; neg = 1; }
    if (a > 64) a = 128 - a;
    /* Accurate parabolic sine approximation in [-1024..1024]: 4*x*(128-x)*1024/4096 */
    int v = (a * (128 - a)) >> 2;
    return neg ? -v : v;
}

static inline int fast_cos256(int ang256) {
    return fast_sin256(ang256 + 64);
}

static inline u32 hash2d(u32 x, u32 y) {
    u32 h = x * 374761393U + y * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}

static inline u8 clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (u8)v);
}

static inline u32 pack_rgb(int r, int g, int b) {
    return ((u32)clamp8(r) << 16) | ((u32)clamp8(g) << 8) | (u32)clamp8(b);
}

static inline int isqrt32(u32 n) {
    u32 x = n, y = (x + 1U) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return (int)x;
}

/* ---------------- HD Procedural Wallpapers ---------------- */
static void render_aurora_hero(u32 *out, int w, int h) {
    /* Windows 10 Hero + Aurora volumetric light beams through 4-pane window */
    const int wx = w * 60 / 100;
    const int wy = h * 47 / 100;
    for (int y = 0; y < h; y++) {
        int ny = (y * 1024) / (h ? h : 1);
        for (int x = 0; x < w; x++) {
            int nx = (x * 1024) / (w ? w : 1);
            int dx = x - wx;
            int dy = y - wy;
            int dist = isqrt32((u32)(dx * dx + dy * dy));

            /* Deep midnight blue radial base */
            int r = 4 + ((1024 - ny) * 8) / 1024;
            int g = 12 + ((1024 - ny) * 26) / 1024;
            int b = 28 + ((1024 - ny) * 62) / 1024;

            /* Volumetric light rays fanning left from the glowing window */
            int glow = 540 - (dist * 540) / (w * 62 / 100 + 1);
            if (glow > 0) {
                int beam = fast_cos256((dy * 520) / (abs_i(dx) + 140)) + 1024; /* 0..2048 smooth! */
                int intensity = (glow * (beam + 760)) >> 11;
                r += (intensity * 18) >> 8;
                g += (intensity * 138) >> 8;
                b += (intensity * 255) >> 8;
            }

            /* Flowing northern aurora ribbon across upper third */
            int wave_y = (h * 30 / 100) + ((fast_sin256(nx >> 2) * h) >> 13)
                         + ((fast_cos256((nx * 3) >> 3) * h) >> 14);
            int ad = abs_i(y - wave_y);
            int band = 220 - (ad * 220) / (h / 5 + 1);
            if (band > 0) {
                int shimmer = (fast_sin256((nx * 5) >> 2) + 1024) >> 4;
                r += (band * (18 + (shimmer >> 3))) >> 8;
                g += (band * (165 - (shimmer >> 2))) >> 8;
                b += (band * (195 + (shimmer >> 3))) >> 8;
            }

            /* Perspective 4-pane luminous Windows/YoroOS emblem in mist */
            int px_rel = x - wx;
            int half_w = w / 11;
            if (abs_i(px_rel) < half_w) {
                /* Perspective trapezoid height grows to the right */
                int persp_h = (h / 8) + ((px_rel + half_w) * (h / 22)) / (2 * half_w + 1);
                int gap = w / 190 + 3;
                if (abs_i(dy) < persp_h && abs_i(px_rel) > gap && abs_i(dy) > gap) {
                    int edge = persp_h - abs_i(dy);
                    int boost = edge > 18 ? 135 : edge * 7;
                    r += (boost * 95) >> 7;
                    g += (boost * 205) >> 7;
                    b += (boost * 255) >> 7;
                }
            }

            /* Subtle crisp dither to eliminate color banding + stars */
            u32 rnd = hash2d((u32)x, (u32)y);
            int dither = (int)(rnd & 3) - 1;
            if ((rnd & 0x3FFFU) == 0x1A7BU && y < h / 2) {
                r += 110; g += 130; b += 160;
            }
            out[(size_t)y * (size_t)w + (size_t)x] = pack_rgb(r + dither, g + dither, b + dither);
        }
    }
    (void)kSin64;
}

static void render_bloom(u32 *out, int w, int h, bool light_mode) {
    /* Windows 11 sculptural petal folds */
    const int cx = w / 2;
    const int cy = h * 54 / 100;
    const int scale = (w < h ? w : h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r, g, b;
            if (light_mode) {
                r = 232 - (y * 20) / h;
                g = 241 - (y * 14) / h;
                b = 252 - (y * 6) / h;
            } else {
                r = 9 + (y * 8) / h;
                g = 14 + (y * 12) / h;
                b = 30 + (y * 22) / h;
            }
            /* 5 overlapping smooth organic petals */
            for (int k = 0; k < 5; k++) {
                int ang = k * 51 + 18;
                int ox = cx + ((fast_cos256(ang) * scale) >> 13);
                int oy = cy - ((fast_sin256(ang) * scale) >> 13) - (k * scale) / 34;
                int rx = scale * (26 - k * 2) / 100;
                int ry = scale * (20 - k) / 100;
                int dx = x - ox;
                int dy = y - oy;
                /* Soft rotated ellipse */
                int u = (dx * fast_cos256(ang * 2) + dy * fast_sin256(ang * 2)) >> 10;
                int v = (-dx * fast_sin256(ang * 2) + dy * fast_cos256(ang * 2)) >> 10;
                i64 d2 = ((i64)u * u * 1024) / ((i64)rx * rx + 1)
                       + ((i64)v * v * 1024) / ((i64)ry * ry + 1);
                if (d2 < 1024) {
                    int edge = (int)(1024 - d2); /* 1024 center -> 0 rim */
                    int rim = 1024 - edge;       /* bright rim highlight like Win11 Bloom! */
                    int alpha = edge < 96 ? (edge * 220) / 96 : 220;
                    int pr, pg, pb;
                    if (light_mode) {
                        pr = 38 + ((rim * 165) >> 10) + k * 12;
                        pg = 112 + ((rim * 120) >> 10) + k * 6;
                        pb = 238 + ((rim * 17) >> 10);
                    } else {
                        pr = 18 + ((rim * 82) >> 10) + k * 14;
                        pg = 64 + ((rim * 145) >> 10) + k * 5;
                        pb = 175 + ((rim * 80) >> 10);
                    }
                    r = (r * (256 - alpha) + pr * alpha) >> 8;
                    g = (g * (256 - alpha) + pg * alpha) >> 8;
                    b = (b * (256 - alpha) + pb * alpha) >> 8;
                }
            }
            int d = (int)(hash2d((u32)x, (u32)y) & 3) - 1;
            out[(size_t)y * (size_t)w + (size_t)x] = pack_rgb(r + d, g + d, b + d);
        }
    }
}

static void render_landscape(u32 *out, int w, int h, int variant) {
    /* 0 = Siberian Midnight Mountains, 1 = Solar Sunset, 2 = Astrakhan Steppe, 3 = Classic Bliss */
    int horizon = h * 66 / 100;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int nx = (x * 1024) / (w ? w : 1);
            int r = 0, g = 0, b = 0;
            u32 rnd = hash2d((u32)x, (u32)y);

            if (variant == 0) {
                /* Siberian Midnight: aurora sky + 2 mountain layers + mirror lake */
                int sy = y > horizon ? (2 * horizon - y) : y;
                if (sy < 0) sy = 0;
                r = 8 + (sy * 18) / h;
                g = 18 + (sy * 48) / h;
                b = 38 + (sy * 78) / h;
                int aw = (h * 24 / 100) + ((fast_sin256((nx * 3) >> 3) * h) >> 13);
                int ab = 190 - (abs_i(sy - aw) * 190) / (h / 5 + 1);
                if (ab > 0) { g += (ab * 175) >> 8; b += (ab * 135) >> 8; r += (ab * 24) >> 8; }

                int m1 = horizon - (h * 19 / 100) - abs_i((nx % 340) - 170) * h / 900
                         - ((fast_sin256(nx >> 2) * h) >> 15);
                int m2 = horizon - (h * 10 / 100) - abs_i(((nx + 130) % 260) - 130) * h / 1100;
                if (y <= horizon && y > m1) {
                    int snow = (y - m1 < h / 28) ? (h / 28 - (y - m1)) * 6 : 0;
                    r = 22 + snow; g = 32 + snow; b = 56 + snow;
                }
                if (y <= horizon && y > m2) { r = 13; g = 20; b = 36; }
                if (y > horizon) { r = (r * 3) >> 2; g = (g * 3) >> 2; b = (b * 4) / 5 + 10; }
                if (y < m1 && (rnd & 0xFFFU) == 0x221U) { r += 150; g += 160; b += 180; }
            } else if (variant == 1) {
                /* Solar Sunset: glowing sun disc + layered dusk silhouettes */
                int t = (y * 255) / (h ? h : 1);
                r = 38 + (t * 212) / 255;
                g = 22 + (t < 150 ? (t * 95) / 150 : 95 + ((t - 150) * 65) / 105);
                b = 76 + (t < 130 ? (t * 60) / 130 : 60 - ((t - 130) * 45) / 125);
                int dsx = x - w / 2, dsy = y - (h * 52 / 100);
                int sr = h / 7;
                if (dsx * dsx + dsy * dsy < sr * sr) {
                    int stripe = ((y / 5) & 1) && y > h * 53 / 100 ? 35 : 0;
                    r = 255; g = 205 - (dsy + sr) * 55 / (2 * sr) - stripe; b = 92;
                }
                for (int layer = 0; layer < 3; layer++) {
                    int base = h * (62 + layer * 10) / 100
                               + ((fast_sin256((nx >> 2) + layer * 57) * h) >> 14);
                    if (y > base) {
                        r = 52 - layer * 16; g = 18 - layer * 5; b = 58 - layer * 16;
                    }
                }
            } else if (variant == 2) {
                /* Astrakhan Steppe & Volga Golden Dusk */
                int t = (y * 255) / (h ? h : 1);
                r = 26 + (t * 196) / 255;
                g = 52 + (t * 124) / 255;
                b = 112 + (t < 140 ? (t * 48) / 140 : 48 - (t - 140) / 3);
                int dune = h * 60 / 100 + ((fast_sin256(nx >> 2) * h) >> 14);
                if (y > dune && y < h * 76 / 100) {
                    int shade = (y - dune) * 2;
                    r = 198 - shade / 2; g = 142 - shade / 2; b = 74 - shade / 3;
                } else if (y >= h * 76 / 100) {
                    int rip = ((fast_sin256(y * 9 + (nx >> 1)) + 1024) >> 7);
                    r = 44 + rip * 4; g = 86 + rip * 3; b = 128 + rip * 3;
                }
            } else {
                /* Classic Bliss: azure sky + soft clouds + rolling green hills */
                if (y < horizon) {
                    int t = (y * 255) / (horizon ? horizon : 1);
                    r = 38 + (t * 115) / 255;
                    g = 118 + (t * 92) / 255;
                    b = 226 + (t * 26) / 255;
                    /* Fluffy clouds */
                    for (int c = 0; c < 4; c++) {
                        int ccx = w * (18 + c * 23) / 100;
                        int ccy = h * (22 + (c & 1) * 9) / 100;
                        int dx = (x - ccx), dy = (y - ccy) * 2;
                        int rad = w / 10;
                        if (dx * dx + dy * dy < rad * rad) {
                            int a = 150 - (dx * dx + dy * dy) * 150 / (rad * rad);
                            r = (r * (256 - a) + 250 * a) >> 8;
                            g = (g * (256 - a) + 252 * a) >> 8;
                            b = (b * (256 - a) + 255 * a) >> 8;
                        }
                    }
                } else {
                    r = 60; g = 148; b = 38;
                }
                int hill1 = horizon - ((fast_sin256(nx >> 2) * h) >> 13);
                int hill2 = horizon + h / 14 + ((fast_cos256(nx >> 2) * h) >> 14);
                if (y > hill1) {
                    int t = (y - hill1) * 120 / (h / 2 + 1);
                    r = 86 - t / 3; g = 178 - t / 2; b = 42 - t / 5;
                }
                if (y > hill2) {
                    r = 56; g = 142; b = 32;
                }
            }
            int d = (int)(rnd & 3) - 1;
            out[(size_t)y * (size_t)w + (size_t)x] = pack_rgb(r + d, g + d, b + d);
        }
    }
}

static void render_cyber_matrix(u32 *out, int w, int h) {
    int horizon = h * 46 / 100;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 8, g = 13, b = 22;
            if (y > horizon) {
                int dy = y - horizon + 8;
                int z = (h * 42) / dy;
                int gx = ((x - w / 2) * z) / (w / 4 + 1);
                if ((abs_i(gx) & 31) == 0 || (z & 15) == 0) {
                    int fade = 210 - (z * 4 > 200 ? 200 : z * 4);
                    g += (fade * 210) >> 8;
                    b += (fade * 245) >> 8;
                    r += (fade * 25) >> 8;
                }
            } else {
                int glow = 140 - abs_i(y - horizon) * 3;
                if (glow > 0) { g += glow / 2; b += glow; r += glow / 4; }
            }
            out[(size_t)y * (size_t)w + (size_t)x] = pack_rgb(r, g, b);
        }
    }
}

/* ---------------- Two-Pass O(1) Sliding-Window Acrylic Blur ---------------- */
static u32 *g_blur_tmp = nullptr;
static size_t g_blur_cap = 0;

static void acrylic_blur(Rect r, Color tint, int radius) {
    if (!fb.back || radius < 1) { fill_rect_alpha(r, tint); return; }
    Rect cl = gfx_get_clip();
    int x0 = r.x < cl.x ? cl.x : r.x;
    int y0 = r.y < cl.y ? cl.y : r.y;
    int x1 = (r.x + r.w < cl.x + cl.w ? r.x + r.w : cl.x + cl.w) - 1;
    int y1 = (r.y + r.h < cl.y + cl.h ? r.y + r.h : cl.y + cl.h) - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)fb.width) x1 = (int)fb.width - 1;
    if (y1 >= (int)fb.height) y1 = (int)fb.height - 1;
    int bw = x1 - x0 + 1;
    int bh = y1 - y0 + 1;
    if (bw <= 2 || bh <= 2) return;
    if (radius > 16) radius = 16;

    size_t need = (size_t)bw * (size_t)bh;
    if (need > g_blur_cap) {
        u32 *nb = (u32 *)kmalloc(need * sizeof(u32));
        if (!nb) { fill_rect_alpha(r, tint); return; }
        if (g_blur_tmp) kfree(g_blur_tmp);
        g_blur_tmp = nb;
        g_blur_cap = need;
    }

    const u32 win = (u32)(2 * radius + 1);
    const u32 ta = tint.a;
    const u32 ia = 255U - ta;

    /* Pass 1: Horizontal sliding sum per row -> g_blur_tmp */
    for (int y = 0; y < bh; y++) {
        const u32 *src = (const u32 *)(fb.back + (size_t)(y0 + y) * fb.bstride) + x0;
        u32 *dst = g_blur_tmp + (size_t)y * (size_t)bw;
        u32 sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; k++) {
            int ix = k < 0 ? 0 : (k >= bw ? bw - 1 : k);
            u32 p = src[ix];
            sr += (p >> 16) & 0xFFU; sg += (p >> 8) & 0xFFU; sb += p & 0xFFU;
        }
        for (int x = 0; x < bw; x++) {
            dst[x] = ((sr / win) << 16) | ((sg / win) << 8) | (sb / win);
            int rem = x - radius < 0 ? 0 : x - radius;
            int add = x + radius + 1 >= bw ? bw - 1 : x + radius + 1;
            u32 pr = src[rem], pa = src[add];
            sr = sr - ((pr >> 16) & 0xFFU) + ((pa >> 16) & 0xFFU);
            sg = sg - ((pr >> 8) & 0xFFU) + ((pa >> 8) & 0xFFU);
            sb = sb - (pr & 0xFFU) + (pa & 0xFFU);
        }
    }

    /* Pass 2: Vertical sliding sum per column + Acrylic tint composite -> fb.back */
    for (int x = 0; x < bw; x++) {
        u32 sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; k++) {
            int iy = k < 0 ? 0 : (k >= bh ? bh - 1 : k);
            u32 p = g_blur_tmp[(size_t)iy * (size_t)bw + (size_t)x];
            sr += (p >> 16) & 0xFFU; sg += (p >> 8) & 0xFFU; sb += p & 0xFFU;
        }
        for (int y = 0; y < bh; y++) {
            u32 br = sr / win, bg = sg / win, bb = sb / win;
            u32 out_r = (br * ia + (u32)tint.r * ta + 127U) / 255U;
            u32 out_g = (bg * ia + (u32)tint.g * ta + 127U) / 255U;
            u32 out_b = (bb * ia + (u32)tint.b * ta + 127U) / 255U;
            u32 *row = (u32 *)(fb.back + (size_t)(y0 + y) * fb.bstride);
            row[x0 + x] = (out_r << 16) | (out_g << 8) | out_b;

            int rem = y - radius < 0 ? 0 : y - radius;
            int add = y + radius + 1 >= bh ? bh - 1 : y + radius + 1;
            u32 pr = g_blur_tmp[(size_t)rem * (size_t)bw + (size_t)x];
            u32 pa = g_blur_tmp[(size_t)add * (size_t)bw + (size_t)x];
            sr = sr - ((pr >> 16) & 0xFFU) + ((pa >> 16) & 0xFFU);
            sg = sg - ((pr >> 8) & 0xFFU) + ((pa >> 8) & 0xFFU);
            sb = sb - (pr & 0xFFU) + (pa & 0xFFU);
        }
    }
}

} /* namespace yoro::fx */

extern "C" void fx_blur_rect(Rect r, int radius, Color tint) {
    yoro::fx::acrylic_blur(r, tint, radius);
}

extern "C" void fx_acrylic_panel(Rect r, int corner_rad, Color tint, Color border, int shadow) {
    if (shadow > 0) draw_shadow(r, shadow);
    yoro::fx::acrylic_blur(r, tint, 9);
    if (border.a > 0) draw_round_rect(r, corner_rad, border);
}

extern "C" void fx_render_wallpaper(u32 *out, int w, int h, int id) {
    switch (id & 7) {
    case 0: yoro::fx::render_aurora_hero(out, w, h); break;
    case 1: yoro::fx::render_bloom(out, w, h, false); break;
    case 2: yoro::fx::render_bloom(out, w, h, true); break;
    case 3: yoro::fx::render_landscape(out, w, h, 0); break;
    case 4: yoro::fx::render_landscape(out, w, h, 1); break;
    case 5: yoro::fx::render_cyber_matrix(out, w, h); break;
    case 6: yoro::fx::render_landscape(out, w, h, 2); break;
    default: yoro::fx::render_landscape(out, w, h, 3); break;
    }
}

extern "C" void fx_draw_spinner(int cx, int cy, int rad, u32 tick, Color col) {
    for (int i = 0; i < 6; i++) {
        int base = (int)((tick * 5U) & 255U) - i * 20;
        int ease = yoro::fx::fast_sin256(base >> 1);
        int ang = base + (ease >> 5);
        int dx = (yoro::fx::fast_cos256(ang) * rad) >> 10;
        int dy = (yoro::fx::fast_sin256(ang) * rad) >> 10;
        u8 alpha = (u8)(255 - i * 32);
        fill_circle(cx + dx, cy + dy, i < 2 ? 3 : 2, RGBA(col.r, col.g, col.b, alpha));
    }
}

/* Windows 11 crisp 20x24 pointer bitmap: 0=transparent, 1=border, 2=white fill */
static const char *const kCursorRows[21] = {
    "1...................",
    "11..................",
    "121.................",
    "1221................",
    "12221...............",
    "122221..............",
    "1222221.............",
    "12222221............",
    "122222221...........",
    "1222222221..........",
    "12222222221.........",
    "122222222221........",
    "1222222111111.......",
    "12221221............",
    "1221.1221...........",
    "121..1221...........",
    "11....1221..........",
    "1.....1221..........",
    ".......121..........",
    ".......11...........",
    "....................",
};

extern "C" void fx_draw_cursor(int x, int y, int pressed) {
    Rect saved = gfx_get_clip();
    Color fill = pressed ? RGB(214, 236, 255) : C_WHITE;
    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
    for (int r = 0; r < 21; r++) {
        const char *row = kCursorRows[r];
        for (int c = 0; c < 20; c++) {
            if (row[c] == '1' || row[c] == '2')
                blend_px(x + c + 2, y + r + 2, C_BLACK, 85);
        }
    }
    for (int r = 0; r < 21; r++) {
        const char *row = kCursorRows[r];
        for (int c = 0; c < 20; c++) {
            if (row[c] == '1') px(x + c, y + r, RGB(18, 22, 28));
            else if (row[c] == '2') px(x + c, y + r, fill);
        }
    }
    gfx_set_clip(saved);
}
