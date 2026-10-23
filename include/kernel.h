/* YoroOS - kernel & shell public interface */
#ifndef YORO_KERNEL_H
#define YORO_KERNEL_H

#include <stdarg.h>

typedef unsigned char  u8;
typedef signed char    i8;
typedef unsigned short u16;
typedef signed short   i16;
typedef unsigned int   u32;
typedef signed int     i32;
typedef unsigned long long u64;
typedef unsigned int   size_t;

#define NULL ((void*)0)
#define UNUSED(x) (void)(x)

/* ---------------- string / memory ---------------- */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
int   atoi(const char *s);
void  itoa(int v, char *buf);
void  utoa(u32 v, char *buf);
int   sprintf(char *buf, const char *fmt, ...);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
int   vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int   str_utf8_decode(const char *s, int *advance);   /* returns codepoint */
int   utf8_encode(u16 cp, char *out);
char *strstr(const char *h, const char *n);

/* ---------------- heap ---------------- */
void  heap_init(u32 start, u32 size);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
u32   heap_used(void);
u32   heap_total(void);

/* ---------------- port io / hardware ---------------- */
static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static inline u8  inb(u16 port) { u8 r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }
static inline void outw(u16 port, u16 v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port)); }
static inline u16 inw(u16 port) { u16 r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(port)); return r; }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }

/* ---------------- graphics ---------------- */
typedef struct {
    u32 width, height, pitch;   /* pitch in bytes */
    u8  bpp;
    u8  red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
    u8 *vram;                   /* real framebuffer */
    u8 *back;                   /* software back buffer (BGRA32) */
    int dual;                   /* back buffer active */
} FBInfo;

typedef struct { int x, y, w, h; } Rect;
typedef struct { u8 r, g, b, a; } Color;

extern FBInfo fb;

#define RGB(r,g,b) ((Color){(u8)(r),(u8)(g),(u8)(b),255})
#define RGBA(r,g,b,a) ((Color){(u8)(r),(u8)(g),(u8)(b),(u8)(a)})
#define C_WHITE   RGB(255,255,255)
#define C_BLACK   RGB(0,0,0)
#define C_ACCENT  RGB(0,120,215)

void gfx_init(void);
void gfx_present(void);
void gfx_present_rect(Rect r);
void gfx_clear(Color c);
void px(int x, int y, Color c);
void hline(int x0, int x1, int y, Color c);
void vline(int x, int y0, int y1, Color c);
void fill_rect(Rect r, Color c);
void fill_rect_alpha(Rect r, Color c);
void draw_rect(Rect r, Color c);         /* 1px outline */
void fill_round_rect(Rect r, int rad, Color c);
void fill_round_rect_alpha(Rect r, int rad, Color c);
void draw_round_rect(Rect r, int rad, Color c);
void fill_grad_v(Rect r, Color top, Color bottom);
void fill_grad_h(Rect r, Color left, Color right);
void fill_circle(int cx, int cy, int rad, Color c);
void draw_line(int x0, int y0, int x1, int y1, Color c);
void fill_ellipse(int cx, int cy, int rx, int ry, Color c);
void fill_poly(const int *xs, const int *ys, int n, Color c);
void draw_shadow(Rect r, int size);
void blit_copy(Rect dst, const u8 *src, int srcw);
void blit_scaled(Rect dst, const u8 *src, int sw, int sh);
void blend_px(int x, int y, Color c, u8 a);
void gfx_set_clip(Rect r);
Rect gfx_get_clip(void);
void *kzalloc(size_t n);
void  kfree(void *p);

/* font */
typedef struct { u16 cp; u8 w; u8 adv; u32 off; } Glyph;
extern const unsigned char font_pixels[];
extern const Glyph font_glyphs[];
extern const int font_count;
extern const int font_height;
#define FONT_H 18
const Glyph *font_lookup(u16 cp);
int  text_width(const char *s);
int  text_width_n(const char *s, int n);
void draw_text(int x, int y, const char *s, Color c);
void draw_text_bold(int x, int y, const char *s, Color c);
void draw_text_center(int cx, int y, const char *s, Color c);
void draw_text_right(int rx, int y, const char *s, Color c);
void draw_text_clip(int x, int y, const char *s, Color c, Rect clip);
void draw_text_wrap(Rect box, const char *s, Color c, int lineh);

/* ---------------- input events ---------------- */
#define EV_KEY        1
#define EV_MOUSE_MOVE 2
#define EV_MOUSE_DOWN 3
#define EV_MOUSE_UP   4
#define EV_WHEEL      5
#define EV_TICK       6
#define EV_RESIZE     7

