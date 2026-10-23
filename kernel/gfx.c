/* YoroOS - software graphics: framebuffer, shapes, alpha, scaling */
#include "kernel.h"

FBInfo fb;
static Rect clip;

static inline u32 *rowptr(int y) { return (u32 *)(fb.back + (u32)y * fb.pitch); }

void gfx_set_clip(Rect r) { clip = r; }
Rect gfx_get_clip(void) { return clip; }

void gfx_clear(Color c) {
    Rect r = {0, 0, (int)fb.width, (int)fb.height};
    gfx_set_clip(r);
    fill_rect(r, c);
}

void gfx_present(void) {
    u32 pixels = fb.width * fb.height;
    if (fb.bpp == 32) {
        u32 y;
        for (y = 0; y < fb.height; y++)
            memcpy(fb.vram + y * fb.pitch, fb.back + y * fb.pitch, fb.width * 4);
    } else {
        u32 x, y;
        u32 rp = fb.red_pos, gp = fb.green_pos, bp = fb.blue_pos;
        for (y = 0; y < fb.height; y++) {
            u32 *src = rowptr((int)y);
            u8 *dst = fb.vram + y * fb.pitch;
            for (x = 0; x < fb.width; x++) {
                u32 v = src[x];
                u32 r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                u32 o = (r >> (8 - fb.red_size)) << rp |
                        (g >> (8 - fb.green_size)) << gp |
                        (b >> (8 - fb.blue_size)) << bp;
                if (fb.bpp == 32) { *(u32 *)(dst + x * 4) = o; }
                else if (fb.bpp == 24) { dst[x * 3] = o & 0xFF; dst[x * 3 + 1] = (o >> 8) & 0xFF; dst[x * 3 + 2] = (o >> 16) & 0xFF; }
                else if (fb.bpp == 16) { *(u16 *)(dst + x * 2) = (u16)o; }
            }
        }
    }
    UNUSED(pixels);
}

void gfx_present_rect(Rect r) { gfx_present(); }

static inline int in_clip(int x, int y) {
    return x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h;
}

void px(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) return;
    if (!in_clip(x, y)) return;
    rowptr(y)[x] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
}

void blend_px(int x, int y, Color c, u8 a) {
    u32 *p;
    u32 d;
    int r, g, b;
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) return;
    if (!in_clip(x, y)) return;
    if (a == 0) return;
    p = &rowptr(y)[x];
    if (a == 255) { *p = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b; return; }
    d = *p;
    r = (int)((c.r * a + ((d >> 16) & 0xFF) * (255 - a)) / 255);
    g = (int)((c.g * a + ((d >> 8) & 0xFF) * (255 - a)) / 255);
    b = (int)((c.b * a + (d & 0xFF) * (255 - a)) / 255);
    *p = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

void hline(int x0, int x1, int y, Color c) {
    if (y < 0 || y >= (int)fb.height) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < clip.x) x0 = clip.x;
    if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
    if (x1 < 0 || x0 >= (int)fb.width) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int)fb.width) x1 = (int)fb.width - 1;
    {
        u32 v = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
        u32 *p = rowptr(y);
        while (x0 <= x1) p[x0++] = v;
    }
}

void vline(int x, int y0, int y1, Color c) {
    int yy;
    if (x < 0 || x >= (int)fb.width) return;
    if (x < clip.x || x >= clip.x + clip.w) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < clip.y) y0 = clip.y;
    if (y1 >= clip.y + clip.h) y1 = clip.y + clip.h - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= (int)fb.height) y1 = (int)fb.height - 1;
    for (yy = y0; yy <= y1; yy++) rowptr(yy)[x] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
}

void fill_rect(Rect r, Color c) {
    int y;
    int x0 = r.x, x1 = r.x + r.w - 1;
    if (r.w <= 0 || r.h <= 0) return;
    if (x0 < clip.x) x0 = clip.x;
    if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
    if (x0 > x1) return;
    for (y = r.y; y < r.y + r.h; y++) {
        int yc = y;
        if (yc < clip.y || yc >= clip.y + clip.h || yc < 0 || yc >= (int)fb.height) continue;
        {
            u32 v = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
            u32 *p = rowptr(yc);
            int x;
            for (x = x0; x <= x1; x++) p[x] = v;
        }
    }
}

