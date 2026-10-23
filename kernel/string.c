/* YoroOS 0.2 - Portable 32/64-bit freestanding string, printf, UTF-8 & CRC32 */
#include "kernel.h"

void *memset(void *d, int c, size_t n) {
    u8 *p = (u8 *)d;
    u8 v = (u8)c;
    while (n && ((uptr)p & 7)) { *p++ = v; n--; }
    if (n >= 8) {
        u64 v64 = (u64)v * 0x0101010101010101ULL;
        u64 *p64 = (u64 *)p;
        while (n >= 32) {
            p64[0] = v64; p64[1] = v64; p64[2] = v64; p64[3] = v64;
            p64 += 4; n -= 32;
        }
        while (n >= 8) { *p64++ = v64; n -= 8; }
        p = (u8 *)p64;
    }
    while (n--) *p++ = v;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    u8 *dp = (u8 *)d;
    const u8 *sp = (const u8 *)s;
    if ((((uptr)dp | (uptr)sp) & 7) == 0) {
        u64 *d64 = (u64 *)dp;
        const u64 *s64 = (const u64 *)sp;
        while (n >= 32) {
            d64[0] = s64[0]; d64[1] = s64[1]; d64[2] = s64[2]; d64[3] = s64[3];
            d64 += 4; s64 += 4; n -= 32;
        }
        while (n >= 8) { *d64++ = *s64++; n -= 8; }
        dp = (u8 *)d64;
        sp = (const u8 *)s64;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    u8 *dp = (u8 *)d;
    const u8 *sp = (const u8 *)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp) return memcpy(d, s, n);
    dp += n;
    sp += n;
    while (n--) *--dp = *--sp;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *pa = (const u8 *)a, *pb = (const u8 *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    if (!s) return 0;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? ((int)(u8)*a - (int)(u8)*b) : 0;
}

static char fold_lo(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

int strcasecmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        char ca = fold_lo(*a), cb = fold_lo(*b);
        if (ca != cb) return (int)(u8)ca - (int)(u8)cb;
        a++; b++;
    }
    return (int)(u8)fold_lo(*a) - (int)(u8)fold_lo(*b);
}

char *strcasestr_ascii(const char *hay, const char *needle) {
    size_t nl;
    if (!hay || !needle) return NULL;
    if (!*needle) return (char *)hay;
    nl = strlen(needle);
    while (*hay) {
        size_t i = 0;
        while (i < nl && hay[i] && fold_lo(hay[i]) == fold_lo(needle[i])) i++;
        if (i == nl) return (char *)hay;
        hay++;
    }
    return NULL;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++) != 0) {}
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}

char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++) != 0) {}
    return r;
}

char *strchr_y(const char *s, int ch) {
    if (!s) return NULL;
    while (*s) {
        if ((u8)*s == (u8)ch) return (char *)s;
        s++;
    }
    return ch == 0 ? (char *)s : NULL;
}

char *strrchr_y(const char *s, int ch) {
    const char *found = NULL;
    if (!s) return NULL;
    while (*s) {
        if ((u8)*s == (u8)ch) found = s;
        s++;
    }
    return ch == 0 ? (char *)s : (char *)found;
}

char *strstr(const char *h, const char *n) {
    size_t nl;
    if (!*n) return (char *)h;
    nl = strlen(n);
    while (*h) {
        if (!strncmp(h, n, nl)) return (char *)h;
        h++;
    }
    return NULL;
}

