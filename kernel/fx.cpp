/* YoroOS 0.2.10 - QOI Minimalist Wallpaper Decoder, Frosted Compositor & Crisp Cursor
 *
 * Renders the 3 real generated minimalist wallpapers embedded in kernel/assets_data.c
 * via lossless QOI decode + smooth 16.16 fixed-point bilinear upscaling + anti-banding dither,
 * plus ultra-clean pure minimal gradient options. ZERO Windows emblems or laser slop!
 */
#include "kernel.h"
#include "cursor_data.h"

extern "C" {
extern const u8  g_wp_qoi_0[];
extern const u32 g_wp_qoi_0_len;
extern const u8  g_wp_qoi_1[];
extern const u32 g_wp_qoi_1_len;
extern const u8  g_wp_qoi_2[];
extern const u32 g_wp_qoi_2_len;
}

namespace yoro::fx {

static inline u8 clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (u8)v);
}

static inline u32 pack_rgb(int r, int g, int b) {
    return ((u32)clamp8(r) << 16) | ((u32)clamp8(g) << 8) | (u32)clamp8(b);
}

static inline int fast_sin256(int ang256) {
    int a = ang256 & 255;
    int neg = 0;
    if (a >= 128) { a -= 128; neg = 1; }
    if (a > 64) a = 128 - a;
    int v = (a * (128 - a)) >> 2;
    return neg ? -v : v;
}

static inline int fast_cos256(int ang256) {
    return fast_sin256(ang256 + 64);
}

/* Standard lossless QOI decoder into u32 0x00RRGGBB buffer */
static u32 *decode_qoi(const u8 *src, u32 len, int *out_w, int *out_h) {
    if (!src || len < 22 || src[0] != 'q' || src[1] != 'o' || src[2] != 'i' || src[3] != 'f')
        return nullptr;

    u32 w = ((u32)src[4] << 24) | ((u32)src[5] << 16) | ((u32)src[6] << 8) | (u32)src[7];
    u32 h = ((u32)src[8] << 24) | ((u32)src[9] << 16) | ((u32)src[10] << 8) | (u32)src[11];
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return nullptr;

    size_t npix = (size_t)w * (size_t)h;
    u32 *dst = (u32 *)kmalloc(npix * sizeof(u32));
    if (!dst) return nullptr;

    u32 index[64];
    memset(index, 0, sizeof(index));
    u8 r = 0, g = 0, b = 0, a = 255;
    size_t p = 14, end = (size_t)len - 8;
    int run = 0;

    for (size_t px_i = 0; px_i < npix; px_i++) {
        if (run > 0) {
            run--;
        } else if (p < end) {
            u8 b1 = src[p++];
            if (b1 == 0xFE) {
                r = src[p]; g = src[p + 1]; b = src[p + 2]; p += 3;
            } else if (b1 == 0xFF) {
                r = src[p]; g = src[p + 1]; b = src[p + 2]; a = src[p + 3]; p += 4;
            } else if ((b1 & 0xC0) == 0x00) {
                u32 v = index[b1];
                r = (u8)(v >> 16); g = (u8)(v >> 8); b = (u8)v; a = (u8)(v >> 24);
            } else if ((b1 & 0xC0) == 0x40) {
                r = (u8)(r + ((b1 >> 4) & 3) - 2);
                g = (u8)(g + ((b1 >> 2) & 3) - 2);
                b = (u8)(b + (b1 & 3) - 2);
            } else if ((b1 & 0xC0) == 0x80) {
                int dg = (b1 & 0x3F) - 32;
                u8 b2 = src[p++];
                r = (u8)(r + dg - 8 + ((b2 >> 4) & 0x0F));
                g = (u8)(g + dg);
                b = (u8)(b + dg - 8 + (b2 & 0x0F));
            } else {
                run = (b1 & 0x3F);
            }
            index[(r * 3U + g * 5U + b * 7U + a * 11U) & 63U] =
                ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
        }
        dst[px_i] = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }

    *out_w = (int)w;
    *out_h = (int)h;
    return dst;
}

