/* YoroOS 0.2 "Aurora" - Portable 32/64-bit Kernel, HAL & Desktop Public Interface */
#ifndef YORO_KERNEL_H
#define YORO_KERNEL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t   u8;
typedef int8_t    i8;
typedef uint16_t  u16;
typedef int16_t   i16;
typedef uint32_t  u32;
typedef int32_t   i32;
typedef uint64_t  u64;
typedef int64_t   i64;
typedef uintptr_t uptr;

#define UNUSED(x) (void)(x)

#define YORO_VERSION_STR   "0.2.8"
#define YORO_CODENAME      "Aurora"
#define YORO_BUILD_STR     "0.2.8.2610"

/* ---------------- string / memory ---------------- */
void  *memset(void *d, int c, size_t n);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp_ascii(const char *a, const char *b);
char  *strcasestr_ascii(const char *hay, const char *needle);
#define strcasecmp strcasecmp_ascii
#define strchr     strchr_y
#define strrchr    strrchr_y
static inline int abs_i(int v) { return v < 0 ? -v : v; }
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strchr_y(const char *s, int ch);
char  *strrchr_y(const char *s, int ch);
int    atoi(const char *s);
void   itoa(int v, char *buf);
void   utoa(u32 v, char *buf);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, size_t n, const char *fmt, ...);
int    vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int    str_utf8_decode(const char *s, int *advance);
int    utf8_encode(u16 cp, char *out);
char  *strstr(const char *h, const char *n);
int    utf8_len(const char *s);
void   utf8_next(const char **s, u16 *cp);
u32    crc32_calc(const void *data, size_t len);

/* ---------------- kernel heap ---------------- */
void  heap_init(void *start, size_t size);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void *krealloc(void *p, size_t n);
void  kfree(void *p);
size_t heap_used(void);
size_t heap_total(void);

/* ---------------- graphics ---------------- */
typedef struct {
    u32 width, height, pitch;
    u32 bstride;
    u8  bpp;
    u8  red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
    u8 *vram;
    u8 *back;
    int dual;
} FBInfo;

typedef struct { int x, y, w, h; } Rect;
typedef struct { u8 r, g, b, a; } Color;

extern FBInfo fb;

#define RGB(r,g,b)    ((Color){(u8)(r),(u8)(g),(u8)(b),255})
#define RGBA(r,g,b,a) ((Color){(u8)(r),(u8)(g),(u8)(b),(u8)(a)})
#define C_WHITE       RGB(255,255,255)
#define C_BLACK       RGB(0,0,0)
#define C_ACCENT      RGB(0,120,215)

void gfx_init(void);
void gfx_realloc_backbuffer(void);
void gfx_present(void);
void gfx_present_rect(Rect r);
void gfx_clear(Color c);
void px(int x, int y, Color c);
void blend_px(int x, int y, Color c, u8 a);
void hline(int x0, int x1, int y, Color c);
void vline(int x, int y0, int y1, Color c);
void fill_rect(Rect r, Color c);
void fill_rect_alpha(Rect r, Color c);
void draw_rect(Rect r, Color c);
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
void gfx_set_clip(Rect r);
Rect gfx_get_clip(void);

/* C++20 Compositor & HD Wallpapers (fx.cpp) */
void fx_blur_rect(Rect r, int radius, Color tint);
void fx_acrylic_panel(Rect r, int corner_rad, Color tint, Color border, int shadow);
void fx_render_wallpaper(u32 *dst, int w, int h, int style_id);
void fx_draw_cursor(int mx, int my, int pressed);
void fx_draw_spinner(int cx, int cy, int rad, u32 tick, Color col);

/* ---------------- multi-face typography ---------------- */
#define FACE_UI    0
#define FACE_SMALL 1
#define FACE_TITLE 2
#define FACE_HERO  3
#define FACE_MONO  4

typedef struct {
    u16 cp;
    i8  xoff, yoff;
    u8  w, h, adv;
    u32 off;
} Glyph;

typedef struct {
    int line_h;
    int start;
    int count;
} FontFace;

extern const unsigned char font_pixels[];
extern const Glyph font_glyphs[];
extern const FontFace font_faces[5];
extern const int font_count;
extern const int font_height;
#define FONT_H 18

