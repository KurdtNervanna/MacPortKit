/* Menu Manager: the menu bar and pull-down menus, drawn into the screen in the
 * System 6 style (Chicago 12) from the game's own MenuInfo records.
 *
 * MenuInfo: menuID, menuWidth, menuHeight, menuProc, enableFlags, then the title and
 * items as Pascal strings, each item followed by [icon][cmdChar][mark][style].
 */
#include "mac.h"

#define MBAR_H 20
#define ITEM_H 16
#define MAXMENUS 32

static unsigned g_menus[MAXMENUS]; static int g_nmenus;
static short g_tl[MAXMENUS], g_tr[MAXMENUS];         /* title extents in the bar */
static int g_hilite_id;

static unsigned mdata(unsigned mh){ return mh ? MA(rl(mh)) : 0; }
static int menu_id(unsigned mh){ return sw(mdata(mh)); }

/* address of item n's string (1-based); 0 if none */
static unsigned item_addr(unsigned mh, int n){
    unsigned p = mdata(mh) + 14; int i;
    if (!mdata(mh)) return 0;
    p += rb(p) + 1u;                                          /* skip the title */
    for (i = 1; rb(p); i++){
        if (i == n) return p;
        p += rb(p) + 1u + 4u;
    }
    return 0;
}
static int count_items(unsigned mh){ unsigned p = mdata(mh) + 14; int n = 0; if (!mdata(mh)) return 0; p += rb(p) + 1u; while (rb(p)){ n++; p += rb(p) + 5u; } return n; }
static int item_enabled(unsigned mh, int n){ unsigned f = rl(mdata(mh) + 10); if (!(f & 1)) return 0; return n > 31 ? 1 : (int)((f >> n) & 1); }

static void port_text(unsigned port){ ww(port + GP_txFont, 0); ww(port + GP_txSize, 12); wb(port + GP_txFace, 0); ww(port + GP_txMode, 1); }
static int str_w(unsigned port, unsigned s){ return qd_text_width(port, g_ram + MA(s) + 1, (int)rb(s)); }

static void calc_size(unsigned mh){
    unsigned save = qd_port(), port = rl(LM_WMgrPort), p; int i, n = count_items(mh), w = 0;
    unsigned char sv[GP_SIZE]; memcpy(sv, g_ram + MA(port), GP_SIZE);
    port_text(port);
    for (i = 1; i <= n; i++){
        int iw; p = item_addr(mh, i);
        iw = str_w(port, p) + 30;
        if (rb(p + rb(p) + 2)) iw += 30;                      /* command key */
        if (iw > w) w = iw;
    }
    ww(mdata(mh) + 2, (unsigned)w); ww(mdata(mh) + 4, (unsigned)(n * ITEM_H));
    memcpy(g_ram + MA(port), sv, GP_SIZE); qd_setport(save);
}

