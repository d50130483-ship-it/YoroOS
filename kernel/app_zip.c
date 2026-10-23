/* YoroOS - ZIP archive browser and Windows executable (EXE) inspector */
#include "kernel.h"

/* ---------------- little endian helpers ---------------- */
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

#define ZIP_MAX_ENTRIES 64
#define ZIP_NAME_MAX 80

typedef struct {
    char name[ZIP_NAME_MAX];
    u32 csize, usize, offset;
    u16 method;
} ZipEntry;

static u8 *zip_buf = NULL;
static int zip_len = 0;
static char zip_title[80];
static ZipEntry zip_entries[ZIP_MAX_ENTRIES];
static int zip_count = 0;
static int zip_scroll = 0;
static int zip_sel = -1;
static char zip_status[160] = "";
static int zip_node = -1;

static int zip_parse(void) {
    int off = 0, n = 0;
    zip_count = 0;
    while (off + 30 <= zip_len && n < ZIP_MAX_ENTRIES) {
        const u8 *p = zip_buf + off;
        u16 nlen, xlen;
        if (rd32(p) != 0x04034b50UL) break;
        nlen = rd16(p + 26);
        xlen = rd16(p + 28);
        if (off + 30 + nlen > zip_len) break;
        {
            ZipEntry *e = &zip_entries[n];
            int copy = nlen < ZIP_NAME_MAX - 1 ? nlen : ZIP_NAME_MAX - 1;
            memcpy(e->name, p + 30, (size_t)copy);
            e->name[copy] = 0;
            e->method = rd16(p + 8);
            e->csize = rd32(p + 18);
            e->usize = rd32(p + 22);
            e->offset = (u32)(off + 30 + nlen + xlen);
            n++;
        }
        off += 30 + nlen + xlen + (int)zip_entries[n - 1].csize;
    }
    zip_count = n;
    return n;
}

static int zip_extract(int idx, int dest_dir) {
    ZipEntry *e;
    u8 *out;
    int size;
    if (idx < 0 || idx >= zip_count) return -1;
    e = &zip_entries[idx];
    if ((int)e->offset + (int)e->csize > zip_len) return -1;
    if (e->usize > 64 * 1024) { strcpy(zip_status, "Файл слишком большой для YoroFS (лимит 64 КиБ)"); return -1; }
    out = (u8 *)kmalloc(e->usize + 1);
    if (!out) return -1;
    if (e->method == 0) {
        if ((int)e->csize > (int)e->usize) { kfree(out); return -1; }
        memcpy(out, zip_buf + e->offset, e->csize);
        size = (int)e->csize;
    } else if (e->method == 8) {
        size = inflate_raw(zip_buf + e->offset, (int)e->csize, out, (int)e->usize);
        if (size < 0) { kfree(out); strcpy(zip_status, "Ошибка распаковки Deflate"); return -1; }
    } else {
        kfree(out);
        strcpy(zip_status, "Метод сжатия не поддерживается");
        return -1;
    }
    out[size] = 0;
    {
        char name[FS_NAME_MAX];
        int i, l = (int)strlen(e->name);
        /* strip directories, keep the base name */
        for (i = l - 1; i >= 0; i--) if (e->name[i] == '/' || e->name[i] == '\\') break;
        i++;
        strncpy(name, e->name + i, FS_NAME_MAX - 1);
        name[FS_NAME_MAX - 1] = 0;
        if (!name[0]) { kfree(out); return -1; }
        fs_create(dest_dir, name, (const char *)out);
        snprintf(zip_status, sizeof(zip_status), "Распаковано: %s (%d Б)", name, size);
    }
    kfree(out);
    return 0;
}

