/* YoroOS - freestanding string library */
#include "kernel.h"
#include <stdarg.h>

void *memset(void *d, int c, size_t n) {
    u8 *p = (u8 *)d;
    while (n--) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    u8 *dp = (u8 *)d;
    const u8 *sp = (const u8 *)s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    u8 *dp = (u8 *)d;
    const u8 *sp = (const u8 *)s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n; sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) {}
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    if (n) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) {
    char *p = d + strlen(d);
    while ((*p++ = *s++)) {}
    return d;
}

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}

static int utoa_base(char *buf, u32 v, int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[34];
    int i = 0, n = 0;
    if (!v) tmp[i++] = '0';
    while (v) { tmp[i++] = digits[v % base]; v /= base; }
    while (i) buf[n++] = tmp[--i];
    buf[n] = 0;
    return n;
}

void itoa(int v, char *buf) {
    if (v < 0) { buf[0] = '-'; utoa_base(buf + 1, (u32)(-(v + 1)) + 1u, 10, 0); }
    else utoa_base(buf, (u32)v, 10, 0);
}

void utoa(u32 v, char *buf) { utoa_base(buf, v, 10, 0); }

static int vsnprintf_impl(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t n = 0;
    char tmp[40];
    const char *s;
    int width, zero, len, i;
#define PUT(ch) do { if (n + 1 < cap) out[n] = (char)(ch); n++; } while (0)
    while (*fmt) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        zero = 0; width = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            int neg = v < 0;
            u32 uv = neg ? (u32)(-(v + 1)) + 1u : (u32)v;
            utoa_base(tmp, uv, 10, 0);
            len = (int)strlen(tmp) + (neg ? 1 : 0);
            for (i = len; i < width; i++) PUT(zero ? '0' : ' ');
            if (neg) PUT('-');
            for (s = tmp; *s; s++) PUT(*s);
            break;
        }
        case 'u': {
            u32 v = va_arg(ap, u32);
            utoa_base(tmp, v, 10, 0);
            len = (int)strlen(tmp);
            for (i = len; i < width; i++) PUT(zero ? '0' : ' ');
            for (s = tmp; *s; s++) PUT(*s);
            break;
        }
        case 'x': case 'X': {
            u32 v = va_arg(ap, u32);
            utoa_base(tmp, v, 16, *fmt == 'X');
            len = (int)strlen(tmp);
            for (i = len; i < width; i++) PUT(zero ? '0' : ' ');
            for (s = tmp; *s; s++) PUT(*s);
            break;
        }
        case 'p': {
            u32 v = (u32)va_arg(ap, void *);
            PUT('0'); PUT('x');
            utoa_base(tmp, v, 16, 0);
            for (s = tmp; *s; s++) PUT(*s);
            break;
        }
        case 'c':
            PUT((char)va_arg(ap, int));
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = (int)strlen(s);
            for (i = len; i < width; i++) PUT(' ');
            for (; *s; s++) PUT(*s);
            break;
        case '%':
            PUT('%');
            break;
        default:
            if (!*fmt) goto done;
            PUT(*fmt);
            break;
        }
        if (*fmt) fmt++;
    }
done:
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return (int)n;
#undef PUT
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf_impl(buf, 4096, fmt, ap);
    va_end(ap);
    return r;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf_impl(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    return vsnprintf_impl(buf, n, fmt, ap);
}

/* ---------------- utf-8 ---------------- */
int utf8_len(const char *s) {
    int n = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) s += 1;
        else if ((c & 0xE0) == 0xC0) s += 2;
        else if ((c & 0xF0) == 0xE0) s += 3;
        else return n;
        n++;
    }
    return n;
}

int str_utf8_decode(const char *s, int *advance) {
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { if (advance) *advance = 1; return p[0]; }
    if ((p[0] & 0xE0) == 0xC0) {
        if (advance) *advance = 2;
        return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0) {
        if (advance) *advance = 3;
        return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if (advance) *advance = 1;
    return '?';
}

void utf8_next(const char **s, u16 *cp) {
    int adv = 1;
    *cp = (u16)str_utf8_decode(*s, &adv);
    *s += adv;
}

char *strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    size_t i;
    if (!nl) return (char *)h;
    for (i = 0; h[i]; i++)
        if (!strncmp(h + i, n, nl)) return (char *)(h + i);
    return NULL;
}

/* encode a unicode codepoint as UTF-8, returns byte count (0 for control chars) */
int utf8_encode(u16 cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}
