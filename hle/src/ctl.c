/* Control Manager: push buttons, check boxes, radio buttons and scroll bars, drawn
 * in the System 6 style. ControlRecords live in emulated memory (the game reads
 * contrlValue etc. directly); TrackControl runs the game's action procs as 68k code.
 */
#include "mac.h"

#define C_next   0
#define C_owner  4
#define C_rect   8
#define C_vis   16
#define C_hilite 17
#define C_value 18
#define C_min   20
#define C_max   22
#define C_def   24
#define C_data  28
#define C_action 32
#define C_refCon 36
#define C_title 40
#define C_SIZE 296
#define W_ctlList 140

static unsigned cp(unsigned c){ return MA(rl(c)); }        /* ControlHandle -> record */
static int cproc(unsigned c){ return (int)(rl(cp(c) + C_def) >> 24); }   /* procID kept in the defProc high byte */
static int ckind(unsigned c){ int p = cproc(c); return p >= 16 ? 16 : (p & 7); }

static void with_port(unsigned w, unsigned *save){ *save = qd_port(); qd_setport(w); }

static void draw_button(unsigned w, unsigned c){
    unsigned r = cp(c); Rect b = rect_at(r + C_rect); int hil = (int)rb(r + C_hilite), k = ckind(c);
    unsigned s = r + C_title; int n = (int)rb(s), tw, a, d, wm, l;
    int useW = (cproc(c) & 8) != 0;
    int sf = sw(w + GP_txFont), ss = sw(w + GP_txSize), sm = sw(w + GP_txMode), sface = (int)rb(w + GP_txFace);
    if (!useW){ ww(w + GP_txFont, 0); ww(w + GP_txSize, 12); wb(w + GP_txFace, 0); }
    ww(w + GP_txMode, 1);
    qd_font_info(w, &a, &d, &wm, &l);
    tw = qd_text_width(w, g_ram + s + 1, n);
    if (k == 0){
        int rr = (b.bottom - b.top) < 20 ? (b.bottom - b.top) : 16;
        qd_frame_round(w, b, rr, rr, 1, PAT_WHITE, 8);
        ww(w + GP_pnSize, 1); ww(w + GP_pnSize + 2, 1); ww(w + GP_pnMode, 8); memcpy(g_ram + MA(w + GP_pnPat), PAT_BLACK, 8);
        qd_frame_round(w, b, rr, rr, 0, 0, 0);
        ww(w + GP_pnLoc, (unsigned)((b.top + b.bottom + a - d) / 2)); ww(w + GP_pnLoc + 2, (unsigned)((b.left + b.right - tw) / 2));
        qd_draw_text(w, g_ram + s + 1, n);
        if (hil == 255) qd_frame_round(w, mkrect(b.top+1,b.left+1,b.bottom-1,b.right-1), rr-2, rr-2, 1, PAT_GRAY, 11);
        else if (hil) qd_frame_round(w, mkrect(b.top+1,b.left+1,b.bottom-1,b.right-1), rr-2, rr-2, 1, PAT_BLACK, 10);
    } else {
        int val = sw(r + C_value), cy = (b.top + b.bottom) / 2; Rect box = mkrect(cy - 6, b.left + 2, cy + 6, b.left + 14);
        qd_fill_rect(w, b, PAT_WHITE, 8);
        ww(w + GP_pnSize, 1); ww(w + GP_pnSize + 2, 1); ww(w + GP_pnMode, 8); memcpy(g_ram + MA(w + GP_pnPat), PAT_BLACK, 8);
        if (k == 1){
            qd_frame_rect(w, box);
            if (hil && hil != 255) qd_frame_rect(w, mkrect(box.top+1, box.left+1, box.bottom-1, box.right-1));
            if (val){ qd_line(w, box.left, box.top, box.right - 1, box.bottom - 1); qd_line(w, box.right - 1, box.top, box.left, box.bottom - 1); }
        } else {
            qd_oval(w, box, 0, 0, 0);
            if (hil && hil != 255) qd_oval(w, mkrect(box.top+1, box.left+1, box.bottom-1, box.right-1), 0, 0, 0);
            if (val) qd_oval(w, mkrect(box.top+3, box.left+3, box.bottom-3, box.right-3), 1, PAT_BLACK, 8);
        }
        ww(w + GP_pnLoc, (unsigned)((b.top + b.bottom + a - d) / 2)); ww(w + GP_pnLoc + 2, (unsigned)(box.right + 5));
        qd_draw_text(w, g_ram + s + 1, n);
        if (hil == 255) qd_fill_rect(w, b, PAT_GRAY, 11);
    }
    ww(w + GP_txFont, (unsigned)sf); ww(w + GP_txSize, (unsigned)ss); ww(w + GP_txMode, (unsigned)sm); wb(w + GP_txFace, (unsigned)sface);
}

