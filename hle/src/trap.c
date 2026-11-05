/* A-line trap dispatch, 68k callbacks, the OS memory/utility traps and the misc
 * Toolbox traps that belong to no manager.
 *
 * The A-line vector points at MAGIC_ALINE, which holds an RTE. The instruction
 * hook sees the CPU arrive there, decodes the trap word from the stacked PC,
 * runs the handler, and edits the exception frame (return PC, popped Pascal
 * arguments, condition codes) so the RTE resumes the game after the trap.
 */
#include "mac.h"
#include "plat.h"
#include <stdarg.h>
#include <time.h>

int g_quit = 0;
int g_trace = 0;
extern int m68ki_remaining_cycles, m68ki_initial_cycles;

void logf_(const char *fmt, ...){ va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fflush(stderr); }
void fatal(const char *fmt, ...){
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    fprintf(stderr, "FATAL: %s\n", b); host_message(HLE_APP_NAME, b); exit(1);
}
unsigned app_a5(void){ return rl(LM_CurrentA5); }

/* ---- unimplemented-trap bookkeeping ------------------------------------------- */
static int g_unimpl_seen[0x1000];
static void unimpl(unsigned word, unsigned pc){
    if (!g_unimpl_seen[word & 0xFFF]++)
        logf_("[trap] unimplemented %04X at %06X\n", word, pc);
}

/* ---- callbacks: run a 68k Pascal procedure/function to completion ---------------- */
static int g_cb_depth = 0, g_cb_done[64];
unsigned call68k(unsigned addr, int resbytes, const char *sig, ...){
    static unsigned char *ctx[64]; va_list ap; unsigned sp, res = 0, resaddr = 0;
    int rem = m68ki_remaining_cycles, init = m68ki_initial_cycles, d, guard = 0;
    if (!addr || g_cb_depth >= 63) return 0;
    d = ++g_cb_depth;
    if (!ctx[d]) ctx[d] = (unsigned char*)malloc(m68k_context_size());
    m68k_get_context(ctx[d]);
    sp = (reg(M68K_REG_SP) - 128) & ~1u;
    if (resbytes){ sp -= (unsigned)((resbytes + 1) & ~1); resaddr = sp; wl(sp, 0); }
    va_start(ap, sig);
    for (; *sig; sig++){
        unsigned v = va_arg(ap, unsigned);
        if (*sig == 'L'){ sp -= 4; wl(sp, v); }
        else if (*sig == 'W'){ sp -= 2; ww(sp, v); }
        else if (*sig == 'B'){ sp -= 2; ww(sp, (v & 0xFF) << 8); }
    }
    va_end(ap);
    sp -= 4; wl(sp, MAGIC_RET);
    setreg(M68K_REG_SP, sp); setreg(M68K_REG_PC, addr);
    g_cb_done[d] = 0;
    while (!g_cb_done[d] && !g_quit){
        m68k_execute(200000);
        if (!g_cb_done[d] && ++guard % 50 == 0) hle_idle(0);
    }
    if (resbytes == 4) res = rl(resaddr);
    else if (resbytes == 2) res = rw(resaddr);
    else if (resbytes == 1) res = rb(resaddr);
    m68k_set_context(ctx[d]);
    m68ki_remaining_cycles = rem; m68ki_initial_cycles = init;
    g_cb_depth--;
    return res;
}

/* ---- the OS traps that are not File Manager ------------------------------------ */
static void set_d0(int v){ setreg(M68K_REG_D0, (unsigned)v); }

static unsigned thunk_addr(int toolbox, unsigned idx){
    return THUNK_BASE + (toolbox ? 0x1000u : 0u) + (idx * 4u);
}

