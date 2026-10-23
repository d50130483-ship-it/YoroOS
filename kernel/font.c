/* YoroOS - UTF-8 anti-aliased text rendering */
#include "kernel.h"

const Glyph *font_lookup(u16 cp) {
    int lo = 0, hi = font_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_glyphs[mid].cp == cp) return &font_glyphs[mid];
        if (font_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int text_width(const char *s) { return text_width_n(s, 1 << 30); }

int text_width_n(const char *s, int n) {
    int w = 0;
    while (*s && n-- > 0) {
        u16 cp;
        const Glyph *g;
        utf8_next(&s, &cp);
        g = font_lookup(cp);
        w += g ? g->adv : 6;
    }
    return w;
}

static void draw_glyph(int x, int y, const Glyph *g, Color c, int bold) {
    int yy, xx;
    const u8 *pxs = font_pixels + g->off;
    for (yy = 0; yy < font_height; yy++) {
        for (xx = 0; xx < g->w; xx++) {
            u8 a = pxs[yy * g->w + xx];
            if (!a) continue;
            blend_px(x + xx, y + yy, c, a);
            if (bold) blend_px(x + xx + 1, y + yy, c, (u8)(a / 2));
        }
    }
}

static Rect rect_intersect(Rect a, Rect b) {
    Rect r;
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    r.x = x0; r.y = y0;
    r.w = x1 - x0; if (r.w < 0) r.w = 0;
    r.h = y1 - y0; if (r.h < 0) r.h = 0;
    return r;
}

static void draw_string(int x, int y, const char *s, Color c, int bold, Rect clipr, int has_clip) {
    Rect save = gfx_get_clip();
    if (has_clip) gfx_set_clip(rect_intersect(save, clipr));
    while (*s) {
        u16 cp;
        const Glyph *g;
        utf8_next(&s, &cp);
        g = font_lookup(cp);
        if (!g) { x += 6; continue; }
        if (has_clip && x > clipr.x + clipr.w) break;
        if (cp != ' ') draw_glyph(x, y, g, c, bold);
        x += g->adv;
    }
    if (has_clip) gfx_set_clip(save);
}

void draw_text(int x, int y, const char *s, Color c) {
    draw_string(x, y, s, c, 0, (Rect){0, 0, 0, 0}, 0);
}

void draw_text_bold(int x, int y, const char *s, Color c) {
    draw_string(x, y, s, c, 1, (Rect){0, 0, 0, 0}, 0);
}

void draw_text_clip(int x, int y, const char *s, Color c, Rect clip) {
    draw_string(x, y, s, c, 0, clip, 1);
}

void draw_text_center(int cx, int y, const char *s, Color c) {
    draw_text(cx - text_width(s) / 2, y, s, c);
}

void draw_text_right(int rx, int y, const char *s, Color c) {
    draw_text(rx - text_width(s), y, s, c);
}

void draw_text_wrap(Rect box, const char *s, Color c, int lineh) {
    char line[256];
    int n = 0, y = box.y;
    const char *p = s;
    while (*p && y + FONT_H < box.y + box.h) {
        const char *word = p;
        int wlen = 0;
        while (*word == ' ') { word++; }
        while (word[wlen] && word[wlen] != ' ') wlen++;
        if (!wlen) break;
        if (n > 0 && text_width_n(line, n) + 4 + text_width_n(word, wlen) > box.w) {
            line[n] = 0;
            draw_text(box.x, y, line, c);
            y += lineh;
            n = 0;
        }
        if (n + wlen + 1 < (int)sizeof(line)) {
            if (n) line[n++] = ' ';
            memcpy(line + n, word, (size_t)wlen);
            n += wlen;
        }
        p = word + wlen;
    }
    if (n) {
        line[n] = 0;
        draw_text(box.x, y, line, c);
    }
}
