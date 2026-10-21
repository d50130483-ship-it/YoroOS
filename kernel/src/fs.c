/* YoroFS — простая ФС на диске:
   LBA 4096      — суперблок {магия, next_free, count}
   LBA 4097..4112— таблица файлов (16 секторов * 8 записей = 128 файлов, 64 байта на запись)
   LBA 4160..    — данные */
#include "fs.h"
#include "hw.h"
#include "gfx.h"

#define FS_LBA 4096
#define ENT_SECTORS 16
#define DATA_LBA (FS_LBA + 64)
#define FS_MAGIC 0x31534659u /* 'YFS1' */

struct fs_entry {
    char name[FS_MAX_NAME];
    u32 start;
    u32 size;
    u8 pad[8];
};

struct fs_sb {
    u32 magic;
    u32 next_free;
    u32 count;
    u32 pad;
};

static struct fs_sb sb;
static struct fs_entry entries[FS_MAX_FILES];
static int mounted = 0;

char g_user[24] = "user";
char g_serial[16] = "YORO-000000";

/* ---- чтение/запись settings.dat ---- */
static void parse_settings(const char *txt) {
    char line[64];
    int i = 0;
    while (txt[i]) {
        int j = 0;
        while (txt[i] && txt[i] != '\n' && j < 63) line[j++] = txt[i++];
        line[j] = 0;
        if (txt[i] == '\n') i++;
        if (!strncmp(line, "user=", 5)) strcpy(g_user, line + 5, sizeof(g_user));
        else if (!strncmp(line, "serial=", 7)) strcpy(g_serial, line + 7, sizeof(g_serial));
        else if (!strncmp(line, "theme=", 6)) { int t = line[6] - '0'; if (t >= 0 && t < 5) g_theme = t; }
    }
}

void settings_save(void) {
    char buf[128];
    int p = 0;
    strcpy(buf + p, "user=", 8); p += 5;
    strcpy(buf + p, g_user, 24); p += strlen(g_user);
    buf[p++] = '\n';
    strcpy(buf + p, "serial=", 8); p += 7;
    strcpy(buf + p, g_serial, 16); p += strlen(g_serial);
    buf[p++] = '\n';
    strcpy(buf + p, "theme=", 8); p += 6;
    buf[p++] = (char)('0' + g_theme);
    buf[p++] = '\n';
    buf[p] = 0;
    fs_write("settings.dat", buf, p);
}

void settings_load(void) {
    char buf[256];
    if (fs_read("settings.dat", buf, sizeof(buf) - 1) > 0) {
        buf[sizeof(buf) - 1] = 0;
        parse_settings(buf);
    }
}

/* ---- ядро ФС ---- */
static u8 fs_sec[512];

void fs_init(void) {
    ata_read(FS_LBA, 1, fs_sec);
    memcpy(&sb, fs_sec, sizeof(sb));
    if (sb.magic == FS_MAGIC) {
        ata_read(FS_LBA + 1, ENT_SECTORS, entries);
        mounted = 1;
    }
}

int fs_valid(void) { return mounted; }

void fs_format(void) {
    memset(&sb, 0, sizeof(sb));
    sb.magic = FS_MAGIC;
    sb.next_free = DATA_LBA;
    memset(entries, 0, sizeof(entries));
    memcpy(fs_sec, &sb, sizeof(sb));
    ata_write(FS_LBA, 1, fs_sec);
    ata_write(FS_LBA + 1, ENT_SECTORS, entries);
    mounted = 1;
}

static void flush_sb(void) {
    memcpy(fs_sec, &sb, sizeof(sb));
    ata_write(FS_LBA, 1, fs_sec);
}
static void flush_entry(int i) {
    int sec = i / 8;
    ata_write(FS_LBA + 1 + sec, 1, entries + sec * 8);
}

int fs_count(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (entries[i].name[0]) n++;
    return n;
}