/* scroll bar geometry */
static int sb_vert(Rect b){ return (b.bottom - b.top) > (b.right - b.left); }
static int sb_len(Rect b){ return sb_vert(b) ? b.bottom - b.top : b.right - b.left; }
static int sb_active(unsigned r){ return sw(r + C_max) > sw(r + C_min) && rb(r + C_hilite) != 255; }
static Rect sb_thumb(unsigned r, int pos_override){
    Rect b = rect_at(r + C_rect); int len = sb_len(b), span = len - 32 - 16, mn = sw(r + C_min), mx = sw(r + C_max), v = sw(r + C_value), p;
    if (span < 0) span = 0;
    p = pos_override >= 0 ? pos_override : (mx > mn ? (int)((long)(v - mn) * span / (mx - mn)) : 0);
    if (sb_vert(b)) return mkrect(b.top + 15 + p, b.left, b.top + 15 + p + 17, b.right);
    return mkrect(b.top, b.left + 15 + p, b.bottom, b.left + 15 + p + 17);
}
static void draw_arrow(unsigned w, Rect a, int dir, int hil){
    /* dir 0 up 1 down 2 left 3 right: a filled-or-outlined arrow glyph */
    static const char *up[10] = { "      #      ","     # #     ","    #   #    ","   #     #   ","  #       #  "," ##### #####","     # #    ","     # #    ","     # #    ","     ###    " };
    int i, j, cx = (a.left + a.right) / 2, cy = (a.top + a.bottom) / 2;
    qd_fill_rect(w, mkrect(a.top + 1, a.left + 1, a.bottom - 1, a.right - 1), PAT_WHITE, 8);
    for (i = 0; i < 10; i++) for (j = 0; j < 13 && up[i][j]; j++){
        int on = up[i][j] == '#', inside = 0, k, first = -1, last = -1;
        for (k = 0; k < 13 && up[i][k]; k++) if (up[i][k] == '#'){ if (first < 0) first = k; last = k; }
        inside = hil && j > first && j < last && i < 9;
        if (!on && !inside) continue;
        {   int x, y;
            if (dir == 0){ x = cx - 6 + j; y = cy - 5 + i; }
            else if (dir == 1){ x = cx - 6 + j; y = cy + 4 - i; }
            else if (dir == 2){ x = cx - 5 + i; y = cy - 6 + j; }
            else { x = cx + 4 - i; y = cy - 6 + j; }
            qd_fill_rect(w, mkrect(y, x, y + 1, x + 1), PAT_BLACK, 8); }
    }
}
static void draw_scroll(unsigned w, unsigned c, int hilpart){
    unsigned r = cp(c); Rect b = rect_at(r + C_rect); int v = sb_vert(b), act = sb_active(r);
    Rect a1 = v ? mkrect(b.top, b.left, b.top + 16, b.right) : mkrect(b.top, b.left, b.bottom, b.left + 16);
    Rect a2 = v ? mkrect(b.bottom - 16, b.left, b.bottom, b.right) : mkrect(b.top, b.right - 16, b.bottom, b.right);
    Rect tr = v ? mkrect(b.top + 16, b.left + 1, b.bottom - 16, b.right - 1) : mkrect(b.top + 1, b.left + 16, b.bottom - 1, b.right - 16);
    ww(w + GP_pnSize, 1); ww(w + GP_pnSize + 2, 1); ww(w + GP_pnMode, 8); memcpy(g_ram + MA(w + GP_pnPat), PAT_BLACK, 8);
    qd_frame_rect(w, b);
    qd_fill_rect(w, tr, act ? PAT_GRAY : PAT_WHITE, 8);
    qd_frame_rect(w, a1); qd_frame_rect(w, a2);
    draw_arrow(w, a1, v ? 0 : 2, hilpart == 20); draw_arrow(w, a2, v ? 1 : 3, hilpart == 21);
    if (act){ Rect t = sb_thumb(r, -1); qd_fill_rect(w, t, PAT_WHITE, 8); qd_frame_rect(w, t); }
}

