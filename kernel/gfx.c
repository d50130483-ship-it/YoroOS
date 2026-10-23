/* YoroOS 0.2 - Portable 2D Graphics Primitives (32/64-bit clean) */
#include "kernel.h"

FBInfo fb;
static Rect clip;

static inline u32 *rowptr(int y) {
    return (u32 *)(fb.back + (size_t)y * fb.bstride);
}

void gfx_set_clip(Rect r) { clip = r; }
Rect gfx_get_clip(void) { return clip; }

void gfx_realloc_backbuffer(void) {
    size_t need = (size_t)fb.bstride * (size_t)fb.height;
    u8 *nbuf = (u8 *)kmalloc(need);
    if (nbuf) {
        memset(nbuf, 0, need);
        if (fb.back && fb.back != fb.vram) kfree(fb.back);
        fb.back = nbuf;
        fb.dual = 1;
    } else if (!fb.back) {
        fb.back = fb.vram;
        fb.bstride = fb.pitch;
        fb.dual = 0;
    }
    clip = (Rect){0, 0, (int)fb.width, (int)fb.height};
}

void gfx_init(void) {
    fb.back = NULL;
    gfx_realloc_backbuffer();
}

void gfx_present(void) { hal_present(); }
void gfx_present_rect(Rect r) { hal_present_rect(r); }

void gfx_clear(Color c) {
    Rect r = {0, 0, (int)fb.width, (int)fb.height};
    gfx_set_clip(r);
    fill_rect(r, c);
}

static inline int in_clip(int x, int y) {
    return x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h;
}

void px(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) return;
    if (!in_clip(x, y)) return;
    rowptr(y)[x] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
}

void blend_px(int x, int y, Color c, u8 a) {
    u32 *p, d;
    u32 r, g, b;
    if (a == 0 || x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) return;
    if (!in_clip(x, y)) return;
    p = &rowptr(y)[x];
    if (a == 255) { *p = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b; return; }
    d = *p;
    r = ((u32)c.r * a + ((d >> 16) & 0xFFU) * (255U - a) + 127U) / 255U;
    g = ((u32)c.g * a + ((d >> 8) & 0xFFU) * (255U - a) + 127U) / 255U;
    b = ((u32)c.b * a + (d & 0xFFU) * (255U - a) + 127U) / 255U;
    *p = (r << 16) | (g << 8) | b;
}

void hline(int x0, int x1, int y, Color c) {
    u32 v, *p;
    if (y < clip.y || y >= clip.y + clip.h || y < 0 || y >= (int)fb.height) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < clip.x) x0 = clip.x;
    if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int)fb.width) x1 = (int)fb.width - 1;
    if (x0 > x1) return;
    v = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
    p = rowptr(y);
    while (x0 <= x1) p[x0++] = v;
}

void vline(int x, int y0, int y1, Color c) {
    u32 v;
    if (x < clip.x || x >= clip.x + clip.w || x < 0 || x >= (int)fb.width) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < clip.y) y0 = clip.y;
    if (y1 >= clip.y + clip.h) y1 = clip.y + clip.h - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= (int)fb.height) y1 = (int)fb.height - 1;
    v = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
    while (y0 <= y1) { rowptr(y0)[x] = v; y0++; }
}

void fill_rect(Rect r, Color c) {
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1, y;
    u32 v;
    if (c.a < 255) { fill_rect_alpha(r, c); return; }
    if (x0 < clip.x) x0 = clip.x;
    if (y0 < clip.y) y0 = clip.y;
    if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
    if (y1 >= clip.y + clip.h) y1 = clip.y + clip.h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)fb.width) x1 = (int)fb.width - 1;
    if (y1 >= (int)fb.height) y1 = (int)fb.height - 1;
    if (x0 > x1 || y0 > y1) return;
    v = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
    for (y = y0; y <= y1; y++) {
        u32 *p = rowptr(y) + x0;
        int n = x1 - x0 + 1;
        while (n--) *p++ = v;
    }
}

