/* YoroOS - Win32 PE32 subsystem: real image loader + import resolution.
 *
 * This is not a "file inspector": the image is mapped at its preferred base,
 * every section is copied with the right virtual/raw sizes, base relocations
 * are applied when the arena forces a different address, imports are resolved
 * by name against the YoroOS Win32 stub table and finally the entry point is
 * executed on its own stack.
 *
 * The arena sits at 4 MiB (the usual ImageBase of Windows binaries), so most
 * images load with a zero delta and need no relocation at all.
 */
#include "kernel.h"

#define PE_ARENA_BASE 0x00400000u
#define PE_ARENA_SIZE 0x00400000u          /* 4 MiB */

#define PE_MAX_IMPORTS 96
#define PE_STUB_STACK  (64 * 1024)

extern void pe_enter(u32 entry, u32 stack_top);
extern void pe_exit_now(void);

static u8  pe_stack[PE_STUB_STACK];
static char *pe_log;
static int   pe_logcap;
static int   pe_logpos;

static u32 pe_img_base;
static u32 pe_exit_code;
static int pe_ticks_start;

/* ------------------------------------------------------------------ */
/* little helpers                                                      */
/* ------------------------------------------------------------------ */
static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void log_puts(const char *s) {
    if (!pe_log) return;
    while (*s && pe_logpos < pe_logcap - 1) pe_log[pe_logpos++] = *s++;
    pe_log[pe_logpos] = 0;
}

static void log_write(const char *s, int n) {
    int i;
    if (!pe_log) return;
    for (i = 0; i < n && pe_logpos < pe_logcap - 1; i++) {
        char c = s[i];
        if (c == '\n' && (pe_logpos == 0 || pe_log[pe_logpos - 1] != '\r')) {
            pe_log[pe_logpos++] = '\r';
            if (pe_logpos >= pe_logcap - 1) break;
        }
        pe_log[pe_logpos++] = c;
    }
    pe_log[pe_logpos] = 0;
}

static void log_hex(const char *pre, u32 v, const char *post) {
    char buf[12];
    int i;
    log_puts(pre);
    for (i = 7; i >= 0; i--) buf[7 - i] = "0123456789abcdef"[(v >> (i * 4)) & 0xF];
    buf[8] = 0;
    i = 0;
    while (i < 7 && buf[i] == '0') i++;
    log_puts(buf + i);
    if (post) log_puts(post);
}

