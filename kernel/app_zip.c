/* YoroOS 0.2 - YoroZIP Full PKZIP Archive Reader, Deflate Extractor & ZIP Creator */
#include "kernel.h"

#define MAX_ZIP_ENTRIES 64

typedef struct {
    char name[96];
    u16  method;
    u32  comp_size;
    u32  uncomp_size;
    u32  crc32;
    u32  data_off;
} ZipEntry;

typedef struct {
    int      node;
    int      count;
    int      sel;
    u32      last_click;
    int      last_idx;
    ZipEntry entries[MAX_ZIP_ENTRIES];
} ZipWin;

static u16 zl16(const u8 *p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static u32 zl32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void zw16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void zw32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int zip_parse_entries(const u8 *buf, int len, ZipEntry *out, int max_out) {
    int pos = 0, n = 0;
    if (!buf || len < 30) return 0;

    /* Walk local file headers PK\x03\x04 cleanly */
    while (pos + 30 <= len && n < max_out) {
        u32 sig = zl32(buf + pos);
        u16 method, fnlen, exlen;
        u32 csz, usz, crc, doff;
        if (sig != 0x04034B50U) break;
        method = zl16(buf + pos + 8);
        crc    = zl32(buf + pos + 14);
        csz    = zl32(buf + pos + 18);
        usz    = zl32(buf + pos + 22);
        fnlen  = zl16(buf + pos + 26);
        exlen  = zl16(buf + pos + 28);
        doff   = (u32)pos + 30U + fnlen + exlen;
        if (doff > (u32)len) break;

        if (fnlen > 0 && buf[pos + 30 + fnlen - 1] != '/') {
            int cp = fnlen < (int)sizeof(out[n].name) - 1 ? fnlen : (int)sizeof(out[n].name) - 1;
            memcpy(out[n].name, buf + pos + 30, (size_t)cp);
            out[n].name[cp] = 0;
            out[n].method = method;
            out[n].comp_size = csz;
            out[n].uncomp_size = usz;
            out[n].crc32 = crc;
            out[n].data_off = doff;
            n++;
        }
        pos = (int)(doff + csz);
    }
    return n;
}

static int zip_extract_entry(const u8 *buf, int len, const ZipEntry *e, int dest_dir) {
    u8 *out;
    int target_dir = dest_dir;
    const char *base = e->name;
    const char *slash;
    int got;

    if (e->uncomp_size > FS_DATA_MAX || (int)(e->data_off + e->comp_size) > len) return -1;

    /* Create intermediate subdirectories inside archive if present */
    slash = strrchr(e->name, '/');
    if (slash) {
        char sub[96];
        int sl = (int)(slash - e->name);
        if (sl > 0 && sl < (int)sizeof(sub)) {
            memcpy(sub, e->name, (size_t)sl);
            sub[sl] = 0;
            target_dir = fs_mkdir_p(dest_dir, sub);
        }
        base = slash + 1;
    }

    out = (u8 *)kmalloc((size_t)e->uncomp_size + 16U);
    if (!out) return -1;

    if (e->method == 0) {
        memcpy(out, buf + e->data_off, e->uncomp_size);
        got = (int)e->uncomp_size;
    } else if (e->method == 8) {
        got = inflate_raw(buf + e->data_off, (int)e->comp_size, out, (int)e->uncomp_size + 8);
        if (got < 0) { kfree(out); return -1; }
    } else {
        kfree(out);
        return -1;
    }

    got = fs_create_bin(target_dir, base, out, got);
    kfree(out);
    desktop_rebuild();
    return got;
}

int zip_extract_all_to(const u8 *buf, int len, int dest_dir) {
    ZipEntry *ents = (ZipEntry *)kzalloc(sizeof(ZipEntry) * MAX_ZIP_ENTRIES);
    int n, i, ok = 0;
    if (!ents) return -1;
    n = zip_parse_entries(buf, len, ents, MAX_ZIP_ENTRIES);
    for (i = 0; i < n; i++) {
        if (zip_extract_entry(buf, len, &ents[i], dest_dir) >= 0) ok++;
    }
    kfree(ents);
    return ok;
}

/* Standards-compliant PKZIP creator from any YoroFS file or folder! */
typedef struct {
    u8    *buf;
    size_t cap, pos;
    struct { char name[96]; u32 crc, sz, lhdr_off; } cd[48];
    int    ncd;
} ZipWriter;

static void zw_collect(ZipWriter *zw, int node, const char *prefix) {
    char rel[96];
    if (zw->ncd >= 48 || node < 0) return;
    if (prefix && *prefix) snprintf(rel, sizeof(rel), "%s/%s", prefix, fs_name(node));
    else strncpy(rel, fs_name(node), sizeof(rel) - 1);

    if (fs_is_dir(node)) {
        int i, cnt = fs_count(node);
        for (i = 0; i < cnt; i++) zw_collect(zw, fs_child(node, i), rel);
        return;
    }

    {
        const u8 *raw = fs_raw(node);
        u32 sz = (u32)fs_size(node);
        u16 fnl = (u16)strlen(rel);
        u32 crc = raw && sz ? crc32_calc(raw, sz) : 0U;
        u8 *lh;
        if (zw->pos + 30U + fnl + sz + 2048U > zw->cap) return;
        lh = zw->buf + zw->pos;
        zw->cd[zw->ncd].lhdr_off = (u32)zw->pos;
        zw->cd[zw->ncd].crc = crc;
        zw->cd[zw->ncd].sz = sz;
        strncpy(zw->cd[zw->ncd].name, rel, 95);
        zw->ncd++;

        zw32(lh + 0,  0x04034B50U);
        zw16(lh + 4,  20);
        zw16(lh + 6,  0);
        zw16(lh + 8,  0); /* STORED */
        zw16(lh + 10, 0x6000);
        zw16(lh + 12, 0x5557);
        zw32(lh + 14, crc);
        zw32(lh + 18, sz);
        zw32(lh + 22, sz);
        zw16(lh + 26, fnl);
        zw16(lh + 28, 0);
        memcpy(lh + 30, rel, fnl);
        if (sz > 0 && raw) memcpy(lh + 30 + fnl, raw, sz);
        zw->pos += 30U + fnl + sz;
    }
}

int zip_create_from_node(int src_node, int dest_dir, char *out_name, int cap) {
    ZipWriter *zw;
    u32 cd_start, cd_size;
    int i, created;
    char zname[64];

    if (src_node < 0 || dest_dir < 0) return -1;
    zw = (ZipWriter *)kzalloc(sizeof(ZipWriter));
    if (!zw) return -1;
    zw->cap = 384U * 1024U;
    zw->buf = (u8 *)kzalloc(zw->cap);
    if (!zw->buf) { kfree(zw); return -1; }

    zw_collect(zw, src_node, "");
    cd_start = (u32)zw->pos;
    for (i = 0; i < zw->ncd; i++) {
        u16 fnl = (u16)strlen(zw->cd[i].name);
        u8 *c = zw->buf + zw->pos;
        zw32(c + 0,  0x02014B50U);
        zw16(c + 4,  20); zw16(c + 6, 20);
        zw32(c + 16, zw->cd[i].crc);
        zw32(c + 20, zw->cd[i].sz);
        zw32(c + 24, zw->cd[i].sz);
        zw16(c + 28, fnl);
        zw32(c + 42, zw->cd[i].lhdr_off);
        memcpy(c + 46, zw->cd[i].name, fnl);
        zw->pos += 46U + fnl;
    }
    cd_size = (u32)zw->pos - cd_start;
    {
        u8 *eo = zw->buf + zw->pos;
        zw32(eo + 0,  0x06054B50U);
        zw16(eo + 8,  (u16)zw->ncd);
        zw16(eo + 10, (u16)zw->ncd);
        zw32(eo + 12, cd_size);
        zw32(eo + 16, cd_start);
        zw->pos += 22U;
    }

    snprintf(zname, sizeof(zname), "%s.zip", fs_name(src_node));
    if (out_name && cap > 0) { strncpy(out_name, zname, (size_t)cap - 1); out_name[cap - 1] = 0; }
    created = fs_create_bin(dest_dir, zname, zw->buf, (int)zw->pos);
    kfree(zw->buf);
    kfree(zw);
    toast("Архив .ZIP успешно создан!");
    return created;
}

static void zip_draw(Window *w, Rect r) {
    ZipWin *st = (ZipWin *)w->user;
    int mx = mouse_get_x(), my = mouse_get_y();
    Rect bar = {r.x, r.y, r.w, 44};
    int i, y;

    fill_rect(r, theme->win_bg);
    fill_rect(bar, theme->card_bg);
    ui_button((Rect){r.x + 10, bar.y + 7, 232, 30}, "Распаковать всё в Загрузки",
              pt_in(mx, my, (Rect){r.x + 10, bar.y + 7, 232, 30}), 1);
    ui_button((Rect){r.x + 250, bar.y + 7, 234, 30}, "Открыть / Запустить файл",
              pt_in(mx, my, (Rect){r.x + 250, bar.y + 7, 234, 30}), 0);
    ui_button((Rect){r.x + 492, bar.y + 7, 224, 30}, "Распаковать на Раб. стол",
              pt_in(mx, my, (Rect){r.x + 492, bar.y + 7, 224, 30}), 0);
    hline(r.x, r.x + r.w - 1, bar.y + 43, RGBA(130, 145, 170, 60));

    fill_rect_alpha((Rect){r.x, r.y + 44, r.w, 26}, RGBA(130, 145, 170, 26));
    draw_text_small(r.x + 42, r.y + 49, "Файл в архиве", theme->text_muted);
    draw_text_small(r.x + r.w - 280, r.y + 49, "Исходный", theme->text_muted);
    draw_text_small(r.x + r.w - 190, r.y + 49, "Сжатый", theme->text_muted);
    draw_text_small(r.x + r.w - 104, r.y + 49, "CRC32", theme->text_muted);

    y = r.y + 74 - w->scroll;
    for (i = 0; i < st->count; i++) {
        Rect row = {r.x + 8, y, r.w - 16, 32};
        const char *dot = strrchr(st->entries[i].name, '.');
        char b1[24], b2[24], b3[16];
        if (y + 32 >= r.y + 70 && y < r.y + r.h - 8) {
            if (st->sel == i) {
                fill_round_rect_alpha(row, 5, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 68));
                draw_round_rect(row, 5, theme->accent);
            } else if (pt_in(mx, my, row)) {
                fill_round_rect_alpha(row, 5, RGBA(130, 145, 170, 30));
            }
            icon_draw((dot && !strcasecmp(dot, ".exe")) ? ICON_EXE : ICON_TEXTFILE, row.x + 8, row.y + 4, 24);
            draw_text_clipped(row.x + 38, row.y + 7, r.w - 330, st->entries[i].name, theme->text_main);
            snprintf(b1, sizeof(b1), "%u Б", st->entries[i].uncomp_size);
            snprintf(b2, sizeof(b2), "%u Б", st->entries[i].comp_size);
            snprintf(b3, sizeof(b3), "%08X", st->entries[i].crc32);
            draw_text_small(r.x + r.w - 280, row.y + 8, b1, theme->text_muted);
            draw_text_small(r.x + r.w - 190, row.y + 8, b2, theme->text_muted);
            draw_text_mono(r.x + r.w - 104, row.y + 7, b3, theme->accent);
        }
        y += 34;
    }
}