const Glyph *font_lookup_face(int face, u16 cp);
const Glyph *font_lookup(u16 cp);
int  text_width_face(int face, const char *s);
int  text_width(const char *s);
int  text_width_n(const char *s, int n);
void draw_text_face(int face, int x, int y, const char *s, Color c);
void draw_text_face_clip(int face, int x, int y, const char *s, Color c, Rect clip);
void draw_text(int x, int y, const char *s, Color c);
void draw_text_small(int x, int y, const char *s, Color c);
void draw_text_title(int x, int y, const char *s, Color c);
void draw_text_hero(int x, int y, const char *s, Color c);
void draw_text_mono(int x, int y, const char *s, Color c);
void draw_text_bold(int x, int y, const char *s, Color c);
void draw_text_center(int cx, int y, const char *s, Color c);
void draw_text_center_face(int face, int cx, int y, const char *s, Color c);
void draw_text_right(int rx, int y, const char *s, Color c);
void draw_text_clip(int x, int y, const char *s, Color c, Rect clip);
void draw_text_wrap(Rect box, const char *s, Color c, int lineh);
static inline int font_line_height(int face) { return font_faces[(face >= 0 && face < 5) ? face : 0].line_h; }
static inline void draw_text_clipped(int x, int y, int max_w, const char *s, Color c) {
    if (max_w > 0) draw_text_clip(x, y, s, c, (Rect){x, y - 4, max_w, 32});
}

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
#define KEY_PGUP   0x108
#define KEY_PGDN   0x109
#define KEY_WIN    0x10A
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
#define KEY_F11    0x12A
#define KEY_F12    0x12B

typedef struct {
    int type;
    int x, y;
    int key;
    int ch;
    int btn;        /* 1 left, 2 right, 3 middle */
    int wheel;
    int shift, ctrl, alt;
} YoroEvent;

/* ---------------- HAL: Unified Hardware Layer (BIOS x86 + UEFI x86_64 Gen 2) ---------------- */
typedef struct {
    int  id;
    char model[64];
    u64  sectors;        /* 512-byte logical sectors */
    u32  sector_bytes;
    int  removable;
    int  is_boot_disk;
} HalDiskInfo;

typedef struct {
    int mode_id;
    u32 width, height, bpp;
} HalVideoMode;

const char *hal_arch_name(void);     /* "x86_64 (UEFI Gen 2)" or "x86 (BIOS Gen 1)" */
const char *hal_cpu_brand(void);
const char *hal_pointer_driver_name(void);
u64   hal_ram_mb(void);
int   hal_is_uefi(void);
int   hal_booted_from_disk(void);    /* 1 if booted from installed YoroOS disk, 0 if Live ISO */
void  hal_present(void);
void  hal_present_rect(Rect r);
int   hal_video_mode_count(void);
int   hal_video_mode_get(int idx, HalVideoMode *out);
int   hal_video_set_mode(int mode_id);
void  hal_poll_input(void);          /* polls UEFI/Hyper-V mouse, keyboard, timer ticks */
void  hal_idle(void);
void  hal_beep(u32 freq_hz, u32 dur_ms);

int   hal_disk_count(void);
int   hal_disk_info(int idx, HalDiskInfo *out);
int   hal_disk_read(int idx, u64 lba512, u32 count512, void *buf);
int   hal_disk_write(int idx, u64 lba512, u32 count512, const void *buf);
int   hal_boot_media_read(u64 lba512, u32 count512, void *buf);
void  hal_disk_rescan(void);
const u8 *hal_self_efi_bytes(u32 *out_len);
const u8 *hal_bios_img_bytes(u32 *out_len);
int   hal_register_nvram_boot(const u8 part_guid[16], u64 esp_start_lba, u64 esp_sectors);

int   mouse_get_x(void);
int   mouse_get_y(void);
void  mouse_set_pos(int x, int y);
int   mouse_state(void);
int   kbd_is_ru(void);
void  kbd_set_ru(int ru);
void  kbd_toggle_layout(void);
void  rtc_read(int *hour, int *min, int *sec, int *day, int *mon, int *year);
u32   sys_ticks(void);               /* 100 Hz ticks (10 ms units) */
int   input_pop(YoroEvent *e);
void  input_push(const YoroEvent *e);
void  serial_putc(char c);
void  serial_puts(const char *s);
void  klog(const char *fmt, ...);
void  system_shutdown(void);
void  system_reboot(void);

