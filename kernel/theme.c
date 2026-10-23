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
    { "Windows 10/11 Тёмная (Синяя)",
      {0, 120, 212, 255}, {0, 88, 164, 255},
      {14, 18, 28, 188}, {18, 23, 35, 206},
      {28, 31, 40, 255}, {21, 24, 32, 255}, {38, 43, 55, 255},
      {242, 245, 250, 255}, {154, 164, 182, 255}, 1, 0 },
    { "Windows 10/11 Светлая (Дневная)",
      {0, 120, 212, 255}, {0, 88, 164, 255},
      {20, 25, 36, 198}, {244, 247, 252, 218},
      {246, 248, 252, 255}, {235, 240, 248, 255}, {255, 255, 255, 255},
      {24, 28, 36, 255}, {96, 104, 120, 255}, 0, 2 },
    { "Aurora Северное Сияние",
      {0, 184, 156, 255}, {0, 134, 114, 255},
      {12, 22, 30, 190}, {16, 28, 38, 208},
      {24, 31, 38, 255}, {18, 24, 30, 255}, {34, 44, 54, 255},
      {240, 248, 248, 255}, {148, 170, 174, 255}, 1, 3 },
    { "Gaming Фиолетовый Неон",
      {142, 82, 245, 255}, {104, 52, 196, 255},
      {18, 14, 30, 194}, {22, 18, 38, 212},
      {27, 24, 40, 255}, {20, 17, 32, 255}, {38, 34, 56, 255},
      {245, 242, 255, 255}, {164, 154, 192, 255}, 1, 1 },
    { "Янтарный Закат",
      {224, 96, 36, 255}, {176, 68, 18, 255},
      {26, 18, 24, 192}, {32, 22, 30, 210},
      {34, 28, 33, 255}, {26, 20, 25, 255}, {46, 38, 45, 255},
      {250, 244, 242, 255}, {180, 162, 162, 255}, 1, 4 },
    { "Изумрудная Классика",
      {22, 152, 84, 255}, {14, 112, 60, 255},
      {16, 24, 22, 192}, {244, 249, 246, 216},
      {246, 250, 247, 255}, {234, 242, 237, 255}, {255, 255, 255, 255},
      {22, 32, 26, 255}, {92, 110, 98, 255}, 0, 7 },
};

static const char *const kWallNames[8] = {
    "YoroOS Hero",
    "Bloom Тёмная",
    "Bloom Светлая",
    "Северное сияние",
    "Солнечный закат",
    "Кибер-сетка",
    "Степь и Волга",
    "Зелёные холмы",
};

static const char *const kEditions[4] = {
    "YoroOS 0.2 Pro",
    "YoroOS 0.2 Developer Workstation",
    "YoroOS 0.2 Gaming Edition",
    "YoroOS 0.2 Home",
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

int wallpaper_count(void) { return 8; }
const char *wallpaper_name(int i) { return kWallNames[i & 7]; }
int wallpaper_current(void) { return g_cur_wall; }

void desktop_set_wall_cache_dirty(void) { g_wall_cached_id = -1; }

void wallpaper_set(int i) {
    g_cur_wall = i & 7;
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
