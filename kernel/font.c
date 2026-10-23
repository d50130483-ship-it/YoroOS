/* YoroOS 0.2 - Multi-face anti-aliased UTF-8 font renderer */
#include "kernel.h"

const Glyph *font_lookup_face(int face, u16 cp) {
    int lo, hi;
    if (face < 0 || face > 4) face = FACE_UI;
    lo = font_faces[face].start;
    hi = lo + font_faces[face].count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (font_glyphs[mid].cp == cp) return &font_glyphs[mid];
        if (font_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    if (face != FACE_UI) return font_lookup_face(FACE_UI, cp);
    if (cp != '?') return font_lookup_face(FACE_UI, '?');
    return NULL;
}

const Glyph *font_lookup(u16 cp) {
    return font_lookup_face(FACE_UI, cp);
}

int text_width_face(int face, const char *s) {
    int w = 0, adv;
    if (!s) return 0;
    while (*s) {
        int cp = str_utf8_decode(s, &adv);
        const Glyph *g;
        if (adv <= 0) break;
        s += adv;
        if (cp == '\r' || cp == '\n') break;
        g = font_lookup_face(face, (u16)cp);
        w += g ? g->adv : 7;
    }
    return w;
}

int text_width(const char *s) { return text_width_face(FACE_UI, s); }

int text_width_n(const char *s, int n) {
    int w = 0, pos = 0, adv;
    if (!s) return 0;
    while (pos < n && s[pos]) {
        int cp = str_utf8_decode(s + pos, &adv);
        const Glyph *g;
        if (adv <= 0) break;
        pos += adv;
        g = font_lookup_face(FACE_UI, (u16)cp);
        w += g ? g->adv : 7;
    }
    return w;
}

void draw_text_face(int face, int x, int y, const char *s, Color c) {
    int cx = x, adv;
    if (!s || c.a == 0) return;
    while (*s) {
        int cp = str_utf8_decode(s, &adv);
        const Glyph *g;
        if (adv <= 0) break;
        s += adv;
        if (cp == '\n') { cx = x; y += font_faces[face].line_h; continue; }
        if (cp == '\r') continue;
        g = font_lookup_face(face, (u16)cp);
        if (g) {
            if (g->w > 0 && g->h > 0) {
                const u8 *src = font_pixels + g->off;
                int row, col;
                for (row = 0; row < (int)g->h; row++) {
                    int py = y + g->yoff + row;
                    for (col = 0; col < (int)g->w; col++) {
                        u8 a = src[row * g->w + col];
                        if (a) {
                            u8 eff = (c.a == 255) ? a : (u8)(((u32)a * c.a) / 255U);
                            blend_px(cx + g->xoff + col, py, c, eff);
                        }
                    }
                }
            }
            cx += g->adv;
        } else {
            cx += 7;
        }
    }
}

void draw_text_face_clip(int face, int x, int y, const char *s, Color c, Rect clip) {
    Rect prev = gfx_get_clip();
    int x0 = clip.x > prev.x ? clip.x : prev.x;
    int y0 = clip.y > prev.y ? clip.y : prev.y;
    int x1 = (clip.x + clip.w) < (prev.x + prev.w) ? (clip.x + clip.w) : (prev.x + prev.w);
    int y1 = (clip.y + clip.h) < (prev.y + prev.h) ? (clip.y + clip.h) : (prev.y + prev.h);
    if (x1 > x0 && y1 > y0) {
        gfx_set_clip((Rect){x0, y0, x1 - x0, y1 - y0});
        draw_text_face(face, x, y, s, c);
        gfx_set_clip(prev);
    }
}

void draw_text(int x, int y, const char *s, Color c)       { draw_text_face(FACE_UI, x, y, s, c); }
void draw_text_small(int x, int y, const char *s, Color c) { draw_text_face(FACE_SMALL, x, y, s, c); }
void draw_text_title(int x, int y, const char *s, Color c) { draw_text_face(FACE_TITLE, x, y, s, c); }
void draw_text_hero(int x, int y, const char *s, Color c)  { draw_text_face(FACE_HERO, x, y, s, c); }
void draw_text_mono(int x, int y, const char *s, Color c)  { draw_text_face(FACE_MONO, x, y, s, c); }

void draw_text_bold(int x, int y, const char *s, Color c) {
    draw_text_face(FACE_UI, x, y, s, c);
    draw_text_face(FACE_UI, x + 1, y, s, RGBA(c.r, c.g, c.b, (u8)(c.a * 2 / 3)));
}

void draw_text_center_face(int face, int cx, int y, const char *s, Color c) {
    draw_text_face(face, cx - text_width_face(face, s) / 2, y, s, c);
}

void draw_text_center(int cx, int y, const char *s, Color c) {
    draw_text_center_face(FACE_UI, cx, y, s, c);
}

void draw_text_right(int rx, int y, const char *s, Color c) {
    draw_text(rx - text_width(s), y, s, c);
}

void draw_text_clip(int x, int y, const char *s, Color c, Rect clip) {
    draw_text_face_clip(FACE_UI, x, y, s, c, clip);
}

void draw_text_wrap(Rect box, const char *s, Color c, int lineh) {
    int y = box.y;
    const char *p = s;
    if (!s || box.w <= 0) return;
    while (*p && y + FONT_H <= box.y + box.h) {
        char line[256];
        int len = 0;
        if (*p == '\r') { p++; continue; }
        if (*p == '\n') { p++; y += lineh; continue; }
        while (*p && *p != '\n' && *p != '\r') {
            const char *ws = p;
            char word[128];
            int wl = 0;
            while (*ws && *ws != ' ' && *ws != '\n' && *ws != '\r' && wl < 120) word[wl++] = *ws++;
            if (*ws == ' ' && wl < 120) word[wl++] = *ws++;
            word[wl] = 0;
            if (len + wl >= (int)sizeof(line) - 1) break;
            memcpy(line + len, word, (size_t)wl + 1);
            if (len > 0 && text_width(line) > box.w) {
                line[len] = 0;
                break;
            }
            len += wl;
            p = ws;
        }
        line[len] = 0;
        draw_text(box.x, y, line, c);
        y += lineh;
    }
}