/* ---- bar ------------------------------------------------------------------------- */
static void draw_title(unsigned port, int i, int hil){
    unsigned t = mdata(g_menus[i]) + 14; Rect r = mkrect(0, g_tl[i], MBAR_H - 1, g_tr[i]);
    qd_fill_rect(port, r, PAT_WHITE, 8);
    ww(port + GP_pnLoc, 14); ww(port + GP_pnLoc + 2, (unsigned)(g_tl[i] + 8));
    qd_draw_text(port, g_ram + t + 1, (int)rb(t));
    if (!(rl(mdata(g_menus[i]) + 10) & 1)) qd_fill_rect(port, r, PAT_GRAY, 11);   /* dimmed */
    if (hil) qd_fill_rect(port, r, PAT_BLACK, 10);
}
void menu_draw_bar(void){
    unsigned port = rl(LM_WMgrPort), save = qd_port(); int i, x = 10;
    unsigned char sv[GP_SIZE];
    if (!port) return;
    memcpy(sv, g_ram + MA(port), GP_SIZE);
    rgn_set_rect(rl(port + GP_clipRgn), mkrect(0, 0, MBAR_H, g_scr_w));
    port_text(port);
    qd_fill_rect(port, mkrect(0, 0, MBAR_H - 1, g_scr_w), PAT_WHITE, 8);
    qd_fill_rect(port, mkrect(MBAR_H - 1, 0, MBAR_H, g_scr_w), PAT_BLACK, 8);
    for (i = 0; i < g_nmenus; i++){
        unsigned t = mdata(g_menus[i]) + 14;
        g_tl[i] = (short)x; x += str_w(port, t) + 16; g_tr[i] = (short)x;
        draw_title(port, i, menu_id(g_menus[i]) == g_hilite_id && g_hilite_id);
    }
    /* rounded top corners of the screen */
    qd_fill_rect(port, mkrect(0,0,1,5), PAT_BLACK, 8); qd_fill_rect(port, mkrect(1,0,2,3), PAT_BLACK, 8); qd_fill_rect(port, mkrect(2,0,5,1), PAT_BLACK, 8);
    qd_fill_rect(port, mkrect(0,g_scr_w-5,1,g_scr_w), PAT_BLACK, 8); qd_fill_rect(port, mkrect(1,g_scr_w-3,2,g_scr_w), PAT_BLACK, 8); qd_fill_rect(port, mkrect(2,g_scr_w-1,5,g_scr_w), PAT_BLACK, 8);
    memcpy(g_ram + MA(port), sv, GP_SIZE);
    rgn_set_rect(rl(port + GP_clipRgn), mkrect(-32767, -32767, 32767, 32767));
    qd_setport(save);
    hle_dirty();
}
static void hilite(int id){
    int i; unsigned port = rl(LM_WMgrPort), save = qd_port(); unsigned char sv[GP_SIZE];
    if (id == g_hilite_id) return;
    memcpy(sv, g_ram + MA(port), GP_SIZE); port_text(port);
    for (i = 0; i < g_nmenus; i++){
        int mid = menu_id(g_menus[i]);
        if (mid == g_hilite_id || mid == id) draw_title(port, i, mid == id && id);
    }
    memcpy(g_ram + MA(port), sv, GP_SIZE); qd_setport(save);
    g_hilite_id = id; hle_dirty();
}

