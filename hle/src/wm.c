/* Window Manager.
 *
 * Windows are real WindowRecords in emulated memory whose ports draw straight into
 * the screen bitmap (portBits = screenBits offset to the window's position). After
 * any change to the window stack, wm_compose() recomputes every window's structure,
 * content and visible regions front to back, repaints the desktop and frames, erases
 * newly exposed content and adds it to the window's updateRgn, so the game gets
 * update events exactly as on a Mac. Standard window kinds are drawn natively; the
 * game's own WDEF (round windows) runs as 68k code.
 */
#include "mac.h"

/* WindowRecord offsets */
#define W_kind     108
#define W_visible  110
#define W_hilited  111
#define W_goAway   112
#define W_strucRgn 114
#define W_contRgn  118
#define W_updRgn   122
#define W_defProc  126
#define W_data     130
#define W_title    134
#define W_titleW   138
#define W_ctlList  140
#define W_next     144
#define W_pic      148
#define W_refCon   152
#define W_SIZE     156
#define DLG_SIZE   170

#define MBAR_H 20

static unsigned g_wmport;                  /* WMgrPort */
static unsigned g_gray;                    /* GrayRgn (desktop) */
typedef struct { unsigned w, oldvis, savevis; int procID; } WInfo;
static WInfo g_wi[64]; static int g_nwi;

static WInfo *winfo(unsigned w){ int i; for (i = 0; i < g_nwi; i++) if (g_wi[i].w == w) return &g_wi[i]; return 0; }
static int is_window(unsigned w){ return w && winfo(w) != 0; }
unsigned wm_first(void){ return rl(LM_WindowList); }
static unsigned wnext(unsigned w){ return rl(w + W_next); }

void wm_global_origin(unsigned port, int *gh, int *gv){ Rect b = rect_at(port + GP_portBits + 6); *gh = -b.left; *gv = -b.top; }
static Rect content_global(unsigned w){ int gh, gv; Rect r = rect_at(w + GP_portRect); wm_global_origin(w, &gh, &gv);
    return rect_off(r, gh - 0, gv - 0); }

unsigned wm_front(void){ unsigned w; for (w = wm_first(); w; w = wnext(w)) if (rb(w + W_visible)) return w; return 0; }

/* ---- native window definitions --------------------------------------------------- */
static int proc_kind(int procID){ return procID & 0xFFF0 ? ((procID >> 4) == 1 ? 16 : -1) : (procID & 0xF); }
static Rect struct_rect(int kind, Rect c){
    switch (kind){
    case 1:  return mkrect(c.top-8, c.left-8, c.bottom+8, c.right+8);
    case 2:  return mkrect(c.top-1, c.left-1, c.bottom+1, c.right+1);
    case 3:  return mkrect(c.top-1, c.left-1, c.bottom+3, c.right+3);
    case 16: return mkrect(c.top-19, c.left-1, c.bottom+1, c.right+1);
    default: return mkrect(c.top-19, c.left-1, c.bottom+2, c.right+2);   /* document: frame + shadow */
    }
}
static int rdoc_diam(int procID){ static const int d[8] = {16,4,6,8,10,12,20,24}; return d[procID & 7]; }

static unsigned wdef_code(unsigned w){ unsigned h = rl(w + W_defProc) & 0x00FFFFFFu; return h ? MA(rl(h)) : 0; }
static int wdef_custom(unsigned w){ WInfo *i = winfo(w); return i && (i->procID >> 4) > 1; }
static unsigned call_wdef(unsigned w, int msg, unsigned param){
    WInfo *i = winfo(w); unsigned code = wdef_code(w), save = qd_port(), r;
    if (!code) return 0;
    r = call68k(code, 4, "WLWL", (unsigned)(i->procID & 15), w, (unsigned)msg, param);
    qd_setport(save);
    return r;
}

static void calc_rgns(unsigned w){
    WInfo *i = winfo(w); Rect c = content_global(w);
    if (wdef_custom(w)){ call_wdef(w, 2, 0); return; }
    {   int k = proc_kind(i->procID); Rect s = struct_rect(k, c);
        if (k == 16){ int d = rdoc_diam(i->procID); rgn_from_round(rl(w + W_strucRgn), s, d, d); }
        else if (k == 0 || k == 4 || k == 8 || k == 12){
            /* frame + 1-pixel drop shadow (an L-shape, not the full rectangle) */
            unsigned sh = rgn_new();
            rgn_set_rect(rl(w + W_strucRgn), mkrect(s.top, s.left, s.bottom - 1, s.right - 1));
            rgn_set_rect(sh, mkrect(s.top + 1, s.left + 1, s.bottom, s.right));
            rgn_op(rl(w + W_strucRgn), rl(w + W_strucRgn), sh, 1);
            mm_disposhandle(sh);
        } else if (k == 3){
            unsigned sh = rgn_new();
            rgn_set_rect(rl(w + W_strucRgn), mkrect(s.top, s.left, s.bottom - 2, s.right - 2));
            rgn_set_rect(sh, mkrect(s.top + 2, s.left + 2, s.bottom, s.right));
            rgn_op(rl(w + W_strucRgn), rl(w + W_strucRgn), sh, 1);
            mm_disposhandle(sh);
        } else rgn_set_rect(rl(w + W_strucRgn), s);
        rgn_set_rect(rl(w + W_contRgn), c);
    }
}

