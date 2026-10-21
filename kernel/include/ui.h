#ifndef UI_H
#define UI_H
#include "klib.h"

enum {
    AP_TERMINAL = 0, AP_NOTES, AP_CALENDAR, AP_MUSIC, AP_CALC, AP_FINDER, AP_SETTINGS, AP_ABOUT, AP_VIEWER, AP_COUNT
};

#define MENUBAR_H 22
#define DOCK_H 58

void ui_init(void);
void ui_frame(void);                     /* обработка ввода + перерисовка при dirty */
void ui_request_redraw(void);
void ui_redraw(void);
void ui_anim_tick(void);                 /* вызывается на тике PIT */

/* установщик (installer.c) */
void installer_start(void);
int installer_frame(void);               /* 1 пока активен */
int installer_active(void);
void inst_theme_save(void);

/* окна */
int ui_open(int app, int w, int h);      /* индекс окна или -1 */
void ui_close(int idx);
void ui_close_app(int app);
int ui_focus(void);                      /* индекс в фокусе или -1 */
int ui_find_win(int app);
int ui_win_app(int idx);
void ui_set_title(int idx, const char *s);
int ui_minimized(int idx);
void ui_unminimize(int idx);

/* курсор */
int ui_mx(void);
int ui_my(void);
int ui_mbuttons(void);

/* приложение -> ui */
const char *app_name(int id);
void app_launch(int id);
void app_close_app(int app);
void app_close_cleanup(int id);
void ui_in_window_content(int idx, int *x, int *y); /* координаты контента */

/* меню YoroOS */
void ui_set_menu_shown(int on);
#endif