static void os_trap(unsigned word, unsigned pc){
    unsigned a0 = reg(M68K_REG_A0), a1 = reg(M68K_REG_A1), d0 = reg(M68K_REG_D0);
    int clear = (word & 0x200) != 0;
    switch (word & 0xFF){
    case 0x1E: { unsigned p = mm_newptr(d0, clear); setreg(M68K_REG_A0, p); set_d0(p ? 0 : -108); } break;   /* NewPtr */
    case 0x1F: mm_disposptr(a0); set_d0(0); break;                                                         /* DisposPtr */
    case 0x20: set_d0(mm_ptrsize(a0) >= d0 ? 0 : -108); if (mm_ptrsize(a0) >= d0) wl(MA(a0)-8, d0); break;   /* SetPtrSize */
    case 0x21: set_d0((int)mm_ptrsize(a0)); break;                                                          /* GetPtrSize */
    case 0x22: { unsigned h = mm_newhandle(d0, clear); setreg(M68K_REG_A0, h); set_d0(h ? 0 : -108); } break; /* NewHandle */
    case 0x23: mm_disposhandle(a0); set_d0(0); break;                                                      /* DisposHandle */
    case 0x24: set_d0(mm_sethandlesize(a0, d0)); break;                                                     /* SetHandleSize */
    case 0x25: set_d0(mm_is_handle(a0) ? (int)mm_handlesize(a0) : -109); break;                             /* GetHandleSize */
    case 0x26: setreg(M68K_REG_A0, rl(LM_ApplZone)); set_d0(0); break;                                     /* HandleZone */
    case 0x27: mm_sethandlesize(a0, d0); set_d0(0); break;                                                  /* ReallocHandle */
    case 0x28: setreg(M68K_REG_A0, mm_recover(a0)); set_d0(0); break;                                      /* RecoverHandle */
    case 0x29: if (mm_is_handle(a0)) wb(a0, rb(a0) | 0x80); set_d0(0); break;                               /* HLock */
    case 0x2A: if (mm_is_handle(a0)) wb(a0, rb(a0) & 0x7F); set_d0(0); break;                               /* HUnlock */
    case 0x2B: if (mm_is_handle(a0)){ unsigned b = MA(rl(a0)); if (b) mm_sethandlesize(a0, 0); } set_d0(0); break;  /* EmptyHandle */
    case 0x2C: case 0x2D: case 0x36: case 0x4B: case 0x57: case 0x63: case 0x64: case 0x4D:
        set_d0(0); break;                       /* InitApplZone SetApplLimit MoreMasters SetGrowZone SetApplBase MaxApplZone MoveHHi PurgeMem */
    case 0x2E: if (d0 && d0 < MEM_SIZE) memmove(g_ram + MA(a1), g_ram + MA(a0), d0); set_d0(0); break;     /* BlockMove */
    case 0x1C: case 0x1D: set_d0((int)mm_freemem()); if ((word&0xFF)==0x1D) setreg(M68K_REG_A0, mm_freemem()); break;  /* FreeMem MaxMem */
    case 0x4C: set_d0((int)mm_freemem()); break;                                                            /* CompactMem */
    case 0x62: set_d0((int)mm_freemem()); setreg(M68K_REG_A0, mm_freemem()); break;                         /* PurgeSpace */
    case 0x49: if (mm_is_handle(a0)) wb(a0, rb(a0) | 0x40); set_d0(0); break;                               /* HPurge */
    case 0x4A: if (mm_is_handle(a0)) wb(a0, rb(a0) & 0xBF); set_d0(0); break;                               /* HNoPurge */
    case 0x69: set_d0(mm_is_handle(a0) ? (int)(rb(a0) & 0xE0) : 0); break;                                  /* HGetState */
    case 0x6A: if (mm_is_handle(a0)) wb(a0, (rb(a0) & 0x1F) | (d0 & 0xE0)); set_d0(0); break;               /* HSetState */
    case 0x67: if (mm_is_handle(a0)) wb(a0, rb(a0) | 0x20); set_d0(0); break;                               /* HSetRBit */
    case 0x68: if (mm_is_handle(a0)) wb(a0, rb(a0) & 0xDF); set_d0(0); break;                               /* HClrRBit */
    case 0x1A: setreg(M68K_REG_A0, rl(LM_ApplZone)); set_d0(0); break;                                     /* GetZone */
    case 0x1B: case 0x19: set_d0(0); break;                                                                 /* SetZone InitZone */
    case 0x55: set_d0((int)(d0 & 0x00FFFFFFu)); break;                                                      /* StripAddress */
    case 0x2F: evt_post((int)(a0 & 0xFFFF), d0, 0, 0, 0); set_d0(0); break;                                 /* PostEvent */
    case 0x32: { extern void evt_flush(int mask, int stop); evt_flush((int)(d0 & 0xFFFF), (int)(d0 >> 16)); set_d0(0); } break;  /* FlushEvents */
    case 0x30: case 0x31: set_d0(evt_get((int)(d0 & 0xFFFF), a0, (word & 0xFF) == 0x31) ? 0 : -1); break;   /* OSEventAvail GetOSEvent */
    case 0x3B: { unsigned end = rl(LM_Ticks) + a0; while (rl(LM_Ticks) < end && !g_quit) hle_idle(5);       /* Delay */
                 set_d0((int)rl(LM_Ticks)); } break;
    case 0x39: wl(MA(a0), rl(LM_Time)); set_d0(0); break;                                                   /* ReadDateTime */
    case 0x3A: set_d0(0); break;                                                                            /* SetDateTime */
    case 0x3C: { int n1 = (int)(d0 >> 16), n2 = (int)(d0 & 0xFFFF), eq = n1 == n2, i;                     /* CmpString */
                 for (i = 0; eq && i < n1; i++){ int c1 = rb(a0+i), c2 = rb(a1+i);
                     if (!(word & 0x200)){ if (c1>='a'&&c1<='z') c1-=32; if (c2>='a'&&c2<='z') c2-=32; }
                     eq = c1 == c2; }
                 set_d0(eq ? 0 : 1); } break;
    case 0x46: {                                                                                            /* GetTrapAddress */
        unsigned n = d0 & 0xFFFF; int tb;
        if (word & 0x200) tb = (word & 0x400) != 0;
        else tb = (n & 0x800) || ((n & 0x3FF) >= 0x50 && !(n & 0xF000));
        if (n >= 0xA000) tb = (n & 0x800) != 0;
        if (tb && (n & 0x3FF) == 0x09F) setreg(M68K_REG_A0, thunk_addr(1, 0x9F));
        else setreg(M68K_REG_A0, thunk_addr(tb, tb ? (n & 0x3FF) : (n & 0xFF)));
        set_d0(0); } break;
    case 0x47: set_d0(0); break;                                                                            /* SetTrapAddress */
    case 0x90: {                                                                                            /* SysEnvirons */
        unsigned r = a0; memset(g_ram + MA(r), 0, 16);
        ww(r, 1); ww(r+2, 6); ww(r+4, 0x0608); ww(r+6, 3); wb(r+8, 0); wb(r+9, 0); ww(r+10, 5); ww(r+12, 0); ww(r+14, (unsigned)-1);
        set_d0(0); } break;
    case 0x33: case 0x34: set_d0(0); break;                                                                 /* VInstall VRemove */
    case 0x58: case 0x59: case 0x5A: case 0x5B: set_d0(0); break;                                         /* Time Manager */
    case 0x93: case 0x98: set_d0(0); break;
    default:
        if ((word & 0xFF) <= 0x18 || ((word & 0xFF) >= 0x41 && (word & 0xFF) <= 0x45) || (word & 0xFF) == 0x60 || (word & 0xFF) == 0x06)
            fs_os_trap(word);
        else { unimpl(word, pc); set_d0(0); }
    }
}

