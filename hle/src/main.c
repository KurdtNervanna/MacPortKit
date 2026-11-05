/* Start-up, the Segment Loader and the main loop of the generic Mac Toolbox HLE.
 *
 *   <exe> [game folder]
 *
 * The game folder holds the application's resource fork (HLE_APP_FILE) and a System 6
 * System file (System.rsrc, for its fonts); see find_game. Settings and documents go
 * to a "save" folder: beside the game folder on Windows, in the user's data folder
 * on macOS and Linux.
 */
#include "mac.h"
#include "plat.h"
#include "audio.h"
#include <time.h>

#ifndef HLE_VERSION
#define HLE_VERSION "dev"
#endif

/* ---- timing, presenting and deferred 68k completions -------------------------------- */
static unsigned g_t0;
static int g_dirty_flag;
static unsigned g_last_present;
typedef struct { unsigned addr, a0, due; } Deferred;
static Deferred g_def[32]; static int g_ndef;

void hle_dirty(void){ g_dirty_flag = 1; }
void hle_defer(unsigned addr, unsigned a0, unsigned due){ if (g_ndef < 32){ g_def[g_ndef].addr = addr; g_def[g_ndef].a0 = a0; g_def[g_ndef].due = due; g_ndef++; } }

static void run_deferred(void){
    int i;
    for (i = 0; i < g_ndef; ){
        if (rl(LM_Ticks) >= g_def[i].due){
            Deferred d = g_def[i]; unsigned sa0;
            g_def[i] = g_def[--g_ndef];
            sa0 = reg(M68K_REG_A0); setreg(M68K_REG_A0, d.a0);
            call68k(d.addr, 0, "");
            setreg(M68K_REG_A0, sa0);
        } else i++;
    }
}

static void dump_screen(const char *path){
    FILE *f = fopen(path, "wb");
    if (f){ fprintf(f, "P4\n%d %d\n", g_scr_w, g_scr_h); fwrite(g_ram + SCREEN_BASE, 1, (size_t)g_scr_rb * (size_t)g_scr_h, f); fclose(f); }
}

/* HLE_SCRIPT="ms:action;..." for headless tests. Actions: click x y | down x y | up x y |
   key c | cmd c | dump file | quit. Times are milliseconds since start. */
static void run_script(unsigned now){
    static const char *s = (const char*)1; static unsigned next_at; static char act[256];
    extern void host_inject(int type, int h, int v, int ch, int mods);
    if (s == (const char*)1){ s = getenv(HLE_ENV("SCRIPT")); next_at = 0; act[0] = 0; }
    while (s && *s){
        char buf[300]; const char *e = strchr(s, ';'); size_t n = e ? (size_t)(e - s) : strlen(s); unsigned at; char *c;
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, s, n); buf[n] = 0;
        at = (unsigned)atol(buf); c = strchr(buf, ':');
        if (now < at || !c) { if (!c) s = e ? e + 1 : 0; return; }
        c++;
        logf_("[script %u] %s\n", now, c);
        if (!strncmp(c, "click ", 6)){ int x, y; sscanf(c + 6, "%d %d", &x, &y); host_inject(HE_MOUSEDOWN, x, y, 0, 0); host_inject(HE_MOUSEUP, x, y, 0, 0); }
        else if (!strncmp(c, "down ", 5)){ int x, y; sscanf(c + 5, "%d %d", &x, &y); host_inject(HE_MOUSEDOWN, x, y, 0, 0); }
        else if (!strncmp(c, "up ", 3)){ int x, y; sscanf(c + 3, "%d %d", &x, &y); host_inject(HE_MOUSEUP, x, y, 0, 0); }
        else if (!strncmp(c, "move ", 5)){ int x, y; sscanf(c + 5, "%d %d", &x, &y); host_inject(0, x, y, 0, 0); }
        else if (!strncmp(c, "key ", 4)){ int ch = c[4] == '\\' ? (c[5] == 'r' ? 13 : c[5] == 't' ? 9 : c[5] == 'b' ? 8 : c[5]) : c[4]; host_inject(HE_KEYDOWN, 0, 0, ch, 0); }
        else if (!strncmp(c, "cmd ", 4)) host_inject(HE_KEYDOWN, 0, 0, c[4], 0x100);
        else if (!strncmp(c, "dump ", 5)) dump_screen(c + 5);
        else if (!strncmp(c, "quit", 4)) g_quit = 1;
        s = e ? e + 1 : 0;
    }
}

