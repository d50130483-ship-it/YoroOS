/* YoroOS 0.2 - Windows 11 Style System Settings (Display, Themes, Apps, User, Disks, Recovery) */
#include "kernel.h"

typedef struct {
    int sec;   /* 0=System, 1=Personalize, 2=Apps, 3=Accounts, 4=Disks, 5=Recovery */
    int focus; /* 0=username, 1=pc_name */
    int ulen, ucaret, plen, pcaret;
} SetState;

static void set_draw(Window *w, Rect r) {
    SetState *st = (SetState *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect side = {r.x, r.y, 214, r.h};
    Rect body = {r.x + 214, r.y, r.w - 214, r.h};
    static const struct { const char *name; u32 ic; } nav[6] = {
        { "Система и Экран",    ICON_COMPUTER },
        { "Персонализация",     ICON_PAINT    },
        { "Приложения",         ICON_STORE    },
        { "Учётные записи",     ICON_USER     },
        { "Диски и Загрузчик",  ICON_DRIVE    },
        { "Восстановление ОС",  ICON_RESTORE  },
    };
    int i, bx = body.x + 24, by = body.y + 20;

    fill_rect(r, theme->win_bg);
    fill_rect(side, theme->card_bg);
    vline(side.x + side.w - 1, side.y, side.y + side.h - 1, RGBA(130, 145, 170, 55));

    avatar_draw(sys_profile.avatar_id, side.x + 16, side.y + 16, 40);
    draw_text_bold(side.x + 66, side.y + 18, sys_profile.username, theme->text_main);
    draw_text_small(side.x + 66, side.y + 38, sys_profile.pc_name, theme->text_muted);

    for (i = 0; i < 6; i++) {
        Rect nr = {side.x + 10, side.y + 72 + i * 40, side.w - 20, 36};
        int act = (st->sec == i);
        if (act) {
            fill_round_rect_alpha(nr, 6, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 58));
            fill_rect((Rect){nr.x, nr.y + 8, 3, 20}, theme->accent);
        } else if (pt_in(mx, my, nr)) {
            fill_round_rect_alpha(nr, 6, RGBA(130, 145, 170, 30));
        }
        icon_draw(nav[i].ic, nr.x + 10, nr.y + 7, 22);
        draw_text(nr.x + 40, nr.y + 9, nav[i].name, theme->text_main);
    }

    if (st->sec == 0) {
        char buf[160];
        int nm = hal_video_mode_count();
        draw_text_title(bx, by, "Система, Дисплей и Оборудование", theme->text_main);
        ui_card((Rect){bx, by + 38, body.w - 48, 126}, 0, 8);
        snprintf(buf, sizeof(buf), "Редакция ОС : %s", edition_name(sys_profile.edition));
        draw_text_bold(bx + 16, by + 52, buf, theme->text_main);
        snprintf(buf, sizeof(buf), "Архитектура : %s", hal_arch_name());
        draw_text_small(bx + 16, by + 76, buf, theme->text_muted);
        snprintf(buf, sizeof(buf), "Процессор   : %s", hal_cpu_brand());
        draw_text_small(bx + 16, by + 96, buf, theme->text_muted);
        snprintf(buf, sizeof(buf), "Указатель   : %s   |   ОЗУ: %u МБ", hal_pointer_driver_name(), (u32)hal_ram_mb());
        draw_text_small(bx + 16, by + 116, buf, theme->accent);
        snprintf(buf, sizeof(buf), "Текущий режим: %ux%u (%u bpp)", fb.width, fb.height, fb.bpp);
        draw_text_small(bx + 16, by + 138, buf, theme->text_main);

        draw_text_bold(bx, by + 180, "Быстрые переключатели экрана:", theme->text_main);
        ui_button((Rect){bx, by + 206, 220, 36},
                  sys_profile.night_light ? "Ночной свет: ВКЛЮЧЁН" : "Ночной свет: ВЫКЛЮЧЕН",
                  pt_in(mx, my, (Rect){bx, by + 206, 220, 36}), sys_profile.night_light);
        ui_button((Rect){bx + 232, by + 206, 230, 36},
                  kbd_is_ru() ? "Язык ввода: Русский (RU)" : "Язык ввода: English (US)",
                  pt_in(mx, my, (Rect){bx + 232, by + 206, 230, 36}), 0);

        draw_text_bold(bx, by + 258, "Доступные видеорежимы UEFI GOP / VBE:", theme->text_main);
        for (i = 0; i < nm && i < 6; i++) {
            HalVideoMode vm;
            char mlbl[36];
            Rect mb = {bx + (i % 3) * 182, by + 286 + (i / 3) * 44, 172, 36};
            hal_video_mode_get(i, &vm);
            snprintf(mlbl, sizeof(mlbl), "%ux%u", vm.width, vm.height);
            ui_button(mb, mlbl, pt_in(mx, my, mb), vm.width == fb.width && vm.height == fb.height);
        }
    }
    else if (st->sec == 1) {
        draw_text_title(bx, by, "Персонализация: Темы и HD-обои", theme->text_main);
        draw_text_bold(bx, by + 38, "Цветовая тема интерфейса (Fluent Acrylic):", theme->text_main);
        for (i = 0; i < theme_count(); i++) {
            Theme *t = theme_get(i);
            Rect tb2 = {bx + (i % 2) * 276, by + 64 + (i / 2) * 44, 266, 38};
            fill_round_rect(tb2, 6, t->win_bg);
            draw_round_rect(tb2, 6, theme_current() == i ? t->accent : RGBA(130, 145, 170, 80));
            fill_circle(tb2.x + 18, tb2.y + 19, 8, t->accent);
            draw_text_clipped(tb2.x + 34, tb2.y + 10, 224, t->name, t->text_main);
        }

        draw_text_bold(bx, by + 208, "Обои Рабочего стола (8 процедурных HD-полотен):", theme->text_main);
        for (i = 0; i < wallpaper_count(); i++) {
            Rect wb = {bx + (i % 2) * 276, by + 236 + (i / 2) * 42, 266, 36};
            ui_button(wb, wallpaper_name(i), pt_in(mx, my, wb), wallpaper_current() == i);
        }
    }
    else if (st->sec == 2) {
        int row_y = by + 46 - w->scroll;
        draw_text_title(bx, by, "Установленные и системные приложения", theme->text_main);
        for (i = 0; i < app_count(); i++) {
            App *a = app_by_index(i);
            Rect card = {bx, row_y, body.w - 48, 42};
            if (!a) continue;
            if (row_y + 42 >= by + 40 && row_y < body.y + body.h - 10) {
                ui_card(card, 0, 6);
                icon_draw(a->icon, card.x + 10, card.y + 8, 26);
                draw_text_bold(card.x + 44, card.y + 11, a->name, theme->text_main);
                ui_button((Rect){card.x + card.w - 208, card.y + 6, 94, 30},
                          a->installed ? "Открыть" : "Установить",
                          pt_in(mx, my, (Rect){card.x + card.w - 208, card.y + 6, 94, 30}), 1);
                if (!a->builtin && a->installed)
                    ui_button_danger((Rect){card.x + card.w - 104, card.y + 6, 92, 30}, "Удалить",
                                     pt_in(mx, my, (Rect){card.x + card.w - 104, card.y + 6, 92, 30}));
            }
            row_y += 48;
        }
    }
    else if (st->sec == 3) {
        char tz[64];
        draw_text_title(bx, by, "Учётная запись владельца ПК", theme->text_main);
        draw_text(bx, by + 46, "Имя пользователя:", theme->text_main);
        ui_textfield((Rect){bx, by + 72, 320, 36}, sys_profile.username, st->ucaret, st->focus == 0, "User");
        draw_text(bx, by + 120, "Имя компьютера в сети:", theme->text_main);
        ui_textfield((Rect){bx, by + 146, 320, 36}, sys_profile.pc_name, st->pcaret, st->focus == 1, "YORO-PC");

        draw_text(bx, by + 198, "Аватар профиля:", theme->text_main);
        for (i = 0; i < 6; i++) {
            avatar_draw(i, bx + i * 58, by + 226, 44);
            if (sys_profile.avatar_id == i)
                draw_round_rect((Rect){bx + i * 58 - 3, by + 223, 50, 50}, 25, theme->accent);
        }

        snprintf(tz, sizeof(tz), "Часовой пояс: UTC%+d", sys_profile.tz_offset);
        ui_button((Rect){bx, by + 292, 220, 36}, tz, pt_in(mx, my, (Rect){bx, by + 292, 220, 36}), 0);
        ui_button((Rect){bx + 234, by + 292, 220, 36}, "Сохранить профиль",
                  pt_in(mx, my, (Rect){bx + 234, by + 292, 220, 36}), 1);
    }
    else if (st->sec == 4) {
        int nd = hal_disk_count();
        char stline[140];
        draw_text_title(bx, by, "Управление дисками GPT / UEFI ESP / YoroFS2", theme->text_main);
        snprintf(stline, sizeof(stline), "Режим текущего тома C: : %s",
                 sys_profile.installed_disk_idx >= 0
                 ? "Установлен на диск (автосохранение YoroFS2 активно!)"
                 : "Live-сессия в ОЗУ (установите на диск для сохранения после перезагрузки)");
        draw_text(bx, by + 42, stline, sys_profile.installed_disk_idx >= 0 ? RGB(72, 214, 142) : theme->accent);

        for (i = 0; i < nd && i < 4; i++) {
            HalDiskInfo di;
            Rect c = {bx, by + 76 + i * 54, body.w - 48, 46};
            hal_disk_info(i, &di);
            ui_card(c, 0, 7);
            icon_draw(ICON_DRIVE, c.x + 12, c.y + 10, 26);
            draw_text_bold(c.x + 48, c.y + 13, di.model, theme->text_main);
        }

        if (sys_profile.installed_disk_idx >= 0) {
            ui_button((Rect){bx + 272, by + 310, 250, 40}, "Синхронизировать диск сейчас",
                      pt_in(mx, my, (Rect){bx + 272, by + 310, 250, 40}), 0);
        }
    }
    else {
        draw_text_title(bx, by, "Восстановление и целостность YoroOS", theme->text_main);
        ui_card((Rect){bx, by + 44, body.w - 48, 96}, 0, 8);
        draw_text_bold(bx + 16, by + 58,
            fs_windows_present()
            ? "Статус: Все системные каталоги (C:\\YoroOS\\System32) присутствуют на месте."
            : "Внимание: Вы удалили системные папки C:\\YoroOS! Система продолжает работать стабильно.",
            fs_windows_present() ? RGB(72, 214, 142) : RGB(255, 192, 68));
        draw_text_small(bx + 16, by + 86,
            "В YoroOS вы полный хозяин ПК: можно удалять любые системные файлы и восстанавливать их в 1 клик.",
            theme->text_muted);

        ui_button((Rect){bx, by + 162, 320, 42}, "Восстановить системные файлы YoroOS",
                  pt_in(mx, my, (Rect){bx, by + 162, 320, 42}), 1);
        ui_button_danger((Rect){bx, by + 218, 320, 42}, "Сбросить все файлы YoroFS к заводским",
                         pt_in(mx, my, (Rect){bx, by + 218, 320, 42}));
    }
}