void ctl_draw(unsigned c){
    unsigned r = cp(c), w = rl(r + C_owner), save;
    if (!r || !rb(r + C_vis)) return;
    with_port(w, &save);
    if (ckind(c) == 16) draw_scroll(w, c, rb(r + C_hilite));
    else draw_button(w, c);
    qd_setport(save);
}
void ctl_draw_all(unsigned w){ unsigned c; for (c = rl(w + W_ctlList); c; c = rl(cp(c) + C_next)) ctl_draw(c); }

unsigned ctl_new(unsigned w, Rect r, const unsigned char *title, int vis, int val, int min, int max, int proc, unsigned refCon){
    unsigned h = mm_newhandle(C_SIZE, 1), p = cp(h);
    wl(p + C_next, rl(w + W_ctlList)); wl(w + W_ctlList, h);
    wl(p + C_owner, w); rect_put(p + C_rect, r); wb(p + C_vis, (unsigned)(vis != 0)); wb(p + C_hilite, 0);
    ww(p + C_value, (unsigned)val); ww(p + C_min, (unsigned)min); ww(p + C_max, (unsigned)max);
    wl(p + C_def, (unsigned)(proc & 0xFF) << 24); wl(p + C_refCon, refCon);
    memcpy(g_ram + p + C_title, title, (size_t)title[0] + 1);
    if (vis) ctl_draw(h);
    return h;
}
void ctl_dispose(unsigned c){
    unsigned r = cp(c), w = rl(r + C_owner), p = rl(w + W_ctlList), prev = 0;
    while (p && p != c){ prev = p; p = rl(cp(p) + C_next); }
    if (p){ if (prev) wl(cp(prev) + C_next, rl(r + C_next)); else wl(w + W_ctlList, rl(r + C_next)); }
    mm_disposhandle(c);
}
void ctl_kill_all(unsigned w){ while (rl(w + W_ctlList)) ctl_dispose(rl(w + W_ctlList)); }

static int test_part(unsigned c, int h, int v){
    unsigned r = cp(c); Rect b = rect_at(r + C_rect);
    if (!rb(r + C_vis) || rb(r + C_hilite) == 255 || !pt_in(b, h, v)) return 0;
    if (ckind(c) == 0) return 10;
    if (ckind(c) != 16) return 11;
    if (!sb_active(r)) return 0;
    {   int vert = sb_vert(b), pos = vert ? v - b.top : h - b.left, len = sb_len(b); Rect t = sb_thumb(r, -1);
        if (pos < 16) return 20;
        if (pos >= len - 16) return 21;
        if (pt_in(t, h, v)) return 129;
        return (vert ? v < t.top : h < t.left) ? 22 : 23; }
}
unsigned ctl_find(unsigned w, int h, int v, int *part){
    unsigned c;
    for (c = rl(w + W_ctlList); c; c = rl(cp(c) + C_next)){ int p = test_part(c, h, v); if (p){ *part = p; return c; } }
    *part = 0; return 0;
}

static void set_value(unsigned c, int v){
    unsigned r = cp(c); int mn = sw(r + C_min), mx = sw(r + C_max);
    if (v < mn) v = mn; if (v > mx) v = mx;
    if (sw(r + C_value) != v){ ww(r + C_value, (unsigned)v); ctl_draw(c); }
}

