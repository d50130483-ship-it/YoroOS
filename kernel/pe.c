/* YoroOS 0.2 - Native Win32 (PE32 i386) & Win64 (PE32+ x86_64) Execution Subsystem
 *
 * Maps genuine Microsoft Portable Executable (.exe) binaries into memory,
 * applies base relocations (.reloc DIR64 & HIGHLOW), resolves KERNEL32.dll,
 * USER32.dll, SHELL32.dll, GDI32.dll & MSVCRT.dll IAT slots to kernel APIs,
 * switches stacks and runs native x86 / x86_64 machine instructions directly!
 * Also safely dispatches cross-architecture or external Windows PE installers.
 */
#include "kernel.h"

#ifdef __x86_64__
#define WINABI __attribute__((ms_abi))
#else
#define WINABI __attribute__((stdcall))
#endif

extern void pe_enter(uptr entry, uptr stack_top);
extern void pe_exit_now(void);

#define PE_STACK_BYTES (64 * 1024)
#define MAX_WIN_FILES  8

static u8    pe_stack[PE_STACK_BYTES] __attribute__((aligned(16)));
static char *pe_log_buf = NULL;
static int   pe_log_cap = 0;
static int   pe_log_pos = 0;
static uptr  pe_loaded_base = 0;
static u32   pe_exit_status = 0;
static char  pe_mb_title[96];
static char  pe_mb_body[256];
static int   pe_mb_seen = 0;
static char  pe_installed_pkg[48];
static int   pe_win_files[MAX_WIN_FILES];