/* ---------------- System Profile & Persistent YoroFS2 ---------------- */
#define EDITION_PRO      0
#define EDITION_DEV      1
#define EDITION_GAMING   2
#define EDITION_HOME     3

typedef struct {
    char username[32];
    char pc_name[32];
    char pin[16];
    int  avatar_id;
    int  edition;
    int  theme_id;
    int  wallpaper_id;
    int  lang;           /* 0 = RU, 1 = EN */
    int  tz_offset;      /* hours offset from UTC, default +4 Astrakhan */
    int  night_light;
    int  sound_on;
    int  setup_completed;
    int  installed_disk_idx;
} SysProfile;

extern SysProfile sys_profile;
const char *edition_name(int ed);

/* ---------------- Filesystem (YoroFS v2 - RAM + Persistent Disk Sync) ---------------- */
#define FS_MAX_NODES 256
#define FS_NAME_MAX  48
#define FS_DATA_MAX  (512 * 1024)

typedef struct FSNode {
    char name[FS_NAME_MAX];
    u8   is_dir;
    u8   used;
    int  parent;
    int  first_child;
    int  next_sibling;
    int  size;
    u8  *data;
    u32  icon;
} FSNode;

void  fs_init(void);
void  fs_reset(void);
int   fs_root(void);
int   fs_desktop_dir(void);
int   fs_downloads_dir(void);
int   fs_documents_dir(void);
int   fs_pictures_dir(void);
int   fs_trash_dir(void);
int   fs_windows_present(void);
void  fs_restore_system_files(void);
int   fs_lookup(int dir, const char *name);
int   fs_mkdir(int parent, const char *name);
int   fs_mkdir_p(int start_dir, const char *rel_path);
int   fs_create(int parent, const char *name, const char *data);
int   fs_create_bin(int parent, const char *name, const void *data, int len);
int   fs_write(int node, const void *data, int len);
int   fs_remove(int node);
int   fs_move_to_trash(int node);
int   fs_rename(int node, const char *name);
int   fs_copy_node(int node, int dest_dir);
int   fs_path(int node, char *out, int outlen);
int   fs_parse_path(int cwd, const char *path);
int   fs_count(int dir);
int   fs_child(int dir, int index);
int   fs_parent(int node);
const char *fs_name(int node);
int   fs_is_dir(int node);
int   fs_size(int node);
u32   fs_icon(int node);
const char *fs_data(int node);
const u8   *fs_raw(int node);
size_t fs_total_bytes(void);
int    fs_total_nodes(void);
void  fs_seed(void);
void  fs_open_any_file(int node);    /* Universal Windows file dispatcher! */

/* Persistent Disk Formatting, Pure-C FAT16 ESP Builder & Live Async OS Installer (diskfmt.c) */
typedef void (*InstallProgressFn)(int pct, const char *stage_msg);

typedef struct {
    int  active;
    int  done;
    int  failed;
    int  phase;
    int  disk_idx;
    int  pct;
    int  nvram_ok;
    u64  custom_c_mb;
    u64  total_lba;
    u64  last_usable;
    u64  c_end_lba;
    u64  bytes_written;
    u32  cursor_sec;
    u32  total_phase_sec;
    u8  *esp_img;        /* 16 MiB pure-C formatted FAT16 EFI System Partition image */
    u32  esp_sectors;
    u8   part1_guid[16];
    char status[160];
    char detail[160];
    char err[160];
} DiskInstallTask;

int   disk_install_begin(DiskInstallTask *t, int disk_idx, u64 custom_c_size_mb);
int   disk_install_step(DiskInstallTask *t);   /* returns 1 while running, 0 when finished/failed */
void  disk_install_free(DiskInstallTask *t);

int   disk_install_yoroos(int disk_idx, u64 custom_c_size_mb, InstallProgressFn cb, char *err_buf, int err_cap);
int   disk_save_yorofs(int disk_idx);
int   disk_load_yorofs(int disk_idx);
int   disk_probe_installed_yorofs(void);

/* ---------------- UI Theme & Wallpapers ---------------- */
typedef struct {
    char  name[64];
    Color accent;
    Color accent_dark;
    Color task_bg;
    Color menu_bg;
    Color win_bg;
    Color win_head;
    Color card_bg;
    Color text_main;
    Color text_muted;
    int   dark;
    int   default_wall;
} Theme;

