/* Portability layer (see plat.h). */
#include "plat.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

int plat_mkdir(const char *path){
    char p[1024]; size_t i, n = strlen(path);
    if (n >= sizeof p) return -1;
    memcpy(p, path, n + 1);
    for (i = 1; i <= n; i++){
        if (p[i] == '/' || p[i] == '\\' || p[i] == 0){
            char c = p[i]; p[i] = 0;
            if (!(i == 2 && p[1] == ':')){
#ifdef _WIN32
                _mkdir(p);
#else
                mkdir(p, 0755);
#endif
            }
            p[i] = c;
        }
    }
    return plat_is_dir(path) ? 0 : -1;
}
void plat_hide(const char *path){
#ifdef _WIN32
    SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
#else
    (void)path;                        /* dot-folders are already hidden */
#endif
}
int plat_exists(const char *path){ struct stat st; return stat(path, &st) == 0; }
int plat_is_dir(const char *path){ struct stat st; return stat(path, &st) == 0 && (st.st_mode & S_IFDIR); }
int plat_stat(const char *path, long long *size, time_t *c, time_t *m){
    struct stat st;
    if (stat(path, &st)) return -1;
    if (size) *size = (long long)st.st_size; if (c) *c = st.st_ctime; if (m) *m = st.st_mtime;
    return 0;
}
int plat_nth_file(const char *dir, int idx, char *out, int outlen){
    int k = 0;
#ifdef _WIN32
    struct _finddata_t fd; intptr_t h; char pat[1024];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    if ((h = _findfirst(pat, &fd)) == -1) return 0;
    do { if (!(fd.attrib & _A_SUBDIR) && ++k == idx){ snprintf(out, (size_t)outlen, "%s", fd.name); _findclose(h); return 1; } } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(dir); struct dirent *e; char p[1024];
    if (!d) return 0;
    while ((e = readdir(d)) != 0){
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (plat_is_dir(p)) continue;
        if (++k == idx){ snprintf(out, (size_t)outlen, "%s", e->d_name); closedir(d); return 1; }
    }
    closedir(d);
#endif
    return 0;
}
int plat_truncate(FILE *f, long size){
    fflush(f);
#ifdef _WIN32
    return _chsize(_fileno(f), size);
#else
    return ftruncate(fileno(f), size);
#endif
}
int plat_copy(const char *src, const char *dst){
    FILE *a = fopen(src, "rb"), *b; char buf[65536]; size_t n;
    if (!a) return -1;
    if (!(b = fopen(dst, "wb"))){ fclose(a); return -1; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a); fclose(b);
    return 0;
}
void plat_exe_dir(char *out, int len){
    char *s;
    out[0] = 0;
#ifdef _WIN32
    GetModuleFileNameA(0, out, (DWORD)len);
#elif defined(__APPLE__)
    {   uint32_t n = (uint32_t)len; char tmp[1024];
        if (_NSGetExecutablePath(tmp, &n) == 0 && !realpath(tmp, out)) snprintf(out, (size_t)len, "%s", tmp); }
#else
    {   ssize_t n = readlink("/proc/self/exe", out, (size_t)len - 1); if (n > 0) out[n] = 0; else out[0] = 0; }
#endif
    s = strrchr(out, PSEP[0]);
    if (s) *s = 0; else snprintf(out, (size_t)len, ".");
}
void plat_user_dir(char *out, int len){
#ifdef _WIN32
    const char *a = getenv("APPDATA");
    snprintf(out, (size_t)len, "%s\\" HLE_APP_NAME, a ? a : ".");
#elif defined(__APPLE__)
    const char *h = getenv("HOME");
    snprintf(out, (size_t)len, "%s/Library/Application Support/" HLE_APP_NAME, h ? h : ".");
#else
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(out, (size_t)len, "%s/" HLE_APP_SLUG, x);
    else snprintf(out, (size_t)len, "%s/.local/share/" HLE_APP_SLUG, h ? h : ".");
#endif
}
int plat_stricmp(const char *a, const char *b){
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}
int plat_strnicmp(const char *a, const char *b, size_t n){
#ifdef _WIN32
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}
time_t plat_timegm(struct tm *t){
#ifdef _WIN32
    return _mkgmtime(t);
#else
    return timegm(t);
#endif
}