/* ---------------- ZIP viewer window ---------------- */
static void zip_draw(Window *w, Rect c) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int i;
    Rect head = {c.x, c.y, c.w, 74};
    Rect list = {c.x + 14, c.y + 84, c.w - 28, c.h - 150};
    Rect foot = {c.x, c.y + c.h - 58, c.w, 58};

    fill_grad_v(head, RGB(24, 48, 88), RGB(16, 30, 56));
    icon_draw(ICON_FOLDER, head.x + 18, head.y + 18, 38);
    draw_text(head.x + 68, head.y + 16, zip_title, C_WHITE);
    {
        char sub[120];
        snprintf(sub, sizeof(sub), "ZIP-архив · файлов: %d", zip_count);
        draw_text(head.x + 68, head.y + 40, sub, RGB(170, 200, 240));
    }

    fill_round_rect(list, 8, C_WHITE);
    draw_round_rect(list, 8, RGB(222, 226, 236));
    gfx_set_clip((Rect){list.x + 1, list.y + 1, list.w - 2, list.h - 2});
    for (i = 0; i < zip_count; i++) {
        Rect row = {list.x + 1, list.y + 1 + i * 32 - zip_scroll, list.w - 2, 32};
        ZipEntry *e = &zip_entries[i];
        int hov = pt_in(mx, my, row);
        if (zip_sel == i) fill_rect(row, theme->accent);
        else if (hov) fill_rect(row, RGB(243, 246, 251));
        icon_draw(ICON_TEXTFILE, row.x + 10, row.y + 7, 18);
        draw_text_clip(row.x + 36, row.y + (32 - FONT_H) / 2, e->name,
                       zip_sel == i ? C_WHITE : RGB(34, 40, 54),
                       (Rect){row.x + 36, row.y, row.w - 200, row.h});
        {
            char sz[48];
            snprintf(sz, sizeof(sz), "%d Б  %s", (int)e->usize,
                     e->method == 0 ? "stored" : "deflate");
            draw_text_right(row.x + row.w - 12, row.y + (32 - FONT_H) / 2, sz,
                            zip_sel == i ? RGB(226, 238, 252) : RGB(132, 140, 156));
        }
    }
    gfx_set_clip((Rect){0, 0, (int)fb.width, (int)fb.height});
    if (zip_count == 0)
        draw_text_center(list.x + list.w / 2, list.y + list.h / 2, "Архив пуст или повреждён", RGB(150, 158, 172));

    fill_rect(foot, RGB(247, 249, 252));
    fill_rect((Rect){foot.x, foot.y, foot.w, 1}, RGB(226, 230, 238));
    draw_text_clip(foot.x + 16, foot.y + 18, zip_status[0] ? zip_status : "Выберите файл и нажмите «Извлечь»",
                   RGB(110, 118, 134), (Rect){foot.x + 16, foot.y, foot.w - 340, foot.h});
    ui_button((Rect){foot.x + foot.w - 300, foot.y + 12, 140, 34}, "Извлечь", 0, 1);
    ui_button((Rect){foot.x + foot.w - 150, foot.y + 12, 136, 34}, "Извлечь всё", 0, 0);
    UNUSED(w);
}

static void zip_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    Rect list = {c.x + 14, c.y + 84, c.w - 28, c.h - 150};
    int i;

    if (e->type == EV_WHEEL) {
        zip_scroll -= e->wheel * 40;
        if (zip_scroll > zip_count * 32 - list.h) zip_scroll = zip_count * 32 - list.h;
        if (zip_scroll < 0) zip_scroll = 0;
        wm_invalidate();
        return;
    }
    if (e->type != EV_MOUSE_DOWN) {
        if (e->type == EV_KEY && e->key == KEY_ENTER && zip_sel >= 0) {
            zip_extract(zip_sel, fs_root());
            wm_invalidate();
        }
        return;
    }
    for (i = 0; i < zip_count; i++) {
        Rect row = {list.x + 1, list.y + 1 + i * 32 - zip_scroll, list.w - 2, 32};
        if (pt_in(mx, my, row)) { zip_sel = i; wm_invalidate(); return; }
    }
    {
        Rect b1 = {c.x + c.w - 300, c.y + c.h - 46, 140, 34};
        Rect b2 = {c.x + c.w - 150, c.y + c.h - 46, 136, 34};
        if (pt_in(mx, my, b1) || pt_in(mx, my, b2)) {
            int k, ok = 0;
            for (k = 0; k < zip_count; k++) {
                if (zip_sel >= 0 && pt_in(mx, my, b1) && k != zip_sel) continue;
                zip_extract(k, fs_root());
                ok++;
            }
            if (ok) snprintf(zip_status, sizeof(zip_status), "Распаковано файлов: %d (в корень C:)", ok);
            wm_invalidate();
        }
    }
    UNUSED(w);
}

