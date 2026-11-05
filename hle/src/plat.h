/* Small portability layer: file system and string helpers the core needs on
 * Windows, macOS and Linux. */
#ifndef HLE_PLAT_H
#define HLE_PLAT_H
#include "config.h"
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#define PSEP "\\"
#else
#define PSEP "/"
#endif

int    plat_mkdir(const char *path);                 /* also makes missing parents */
void   plat_hide(const char *path);                  /* hidden attribute where it matters (Windows) */
int    plat_exists(const char *path);
int    plat_is_dir(const char *path);
int    plat_stat(const char *path, long long *size, time_t *ctime_, time_t *mtime_);
int    plat_nth_file(const char *dir, int idx, char *out, int outlen);   /* 1-based, regular files only */
int    plat_truncate(FILE *f, long size);
int    plat_copy(const char *src, const char *dst);
void   plat_exe_dir(char *out, int len);             /* folder holding the executable */
void   plat_user_dir(char *out, int len);            /* per-user data folder for the game */
int    plat_stricmp(const char *a, const char *b);
int    plat_strnicmp(const char *a, const char *b, size_t n);
time_t plat_timegm(struct tm *t);

#endif
