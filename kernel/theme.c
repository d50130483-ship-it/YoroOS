/* YoroOS 0.2 - Windows 10/11 Fluent Theme, Edition & Cached Wallpaper Engine */
#include "kernel.h"

SysProfile sys_profile = {
    "User",         /* username */
    "YORO-PC",      /* pc_name */
    "",             /* pin */
    0,              /* avatar_id */
    EDITION_PRO,    /* edition */
    0,              /* theme_id */
    0,              /* wallpaper_id */
    0,              /* lang: 0=RU */
    4,              /* tz_offset: UTC+4 Astrakhan */
    0,              /* night_light */
    1,              /* sound_on */
    0,              /* setup_completed */
    -1              /* installed_disk_idx */
};

static Theme g_themes[6] = {
    { "Yoro Сумерки (Тёмная)",
      {59, 130, 246, 255}, {37, 99, 235, 255},
      {15, 23, 42, 212}, {17, 24, 39, 230},
      {24, 30, 44, 255}, {17, 23, 36, 255}, {33, 41, 58, 255},
      {248, 250, 252, 255}, {148, 163, 184, 255}, 1, 0 },
    { "Yoro Дневная (Светлая)",
      {37, 99, 235, 255}, {29, 78, 216, 255},
      {15, 23, 42, 214}, {248, 250, 252, 236},
      {248, 250, 252, 255}, {238, 242, 249, 255}, {255, 255, 255, 255},
      {15, 23, 42, 255}, {100, 116, 139, 255}, 0, 0 },
    { "Изумрудный туман",
      {16, 185, 129, 255}, {5, 150, 105, 255},
      {12, 20, 24, 214}, {16, 26, 30, 230},
      {21, 30, 34, 255}, {15, 23, 27, 255}, {30, 42, 46, 255},
      {241, 245, 249, 255}, {148, 163, 184, 255}, 1, 2 },
    { "Индиго Минимал",
      {129, 140, 248, 255}, {99, 102, 241, 255},
      {17, 17, 27, 216}, {22, 22, 35, 232},
      {26, 26, 40, 255}, {19, 19, 31, 255}, {36, 36, 54, 255},
      {248, 250, 252, 255}, {165, 180, 252, 255}, 1, 3 },
    { "Тёплые дюны",
      {217, 119, 6, 255}, {180, 83, 9, 255},
      {24, 20, 20, 214}, {30, 25, 25, 232},
      {33, 28, 28, 255}, {25, 21, 21, 255}, {44, 37, 37, 255},
      {250, 250, 249, 255}, {168, 162, 158, 255}, 1, 1 },
    { "Чистый графит",
      {56, 189, 248, 255}, {2, 132, 199, 255},
      {15, 17, 21, 220}, {19, 22, 28, 236},
      {23, 26, 33, 255}, {17, 19, 25, 255}, {32, 36, 46, 255},
      {244, 244, 245, 255}, {161, 161, 170, 255}, 1, 3 },
};

static const char *const kWallNames[4] = {
    "Северные сумерки",
    "Песчаные дюны",
    "Изумрудный туман",
    "Чистый графит",
};

static const char *const kEditions[4] = {
    "YoroOS 0.2.7 Standard",
    "YoroOS 0.2.7 Developer",
    "YoroOS 0.2.7 Gaming",
    "YoroOS 0.2.7 Lite",
};

Theme *theme = &g_themes[0];
static int g_cur_theme = 0;
static int g_cur_wall = 0;

static u32 *g_wall_cache = NULL;
static int  g_wall_w = 0, g_wall_h = 0, g_wall_cached_id = -1;

const char *edition_name(int ed) {
    if (ed < 0 || ed >= 4) ed = 0;
    return kEditions[ed];
}

int theme_count(void) { return 6; }
Theme *theme_get(int i) { return (i >= 0 && i < 6) ? &g_themes[i] : &g_themes[0]; }
int theme_current(void) { return g_cur_theme; }

void theme_set(int i) {
    if (i < 0 || i >= 6) i = 0;
    g_cur_theme = i;
    theme = &g_themes[i];
    sys_profile.theme_id = i;
    wm_invalidate();
}

int wallpaper_count(void) { return 4; }
const char *wallpaper_name(int i) { return kWallNames[i & 3]; }
int wallpaper_current(void) { return g_cur_wall; }

void desktop_set_wall_cache_dirty(void) { g_wall_cached_id = -1; }

void wallpaper_set(int i) {
    g_cur_wall = i & 3;
    sys_profile.wallpaper_id = g_cur_wall;
    g_wall_cached_id = -1;
    wm_invalidate();
}

void theme_draw_wallpaper(void) {
    int w = (int)fb.width, h = (int)fb.height, y;
    if (w <= 0 || h <= 0 || !fb.back) return;
    if (!g_wall_cache || g_wall_w != w || g_wall_h != h) {
        u32 *nb = (u32 *)kmalloc((size_t)w * (size_t)h * sizeof(u32));
        if (!nb) {
            fill_grad_v((Rect){0, 0, w, h}, RGB(8, 22, 52), RGB(4, 58, 118));
            return;
        }
        if (g_wall_cache) kfree(g_wall_cache);
        g_wall_cache = nb;
        g_wall_w = w; g_wall_h = h;
        g_wall_cached_id = -1;
    }
    if (g_wall_cached_id != g_cur_wall) {
        fx_render_wallpaper(g_wall_cache, w, h, g_cur_wall);
        g_wall_cached_id = g_cur_wall;
    }
    for (y = 0; y < h; y++) {
        memcpy(fb.back + (size_t)y * fb.bstride, g_wall_cache + (size_t)y * (size_t)w, (size_t)w * 4U);
    }
    if (sys_profile.night_light)
        fill_rect_alpha((Rect){0, 0, w, h}, RGBA(255, 146, 56, 28));
}

void theme_init(void) {
    theme_set(sys_profile.theme_id);
    wallpaper_set(sys_profile.wallpaper_id);
}