void hle_idle(int sleep_ms){
    static int depth; unsigned now; int h, v, down;
    if (!host_pump()) g_quit = 1;
    now = host_ms() - g_t0;
    run_script(now);
    wl(LM_Ticks, now * 60u / 1000u);
    wl(LM_Time, (unsigned)(time(0) + 2082844800u));
    host_mouse(&h, &v, &down);
    ww(LM_Mouse, (unsigned)v); ww(LM_Mouse + 2, (unsigned)h); wb(LM_MBState, down ? 0 : 0x80);
    {   unsigned char km[16]; int i; host_keymap(km); for (i = 0; i < 16; i++) wb(LM_KeyMap + (unsigned)i, km[i]); }
    audio_service();
    if (g_dirty_flag && host_ms() - g_last_present >= 15){
        host_present(g_ram + SCREEN_BASE, g_scr_rb);
        g_dirty_flag = 0; g_last_present = host_ms();
    }
    if (!depth && g_ndef){ depth++; run_deferred(); depth--; }
    if (sleep_ms > 0) host_sleep_ms(sleep_ms);
}

/* ---- launch ---------------------------------------------------------------------- */
/* A game folder holds the application's resource fork (any file containing the
   'Watc' signature resource: raw fork, AppleDouble or MacBinary) and a System file
   (any file with 'FONT' resources). HLE_APP_FILE / HLE_SYS_FILE are tried first.
   HLE_APP_SIGNATURE is set in config.h. */
static int scan_game(const char *dir, char *app, char *sys){
    char p[1024], n[512]; int k;
    app[0] = sys[0] = 0;
    if (!plat_is_dir(dir)) return 0;
    snprintf(p, sizeof p, "%s" PSEP HLE_APP_FILE, dir); if (plat_exists(p) && rs_file_has(p, HLE_APP_SIGNATURE)) strcpy(app, p);
    snprintf(p, sizeof p, "%s" PSEP HLE_SYS_FILE, dir); if (plat_exists(p) && rs_file_has(p, 0x464F4E54u)) strcpy(sys, p);
    for (k = 1; (!app[0] || !sys[0]) && plat_nth_file(dir, k, n, sizeof n); k++){
        long long sz = 0;
        snprintf(p, sizeof p, "%s" PSEP "%s", dir, n);
        if (plat_stat(p, &sz, 0, 0) || sz > 8 * 1024 * 1024) continue;
        if (!app[0] && rs_file_has(p, HLE_APP_SIGNATURE)) strcpy(app, p);              /* 'Watc' */
        else if (!sys[0] && rs_file_has(p, 0x464F4E54u)) strcpy(sys, p);         /* 'FONT' */
    }
    return app[0] && sys[0];
}
static int find_game(const char *arg, char *dir, char *app, char *sys){
    char exe[600], user[600], c[1024]; int k; FILE *f;
    static const char *rel[] = { "game", ".." PSEP "game", ".." PSEP ".." PSEP "game", ".." PSEP "Resources" PSEP "game", ".", 0 };
    if (arg && scan_game(arg, app, sys)){ snprintf(dir, 600, "%s", arg); return 1; }
    if (getenv(HLE_ENV("GAME")) && scan_game(getenv(HLE_ENV("GAME")), app, sys)){ snprintf(dir, 600, "%s", getenv(HLE_ENV("GAME"))); return 1; }
    plat_exe_dir(exe, sizeof exe);
    for (k = 0; rel[k]; k++){
        snprintf(c, sizeof c, "%s" PSEP "%s", exe, rel[k]);
        if (scan_game(c, app, sys)){ snprintf(dir, 600, "%s", c); return 1; }
    }
    plat_user_dir(user, sizeof user);
    snprintf(c, sizeof c, "%s" PSEP "game", user);
    if (scan_game(c, app, sys)){ snprintf(dir, 600, "%s", c); return 1; }
    snprintf(c, sizeof c, "%s" PSEP "game-folder.txt", user);                     /* a folder picked earlier */
    if ((f = fopen(c, "r")) != 0){
        char line[600] = {0}; if (fgets(line, sizeof line, f)){ line[strcspn(line, "\r\n")] = 0;
            if (scan_game(line, app, sys)){ fclose(f); snprintf(dir, 600, "%s", line); return 1; } }
        fclose(f);
    }
    return 0;
}
static int ask_game_folder(char *dir, char *app, char *sys){
    char user[600], c[1024], pick[1024]; FILE *f;
    host_message(HLE_APP_NAME,
        "Choose the folder that holds your game files:\n\n"
        "  - the " HLE_APP_NAME " application (its resource fork: " HLE_APP_FILE ", or the\n"
        "    AppleDouble/MacBinary file from unpacking the original archive), and\n"
        "  - a System 6 \"System\" file (raw fork, AppleDouble or MacBinary), whose fonts the game uses.\n\n"
        "The folder is remembered.");
    while (host_pick_folder(pick, sizeof pick)){
        if (scan_game(pick, app, sys)){
            plat_user_dir(user, sizeof user); plat_mkdir(user);
            snprintf(c, sizeof c, "%s" PSEP "game-folder.txt", user);
            if ((f = fopen(c, "w")) != 0){ fprintf(f, "%s\n", pick); fclose(f); }
            snprintf(dir, 600, "%s", pick);
            return 1;
        }
        host_message(HLE_APP_NAME, app[0] ? "That folder has the game but no System file (for the fonts)."
                                                       : "That folder does not hold the " HLE_APP_NAME " application.");
    }
    return 0;
}

