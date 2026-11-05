/* Packages and leftovers: Standard File (Pack 3) through Windows file dialogs, SANE
 * (Pack 4, the environment calls the runtime makes), Binary-Decimal (Pack 7),
 * the Printing Manager (reports printing as unavailable) and sound: the Sound
 * Manager's SndPlay and the old Sound Driver's free-form synthesizer.
 */
#include "mac.h"
#include "plat.h"
#include "audio.h"
#include <math.h>

#define OUT_RATE 22254

/* ---- sound ------------------------------------------------------------------------- */
static void play_samples(const unsigned char *s, int n, double rate){
    /* resample 8-bit unsigned mono to the output rate */
    static unsigned char buf[1 << 20]; int i, m = (int)((double)n * OUT_RATE / rate);
    if (m > (int)sizeof buf) m = (int)sizeof buf;
    for (i = 0; i < m; i++){ int k = (int)((double)i * rate / OUT_RATE); buf[i] = s[k < n ? k : n - 1]; }
    audio_push(buf, m);
}

/* 'snd ' resource (format 1 or 2) with a standard sampled sound header */
static int play_snd(const unsigned char *d, unsigned len){
    unsigned p = 0, ncmd, i; int fmt;
    if (len < 6) return -1;
    fmt = d[0] << 8 | d[1];
    if (fmt == 1){ unsigned nmod = (unsigned)(d[2] << 8 | d[3]); p = 4 + nmod * 6; }
    else if (fmt == 2) p = 4;
    else return -206;
    ncmd = (unsigned)(d[p] << 8 | d[p+1]); p += 2;
    for (i = 0; i < ncmd; i++, p += 8){
        unsigned cmd = (unsigned)(d[p] << 8 | d[p+1]) & 0x7FFF, off = (unsigned)(d[p+4] << 24 | d[p+5] << 16 | d[p+6] << 8 | d[p+7]);
        if ((cmd == 0x50 || cmd == 0x51) && off + 22 <= len){
            const unsigned char *h = d + off;
            unsigned n = (unsigned)(h[4] << 24 | h[5] << 16 | h[6] << 8 | h[7]);
            unsigned rate = (unsigned)(h[8] << 24 | h[9] << 16 | h[10] << 8 | h[11]);
            if (h[20] != 0) return -206;                    /* only standard 8-bit headers */
            if (off + 22 + n > len) n = len - off - 22;
            play_samples(h + 22, (int)n, rate / 65536.0);
            return 0;
        }
    }
    return -206;
}
void snd_play_handle(unsigned h){ if (h && MA(rl(h))) play_snd(g_ram + MA(rl(h)), mm_handlesize(h)); }

void snd_stop(void){ }

/* free-form synthesizer record: [mode][Fixed rate][8-bit samples] (rate 1.0 = 22257 Hz) */
void snd_driver_write(unsigned pb){
    unsigned buf = rl(pb + 32), n = rl(pb + 36), done = rl(LM_Ticks);
    int mode = sw(buf);
    if (mode == 0 && n > 6){
        double rate = (double)rl(buf + 2) / 65536.0 * 22257.0;
        if (rate < 1000) rate = 22257.0;
        play_samples(g_ram + MA(buf + 6), (int)(n - 6), rate);
        done += (unsigned)((n - 6) * 60.0 / rate);
    }
    if (rl(pb + 12)){ extern void hle_defer(unsigned addr, unsigned a0, unsigned due); hle_defer(rl(pb + 12), pb, done); }
}

