/* YoroOS - Settings app */
#include "kernel.h"

#define NAV_W 210
#define HEAD_H 64

static int section = 1;     /* 0 system, 1 personalization, 2 apps, 3 about */
static int apps_scroll = 0;

static const char *nav_names[] = { "Система", "Персонализация", "Приложения", "О системе" };
static const u32 nav_icons[] = { ICON_COMPUTER, ICON_PAINT, ICON_STORE, ICON_ABOUT };

static void draw_theme_preview(Rect r, int idx, int selected) {
    Theme *t = theme_get(idx);
    fill_round_rect(r, 8, t->wall_top);
    fill_grad_v(r, t->wall_top, t->wall_bottom);
    fill_round_rect((Rect){r.x + r.w - 34, r.y + 8, 26, 26}, 6, t->accent);
    if (selected) {
        draw_round_rect((Rect){r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 10, t->accent);
        fill_circle(r.x + r.w - 18, r.y + 18, 9, C_WHITE);
        icon_draw(ICON_CHECK, r.x + r.w - 27, r.y + 9, 18);
    }
}

static void draw_section_system(Rect a) {
    char buf[160];
    int h, m, s, d, mo, yy;
    Rect card = {a.x, a.y, a.w, 210};
    rtc_read(&h, &m, &s, &d, &mo, &yy);
    ui_card(card, 0, 10);
    draw_text(card.x + 20, card.y + 16, "Сведения о системе", RGB(24, 30, 42));
    sprintf(buf, "YoroOS 0.1 «Astra»   сборка %d", 1000 + (int)(sys_ticks() / 1000));
    draw_text(card.x + 20, card.y + 46, buf, RGB(80, 88, 102));
    sprintf(buf, "Экран: %d × %d, %d бит", (int)fb.width, (int)fb.height, (int)fb.bpp);
    draw_text(card.x + 20, card.y + 72, buf, RGB(80, 88, 102));
    sprintf(buf, "Память: %d МиБ (занято %d КиБ)", (int)heap_total() / 1024, (int)heap_used() / 1024);
    draw_text(card.x + 20, card.y + 98, buf, RGB(80, 88, 102));
    sprintf(buf, "Дата: %02d.%02d.%04d   Время: %02d:%02d:%02d", d, mo, yy, h, m, s);
    draw_text(card.x + 20, card.y + 124, buf, RGB(80, 88, 102));
    draw_text(card.x + 20, card.y + 150, "Ядро: YoroKernel 0.1 · Загрузчик: YoroBoot", RGB(140, 148, 162));

    /* actions */
    {
        Rect c2 = {a.x, a.y + 226, a.w, 150};
        ui_card(c2, 0, 10);
        draw_text(c2.x + 20, c2.y + 16, "Обслуживание", RGB(24, 30, 42));
        ui_button((Rect){c2.x + 20, c2.y + 50, 220, 34}, "Перезагрузить", 0, 0);
        ui_button((Rect){c2.x + 254, c2.y + 50, 220, 34}, "Выключить", 0, 0);
        ui_button((Rect){c2.x + 20, c2.y + 96, 220, 34}, "Сбросить файлы (format)", 0, 0);
        draw_text(c2.x + 254, c2.y + 104, "Сброс восстановит стартовый набор файлов", RGB(150, 158, 172));
    }
}

static void draw_section_personalization(Rect a) {
    int i, cols = 3, cw = (a.w - 32) / cols, ch = 110;
    draw_text(a.x, a.y, "Тема оформления", RGB(24, 30, 42));
    for (i = 0; i < theme_count(); i++) {
        int col = i % cols, row = i / cols;
        Rect card = {a.x + col * (cw + 16), a.y + 30 + row * (ch + 52), cw, ch};
        draw_theme_preview(card, i, theme_current() == i);
        draw_text(card.x, card.y + ch + 8, theme_get(i)->name, RGB(60, 68, 82));
    }
    {
        Rect info = {a.x, a.y + 30 + ((theme_count() + cols - 1) / cols) * (ch + 52) + 4, a.w, 90};
        ui_card(info, 0, 10);
        draw_text(info.x + 16, info.y + 14, "Обои, цвета и акценты меняются мгновенно", RGB(80, 88, 102));
        draw_text(info.x + 16, info.y + 40, "Раскладка клавиатуры: Ctrl+Alt+L (RU / EN)", RGB(80, 88, 102));
        draw_text(info.x + 16, info.y + 62, "Совет: наберите в терминале theme 3", RGB(150, 158, 172));
    }
}

static void draw_section_apps(Rect a) {
    int i, n = app_count(), y = a.y + 4;
    Rect list = {a.x, a.y, a.w, a.h};
    draw_text(a.x, a.y - 4, "Установленные приложения", RGB(24, 30, 42));
    y = a.y + 26;
    gfx_set_clip(list);
    for (i = 0; i < n; i++) {
        App *ap = app_by_index(i);
        Rect row;
        if (!ap->installed) continue;
        y -= apps_scroll;
        row = (Rect){a.x, y, a.w, 56};
        if (row.y + row.h > a.y && row.y < a.y + a.h) {
            ui_card(row, 0, 8);
            icon_draw(ap->icon, row.x + 12, row.y + 12, 32);
            draw_text(row.x + 56, row.y + 10, ap->name, RGB(30, 36, 48));
            draw_text(row.x + 56, row.y + 30, ap->builtin ? "Системное приложение" : "Из Магазина YoroOS",
                      RGB(140, 148, 162));
            ui_button((Rect){row.x + row.w - 130, row.y + 12, 116, 32}, "Удалить", 0, 0);
        }
        y += 64;
    }
    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
}

static void draw_section_about(Rect a) {
    Rect logo = {a.x + (a.w - 96) / 2, a.y + 10, 96, 96};
    fill_circle(logo.x + 48, logo.y + 48, 48, theme->accent);
    draw_text_center(logo.x + 48, logo.y + 30, "Yoro", C_WHITE);
    draw_text_center(logo.x + 48, logo.y + 50, "OS", C_WHITE);
    draw_text_center(a.x + a.w / 2, a.y + 124, "YoroOS 0.1 «Astra»", RGB(24, 30, 42));
    draw_text_center(a.x + a.w / 2, a.y + 148, "Своё ядро · свой загрузчик · свой ISO", RGB(120, 128, 144));
    {
        Rect card = {a.x + 20, a.y + 190, a.w - 40, 190};
        char buf[160];
        ui_card(card, 0, 10);
        draw_text(card.x + 18, card.y + 16, "Особенности", RGB(24, 30, 42));
        draw_text(card.x + 18, card.y + 44, "• Полностью удаляемая файловая система", RGB(70, 78, 92));
        draw_text(card.x + 18, card.y + 68, "• Проводник, Терминал, Параметры, YoroNet, Магазин", RGB(70, 78, 92));
        draw_text(card.x + 18, card.y + 92, "• Установка приложений с иконками", RGB(70, 78, 92));
        draw_text(card.x + 18, card.y + 116, "• 6 тем оформления и антиалиасинг-шрифты", RGB(70, 78, 92));
        sprintf(buf, "• Файловая система: %d узлов, всё в ОЗУ", 96);
        draw_text(card.x + 18, card.y + 140, buf, RGB(70, 78, 92));
        draw_text(card.x + 18, card.y + 164, "• Собрано из исходников: C, C++/ассемблер", RGB(70, 78, 92));
    }
}

static void draw(Window *w, Rect c) {
    int i, mx = mouse_get_x(), my = mouse_get_y();
    Rect nav = {c.x, c.y, NAV_W, c.h};
    Rect area = {c.x + NAV_W + 26, c.y + HEAD_H + 18, c.w - NAV_W - 52, c.h - HEAD_H - 44};
    Rect head = {c.x + NAV_W, c.y, c.w - NAV_W, HEAD_H};

    fill_rect(nav, RGB(242, 244, 249));
    fill_rect((Rect){nav.x + nav.w - 1, nav.y, 1, nav.h}, RGB(226, 230, 238));
    draw_text(nav.x + 20, nav.y + 20, "Параметры", RGB(30, 36, 48));
    for (i = 0; i < 4; i++) {
        Rect it = {nav.x + 8, nav.y + 56 + i * 46, nav.w - 16, 40};
        int hov = pt_in(mx, my, it);
        if (section == i) fill_round_rect_alpha(it, 6, RGBA(0, 120, 215, 36));
        else if (hov) fill_round_rect_alpha(it, 6, RGBA(0, 0, 0, 12));
        if (section == i) fill_round_rect((Rect){it.x, it.y + 10, 3, 20}, 2, theme->accent);
        icon_draw(nav_icons[i], it.x + 12, it.y + 11, 18);
        draw_text(it.x + 40, it.y + (it.h - FONT_H) / 2, nav_names[i], RGB(40, 46, 60));
    }
    draw_text(nav.x + 20, nav.y + nav.h - 40, "YoroOS 0.1", RGB(150, 158, 172));

    fill_rect(head, C_WHITE);
    fill_rect((Rect){head.x, head.y + HEAD_H - 1, head.w, 1}, RGB(232, 235, 242));
    draw_text(head.x + 26, head.y + 22, nav_names[section], RGB(24, 30, 42));

    fill_rect((Rect){c.x + NAV_W, c.y + HEAD_H, c.w - NAV_W, c.h - HEAD_H}, RGB(250, 251, 253));
    if (section == 0) draw_section_system(area);
    else if (section == 1) draw_section_personalization(area);
    else if (section == 2) draw_section_apps(area);
    else draw_section_about(area);
    UNUSED(w);
}

static void on_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    int i;
    if (e->type == EV_WHEEL && section == 2) {
        apps_scroll -= e->wheel * 40;
        if (apps_scroll < 0) apps_scroll = 0;
        if (apps_scroll > 1200) apps_scroll = 1200;
        wm_invalidate();
        return;
    }
    if (e->type != EV_MOUSE_DOWN) return;
    for (i = 0; i < 4; i++) {
        Rect it = {c.x + 8, c.y + 56 + i * 46, NAV_W - 16, 40};
        if (pt_in(mx, my, it)) { section = i; apps_scroll = 0; wm_invalidate(); return; }
    }
    if (section == 1) {
        int cols = 3, cw = (c.w - NAV_W - 52 - 32) / cols, ch = 96;
        for (i = 0; i < theme_count(); i++) {
            int col = i % cols, row = i / cols;
            Rect card = {c.x + NAV_W + 26 + col * (cw + 16), c.y + HEAD_H + 18 + 30 + row * (ch + 52), cw, ch};
            if (pt_in(mx, my, card)) { theme_set(i); toast("Тема применена"); wm_invalidate(); return; }
        }
    }
    if (section == 2) {
        int n = app_count(), y = c.y + HEAD_H + 18 + 26 - apps_scroll;
        for (i = 0; i < n; i++) {
            App *ap = app_by_index(i);
            Rect row;
            if (!ap->installed) continue;
            row = (Rect){c.x + NAV_W + 26, y, c.w - NAV_W - 52, 56};
            if (pt_in(mx, my, (Rect){row.x + row.w - 130, row.y + 12, 116, 32})) {
                ap->installed = 0;
                toast("Приложение удалено");
                wm_invalidate();
                return;
            }
            y += 64;
        }
    }
    if (section == 0) {
        Rect c2 = {c.x + NAV_W + 26, c.y + HEAD_H + 18 + 226, c.w - NAV_W - 52, 150};
        if (pt_in(mx, my, (Rect){c2.x + 20, c2.y + 50, 220, 34})) { system_reboot(); return; }
        if (pt_in(mx, my, (Rect){c2.x + 254, c2.y + 50, 220, 34})) { system_shutdown(); return; }
        if (pt_in(mx, my, (Rect){c2.x + 20, c2.y + 96, 220, 34})) {
            fs_reset();
            fs_seed();
            toast("Файловая система сброшена к стандартной");
            wm_invalidate();
            return;
        }
    }
}

void settings_open(void) {
    wm_open(app_find("settings"), "Параметры", -1, -1, 880, 620, draw, on_event, NULL);
    wm_invalidate();
}
