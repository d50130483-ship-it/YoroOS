/* YoroOS 0.2 - Fluent UI Widget Toolkit */
#include "kernel.h"

int pt_in(int x, int y, Rect r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void ui_panel(Rect r, Color bg, int radius, int shadow) {
    if (shadow > 0) draw_shadow(r, shadow);
    fill_round_rect(r, radius, bg);
    draw_round_rect(r, radius, RGBA(130, 145, 170, theme->dark ? 48 : 85));
}

void ui_card(Rect r, int hover, int radius) {
    Color bg = theme->card_bg;
    if (hover) {
        bg = theme->dark
            ? RGB(bg.r + 12 > 255 ? 255 : bg.r + 12, bg.g + 12 > 255 ? 255 : bg.g + 12, bg.b + 14 > 255 ? 255 : bg.b + 14)
            : RGB(bg.r - 8, bg.g - 6, bg.b - 4);
    }
    fill_round_rect(r, radius, bg);
    draw_round_rect(r, radius, hover ? theme->accent : RGBA(130, 145, 170, theme->dark ? 52 : 90));
}

void ui_button(Rect r, const char *label, int hover, int primary) {
    Color bg, fg, bd;
    int ty = r.y + (r.h - font_line_height(FACE_UI)) / 2;
    if (primary) {
        bg = hover ? RGB(theme->accent.r + 22 > 255 ? 255 : theme->accent.r + 22,
                         theme->accent.g + 22 > 255 ? 255 : theme->accent.g + 22,
                         theme->accent.b + 22 > 255 ? 255 : theme->accent.b + 22)
                   : theme->accent;
        fg = C_WHITE;
        bd = RGBA(255, 255, 255, 48);
    } else {
        bg = hover ? RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 38) : theme->card_bg;
        fg = theme->text_main;
        bd = hover ? theme->accent : RGBA(130, 145, 170, 75);
    }
    fill_round_rect(r, 6, bg);
    draw_round_rect(r, 6, bd);
    draw_text_center(r.x + r.w / 2, ty, label, fg);
}

void ui_button_danger(Rect r, const char *label, int hover) {
    Color bg = hover ? RGB(236, 48, 60) : RGB(198, 34, 46);
    int ty = r.y + (r.h - font_line_height(FACE_UI)) / 2;
    fill_round_rect(r, 6, bg);
    draw_round_rect(r, 6, RGBA(255, 180, 180, 70));
    draw_text_center(r.x + r.w / 2, ty, label, C_WHITE);
}

void ui_icon_button(Rect r, u32 icon, int hover) {
    int isz = r.h - 10;
    if (isz > 20) isz = 20;
    if (hover) fill_round_rect_alpha(r, 5, RGBA(140, 160, 190, 46));
    icon_draw(icon, r.x + (r.w - isz) / 2, r.y + (r.h - isz) / 2, isz);
}

void ui_toggle(Rect r, int on) {
    Rect pill = {r.x, r.y + (r.h - 20) / 2, 40, 20};
    int kx = on ? pill.x + 28 : pill.x + 12;
    fill_round_rect(pill, 10, on ? theme->accent : RGBA(130, 140, 160, 110));
    fill_circle(kx, pill.y + 10, 7, C_WHITE);
}

void ui_checkbox(Rect r, int checked, const char *label, Color fg) {
    Rect box = {r.x, r.y + (r.h - 18) / 2, 18, 18};
    fill_round_rect(box, 4, checked ? theme->accent : theme->card_bg);
    draw_round_rect(box, 4, checked ? theme->accent : RGBA(130, 145, 170, 120));
    if (checked) {
        draw_line(box.x + 4, box.y + 9, box.x + 8, box.y + 13, C_WHITE);
        draw_line(box.x + 8, box.y + 13, box.x + 14, box.y + 5, C_WHITE);
    }
    if (label)
        draw_text(box.x + 26, r.y + (r.h - font_line_height(FACE_UI)) / 2, label, fg);
}

void ui_progress(Rect r, int pct, Color c) {
    int fw;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fill_round_rect(r, r.h / 2, RGBA(130, 145, 170, 50));
    fw = (r.w * pct) / 100;
    if (fw > 2)
        fill_round_rect((Rect){r.x, r.y, fw, r.h}, r.h / 2, c);
}

void ui_scrollbar(Rect track, int total, int view, int scroll) {
    int th, ty;
    if (total <= view || track.h < 16) return;
    fill_round_rect(track, track.w / 2, RGBA(120, 135, 160, 30));
    th = (track.h * view) / total;
    if (th < 24) th = 24;
    if (th > track.h) th = track.h;
    ty = track.y + ((track.h - th) * scroll) / (total - view > 0 ? total - view : 1);
    if (ty + th > track.y + track.h) ty = track.y + track.h - th;
    fill_round_rect((Rect){track.x + 1, ty, track.w - 2, th}, (track.w - 2) / 2, RGBA(150, 166, 192, 130));
}

void ui_caret(int x, int y, int h, Color c) {
    if (((sys_ticks() / 45U) & 1U) == 0)
        fill_rect((Rect){x, y, 2, h}, c);
}