const char *fs_name(int i) { return entries[i].name[0] ? entries[i].name : ""; }
int fs_size(int i) { return entries[i].name[0] ? (int)entries[i].size : 0; }

int fs_find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (entries[i].name[0] && !strcmp(entries[i].name, name)) return i;
    return -1;
}

int fs_create(const char *name, const void *data, int len) {
    if (!mounted || fs_find(name) >= 0) return 0;
    int slot = -1;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (!entries[i].name[0]) { slot = i; break; }
    if (slot < 0) return 0;
    u32 sectors = ((u32)len + 511) / 512;
    if (sectors == 0) sectors = 1;
    if (sb.next_free + sectors > 32768) return 0;
    static u8 zero[512];
    for (u32 s = 0; s < sectors; s++) {
        const u8 *src = (const u8 *)data + s * 512;
        u8 tmp[512];
        int in_chunk = len - (int)(s * 512);
        if (in_chunk >= 512) ata_write(sb.next_free + s, 1, src);
        else {
            memcpy(tmp, src, in_chunk > 0 ? in_chunk : 0);
            memset(tmp + (in_chunk > 0 ? in_chunk : 0), 0, 512 - (in_chunk > 0 ? in_chunk : 0));
            ata_write(sb.next_free + s, 1, tmp);
        }
    }
    struct fs_entry *e = &entries[slot];
    strcpy(e->name, name, FS_MAX_NAME);
    e->start = sb.next_free;
    e->size = len;
    sb.next_free += sectors;
    flush_entry(slot);
    flush_sb();
    return 1;
}

int fs_write(const char *name, const void *data, int len) {
    int i = fs_find(name);
    if (i < 0) return fs_create(name, data, len);
    struct fs_entry *e = &entries[i];
    u32 newsec = ((u32)len + 511) / 512;
    if (newsec == 0) newsec = 1;
    u32 oldsec = (e->size + 511) / 512;
    if (oldsec == 0) oldsec = 1;
    if (newsec <= oldsec) {
        static u8 tmp[512];
        for (u32 s = 0; s < newsec; s++) {
            int in_chunk = len - (int)(s * 512);
            int cp = in_chunk > 512 ? 512 : (in_chunk > 0 ? in_chunk : 0);
            memcpy(tmp, (const u8 *)data + s * 512, cp);
            if (cp < 512) memset(tmp + cp, 0, 512 - cp);
            ata_write(e->start + s, 1, tmp);
        }
        e->size = len;
        flush_entry(i);
        return 1;
    }
    /* не влезает — пересоздаём в конце */
    u32 old_start = e->start;
    char nm[FS_MAX_NAME];
    strcpy(nm, name, FS_MAX_NAME);
    entries[i].name[0] = 0;
    flush_entry(i);
    if (!fs_create(nm, data, len)) {
        /* восстановить запись */
        strcpy(entries[i].name, nm, FS_MAX_NAME);
        entries[i].start = old_start;
        flush_entry(i);
        return 0;
    }
    return 1;
}

int fs_read(const char *name, void *buf, int max) {
    int i = fs_find(name);
    if (i < 0) return -1;
    struct fs_entry *e = &entries[i];
    int n = (int)e->size < max ? (int)e->size : max;
    static u8 sec[512];
    u8 *p = buf;
    for (u32 s = 0; s * 512 < (u32)n; s++) {
        ata_read(e->start + s, 1, sec);
        int cp = n - (int)(s * 512);
        if (cp > 512) cp = 512;
        memcpy(p, sec, cp);
        p += cp;
    }
    if (n < max) ((char *)buf)[n] = 0;
    return n;
}

int fs_delete(const char *name) {
    int i = fs_find(name);
    if (i < 0) return 0;
    entries[i].name[0] = 0;
    flush_entry(i);
    sb.count--;
    flush_sb();
    return 1;
}

void fs_wipe(void) {
    sb.magic = 0;
    flush_sb();
    mounted = 0;
}