void fill_rect_alpha(Rect r, Color c) {
    int y, x;
    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++)
            blend_px(x, y, c, c.a);
}

void draw_rect(Rect r, Color c) {
    hline(r.x, r.x + r.w - 1, r.y, c);
    hline(r.x, r.x + r.w - 1, r.y + r.h - 1, c);
    vline(r.x, r.y, r.y + r.h - 1, c);
    vline(r.x + r.w - 1, r.y, r.y + r.h - 1, c);
}

void fill_grad_v(Rect r, Color top, Color bottom) {
    int y;
    for (y = 0; y < r.h; y++) {
        Color c;
        int t = r.h > 1 ? (y * 255) / (r.h - 1) : 0;
        c.r = (u8)((top.r * (255 - t) + bottom.r * t) / 255);
        c.g = (u8)((top.g * (255 - t) + bottom.g * t) / 255);
        c.b = (u8)((top.b * (255 - t) + bottom.b * t) / 255);
        c.a = 255;
        hline(r.x, r.x + r.w - 1, r.y + y, c);
    }
}

void fill_grad_h(Rect r, Color left, Color right) {
    int x;
    for (x = 0; x < r.w; x++) {
        Color c;
        int t = r.w > 1 ? (x * 255) / (r.w - 1) : 0;
        c.r = (u8)((left.r * (255 - t) + right.r * t) / 255);
        c.g = (u8)((left.g * (255 - t) + right.g * t) / 255);
        c.b = (u8)((left.b * (255 - t) + right.b * t) / 255);
        c.a = 255;
        vline(r.x + x, r.y, r.y + r.h - 1, c);
    }
}

/* filled ellipse using per-scanline intersection with the ellipse equation */
void fill_ellipse(int cx, int cy, int rx, int ry, Color c) {
    int y;
    if (rx <= 0 || ry <= 0) return;
    for (y = -ry; y <= ry; y++) {
        int lo = 0, hi = rx;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if ((long)mid * mid * ry * ry + (long)y * y * rx * rx <= (long)rx * rx * ry * ry) lo = mid;
            else hi = mid - 1;
        }
        hline(cx - lo, cx + lo, cy + y, c);
    }
}

void fill_circle(int cx, int cy, int rad, Color c) {
    int y;
    for (y = -rad; y <= rad; y++) {
        int lo = 0, hi = rad;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if ((long)mid * mid + (long)y * y <= (long)rad * rad) lo = mid;
            else hi = mid - 1;
        }
        hline(cx - lo, cx + lo, cy + y, c);
    }
}

void fill_round_rect(Rect r, int rad, Color c) {
    int y;
    if (rad < 0) rad = 0;
    if (rad > r.h / 2) rad = r.h / 2;
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad == 0) { fill_rect(r, c); return; }
    /* middle band */
    fill_rect((Rect){r.x, r.y + rad, r.w, r.h - 2 * rad}, c);
    for (y = 0; y < rad; y++) {
        int dy = rad - y;
        int lo = 0, hi = rad;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if ((long)mid * mid + (long)dy * dy <= (long)rad * rad) lo = mid;
            else hi = mid - 1;
        }
        {
            int xoff = rad - lo;
            hline(r.x + xoff, r.x + r.w - 1 - xoff, r.y + y, c);
            hline(r.x + xoff, r.x + r.w - 1 - xoff, r.y + r.h - 1 - y, c);
        }
    }
}

void fill_round_rect_alpha(Rect r, int rad, Color c) {
    int y;
    if (rad < 0) rad = 0;
    if (rad > r.h / 2) rad = r.h / 2;
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad == 0) { fill_rect_alpha(r, c); return; }
    fill_rect_alpha((Rect){r.x, r.y + rad, r.w, r.h - 2 * rad}, c);
    for (y = 0; y < rad; y++) {
        int dy = rad - y;
        int lo = 0, hi = rad;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if ((long)mid * mid + (long)dy * dy <= (long)rad * rad) lo = mid;
            else hi = mid - 1;
        }
        {
            int xoff = rad - lo;
            fill_rect_alpha((Rect){r.x + xoff, r.y + y, r.w - 2 * xoff, 1}, c);
            fill_rect_alpha((Rect){r.x + xoff, r.y + r.h - 1 - y, r.w - 2 * xoff, 1}, c);
        }
    }
}