static void draw_title(unsigned port, unsigned w, Rect bar, int inverse){
    unsigned th = rl(w + W_title), s; int n, tw, saveFont, saveSize, saveMode;
    if (!th || !MA(rl(th))) return;
    s = MA(rl(th)); n = (int)rb(s);
    if (!n) return;
    saveFont = sw(port + GP_txFont); saveSize = sw(port + GP_txSize); saveMode = sw(port + GP_txMode);
    ww(port + GP_txFont, 0); ww(port + GP_txSize, 12); ww(port + GP_txMode, inverse ? 3 : 1);
    tw = qd_text_width(port, g_ram + s + 1, n);
    if (!inverse) qd_fill_rect(port, mkrect(bar.top + 1, (bar.left + bar.right - tw)/2 - 6, bar.bottom - 1, (bar.left + bar.right + tw)/2 + 6), PAT_WHITE, 8);
    ww(port + GP_pnLoc, (unsigned)(bar.bottom - 5)); ww(port + GP_pnLoc + 2, (unsigned)((bar.left + bar.right - tw)/2));
    qd_draw_text(port, g_ram + s + 1, n);
    ww(port + GP_txFont, (unsigned)saveFont); ww(port + GP_txSize, (unsigned)saveSize); ww(port + GP_txMode, (unsigned)saveMode);
}

static void draw_frame_native(unsigned w, unsigned port){
    WInfo *i = winfo(w); int k = proc_kind(i->procID), hil = rb(w + W_hilited);
    Rect c = content_global(w), s = struct_rect(k, c);
    ww(port + GP_pnSize, 1); ww(port + GP_pnSize + 2, 1); ww(port + GP_pnMode, 8); memcpy(g_ram + MA(port + GP_pnPat), PAT_BLACK, 8); ww(port + GP_pnVis, 0);
    switch (k){
    case 1:
        qd_fill_rect(port, mkrect(s.top, s.left, s.bottom, s.right), PAT_WHITE, 8);
        qd_frame_rect(port, s);
        { Rect r = mkrect(s.top+3, s.left+3, s.bottom-3, s.right-3);
          ww(port + GP_pnSize, 2); ww(port + GP_pnSize + 2, 2); qd_frame_rect(port, r); ww(port + GP_pnSize, 1); ww(port + GP_pnSize + 2, 1); }
        break;
    case 2: qd_frame_rect(port, s); break;
    case 3: qd_frame_rect(port, mkrect(s.top, s.left, s.bottom-2, s.right-2));
            qd_fill_rect(port, mkrect(s.bottom-2, s.left+2, s.bottom, s.right), PAT_BLACK, 8);
            qd_fill_rect(port, mkrect(s.top+2, s.right-2, s.bottom, s.right), PAT_BLACK, 8); break;
    case 16: {
        int d = rdoc_diam(i->procID); Rect bar = mkrect(s.top, s.left, c.top, s.right);
        qd_frame_round(port, s, d, d, 1, PAT_BLACK, 8);
        qd_fill_rect(port, c, PAT_WHITE, 8);   /* content stays the window's; this only clears the corner join */
        (void)bar; draw_title(port, w, mkrect(s.top, s.left, c.top, s.right), 1);
        } break;
    default: {
        Rect fr = mkrect(s.top, s.left, s.bottom - 1, s.right - 1), bar = mkrect(s.top, s.left, c.top, s.right - 1); int y;
        qd_frame_rect(port, fr);
        qd_fill_rect(port, mkrect(s.bottom - 1, s.left + 1, s.bottom, s.right), PAT_BLACK, 8);
        qd_fill_rect(port, mkrect(s.top + 1, s.right - 1, s.bottom, s.right), PAT_BLACK, 8);
        qd_fill_rect(port, mkrect(bar.top + 1, bar.left + 1, bar.bottom, bar.right - 1), PAT_WHITE, 8);
        qd_fill_rect(port, mkrect(bar.bottom - 1, bar.left, bar.bottom, bar.right), PAT_BLACK, 8);
        if (hil){
            for (y = bar.top + 4; y <= bar.top + 14; y += 2) qd_fill_rect(port, mkrect(y, bar.left + 2, y + 1, bar.right - 2), PAT_BLACK, 8);
            if (rb(w + W_goAway)){
                Rect gb = mkrect(bar.top + 4, bar.left + 9, bar.top + 15, bar.left + 20);
                qd_fill_rect(port, mkrect(gb.top - 1, gb.left - 1, gb.bottom + 1, gb.right + 1), PAT_WHITE, 8);
                qd_frame_rect(port, gb);
            }
        }
        draw_title(port, w, bar, 0);
        } break;
    }
}