static void load_segments(void){
    unsigned h0 = rs_get(0x434F4445u, 0), c0, above, below, jtsize, jtoff, i, a5 = A5_BASE;
    if (!h0) fatal("CODE 0 (the jump table) is missing from the application's resource fork.");
    c0 = MA(rl(h0));
    above = rl(c0); below = rl(c0 + 4); jtsize = rl(c0 + 8); jtoff = rl(c0 + 12);
    (void)above;
    if (below > a5 - STACK_TOP) fatal("Application globals do not fit (%u bytes).", below);
    memset(g_ram + a5 - below, 0, below);
    for (i = 0; i < jtsize / 8; i++){
        unsigned e = c0 + 16 + i*8, off = rw(e), seg = rw(e + 4), entry = a5 + jtoff + i*8;
        unsigned sh = rs_get(0x434F4445u, (int)seg);
        if (!sh){ logf_("[seg] jump table entry %u: CODE %u missing\n", i, seg); continue; }
        if (!(rb(sh) & 0x80) && getenv(HLE_ENV("SEGLOG"))) logf_("[seg] CODE %u at %06X (%u bytes)\n", seg, MA(rl(sh)) + 4, mm_handlesize(sh) - 4);
        wb(sh, rb(sh) | 0x80);                                   /* segments stay put */
        ww(entry, seg); ww(entry + 2, 0x4EF9); wl(entry + 4, MA(rl(sh)) + 4 + off);
    }
    setreg(M68K_REG_A5, a5);
    wl(LM_CurrentA5, a5);
    setreg(M68K_REG_PC, a5 + jtoff + 2);
    logf_("[seg] %u jump table entries, %u bytes of globals, entry %06X\n", jtsize / 8, below, a5 + jtoff + 2);
}

static void init_lowmem(void){
    wl(LM_MemTop, MEM_SIZE); wl(LM_BufPtr, STACK_TOP); wl(LM_HeapEnd, HEAP_END);
    wl(LM_ApplLimit, HEAP_END); wl(LM_ApplZone, HEAP_BASE);
    wl(LM_CurStackBase, STACK_TOP);
    ww(LM_ROM85, 0x7FFF);                                   /* 128K ROMs or later, no Color QuickDraw */
    wl(LM_DoubleTime, 30); wl(LM_CaretTime, 32);
    ww(LM_SysEvtMask, 0xFFEF);
    ww(LM_DlgFont, 0); ww(LM_ApFontID, 3);
    ww(LM_SFSaveDisk, (unsigned)-1); wl(LM_CurDirStore, 2);
    pstr_put(LM_CurApName, HLE_APP_NAME);
    wl(LM_RndSeed, (unsigned)time(0));
    {   unsigned h = mm_newhandle(4, 1); wl(0xAEC, h); }       /* AppParmHandle: no documents */
    wl(0xA02, 0x00010001);                                  /* OneOne */
    wl(0xA06, 0xFFFFFFFF);                                  /* MinusOne */
    wl(0x2B6, 0);                                           /* ExpandMem */
    wl(0x31A, 0x00FFFFFFu);                                 /* Lo3Bytes (StripAddress glue masks with it) */
    wl(0x118, HEAP_BASE); wl(0x2A6, HEAP_BASE);             /* TheZone, SysZone */
    wb(0x2F8, 0xFF);                                        /* ScrDmpEnb */
    wb(0x260, 7);                                           /* SdVolume: the game stays silent at 0 */
    ww(0x21E, 1);                                           /* KbdType */
}