/* ---- misc Toolbox traps ---------------------------------------------------------- */
static void misc_trap(unsigned word, Trap *t){
    t->handled = 1;
    switch (word){
    case 0xA975: {                                                                 /* TickCount */
        static unsigned last_t, spins;                    /* busy-waiting on the clock: give the time back */
        unsigned now = rl(LM_Ticks);
        if (now == last_t){ if (++spins > 8) hle_idle(1); } else { spins = 0; last_t = now; }
        RETL(0, rl(LM_Ticks)); } break;
    case 0xA9C8: host_beep(); POP(2); break;                                       /* SysBeep */
    case 0xA9F0: POP(0); break;                                                    /* LoadSeg: segments are preloaded */
    case 0xA9F1: POP(4); break;                                                    /* UnloadSeg */
    case 0xA9F4: g_quit = 1; m68k_end_timeslice(); break;                          /* ExitToShell */
    case 0xA9F5: { unsigned nm = ARGL(8); if (nm) pstr_put(nm, HLE_APP_NAME);   /* GetAppParms */
                   if (ARGL(4)) ww(ARGL(4), 2); if (ARGL(0)) wl(ARGL(0), rl(0xAEC)); POP(12); } break;
    case 0xA9B4: hle_idle(0); POP(0); break;                                       /* SystemTask */
    case 0xA9B3: POP(8); break;                                                    /* SystemClick */
    case 0xA9B2: RETB(4, 0); break;                                                /* SystemEvent */
    case 0xA9C2: RETB(2, 0); break;                                                /* SystemEdit */
    case 0xA9B5: POP(2); break;                                                    /* SystemMenu */
    case 0xA9B6: RETW(4, 0); break;                                                /* OpenDeskAcc */
    case 0xA9B7: POP(2); break;                                                    /* CloseDeskAcc */
    case 0xA9E5: POP(2); break;                                                    /* InitPack */
    case 0xA9E6: POP(0); break;                                                    /* InitAllPacks */
    case 0xA9FF: logf_("[Debugger]\n"); POP(0); break;
    case 0xABFF: { char s[256]; pstr_get(ARGL(0), s, sizeof s); logf_("[DebugStr] %s\n", s); POP(4); } break;
    case 0xA867: { long long r = (long long)(int)ARGL(4) * (int)ARGL(0); wl(ARGL(8), (unsigned)(r >> 32)); wl(ARGL(8)+4, (unsigned)r); POP(12); } break;  /* LongMul */
    case 0xA868: { long long r = ((long long)(int)ARGL(4) * (int)ARGL(0)) >> 16; RETL(8, (unsigned)r); } break;   /* FixMul */
    case 0xA869: { int n = ARGW(2), dd = ARGW(0); int r = dd ? (int)(((long long)n << 16) / dd) : (n >= 0 ? 0x7FFFFFFF : (int)0x80000000);  /* FixRatio */
                   RETL(4, (unsigned)r); } break;
    case 0xA86A: RETW(4, ARGL(0) >> 16); break;                                    /* HiWord */
    case 0xA86B: RETW(4, ARGL(0) & 0xFFFF); break;                                 /* LoWord */
    case 0xA86C: { int f = (int)ARGL(0); RETW(4, (unsigned)((f + 0x8000) >> 16)); } break;   /* FixRound */
    case 0xA84D: { int f = (int)ARGL(4), d = (int)ARGL(0); RETL(8, d ? (unsigned)(((long long)f << 16) / d) : 0x7FFFFFFFu); } break;   /* FixDiv */
    case 0xA85D: { unsigned p = ARGL(4), n = ARGL(0); RETB(8, (rb(p + (n>>3)) >> (7-(n&7))) & 1); } break;   /* BitTst */
    case 0xA85E: { unsigned p = ARGL(4), n = ARGL(0); wb(p+(n>>3), rb(p+(n>>3)) | (0x80>>(n&7))); POP(8); } break;   /* BitSet */
    case 0xA85F: { unsigned p = ARGL(4), n = ARGL(0); wb(p+(n>>3), rb(p+(n>>3)) & ~(0x80>>(n&7))); POP(8); } break;  /* BitClr */
    case 0xA858: RETL(8, ARGL(4) & ARGL(0)); break;                                /* BitAnd */
    case 0xA859: RETL(8, ARGL(4) ^ ARGL(0)); break;                                /* BitXor */
    case 0xA85A: RETL(4, ~ARGL(0)); break;                                         /* BitNot */
    case 0xA85B: RETL(8, ARGL(4) | ARGL(0)); break;                                /* BitOr */
    case 0xA85C: { unsigned v = ARGL(2); int n = ARGW(0); RETL(6, n >= 0 ? v << (n & 31) : v >> ((-n) & 31)); } break;   /* BitShift */
    case 0xA906: { unsigned s = ARGL(0), h = mm_newhandle(rb(s) + 1u, 0);           /* NewString */
                   if (h) memcpy(g_ram + MA(rl(h)), g_ram + MA(s), rb(s) + 1u); RETL(4, h); } break;
    case 0xA907: { unsigned h = ARGL(4), s = ARGL(0);                              /* SetString */
                   if (mm_is_handle(h)){ mm_sethandlesize(h, rb(s) + 1u); memcpy(g_ram + MA(rl(h)), g_ram + MA(s), rb(s) + 1u); } POP(8); } break;
    case 0xA9BA: RETL(2, rs_get(0x53545220u, ARGW(0))); break;                     /* GetString 'STR ' */
    case 0xA9B9: RETL(2, rs_get(0x43555253u, ARGW(0))); break;                     /* GetCursor 'CURS' */
    case 0xA9B8: RETL(2, rs_get(0x50415420u, ARGW(0))); break;                     /* GetPattern 'PAT ' */
    case 0xA9BB: RETL(2, rs_get(0x49434F4Eu, ARGW(0))); break;                     /* GetIcon 'ICON' */
    case 0xA9BC: RETL(2, rs_get(0x50494354u, ARGW(0))); break;                     /* GetPicture 'PICT' */
    case 0xA850: host_cursor(NULL, 1); { extern int g_cursor_level; g_cursor_level = 0; } POP(0); break;   /* InitCursor */
    case 0xA851: host_cursor(g_ram + MA(ARGL(0)), -1); POP(4); break;              /* SetCursor */
    case 0xA852: { extern int g_cursor_level; g_cursor_level--; host_cursor(0, g_cursor_level >= 0 ? 1 : 0); } POP(0); break;   /* HideCursor */
    case 0xA853: { extern int g_cursor_level; if (g_cursor_level < 0) g_cursor_level++; host_cursor(0, g_cursor_level >= 0 ? 1 : 0); } POP(0); break;  /* ShowCursor */
    case 0xA856: POP(0); break;                                                    /* ObscureCursor */
    case 0xA855: POP(8); break;                                                    /* ShieldCursor */
    case 0xA9E0: { unsigned h = ARGL(4), off = ARGL(0); (void)h; (void)off; RETL(8, 0); } break;   /* Munger stub */
    default: t->handled = 0;
    }
}