/* draw a window's frame through the WMgrPort, clipped to its uncovered structure */
static void draw_frame(unsigned w, unsigned clip){
    unsigned save = qd_port(), oldclip = rgn_new();
    rgn_copy(rl(g_wmport + GP_clipRgn), oldclip);
    rgn_copy(clip, rl(g_wmport + GP_clipRgn));
    qd_setport(g_wmport);
    if (wdef_custom(w)) call_wdef(w, 0, 0);
    else draw_frame_native(w, g_wmport);
    qd_setport(g_wmport);
    rgn_copy(oldclip, rl(g_wmport + GP_clipRgn)); mm_disposhandle(oldclip);
    qd_setport(save);
}

/* ---- composition ------------------------------------------------------------------ */
static int g_compose_depth;
void wm_compose(void){
    unsigned covered = rgn_new(), tmp = rgn_new(), w;
    WInfo *wi;
    if (g_compose_depth++) { g_compose_depth--; mm_disposhandle(covered); mm_disposhandle(tmp); return; }
    if (getenv(HLE_ENV("COMPLOG"))) logf_("[compose] from %06X\n", reg(M68K_REG_PPC));
    for (w = wm_first(); w; w = wnext(w)){
        unsigned vis = rl(w + GP_visRgn); int gh, gv;
        wi = winfo(w); if (!wi) continue;
        wm_global_origin(w, &gh, &gv);
        if (!rb(w + W_visible)){ rgn_set_rect(vis, mkrect(0,0,0,0)); rgn_set_rect(wi->oldvis, mkrect(0,0,0,0)); continue; }
        calc_rgns(w);
        /* visible content (global) = content & desktop - windows in front */
        rgn_op(tmp, rl(w + W_contRgn), g_gray, 0);
        rgn_op(tmp, tmp, covered, 2);
        /* newly exposed content -> erase + update region */
        {   unsigned exposed = rgn_new();
            rgn_op(exposed, tmp, wi->oldvis, 2);
            if (!rgn_empty(exposed)){
                unsigned save = qd_port(), oc = rgn_new(); unsigned char bk[8];
                rgn_op(rl(w + W_updRgn), rl(w + W_updRgn), exposed, 1);
                rgn_copy(rl(g_wmport + GP_clipRgn), oc); rgn_copy(exposed, rl(g_wmport + GP_clipRgn));
                memcpy(bk, g_ram + MA(w + GP_bkPat), 8);
                qd_fill_rect(g_wmport, rgn_bbox(exposed), bk, 8);
                rgn_copy(oc, rl(g_wmport + GP_clipRgn)); mm_disposhandle(oc); qd_setport(save);
            }
            mm_disposhandle(exposed);
        }
        rgn_copy(tmp, wi->oldvis);
        rgn_copy(tmp, vis); rgn_offset(vis, -gh, -gv);
        /* frame: structure - content - covered */
        {   unsigned fr = rgn_new();
            rgn_op(fr, rl(w + W_strucRgn), covered, 2);
            rgn_op(fr, fr, g_gray, 0);
            if (wdef_custom(w)) { unsigned c2 = rgn_new(); rgn_op(c2, fr, rl(w + W_contRgn), 2); draw_frame(w, c2); mm_disposhandle(c2); }
            else { unsigned c2 = rgn_new(); rgn_op(c2, fr, rl(w + W_contRgn), 2); draw_frame(w, c2); mm_disposhandle(c2); }
            mm_disposhandle(fr);
        }
        rgn_op(covered, covered, rl(w + W_strucRgn), 1);
    }
    /* desktop */
    {   unsigned save = qd_port(), oc = rgn_new(), desk = rgn_new();
        rgn_op(desk, g_gray, covered, 2);
        rgn_copy(rl(g_wmport + GP_clipRgn), oc); rgn_copy(desk, rl(g_wmport + GP_clipRgn));
        qd_fill_rect(g_wmport, rgn_bbox(desk), PAT_GRAY, 8);
        rgn_copy(oc, rl(g_wmport + GP_clipRgn));
        mm_disposhandle(oc); mm_disposhandle(desk); qd_setport(save);
    }
    menu_draw_bar();
    mm_disposhandle(covered); mm_disposhandle(tmp);
    hle_dirty();
    g_compose_depth--;
}