/* ---- pull-down -------------------------------------------------------------------- */
static Rect menu_rect(int i){
    unsigned d = mdata(g_menus[i]); int w = sw(d + 2), h = sw(d + 4), l = g_tl[i];
    if (l + w + 2 > g_scr_w) l = g_scr_w - w - 2;
    return mkrect(MBAR_H - 1, l, MBAR_H - 1 + h + 2, l + w + 2);
}
static void draw_item(unsigned port, unsigned mh, Rect mr, int n, int hil){
    unsigned p = item_addr(mh, n); int y = mr.top + 1 + (n-1) * ITEM_H, en = item_enabled(mh, n);
    Rect ir = mkrect(y, mr.left + 1, y + ITEM_H, mr.right - 1);
    qd_fill_rect(port, ir, PAT_WHITE, 8);
    if (rb(p) == 1 && rb(p+1) == '-' ){                              /* separator */
        int x; for (x = ir.left; x < ir.right; x += 2) qd_fill_rect(port, mkrect(y + 8, x, y + 9, x + 1), PAT_BLACK, 8);
        return;
    }
    {   unsigned extra = p + rb(p) + 1u; int cmd = (int)rb(extra + 1), mark = (int)rb(extra + 2);
        wb(port + GP_txFace, rb(extra + 3));
        ww(port + GP_pnLoc, (unsigned)(y + 12));
        if (mark){ unsigned char m = (unsigned char)mark; ww(port + GP_pnLoc + 2, (unsigned)(ir.left + 2)); qd_draw_text(port, &m, 1); }
        ww(port + GP_pnLoc + 2, (unsigned)(ir.left + 14));
        qd_draw_text(port, g_ram + p + 1, (int)rb(p));
        wb(port + GP_txFace, 0);
        if (cmd){ unsigned char c2[2]; c2[0] = 0x11; c2[1] = (unsigned char)cmd;
                  ww(port + GP_pnLoc + 2, (unsigned)(ir.right - 28)); qd_draw_text(port, c2, 2); }
    }
    if (!en) qd_fill_rect(port, ir, PAT_GRAY, 11);
    if (hil && en) qd_fill_rect(port, ir, PAT_BLACK, 10);
}
static unsigned char *save_bits(Rect r){
    int y, rb_ = g_scr_rb; unsigned char *b = (unsigned char*)malloc((size_t)(r.bottom - r.top) * (size_t)rb_ + 1);
    for (y = r.top; y < r.bottom; y++) memcpy(b + (size_t)(y - r.top) * (size_t)rb_, g_ram + SCREEN_BASE + (unsigned)(y * rb_), (size_t)rb_);
    return b;
}
static void restore_bits(Rect r, unsigned char *b){
    int y, rb_ = g_scr_rb;
    for (y = r.top; y < r.bottom; y++) memcpy(g_ram + SCREEN_BASE + (unsigned)(y * rb_), b + (size_t)(y - r.top) * (size_t)rb_, (size_t)rb_);
    free(b); hle_dirty();
}
static void draw_menu(unsigned port, int i, Rect mr){
    int n, k = count_items(g_menus[i]);
    rgn_set_rect(rl(port + GP_clipRgn), mkrect(-32767, -32767, 32767, 32767));
    qd_fill_rect(port, mr, PAT_WHITE, 8);
    qd_frame_rect(port, mr);
    qd_fill_rect(port, mkrect(mr.top + 2, mr.right, mr.bottom + 1, mr.right + 1), PAT_BLACK, 8);
    qd_fill_rect(port, mkrect(mr.bottom, mr.left + 2, mr.bottom + 1, mr.right + 1), PAT_BLACK, 8);
    for (n = 1; n <= k; n++) draw_item(port, g_menus[i], mr, n, 0);
}

int menu_select(int h, int v){
    unsigned port = rl(LM_WMgrPort), save = qd_port(); unsigned char sv[GP_SIZE];
    int cur = -1, item = 0, result = 0, i; Rect mr = mkrect(0,0,0,0), sr = mr; unsigned char *bits = 0;
    (void)v;
    memcpy(sv, g_ram + MA(port), GP_SIZE); port_text(port);
    for (;;){
        int over = -1, it = 0;
        evt_mouse(&h, &v);
        if (v < MBAR_H){ for (i = 0; i < g_nmenus; i++) if (h >= g_tl[i] && h < g_tr[i]){ over = i; break; } }
        else if (cur >= 0 && pt_in(mr, h, v)){ over = cur; it = (v - mr.top - 1) / ITEM_H + 1; if (it > count_items(g_menus[cur])) it = 0; }
        else over = cur >= 0 && v >= MBAR_H ? cur : -1;
        if (over != cur && (v < MBAR_H || over < 0)){
            if (bits){ restore_bits(sr, bits); bits = 0; }
            cur = over; item = 0;
            hilite(cur >= 0 ? menu_id(g_menus[cur]) : 0);
            if (cur >= 0 && (rl(mdata(g_menus[cur]) + 10) & 1)){
                calc_size(g_menus[cur]); port_text(port);
                mr = menu_rect(cur); sr = mkrect(mr.top, 0, mr.bottom + 1 > g_scr_h ? g_scr_h : mr.bottom + 1, g_scr_w);
                bits = save_bits(sr); draw_menu(port, cur, mr);
            }
            hle_dirty();
        }
        if (cur >= 0 && bits && it != item){
            if (item) draw_item(port, g_menus[cur], mr, item, 0);
            item = it;
            if (item) draw_item(port, g_menus[cur], mr, item, 1);
            hle_dirty();
        }
        if (!evt_mouse_down() || g_quit) break;
        hle_idle(5);
    }
    if (cur >= 0 && item && bits && item_enabled(g_menus[cur], item)){
        int k; for (k = 0; k < 3; k++){ draw_item(port, g_menus[cur], mr, item, 0); hle_idle(40); draw_item(port, g_menus[cur], mr, item, 1); hle_idle(40); }
        result = menu_id(g_menus[cur]) << 16 | item;
    }
    if (bits) restore_bits(sr, bits);
    if (!result) hilite(0);
    memcpy(g_ram + MA(port), sv, GP_SIZE); qd_setport(save);
    return result;
}