static void zip_open_selected(ZipWin *st) {
    int out_node;
    if (st->sel < 0 || st->sel >= st->count) return;
    out_node = zip_extract_entry(fs_raw(st->node), fs_size(st->node), &st->entries[st->sel], fs_downloads_dir());
    if (out_node >= 0) fs_open_any_file(out_node);
}

static void zip_event(Window *w, YoroEvent *e) {
    ZipWin *st = (ZipWin *)w->user;
    Rect r = wm_client(w);
    if (e->type == EV_WHEEL) {
        w->scroll -= e->wheel * 32;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
    } else if (e->type == EV_KEY && e->key == KEY_ENTER) {
        zip_open_selected(st);
    } else if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        if (pt_in(e->x, e->y, (Rect){r.x + 10, r.y + 7, 232, 30})) {
            char sub[96], p[160];
            int dest;
            snprintf(sub, sizeof(sub), "Extracted_%s", fs_name(st->node));
            dest = fs_mkdir(fs_downloads_dir(), sub);
            zip_extract_all_to(fs_raw(st->node), fs_size(st->node), dest);
            fs_path(dest, p, sizeof(p));
            toast("Архив полностью распакован в Загрузки!");
            explorer_open_path(p);
            return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 250, r.y + 7, 234, 30})) {
            zip_open_selected(st);
            return;
        }
        if (pt_in(e->x, e->y, (Rect){r.x + 492, r.y + 7, 224, 30})) {
            zip_extract_all_to(fs_raw(st->node), fs_size(st->node), fs_desktop_dir());
            toast("Файлы архива распакованы прямо на Рабочий стол!");
            return;
        }
        if (e->y >= r.y + 70) {
            int idx = (e->y - (r.y + 74 - w->scroll)) / 34;
            if (idx >= 0 && idx < st->count) {
                u32 now = sys_ticks();
                st->sel = idx;
                if (st->last_idx == idx && now - st->last_click < 45) {
                    st->last_click = 0;
                    zip_open_selected(st);
                } else {
                    st->last_idx = idx;
                    st->last_click = now;
                }
                wm_invalidate();
            }
        }
    }
}

static void zip_close(Window *w) { kfree(w->user); }

void zip_open_node(int node) {
    ZipWin *st;
    char title[80];
    if (node < 0) return;
    st = (ZipWin *)kzalloc(sizeof(ZipWin));
    if (!st) return;
    st->node = node;
    st->count = zip_parse_entries(fs_raw(node), fs_size(node), st->entries, MAX_ZIP_ENTRIES);
    st->sel = st->count > 0 ? 0 : -1;
    snprintf(title, sizeof(title), "Архиватор YoroZIP — %s (%d файлов)", fs_name(node), st->count);
    wm_open(NULL, title, -1, -1, 740, 440, zip_draw, zip_event, zip_close)->user = st;
}