int main(int argc, char **argv){
    char dir[600], app[1024], sys[1024], save[700], overlay[800]; int scale = 2;
    const char *arg = 0; int i;
    for (i = 1; i < argc; i++) if (argv[i][0] != '-') arg = argv[i];          /* macOS adds -psn_... */
    if (!find_game(arg, dir, app, sys) && !ask_game_folder(dir, app, sys)) return 2;
    if (getenv(HLE_ENV("SCALE"))) scale = atoi(getenv(HLE_ENV("SCALE")));
    if (scale < 1) scale = 1;
    g_trace = getenv(HLE_ENV("TRACE")) != 0;
#ifdef _WIN32
    snprintf(save, sizeof save, "%s\\..\\save", dir);                           /* portable: next to the game folder */
    {   FILE *t; char probe[800]; plat_mkdir(save); snprintf(probe, sizeof probe, "%s\\.probe", save);
        if ((t = fopen(probe, "wb")) != 0){ fclose(t); remove(probe); }
        else { plat_user_dir(save, sizeof save); strncat(save, "\\save", sizeof save - strlen(save) - 1); } }
#else
    plat_user_dir(save, sizeof save); strncat(save, "/save", sizeof save - strlen(save) - 1);
#endif
    if (getenv(HLE_ENV("SAVE"))) snprintf(save, sizeof save, "%s", getenv(HLE_ENV("SAVE")));
    fs_init(save, dir);
    snprintf(overlay, sizeof overlay, "%s" PSEP HLE_SETTINGS_OVERLAY, save);

    m68k_init(); m68k_set_cpu_type(HLE_CPU);
    m68k_pulse_reset();
    setreg(M68K_REG_SR, 0x2700);
    setreg(M68K_REG_SP, STACK_TOP); setreg(M68K_REG_ISP, STACK_TOP);
    trap_init();
    mm_init();
    init_lowmem();

    if (!rs_open_system(sys)) fatal("Could not read the System file %s.", sys);
    if (!rs_open_app(app, overlay)) fatal("Could not read %s.", app);
    load_segments();

    host_init(g_scr_w, g_scr_h, scale, HLE_APP_NAME);
    {   extern void host_set_icon(const unsigned char *icl8, const unsigned char *icn);
        unsigned icn = rs_get(0x49434E23u, 128), icl8 = rs_get(0x69636C38u, 128);       /* 'ICN#', 'icl8' */
        host_set_icon(icl8 && mm_handlesize(icl8) >= 1024 ? g_ram + MA(rl(icl8)) : 0, icn && mm_handlesize(icn) >= 256 ? g_ram + MA(rl(icn)) : 0); }
    audio_init(22254);
    g_t0 = host_ms();
    memset(g_ram + SCREEN_BASE, 0, (size_t)g_scr_rb * (size_t)g_scr_h);
    hle_dirty();

    {   long maxslices = getenv(HLE_ENV("MAXSLICES")) ? atol(getenv(HLE_ENV("MAXSLICES"))) : 0, n = 0;
        while (!g_quit){
            m68k_execute(100000);
            hle_idle(0);
            if (maxslices && ++n >= maxslices) break;
        }
    }
    rs_flush_all();
    if (getenv(HLE_ENV("DUMP"))) dump_screen(getenv(HLE_ENV("DUMP")));
    audio_shutdown();
    return 0;
}
