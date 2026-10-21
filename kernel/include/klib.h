#ifndef KLIB_H
#define KLIB_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define NULL ((void *)0)

#ifdef YORO_SIM
#include <string.h>
char *yoro_strcpy(char *dst, const char *src, int max);
#define strcpy(dst, src, max) yoro_strcpy((dst), (src), (max))
#else
void *memset(void *dst, int v, u32 n);
void *memcpy(void *dst, const void *src, u32 n);
void *memmove(void *dst, const void *src, u32 n);
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
void strcpy(char *dst, const char *src, int max);
#endif
int stoi(const char *s);
void itoa(int v, char *buf);

/* UTF-8: возвращает кодовую точку, двигает указатель */
unsigned int utf8_next(const char **p);
int utf8_put(char *dst, unsigned cp);

/* спит/логика */
u32 rnd_next(void); /* детерминированный ГПСЧ */

#endif