/* track with the button down; h,v in the owner's local coordinates */
int ctl_track(unsigned c, int h, int v, unsigned action){
    unsigned r = cp(c), w = rl(r + C_owner); int part = test_part(c, h, v), k = ckind(c), in, gh, gv;
    if (!part) return 0;
    if (action == 0xFFFFFFFFu) action = rl(r + C_action);
    wm_global_origin(w, &gh, &gv);
    if (k != 16){
        int last = -1;
        for (;;){
            int mh, mv; evt_mouse(&mh, &mv); mh -= gh; mv -= gv;
            in = test_part(c, mh, mv) == part;
            if (in != last){ wb(r + C_hilite, in ? (unsigned)part : 0); ctl_draw(c); last = in; }
            if (action && in) call68k(action, 0, "");
            if (!evt_mouse_down() || g_quit) break;
            hle_idle(5);
        }
        wb(r + C_hilite, 0); ctl_draw(c);
        return in ? part : 0;
    }
    if (part == 129){                                        /* thumb: drag a ghost, then set the value */
        Rect b = rect_at(r + C_rect), t0 = sb_thumb(r, -1); int vert = sb_vert(b), span = sb_len(b) - 48, start = vert ? v : h, off, newpos;
        int base = vert ? t0.top - b.top - 15 : t0.left - b.left - 15, mn = sw(r + C_min), mx = sw(r + C_max);
        unsigned save; with_port(w, &save);
        ww(w + GP_pnMode, 10); memcpy(g_ram + MA(w + GP_pnPat), PAT_GRAY, 8);
        newpos = base;
        qd_frame_rect(w, t0);
        while (evt_mouse_down() && !g_quit){
            int mh, mv; evt_mouse(&mh, &mv); mh -= gh; mv -= gv;
            off = (vert ? mv : mh) - start;
            {   int np = base + off; if (np < 0) np = 0; if (np > span) np = span;
                if (np != newpos){ qd_frame_rect(w, sb_thumb(r, newpos)); newpos = np; qd_frame_rect(w, sb_thumb(r, newpos)); } }
            hle_idle(5);
        }
        qd_frame_rect(w, sb_thumb(r, newpos));
        ww(w + GP_pnMode, 8); memcpy(g_ram + MA(w + GP_pnPat), PAT_BLACK, 8);
        qd_setport(save);
        if (span > 0) set_value(c, mn + (int)((long)newpos * (mx - mn) + span / 2) / span);
        ctl_draw(c);
        return part;
    }
    {   /* arrows and page regions: repeat while held, calling the action proc */
        unsigned t0 = rl(LM_Ticks); int first = 1;
        for (;;){
            int mh, mv; evt_mouse(&mh, &mv); mh -= gh; mv -= gv;
            in = test_part(c, mh, mv) == part;
            wb(r + C_hilite, in ? (unsigned)part : 0); if (part == 20 || part == 21) ctl_draw(c);
            if (in && (first || rl(LM_Ticks) - t0 > 20)){
                if (action) call68k(action, 0, "LW", c, (unsigned)part);
                first = 0;
            }
            if (!evt_mouse_down() || g_quit) break;
            hle_idle(30);
        }
        wb(r + C_hilite, 0); ctl_draw(c);
        return in ? part : 0;
    }
}