extern Theme *theme;
void   theme_init(void);
int    theme_count(void);
Theme *theme_get(int i);
void   theme_set(int i);
int    theme_current(void);
int    wallpaper_count(void);
const char *wallpaper_name(int i);
void   wallpaper_set(int i);
int    wallpaper_current(void);
void   theme_draw_wallpaper(void);
void   desktop_set_wall_cache_dirty(void);

/* ---------------- Window Manager ---------------- */
#define MAX_WINDOWS 28
#define WM_MAX_APPS 32

typedef struct Window Window;
typedef struct App App;

struct App {
    const char *id;
    const char *name;
    u32  icon;
    int  builtin;
    int  installed;
    int  pinned;        /* Start Menu tile */
    int  on_desktop;    /* Shown on Desktop grid */
    int  width, height;
    void (*open)(void);
};

typedef void (*DrawFn)(Window *w, Rect r);
typedef void (*EventFn)(Window *w, YoroEvent *e);
typedef void (*CloseFn)(Window *w);

struct Window {
    int  used;
    int  id;
    char title[80];
    Rect frame;
    int  min_w, min_h;
    int  visible;
    int  minimized;
    int  maximized;
    int  frameless;
    Rect restore;
    u32  icon;
    App *app;
    int  scroll;
    void *user;
    DrawFn  draw;
    EventFn event;
    CloseFn close;
};

void    wm_init(void);
Window *wm_open(App *app, const char *title, int x, int y, int w, int h,
                DrawFn draw, EventFn event, CloseFn close);
void    wm_close(Window *w);
void    wm_close_all(void);
void    wm_focus(Window *w);
Window *wm_focused(void);
Window *wm_at(int x, int y);
void    wm_frame(void);
void    wm_invalidate(void);
void    wm_tick(void);
void    wm_handle_event(YoroEvent *e);
Rect    wm_client(Window *w);
Window *wm_window_by_id(int id);
Window *wm_window_at_order(int i);
int     wm_window_count(void);
void    wm_alt_tab(void);
int     wm_taskbar_height(void);
extern int wm_dirty;

/* Boot Stages / Screen Modes */
#define SCREEN_SETUP   0   /* Booted into Windows-style Graphical Setup Wizard */
#define SCREEN_LOCK    1   /* Windows 10/11 Lock & Sign-in Screen */
#define SCREEN_DESKTOP 2   /* Full YoroOS Desktop */
extern int system_screen_mode;

/* ---------------- Desktop, Taskbar, Start Menu & Context Menu ---------------- */
void desktop_init(void);
void desktop_draw(void);
void desktop_draw_overlay(void);
int  desktop_event(YoroEvent *e);
void desktop_rebuild(void);
void startmenu_toggle(void);
int  startmenu_open(void);
void lockscreen_lock(void);
void lockscreen_draw(void);
void lockscreen_event(YoroEvent *e);
extern int desktop_selected;

/* Context Popup Menu API (usable on Desktop AND inside Explorer / Apps!) */
typedef void (*CtxMenuCb)(int item_index, void *ctx);
void ctx_menu_open(int x, int y, const char **labels, int count, CtxMenuCb cb, void *ctx);
int  ctx_menu_active(void);
void ctx_menu_close(void);

/* ---------------- UI Kit ---------------- */
int  pt_in(int x, int y, Rect r);
void ui_panel(Rect r, Color bg, int radius, int shadow);
void ui_button(Rect r, const char *label, int hover, int primary);
void ui_button_danger(Rect r, const char *label, int hover);
void ui_icon_button(Rect r, u32 icon, int hover);
void ui_toggle(Rect r, int on);
void ui_checkbox(Rect r, int checked, const char *label, Color fg);
void ui_progress(Rect r, int pct, Color c);
void ui_scrollbar(Rect track, int total, int view, int scroll);
void ui_textfield(Rect r, const char *text, int caret, int focused, const char *placeholder);
void ui_caret(int x, int y, int h, Color c);
void ui_card(Rect r, int hover, int radius);
void draw_win_buttons(Rect frame, int hover_close, int hover_min, int hover_max);
void ui_badge(Rect r, const char *txt, Color bg, Color fg);
int  ui_textfield_edit(char *buf, int cap, int *len, int *caret, YoroEvent *e);
void ui_dialog_input(Rect box, const char *title, const char *value, int caret, int focus, Color accent, Color fg);
int  ui_measure_wrapped(const char *s, int width);

