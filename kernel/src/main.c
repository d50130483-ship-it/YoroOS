/* YoroOS — точка входа ядра */
#include "ui.h"
#include "gfx.h"
#include "hw.h"
#include "fs.h"
#include "klib.h"

/* раскладка, переданная загрузчиком в 0x0B00: pitch,w,h,bpp(8бит),pad,fb(u32) */
struct bootvid {
    u16 pitch, w, h;
    u8 bpp, pad;
    u32 fb;
};

void kmain(void) {
    volatile struct bootvid *bv = (volatile struct bootvid *)0x0B00;
    gfx_init((u8 *)(u32)bv->fb, bv->w, bv->h, bv->pitch);
    hw_init();
    gfx_wall_redraw(0);
    fs_init();

    int fresh = !fs_valid();
    if (!fresh) settings_load();
    gfx_wall_redraw(g_theme);

    ui_init();

    if (fresh) {
        installer_start();
        while (installer_frame())
            hw_sleep_wait();
        /* после установки тема уже применена */
    }

    for (;;) {
        ui_frame();
        hw_sleep_wait();
    }
}
