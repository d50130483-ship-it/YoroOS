/* YoroOS kernel: white screen with the default pointer.
 *
 * Runs as a UEFI application (Hyper-V Gen 2 or any UEFI machine). It takes the
 * Graphics Output Protocol framebuffer, paints the background white and draws
 * the pointer. On Hyper-V the pointer follows the synthetic mouse (hv_mouse.c);
 * elsewhere it stays in the centre of the screen.
 */
#include "efi.h"
#include "cursor_data.h"
#include "hv_mouse.h"

#define BG_R 255
#define BG_G 255
#define BG_B 255

#define ALLOCATE_ANY_PAGES 0
#define LOADER_CODE        1      /* executable memory: the hypercall page lives here */
#define POLL_INTERVAL_US   10000  /* pointer poll period on Hyper-V */
#define IDLE_INTERVAL_US   100000 /* period when there is no mouse */

static const EFI_GUID GOP_GUID = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

typedef EFI_STATUS (*locate_protocol_fn)(const EFI_GUID *, void *, void **);
typedef EFI_STATUS (*stall_fn)(u64 microseconds);
typedef EFI_STATUS (*allocate_pages_fn)(u32 type, u32 memory_type, u64 pages, u64 *address);

static EFI_BOOT_SERVICES *g_bs;

/* Debug aid for the Hyper-V mouse bring-up: a black block per step at the top left.
 * The last block on screen shows how far the driver got. Remove when the driver works. */
#define DEBUG_STEPS 1
static u32 *g_fb;
static u32 g_pitch, g_format;

/* Writes one pixel in the framebuffer's native channel order. */
static inline u32 pack_rgb(u32 format, u32 r, u32 g, u32 b) {
    if (format == 1) return (r << 16) | (g << 8) | b; /* BGR: bytes B,G,R,x */
    return (b << 16) | (g << 8) | r;                  /* RGB: bytes R,G,B,x */
}

static void paint_background(u32 *fb, u32 pitch, u32 w, u32 h, u32 format) {
    u32 color = pack_rgb(format, BG_R, BG_G, BG_B);
    for (u32 y = 0; y < h; y++) {
        u32 *row = fb + (u64)y * pitch;
        for (u32 x = 0; x < w; x++) row[x] = color;
    }
}

/* Fills a rectangle with the background colour, clipped to the screen. */
static void paint_rect_bg(u32 *fb, u32 pitch, u32 w, u32 h, u32 format, u32 x0, u32 y0, u32 rw, u32 rh) {
    u32 color = pack_rgb(format, BG_R, BG_G, BG_B);
    for (u32 y = y0; y < y0 + rh && y < h; y++) {
        u32 *row = fb + (u64)y * pitch;
        for (u32 x = x0; x < x0 + rw && x < w; x++) row[x] = color;
    }
}

/* Alpha-blends the cursor over whatever is already on screen. */
static void draw_cursor(u32 *fb, u32 pitch, u32 w, u32 h, u32 format, u32 x0, u32 y0) {
    for (u32 cy = 0; cy < CURSOR_H; cy++) {
        u32 y = y0 + cy;
        if (y >= h) break;
        for (u32 cx = 0; cx < CURSOR_W; cx++) {
            u32 x = x0 + cx;
            if (x >= w) break;
            const unsigned char *p = &CURSOR_RGBA[(cy * CURSOR_W + cx) * 4];
            u32 a = p[3];
            if (a == 0) continue;
            u32 *dst = &fb[(u64)y * pitch + x];
            u32 d = *dst;
            /* Read the destination back in its own channel order. */
            u32 dr, dg, db;
            if (format == 1) { dr = (d >> 16) & 0xFF; dg = (d >> 8) & 0xFF; db = d & 0xFF; }
            else             { db = (d >> 16) & 0xFF; dg = (d >> 8) & 0xFF; dr = d & 0xFF; }
            u32 r = (p[0] * a + dr * (255 - a) + 127) / 255;
            u32 g = (p[1] * a + dg * (255 - a) + 127) / 255;
            u32 b = (p[2] * a + db * (255 - a) + 127) / 255;
            *dst = pack_rgb(format, r, g, b);
        }
    }
}