/* register-based "Toolbox" utilities (A0/D0 conventions) */
static int reg_tool_trap(unsigned word){
    unsigned a0 = reg(M68K_REG_A0), a1 = reg(M68K_REG_A1), d0 = reg(M68K_REG_D0);
    switch (word & 0xFBFF){
    case 0xA9E1: { unsigned n = mm_handlesize(a0), h = mm_newhandle(n, 0);          /* HandToHand */
                   if (h) memcpy(g_ram + MA(rl(h)), g_ram + MA(rl(a0)), n);
                   setreg(M68K_REG_A0, h); set_d0(h ? 0 : -108); } return 1;
    case 0xA9E3: { unsigned h = mm_newhandle(d0, 0);                                /* PtrToHand */
                   if (h) memcpy(g_ram + MA(rl(h)), g_ram + MA(a0), d0);
                   setreg(M68K_REG_A0, h); set_d0(h ? 0 : -108); } return 1;
    case 0xA9E2: { int e = mm_sethandlesize(a1, d0);                                /* PtrToXHand */
                   if (!e) memcpy(g_ram + MA(rl(a1)), g_ram + MA(a0), d0);
                   setreg(M68K_REG_A0, a1); set_d0(e); } return 1;
    case 0xA9E4: { unsigned n1 = mm_handlesize(a0), n2 = mm_handlesize(a1); int e = mm_sethandlesize(a1, n1 + n2);   /* HandAndHand */
                   if (!e) memcpy(g_ram + MA(rl(a1)) + n2, g_ram + MA(rl(a0)), n1);
                   setreg(M68K_REG_A0, a1); set_d0(e); } return 1;
    case 0xA9C6: { time_t tt = (time_t)d0 - 2082844800; struct tm *m = gmtime(&tt);          /* Secs2Date(D0 secs, A0 rec) */
                   if (m){ ww(a0, (unsigned)(m->tm_year + 1900)); ww(a0+2, (unsigned)(m->tm_mon + 1)); ww(a0+4, (unsigned)m->tm_mday);
                           ww(a0+6, (unsigned)m->tm_hour); ww(a0+8, (unsigned)m->tm_min); ww(a0+10, (unsigned)m->tm_sec); ww(a0+12, (unsigned)(m->tm_wday + 1)); }
                   return 1; }
    case 0xA9C7: { struct tm m; memset(&m, 0, sizeof m);                                       /* Date2Secs(A0 rec) -> D0 */
                   m.tm_year = sw(a0) - 1900; m.tm_mon = sw(a0+2) - 1; m.tm_mday = sw(a0+4); m.tm_hour = sw(a0+6); m.tm_min = sw(a0+8); m.tm_sec = sw(a0+10);
                   setreg(M68K_REG_D0, (unsigned)(plat_timegm(&m) + 2082844800)); return 1; }
    case 0xA9EF: { unsigned n2 = mm_handlesize(a1); int e = mm_sethandlesize(a1, n2 + d0);   /* PtrAndHand */
                   if (!e) memcpy(g_ram + MA(rl(a1)) + n2, g_ram + MA(a0), d0);
                   setreg(M68K_REG_A0, a1); set_d0(e); } return 1;
    }
    return 0;
}