/* ---- SANE: 80-bit extended <-> double ----------------------------------------------- */
static double ext_get(unsigned a){
    unsigned se = rw(a); unsigned long long m = (unsigned long long)rl(a + 2) << 32 | rl(a + 6);
    int e = (int)(se & 0x7FFF); double v;
    if (!e && !m) return (se & 0x8000) ? -0.0 : 0.0;
    if (e == 0x7FFF) return (m << 1) ? NAN : ((se & 0x8000) ? -INFINITY : INFINITY);
    v = ldexp((double)m, e - 16383 - 63);
    return (se & 0x8000) ? -v : v;
}
static void ext_put(unsigned a, double v){
    int e; double m; unsigned long long mi; int neg = signbit(v) != 0;
    if (v == 0){ ww(a, neg ? 0x8000 : 0); wl(a + 2, 0); wl(a + 6, 0); return; }
    if (isinf(v)){ ww(a, (neg ? 0x8000u : 0) | 0x7FFF); wl(a + 2, 0); wl(a + 6, 0); return; }
    if (isnan(v)){ ww(a, 0x7FFF); wl(a + 2, 0x40000000); wl(a + 6, 0); return; }
    m = frexp(fabs(v), &e);                       /* v = m * 2^e, 0.5 <= m < 1 */
    mi = (unsigned long long)ldexp(m, 64);
    ww(a, (neg ? 0x8000u : 0) | (unsigned)(e - 1 + 16383));
    wl(a + 2, (unsigned)(mi >> 32)); wl(a + 6, (unsigned)mi);
}
static double fmt_get(unsigned a, int fmt){
    switch (fmt){
    case 0: return ext_get(a);
    case 1: { unsigned long long b = (unsigned long long)rl(a) << 32 | rl(a + 4); double d; memcpy(&d, &b, 8); return d; }
    case 2: { unsigned b = rl(a); float f; memcpy(&f, &b, 4); return f; }
    case 4: return (double)(short)rw(a);
    case 5: return (double)(int)rl(a);
    case 6: return (double)(long long)((unsigned long long)rl(a) << 32 | rl(a + 4));
    }
    return 0;
}
static void fmt_put(unsigned a, int fmt, double v){
    switch (fmt){
    case 0: ext_put(a, v); break;
    case 1: { unsigned long long b; memcpy(&b, &v, 8); wl(a, (unsigned)(b >> 32)); wl(a + 4, (unsigned)b); } break;
    case 2: { float f = (float)v; unsigned b; memcpy(&b, &f, 4); wl(a, b); } break;
    case 4: ww(a, (unsigned)(short)floor(v + 0.5)); break;
    case 5: wl(a, (unsigned)(int)floor(v + 0.5)); break;
    case 6: { long long x = (long long)floor(v + 0.5); wl(a, (unsigned)((unsigned long long)x >> 32)); wl(a + 4, (unsigned)x); } break;
    }
}
static unsigned short g_sane_env;
static int fp68k(Trap *t){
    unsigned op = ARGUW(0), dst = ARGL(2), src; int code = (int)(op & 0xFF), fmt = (int)((op >> 11) & 7);
    if (code & 1){
        switch (code){
        case 0x01: g_sane_env = (unsigned short)rw(dst); return 6;                   /* SetEnv */
        case 0x03: ww(dst, g_sane_env); return 6;                                    /* GetEnv */
        case 0x17: ww(dst, g_sane_env); g_sane_env = 0; return 6;                    /* ProcEntry */
        case 0x19: g_sane_env = (unsigned short)rw(dst); return 6;                   /* ProcExit */
        case 0x0D: ext_put(dst, -ext_get(dst)); return 6;                            /* Neg */
        case 0x0F: ext_put(dst, fabs(ext_get(dst))); return 6;                       /* Abs */
        case 0x15: case 0x1B: case 0x05: case 0x07: return 6;
        }
        logf_("[sane] unhandled op %04X\n", op); return 6;
    }
    src = ARGL(6);
    switch (code){
    case 0x00: ext_put(dst, ext_get(dst) + fmt_get(src, fmt)); return 10;
    case 0x02: ext_put(dst, ext_get(dst) - fmt_get(src, fmt)); return 10;
    case 0x04: ext_put(dst, ext_get(dst) * fmt_get(src, fmt)); return 10;
    case 0x06: ext_put(dst, ext_get(dst) / fmt_get(src, fmt)); return 10;
    case 0x0C: ext_put(dst, remainder(ext_get(dst), fmt_get(src, fmt))); return 10;
    case 0x0E: ext_put(dst, fmt_get(src, fmt)); return 10;                           /* Z2X */
    case 0x10: fmt_put(dst, fmt, ext_get(src)); return 10;                           /* X2Z */
    case 0x12: ext_put(dst, sqrt(ext_get(dst))); return 6;
    case 0x14: ext_put(dst, nearbyint(ext_get(dst))); return 6;                       /* RoundToInt */
    case 0x16: ext_put(dst, trunc(ext_get(dst))); return 6;                           /* TruncToInt */
    }
    logf_("[sane] unhandled op %04X\n", op);
    return 10;
}