/* ---- window list -------------------------------------------------------------------- */
static void list_remove(unsigned w){
    unsigned p = wm_first(), prev = 0;
    while (p && p != w){ prev = p; p = wnext(p); }
    if (!p) return;
    if (prev) wl(prev + W_next, wnext(w)); else wl(LM_WindowList, wnext(w));
    wl(w + W_next, 0);
}
static void list_insert(unsigned w, unsigned behind){
    if (behind == 0xFFFFFFFFu || behind == 0xFFFFFFu){ wl(w + W_next, wm_first()); wl(LM_WindowList, w); return; }   /* front */
    if (behind == 0){ unsigned p = wm_first(); if (!p){ wl(LM_WindowList, w); wl(w + W_next, 0); return; }   /* back */
                      while (wnext(p)) p = wnext(p); wl(p + W_next, w); wl(w + W_next, 0); return; }
    wl(w + W_next, wnext(behind)); wl(behind + W_next, w);
}

static void set_hilite(unsigned w, int on){ if (w && rb(w + W_hilited) != (unsigned)(on != 0)){ wb(w + W_hilited, (unsigned)(on != 0)); } }
void wm_activate_events(unsigned oldf, unsigned newf){
    if (oldf == newf) return;
    if (oldf && is_window(oldf)){ set_hilite(oldf, 0); evt_post(8, oldf, 0, 0, 0); }
    if (newf){ set_hilite(newf, 1); evt_post(8, newf, 0, 0, 1); }
}

unsigned wm_new_window(unsigned st, Rect bounds, const unsigned char *title, int visible, int procID,
                       unsigned behind, int goAway, unsigned refCon, int dialog){
    unsigned w = st ? st : mm_newptr(dialog ? DLG_SIZE : W_SIZE, 1), oldf = wm_front(), save;
    WInfo *wi;
    if (!w) return 0;
    if (!st) memset(g_ram + MA(w), 0, dialog ? DLG_SIZE : W_SIZE);
    save = qd_port();
    wl(w + GP_visRgn, 0); wl(w + GP_clipRgn, 0);
    qd_init_port(w);
    /* local (0,0) at the global top-left of boundsRect */
    rect_put(w + GP_portRect, mkrect(0, 0, bounds.bottom - bounds.top, bounds.right - bounds.left));
    rect_put(w + GP_portBits + 6, rect_off(mkrect(0, 0, g_scr_h, g_scr_w), -bounds.left, -bounds.top));
    ww(w + W_kind, dialog ? 2 : 8);
    wb(w + W_visible, 0); wb(w + W_hilited, 0); wb(w + W_goAway, goAway ? 1 : 0);
    wl(w + W_strucRgn, rgn_new()); wl(w + W_contRgn, rgn_new()); wl(w + W_updRgn, rgn_new());
    wl(w + W_title, mm_newhandle_from(title, (unsigned)title[0] + 1));
    wl(w + W_refCon, refCon);
    if ((procID >> 4) > 1){
        unsigned h = rs_get(0x57444546u, procID >> 4);       /* 'WDEF' */
        if (h){ wb(h, rb(h) | 0x80); wl(w + W_defProc, h | ((unsigned)(procID & 15) << 24)); }
        else procID = 0;
    }
    if (g_nwi < 64){ wi = &g_wi[g_nwi++]; memset(wi, 0, sizeof *wi); wi->w = w; wi->procID = procID; wi->oldvis = rgn_new(); wi->savevis = rgn_new(); }
    list_insert(w, behind);
    if (wdef_custom(w)) call_wdef(w, 3, 0);                /* wNew */
    calc_rgns(w);
    qd_setport(save);
    if (visible){
        wb(w + W_visible, 1);
        if (wm_front() == w) wm_activate_events(oldf, w);
        wm_compose();
    }
    qd_setport(w);
    if (getenv(HLE_ENV("WINLOG"))) logf_("[win] new %06X storage=%06X proc=%d vis=%d rect=(%d,%d,%d,%d) visRgn=%06X clipRgn=%06X\n", w, st, procID, visible,
                                    bounds.top, bounds.left, bounds.bottom, bounds.right, rl(w + GP_visRgn), rl(w + GP_clipRgn));
    return w;
}

void wm_dispose(unsigned w, int free_storage){
    WInfo *wi = winfo(w); int i; unsigned oldf = wm_front();
    if (!wi) return;
    { extern void ctl_kill_all(unsigned w); ctl_kill_all(w); }
    if (wdef_custom(w)) call_wdef(w, 4, 0);
    list_remove(w);
    mm_disposhandle(rl(w + W_strucRgn)); mm_disposhandle(rl(w + W_contRgn)); mm_disposhandle(rl(w + W_updRgn));
    mm_disposhandle(rl(w + W_title));
    mm_disposhandle(rl(w + GP_visRgn)); mm_disposhandle(rl(w + GP_clipRgn));
    mm_disposhandle(wi->oldvis); mm_disposhandle(wi->savevis);
    for (i = 0; i < g_nwi; i++) if (g_wi[i].w == w){ g_wi[i] = g_wi[--g_nwi]; break; }
    if (qd_port() == w) qd_setport(g_wmport);
    if (free_storage) mm_disposptr(w);
    if (oldf == w) wm_activate_events(0, wm_front());
    wm_compose();
}