static void toolbox_trap(unsigned word, Trap *t){
    unsigned base = word & 0xFBFF;
    t->word = base;
    misc_trap(base, t);                    if (t->handled) return;
    qd_trap(base, t);                      if (t->handled) return;
    wm_trap(base, t);                      if (t->handled) return;
    menu_trap(base, t);                    if (t->handled) return;
    dlg_trap(base, t);                     if (t->handled) return;
    ctl_trap(base, t);                     if (t->handled) return;
    evt_trap(base, t);                     if (t->handled) return;
    rs_trap(base, t);                      if (t->handled) return;
    pack_trap(base, t);                    if (t->handled) return;
    unimpl(word, t->pc);
}

/* ---- the instruction hook -------------------------------------------------------- */
static unsigned g_tracebuf[64], g_tracepc[64]; static int g_tracen;
static void on_aline(void){
    unsigned sp = reg(M68K_REG_SP), sr = rw(sp), pc = rl(sp+2), word = rw(pc), ret = pc + 2;
    g_tracebuf[g_tracen & 63] = word; g_tracepc[g_tracen++ & 63] = pc;
    if (g_trace) logf_("[T %04X @%06X sp=%06X]\n", word, pc, sp + 8);
    if (!(word & 0x0800)){                                     /* OS trap: registers, TST.W D0 on return */
        os_trap(word, pc);
        { int d0 = (short)reg(M68K_REG_D0); sr &= ~0x0Fu; if (!d0) sr |= 4; if (d0 < 0) sr |= 8; ww(sp, sr); }
        wl(sp+2, ret);
        return;
    }
    if (reg_tool_trap(word)){
        int d0 = (short)reg(M68K_REG_D0); sr &= ~0x0Fu; if (!d0) sr |= 4; if (d0 < 0) sr |= 8; ww(sp, sr);
        wl(sp+2, ret); return;
    }
    {
        Trap t; unsigned nsp; int i;
        t.word = word; t.pc = pc; t.sp = sp + 8; t.pop = 0; t.handled = 0;
        toolbox_trap(word, &t);
        if (g_quit) return;
        sp = reg(M68K_REG_SP);                                 /* unchanged unless a callback misbehaved */
        nsp = sp + (unsigned)t.pop;
        if (word & 0x400){ ret = rl(t.sp + (unsigned)t.pop); nsp += 4; }   /* auto-pop: drop the caller's return */
        if (nsp != sp){
            unsigned char fr[8]; for (i = 0; i < 8; i++) fr[i] = (unsigned char)rb(sp + (unsigned)i);
            for (i = 0; i < 8; i++) wb(nsp + (unsigned)i, fr[i]);
            setreg(M68K_REG_SP, nsp);
        }
        wl(nsp+2, ret);
    }
}

