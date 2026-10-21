#ifndef GFX_H
#define GFX_H
#include "klib.h"

#define MAXW 800
#define MAXH 600

void gfx_init(u8 *fbptr, int w, int h, int pitch);
int gfx_w(void);
int gfx_h(void);

void gfx_wall_redraw(int theme);          /* перерисовать слой обоев */
void gfx_copy_wall(void);                 /* обои -> back */
void gfx_blit(void);                      /* back -> видеопамять */

void gfx_px(int x, int y, u32 c);
void gfx_fill(int x, int y, int w, int h, u32 c);
void gfx_blend(int x, int y, int w, int h, u32 c, int a); /* a: 0..255 */
void gfx_frame(int x, int y, int w, int h, u32 c);
void gfx_round(int x, int y, int w, int h, int r, u32 c);
void gfx_round_blend(int x, int y, int w, int h, int r, u32 c, int a);

int gfx_text(int x, int y, const char *s, u32 c, int mono);
int gfx_text_w(const char *s, int mono);
void gfx_text_c(int cx, int y, const char *s, u32 c, int mono);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_clip_win(int x, int y, int w, int h); /* пересечение с текущим */
u8 *gfx_fb_ptr(void);
int gfx_pitch(void);

u32 theme_top(int t);
u32 theme_bottom(int t);
u32 theme_accent(int t);
u32 theme_winbg(int t);
u32 theme_text(int t);
extern int g_theme;
#endif