void zip_open_node(int node) {
    const char *data = fs_data(node);
    int size = fs_size(node);
    zip_len = size;
    zip_buf = (u8 *)data;
    strncpy(zip_title, fs_name(node), sizeof(zip_title) - 1);
    zip_node = node;
    zip_status[0] = 0;
    zip_scroll = 0;
    zip_sel = -1;
    zip_parse();
    wm_open(app_find("zip"), "Архив ZIP", -1, -1, 720, 540, zip_draw, zip_event, NULL);
    wm_invalidate();
}

/* ---------------- EXE inspector ---------------- */
static u8 *exe_data = NULL;
static int exe_len = 0;
static char exe_name[80];

static void exe_draw(Window *w, Rect c) {
    char buf[200];
    int is_pe = 0, machine = 0, subsystem = 0, sections = 0;
    u32 entry = 0;
    Rect card = {c.x + 24, c.y + 24, c.w - 48, c.h - 120};

    if (exe_len > 0x40 && exe_data[0] == 'M' && exe_data[1] == 'Z') {
        u32 lfanew = rd32(exe_data + 0x3C);
        if ((int)lfanew + 24 < exe_len && rd32(exe_data + lfanew) == 0x00004550UL) {
            is_pe = 1;
            machine = rd16(exe_data + lfanew + 4);
            sections = rd16(exe_data + lfanew + 6);
            entry = rd32(exe_data + lfanew + 40);
            subsystem = rd16(exe_data + lfanew + 24 + 68);
        }
    }

    fill_grad_v(c, RGB(18, 26, 44), RGB(8, 12, 22));
    icon_draw(ICON_DOWNLOAD, c.x + (c.w - 64) / 2, c.y + 26, 64);
    draw_text_center(c.x + c.w / 2, c.y + 100, exe_name, C_WHITE);
    draw_text_center(c.x + c.w / 2, c.y + 124, "Исполняемый файл Windows", RGB(150, 172, 202));

    fill_round_rect(card, 10, RGBA(255, 255, 255, 18));
    draw_round_rect(card, 10, RGBA(255, 255, 255, 40));
    draw_text(card.x + 18, card.y + 16, "Сведения о файле", C_WHITE);
    snprintf(buf, sizeof(buf), "Размер: %d Б", exe_len);
    draw_text(card.x + 18, card.y + 46, buf, RGB(190, 206, 226));
    if (is_pe) {
        snprintf(buf, sizeof(buf), "PE32 · machine 0x%04x · секций %d", machine, sections);
        draw_text(card.x + 18, card.y + 70, buf, RGB(190, 206, 226));
        snprintf(buf, sizeof(buf), "Точка входа RVA: 0x%x · подсистема %d", entry, subsystem);
        draw_text(card.x + 18, card.y + 94, buf, RGB(190, 206, 226));
    } else {
        draw_text(card.x + 18, card.y + 70, "Формат MZ/COM (DOS)", RGB(190, 206, 226));
    }
    draw_text(card.x + 18, card.y + 126,
              "YoroOS 0.1 умеет читать структуру EXE и открывать его ресурсы,", RGB(150, 168, 192));
    draw_text(card.x + 18, card.y + 148,
              "но не исполняет Win32-код: для этого нужен подсистема PE в 0.2.", RGB(150, 168, 192));

    ui_button((Rect){c.x + c.w / 2 - 170, c.y + c.h - 74, 160, 38}, "Открыть как текст", 0, 0);
    ui_button((Rect){c.x + c.w / 2 + 10, c.y + c.h - 74, 160, 38}, "Закрыть", 0, 1);
    UNUSED(w);
}

static void exe_event(Window *w, YoroEvent *e) {
    Rect c = wm_client(w);
    int mx = e->x + c.x, my = e->y + c.y;
    if (e->type != EV_MOUSE_DOWN) return;
    if (pt_in(mx, my, (Rect){c.x + c.w / 2 - 170, c.y + c.h - 74, 160, 38})) {
        int n = fs_lookup(fs_root(), exe_name);
        if (n >= 0) notepad_open_file(n);
        return;
    }
    if (pt_in(mx, my, (Rect){c.x + c.w / 2 + 10, c.y + c.h - 74, 160, 38})) {
        wm_close(w);
        return;
    }
}

void exe_open_node(int node) {
    exe_data = (u8 *)fs_data(node);
    exe_len = fs_size(node);
    strncpy(exe_name, fs_name(node), sizeof(exe_name) - 1);
    wm_open(app_find("exe"), "Запуск EXE", -1, -1, 560, 460, exe_draw, exe_event, NULL);
    wm_invalidate();
}