void ctl_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv; unsigned c, r;
    t->handled = 1;
    switch (word){
    case 0xA954: {                                                                /* NewControl */
        unsigned char title[256]; unsigned tp = ARGL(14); memcpy(title, g_ram + MA(tp), (size_t)rb(tp) + 1);
        RETL(26, ctl_new(ARGL(22), rect_at(ARGL(18)), title, ARGB(12), ARGW(10), ARGW(8), ARGW(6), ARGW(4), ARGL(0))); } break;
    case 0xA9BE: {                                                                /* GetNewControl(id, owner) */
        unsigned h = rs_get(0x434E544Cu, ARGW(4)), d; unsigned char title[256];
        if (!h){ RETL(6, 0); break; }
        d = MA(rl(h)); memcpy(title, g_ram + d + 22, (size_t)rb(d + 22) + 1);
        RETL(6, ctl_new(ARGL(0), rect_at(d), title, rb(d + 10), sw(d + 8), sw(d + 14), sw(d + 12), sw(d + 16), rl(d + 18))); } break;
    case 0xA955: ctl_dispose(ARGL(0)); POP(4); break;                            /* DisposeControl */
    case 0xA956: ctl_kill_all(ARGL(0)); POP(4); break;                           /* KillControls */
    case 0xA957: c = ARGL(0); wb(cp(c) + C_vis, 1); ctl_draw(c); POP(4); break;  /* ShowControl */
    case 0xA958: { unsigned save; c = ARGL(0); r = cp(c);                         /* HideControl */
                   if (rb(r + C_vis)){ unsigned w = rl(r + C_owner); with_port(w, &save); qd_fill_rect(w, rect_at(r + C_rect), PAT_WHITE, 8); qd_setport(save);
                                       wm_invalidate(w, rect_at(r + C_rect)); }
                   wb(r + C_vis, 0); POP(4); } break;
    case 0xA959: { Rect b; c = ARGL(4); r = cp(c); b = rect_at(r + C_rect);      /* MoveControl(c, h, v) */
                   rect_put(r + C_rect, rect_off(b, ARGW(2) - b.left, ARGW(0) - b.top)); ctl_draw(c); POP(8); } break;
    case 0xA95C: { Rect b; c = ARGL(4); r = cp(c); b = rect_at(r + C_rect);      /* SizeControl(c, w, h) */
                   b.right = (short)(b.left + ARGW(2)); b.bottom = (short)(b.top + ARGW(0)); rect_put(r + C_rect, b); ctl_draw(c); POP(8); } break;
    case 0xA95D: c = ARGL(2); if (rb(cp(c) + C_hilite) != (ARGUW(0) & 0xFF)){ wb(cp(c) + C_hilite, ARGUW(0) & 0xFF); ctl_draw(c); } POP(6); break;   /* HiliteControl */
    case 0xA95A: RETL(4, rl(cp(ARGL(0)) + C_refCon)); break;                     /* GetCRefCon */
    case 0xA95B: wl(cp(ARGL(4)) + C_refCon, ARGL(0)); POP(8); break;             /* SetCRefCon */
    case 0xA95E: { unsigned s = cp(ARGL(4)) + C_title; memcpy(g_ram + MA(ARGL(0)), g_ram + s, (size_t)rb(s) + 1); POP(8); } break;   /* GetCTitle */
    case 0xA95F: { unsigned s = ARGL(0); c = ARGL(4); memcpy(g_ram + cp(c) + C_title, g_ram + MA(s), (size_t)rb(s) + 1); ctl_draw(c); POP(8); } break;   /* SetCTitle */
    case 0xA960: RETW(4, rw(cp(ARGL(0)) + C_value)); break;                      /* GetCtlValue */
    case 0xA961: RETW(4, rw(cp(ARGL(0)) + C_min)); break;                        /* GetCtlMin */
    case 0xA962: RETW(4, rw(cp(ARGL(0)) + C_max)); break;                        /* GetCtlMax */
    case 0xA963: c = ARGL(2); set_value(c, ARGW(0)); POP(6); break;              /* SetCtlValue */
    case 0xA964: c = ARGL(2); ww(cp(c) + C_min, ARGUW(0)); set_value(c, sw(cp(c) + C_value)); ctl_draw(c); POP(6); break;   /* SetCtlMin */
    case 0xA965: c = ARGL(2); ww(cp(c) + C_max, ARGUW(0)); set_value(c, sw(cp(c) + C_value)); ctl_draw(c); POP(6); break;   /* SetCtlMax */
    case 0xA96A: RETL(4, rl(cp(ARGL(0)) + C_action)); break;                     /* GetCtlAction */
    case 0xA96B: wl(cp(ARGL(4)) + C_action, ARGL(0)); POP(8); break;             /* SetCtlAction */
    case 0xA966: RETW(8, test_part(ARGL(4), ARGW(2), ARGW(0))); break;           /* TestControl(c, pt) */
    case 0xA96C: { int part; unsigned f = ctl_find(ARGL(4), ARGW(10), ARGW(8), &part);   /* FindControl(pt, w, VAR c) */
                   wl(ARGL(0), f); RETW(12, part); } break;
    case 0xA968: RETW(12, ctl_track(ARGL(8), ARGW(6), ARGW(4), ARGL(0))); break; /* TrackControl(c, pt, action) */
    case 0xA967: POP(18); break;                                                 /* DragControl */
    case 0xA969: ctl_draw_all(ARGL(0)); POP(4); break;                           /* DrawControls */
    case 0xA96D: ctl_draw(ARGL(0)); POP(4); break;                               /* Draw1Control */
    case 0xA953: ctl_draw_all(ARGL(4)); POP(8); break;                           /* UpdtControl */
    default: t->handled = 0;
    }
}
