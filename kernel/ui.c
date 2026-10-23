/* YoroOS - UI kit: panels, buttons, fields, toasts */
#include "kernel.h"

int pt_in(int x, int y, Rect r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void ui_panel(Rect r, Color bg, int radius, int shadow) {
    if (shadow > 0) draw_shadow(r, shadow);
    fill_round_rect(r, radius, bg);
}

void ui_button(Rect r, const char *label, int hover, int primary) {
    Color bg, fg;
    if (primary) {
        bg = hover ? theme->accent_dark : theme->accent;
        fg = C_WHITE;
        if (hover) {
            Color l = {0, 0, 0, 0};
            UNUSED(l);
        }
    } else {
        bg = hover ? RGB(232, 236, 243) : RGB(243, 245, 249);
        fg = RGB(30, 36, 48);
    }
    fill_round_rect(r, 6, bg);
    if (!primary) draw_round_rect(r, 6, RGB(206, 212, 222));
    draw_text_center(r.x + r.w / 2, r.y + (r.h - FONT_H) / 2, label, fg);
}

void ui_icon_button(Rect r, u32 icon, int hover) {
    if (hover) fill_round_rect(r, 5, RGB(232, 236, 243));
    icon_draw(icon, r.x + (r.w - 18) / 2, r.y + (r.h - 18) / 2, 18);
}

void ui_toggle(Rect r, int on) {
    Color bg = on ? theme->accent : RGB(190, 196, 206);
    int rad = r.h / 2;
    fill_round_rect(r, rad, bg);
    {
        int kx = on ? r.x + r.w - rad : r.x + rad;
        fill_circle(kx, r.y + rad, rad - 3, C_WHITE);
    }
}

void ui_checkbox(Rect r, int checked, const char *label, Color fg) {
    Rect box = {r.x, r.y + 2, 18, 18};
    if (checked) {
        fill_round_rect(box, 4, theme->accent);
        draw_line(box.x + 4, box.y + 9, box.x + 8, box.y + 13, C_WHITE);
        draw_line(box.x + 8, box.y + 13, box.x + 14, box.y + 5, C_WHITE);
    } else {
        fill_round_rect(box, 4, C_WHITE);
        draw_round_rect(box, 4, RGB(160, 168, 180));
    }
    if (label) draw_text(r.x + 26, r.y + (18 - FONT_H) / 2 + 2, label, fg);
}

void ui_progress(Rect r, int pct, Color c) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fill_round_rect(r, r.h / 2, RGB(226, 230, 238));
    if (pct > 0) fill_round_rect((Rect){r.x, r.y, r.w * pct / 100, r.h}, r.h / 2, c);
}

void ui_scrollbar(Rect track, int total, int view, int scroll) {
    int h, y;
    if (total <= view || view <= 0) return;
    fill_round_rect_alpha(track, track.w / 2, RGBA(0, 0, 0, 14));
    h = track.h * view / total;
    if (h < 24) h = 24;
    y = track.y + (track.h - h) * scroll / (total - view);
    if (y < track.y) y = track.y;
    fill_round_rect((Rect){track.x + 1, y, track.w - 2, h}, (track.w - 2) / 2, RGB(150, 158, 172));
}

void ui_caret(int x, int y, int h, Color c) {
    if ((sys_ticks() / 30) % 2 == 0) fill_rect((Rect){x, y, 1, h}, c);
}

void ui_textfield(Rect r, const char *text, int caret, int focused, const char *placeholder) {
    Color bg = focused ? C_WHITE : RGB(247, 248, 251);
    Rect saved_clip = gfx_get_clip();
    fill_round_rect(r, 5, bg);
    draw_round_rect(r, 5, focused ? theme->accent : RGB(210, 214, 222));
    {
        int tx = r.x + 9;
        Rect clip = {r.x + 4, r.y, r.w - 8, r.h};
        if (!text || !*text) {
            draw_text_clip(tx, r.y + (r.h - FONT_H) / 2, placeholder ? placeholder : "", RGB(150, 156, 168), clip);
            if (focused) ui_caret(tx, r.y + (r.h - FONT_H) / 2, FONT_H, RGB(60, 66, 80));
        } else {
            /* horizontal scrolling so the caret stays visible */
            int w = text_width(text);
            int adv = text_width_n(text, caret);
            int shift = 0;
            if (adv + 12 > r.w - 16) shift = adv + 12 - (r.w - 16);
            gfx_set_clip(clip);
            draw_text(tx - shift, r.y + (r.h - FONT_H) / 2, text, RGB(28, 32, 42));
            if (focused) ui_caret(tx - shift + adv, r.y + (r.h - FONT_H) / 2, FONT_H, RGB(60, 66, 80));
            gfx_set_clip(saved_clip);
            UNUSED(w);
        }
    }
}