static unsigned g_lastgood;
static void instr_hook(unsigned int pc){
    if (pc == MAGIC_ALINE){ on_aline(); return; }
    if (pc == MAGIC_RET){ if (g_cb_depth > 0) g_cb_done[g_cb_depth] = 1; m68k_end_timeslice(); return; }
    if (pc < 0x3000 || pc >= MEM_SIZE || (pc >= STACK_TOP - 0x100000 && pc < A5_BASE)){
        int i; char dis[128];
        m68k_disassemble(dis, g_lastgood, M68K_CPU_TYPE_68020);
        logf_("\n*** bad jump to %08X (last good %06X: %s)\nrecent traps:", pc, g_lastgood, dis);
        for (i = g_tracen - 24; i < g_tracen; i++) if (i >= 0) logf_(" %04X@%06X", g_tracebuf[i&63], g_tracepc[i&63]);
        logf_("\nA5=%06X SP=%06X A6=%06X A7(0..3)=%08X %08X %08X %08X", reg(M68K_REG_A5), reg(M68K_REG_SP), reg(M68K_REG_A6),
              rl(reg(M68K_REG_SP)), rl(reg(M68K_REG_SP)+4), rl(reg(M68K_REG_SP)+8), rl(reg(M68K_REG_SP)+12));
        logf_("\n");
        fatal("The game jumped to an invalid address (%08X). See the log for details.", pc);
    }
    g_lastgood = pc;
    {   /* HLE_PCLOG=trapcount,instructions: log every instruction after that many traps */
        static long from = -2, n = 0;
        if (from == -2){ const char *e = getenv(HLE_ENV("PCLOG")); from = e ? atol(e) : -1; n = e && strchr(e, ',') ? atol(strchr(e, ',') + 1) : 200; }
        if (from >= 0 && g_tracen >= from && n > 0){ char dis[128]; n--;
            m68k_disassemble(dis, pc, M68K_CPU_TYPE_68020);
            logf_("  %06X  %-40s D0=%08X D1=%08X A0=%08X A1=%08X SP=%06X\n", pc, dis, reg(M68K_REG_D0), reg(M68K_REG_D1), reg(M68K_REG_A0), reg(M68K_REG_A1), reg(M68K_REG_SP)); }
    }
}

void trap_init(void){
    unsigned i;
    for (i = 0; i < 0x400; i++){                     /* GetTrapAddress thunks */
        ww(thunk_addr(1, i), 0xA800 | i); ww(thunk_addr(1, i) + 2, 0x4E75);
        if (i < 0x100){ ww(thunk_addr(0, i), 0xA000 | i); ww(thunk_addr(0, i) + 2, 0x4E75); }
    }
    ww(MAGIC_ALINE, 0x4E73);                         /* RTE */
    ww(MAGIC_RET, 0x4E71); ww(MAGIC_RET + 2, 0x4E71);
    wl(0x28, MAGIC_ALINE);                           /* A-line vector */
    m68k_set_instr_hook_callback(instr_hook);
}