void fill_rect_alpha(Rect r, Color c) {
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1, x, y;
    if (c.a == 0) return;
    if (c.a == 255) { fill_rect(r, c); return; }
    if (x0 < clip.x) x0 = clip.x;
    if (y0 < clip.y) y0 = clip.y;
    if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
    if (y1 >= clip.y + clip.h) y1 = clip.y + clip.h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)fb.width) x1 = (int)fb.width - 1;
    if (y1 >= (int)fb.height) y1 = (int)fb.height - 1;
    for (y = y0; y <= y1; y++) {
        u32 *p = rowptr(y);
        u32 a = c.a, ia = 255U - c.a;
        for (x = x0; x <= x1; x++) {
            u32 d = p[x];
            u32 rr = ((u32)c.r * a + ((d >> 16) & 0xFFU) * ia + 127U) / 255U;
            u32 gg = ((u32)c.g * a + ((d >> 8) & 0xFFU) * ia + 127U) / 255U;
            u32 bb = ((u32)c.b * a + (d & 0xFFU) * ia + 127U) / 255U;
            p[x] = (rr << 16) | (gg << 8) | bb;
        }
    }
}

void draw_rect(Rect r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    if (c.a < 255) {
        fill_rect_alpha((Rect){r.x, r.y, r.w, 1}, c);
        fill_rect_alpha((Rect){r.x, r.y + r.h - 1, r.w, 1}, c);
        fill_rect_alpha((Rect){r.x, r.y + 1, 1, r.h - 2}, c);
        fill_rect_alpha((Rect){r.x + r.w - 1, r.y + 1, 1, r.h - 2}, c);
        return;
    }
    hline(r.x, r.x + r.w - 1, r.y, c);
    hline(r.x, r.x + r.w - 1, r.y + r.h - 1, c);
    vline(r.x, r.y, r.y + r.h - 1, c);
    vline(r.x + r.w - 1, r.y, r.y + r.h - 1, c);
}

static void round_scanline(Rect r, int rad, Color c, int alpha_mode) {
    int y;
    if (r.w <= 0 || r.h <= 0) return;
    if (rad < 1) { if (alpha_mode) fill_rect_alpha(r, c); else fill_rect(r, c); return; }
    if (rad * 2 > r.w) rad = r.w / 2;
    if (rad * 2 > r.h) rad = r.h / 2;
    for (y = 0; y < r.h; y++) {
        int inset = 0;
        if (y < rad) {
            int dy = rad - 1 - y;
            while (inset < rad && (rad - inset - 1) * (rad - inset - 1) + dy * dy > rad * rad)
                inset++;
        } else if (y >= r.h - rad) {
            int dy = y - (r.h - rad);
            while (inset < rad && (rad - inset - 1) * (rad - inset - 1) + dy * dy > rad * rad)
                inset++;
        }
        if (alpha_mode)
            fill_rect_alpha((Rect){r.x + inset, r.y + y, r.w - inset * 2, 1}, c);
        else
            hline(r.x + inset, r.x + r.w - 1 - inset, r.y + y, c);
    }
}

void fill_round_rect(Rect r, int rad, Color c) { round_scanline(r, rad, c, c.a < 255); }
void fill_round_rect_alpha(Rect r, int rad, Color c) { round_scanline(r, rad, c, 1); }

void draw_round_rect(Rect r, int rad, Color c) {
    int y, prev_in = -1;
    if (r.w <= 0 || r.h <= 0) return;
    if (rad < 1) { draw_rect(r, c); return; }
    if (rad * 2 > r.w) rad = r.w / 2;
    if (rad * 2 > r.h) rad = r.h / 2;
    for (y = 0; y < r.h; y++) {
        int inset = 0;
        if (y < rad) {
            int dy = rad - 1 - y;
            while (inset < rad && (rad - inset - 1) * (rad - inset - 1) + dy * dy > rad * rad)
                inset++;
        } else if (y >= r.h - rad) {
            int dy = y - (r.h - rad);
            while (inset < rad && (rad - inset - 1) * (rad - inset - 1) + dy * dy > rad * rad)
                inset++;
        }
        if (y == 0 || y == r.h - 1) {
            fill_rect_alpha((Rect){r.x + inset, r.y + y, r.w - inset * 2, 1}, c);
        } else {
            blend_px(r.x + inset, r.y + y, c, c.a);
            blend_px(r.x + r.w - 1 - inset, r.y + y, c, c.a);
            if (prev_in >= 0 && prev_in != inset) {
                int lo = prev_in < inset ? prev_in : inset;
                int hi = prev_in > inset ? prev_in : inset;
                int k;
                for (k = lo; k <= hi; k++) {
                    blend_px(r.x + k, r.y + y, c, (u8)(c.a / 2));
                    blend_px(r.x + r.w - 1 - k, r.y + y, c, (u8)(c.a / 2));
                }
            }
        }
        prev_in = inset;
    }
}