#define KEY_ESC    27
#define KEY_BACKSP 8
#define KEY_TAB    9
#define KEY_ENTER  13
#define KEY_SHIFT  16
#define KEY_CTRL   17
#define KEY_ALT    18
#define KEY_UP     0x101
#define KEY_DOWN   0x102
#define KEY_LEFT   0x103
#define KEY_RIGHT  0x104
#define KEY_DEL    0x105
#define KEY_HOME   0x106
#define KEY_END    0x107
#define KEY_F1     0x120
#define KEY_F2     0x121
#define KEY_F3     0x122
#define KEY_F4     0x123
#define KEY_F5     0x124
#define KEY_F6     0x125
#define KEY_F7     0x126
#define KEY_F8     0x127
#define KEY_F9     0x128
#define KEY_F10    0x129

typedef struct {
    int type;
    int x, y;
    int key;        /* KEY_* or ascii */
    int ch;         /* unicode codepoint produced by key press */
    int btn;        /* 1 left, 2 right */
    int wheel;
    int shift, ctrl, alt;
} YoroEvent;

/* utf-8 helpers */
int  utf8_len(const char *s);
void utf8_next(const char **s, u16 *cp);

/* ---------------- RAM filesystem ---------------- */
#define FS_MAX_NODES 96
#define FS_NAME_MAX  40
#define FS_DATA_MAX  16384

typedef struct FSNode {
    char name[FS_NAME_MAX];
    u8   is_dir;
    u8   used;
    int  parent;      /* index, -1 for root */
    int  first_child;
    int  next_sibling;
    int  size;
    u8  *data;
    u32  icon;        /* 0 generic, 1 text, 2 image, 3 exe, 4 dir */
} FSNode;

void  fs_init(void);
void  fs_reset(void);
int   fs_root(void);
int   fs_lookup(int dir, const char *name);
int   fs_mkdir(int parent, const char *name);
int   fs_create(int parent, const char *name, const char *data);
int   fs_create_bin(int parent, const char *name, const char *data, int len);
int   fs_write(int node, const char *data, int len);
int   fs_remove(int node);
int   fs_rename(int node, const char *name);
int   fs_path(int node, char *out, int outlen);
int   fs_parse_path(int cwd, const char *path);
int   fs_count(int dir);
int   fs_child(int dir, int index);
const char *fs_name(int node);
int   fs_is_dir(int node);
int   fs_size(int node);
const char *fs_data(int node);
void  fs_seed(void);

/* ---------------- UI theme ---------------- */
typedef struct {
    char name[40];
    Color wall_top, wall_bottom, accent, accent_dark;
    u32 kind;      /* wallpaper style */
} Theme;
extern Theme *theme;
void  theme_init(void);
int   theme_count(void);
Theme *theme_get(int i);
void  theme_set(int i);
int   theme_current(void);
void  theme_draw_wallpaper(void);

/* ---------------- window manager ---------------- */
#define MAX_WINDOWS 24
#define WM_MAX_APPS 32

typedef struct Window Window;
typedef struct App App;

struct App {
    const char *id;
    const char *name;
    u32  icon;              /* icon id */
    int  builtin;
    int  installed;
    int  pinned;            /* shown on desktop */
    int  width, height;
    void (*open)(void);     /* create window */
};

typedef void (*DrawFn)(Window *w, Rect r);
typedef void (*EventFn)(Window *w, YoroEvent *e);
typedef void (*CloseFn)(Window *w);

struct Window {
    int  used;
    int  id;
    char title[64];
    Rect frame;             /* absolute */
    int  min_w, min_h;
    int  visible;
    int  minimized;
    int  maximized;
    Rect restore;           /* pre-maximize frame */
    u32  icon;
    App *app;
    int  scroll;            /* generic scroll offset for content */
    void *user;
    DrawFn  draw;           /* draw content into window client area */
    EventFn event;
    CloseFn close;
};

void  wm_init(void);
Window *wm_open(App *app, const char *title, int x, int y, int w, int h,
                DrawFn draw, EventFn event, CloseFn close);
void  wm_close(Window *w);
void  wm_focus(Window *w);
Window *wm_focused(void);
Window *wm_at(int x, int y);
void  wm_frame(void);       /* redraw everything + present */
void  wm_invalidate(void);
void  wm_tick(void);
void  wm_handle_event(YoroEvent *e);
Rect  wm_client(Window *w);
Window *wm_window_by_id(int id);
Window *wm_window_at_order(int i);
int   wm_window_count(void);
void  wm_alt_tab(void);
int   wm_taskbar_height(void);
void  system_shutdown(void);
void  system_reboot(void);
extern int wm_dirty;

/* ---------------- hardware / boot ---------------- */
void  mouse_init(void);
int   mouse_get_x(void);
int   mouse_get_y(void);
int   mouse_state(void);
void  rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year);
u32   sys_ticks(void);
int   input_pop(YoroEvent *e);
void  input_push(const YoroEvent *e);
void  serial_init(void);
void  serial_putc(char c);
void  serial_puts(const char *s);
void  klog(const char *fmt, ...);
void  kbd_toggle_layout(void);
void  ki_boot_splash(void);
void  main_loop(void);