/* ---------------- Vector Icons ---------------- */
u32  icon_id(const char *name);
void icon_draw(u32 id, int x, int y, int size);
void avatar_draw(int avatar_id, int x, int y, int size);

#define ICON_APP          0
#define ICON_FOLDER       1
#define ICON_TERMINAL     2
#define ICON_SETTINGS     3
#define ICON_BROWSER      4
#define ICON_STORE        5
#define ICON_STEAM        6
#define ICON_PYTHON       7
#define ICON_VSCODE       8
#define ICON_NOTEPAD      9
#define ICON_CALC         10
#define ICON_COMPUTER     11
#define ICON_TRASH        12
#define ICON_TEXTFILE     13
#define ICON_IMAGE        14
#define ICON_MATRONIX     15
#define ICON_CHROME       16
#define ICON_FIREFOX      17
#define ICON_DOWNLOAD     18
#define ICON_ABOUT        19
#define ICON_POWER        20
#define ICON_USER         21
#define ICON_SEARCH       22
#define ICON_GIT          23
#define ICON_VLC          24
#define ICON_DISCORD      25
#define ICON_PAINT        26
#define ICON_DRIVE        27
#define ICON_UP           28
#define ICON_BACK         29
#define ICON_PLUS         30
#define ICON_TRASH_EMPTY  31
#define ICON_WIFI         32
#define ICON_VOLUME       33
#define ICON_BELL         34
#define ICON_ARROW_DOWN   35
#define ICON_CHECK        36
#define ICON_SETUP        37
#define ICON_ZIP          38
#define ICON_EXE          39
#define ICON_TASKMGR      40
#define ICON_REGEDIT      41
#define ICON_SHIELD       42
#define ICON_CODEFILE     43
#define ICON_RESTORE      44

/* ---------------- Kernel Lifecycle ---------------- */
void ki_boot_splash(void);
void kernel_boot_common(void);
void main_loop(void);

/* ---------------- Applications ---------------- */
void apps_init(void);
App *app_find(const char *id);
App *app_by_index(int i);
int  app_count(void);
void app_launch(App *a);
void app_install(App *a);
void app_uninstall(App *a);
void app_desktop_rebuild(void);

void explorer_open(void);
void explorer_open_path(const char *path);
void terminal_open(void);
void terminal_open_cmd(const char *cmd);
void settings_open(void);
void settings_open_section(int sec);
void browser_open(void);
void browser_open_url(const char *url);
void matronix_open(void);
void chrome_open(void);
void firefox_open(void);
void store_open(void);
void notepad_open(void);
void notepad_open_file(int node);
void calc_open(void);
void about_open(void);
void paint_open(void);
void paint_open_file(int node);
void image_viewer_open_node(int node);
void taskmgr_open(void);
void regedit_open(void);
void regedit_open_file(int node);
void vscode_open(void);
void vscode_open_file(int node);
void python_app_open(void);
void python_run_script(const char *code, char *out, int outcap);
void steam_open(void);
void vlc_open(void);
void git_app_open(void);
void discord_open(void);
void batch_run_node(int node);

/* Windows-style Setup Wizard (fullscreen boot mode + desktop window mode!) */
void installer_init_boot(void);
void installer_draw_fullscreen(void);
void installer_event_fullscreen(YoroEvent *e);
void installer_tick(void);
void installer_open(void);
int  installer_running(void);

/* ZIP & Win32/PE64 Executable Subsystem */
void zip_open_node(int node);
int  zip_extract_all_to(const u8 *buf, int len, int dest_dir);
int  zip_create_from_node(int src_node, int dest_dir, char *out_name, int cap);
void exe_open_node(int node);

int  pe_is_pe(const u8 *data, int len);
int  pe_is_pe64(const u8 *data, int len);
void pe_summary(const u8 *data, int len, char *out, int cap);
int  pe_run(const u8 *data, int len, const char *cmdline, char *log, int cap, int *exit_code);
int  pe_last_msgbox(char *title_out, int tcap, char *body_out, int bcap);

/* Compression */
int  inflate_raw(const u8 *in, int inlen, u8 *out, int outcap);

/* Toast Notifications & Action Center */
void toast(const char *text);
void toast_draw(void);

#ifdef __cplusplus
}
#endif

#endif
