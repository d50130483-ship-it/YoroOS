/* YoroOS - themes & wallpapers */
#include "kernel.h"

static Theme themes[] = {
    { "Синий горизонт", RGB(10, 32, 64), RGB(2, 12, 28), RGB(0, 120, 215), RGB(0, 84, 158), 0 },
    { "Закат",          RGB(36, 18, 62), RGB(94, 32, 58), RGB(255, 110, 90), RGB(196, 72, 60), 1 },
    { "Изумруд",        RGB(6, 40, 38),  RGB(3, 18, 22), RGB(24, 170, 140), RGB(16, 128, 104), 2 },
    { "Графит",         RGB(28, 30, 34), RGB(14, 15, 18), RGB(96, 160, 255), RGB(60, 110, 200), 3 },
    { "Аврора",         RGB(20, 16, 48), RGB(4, 24, 44), RGB(150, 110, 255), RGB(96, 70, 200), 4 },
    { "Янтарь",         RGB(52, 32, 6),  RGB(24, 12, 2), RGB(255, 160, 40), RGB(200, 116, 20), 5 }
};

static int theme_index = 0;
Theme *theme = &themes[0];

/* wallpaper cache */
static u8 *wall_cache = NULL;
static int wall_w = 0, wall_h = 0, wall_id = -1;

static void glow_circle(int cx, int cy, int rad, Color c, int alpha_max) {
    int x, y;
    long r2 = (long)rad * rad;
    for (y = -rad; y <= rad; y++) {
        for (x = -rad; x <= rad; x++) {
            long d2 = (long)x * x + (long)y * y;
            int a;
            if (d2 > r2) continue;
            a = (int)((r2 - d2) * alpha_max / r2);
            blend_px(cx + x, cy + y, c, (u8)a);
        }
    }
}

static void vignette(int w, int h, int strength) {
    int x, y, cx = w / 2, cy = h / 2;
    int maxd2 = cx * cx + cy * cy;
    for (y = 0; y < h; y += 2) {
        for (x = 0; x < w; x += 2) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            int a = d2 * strength / maxd2;
            if (a > 0) {
                Color c = {0, 0, 0, (u8)(a > 255 ? 255 : a)};
                fill_rect_alpha((Rect){x, y, 2, 2}, c);
            }
        }
    }
}

static void render_wallpaper(int w, int h) {
    Rect full = {0, 0, w, h};
    Theme *t = theme;
    gfx_set_clip(full);
    fill_grad_v(full, t->wall_top, t->wall_bottom);
    switch (t->kind) {
    case 0: {  /* blue horizon: big soft blooms */
        glow_circle(w * 23 / 100, h * 78 / 100, h * 62 / 100, RGB(46, 132, 226), 52);
        glow_circle(w * 78 / 100, h * 22 / 100, h * 48 / 100, RGB(70, 160, 255), 34);
        glow_circle(w * 50 / 100, h * 108 / 100, h * 46 / 100, RGB(20, 80, 170), 60);
        break;
    }
    case 1: { /* sunset */
        int cx = w / 2, cy = h * 2 / 3;
        glow_circle(cx, cy, h * 38 / 100, RGB(255, 150, 90), 62);
        glow_circle(w / 5, h / 4, h / 2, RGB(120, 40, 90), 45);
        fill_circle(cx, cy, h / 10, RGBA(255, 214, 150, 210));
        fill_rect_alpha((Rect){0, cy + h / 8, w, h / 2}, RGBA(30, 10, 30, 120));
        break;
    }
    case 2: { /* emerald */
        int i;
        for (i = 0; i < 5; i++) {
            int x = w / 8 + i * (w / 6);
            fill_rect_alpha((Rect){x, 0, w / 40, h}, RGBA(40, 220, 180, 8));
        }
        glow_circle(w / 2, h * 45 / 100, h * 55 / 100, RGB(28, 196, 156), 40);
        glow_circle(w * 85 / 100, h * 85 / 100, h / 3, RGB(10, 120, 110), 50);
        break;
    }
    case 3: { /* graphite grid */
        int x, y;
        for (x = 0; x < w; x += 56) fill_rect_alpha((Rect){x, 0, 1, h}, RGBA(255, 255, 255, 10));
        for (y = 0; y < h; y += 56) fill_rect_alpha((Rect){0, y, w, 1}, RGBA(255, 255, 255, 10));
        glow_circle(w / 2, h * 35 / 100, h * 55 / 100, RGB(90, 150, 255), 34);
        break;
    }
    case 4: { /* aurora with stars */
        int i;
        glow_circle(w / 4, h / 4, h / 2, RGB(120, 90, 255), 50);
        glow_circle(w * 3 / 4, h / 2, h / 2, RGB(50, 200, 220), 40);
        glow_circle(w / 2, h, h / 2, RGB(180, 80, 220), 38);
        for (i = 0; i < 90; i++) {
            int sx = (i * 137) % w, sy = (i * 271) % (h * 2 / 3);
            int a = 60 + (i * 37) % 160;
            blend_px(sx, sy, RGB(255, 255, 255), (u8)a);
            if (i % 9 == 0) {
                blend_px(sx + 1, sy, RGB(255, 255, 255), 70);
                blend_px(sx, sy + 1, RGB(255, 255, 255), 70);
            }
        }
        break;
    }
    default: { /* amber brand */
        glow_circle(w * 30 / 100, h * 70 / 100, h * 60 / 100, RGB(255, 170, 60), 55);
        glow_circle(w * 80 / 100, h * 30 / 100, h / 3, RGB(200, 110, 40), 45);
        break;
    }
    }
    vignette(w, h, t->kind == 3 ? 60 : 90);
}

void desktop_set_wall_cache_dirty(void) { wall_id = -1; }

void theme_draw_wallpaper(void) {
    int w = (int)fb.width, h = (int)fb.height;
    u8 *old;
    if (!wall_cache || wall_w != w || wall_h != h) {
        if (wall_cache) kfree(wall_cache);
        wall_cache = (u8 *)kmalloc((size_t)fb.bstride * h);
        if (!wall_cache) return;
        wall_w = w; wall_h = h; wall_id = -1;
    }
    if (wall_id != theme_index) {
        u8 *save = fb.back;
        fb.back = wall_cache;
        render_wallpaper(w, h);
        fb.back = save;
        wall_id = theme_index;
    }
    /* fast blit of the cached wallpaper */
    {
        int y;
        Rect full = {0, 0, w, h};
        gfx_set_clip(full);
        for (y = 0; y < h; y++)
            memcpy(fb.back + (u32)y * fb.bstride, wall_cache + (u32)y * fb.bstride, (size_t)w * 4);
    }
    UNUSED(old);
}

void theme_init(void) {
    theme_index = 0;
    theme = &themes[0];
    wall_id = -1;
}

int theme_count(void) { return (int)(sizeof(themes) / sizeof(themes[0])); }
Theme *theme_get(int i) {
    if (i < 0 || i >= theme_count()) return &themes[0];
    return &themes[i];
}
void theme_set(int i) {
    if (i < 0 || i >= theme_count()) return;
    theme_index = i;
    theme = &themes[i];
    wall_id = -1;
    wm_invalidate();
}
int theme_current(void) { return theme_index; }