void fill_grad_v(Rect r, Color top, Color bottom) {
    int y;
    if (r.w <= 0 || r.h <= 0) return;
    for (y = 0; y < r.h; y++) {
        Color c;
        c.r = (u8)(top.r + ((int)bottom.r - (int)top.r) * y / (r.h > 1 ? r.h - 1 : 1));
        c.g = (u8)(top.g + ((int)bottom.g - (int)top.g) * y / (r.h > 1 ? r.h - 1 : 1));
        c.b = (u8)(top.b + ((int)bottom.b - (int)top.b) * y / (r.h > 1 ? r.h - 1 : 1));
        c.a = 255;
        hline(r.x, r.x + r.w - 1, r.y + y, c);
    }
}

void fill_grad_h(Rect r, Color left, Color right) {
    int x;
    if (r.w <= 0 || r.h <= 0) return;
    for (x = 0; x < r.w; x++) {
        Color c;
        c.r = (u8)(left.r + ((int)right.r - (int)left.r) * x / (r.w > 1 ? r.w - 1 : 1));
        c.g = (u8)(left.g + ((int)right.g - (int)left.g) * x / (r.w > 1 ? r.w - 1 : 1));
        c.b = (u8)(left.b + ((int)right.b - (int)left.b) * x / (r.w > 1 ? r.w - 1 : 1));
        c.a = 255;
        vline(r.x + x, r.y, r.y + r.h - 1, c);
    }
}

void fill_circle(int cx, int cy, int rad, Color c) {
    int dy, dx;
    for (dy = -rad; dy <= rad; dy++) {
        for (dx = -rad; dx <= rad; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= rad * rad)
                blend_px(cx + dx, cy + dy, c, c.a);
        }
    }
}

void fill_ellipse(int cx, int cy, int rx, int ry, Color c) {
    int dy, dx;
    if (rx <= 0 || ry <= 0) return;
    for (dy = -ry; dy <= ry; dy++) {
        for (dx = -rx; dx <= rx; dx++) {
            if ((i64)dx * dx * ry * ry + (i64)dy * dy * rx * rx <= (i64)rx * rx * ry * ry)
                blend_px(cx + dx, cy + dy, c, c.a);
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, Color c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blend_px(x0, y0, c, c.a);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

void fill_poly(const int *xs, const int *ys, int n, Color c) {
    int miny = 99999, maxy = -99999, i, y;
    for (i = 0; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    for (y = miny; y <= maxy; y++) {
        int nodes[32], nn = 0, j = n - 1;
        for (i = 0; i < n && nn < 32; i++) {
            if ((ys[i] < y && ys[j] >= y) || (ys[j] < y && ys[i] >= y)) {
                nodes[nn++] = xs[i] + (y - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
            }
            j = i;
        }
        for (i = 0; i < nn - 1; i++)
            for (j = i + 1; j < nn; j++)
                if (nodes[i] > nodes[j]) { int t = nodes[i]; nodes[i] = nodes[j]; nodes[j] = t; }
        for (i = 0; i + 1 < nn; i += 2)
            fill_rect_alpha((Rect){nodes[i], y, nodes[i + 1] - nodes[i] + 1, 1}, c);
    }
}

void draw_shadow(Rect r, int size) {
    int i;
    for (i = 1; i <= size; i++) {
        u8 a = (u8)((size + 1 - i) * 68 / (size + 2));
        Rect s = {r.x - i + 1, r.y + 3 - i / 2, r.w + i * 2 - 2, r.h + i * 2};
        draw_round_rect(s, 8 + i / 2, RGBA(0, 0, 0, a));
    }
}

void blit_copy(Rect dst, const u8 *src, int srcw) {
    int y;
    for (y = 0; y < dst.h; y++) {
        int py = dst.y + y;
        if (py < 0 || py >= (int)fb.height) continue;
        memcpy(rowptr(py) + dst.x, src + (size_t)y * (size_t)srcw * 4U, (size_t)dst.w * 4U);
    }
}

void blit_scaled(Rect dst, const u8 *src, int sw, int sh) {
    int y, x;
    if (dst.w <= 0 || dst.h <= 0 || sw <= 0 || sh <= 0) return;
    for (y = 0; y < dst.h; y++) {
        int sy = y * sh / dst.h;
        for (x = 0; x < dst.w; x++) {
            int sx = x * sw / dst.w;
            const u8 *p = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 4U;
            px(dst.x + x, dst.y + y, RGB(p[2], p[1], p[0]));
        }
    }
}
