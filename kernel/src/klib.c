#include "klib.h"

#include "klib.h"

#ifndef YORO_SIM
void *memset(void *dst, int v, u32 n) {
    u8 *d = dst;
    while (n--) *d++ = (u8)v;
    return dst;
}

void *memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, u32 n) {
    u8 *d = dst;
    const u8 *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n == 0 ? 0 : (u8)*a - (u8)*b;
}

void strcpy(char *dst, const char *src, int max) {
    while (max-- > 1 && *src) *dst++ = *src++;
    *dst = 0;
}

#endif /* !YORO_SIM */
int stoi(const char *s) {
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

void itoa(int v, char *buf) {
    char tmp[12];
    int i = 0, j = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = '0' + v % 10; v /= 10; }
    if (neg) buf[j++] = '-';
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}

unsigned int utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (s[0] < 0x80) { (*p)++; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *p += 2;
        return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *p += 3;
        return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    (*p)++;
    return s[0] ? s[0] : 0x2022;
}

static u32 rnd_state = 0x1234ABCD;
u32 rnd_next(void) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

int utf8_put(char *dst, unsigned cp) {
    if (cp < 0x80) { dst[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    dst[0] = (char)(0xE0 | (cp >> 12));
    dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}