#if DEBUG_STEPS
static void step_block(u32 step) {
    u32 x0 = 8 + step * 24, y0 = 8;
    for (u32 y = y0; y < y0 + 16; y++)
        for (u32 x = x0; x < x0 + 16; x++)
            g_fb[(u64)y * g_pitch + x] = pack_rgb(g_format, 0, 0, 0);
}
#endif

/* Erases the pointer at its old place, then draws it at the new one. */
static void move_cursor(u32 *fb, u32 pitch, u32 w, u32 h, u32 format,
                        u32 old_x, u32 old_y, u32 x0, u32 y0) {
    paint_rect_bg(fb, pitch, w, h, format, old_x, old_y, CURSOR_W, CURSOR_H);
    draw_cursor(fb, pitch, w, h, format, x0, y0);
}

EFI_STATUS EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    (void)image;
    g_bs = st->BootServices;

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = ((locate_protocol_fn)g_bs->LocateProtocol)(&GOP_GUID, 0, (void **)&gop);
    if (status != 0 || gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0) {
        /* No framebuffer: nothing to show. Return to firmware. */
        return status ? status : 1;
    }

    EFI_GOP_MODE *mode = gop->Mode;
    u32 w = mode->Info->HorizontalResolution;
    u32 h = mode->Info->VerticalResolution;
    u32 pitch = mode->Info->PixelsPerScanLine;
    u32 format = mode->Info->PixelFormat;
    u32 *fb = (u32 *)(u64)mode->FrameBufferBase;

    paint_background(fb, pitch, w, h, format);
    g_fb = fb;
    g_pitch = pitch;
    g_format = format;
#if DEBUG_STEPS
    step_block(1);
#endif

    /* On Hyper-V start the synthetic mouse; anywhere else the pointer stays centred. */
    int mouse = 0;
    if (hvm_detect()) {
        u64 pool = 0;
        EFI_STATUS st_pool = ((allocate_pages_fn)g_bs->AllocatePages)(
            ALLOCATE_ANY_PAGES, LOADER_CODE, HVM_POOL_PAGES, &pool);
        if (st_pool == 0 && pool != 0)
#if DEBUG_STEPS
            mouse = hvm_init((u8 *)(u64)pool, (u64)HVM_POOL_PAGES * 4096ULL, step_block) == 0;
#else
            mouse = hvm_init((u8 *)(u64)pool, (u64)HVM_POOL_PAGES * 4096ULL, 0) == 0;
#endif
    }

    u32 cur_x = (w - CURSOR_W) / 2;
    u32 cur_y = (h - CURSOR_H) / 2;
    draw_cursor(fb, pitch, w, h, format, cur_x, cur_y);

    /* Keep the picture up and follow the mouse. Stall keeps the loop from spinning. */
    for (;;) {
        if (mouse && hvm_poll()) {
            u32 rx, ry, mx, my, buttons;
            hvm_pointer(&rx, &ry, &mx, &my, &buttons);
            u32 nx = mx ? (u32)(((u64)rx * (w - 1)) / mx) : 0;
            u32 ny = my ? (u32)(((u64)ry * (h - 1)) / my) : 0;
            if (nx > w - CURSOR_W) nx = w - CURSOR_W;
            if (ny > h - CURSOR_H) ny = h - CURSOR_H;
            if (nx != cur_x || ny != cur_y) {
                move_cursor(fb, pitch, w, h, format, cur_x, cur_y, nx, ny);
                cur_x = nx;
                cur_y = ny;
            }
        }
        ((stall_fn)g_bs->Stall)(mouse ? POLL_INTERVAL_US : IDLE_INTERVAL_US);
    }
    return 0;
}