/* ---- Standard File ------------------------------------------------------------------- */
static void sf_reply(unsigned reply, int good, const char *name){
    memset(g_ram + MA(reply), 0, 74);
    wb(reply, (unsigned)good);
    if (good){ ww(reply + 6, (unsigned)-1); pstr_put(reply + 10, name); }
}
static const char *base_name(const char *p){ const char *s = strrchr(p, '\\'), *t = strrchr(p, '/'); if (t > s) s = t; return s ? s + 1 : p; }
static int sf_dialog(int save, const char *initial, char *name, const char *title){
    char out[700];
    if (!host_file_dialog(save, fs_save_dir(), initial, out, sizeof out, title)) return 0;
    {   /* documents live in the save folder; bring one in from elsewhere */
        char dir[700]; const char *b = base_name(out);
        strncpy(dir, out, sizeof dir - 1); dir[b - out > 0 ? (int)(b - out) - 1 : 0] = 0;
        if (plat_stricmp(dir, fs_save_dir()) != 0 && !save){
            char dst[700]; snprintf(dst, sizeof dst, "%s" PSEP "%s", fs_save_dir(), b);
            plat_copy(out, dst);
        }
        strncpy(name, b, 63); name[63] = 0;
    }
    return 1;
}

/* ---- Printing Manager --------------------------------------------------------------- */
static int prglue(Trap *t){
    unsigned sel = ARGL(0); int params = (int)((sel >> 8) & 0xFF), r = (int)(sel >> 24);
    ww(LM_PrintErr, 128);                               /* iPrAbort: printing is not available */
    switch (r){
    case 0xBA: RETW(4 + params, 128); break;            /* PrError */
    case 0x52: case 0x2A: case 0x32: case 0x4A: RETB(4 + params, 0); break;   /* PrValidate / dialogs */
    case 0x04: case 0x3C: case 0x44: case 0x94: RETL(4 + params, 0); break;
    case 0x9A: RETW(4 + params, 0); break;
    default: POP(4 + params);
    }
    return 1;
}

void pack_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv;
    t->handled = 1;
    switch (word){
    case 0xA9EA: {                                                      /* Pack3 Standard File */
        int sel = ARGW(0);
        if (sel == 1 || sel == 3){                                      /* SFPutFile / SFPPutFile */
            unsigned reply = ARGL(2), orig = ARGL(10), prompt = ARGL(14); char nm[256], pr[256], init[256];
            pstr_get(orig, init, sizeof init); pstr_get(prompt, pr, sizeof pr);
            sf_reply(reply, sf_dialog(1, init, nm, pr), nm);
            POP(sel == 1 ? 22 : 30);
        } else if (sel == 2 || sel == 4){                               /* SFGetFile / SFPGetFile */
            unsigned reply = ARGL(2); char nm[256];
            sf_reply(reply, sf_dialog(0, "", nm, "Open"), nm);
            if (rb(reply)){ char p[700]; FILE *f; snprintf(p, sizeof p, "%s" PSEP ".finf" PSEP "%s", fs_save_dir(), nm);
                if ((f = fopen(p, "rb")) != 0){ unsigned char fi[4]; if (fread(fi, 1, 4, f) == 4) wl(reply + 2, (unsigned)(fi[0]<<24|fi[1]<<16|fi[2]<<8|fi[3])); fclose(f); } }
            POP(sel == 2 ? 28 : 36);
        } else { logf_("[sf] selector %d\n", sel); POP(2); }
        } break;
    case 0xA9EB: POP(fp68k(t)); break;                                  /* Pack4 FP68K */
    case 0xA9EE: {                                                      /* Pack7 */
        int sel = ARGW(0);
        if (sel == 0){ char b[32]; sprintf(b, "%d", (int)reg(M68K_REG_D0)); pstr_put(reg(M68K_REG_A0), b); }
        else if (sel == 1){ char b[256]; pstr_get(reg(M68K_REG_A0), b, sizeof b); setreg(M68K_REG_D0, (unsigned)atol(b)); }
        else logf_("[pack7] selector %d\n", sel);
        POP(2); } break;
    case 0xA8FD: prglue(t); break;                                      /* PrGlue */
    case 0xA805: {                                                      /* SndPlay(chan, snd, async) */
        unsigned h = ARGL(2); int e = -1;
        if (h && MA(rl(h))) e = play_snd(g_ram + MA(rl(h)), mm_handlesize(h));
        RETW(10, e); } break;
    case 0xA807: RETW(14, 0); break;                                    /* SndNewChannel */
    case 0xA801: RETW(6, 0); break;                                     /* SndDisposeChannel */
    case 0xA803: case 0xA804: RETW(10, 0); break;                       /* SndDoCommand / SndDoImmediate */
    case 0xA806: RETW(6, 0); break;                                     /* SndControl */
    case 0xA802: RETW(16, 0); break;                                    /* SndAddModifier */
    default: t->handled = 0;
    }
}