void draw_round_rect(Rect r, int rad, Color c) {
    /* outline: top/bottom arcs */
    int y;
    if (rad == 0) { draw_rect(r, c); return; }
    hline(r.x + rad, r.x + r.w - 1 - rad, r.y, c);
    hline(r.x + rad, r.x + r.w - 1 - rad, r.y + r.h - 1, c);
    vline(r.x, r.y + rad, r.y + r.h - 1 - rad, c);
    vline(r.x + r.w - 1, r.y + rad, r.y + r.h - 1 - rad, c);
    for (y = 0; y < rad; y++) {
        int dy = rad - y;
        int lo = 0, hi = rad;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if ((long)mid * mid + (long)dy * dy <= (long)rad * rad) lo = mid;
            else hi = mid - 1;
        }
        {
            int xo = rad - lo;
            px(r.x + xo, r.y + y, c);
            px(r.x + r.w - 1 - xo, r.y + y, c);
            px(r.x + xo, r.y + r.h - 1 - y, c);
            px(r.x + r.w - 1 - xo, r.y + r.h - 1 - y, c);
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, Color c) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx - dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void fill_poly(const int *xs, const int *ys, int n, Color c) {
    int i, j, y;
    int miny = ys[0], maxy = ys[0];
    for (i = 1; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    for (y = miny; y <= maxy; y++) {
        int nodes[32], cnt = 0;
        for (i = 0, j = n - 1; i < n; j = i++) {
            if ((ys[i] < y && ys[j] >= y) || (ys[j] < y && ys[i] >= y)) {
                int x = xs[i] + (int)((long)(y - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]));
                if (cnt < 32) nodes[cnt++] = x;
            }
        }
        for (i = 0; i < cnt; i++)
            for (j = i + 1; j < cnt; j++)
                if (nodes[j] < nodes[i]) { int t = nodes[i]; nodes[i] = nodes[j]; nodes[j] = t; }
        for (i = 0; i + 1 < cnt; i += 2)
            hline(nodes[i], nodes[i + 1], y, c);
    }
}

void draw_shadow(Rect r, int size) {
    int i;
    for (i = size; i > 0; i--) {
        Color c = {0, 0, 0, (u8)(10 + (size - i) * 6)};
        fill_rect_alpha((Rect){r.x - i, r.y - i + 2, r.w + 2 * i, r.h + 2 * i}, c);
    }
}

void blit_copy(Rect dst, const u8 *src, int srcw) {
    int y, x;
    for (y = 0; y < dst.h; y++) {
        const u32 *s = (const u32 *)(src + (u32)y * srcw * 4);
        for (x = 0; x < dst.w; x++) {
            int dx = dst.x + x, dy = dst.y + y;
            if (dx < 0 || dy < 0 || dx >= (int)fb.width || dy >= (int)fb.height) continue;
            if (!in_clip(dx, dy)) continue;
            {
                u32 v = s[x];
                rowptr(dy)[dx] = v;
            }
        }
    }
}

void blit_scaled(Rect dst, const u8 *src, int sw, int sh) {
    int x, y;
    if (sw <= 0 || sh <= 0) return;
    for (y = 0; y < dst.h; y++) {
        int sy = (int)((long)y * sh / dst.h);
        const u32 *s = (const u32 *)(src + (u32)sy * sw * 4);
        for (x = 0; x < dst.w; x++) {
            int sx = (int)((long)x * sw / dst.w);
            int dx = dst.x + x, dy = dst.y + y;
            if (dx < 0 || dy < 0 || dx >= (int)fb.width || dy >= (int)fb.height) continue;
            if (!in_clip(dx, dy)) continue;
            rowptr(dy)[dx] = s[sx];
        }
    }
}
