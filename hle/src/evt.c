/* Event Manager: Windows input becomes Mac events in an OS event queue; activate
 * events (from the Window Manager) come first and update events are generated from
 * the windows' update regions, in the same priority order as a Mac.
 */
#include "mac.h"

typedef struct { int what; unsigned msg, when; int h, v, mods; } Ev;
#define QMAX 128
static Ev g_q[QMAX]; static int g_qn;
static Ev g_act[8]; static int g_actn;
static int g_mh, g_mv;                         /* last known mouse (global) */

static void q_push(Ev *e){ if (g_qn < QMAX) g_q[g_qn++] = *e; }
static void q_del(int i){ int k; for (k = i; k < g_qn - 1; k++) g_q[k] = g_q[k+1]; g_qn--; }

static void pump(void){
    HostEvent he; Ev e; int down;
    host_pump();
    host_mouse(&g_mh, &g_mv, &down);
    while (host_next_event(&he)){
        memset(&e, 0, sizeof e);
        e.when = rl(LM_Ticks); e.h = he.h; e.v = he.v; e.mods = he.mods;
        switch (he.type){
        case HE_MOUSEDOWN: e.what = 1; break;
        case HE_MOUSEUP:   e.what = 2; break;
        case HE_KEYDOWN:   e.what = 3; e.msg = (unsigned)(he.mac_key << 8 | (he.ch & 0xFF)); break;
        case HE_AUTOKEY:   e.what = 5; e.msg = (unsigned)(he.mac_key << 8 | (he.ch & 0xFF)); break;
        case HE_KEYUP:     e.what = 4; e.msg = (unsigned)(he.mac_key << 8 | (he.ch & 0xFF)); break;
        case HE_QUIT:      g_quit = 1; continue;
        default: continue;
        }
        q_push(&e);
    }
}

void evt_post(int what, unsigned msg, int h, int v, int mods){
    Ev e; memset(&e, 0, sizeof e);
    e.what = what; e.msg = msg; e.when = rl(LM_Ticks);
    e.h = h ? h : g_mh; e.v = v ? v : g_mv; e.mods = mods | host_modifiers();
    if (what == 8){ if (g_actn < 8) g_act[g_actn++] = e; return; }
    q_push(&e);
}
void evt_flush(int mask, int stop){
    int i;
    (void)stop;
    pump();
    for (i = 0; i < g_qn; ){ if (mask & (1 << g_q[i].what)) q_del(i); else i++; }
}

static void fill(unsigned rec, Ev *e){
    int down; host_mouse(&g_mh, &g_mv, &down);
    if (e->what && getenv(HLE_ENV("EVLOG"))) logf_("[ev] what=%d msg=%08X at %d,%d mods=%04X\n", e->what, e->msg, e->h, e->v, e->mods);
    ww(rec, (unsigned)e->what); wl(rec + 2, e->msg); wl(rec + 6, e->when);
    ww(rec + 10, (unsigned)e->v); ww(rec + 12, (unsigned)e->h);
    ww(rec + 14, (unsigned)(e->mods | (down ? 0 : 0x80)));
}

int evt_get(int mask, unsigned rec, int remove){
    int i; Ev e; unsigned w;
    pump();
    if ((mask & (1 << 8)) && g_actn){
        e = g_act[0];
        if (remove){ for (i = 0; i < g_actn - 1; i++) g_act[i] = g_act[i+1]; g_actn--; }
        fill(rec, &e); return 1;
    }
    for (i = 0; i < g_qn; i++) if (mask & (1 << g_q[i].what)){
        e = g_q[i]; if (remove) q_del(i);
        fill(rec, &e); return 1;
    }
    if ((mask & (1 << 6)) && wm_update_pending(&w)){
        memset(&e, 0, sizeof e); e.what = 6; e.msg = w; e.when = rl(LM_Ticks); e.h = g_mh; e.v = g_mv; e.mods = host_modifiers();
        fill(rec, &e); return 1;
    }
    memset(&e, 0, sizeof e); e.when = rl(LM_Ticks); e.h = g_mh; e.v = g_mv; e.mods = host_modifiers();
    fill(rec, &e);
    return 0;
}

/* tracking helpers: a queued mouse-up means the button is already up */
int evt_mouse_down(void){
    int i, h, v, down;
    pump();
    for (i = 0; i < g_qn; i++) if (g_q[i].what == 2){ g_mh = g_q[i].h; g_mv = g_q[i].v; q_del(i); return 0; }
    host_mouse(&h, &v, &down);
    return down;
}
void evt_mouse(int *h, int *v){ int down; host_mouse(&g_mh, &g_mv, &down); *h = g_mh; *v = g_mv; }

void evt_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv;
    t->handled = 1;
    switch (word){
    case 0xA970: {                                                                /* GetNextEvent(mask, VAR ev) */
        int got = evt_get(ARGUW(4), ARGL(0), 1);
        if (!got) hle_idle(1);
        RETB(6, got); } break;
    case 0xA971: RETB(6, evt_get(ARGUW(4), ARGL(0), 0)); break;                   /* EventAvail */
    case 0xA860: {                                                                /* WaitNextEvent(mask, VAR ev, sleep, rgn) */
        int mask = ARGUW(12); unsigned ev = ARGL(8), sleep = ARGL(4), end = rl(LM_Ticks) + (sleep > 60 ? 60 : sleep);
        int got;
        while (!(got = evt_get(mask, ev, 1)) && rl(LM_Ticks) < end && !g_quit) hle_idle(5);
        if (!got) hle_idle(1);
        RETB(14, got); } break;
    case 0xA972: {                                                                /* GetMouse(VAR pt) local */
        unsigned pt = ARGL(0), P = qd_port(); int h, v; Rect b;
        evt_mouse(&h, &v);
        b = P ? rect_at(P + GP_portBits + 6) : mkrect(0,0,0,0);
        ww(pt, (unsigned)(v + b.top)); ww(pt + 2, (unsigned)(h + b.left)); POP(4); } break;
    case 0xA974: { int h, v, d; pump(); host_mouse(&h, &v, &d); RETB(0, d); } break;   /* Button */
    case 0xA973: {                                                                /* StillDown */
        int i, up = 0, h, v, d; pump();
        for (i = 0; i < g_qn; i++) if (g_q[i].what == 1 || g_q[i].what == 2){ up = 1; break; }
        host_mouse(&h, &v, &d);
        RETB(0, d && !up); } break;
    case 0xA977: RETB(0, evt_mouse_down()); break;                                /* WaitMouseUp */
    case 0xA976: { unsigned char km[16]; host_keymap(km); memcpy(g_ram + MA(ARGL(0)), km, 16); POP(4); } break;   /* GetKeys */
    case 0xA9C3: RETL(8, ARGUW(0) & 0xFF); break;                                 /* KeyTrans */
    default: t->handled = 0;
    }
}