int menu_key(int ch){
    int i, n, k;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    for (i = g_nmenus - 1; i >= 0; i--){
        if (!(rl(mdata(g_menus[i]) + 10) & 1)) continue;
        k = count_items(g_menus[i]);
        for (n = 1; n <= k; n++){
            unsigned p = item_addr(g_menus[i], n); int c = (int)rb(p + rb(p) + 2);
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c && c == ch && item_enabled(g_menus[i], n)){ hilite(menu_id(g_menus[i])); return menu_id(g_menus[i]) << 16 | n; }
        }
    }
    return 0;
}

/* ---- building menus --------------------------------------------------------------- */
static void append_items(unsigned mh, unsigned s){
    char buf[256]; int n = (int)rb(s), i = 0;
    memcpy(buf, g_ram + MA(s) + 1, (size_t)n); buf[n] = 0;
    while (i < n){
        char name[256]; int nl = 0, icon = 0, cmd = 0, mark = 0, style = 0, dis = 0, end, sz, cnt;
        unsigned d, at;
        for (end = i; end < n && buf[end] != ';' && buf[end] != '\r'; end++);
        for (; i < end; i++){
            char c = buf[i];
            if (c == '^' && i + 1 < end){ icon = buf[++i] - '0'; }
            else if (c == '!' && i + 1 < end){ mark = (unsigned char)buf[++i]; }
            else if (c == '<' && i + 1 < end){ char st = buf[++i]; style |= st=='B'?1:st=='I'?2:st=='U'?4:st=='O'?8:st=='S'?16:0; }
            else if (c == '/' && i + 1 < end){ cmd = (unsigned char)buf[++i]; }
            else if (c == '(' ){ dis = 1; }
            else name[nl++] = c;
        }
        i = end + 1;
        if (!nl){ name[nl++] = ' '; }
        /* insert before the terminating 0 */
        sz = (int)mm_handlesize(mh); cnt = count_items(mh);
        mm_sethandlesize(mh, (unsigned)(sz + nl + 5));
        d = mdata(mh); at = d + (unsigned)sz - 1;              /* the terminator */
        wb(at, (unsigned)nl); memcpy(g_ram + at + 1, name, (size_t)nl);
        wb(at + 1 + nl, (unsigned)icon); wb(at + 2 + nl, (unsigned)cmd); wb(at + 3 + nl, (unsigned)mark); wb(at + 4 + nl, (unsigned)style);
        wb(at + 5 + nl, 0);
        if (cnt + 1 <= 31){ unsigned f = rl(d + 10); if (dis) f &= ~(1u << (cnt + 1)); else f |= 1u << (cnt + 1); wl(d + 10, f); }
    }
    calc_size(mh);
}
static unsigned new_menu(int id, unsigned title){
    unsigned n = rb(title), h = mm_newhandle(14 + n + 2, 1), d = mdata(h);
    ww(d, (unsigned)id); wl(d + 10, 0xFFFFFFFFu);
    memcpy(g_ram + d + 14, g_ram + MA(title), n + 1); wb(d + 15 + n, 0);
    return h;
}
static int find_menu(int id){ int i; for (i = 0; i < g_nmenus; i++) if (menu_id(g_menus[i]) == id) return i; return -1; }
static unsigned get_mhandle(int id){ int i = find_menu(id); return i >= 0 ? g_menus[i] : 0; }