void ui_textfield(Rect r, const char *text, int caret, int focused, const char *placeholder) {
    int ty = r.y + (r.h - font_line_height(FACE_UI)) / 2;
    Color bg = theme->dark ? RGB(19, 22, 30) : C_WHITE;
    fill_round_rect(r, 6, bg);
    draw_round_rect(r, 6, focused ? theme->accent : RGBA(130, 145, 170, 88));
    if (focused)
        fill_rect((Rect){r.x + 4, r.y + r.h - 2, r.w - 8, 2}, theme->accent);

    if (text && *text) {
        draw_text_clipped(r.x + 10, ty, r.w - 20, text, theme->text_main);
    } else if (placeholder && *placeholder) {
        draw_text_clipped(r.x + 10, ty, r.w - 20, placeholder, theme->text_muted);
    }

    if (focused && caret >= 0 && text) {
        char sub[256];
        int n = caret < (int)sizeof(sub) - 1 ? caret : (int)sizeof(sub) - 1;
        int cx;
        memcpy(sub, text, (size_t)n);
        sub[n] = 0;
        cx = r.x + 10 + text_width(sub);
        if (cx < r.x + r.w - 8)
            ui_caret(cx, ty + 1, font_line_height(FACE_UI) - 2, theme->accent);
    }
}

void ui_badge(Rect r, const char *txt, Color bg, Color fg) {
    fill_round_rect(r, r.h / 2, bg);
    draw_text_center_face(FACE_SMALL, r.x + r.w / 2, r.y + (r.h - font_line_height(FACE_SMALL)) / 2, txt, fg);
}

void draw_win_buttons(Rect frame, int hover_close, int hover_min, int hover_max) {
    int bw = 44, bh = 32;
    Rect rc = {frame.x + frame.w - bw,     frame.y + 1, bw - 1, bh};
    Rect rx = {frame.x + frame.w - bw * 2, frame.y + 1, bw,     bh};
    Rect rm = {frame.x + frame.w - bw * 3, frame.y + 1, bw,     bh};
    Color fg = theme->text_main;

    if (hover_min) fill_rect_alpha(rm, RGBA(140, 155, 180, 48));
    if (hover_max) fill_rect_alpha(rx, RGBA(140, 155, 180, 48));
    if (hover_close) { fill_round_rect(rc, 4, RGB(232, 17, 35)); }

    hline(rm.x + 16, rm.x + 27, rm.y + 16, fg);
    draw_rect((Rect){rx.x + 16, rx.y + 11, 11, 10}, fg);
    draw_line(rc.x + 17, rc.y + 11, rc.x + 26, rc.y + 20, hover_close ? C_WHITE : fg);
    draw_line(rc.x + 26, rc.y + 11, rc.x + 17, rc.y + 20, hover_close ? C_WHITE : fg);
}

int ui_textfield_edit(char *buf, int cap, int *len, int *caret, YoroEvent *e) {
    int l = *len, c = *caret;
    if (e->type != EV_KEY) return 0;
    if (c < 0) c = 0;
    if (c > l) c = l;

    if (e->key == KEY_LEFT) {
        while (c > 0 && ((u8)buf[--c] & 0xC0) == 0x80) {}
        *caret = c; return 1;
    }
    if (e->key == KEY_RIGHT) {
        if (c < l) { c++; while (c < l && ((u8)buf[c] & 0xC0) == 0x80) c++; }
        *caret = c; return 1;
    }
    if (e->key == KEY_HOME) { *caret = 0; return 1; }
    if (e->key == KEY_END)  { *caret = l; return 1; }
    if (e->key == KEY_BACKSP && c > 0) {
        int prev = c;
        while (prev > 0 && ((u8)buf[--prev] & 0xC0) == 0x80) {}
        memmove(buf + prev, buf + c, (size_t)(l - c + 1));
        *len = l - (c - prev);
        *caret = prev;
        return 1;
    }
    if (e->key == KEY_DEL && c < l) {
        int next = c + 1;
        while (next < l && ((u8)buf[next] & 0xC0) == 0x80) next++;
        memmove(buf + c, buf + next, (size_t)(l - next + 1));
        *len = l - (next - c);
        return 1;
    }
    if (e->ch >= 32) {
        char u8[5];
        int nb = utf8_encode((u16)e->ch, u8);
        if (nb > 0 && l + nb < cap) {
            memmove(buf + c + nb, buf + c, (size_t)(l - c + 1));
            memcpy(buf + c, u8, (size_t)nb);
            *len = l + nb;
            *caret = c + nb;
            return 1;
        }
    }
    return 0;
}

void ui_dialog_input(Rect box, const char *title, const char *value, int caret, int focus, Color accent, Color fg) {
    ui_panel(box, theme->win_bg, 10, 10);
    draw_text_bold(box.x + 18, box.y + 16, title, fg);
    ui_textfield((Rect){box.x + 18, box.y + 48, box.w - 36, 34}, value, caret, focus, "");
    ui_button((Rect){box.x + box.w - 118, box.y + box.h - 44, 100, 30}, "ОК", 0, 1);
    UNUSED(accent);
}

int ui_measure_wrapped(const char *s, int width) {
    int lines = 1, curw = 0;
    const char *p = s;
    if (!p || width <= 20) return font_line_height(FACE_UI);
    while (*p) {
        int adv = 0;
        int cp = str_utf8_decode(p, &adv);
        char chs[5];
        int w;
        if (adv <= 0) break;
        p += adv;
        if (cp == '\n') { lines++; curw = 0; continue; }
        w = utf8_encode((u16)cp, chs);
        chs[w] = 0;
        w = text_width(chs);
        if (curw + w > width) { lines++; curw = w; }
        else curw += w;
    }
    return lines * font_line_height(FACE_UI);
}
