/* File Manager over a host folder, plus the .Sound driver.
 *
 * The game sees one volume. Each Mac file "Name" is stored in the save folder as
 * Name (data fork), .rsrc\Name (resource fork) and .finf\Name (16-byte Finder info),
 * so the folder itself only shows the documents.
 */
#include "mac.h"
#include "plat.h"
#include <time.h>

#define VREF -1
#define VOLNAME HLE_VOLNAME
static char g_save[600];
#define MAXF 24
static struct { int used, rsrc; FILE *f; char path[700]; } g_fcb[MAXF];
#define FREF0 100

const char *fs_save_dir(void){ return g_save; }
void fs_init(const char *save_dir, const char *app_dir){
    char p[700]; (void)app_dir;
    strncpy(g_save, save_dir, sizeof g_save - 1);
    plat_mkdir(g_save);
    snprintf(p, sizeof p, "%s" PSEP ".rsrc", g_save); plat_mkdir(p); plat_hide(p);
    snprintf(p, sizeof p, "%s" PSEP ".finf", g_save); plat_mkdir(p); plat_hide(p);
}

static void name_of(unsigned pname, char *out){
    char n[256], *c; int i;
    pstr_get(pname, n, sizeof n);
    c = strrchr(n, ':'); if (c) memmove(n, c + 1, strlen(c + 1) + 1);
    for (i = 0; n[i]; i++){ unsigned char ch = (unsigned char)n[i]; if (strchr("/\\*?\"<>|", ch) || ch < 32) n[i] = '_'; }
    strcpy(out, n);
}
static void host_path(const char *name, int fork, char *out){
    if (fork == 0) snprintf(out, 700, "%s" PSEP "%s", g_save, name);
    else snprintf(out, 700, "%s" PSEP "%s" PSEP "%s", g_save, fork == 1 ? ".rsrc" : ".finf", name);
}
static int exists(const char *p){ return plat_exists(p); }
static void touch(const char *p){ FILE *f; if (!exists(p) && (f = fopen(p, "wb")) != 0) fclose(f); }

int fs_rsrc_path(unsigned pname, int vref, char *out, int create){
    char n[256], dp[700]; (void)vref;
    name_of(pname, n);
    if (!n[0]) return -1;
    host_path(n, 1, out);
    if (create){ host_path(n, 0, dp); touch(dp); return 0; }
    return exists(out) ? 0 : -1;
}

static void set_res(unsigned pb, int e){ ww(pb + 16, (unsigned)e); setreg(M68K_REG_D0, (unsigned)e); }
static FILE *fcb(int ref){ int i = ref - FREF0; return (i >= 0 && i < MAXF && g_fcb[i].used) ? g_fcb[i].f : 0; }
static long fsize(FILE *f){ long c = ftell(f), n; fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, c, SEEK_SET); return n; }

static int seek(FILE *f, unsigned pb){
    int mode = sw(pb + 44) & 3; long off = (long)rl(pb + 46), base = 0;
    if (mode == 1) base = 0; else if (mode == 2) base = fsize(f); else if (mode == 3) base = ftell(f); else return 0;
    if (mode == 0) return 0;
    return fseek(f, base + off, SEEK_SET) ? -40 : 0;
}

static int open_file(unsigned pb, int rsrc){
    char n[256], p[700]; int i; FILE *f;
    unsigned np = rl(pb + 18);
    if (np && rb(np) > 0 && rb(np + 1) == '.'){                       /* driver */
        char dn[64]; pstr_get(np, dn, sizeof dn);
        if (!plat_stricmp(dn, ".Sound")){ ww(pb + 24, (unsigned)-4); return 0; }
        return -43;
    }
    name_of(np, n);
    if (!n[0]) return -43;
    host_path(n, rsrc ? 1 : 0, p);
    if (!exists(p)){ char dp[700]; host_path(n, 0, dp); if (rsrc && exists(dp)) touch(p); else return -43; }
    f = fopen(p, (rb(pb + 27) == 1) ? "rb" : "r+b");
    if (!f) f = fopen(p, "rb");
    if (!f) return -54;
    for (i = 0; i < MAXF && g_fcb[i].used; i++);
    if (i == MAXF){ fclose(f); return -42; }
    g_fcb[i].used = 1; g_fcb[i].rsrc = rsrc; g_fcb[i].f = f; strcpy(g_fcb[i].path, p);
    ww(pb + 24, (unsigned)(FREF0 + i));
    return 0;
}

