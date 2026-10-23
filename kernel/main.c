/* YoroOS - boot splash, main loop and global hotkeys */
#include "kernel.h"

static void splash_logo(int cx, int cy, int r) {
    int i;
    /* ring of 4 rounded squares - the YoroOS mark */
    fill_circle(cx, cy, r, RGBA(255, 255, 255, 16));
    for (i = 0; i < 4; i++) {
        int ox = (i % 2) ? 1 : -1;
        int oy = (i / 2) ? 1 : -1;
        Rect q = {cx + ox * r / 3 - r / 4, cy + oy * r / 3 - r / 4, r / 2, r / 2};
        fill_round_rect_alpha(q, r / 8, RGBA(255, 255, 255, 220));
    }
}

void ki_boot_splash(void) {
    int frame;
    Rect full = {0, 0, (int)fb.width, (int)fb.height};
    gfx_set_clip(full);
    for (frame = 0; frame < 16; frame++) {
        int cx = (int)fb.width / 2;
        int cy = (int)fb.height / 2;
        fill_grad_v(full, RGB(8, 20, 40), RGB(2, 6, 14));
        {
            int i;
            for (i = 0; i < 3; i++) {
                int rr = 26 + ((frame * 3 + i * 14) % 90);
                Color c = {60, 150, 240, (u8)(60 - (rr - 26) / 2)};
                int k;
                for (k = 0; k < 360; k += 6) {
                    int x = cx + rr * 1000 / 1000 * 0;
                    UNUSED(x);
                }
                fill_rect_alpha((Rect){cx - rr, cy - rr, rr * 2, rr * 2}, c);
            }
        }
        splash_logo(cx, cy - 24, 44);
        draw_text_center(cx, cy + 44, "YoroOS", C_WHITE);
        draw_text_center(cx, cy + 70, "0.1 «Astra»", RGB(140, 180, 230));
        {
            Rect bar = {cx - 130, (int)fb.height - 90, 260, 4};
            fill_round_rect_alpha(bar, 2, RGBA(255, 255, 255, 30));
            fill_round_rect((Rect){bar.x, bar.y, bar.w * frame / 16, bar.h}, 2, RGB(90, 170, 255));
        }
        draw_text_center(cx, (int)fb.height - 60, "загрузка ядра...", RGB(110, 130, 160));
        gfx_present();
        {
            volatile int spin;
            for (spin = 0; spin < 400000; spin++) {}
        }
    }
}

void yoro_init(void) {
    wm_invalidate();
}

void yoro_event(YoroEvent *e) {
    if (!e) return;

    if (e->type == EV_TICK) {
        if ((sys_ticks() % 25) != 0) return;
        wm_dirty = 1;
        wm_handle_event(e);
        return;
    }

    if (e->type == EV_MOUSE_MOVE || e->type == EV_MOUSE_DOWN || e->type == EV_MOUSE_UP ||
        e->type == EV_WHEEL)
        wm_dirty = 1;

    if (e->type == EV_KEY) {
        if (e->ctrl && e->alt && (e->key == 't' || e->key == 'T' || e->key == 0x0422)) {
            terminal_open();
            return;
        }
        if (e->ctrl && e->alt && (e->key == 'l' || e->key == 'L' || e->key == 0x0414)) {
            kbd_toggle_layout();
            toast("Раскладка: RU / EN");
            return;
        }
        if (e->key == KEY_F10) {
            app_launch(app_find("store"));
            return;
        }
        if (e->key == KEY_ESC) {
            /* Escape closes the start menu first */
        }
    }
    wm_handle_event(e);
    wm_dirty = 1;
}

void yoro_tick(void) {
    wm_dirty = 1;
}

void main_loop(void) {
    YoroEvent e;
    /* first paint */
    wm_dirty = 1;
    for (;;) {
        int had = 0;
        while (input_pop(&e)) {
            yoro_event(&e);
            had = 1;
        }
        if (had || wm_dirty) {
            wm_frame();
        } else {
            __asm__ volatile("sti; hlt");
        }
    }
}
