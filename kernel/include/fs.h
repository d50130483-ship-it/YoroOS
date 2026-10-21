#ifndef FS_H
#define FS_H
#include "klib.h"

#define FS_MAX_FILES 128
#define FS_MAX_NAME 48

void fs_init(void);
int fs_valid(void);
void fs_format(void);
int fs_count(void);
const char *fs_name(int i);
int fs_size(int i);
int fs_find(const char *name);
int fs_create(const char *name, const void *data, int len);
int fs_write(const char *name, const void *data, int len);
int fs_read(const char *name, void *buf, int max);
int fs_delete(const char *name);
void fs_wipe(void);

/* settings.dat */
void settings_load(void);
void settings_save(void);

extern char g_user[24];
extern char g_serial[16];

#endif