void wm_show(unsigned w, int show){
    unsigned oldf = wm_front();
    if (!is_window(w) || rb(w + W_visible) == (unsigned)(show != 0)) return;
    wb(w + W_visible, (unsigned)(show != 0));
    if (wm_front() != oldf) wm_activate_events(show ? oldf : 0, wm_front());
    if (!show && oldf == w) wm_activate_events(0, wm_front());
    wm_compose();
}

void wm_select(unsigned w){
    unsigned oldf = wm_front();
    if (!is_window(w)) return;
    if (wm_first() == w && oldf == w) return;              /* already the active front window */
    list_remove(w); list_insert(w, 0xFFFFFFFFu);
    if (oldf != w) wm_activate_events(oldf, w);
    wm_compose();
}

void wm_invalidate(unsigned w, Rect r){
    unsigned g; int gh, gv;
    if (!is_window(w)) return;
    wm_global_origin(w, &gh, &gv);
    g = rgn_new(); rgn_set_rect(g, rect_off(r, gh, gv));
    rgn_op(rl(w + W_updRgn), rl(w + W_updRgn), g, 1);
    mm_disposhandle(g);
}

int wm_update_pending(unsigned *out){
    unsigned w;
    for (w = wm_first(); w; w = wnext(w))
        if (rb(w + W_visible) && is_window(w)){
            unsigned u = rl(w + W_updRgn), t = rgn_new(); int has;
            rgn_op(t, u, rl(w + W_contRgn), 0); has = !rgn_empty(t); mm_disposhandle(t);
            if (has){ *out = w; return 1; }
            rgn_set_rect(u, mkrect(0,0,0,0));
        }
    return 0;
}

static int hit_native(unsigned w, int h, int v){
    WInfo *i = winfo(w); int k = proc_kind(i->procID); Rect c = content_global(w);
    if (pt_in(c, h, v)) return 3;                                            /* inContent */
    if (!rgn_contains(rl(w + W_strucRgn), h, v)) return 0;
    if (k == 0 || k == 4 || k == 8 || k == 12 || k == 16){
        if (v < c.top){
            if (rb(w + W_goAway) && k != 16 && h >= c.left + 8 && h < c.left + 21 && v >= c.top - 16 && v < c.top - 3) return 6;   /* inGoAway */
            return 4;                                                         /* inDrag */
        }
    }
    return 0;
}
unsigned wm_find(int h, int v, int *part){
    unsigned w;
    if (v < MBAR_H){ *part = 1; return 0; }
    for (w = wm_first(); w; w = wnext(w)){
        if (!rb(w + W_visible) || !is_window(w)) continue;
        if (!rgn_contains(rl(w + W_strucRgn), h, v)) continue;
        if (wdef_custom(w)){
            unsigned r = call_wdef(w, 1, (unsigned)(v << 16) | (unsigned)(h & 0xFFFF));
            *part = r == 1 ? 3 : r == 2 ? 4 : r == 3 ? 5 : r == 4 ? 6 : 0;
            if (*part) return w;
            continue;
        }
        *part = hit_native(w, h, v);
        if (*part) return w;
    }
    *part = 0;
    return 0;
}

/* drag an outline of the window until the button comes up; returns the offset */
static void drag_outline(unsigned w, int h0, int v0, Rect limit, int *dh, int *dv){
    Rect s = rgn_bbox(rl(w + W_strucRgn)); int h = h0, v = v0, lh = h0, lv = v0, drawn = 0;
    unsigned save = qd_port();
    qd_setport(g_wmport);
    ww(g_wmport + GP_pnMode, 10); memcpy(g_ram + MA(g_wmport + GP_pnPat), PAT_GRAY, 8); ww(g_wmport + GP_pnVis, 0);
    rgn_set_rect(rl(g_wmport + GP_clipRgn), mkrect(MBAR_H, 0, g_scr_h, g_scr_w));
    while (evt_mouse_down() && !g_quit){
        evt_mouse(&h, &v);
        if (h < limit.left) h = limit.left; if (h >= limit.right) h = limit.right - 1;
        if (v < limit.top) v = limit.top; if (v >= limit.bottom) v = limit.bottom - 1;
        if (!drawn || h != lh || v != lv){
            if (drawn) qd_frame_rect(g_wmport, rect_off(s, lh - h0, lv - v0));
            qd_frame_rect(g_wmport, rect_off(s, h - h0, v - v0)); drawn = 1; lh = h; lv = v;
        }
        hle_idle(5);
    }
    if (drawn) qd_frame_rect(g_wmport, rect_off(s, lh - h0, lv - v0));
    ww(g_wmport + GP_pnMode, 8); memcpy(g_ram + MA(g_wmport + GP_pnPat), PAT_BLACK, 8);
    rgn_set_rect(rl(g_wmport + GP_clipRgn), mkrect(-32767, -32767, 32767, 32767));
    qd_setport(save);
    *dh = lh - h0; *dv = lv - v0;
}