void ui_card(Rect r, int hover, int radius) {
    if (hover) {
        draw_shadow(r, 6);
        fill_round_rect(r, radius, C_WHITE);
        draw_round_rect(r, radius, theme->accent);
    } else {
        fill_round_rect(r, radius, C_WHITE);
        draw_round_rect(r, radius, RGB(222, 226, 234));
    }
}

void ui_badge(Rect r, const char *txt, Color bg, Color fg) {
    if (bg.a < 255) fill_round_rect_alpha(r, r.h / 2, bg);
    else fill_round_rect(r, r.h / 2, bg);
    draw_text_center(r.x + r.w / 2, r.y + (r.h - FONT_H) / 2, txt, fg);
}

void draw_win_buttons(Rect frame, int hover_close, int hover_min, int hover_max) {
    UNUSED(frame); UNUSED(hover_close); UNUSED(hover_min); UNUSED(hover_max);
}

/* handles typed characters for a single line edit control.
   returns 1 if the buffer changed */
int ui_textfield_edit(char *buf, int cap, int *len, int *caret, YoroEvent *e) {
    if (e->type != EV_KEY) return 0;
    if (e->key == KEY_BACKSP) {
        if (*caret > 0) {
            memmove(buf + *caret - 1, buf + *caret, (size_t)(*len - *caret + 1));
            (*caret)--;
            (*len)--;
            return 1;
        }
        return 0;
    }
    if (e->key == KEY_DEL) {
        if (*caret < *len) {
            memmove(buf + *caret, buf + *caret + 1, (size_t)(*len - *caret));
            (*len)--;
            return 1;
        }
        return 0;
    }
    if (e->key == KEY_LEFT) { if (*caret > 0) (*caret)--; return 1; }
    if (e->key == KEY_RIGHT) { if (*caret < *len) (*caret)++; return 1; }
    if (e->key == KEY_HOME) { *caret = 0; return 1; }
    if (e->key == KEY_END) { *caret = *len; return 1; }
    if (e->ch >= 32 && *len + 4 < cap) {
        char enc[4];
        int n = utf8_encode((u16)e->ch, enc), k;
        if (*caret < *len)
            memmove(buf + *caret + n, buf + *caret, (size_t)(*len - *caret + 1));
        for (k = 0; k < n; k++) buf[*caret + k] = enc[k];
        (*caret) += n;
        (*len) += n;
        buf[*len] = 0;
        return 1;
    }
    return 0;
}

void ui_dialog_input(Rect box, const char *title, const char *value, int caret, int focus, Color accent, Color fg) {
    UNUSED(focus);
    fill_round_rect(box, 10, C_WHITE);
    draw_round_rect(box, 10, RGB(200, 206, 216));
    draw_text(box.x + 20, box.y + 18, title, fg);
    {
        Rect f = {box.x + 20, box.y + 50, box.w - 40, 32};
        ui_textfield(f, value, caret, 1, "");
        UNUSED(accent);
    }
}

int ui_measure_wrapped(const char *s, int width) {
    int lines = 1, x = 0;
    while (*s) {
        u16 cp;
        const Glyph *g;
        utf8_next(&s, &cp);
        g = font_lookup(cp);
        if (cp == '\n' || cp == '\r') { continue; }
        x += g ? g->adv : 6;
        if (x > width) { lines++; x = 0; }
    }
    return lines;
}

/* ---------------- toasts ---------------- */
static char toast_text[160] = "";
static u32 toast_until = 0;

void toast(const char *text) {
    strncpy(toast_text, text, 159);
    toast_text[159] = 0;
    toast_until = sys_ticks() + 260;
    wm_invalidate();
}

void toast_draw(void) {
    if (!toast_text[0] || sys_ticks() > toast_until) return;
    {
        int w = text_width(toast_text) + 44;
        Rect r = {(int)fb.width - w - 20, (int)fb.height - 46 - 64, w, 46};
        draw_shadow(r, 6);
        fill_round_rect(r, 8, RGB(28, 36, 52));
        fill_round_rect((Rect){r.x + 10, r.y + 14, 18, 18}, 9, theme->accent);
        draw_text(r.x + 38, r.y + (r.h - FONT_H) / 2, toast_text, RGB(240, 244, 252));
    }
}