/* nth document in the save folder (1-based), skipping the sidecar folders */
static int nth_file(int idx, char *out){ return plat_nth_file(g_save, idx, out, 256); }
static unsigned mac_time(time_t t){ return (unsigned)(t + 2082844800u); }

static int file_info(unsigned pb, int set){
    char n[256], p[700], fp[700]; int idx = sw(pb + 28); long long size = 0; time_t ct = 0, mt = 0; unsigned np = rl(pb + 18);
    if (!set && idx > 0){ if (!nth_file(idx, n)) return -43; if (np) pstr_put(np, n); }
    else name_of(np, n);
    host_path(n, 0, p); host_path(n, 2, fp);
    if (!exists(p)) return -43;
    if (set){
        FILE *f = fopen(fp, "wb"); if (f){ fwrite(g_ram + MA(pb + 32), 1, 16, f); fclose(f); }
        return 0;
    }
    memset(g_ram + MA(pb + 30), 0, 50);
    {   FILE *f = fopen(fp, "rb"); if (f){ fread(g_ram + MA(pb + 32), 1, 16, f); fclose(f); } }
    plat_stat(p, &size, &ct, &mt);
    wl(pb + 48, 1);
    wl(pb + 54, (unsigned)size); wl(pb + 58, ((unsigned)size + 511) & ~511u);
    {   char rp[700]; long long rs; host_path(n, 1, rp);
        if (plat_stat(rp, &rs, 0, 0) == 0){ wl(pb + 64, (unsigned)rs); wl(pb + 68, ((unsigned)rs + 511) & ~511u); } }
    wl(pb + 72, mac_time(ct)); wl(pb + 76, mac_time(mt));
    ww(pb + 24, 0);
    return 0;
}