static void move_window(unsigned w, int gh, int gv){
    Rect b = rect_at(w + GP_portBits + 6), pr = rect_at(w + GP_portRect); int oh, ov;
    wm_global_origin(w, &oh, &ov);
    /* keep portRect.topLeft at global (gh, gv) */
    {   int nh = gh - pr.left, nv = gv - pr.top;
        rect_put(w + GP_portBits + 6, rect_off(b, oh - nh, ov - nv)); }
    {   WInfo *wi = winfo(w); if (wi) rgn_set_rect(wi->oldvis, mkrect(0,0,0,0)); }
    wm_compose();
}

/* visRgn &= updateRgn for the duration of an update; updateRgn is cleared */
void wm_begin_update(unsigned w){
    WInfo *wi = winfo(w); int gh, gv; unsigned u;
    if (!wi) return;
    wm_global_origin(w, &gh, &gv);
    rgn_copy(rl(w + GP_visRgn), wi->savevis);
    u = rgn_new(); rgn_copy(rl(w + W_updRgn), u); rgn_offset(u, -gh, -gv);
    rgn_op(rl(w + GP_visRgn), rl(w + GP_visRgn), u, 0);
    rgn_set_rect(rl(w + W_updRgn), mkrect(0,0,0,0));
    mm_disposhandle(u);
}
void wm_end_update(unsigned w){ WInfo *wi = winfo(w); if (wi) rgn_copy(wi->savevis, rl(w + GP_visRgn)); }
int wm_has_update(unsigned w){
    unsigned t; int has;
    if (!is_window(w) || !rb(w + W_visible)) return 0;
    t = rgn_new(); rgn_op(t, rl(w + W_updRgn), rl(w + W_contRgn), 0); has = !rgn_empty(t); mm_disposhandle(t);
    return has;
}
void wm_validate_all(unsigned w){ if (is_window(w)) rgn_set_rect(rl(w + W_updRgn), mkrect(0,0,0,0)); }

/* ---- traps ------------------------------------------------------------------------- */
void wm_init(void){
    g_wmport = mm_newptr(GP_SIZE, 1);
    wl(g_wmport + GP_visRgn, 0); wl(g_wmport + GP_clipRgn, 0);
    qd_init_port(g_wmport);
    wl(LM_WMgrPort, g_wmport);
    g_gray = rgn_new(); rgn_from_round(g_gray, mkrect(0, 0, g_scr_h, g_scr_w), 16, 16);
    {   unsigned mb = rgn_new(); rgn_set_rect(mb, mkrect(0, 0, MBAR_H, g_scr_w)); rgn_op(g_gray, g_gray, mb, 2); mm_disposhandle(mb); }
    wl(LM_GrayRgn, g_gray);
    wl(LM_WindowList, 0);
    ww(LM_MBarHeight, MBAR_H);
    qd_fill_rect(g_wmport, mkrect(0, 0, g_scr_h, g_scr_w), PAT_BLACK, 8);
    wm_compose();
}

static unsigned wmgr_port(void){ return g_wmport; }