/* ---------------- desktop ---------------- */
void  desktop_init(void);
void  desktop_draw(void);
void  desktop_draw_overlay(void);
int   desktop_event(YoroEvent *e);
void  desktop_rebuild(void);
void  startmenu_toggle(void);
int   startmenu_open(void);
void  desktop_set_wall_cache_dirty(void);
extern int desktop_selected;

/* ---------------- ui kit ---------------- */
int   pt_in(int x, int y, Rect r);
void  ui_panel(Rect r, Color bg, int radius, int shadow);
void  ui_button(Rect r, const char *label, int hover, int primary);
void  ui_icon_button(Rect r, u32 icon, int hover);
void  ui_toggle(Rect r, int on);
void  ui_checkbox(Rect r, int checked, const char *label, Color fg);
void  ui_progress(Rect r, int pct, Color c);
void  ui_scrollbar(Rect track, int total, int view, int scroll);
void  ui_textfield(Rect r, const char *text, int caret, int focused, const char *placeholder);
void  ui_caret(int x, int y, int h, Color c);
void  ui_card(Rect r, int hover, int radius);
void  draw_win_buttons(Rect frame, int hover_close, int hover_min, int hover_max);
void  ui_badge(Rect r, const char *txt, Color bg, Color fg);
int   ui_textfield_edit(char *buf, int cap, int *len, int *caret, YoroEvent *e);
void  ui_dialog_input(Rect box, const char *title, const char *value, int caret, int focus, Color accent, Color fg);
int   ui_measure_wrapped(const char *s, int width); /* returns line count */

/* ---------------- icons ---------------- */
u32 icon_id(const char *name);
void icon_draw(u32 id, int x, int y, int size);
#define ICON_APP       0
#define ICON_FOLDER    1
#define ICON_TERMINAL  2
#define ICON_SETTINGS  3
#define ICON_BROWSER   4
#define ICON_STORE     5
#define ICON_STEAM     6
#define ICON_PYTHON    7
#define ICON_VSCODE    8
#define ICON_NOTEPAD   9
#define ICON_CALC      10
#define ICON_COMPUTER  11
#define ICON_TRASH     12
#define ICON_TEXTFILE  13
#define ICON_IMAGE     14
#define ICON_MATRONIX  15
#define ICON_CHROME    16
#define ICON_FIREFOX   17
#define ICON_DOWNLOAD  18
#define ICON_ABOUT     19
#define ICON_POWER     20
#define ICON_USER      21
#define ICON_SEARCH    22
#define ICON_GIT       23
#define ICON_VLC       24
#define ICON_DISCORD   25
#define ICON_PAINT     26
#define ICON_DRIVE     27
#define ICON_UP        28
#define ICON_BACK      29
#define ICON_PLUS      30
#define ICON_TRASH_EMPTY 31
#define ICON_WIFI      32
#define ICON_VOLUME    33
#define ICON_BELL      34
#define ICON_ARROW_DOWN 35
#define ICON_CHECK     36
#define ICON_SETUP     37
#define ICON_ZIP       38
#define ICON_EXE       39

/* ---------------- events ---------------- */
void  yoro_init(void);
void  yoro_event(YoroEvent *e);
void  yoro_frame(void);
void  yoro_tick(void);
void  yoro_run_once(void);

/* apps */
void apps_init(void);
App *app_find(const char *id);
App *app_by_index(int i);
int  app_count(void);
void app_launch(App *a);
void app_install(App *a);
void app_desktop_rebuild(void);

void explorer_open(void);
void terminal_open(void);
void settings_open(void);
void browser_open(void);
void store_open(void);
void notepad_open(void);
void calc_open(void);
void about_open(void);
void notepad_open_file(int node);
void image_viewer_open(const char *name);
void explorer_open_path(const char *path);
void installer_open(void);
int  installer_running(void);
void zip_open_node(int node);
void exe_open_node(int node);

/* ---------------- ATA disk ---------------- */
void ata_init(void);
int  ata_disk_count(void);
int  ata_disk_index(int n);
const char *ata_disk_model(int d);
u32  ata_disk_sectors(int d);
int  ata_read(int d, u32 lba, u32 count, u8 *buf);
int  ata_write(int d, u32 lba, u32 count, const u8 *buf);
int  ata_write_buffer(int d, u32 lba, const u8 *data, u32 bytes);

/* ---------------- compression ---------------- */
int  inflate_raw(const u8 *in, int inlen, u8 *out, int outcap);

/* toast / notifications */
void toast(const char *text);
void toast_draw(void);

#endif