void fs_os_trap(unsigned word){
    unsigned pb = reg(M68K_REG_A0); int e = 0, op = (int)(word & 0xFF);
    int async = (word & 0x400) != 0;
    if (op == 0x60){                                                   /* HFSDispatch */
        int sel = (int)(reg(M68K_REG_D0) & 0xFFFF);
        switch (sel){
        case 0x01: ww(pb + 22, (unsigned)VREF); e = 0; break;          /* OpenWD */
        case 0x02: e = 0; break;                                       /* CloseWD */
        case 0x07: ww(pb + 32, (unsigned)VREF); wl(pb + 48, 2); wl(pb + 28, 0); if (rl(pb + 18)) pstr_put(rl(pb + 18), VOLNAME); e = 0; break;   /* GetWDInfo */
        case 0x09: e = file_info(pb, 0); break;                        /* GetCatInfo */
        case 0x0A: e = file_info(pb, 1); break;                        /* SetCatInfo */
        case 0x1A: e = open_file(pb, 0); break;                        /* OpenDF */
        case 0x1B: e = open_file(pb, 1); break;                        /* OpenRF */
        default: e = -50;
        }
        set_res(pb, e); return;
    }
    switch (op){
    case 0x00: e = open_file(pb, 0); break;                            /* Open */
    case 0x0A: e = open_file(pb, 1); break;                            /* OpenRF */
    case 0x01: { int ref = sw(pb + 24), i = ref - FREF0;              /* Close */
                 if (ref < 0) e = 0; else if (i >= 0 && i < MAXF && g_fcb[i].used){ fclose(g_fcb[i].f); g_fcb[i].used = 0; } else e = -51; } break;
    case 0x02: case 0x03: {                                            /* Read / Write */
        int ref = sw(pb + 24); unsigned buf = rl(pb + 32), n = rl(pb + 36), got = 0; FILE *f;
        if (ref == -4){ if (op == 3) snd_driver_write(pb); else e = 0; wl(pb + 40, n); set_res(pb, 0); return; }
        f = fcb(ref);
        if (!f){ e = -51; break; }
        e = seek(f, pb);
        if (!e){
            if (op == 2){ got = (unsigned)fread(g_ram + MA(buf), 1, n, f); if (got < n) e = -39; }
            else { fflush(f); got = (unsigned)fwrite(g_ram + MA(buf), 1, n, f); fflush(f); if (got < n) e = -34; }
        }
        wl(pb + 40, got); wl(pb + 46, (unsigned)ftell(f));
        } break;
    case 0x04: case 0x05: e = sw(pb + 24) == -4 ? 0 : (sw(pb + 24) < 0 ? -17 : -51); break;   /* Control / Status */
    case 0x06: if (sw(pb + 24) == -4) snd_stop(); e = 0; break;       /* KillIO */
    case 0x07: {                                                       /* GetVolInfo */
        unsigned np = rl(pb + 18);
        if (np) pstr_put(np, VOLNAME);
        ww(pb + 22, (unsigned)VREF); wl(pb + 30, mac_time(time(0))); wl(pb + 34, 0); ww(pb + 38, 0);
        {   int k = 0; char tmp[256]; while (nth_file(k + 1, tmp)) k++; ww(pb + 40, (unsigned)k); }
        ww(pb + 42, 0); ww(pb + 44, 0); ww(pb + 46, 0xFFFF); wl(pb + 48, 0x4000); wl(pb + 52, 0x4000); ww(pb + 56, 0); wl(pb + 58, 100); ww(pb + 62, 0x8000);
        if (word & 0x200){ wl(pb + 64, 0); ww(pb + 68, 1); wl(pb + 70, 0); ww(pb + 74, (unsigned)VREF); ww(pb + 76, 2); wl(pb + 78, 0); }
        e = 0; } break;
    case 0x08: {                                                       /* Create */
        char n[256], p[700]; name_of(rl(pb + 18), n); host_path(n, 0, p);
        if (exists(p)) e = -48; else { FILE *f = fopen(p, "wb"); if (f){ fclose(f); host_path(n, 2, p); f = fopen(p, "wb"); if (f){ char z[16] = {0}; fwrite(z, 1, 16, f); fclose(f); } } else e = -34; }
        } break;
    case 0x09: {                                                       /* Delete */
        char n[256], p[700]; int k; name_of(rl(pb + 18), n); host_path(n, 0, p);
        if (!exists(p)) e = -43; else for (k = 0; k < 3; k++){ host_path(n, k, p); remove(p); } } break;
    case 0x0B: {                                                       /* Rename */
        char a[256], b[256], pa[700], pbb[700]; int k; name_of(rl(pb + 18), a); name_of(rl(pb + 28), b);
        for (k = 0; k < 3; k++){ host_path(a, k, pa); host_path(b, k, pbb); if (exists(pa)) rename(pa, pbb); } } break;
    case 0x0C: e = file_info(pb, 0); break;                            /* GetFileInfo */
    case 0x0D: e = file_info(pb, 1); break;                            /* SetFileInfo */
    case 0x11: { FILE *f = fcb(sw(pb + 24)); if (f) wl(pb + 28, (unsigned)fsize(f)); else e = -51; } break;   /* GetEOF */
    case 0x12: { FILE *f = fcb(sw(pb + 24)); if (f) plat_truncate(f, (long)rl(pb + 28)); else e = -51; } break;   /* SetEOF */
    case 0x18: { FILE *f = fcb(sw(pb + 24)); if (f) wl(pb + 46, (unsigned)ftell(f)); else e = -51; } break;   /* GetFPos */
    case 0x44: { FILE *f = fcb(sw(pb + 24)); if (f) e = seek(f, pb); else e = -51; } break;   /* SetFPos */
    case 0x10: case 0x13: case 0x0E: case 0x0F: case 0x16: case 0x17: e = 0; break;   /* Allocate FlushVol Unmount Mount FInitQueue Eject */
    case 0x14: { unsigned np = rl(pb + 18); if (np) pstr_put(np, VOLNAME); ww(pb + 22, (unsigned)VREF); e = 0; } break;   /* GetVol */
    case 0x15: e = 0; break;                                           /* SetVol */
    case 0x41: case 0x42: case 0x43: case 0x45: e = 0; break;
    default: logf_("[fs] unhandled file trap %04X\n", word); e = -50;
    }
    set_res(pb, e);
    if (async && rl(pb + 12)){ extern void hle_defer(unsigned addr, unsigned a0, unsigned due); hle_defer(rl(pb + 12), pb, rl(LM_Ticks)); }
}