int atoi(const char *s) {
    int neg = 0, v = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static void u64_to_str(u64 v, int base, int upper, char *out) {
    char tmp[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = dig[v % (u64)base]; v /= (u64)base; }
    while (n > 0) out[i++] = tmp[--n];
    out[i] = 0;
}

void utoa(u32 v, char *buf) { u64_to_str((u64)v, 10, 0, buf); }

void itoa(int v, char *buf) {
    if (v < 0) {
        *buf++ = '-';
        u64_to_str((u64)(-(i64)v), 10, 0, buf);
    } else {
        u64_to_str((u64)v, 10, 0, buf);
    }
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    size_t pos = 0;
    if (!buf || n == 0) return 0;
    #define PUT(ch) do { if (pos + 1 < n) buf[pos] = (ch); pos++; } while (0)

    while (*fmt) {
        int left = 0, zpad = 0, width = 0, prec = -1, is_ll = 0;
        char num[40];
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        if (*fmt == '%') { PUT('%'); fmt++; continue; }
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zpad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { is_ll = 1; fmt++; }
            else if (sizeof(void *) == 8) is_ll = 1;
        } else if (*fmt == 'z') {
            if (sizeof(size_t) == 8) is_ll = 1;
            fmt++;
        }
        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            PUT(c);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int slen, pad, i;
            if (!s) s = "(null)";
            slen = (int)strlen(s);
            if (prec >= 0 && slen > prec) slen = prec;
            pad = width - slen;
            if (!left) while (pad-- > 0) PUT(' ');
            for (i = 0; i < slen; i++) PUT(s[i]);
            if (left) while (pad-- > 0) PUT(' ');
            break;
        }
        case 'd': case 'i': {
            i64 sv = is_ll ? va_arg(ap, i64) : (i64)va_arg(ap, int);
            int neg = sv < 0, len, pad, i;
            u64 uv = neg ? (u64)(-sv) : (u64)sv;
            u64_to_str(uv, 10, 0, num);
            len = (int)strlen(num) + neg;
            pad = width - len;
            if (!left && !zpad) while (pad-- > 0) PUT(' ');
            if (neg) PUT('-');
            if (!left && zpad) while (pad-- > 0) PUT('0');
            for (i = 0; num[i]; i++) PUT(num[i]);
            if (left) while (pad-- > 0) PUT(' ');
            break;
        }
        case 'u': case 'x': case 'X': case 'p': {
            int base = (*fmt == 'u') ? 10 : 16;
            int upper = (*fmt == 'X');
            u64 uv;
            int len, pad, i;
            if (*fmt == 'p') {
                uv = (u64)(uptr)va_arg(ap, void *);
                PUT('0'); PUT('x');
            } else {
                uv = is_ll ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned int);
            }
            u64_to_str(uv, base, upper, num);
            len = (int)strlen(num);
            pad = width - len;
            if (!left) while (pad-- > 0) PUT(zpad ? '0' : ' ');
            for (i = 0; num[i]; i++) PUT(num[i]);
            if (left) while (pad-- > 0) PUT(' ');
            break;
        }
        default:
            PUT(*fmt);
            break;
        }
        if (*fmt) fmt++;
    }
    buf[pos < n ? pos : n - 1] = 0;
    #undef PUT
    return (int)pos;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, 65536, fmt, ap);
    va_end(ap);
    return r;
}

int str_utf8_decode(const char *s, int *advance) {
    u8 b0 = (u8)s[0];
    if (!b0) { *advance = 0; return 0; }
    if (b0 < 0x80) { *advance = 1; return b0; }
    if ((b0 & 0xE0) == 0xC0 && ((u8)s[1] & 0xC0) == 0x80) {
        *advance = 2;
        return ((b0 & 0x1F) << 6) | ((u8)s[1] & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0 && ((u8)s[1] & 0xC0) == 0x80 && ((u8)s[2] & 0xC0) == 0x80) {
        *advance = 3;
        return ((b0 & 0x0F) << 12) | (((u8)s[1] & 0x3F) << 6) | ((u8)s[2] & 0x3F);
    }
    *advance = 1;
    return '?';
}

int utf8_encode(u16 cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; out[1] = 0; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    out[3] = 0;
    return 3;
}

int utf8_len(const char *s) {
    int n = 0, adv;
    while (*s) {
        str_utf8_decode(s, &adv);
        if (adv <= 0) break;
        s += adv;
        n++;
    }
    return n;
}

void utf8_next(const char **s, u16 *cp) {
    int adv = 0;
    int v = str_utf8_decode(*s, &adv);
    *cp = (u16)v;
    *s += adv;
}

u32 crc32_calc(const void *data, size_t len) {
    const u8 *p = (const u8 *)data;
    u32 crc = 0xFFFFFFFFU;
    size_t i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320U & (u32)(-(i32)(crc & 1U)));
    }
    return ~crc;
}

#ifndef YORO_64
u64 __udivmoddi4(u64 num, u64 den, u64 *rem_p) {
    u64 quot = 0, qbit = 1;
    if (den == 0) { if (rem_p) *rem_p = 0; return 0; }
    while ((i64)den >= 0) { den <<= 1; qbit <<= 1; }
    while (qbit) {
        if (den <= num) { num -= den; quot += qbit; }
        den >>= 1;
        qbit >>= 1;
    }
    if (rem_p) *rem_p = num;
    return quot;
}
u64 __udivdi3(u64 a, u64 b) { return __udivmoddi4(a, b, NULL); }
u64 __umoddi3(u64 a, u64 b) { u64 r = 0; __udivmoddi4(a, b, &r); return r; }
i64 __divdi3(i64 a, i64 b) {
    int neg = ((a < 0) ^ (b < 0));
    u64 ua = a < 0 ? -(u64)a : (u64)a;
    u64 ub = b < 0 ? -(u64)b : (u64)b;
    u64 q = __udivmoddi4(ua, ub, NULL);
    return neg ? -(i64)q : (i64)q;
}
i64 __moddi3(i64 a, i64 b) {
    int neg = (a < 0);
    u64 ua = a < 0 ? -(u64)a : (u64)a;
    u64 ub = b < 0 ? -(u64)b : (u64)b;
    u64 r = 0;
    __udivmoddi4(ua, ub, &r);
    return neg ? -(i64)r : (i64)r;
}
#endif