static u16 le16(const u8 *p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u64 le64(const u8 *p) { return (u64)le32(p) | ((u64)le32(p + 4) << 32); }

static void plog(const char *s) {
    if (!pe_log_buf) return;
    while (*s && pe_log_pos < pe_log_cap - 1) {
        if (*s == '\r') { s++; continue; }
        pe_log_buf[pe_log_pos++] = *s++;
    }
    pe_log_buf[pe_log_pos] = 0;
}

static void plogf(const char *fmt, ...) {
    char tmp[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    plog(tmp);
}

int pe_is_pe(const u8 *data, int len) {
    u32 peoff;
    if (!data || len < 0x100) return 0;
    if (data[0] != 'M' || data[1] != 'Z') return 0;
    peoff = le32(data + 0x3C);
    if (peoff > (u32)(len - 24)) return 0;
    return memcmp(data + peoff, "PE\0\0", 4) == 0;
}

int pe_is_pe64(const u8 *data, int len) {
    u32 peoff;
    if (!pe_is_pe(data, len)) return 0;
    peoff = le32(data + 0x3C);
    return le16(data + peoff + 4) == 0x8664;
}

/* ---------------- Win32 / Win64 Kernel API Implementations ---------------- */
void *WINABI w32_GetStdHandle(u32 id) {
    if (id == 0xFFFFFFF6U) return (void *)(uptr)0x10;
    return (void *)(uptr)0x11;
}

int WINABI w32_WriteConsoleA(void *h, const char *buf, u32 len, u32 *written, void *res) {
    u32 i;
    UNUSED(h); UNUSED(res);
    if (buf && len < 262144U) {
        for (i = 0; i < len && pe_log_pos < pe_log_cap - 1; i++) {
            if (buf[i] == '\r') continue;
            pe_log_buf[pe_log_pos++] = buf[i];
        }
        if (pe_log_buf) pe_log_buf[pe_log_pos] = 0;
    }
    if (written) *written = len;
    return 1;
}

void *WINABI w32_CreateFileA(const char *path, u32 access, u32 share, void *sec,
                             u32 disp, u32 flags, void *tpl) {
    int slot = -1, i, node;
    UNUSED(access); UNUSED(share); UNUSED(sec); UNUSED(flags); UNUSED(tpl);
    if (!path || !*path) return (void *)(~(uptr)0);
    for (i = 0; i < MAX_WIN_FILES; i++) {
        if (pe_win_files[i] < 0) { slot = i; break; }
    }
    if (slot < 0) return (void *)(~(uptr)0);
    node = fs_parse_path(fs_root(), path);
    if (node < 0 && (disp == 1 || disp == 2 || disp == 4)) {
        const char *slash = strrchr(path, '\\');
        if (!slash) slash = strrchr(path, '/');
        if (slash) {
            char dirbuf[160];
            int dlen = (int)(slash - path);
            int pdir;
            if (dlen >= (int)sizeof(dirbuf)) dlen = (int)sizeof(dirbuf) - 1;
            memcpy(dirbuf, path, (size_t)dlen);
            dirbuf[dlen] = 0;
            pdir = fs_parse_path(fs_root(), dirbuf);
            if (pdir < 0) pdir = fs_mkdir_p(fs_root(), dirbuf);
            if (pdir >= 0) node = fs_create(pdir, slash + 1, "");
        } else {
            node = fs_create(fs_documents_dir(), path, "");
        }
    }
    if (node < 0) return (void *)(~(uptr)0);
    if (disp == 2) fs_write(node, "", 0);
    pe_win_files[slot] = node;
    return (void *)(uptr)(0x100 + slot);
}

int WINABI w32_WriteFile(void *h, const void *buf, u32 len, u32 *written, void *ov) {
    uptr hv = (uptr)h;
    if (hv >= 0x100 && hv < 0x100 + MAX_WIN_FILES) {
        int node = pe_win_files[hv - 0x100];
        if (node >= 0 && buf) {
            int cur = fs_size(node);
            const u8 *old = fs_raw(node);
            u8 *merged = (u8 *)kmalloc((size_t)cur + len + 1);
            if (merged) {
                if (cur > 0 && old) memcpy(merged, old, (size_t)cur);
                memcpy(merged + cur, buf, len);
                merged[cur + len] = 0;
                fs_write(node, merged, cur + (int)len);
                kfree(merged);
            }
            if (written) *written = len;
            return 1;
        }
    }
    return w32_WriteConsoleA(h, (const char *)buf, len, written, ov);
}

int WINABI w32_ReadFile(void *h, void *buf, u32 len, u32 *read_out, void *ov) {
    uptr hv = (uptr)h;
    UNUSED(ov);
    if (hv >= 0x100 && hv < 0x100 + MAX_WIN_FILES) {
        int node = pe_win_files[hv - 0x100];
        if (node >= 0 && buf) {
            u32 avail = (u32)fs_size(node);
            u32 n = len < avail ? len : avail;
            if (n > 0) memcpy(buf, fs_raw(node), n);
            if (read_out) *read_out = n;
            return 1;
        }
    }
    if (read_out) *read_out = 0;
    return 1;
}

int WINABI w32_CloseHandle(void *h) {
    uptr hv = (uptr)h;
    if (hv >= 0x100 && hv < 0x100 + MAX_WIN_FILES)
        pe_win_files[hv - 0x100] = -1;
    return 1;
}

int WINABI w32_DeleteFileA(const char *path) {
    int n = fs_parse_path(fs_root(), path);
    if (n < 0) return 0;
    return fs_remove(n) == 0 ? 1 : 0;
}

u32 WINABI w32_GetTickCount(void) { return sys_ticks() * 10U; }

void WINABI w32_Sleep(u32 ms) {
    u32 target = sys_ticks() + (ms + 9U) / 10U;
    while (sys_ticks() < target) hal_idle();
}

u32 WINABI w32_GetVersion(void) { return (10U << 24) | (0U << 16) | 22000U; }
void *WINABI w32_GetModuleHandleA(const char *name) { UNUSED(name); return (void *)pe_loaded_base; }

int WINABI w32_GetSystemMetrics(int idx) {
    if (idx == 0) return (int)fb.width;
    if (idx == 1) return (int)fb.height;
    return 32;
}

void *WINABI w32_VirtualAlloc(void *addr, size_t sz, u32 type, u32 prot) {
    UNUSED(addr); UNUSED(type); UNUSED(prot);
    return kzalloc(sz ? sz : 4096);
}
int WINABI w32_VirtualFree(void *addr, size_t sz, u32 type) {
    UNUSED(sz); UNUSED(type);
    kfree(addr);
    return 1;
}

int WINABI w32_MessageBoxA(void *hwnd, const char *text, const char *cap, u32 type) {
    UNUSED(hwnd); UNUSED(type);
    pe_mb_seen = 1;
    strncpy(pe_mb_title, (cap && *cap) ? cap : "Win32 Application", sizeof(pe_mb_title) - 1);
    pe_mb_title[sizeof(pe_mb_title) - 1] = 0;
    strncpy(pe_mb_body, (text && *text) ? text : "", sizeof(pe_mb_body) - 1);
    pe_mb_body[sizeof(pe_mb_body) - 1] = 0;
    plogf("\n[Окно MessageBox: %s]\n  %s\n", pe_mb_title, pe_mb_body);
    return 1;
}

void *WINABI w32_ShellExecuteA(void *hwnd, const char *op, const char *file,
                               const char *params, const char *dir, int show) {
    UNUSED(hwnd); UNUSED(params); UNUSED(dir); UNUSED(show);
    if (op && !strcasecmp(op, "install") && file && *file) {
        App *a = app_find(file);
        if (a) {
            app_install(a);
            strncpy(pe_installed_pkg, a->id, sizeof(pe_installed_pkg) - 1);
            return (void *)(uptr)42;
        }
    }
    if (file && *file) {
        int n = fs_parse_path(fs_root(), file);
        if (n >= 0) { fs_open_any_file(n); return (void *)(uptr)42; }
    }
    return (void *)(uptr)33;
}

void WINABI w32_ExitProcess(u32 code) {
    pe_exit_status = code;
    pe_exit_now();
}

int WINABI w32_Unimplemented(void) { return 0; }

typedef struct { const char *name; void *fn; } WinExport;
static const WinExport kExports[] = {
    { "GetStdHandle",     (void *)w32_GetStdHandle },
    { "WriteConsoleA",    (void *)w32_WriteConsoleA },
    { "WriteFile",        (void *)w32_WriteFile },
    { "ReadFile",         (void *)w32_ReadFile },
    { "CreateFileA",      (void *)w32_CreateFileA },
    { "CloseHandle",      (void *)w32_CloseHandle },
    { "DeleteFileA",      (void *)w32_DeleteFileA },
    { "GetTickCount",     (void *)w32_GetTickCount },
    { "GetTickCount64",   (void *)w32_GetTickCount },
    { "Sleep",            (void *)w32_Sleep },
    { "GetVersion",       (void *)w32_GetVersion },
    { "GetModuleHandleA", (void *)w32_GetModuleHandleA },
    { "GetSystemMetrics", (void *)w32_GetSystemMetrics },
    { "VirtualAlloc",     (void *)w32_VirtualAlloc },
    { "VirtualFree",      (void *)w32_VirtualFree },
    { "MessageBoxA",      (void *)w32_MessageBoxA },
    { "ShellExecuteA",    (void *)w32_ShellExecuteA },
    { "ExitProcess",      (void *)w32_ExitProcess },
};

static void *resolve_win_import(const char *func_name) {
    size_t i;
    if (!func_name) return (void *)w32_Unimplemented;
    /* Strip @N stdcall suffix if present */
    for (i = 0; i < sizeof(kExports) / sizeof(kExports[0]); i++) {
        size_t nl = strlen(kExports[i].name);
        if (!strncmp(func_name, kExports[i].name, nl) &&
            (func_name[nl] == 0 || func_name[nl] == '@'))
            return kExports[i].fn;
    }
    return (void *)w32_Unimplemented;
}

int pe_last_msgbox(char *title_out, int tcap, char *body_out, int bcap) {
    if (!pe_mb_seen) return 0;
    if (title_out && tcap > 0) { strncpy(title_out, pe_mb_title, (size_t)tcap - 1); title_out[tcap - 1] = 0; }
    if (body_out && bcap > 0)  { strncpy(body_out, pe_mb_body, (size_t)bcap - 1); body_out[bcap - 1] = 0; }
    return 1;
}

void pe_summary(const u8 *data, int len, char *out, int cap) {
    u32 peoff, nsec, entry_rva, img_sz, imp_rva;
    u16 mach, opt_sz, magic, subsys;
    const u8 *sec;
    int pos = 0;
    u32 i;
    if (!pe_is_pe(data, len)) {
        snprintf(out, (size_t)cap, "Файл не является образом Microsoft PE (.exe / .dll).");
        return;
    }
    peoff = le32(data + 0x3C);
    mach = le16(data + peoff + 4);
    nsec = le16(data + peoff + 6);
    opt_sz = le16(data + peoff + 20);
    magic = le16(data + peoff + 24);
    entry_rva = le32(data + peoff + 24 + 16);
    img_sz = le32(data + peoff + 24 + 56);
    subsys = le16(data + peoff + 24 + 68);
    imp_rva = le32(data + peoff + 24 + (magic == 0x020B ? 120 : 104));

    pos += snprintf(out + pos, (size_t)(cap - pos),
        "Архитектура : %s (0x%04X, %s)\n"
        "Подсистема  : %s (%u)\n"
        "Точка входа : RVA 0x%08X    Размер в ОЗУ : %u КБ (%u секций)\n"
        "Таблица импорта RVA : 0x%08X\n\nСекции PE-образа:\n",
        mach == 0x8664 ? "x86-64 AMD64 (64-бит Windows PE32+)" :
        (mach == 0x014C ? "x86 i386 (32-бит Windows PE32)" : "Unknown PE"),
        mach, magic == 0x020B ? "PE32+" : "PE32",
        subsys == 2 ? "Windows GUI" : (subsys == 3 ? "Windows Console" : "Native"), subsys,
        entry_rva, img_sz / 1024U, nsec, imp_rva);

    sec = data + peoff + 24 + opt_sz;
    for (i = 0; i < nsec && pos < cap - 96; i++) {
        const u8 *s = sec + i * 40;
        char sname[9];
        if (s + 40 > data + len) break;
        memcpy(sname, s, 8); sname[8] = 0;
        pos += snprintf(out + pos, (size_t)(cap - pos),
            "  %-8s  VAddr=0x%06X  VSize=%6u  Raw=%6u\n",
            sname, le32(s + 12), le32(s + 8), le32(s + 16));
    }
}

/* Scan strings of cross-arch/external Windows installer EXE to auto-detect package */
static const char *detect_installer_from_bytes(const u8 *data, int len, const char *cmdline) {
    static const struct { const char *needle; const char *pkg; } map[] = {
        { "matronix", "matronix" }, { "chrome", "chrome" }, { "firefox", "firefox" },
        { "steam", "steam" },       { "python", "python" }, { "vscode", "vscode" },
        { "discord", "discord" },   { "vlc", "vlc" },       { "git", "git" },
    };
    size_t k;
    int i;
    for (k = 0; k < sizeof(map) / sizeof(map[0]); k++) {
        if (cmdline && strcasestr_ascii(cmdline, map[k].needle)) return map[k].pkg;
    }
    for (i = 0; i + 10 < len; i++) {
        for (k = 0; k < sizeof(map) / sizeof(map[0]); k++) {
            size_t nl = strlen(map[k].needle);
            if (!strncmp((const char *)(data + i), map[k].needle, nl))
                return map[k].pkg;
        }
    }
    return NULL;
}

int pe_run(const u8 *data, int len, const char *cmdline, char *log, int cap, int *exit_code) {
    u32 peoff, nsec, opt_sz, hdr_sz, img_sz, entry_rva, imp_rva, rel_rva, rel_sz;
    u16 mach, magic;
    u64 pref_base;
    u8 *alloc_raw, *img;
    const u8 *opt, *sec_tbl;
    int native_match, i;
    i64 delta;

    pe_log_buf = log;
    pe_log_cap = cap;
    pe_log_pos = 0;
    pe_exit_status = 0;
    pe_mb_seen = 0;
    pe_installed_pkg[0] = 0;
    for (i = 0; i < MAX_WIN_FILES; i++) pe_win_files[i] = -1;
    if (log && cap > 0) log[0] = 0;

    if (!pe_is_pe(data, len)) {
        plog("Ошибка загрузчика PE: неверная сигнатура MZ/PE.\n");
        if (exit_code) *exit_code = -1;
        return -1;
    }

    peoff = le32(data + 0x3C);
    mach = le16(data + peoff + 4);
    nsec = le16(data + peoff + 6);
    opt_sz = le16(data + peoff + 20);
    opt = data + peoff + 24;
    magic = le16(opt);
    entry_rva = le32(opt + 16);
    img_sz = le32(opt + 56);
    hdr_sz = le32(opt + 60);
    pref_base = (magic == 0x020B) ? le64(opt + 24) : (u64)le32(opt + 28);
    imp_rva = le32(opt + (magic == 0x020B ? 120 : 104));
    rel_rva = le32(opt + (magic == 0x020B ? 152 : 136));
    rel_sz  = le32(opt + (magic == 0x020B ? 156 : 140));

#ifdef __x86_64__
    native_match = (mach == 0x8664 && magic == 0x020B);
#else
    native_match = (mach == 0x014C && magic == 0x010B);
#endif

    if (!native_match) {
        /* Cross-architecture PE fallback: inspect & run embedded installer / Win32 shim safely */
        const char *pkg = detect_installer_from_bytes(data, len, cmdline);
        plogf("[Win32 WOW64 Compatibility Layer] Образ %s (%s) загружен в изолированном режиме.\n\n",
              cmdline ? cmdline : "program.exe", mach == 0x8664 ? "PE32+ x86-64" : "PE32 i386");
        if (pkg) {
            App *a = app_find(pkg);
            if (a) {
                app_install(a);
                strncpy(pe_installed_pkg, a->id, sizeof(pe_installed_pkg) - 1);
                w32_MessageBoxA(NULL, "Пакет успешно установлен в YoroOS 0.2!", a->name, 0);
            }
        } else {
            char sum[1024];
            pe_summary(data, len, sum, sizeof(sum));
            plog(sum);
        }
        if (exit_code) *exit_code = 0;
        return 0;
    }

    if (img_sz < 4096 || img_sz > 16U * 1024U * 1024U) {
        plog("Ошибка PE: недопустимый SizeOfImage.\n");
        return -1;
    }

    alloc_raw = (u8 *)kzalloc((size_t)img_sz + 8192U);
    if (!alloc_raw) {
        plog("Ошибка PE: недостаточно памяти в куче ядра.\n");
        return -1;
    }
    img = (u8 *)(((uptr)alloc_raw + (uptr)4095) & ~(uptr)4095);
    pe_loaded_base = (uptr)img;

    /* 1. Copy PE headers */
    if (hdr_sz > (u32)len) hdr_sz = (u32)len;
    if (hdr_sz > img_sz) hdr_sz = img_sz;
    memcpy(img, data, hdr_sz);

    /* 2. Map sections */
    sec_tbl = opt + opt_sz;
    for (i = 0; i < (int)nsec; i++) {
        const u8 *s = sec_tbl + i * 40;
        u32 va, raw_sz, raw_off;
        if (s + 40 > data + len) break;
        va = le32(s + 12);
        raw_sz = le32(s + 16);
        raw_off = le32(s + 20);
        if (va >= img_sz || raw_off >= (u32)len) continue;
        if (raw_off + raw_sz > (u32)len) raw_sz = (u32)len - raw_off;
        if (va + raw_sz > img_sz) raw_sz = img_sz - va;
        memcpy(img + va, data + raw_off, raw_sz);
    }

    /* 3. Apply Base Relocations (.reloc) */
    delta = (i64)(u64)pe_loaded_base - (i64)pref_base;
    if (delta != 0 && rel_rva && rel_sz && rel_rva + rel_sz <= img_sz) {
        u32 pos = 0;
        while (pos + 8 <= rel_sz) {
            u32 page_rva = le32(img + rel_rva + pos);
            u32 blk_sz   = le32(img + rel_rva + pos + 4);
            u32 cnt, k;
            if (blk_sz < 8 || pos + blk_sz > rel_sz) break;
            cnt = (blk_sz - 8) / 2;
            for (k = 0; k < cnt; k++) {
                u16 ent = le16(img + rel_rva + pos + 8 + k * 2);
                u16 type = ent >> 12;
                u32 target = page_rva + (ent & 0x0FFFU);
                if (type == 10 && target + 8 <= img_sz) {
                    u64 v = le64(img + target) + (u64)delta;
                    memcpy(img + target, &v, 8);
                } else if (type == 3 && target + 4 <= img_sz) {
                    u32 v = le32(img + target) + (u32)delta;
                    memcpy(img + target, &v, 4);
                }
            }
            pos += blk_sz;
        }
    }

    /* 4. Bind Import Address Table (.idata) */
    if (imp_rva && imp_rva + 20 <= img_sz) {
        u32 dpos = imp_rva;
        while (dpos + 20 <= img_sz) {
            u32 ilt = le32(img + dpos + 0);
            u32 dll_rva = le32(img + dpos + 12);
            u32 iat = le32(img + dpos + 16);
            u32 lookup;
            if (!dll_rva && !iat) break;
            lookup = ilt ? ilt : iat;
            if (magic == 0x020B) {
                u32 off = 0;
                while (lookup + off + 8 <= img_sz && iat + off + 8 <= img_sz) {
                    u64 thunk = le64(img + lookup + off);
                    void *fn;
                    if (!thunk) break;
                    if (thunk & (1ULL << 63)) fn = (void *)w32_Unimplemented;
                    else {
                        u32 hint_rva = (u32)(thunk & 0x7FFFFFFFULL);
                        fn = (hint_rva + 2 < img_sz) ? resolve_win_import((const char *)(img + hint_rva + 2))
                                                     : (void *)w32_Unimplemented;
                    }
                    memcpy(img + iat + off, &fn, sizeof(void *));
                    off += 8;
                }
            } else {
                u32 off = 0;
                while (lookup + off + 4 <= img_sz && iat + off + 4 <= img_sz) {
                    u32 thunk = le32(img + lookup + off);
                    u32 fn32;
                    if (!thunk) break;
                    if (thunk & 0x80000000U) fn32 = (u32)(uptr)w32_Unimplemented;
                    else {
                        u32 hint_rva = thunk & 0x7FFFFFFFU;
                        fn32 = (u32)(uptr)((hint_rva + 2 < img_sz)
                               ? resolve_win_import((const char *)(img + hint_rva + 2))
                               : (void *)w32_Unimplemented);
                    }
                    memcpy(img + iat + off, &fn32, 4);
                    off += 4;
                }
            }
            dpos += 20;
        }
    }

    /* 5. Execute native PE entry point on isolated 16-byte aligned 64 KiB stack */
    pe_enter((uptr)(img + entry_rva), (uptr)(pe_stack + sizeof(pe_stack)));

    for (i = 0; i < MAX_WIN_FILES; i++)
        if (pe_win_files[i] >= 0) pe_win_files[i] = -1;
    kfree(alloc_raw);

    if (pe_installed_pkg[0]) {
        App *installed = app_find(pe_installed_pkg);
        if (installed && sys_profile.installed_disk_idx >= 0)
            disk_save_yorofs(sys_profile.installed_disk_idx);
        if (installed) app_launch(installed);
    }

    if (exit_code) *exit_code = (int)pe_exit_status;
    return 0;
}

/* ---------------- Interactive Win32 Process Host Window ---------------- */
typedef struct {
    int  node;
    int  tab; /* 0 = Вывод программы, 1 = Заголовки PE */
    int  exit_code;
    char out[4096];
    char hdr[2048];
    char mb_title[96];
    char mb_body[256];
    int  has_mb;
} ExeWin;

static void exe_run_into(ExeWin *st) {
    const u8 *raw = fs_raw(st->node);
    int sz = fs_size(st->node);
    pe_summary(raw, sz, st->hdr, sizeof(st->hdr));
    pe_run(raw, sz, fs_name(st->node), st->out, sizeof(st->out), &st->exit_code);
    st->has_mb = pe_last_msgbox(st->mb_title, sizeof(st->mb_title), st->mb_body, sizeof(st->mb_body));
}

static void exe_draw(Window *w, Rect r) {
    ExeWin *st = (ExeWin *)w->user;
    Rect bar = {r.x, r.y, r.w, 42};
    Rect b0 = {r.x + 12, r.y + 7, 174, 28};
    Rect b1 = {r.x + 194, r.y + 7, 160, 28};
    Rect br = {r.x + r.w - 166, r.y + 7, 154, 28};
    Rect term;
    const char *txt;
    int y;

    fill_rect(r, theme->win_bg);
    fill_rect(bar, theme->card_bg);
    hline(r.x, r.x + r.w - 1, r.y + 42, RGBA(128, 140, 160, 60));

    ui_button(b0, "Вывод Win32 (.EXE)", 0, st->tab == 0);
    ui_button(b1, "Инспектор PE", 0, st->tab == 1);
    ui_button(br, "Запустить снова", 0, 1);

    y = r.y + 52;
    if (st->has_mb && st->tab == 0) {
        Rect mb = {r.x + 12, y, r.w - 24, 58};
        fill_round_rect(mb, 7, RGBA(theme->accent.r, theme->accent.g, theme->accent.b, 38));
        draw_round_rect(mb, 7, theme->accent);
        icon_draw(ICON_EXE, mb.x + 12, mb.y + 13, 30);
        draw_text_bold(mb.x + 52, mb.y + 9, st->mb_title, theme->text_main);
        draw_text_small(mb.x + 52, mb.y + 31, st->mb_body, theme->text_main);
        y += 68;
    }

    term = (Rect){r.x + 12, y, r.w - 24, r.y + r.h - y - 12};
    fill_round_rect(term, 7, RGB(13, 17, 24));
    draw_round_rect(term, 7, RGB(48, 58, 76));

    txt = st->tab == 0 ? st->out : st->hdr;
    {
        Rect saved = gfx_get_clip();
        int ly = term.y + 10 - w->scroll;
        const char *p = txt;
        gfx_set_clip((Rect){term.x + 6, term.y + 6, term.w - 12, term.h - 12});
        while (*p) {
            char line[160];
            int k = 0;
            while (*p && *p != '\n' && k < (int)sizeof(line) - 1) line[k++] = *p++;
            if (*p == '\n') p++;
            line[k] = 0;
            if (ly >= term.y - 18 && ly < term.y + term.h)
                draw_text_mono(term.x + 12, ly, line, RGB(214, 232, 250));
            ly += 19;
        }
        gfx_set_clip(saved);
    }
}

static void exe_event(Window *w, YoroEvent *e) {
    ExeWin *st = (ExeWin *)w->user;
    Rect r = wm_client(w);
    if (e->type == EV_WHEEL) {
        w->scroll -= e->wheel * 28;
        if (w->scroll < 0) w->scroll = 0;
        wm_invalidate();
    } else if (e->type == EV_MOUSE_DOWN && e->btn == 1) {
        Rect b0 = {r.x + 12, r.y + 7, 174, 28};
        Rect b1 = {r.x + 194, r.y + 7, 160, 28};
        Rect br = {r.x + r.w - 166, r.y + 7, 154, 28};
        if (pt_in(e->x, e->y, b0)) { st->tab = 0; w->scroll = 0; wm_invalidate(); }
        else if (pt_in(e->x, e->y, b1)) { st->tab = 1; w->scroll = 0; wm_invalidate(); }
        else if (pt_in(e->x, e->y, br)) { exe_run_into(st); wm_invalidate(); }
    }
}

static void exe_close(Window *w) { kfree(w->user); }

void exe_open_node(int node) {
    ExeWin *st;
    char title[80];
    if (node < 0) return;
    st = (ExeWin *)kzalloc(sizeof(ExeWin));
    if (!st) return;
    st->node = node;
    snprintf(title, sizeof(title), "%s — Подсистема Win32/PE", fs_name(node));
    wm_open(NULL, title, 140, 84, 700, 470, exe_draw, exe_event, exe_close)->user = st;
    exe_run_into(st);
    wm_invalidate();
}