void menu_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv;
    t->handled = 1;
    switch (word){
    case 0xA930: g_nmenus = 0; g_hilite_id = 0; menu_draw_bar(); POP(0); break;        /* InitMenus */
    case 0xA931: RETL(6, new_menu(ARGW(4), ARGL(0))); break;                           /* NewMenu(id, title) */
    case 0xA9BF: { unsigned h = rs_get(0x4D454E55u, ARGW(0)); if (h) calc_size(h); RETL(2, h); } break;   /* GetMenu */
    case 0xA932: { unsigned h = ARGL(0); int i = -1, k; for (k = 0; k < g_nmenus; k++) if (g_menus[k] == h) i = k;   /* DisposeMenu */
                   if (i >= 0){ for (k = i; k < g_nmenus - 1; k++) g_menus[k] = g_menus[k+1]; g_nmenus--; }
                   POP(4); } break;
    case 0xA933: append_items(ARGL(4), ARGL(0)); POP(8); break;                        /* AppendMenu */
    case 0xA935: {                                                                     /* InsertMenu(menu, beforeID) */
        unsigned h = ARGL(2); int before = ARGW(0), i, at = g_nmenus;
        if (before == -1 || find_menu(menu_id(h)) >= 0 || g_nmenus >= MAXMENUS){ POP(6); break; }
        if (before){ i = find_menu(before); if (i >= 0) at = i; }
        for (i = g_nmenus; i > at; i--) g_menus[i] = g_menus[i-1];
        g_menus[at] = h; g_nmenus++; calc_size(h); POP(6); } break;
    case 0xA936: { int i = find_menu(ARGW(0)), k; if (i >= 0){ for (k = i; k < g_nmenus - 1; k++) g_menus[k] = g_menus[k+1]; g_nmenus--; } POP(2); } break;   /* DeleteMenu */
    case 0xA934: g_nmenus = 0; POP(0); break;                                          /* ClearMenuBar */
    case 0xA937: menu_draw_bar(); POP(0); break;                                       /* DrawMenuBar */
    case 0xA938: hilite(ARGW(0)); POP(2); break;                                       /* HiliteMenu */
    case 0xA939: case 0xA93A: {                                                        /* EnableItem / DisableItem */
        unsigned d = mdata(ARGL(2)); int item = ARGW(0); unsigned f;
        if (d && item <= 31){ f = rl(d + 10); if (word == 0xA939) f |= 1u << item; else f &= ~(1u << item); wl(d + 10, f); }
        POP(6); } break;
    case 0xA93B: { unsigned h = mm_newhandle((unsigned)(2 + g_nmenus * 4), 1), d = MA(rl(h)); int i;   /* GetMenuBar */
                   ww(d, (unsigned)g_nmenus); for (i = 0; i < g_nmenus; i++) wl(d + 2 + (unsigned)i*4, g_menus[i]); RETL(0, h); } break;
    case 0xA93C: { unsigned h = ARGL(0), d = h ? MA(rl(h)) : 0; int i;                 /* SetMenuBar */
                   if (d && mm_handlesize(h) >= 2){ g_nmenus = sw(d); if (g_nmenus > MAXMENUS) g_nmenus = MAXMENUS; for (i = 0; i < g_nmenus; i++) g_menus[i] = rl(d + 2 + (unsigned)i*4); }
                   POP(4); } break;
    case 0xA9C0: { unsigned mb = rs_get(0x4D424152u, ARGW(0)), d = mb ? MA(rl(mb)) : 0, h = 0; int n, i;   /* GetNewMBar */
                   if (d){ n = sw(d); h = mm_newhandle((unsigned)(2 + n*4), 1);
                           ww(MA(rl(h)), (unsigned)n);
                           for (i = 0; i < n; i++){ unsigned m = rs_get(0x4D454E55u, sw(d + 2 + (unsigned)i*2)); if (m) calc_size(m); wl(MA(rl(h)) + 2 + (unsigned)i*4, m); } }
                   RETL(2, h); } break;
    case 0xA93D: RETL(4, menu_select(ARGW(2), ARGW(0))); break;                        /* MenuSelect(startPt) */
    case 0xA93E: RETL(2, menu_key(ARGUW(0) & 0xFF)); break;                            /* MenuKey */
    case 0xA945: { unsigned p = item_addr(ARGL(4), ARGW(2)); if (p) wb(p + rb(p) + 3, ARGB(0) ? 0x12 : 0); POP(8); } break;   /* CheckItem */
    case 0xA944: { unsigned p = item_addr(ARGL(4), ARGW(2)); if (p) wb(p + rb(p) + 3, ARGUW(0) & 0xFF); POP(8); } break;      /* SetItemMark */
    case 0xA943: { unsigned p = item_addr(ARGL(8), ARGW(4)); ww(ARGL(0), p ? rb(p + rb(p) + 3) : 0); POP(10); } break;       /* GetItemMark */
    case 0xA942: { unsigned p = item_addr(ARGL(4), ARGW(2)); if (p) wb(p + rb(p) + 4, ARGB(0)); POP(8); } break;              /* SetItemStyle */
    case 0xA941: { unsigned p = item_addr(ARGL(8), ARGW(4)); wb(ARGL(0), p ? rb(p + rb(p) + 4) : 0); POP(10); } break;       /* GetItemStyle */
    case 0xA940: { unsigned p = item_addr(ARGL(4), ARGW(2)); if (p) wb(p + rb(p) + 1, ARGB(0)); POP(8); } break;              /* SetItemIcon */
    case 0xA93F: { unsigned p = item_addr(ARGL(8), ARGW(4)); ww(ARGL(0), p ? rb(p + rb(p) + 1) : 0); POP(10); } break;       /* GetItemIcon */
    case 0xA946: { unsigned p = item_addr(ARGL(8), ARGW(4)), d = ARGL(0);             /* GetItem */
                   if (p) memcpy(g_ram + MA(d), g_ram + MA(p), (size_t)rb(p) + 1); else wb(d, 0); POP(10); } break;
    case 0xA947: {                                                                     /* SetItem(menu, item, str) */
        unsigned mh = ARGL(6), s = ARGL(0), p = item_addr(mh, ARGW(4));
        if (p){
            int oldn = (int)rb(p), newn = (int)rb(s), delta = newn - oldn; unsigned off = p - mdata(mh), sz = mm_handlesize(mh); unsigned char tmp[256];
            memcpy(tmp, g_ram + MA(s), (size_t)newn + 1);
            if (delta > 0) mm_sethandlesize(mh, sz + (unsigned)delta);
            p = mdata(mh) + off;
            memmove(g_ram + p + 1 + newn, g_ram + p + 1 + oldn, sz - off - 1 - (unsigned)oldn);
            memcpy(g_ram + p, tmp, (size_t)newn + 1);
            if (delta < 0) mm_sethandlesize(mh, sz + (unsigned)delta);
            calc_size(mh);
        }
        POP(10); } break;
    case 0xA948: calc_size(ARGL(0)); POP(4); break;                                    /* CalcMenuSize */
    case 0xA949: RETL(2, get_mhandle(ARGW(0))); break;                                 /* GetMHandle */
    case 0xA94A: POP(2); break;                                                        /* SetMenuFlash */
    case 0xA94C: { unsigned port = rl(LM_WMgrPort); qd_fill_rect(port, mkrect(0, 0, MBAR_H - 1, g_scr_w), PAT_BLACK, 10); POP(2); } break;   /* FlashMenuBar */
    case 0xA94D: case 0xA951: POP(word == 0xA94D ? 8 : 10); break;                    /* AddResMenu / InsertResMenu */
    case 0xA950: RETW(4, count_items(ARGL(0))); break;                                 /* CountMItems */
    case 0xA952: {                                                                     /* DelMenuItem */
        unsigned mh = ARGL(2), p = item_addr(mh, ARGW(0));
        if (p){ unsigned len = rb(p) + 5u, off = p - mdata(mh), sz = mm_handlesize(mh);
                memmove(g_ram + p, g_ram + p + len, sz - off - len); mm_sethandlesize(mh, sz - len); calc_size(mh); }
        POP(6); } break;
    default: t->handled = 0;
    }
}