void wm_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv; unsigned P = qd_port();
    t->handled = 1;
    switch (word){
    case 0xA912: if (!g_wmport) wm_init(); POP(0); break;                         /* InitWindows */
    case 0xA910: case 0xAA48: wl(ARGL(0), wmgr_port()); POP(4); break;             /* GetWMgrPort / GetCWMgrPort */
    case 0xAA45: {                                                                 /* NewCWindow: no Color QuickDraw, a plain window */
        unsigned char title[256]; unsigned tp = ARGL(14);
        memcpy(title, g_ram + MA(tp), (size_t)rb(tp) + 1);
        RETL(26, wm_new_window(ARGL(22), rect_at(ARGL(18)), title, ARGB(12) != 0, ARGW(10), ARGL(6), ARGB(4) != 0, ARGL(0), 0));
        } break;
    case 0xAA46: {                                                                 /* GetNewCWindow */
        unsigned h = rs_get(0x57494E44u, ARGW(8)), d; unsigned char title[256];
        if (!h){ RETL(10, 0); break; }
        d = MA(rl(h)); memcpy(title, g_ram + d + 18, (size_t)rb(d + 18) + 1);
        RETL(10, wm_new_window(ARGL(4), rect_at(d), title, rb(d + 10) != 0, sw(d + 8), ARGL(0), rb(d + 12) != 0, rl(d + 14), 0));
        } break;
    case 0xA913: {                                                                 /* NewWindow */
        unsigned char title[256]; unsigned tp = ARGL(14);
        memcpy(title, g_ram + MA(tp), (size_t)rb(tp) + 1);
        RETL(26, wm_new_window(ARGL(22), rect_at(ARGL(18)), title, ARGB(12) != 0, ARGW(10), ARGL(6), ARGB(4) != 0, ARGL(0), 0));
        } break;
    case 0xA9BD: {                                                                 /* GetNewWindow(id, storage, behind) */
        unsigned h = rs_get(0x57494E44u, ARGW(8)), d; unsigned char title[256];
        if (!h){ RETL(10, 0); break; }
        d = MA(rl(h)); memcpy(title, g_ram + d + 18, (size_t)rb(d + 18) + 1);
        RETL(10, wm_new_window(ARGL(4), rect_at(d), title, rb(d + 10) != 0, sw(d + 8), ARGL(0), rb(d + 12) != 0, rl(d + 14), 0));
        } break;
    case 0xA914: wm_dispose(ARGL(0), 1); POP(4); break;                            /* DisposeWindow */
    case 0xA92D: wm_dispose(ARGL(0), 0); POP(4); break;                            /* CloseWindow */
    case 0xA915: wm_show(ARGL(0), 1); POP(4); break;                               /* ShowWindow */
    case 0xA916: wm_show(ARGL(0), 0); POP(4); break;                               /* HideWindow */
    case 0xA908: wm_show(ARGL(2), ARGB(0)); POP(6); break;                         /* ShowHide */
    case 0xA91F: wm_select(ARGL(0)); POP(4); break;                                /* SelectWindow */
    case 0xA920: { unsigned w = ARGL(0); if (is_window(w)){ list_remove(w); list_insert(w, 0xFFFFFFFFu); wm_compose(); } POP(4); } break;   /* BringToFront */
    case 0xA921: { unsigned w = ARGL(4), b = ARGL(0), oldf = wm_front();          /* SendBehind */
                   if (is_window(w)){ list_remove(w); list_insert(w, b); if (wm_front() != oldf) wm_activate_events(oldf, wm_front()); wm_compose(); } POP(8); } break;
    case 0xA91C: { unsigned w = ARGL(2); set_hilite(w, ARGB(0)); wm_compose(); POP(6); } break;   /* HiliteWindow */
    case 0xA924: RETL(0, wm_front()); break;                                       /* FrontWindow */
    case 0xA91B: { unsigned w = ARGL(6); int h = ARGW(4), v = ARGW(2), front = ARGB(0) != 0;   /* MoveWindow(w,h,v,front) */
                   if (is_window(w)){ if (front && wm_front() != w){ list_remove(w); list_insert(w, 0xFFFFFFFFu); wm_activate_events(0, w); } move_window(w, h, v); }
                   POP(10); } break;
    case 0xA91D: { unsigned w = ARGL(6); int ww_ = ARGW(4), hh = ARGW(2);            /* SizeWindow(w,w,h,update) */
                   if (is_window(w) && ww_ > 0 && hh > 0){ Rect r = rect_at(w + GP_portRect); r.right = (short)(r.left + ww_); r.bottom = (short)(r.top + hh);
                       rect_put(w + GP_portRect, r); { WInfo *wi = winfo(w); if (wi) rgn_set_rect(wi->oldvis, mkrect(0,0,0,0)); } wm_compose(); }
                   POP(10); } break;
    case 0xA925: {                                                                 /* DragWindow(w, startPt, boundsRect) */
        unsigned w = ARGL(8); int v0 = ARGW(4), h0 = ARGW(6), dh, dv; Rect lim = rect_at(ARGL(0));
        if (is_window(w)){
            drag_outline(w, h0, v0, lim, &dh, &dv);
            if (dh || dv){ int gh, gv; Rect pr = rect_at(w + GP_portRect); wm_global_origin(w, &gh, &gv); move_window(w, gh + pr.left + dh, gv + pr.top + dv); }
            if (!(host_modifiers() & 0x100) && wm_front() != w) wm_select(w);
        }
        POP(12); } break;
    case 0xA905: { int v0 = ARGW(18), h0 = ARGW(20); (void)v0; (void)h0; RETL(22, 0x80008000u); } break;   /* DragGrayRgn (unused) */
    case 0xA91E: {                                                                 /* TrackGoAway(w, pt) */
        int h, v, in = 1; unsigned w = ARGL(4); Rect c = content_global(w);
        Rect gb = mkrect(c.top - 15, c.left + 9, c.top - 4, c.left + 20);
        while (evt_mouse_down() && !g_quit) hle_idle(5);
        evt_mouse(&h, &v); in = pt_in(gb, h, v);
        RETB(8, in); } break;
    case 0xA92B: RETL(12, 0); break;                                               /* GrowWindow */
    case 0xA83A: POP(8); break;                                                    /* ZoomWindow */
    case 0xA83B: RETB(10, 0); break;                                               /* TrackBox */
    case 0xA92C: { int v = ARGW(4), h = ARGW(6), part; unsigned w = wm_find(h, v, &part);   /* FindWindow(pt, VAR w) */
                   wl(ARGL(0), w); RETW(8, part); } break;
    case 0xA919: { unsigned th = rl(ARGL(4) + W_title), d = ARGL(0);               /* GetWTitle */
                   if (th && MA(rl(th))) memcpy(g_ram + MA(d), g_ram + MA(rl(th)), (size_t)rb(rl(th)) + 1); else wb(d, 0); POP(8); } break;
    case 0xA91A: { unsigned w = ARGL(4), s = ARGL(0), th = rl(w + W_title);       /* SetWTitle */
                   mm_sethandlesize(th, rb(s) + 1u); memcpy(g_ram + MA(rl(th)), g_ram + MA(s), (size_t)rb(s) + 1); wm_compose(); POP(8); } break;
    case 0xA917: RETL(4, rl(ARGL(0) + W_refCon)); break;                           /* GetWRefCon */
    case 0xA918: wl(ARGL(4) + W_refCon, ARGL(0)); POP(8); break;                   /* SetWRefCon */
    case 0xA92E: wl(ARGL(4) + W_pic, ARGL(0)); POP(8); break;                      /* SetWindowPic */
    case 0xA92F: RETL(4, rl(ARGL(0) + W_pic)); break;                              /* GetWindowPic */
    case 0xA922: wm_begin_update(ARGL(0)); POP(4); break;                         /* BeginUpdate */
    case 0xA923: wm_end_update(ARGL(0)); POP(4); break;                           /* EndUpdate */
    case 0xA928: wm_invalidate(P, rect_at(ARGL(0))); POP(4); break;                /* InvalRect */
    case 0xA927: { unsigned g = rgn_new(); int gh, gv; wm_global_origin(P, &gh, &gv);   /* InvalRgn */
                   rgn_copy(ARGL(0), g); rgn_offset(g, gh, gv); if (is_window(P)) rgn_op(rl(P + W_updRgn), rl(P + W_updRgn), g, 1); mm_disposhandle(g); POP(4); } break;
    case 0xA92A: { unsigned g; int gh, gv; if (is_window(P)){ wm_global_origin(P, &gh, &gv);   /* ValidRect */
                   g = rgn_new(); rgn_set_rect(g, rect_off(rect_at(ARGL(0)), gh, gv)); rgn_op(rl(P + W_updRgn), rl(P + W_updRgn), g, 2); mm_disposhandle(g); } POP(4); } break;
    case 0xA929: { unsigned g; int gh, gv; if (is_window(P)){ wm_global_origin(P, &gh, &gv);   /* ValidRgn */
                   g = rgn_new(); rgn_copy(ARGL(0), g); rgn_offset(g, gh, gv); rgn_op(rl(P + W_updRgn), rl(P + W_updRgn), g, 2); mm_disposhandle(g); } POP(4); } break;
    case 0xA904: {                                                                 /* DrawGrowIcon */
        unsigned w = ARGL(0), save = qd_port(); Rect pr = rect_at(w + GP_portRect);
        Rect g = mkrect(pr.bottom - 15, pr.right - 15, pr.bottom, pr.right);
        qd_setport(w);
        qd_fill_rect(w, g, PAT_WHITE, 8); qd_frame_rect(w, mkrect(g.top, g.left - 1, g.bottom + 1, g.right + 1));
        qd_frame_rect(w, mkrect(g.top + 3, g.left + 3, g.top + 10, g.left + 10));
        qd_frame_rect(w, mkrect(g.top + 5, g.left + 5, g.top + 13, g.left + 13));
        qd_setport(save); POP(4); } break;
    case 0xA909: case 0xA90A: case 0xA90C: case 0xA90D: wm_compose(); POP(word == 0xA909 ? 4 : 8); break;   /* CalcVis CalcVBehind PaintOne PaintBehind */
    case 0xA90B: POP(4); break;                                                    /* ClipAbove */
    case 0xA90E: case 0xA90F: POP(word == 0xA90E ? 4 : 6); break;                 /* SaveOld DrawNew */
    case 0xA911: RETB(4, 0); break;                                                /* CheckUpdate */
    default: t->handled = 0;
    }
}