static void log_num(const char *pre, u32 v, const char *post) {
    char buf[48];
    int n = 0, i, started = 0;
    char digits[16];
    log_puts(pre);
    if (!v) digits[n++] = '0';
    while (v) { digits[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = n - 1; i >= 0; i--) { buf[started++] = digits[i]; }
    buf[started] = 0;
    log_puts(buf);
    if (post) log_puts(post);
}

/* ------------------------------------------------------------------ */
/* Win32 stubs (stdcall: the callee pops the arguments)                */
/* ------------------------------------------------------------------ */
#define API __attribute__((stdcall))

typedef struct {
    const char *dll;
    const char *name;
    void *fn;
} PeStub;

u32 API pe_GetStdHandle(u32 id) {
    UNUSED(id);
    return 1;                                   /* pseudo handle */
}

int API pe_WriteConsoleA(u32 h, const char *buf, u32 len, u32 *written, void *res) {
    UNUSED(h); UNUSED(res);
    if (buf && len < 100000u) log_write(buf, (int)len);
    if (written) *written = len;
    return 1;
}

int API pe_WriteFile(u32 h, const char *buf, u32 len, u32 *written, void *ov) {
    return pe_WriteConsoleA(h, buf, len, written, ov);
}

int API pe_ReadConsoleA(u32 h, char *buf, u32 len, u32 *read, void *res) {
    UNUSED(h); UNUSED(res);
    if (buf && len) { buf[0] = '\r'; if (len > 1) buf[1] = '\n'; }
    if (read) *read = len > 1 ? 2 : len;
    return 1;
}

u32 API pe_GetTickCount(void) { return sys_ticks() * 10u; }

void API pe_Sleep(u32 ms) {
    u32 end = sys_ticks() + (ms + 9) / 10;
    while (sys_ticks() < end) __asm__ volatile("sti; hlt");
}

int API pe_MessageBoxA(u32 hwnd, const char *text, const char *cap, u32 type) {
    UNUSED(hwnd); UNUSED(type);
    log_puts("[MessageBox] ");
    if (cap && *cap) { log_write(cap, (int)strlen(cap)); log_puts(": "); }
    if (text) log_write(text, (int)strlen(text));
    log_puts("\r\n");
    return 1;
}

const char *API pe_GetCommandLineA(void) {
    return "C:\\Users\\Yoro\\Downloads\\hello.exe";
}

u32 API pe_GetModuleHandleA(const char *name) {
    UNUSED(name);
    return pe_img_base;
}

u32 API pe_GetVersion(void) { return 0x0A000000u; }
u32 API pe_GetLastError(void) { return 0; }
void API pe_SetLastError(u32 e) { UNUSED(e); }

void API pe_ExitProcess(u32 code) {
    pe_exit_code = (int)code;
    pe_exit_now();
}

static const PeStub pe_stubs[] = {
    { "kernel32.dll", "GetStdHandle",     (void *)&pe_GetStdHandle },
    { "kernel32.dll", "WriteConsoleA",    (void *)&pe_WriteConsoleA },
    { "kernel32.dll", "WriteFile",        (void *)&pe_WriteFile },
    { "kernel32.dll", "ReadConsoleA",     (void *)&pe_ReadConsoleA },
    { "kernel32.dll", "GetTickCount",     (void *)&pe_GetTickCount },
    { "kernel32.dll", "Sleep",            (void *)&pe_Sleep },
    { "kernel32.dll", "GetCommandLineA",  (void *)&pe_GetCommandLineA },
    { "kernel32.dll", "GetModuleHandleA", (void *)&pe_GetModuleHandleA },
    { "kernel32.dll", "GetModuleHandleW", (void *)&pe_GetModuleHandleA },
    { "kernel32.dll", "GetVersion",       (void *)&pe_GetVersion },
    { "kernel32.dll", "GetLastError",     (void *)&pe_GetLastError },
    { "kernel32.dll", "SetLastError",     (void *)&pe_SetLastError },
    { "kernel32.dll", "ExitProcess",      (void *)&pe_ExitProcess },
    { "user32.dll",   "MessageBoxA",      (void *)&pe_MessageBoxA },
    { "user32.dll",   "MessageBoxW",      (void *)&pe_MessageBoxA },
};

static void *stub_lookup(const char *dll, const char *name) {
    unsigned i;
    for (i = 0; i < sizeof(pe_stubs) / sizeof(pe_stubs[0]); i++)
        if (ieq(pe_stubs[i].dll, dll) && ieq(pe_stubs[i].name, name))
            return pe_stubs[i].fn;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* image parsing                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    u32 size_of_image;
    u32 size_of_headers;
    u32 entry_rva;
    u32 image_base;
    u32 import_rva, import_size;
    u32 reloc_rva, reloc_size;
    u32 nsections;
    u16 subsystem;
    u32 rva_off;
} PeHdr;

static int pe_header(const u8 *d, int len, PeHdr *h) {
    u32 lfanew, opt;
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    lfanew = rd32(d + 0x3C);
    if (lfanew + 24 > (u32)len) return -2;
    if (rd32(d + lfanew) != 0x00004550UL) return -2;
    if (rd16(d + lfanew + 4) != 0x014C && rd16(d + lfanew + 4) != 0x0166) return -3;  /* i386 / ARM kept out */
    h->nsections = rd16(d + lfanew + 6);
    h->rva_off = lfanew + 24;
    if (h->rva_off + 0xE0 > (u32)len) return -2;
    opt = h->rva_off;
    if (rd16(d + opt) != 0x10B) return -3;      /* PE32 only */
    h->entry_rva = rd32(d + opt + 16);
    h->image_base = rd32(d + opt + 28);
    h->size_of_image = rd32(d + opt + 56);
    h->size_of_headers = rd32(d + opt + 60);
    h->subsystem = (u16)rd16(d + opt + 68);
    h->import_rva = rd32(d + opt + 96 + 8);
    h->import_size = rd32(d + opt + 96 + 12);
    h->reloc_rva = rd32(d + opt + 96 + 5 * 8);
    h->reloc_size = rd32(d + opt + 96 + 5 * 8 + 4);
    if (h->size_of_image == 0 || h->size_of_image > PE_ARENA_SIZE) return -4;
    if (h->nsections == 0 || h->nsections > 24) return -4;
    return 0;
}

static const char *pe_err(int code) {
    switch (code) {
        case -1: return "не EXE (нет подписи MZ)";
        case -2: return "повреждён PE-заголовок";
        case -3: return "нужен PE32 для x86 (i386)";
        case -4: return "образ слишком велик для арены";
        case -5: return "не удалось разобрать импорт";
        case -6: return "нет нужной функции в Win32-подсистеме";
        case -7: return "не удалось применить релокации";
        default: return "ошибка загрузки";
    }
}

int pe_is_pe(const u8 *data, int len) {
    PeHdr h;
    return pe_header(data, len, &h) == 0;
}

void pe_summary(const u8 *data, int len, char *out, int cap) {
    PeHdr h;
    if (pe_header(data, len, &h) != 0) {
        snprintf(out, cap, "Не PE32-образ");
        return;
    }
    snprintf(out, cap, "PE32 · i386 · %u секц. · вход 0x%x · база 0x%x · %s",
             h.nsections, h.entry_rva, h.image_base,
             h.subsystem == 3 ? "консоль" : (h.subsystem == 2 ? "GUI" : "иная"));
}

/* ------------------------------------------------------------------ */
/* mapping                                                             */
/* ------------------------------------------------------------------ */
static int pe_map(const u8 *d, int len, const PeHdr *h, PeInfo *pi, int *delta_out) {
    u8 *arena = (u8 *)PE_ARENA_BASE;
    u32 i, sect;
    u32 delta;

    memset(arena, 0, h->size_of_image);
    memcpy(arena, d, h->size_of_headers > (u32)len ? (u32)len : h->size_of_headers);

    sect = h->rva_off + 0xE0;
    for (i = 0; i < h->nsections; i++, sect += 40) {
        u32 vsize, rva, rawsize, rawptr;
        if (sect + 40 > (u32)len) return -2;
        vsize = rd32(d + sect + 8);
        rva = rd32(d + sect + 12);
        rawsize = rd32(d + sect + 16);
        rawptr = rd32(d + sect + 20);
        if (rva + vsize > h->size_of_image) return -4;
        if (rawsize && rawptr < (u32)len) {
            u32 n = rawsize;
            if (rawptr + n > (u32)len) n = (u32)len - rawptr;
            if (n > vsize && vsize) n = vsize;
            memcpy(arena + rva, d + rawptr, n);
        }
    }

    delta = PE_ARENA_BASE - h->image_base;
    *delta_out = (int)delta;
    if (delta && h->reloc_size) {
        u32 pos = 0;
        while (pos + 8 <= h->reloc_size) {
            const u8 *blk = arena + h->reloc_rva + pos;
            u32 page = rd32(blk);
            u32 size = rd32(blk + 4);
            u32 k;
            if (size < 8 || pos + size > h->reloc_size) break;
            for (k = 8; k + 2 <= size; k += 2) {
                u16 e = (u16)rd16(blk + k);
                u32 addr = page + (e & 0xFFF);
                switch (e >> 12) {
                    case 0: break;                       /* absolute, nothing to do */
                    case 3: *(u32 *)(arena + addr) += delta; break;
                    case 2: *(u16 *)(arena + addr) = (u16)(*(u16 *)(arena + addr) + delta); break;
                    case 1: *(u32 *)(arena + addr) = (*(u32 *)(arena + addr) & 0xFFFF) + delta +
                                                     (*(u32 *)(arena + addr) >> 16) << 16; break;
                    default: break;                      /* high/low only images are rare */
                }
                pi->relocs++;
            }
            pos += size;
        }
    } else if (delta) {
        return -7;      /* no relocation table: cannot move the image */
    }
    return 0;
}

static int pe_bind_imports(u8 *arena, const PeHdr *h, PeInfo *pi) {
    u32 desc = h->import_rva;
    int guard = 0;
    if (!h->import_size || !h->import_rva) return 0;
    for (;;) {
        u32 oft = rd32(arena + desc);
        u32 name_rva = rd32(arena + desc + 12);
        u32 ft = rd32(arena + desc + 16);
        const char *dll;
        u32 k;
        if (!name_rva || !ft) break;
        if (++guard > 24) break;
        dll = (const char *)(arena + name_rva);
        if (pi->ndlls < 8) {
            strncpy(pi->dlls[pi->ndlls], dll, 23);
            pi->dlls[pi->ndlls][23] = 0;
            pi->ndlls++;
        }
        for (k = 0; k < 512; k++) {
            u32 entry = oft ? rd32(arena + oft + k * 4) : rd32(arena + ft + k * 4);
            const char *fname;
            void *fn;
            if (!entry) break;
            if (entry & 0x80000000u) {          /* imported by ordinal */
                log_puts("[pe] импорт по номеру не поддержан\r\n");
                return -6;
            }
            fname = (const char *)(arena + entry + 2);
            fn = stub_lookup(dll, fname);
            if (!fn) {
                log_puts("[pe] нет функции ");
                log_puts(fname);
                log_puts(" в ");
                log_puts(dll);
                log_puts("\r\n");
                return -6;
            }
            *(u32 *)(arena + ft + k * 4) = (u32)fn;
            pi->nimports++;
        }
        desc += 20;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* public entry                                                        */
/* ------------------------------------------------------------------ */
int pe_run(const u8 *data, int len, char *log, int logcap, int *exit_code) {
    PeHdr h;
    PeInfo pi;
    int rc, delta = 0;
    u8 *arena = (u8 *)PE_ARENA_BASE;

    pe_log = log;
    pe_logcap = logcap;
    pe_logpos = 0;
    if (log && logcap) log[0] = 0;
    memset(&pi, 0, sizeof(pi));

    log_puts("YoroOS PE32 loader\r\n");
    rc = pe_header(data, len, &h);
    if (rc != 0) {
        log_puts("  отказ: ");
        log_puts(pe_err(rc));
        log_puts("\r\n");
        if (exit_code) *exit_code = -1;
        pe_log = NULL;
        return rc;
    }
    log_num("  файл:   ", (u32)len, " байт\r\n");
    log_num("  секций: ", h.nsections, "\r\n");
    log_hex("  база:   0x", h.image_base, "\r\n");
    log_hex("  вход:   RVA 0x", h.entry_rva, "\r\n");

    rc = pe_map(data, len, &h, &pi, &delta);
    if (rc != 0) {
        log_puts("  отказ: ");
        log_puts(pe_err(rc));
        log_puts("\r\n");
        if (exit_code) *exit_code = -1;
        pe_log = NULL;
        return rc;
    }
    log_num("  смещение: ", (u32)(delta < 0 ? -delta : delta), delta ? " байт (релокации применены)\r\n"
                                                                       : " байт (образ лёг по базе)\r\n");
    log_num("  релокаций: ", pi.relocs, "\r\n");

    rc = pe_bind_imports(arena, &h, &pi);
    if (rc != 0) {
        if (exit_code) *exit_code = -1;
        pe_log = NULL;
        return rc;
    }
    {
        int i;
        log_num("  импорт: ", pi.nimports, " функций из ");
        for (i = 0; i < pi.ndlls; i++) {
            if (i) log_puts(", ");
            log_puts(pi.dlls[i]);
        }
        log_puts("\r\n----------------------------------------\r\n");
    }

    pe_img_base = PE_ARENA_BASE;
    pe_exit_code = 0;
    pe_ticks_start = (int)sys_ticks();
    pe_enter(PE_ARENA_BASE + h.entry_rva,
             (u32)(pe_stack + sizeof(pe_stack) - 16));
    {
        int spent = (int)sys_ticks() - pe_ticks_start;
        log_puts("\r\n----------------------------------------\r\n");
        log_num("процесс завершён, код ", (u32)pe_exit_code, "\r\n");
        log_num("время выполнения: ", (u32)spent * 10u, " мс\r\n");
    }
    if (exit_code) *exit_code = pe_exit_code;
    pe_log = NULL;
    return 0;
}