static bool render_qoi_scaled(u32 *out, int dw, int dh, const u8 *qoi_data, u32 qoi_len) {
    static const int kBayer4[16] = {
        -2,  2, -1,  3,
         4, -4,  3, -3,
        -1,  3, -2,  2,
         3, -3,  4, -4
    };
    int sw = 0, sh = 0;
    u32 *src = decode_qoi(qoi_data, qoi_len, &sw, &sh);
    if (!src) return false;

    for (int y = 0; y < dh; y++) {
        int fy = (dh > 1) ? ((y * (sh - 1) * 256) / (dh - 1)) : 0;
        int sy = fy >> 8, wy = fy & 255;
        int sy1 = (sy + 1 < sh) ? (sy + 1) : sy;
        const u32 *r0 = src + (size_t)sy * (size_t)sw;
        const u32 *r1 = src + (size_t)sy1 * (size_t)sw;

        for (int x = 0; x < dw; x++) {
            int fx = (dw > 1) ? ((x * (sw - 1) * 256) / (dw - 1)) : 0;
            int sx = fx >> 8, wx = fx & 255;
            int sx1 = (sx + 1 < sw) ? (sx + 1) : sx;

            u32 p00 = r0[sx], p10 = r0[sx1];
            u32 p01 = r1[sx], p11 = r1[sx1];
            u32 w00 = (u32)(256 - wx) * (u32)(256 - wy);
            u32 w10 = (u32)wx         * (u32)(256 - wy);
            u32 w01 = (u32)(256 - wx) * (u32)wy;
            u32 w11 = (u32)wx         * (u32)wy;

            int dith = kBayer4[((y & 3) << 2) | (x & 3)] >> 1;
            int cr = (int)((((p00 >> 16) & 255U) * w00 + ((p10 >> 16) & 255U) * w10 +
                            ((p01 >> 16) & 255U) * w01 + ((p11 >> 16) & 255U) * w11) >> 16) + dith;
            int cg = (int)((((p00 >> 8) & 255U) * w00 + ((p10 >> 8) & 255U) * w10 +
                            ((p01 >> 8) & 255U) * w01 + ((p11 >> 8) & 255U) * w11) >> 16) + dith;
            int cb = (int)(((p00 & 255U) * w00 + (p10 & 255U) * w10 +
                            (p01 & 255U) * w01 + (p11 & 255U) * w11) >> 16) + dith;
            out[(size_t)y * (size_t)dw + (size_t)x] = pack_rgb(cr, cg, cb);
        }
    }

    kfree(src);
    return true;
}

/* Clean ultra-minimal calm solid/smooth gradient fallback & minimalist options */
static void render_minimal_gradient(u32 *out, int w, int h, Color c_top, Color c_bot) {
    for (int y = 0; y < h; y++) {
        int r = c_top.r + ((c_bot.r - c_top.r) * y) / (h ? h : 1);
        int g = c_top.g + ((c_bot.g - c_top.g) * y) / (h ? h : 1);
        int b = c_top.b + ((c_bot.b - c_top.b) * y) / (h ? h : 1);
        u32 row_px = pack_rgb(r, g, b);
        u32 *row = out + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) row[x] = row_px;
    }
}

/* ---------------- Two-Pass O(1) Sliding-Window Frosted Glass Blur ---------------- */
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
    switch (id & 3) {
    case 0:
        if (!yoro::fx::render_qoi_scaled(out, w, h, g_wp_qoi_0, g_wp_qoi_0_len))
            yoro::fx::render_minimal_gradient(out, w, h, RGB(15, 29, 58), RGB(28, 62, 92));
        break;
    case 1:
        if (!yoro::fx::render_qoi_scaled(out, w, h, g_wp_qoi_1, g_wp_qoi_1_len))
            yoro::fx::render_minimal_gradient(out, w, h, RGB(68, 52, 48), RGB(168, 96, 56));
        break;
    case 2:
        if (!yoro::fx::render_qoi_scaled(out, w, h, g_wp_qoi_2, g_wp_qoi_2_len))
            yoro::fx::render_minimal_gradient(out, w, h, RGB(14, 18, 22), RGB(22, 48, 38));
        break;
    default:
        yoro::fx::render_minimal_gradient(out, w, h, RGB(17, 24, 39), RGB(30, 41, 59));
        break;
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

extern "C" void fx_draw_cursor(int x, int y, int pressed) {
    /* Arrow pointer in the default Mint style: black fill, white rounded outline.
     * Pixel data is generated by tools/mkcursor.py (anti-aliased coverages). */
    Rect saved = gfx_get_clip();
    int r, c;
    UNUSED(pressed);
    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
    for (r = 0; r < CURSOR_H; r++) {
        for (c = 0; c < CURSOR_W; c++) {
            unsigned a = g_cursor_px[r][c][0] > g_cursor_px[r][c][1] ? g_cursor_px[r][c][0] : g_cursor_px[r][c][1];
            if (a) blend_px(x + c + 1, y + r + 1, C_BLACK, (u8)((a * 50U) / 255U));
        }
    }
    for (r = 0; r < CURSOR_H; r++) {
        for (c = 0; c < CURSOR_W; c++) {
            u8 o = g_cursor_px[r][c][0];
            u8 f = g_cursor_px[r][c][1];
            if (o) blend_px(x + c, y + r, C_WHITE, o);
            if (f) blend_px(x + c, y + r, C_BLACK, f);
        }
    }
    gfx_set_clip(saved);
}