static void set_event(Window *w, YoroEvent *e) {
    SetState *st = (SetState *)w->user;
    Rect r = wm_client(w);
    Rect side = {r.x, r.y, 214, r.h};
    Rect body = {r.x + 214, r.y, r.w - 214, r.h};
    int bx = body.x + 24, by = body.y + 20, i;

    if (e->type == EV_WHEEL && st->sec == 2) {
        w->scroll -= e->wheel * 34;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
        return;
    }

    if (st->sec == 3 && e->type == EV_KEY) {
        if (st->focus == 0) ui_textfield_edit(sys_profile.username, sizeof(sys_profile.username), &st->ulen, &st->ucaret, e);
        else ui_textfield_edit(sys_profile.pc_name, sizeof(sys_profile.pc_name), &st->plen, &st->pcaret, e);
        wm_invalidate();
        return;
    }

    if (e->type != EV_MOUSE_DOWN || e->btn != 1) return;

    for (i = 0; i < 6; i++) {
        if (pt_in(e->x, e->y, (Rect){side.x + 10, side.y + 72 + i * 40, side.w - 20, 36})) {
            st->sec = i; w->scroll = 0; wm_invalidate(); return;
        }
    }

    if (st->sec == 0) {
        int nm = hal_video_mode_count();
        if (pt_in(e->x, e->y, (Rect){bx, by + 206, 220, 36})) { sys_profile.night_light = !sys_profile.night_light; wm_invalidate(); }
        if (pt_in(e->x, e->y, (Rect){bx + 232, by + 206, 230, 36})) kbd_toggle_layout();
        for (i = 0; i < nm && i < 6; i++) {
            Rect mb = {bx + (i % 3) * 182, by + 286 + (i / 3) * 44, 172, 36};
            if (pt_in(e->x, e->y, mb)) {
                HalVideoMode vm;
                hal_video_mode_get(i, &vm);
                hal_video_set_mode(vm.mode_id);
                wm_invalidate();
            }
        }
    } else if (st->sec == 1) {
        for (i = 0; i < theme_count(); i++) {
            Rect tb2 = {bx + (i % 2) * 276, by + 64 + (i / 2) * 44, 266, 38};
            if (pt_in(e->x, e->y, tb2)) {
                theme_set(i);
                if (sys_profile.installed_disk_idx >= 0) disk_save_yorofs(sys_profile.installed_disk_idx);
            }
        }
        for (i = 0; i < wallpaper_count(); i++) {
            Rect wb = {bx + (i % 2) * 276, by + 236 + (i / 2) * 42, 266, 36};
            if (pt_in(e->x, e->y, wb)) {
                wallpaper_set(i);
                if (sys_profile.installed_disk_idx >= 0) disk_save_yorofs(sys_profile.installed_disk_idx);
            }
        }
    } else if (st->sec == 2) {
        int row_y = by + 46 - w->scroll;
        for (i = 0; i < app_count(); i++) {
            App *a = app_by_index(i);
            Rect card = {bx, row_y, body.w - 48, 42};
            if (!a) continue;
            if (pt_in(e->x, e->y, (Rect){card.x + card.w - 208, card.y + 6, 94, 30})) {
                if (!a->installed) app_install(a);
                else app_launch(a);
                return;
            }
            if (!a->builtin && a->installed &&
                pt_in(e->x, e->y, (Rect){card.x + card.w - 104, card.y + 6, 92, 30})) {
                app_uninstall(a);
                return;
            }
            row_y += 48;
        }
    } else if (st->sec == 3) {
        if (pt_in(e->x, e->y, (Rect){bx, by + 72, 320, 36})) st->focus = 0;
        if (pt_in(e->x, e->y, (Rect){bx, by + 146, 320, 36})) st->focus = 1;
        for (i = 0; i < 6; i++)
            if (pt_in(e->x, e->y, (Rect){bx + i * 58, by + 226, 44, 44})) sys_profile.avatar_id = i;
        if (pt_in(e->x, e->y, (Rect){bx, by + 292, 220, 36}))
            sys_profile.tz_offset = (sys_profile.tz_offset + 1) > 12 ? -5 : (sys_profile.tz_offset + 1);
        if (pt_in(e->x, e->y, (Rect){bx + 234, by + 292, 220, 36})) {
            if (sys_profile.installed_disk_idx >= 0) disk_save_yorofs(sys_profile.installed_disk_idx);
            toast("Профиль владельца успешно сохранён!");
        }
        wm_invalidate();
    } else if (st->sec == 4) {
        if (sys_profile.installed_disk_idx >= 0 &&
            pt_in(e->x, e->y, (Rect){bx + 272, by + 310, 250, 40})) {
            disk_save_yorofs(sys_profile.installed_disk_idx);
            toast("Файловая система YoroFS2 успешно синхронизирована с диском!");
        }
    } else {
        if (pt_in(e->x, e->y, (Rect){bx, by + 162, 320, 42})) fs_restore_system_files();
        if (pt_in(e->x, e->y, (Rect){bx, by + 218, 320, 42})) {
            fs_reset();
            if (sys_profile.installed_disk_idx >= 0) disk_save_yorofs(sys_profile.installed_disk_idx);
            desktop_rebuild();
            toast("Файловая система сброшена к заводскому образу YoroOS 0.2!");
        }
    }
}

static void set_close(Window *w) { kfree(w->user); }

void settings_open_section(int sec) {
    SetState *st = (SetState *)kzalloc(sizeof(SetState));
    if (!st) return;
    st->sec = sec;
    st->ulen = (int)strlen(sys_profile.username); st->ucaret = st->ulen;
    st->plen = (int)strlen(sys_profile.pc_name);  st->pcaret = st->plen;
    wm_open(app_find("settings"), "Параметры YoroOS 0.2", -1, -1, 820, 520, set_draw, set_event, set_close)->user = st;
}

void settings_open(void) { settings_open_section(0); }
