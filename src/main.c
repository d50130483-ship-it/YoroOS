/* YoroOS kernel, step 1: white screen with the default pointer.
 *
 * Runs as a UEFI application (Hyper-V Gen 2 or any UEFI machine). It takes the
 * Graphics Output Protocol framebuffer, paints the background white and draws
 * the pointer at the centre of the screen. Input and the rest of the system come
 * in later steps.
 */
#include "efi.h"
#include "cursor_data.h"

#define BG_R 255
#define BG_G 255
#define BG_B 255

static const EFI_GUID GOP_GUID = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

typedef EFI_STATUS (*locate_protocol_fn)(const EFI_GUID *, void *, void **);
typedef EFI_STATUS (*stall_fn)(u64 microseconds);

static EFI_BOOT_SERVICES *g_bs;

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
    draw_cursor(fb, pitch, w, h, format, (w - CURSOR_W) / 2, (h - CURSOR_H) / 2);

    /* Keep the picture up. Stall keeps the loop from spinning at full speed. */
    for (;;) ((stall_fn)g_bs->Stall)(100000);
    return 0;
}
