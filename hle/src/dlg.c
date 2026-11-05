/* Dialog Manager: modal and modeless dialogs and alerts built from DLOG/ALRT/DITL.
 *
 * The item list is a copy of the DITL with each placeholder filled in as on a Mac
 * (control handle, text handle, icon/picture handle or user-item proc), so
 * GetDItem/SetDItem behave exactly as the game expects. Edit text fields get a
 * small single-line editor in place of TextEdit.
 */
#include "mac.h"

#define D_items   156
#define D_textH   160
#define D_editFld 164
#define D_editOpen 166
#define D_aDef    168
#define W_kind    108
#define W_visible 110
#define W_refCon  152

static unsigned g_param[4];                  /* ParamText handles */
static int g_sel_start, g_sel_end;           /* edit selection (single active field) */
static unsigned g_caret_tick; static int g_caret_on;

int dlg_is_dialog(unsigned w){ return w && sw(w + W_kind) == 2; }

/* item k (1-based): address of its record in the item list, or 0 */
static unsigned item_rec(unsigned d, int k){
    unsigned h = rl(d + D_items), p; int n, i;
    if (!h || !MA(rl(h))) return 0;
    p = MA(rl(h)); n = sw(p) + 1; p += 2;
    if (k < 1 || k > n) return 0;
    for (i = 1; i < k; i++){ int len = (int)rb(p + 13); p += 14u + (unsigned)((len + 1) & ~1); }
    return p;
}
static int item_count(unsigned d){ unsigned h = rl(d + D_items); return (h && MA(rl(h))) ? sw(MA(rl(h))) + 1 : 0; }
#define I_handle(p) rl((p))
#define I_rect(p)   rect_at((p) + 4)
#define I_type(p)   ((int)rb((p) + 12))

static void set_dlg_font(unsigned d){ ww(d + GP_txFont, 0); ww(d + GP_txSize, 12); wb(d + GP_txFace, 0); ww(d + GP_txMode, 1); }

/* text with ^0..^3 replaced by the ParamText strings */
static int expand(unsigned src, int n, unsigned char *out, int max){
    int i, o = 0;
    for (i = 0; i < n && o < max; i++){
        int c = (int)rb(src + (unsigned)i);
        if (c == '^' && i + 1 < n && rb(src + (unsigned)i + 1) >= '0' && rb(src + (unsigned)i + 1) <= '3'){
            unsigned h = g_param[rb(src + (unsigned)i + 1) - '0']; i++;
            if (h && MA(rl(h))){ unsigned s = MA(rl(h)); int k; for (k = 0; k < (int)rb(s) && o < max; k++) out[o++] = (unsigned char)rb(s + 1 + (unsigned)k); }
            continue;
        }
        out[o++] = (unsigned char)c;
    }
    return o;
}
/* word-wrapped text in a box (TextBox, left aligned) */
static void text_box(unsigned port, const unsigned char *s, int n, Rect r){
    int a, de, wm, l, x, y, i = 0, lh;
    qd_font_info(port, &a, &de, &wm, &l); lh = a + de + l;
    y = r.top + a;
    while (i < n && y - a < r.bottom){
        int start = i, last_space = -1, w = 0, end;
        while (i < n && s[i] != '\r'){
            int cw = qd_text_width(port, s + i, 1);
            if (w + cw > r.right - r.left && i > start){ break; }
            if (s[i] == ' ') last_space = i;
            w += cw; i++;
        }
        end = i;
        if (i < n && s[i] != '\r' && last_space > start){ end = last_space; i = last_space + 1; }
        else if (i < n && s[i] == '\r') i++;
        x = r.left;
        ww(port + GP_pnLoc, (unsigned)y); ww(port + GP_pnLoc + 2, (unsigned)x);
        qd_draw_text(port, s + start, end - start);
        y += lh;
    }
}

static void draw_edit(unsigned d, int k, int focus){
    unsigned p = item_rec(d, k), h = I_handle(p); Rect r = I_rect(p);
    unsigned char buf[512]; int n = 0;
    qd_fill_rect(d, mkrect(r.top - 3, r.left - 3, r.bottom + 3, r.right + 3), PAT_WHITE, 8);
    ww(d + GP_pnSize, 1); ww(d + GP_pnSize + 2, 1); ww(d + GP_pnMode, 8); memcpy(g_ram + MA(d + GP_pnPat), PAT_BLACK, 8);
    qd_frame_rect(d, mkrect(r.top - 3, r.left - 3, r.bottom + 3, r.right + 3));
    if (h && MA(rl(h))){ n = (int)mm_handlesize(h); if (n > 511) n = 511; memcpy(buf, g_ram + MA(rl(h)), (size_t)n); }
    {   int a, de, wm, l; qd_font_info(d, &a, &de, &wm, &l);
        ww(d + GP_pnLoc, (unsigned)(r.top + a)); ww(d + GP_pnLoc + 2, (unsigned)r.left);
        qd_draw_text(d, buf, n);
        if (focus){
            int s0 = g_sel_start < n ? g_sel_start : n, s1 = g_sel_end < n ? g_sel_end : n;
            int x0 = r.left + qd_text_width(d, buf, s0), x1 = r.left + qd_text_width(d, buf, s1);
            if (s1 > s0) qd_fill_rect(d, mkrect(r.top, x0, r.top + a + de, x1), PAT_BLACK, 10);
            else if (g_caret_on) qd_fill_rect(d, mkrect(r.top, x0, r.top + a + de, x0 + 1), PAT_BLACK, 10);
        }
    }
}

static void draw_item(unsigned d, int k){
    unsigned p = item_rec(d, k), save = qd_port(); int type; Rect r;
    if (!p) return;
    type = I_type(p) & 0x7F; r = I_rect(p);
    if (r.left > 8000) return;                                          /* hidden item */
    qd_setport(d); set_dlg_font(d);
    switch (type){
    case 4: case 5: case 6: case 7: if (I_handle(p)) ctl_draw(I_handle(p)); break;
    case 8: {   unsigned h = I_handle(p); unsigned char buf[1024]; int n;
                if (!h || !MA(rl(h))) break;
                n = expand(MA(rl(h)), (int)mm_handlesize(h), buf, sizeof buf);
                qd_fill_rect(d, r, PAT_WHITE, 8);
                text_box(d, buf, n, r); } break;
    case 16: draw_edit(d, k, sw(d + D_editFld) == k - 1); break;
    case 32: {  unsigned h = I_handle(p); if (h && MA(rl(h))) qd_plot_icon(d, r, MA(rl(h))); } break;
    case 64: {  unsigned h = I_handle(p); if (h) qd_draw_picture(d, h, r); } break;
    case 0: {   unsigned proc = I_handle(p); if (proc) call68k(proc, 0, "LW", d, (unsigned)k); } break;
    }
    qd_setport(save);
}
void dlg_draw(unsigned d){
    int k, n = item_count(d), save = (int)qd_port();
    qd_setport(d);
    for (k = 1; k <= n; k++){
        unsigned p = item_rec(d, k); int type = p ? (I_type(p) & 0x7F) : -1;
        if (type >= 4 && type <= 7) continue;
        draw_item(d, k);
    }
    ctl_draw_all(d);
    qd_setport((unsigned)save);
}

/* ---- creation -------------------------------------------------------------------- */
static unsigned make_dialog(unsigned storage, Rect r, const unsigned char *title, int vis, int proc, unsigned behind, int goAway, unsigned refCon, unsigned items){
    unsigned d = wm_new_window(storage, r, title, 0, proc, behind, goAway, refCon, 1), p; int n, k;
    if (!d) return 0;
    if (!storage){ /* wm_new_window allocated a DialogRecord */ }
    set_dlg_font(d);
    wl(d + D_items, items); ww(d + D_editFld, 0xFFFF); ww(d + D_aDef, 1); wl(d + D_textH, 0);
    n = item_count(d);
    for (k = 1; k <= n; k++){
        int type, len; unsigned data;
        p = item_rec(d, k); type = I_type(p) & 0x7F; len = (int)rb(p + 13); data = p + 14;
        if (type >= 4 && type <= 6){
            unsigned char t[256]; t[0] = (unsigned char)len; memcpy(t + 1, g_ram + data, (size_t)len);
            wl(p, ctl_new(d, I_rect(p), t, 1, 0, 0, 1, type - 4, 0));
            if (I_type(p) & 0x80) wb(MA(rl(I_handle(p))) + 17, 255);
        } else if (type == 7){
            unsigned h = rs_get(0x434E544Cu, sw(data));
            if (h){ unsigned cd = MA(rl(h)); unsigned char t[256]; memcpy(t, g_ram + cd + 22, (size_t)rb(cd + 22) + 1);
                    wl(p, ctl_new(d, I_rect(p), t, rb(cd + 10), sw(cd + 8), sw(cd + 14), sw(cd + 12), sw(cd + 16), rl(cd + 18))); }
        } else if (type == 8 || type == 16){
            unsigned h = mm_newhandle((unsigned)len, 0);
            memcpy(g_ram + MA(rl(h)), g_ram + data, (size_t)len); wl(p, h);
            if (type == 16 && sw(d + D_editFld) == -1){ ww(d + D_editFld, (unsigned)(k - 1)); g_sel_start = 0; g_sel_end = len; }
        } else if (type == 32){ wl(p, rs_get(0x49434F4Eu, sw(data)));
        } else if (type == 64){ wl(p, rs_get(0x50494354u, sw(data)));
        } else wl(p, 0);
    }
    if (vis) wm_show(d, 1);
    return d;
}
static unsigned copy_ditl(int id){
    unsigned h = rs_get(0x4449544Cu, id), n;
    if (!h) return 0;
    n = mm_handlesize(h);
    return mm_newhandle_from(g_ram + MA(rl(h)), n);
}
static unsigned get_new_dialog(int id, unsigned storage, unsigned behind){
    unsigned h = rs_get(0x444C4F47u, id), d, items; unsigned char title[256];
    if (!h) return 0;
    d = MA(rl(h));
    items = copy_ditl(sw(d + 18));
    memcpy(title, g_ram + d + 20, (size_t)rb(d + 20) + 1);
    return make_dialog(storage, rect_at(d), title, rb(d + 10), sw(d + 8), behind, rb(d + 12), rl(d + 14), items);
}
static void dispose_dialog(unsigned d, int free_storage){
    int k, n = item_count(d);
    for (k = 1; k <= n; k++){ unsigned p = item_rec(d, k); int type = I_type(p) & 0x7F;
        if ((type == 8 || type == 16) && I_handle(p)) mm_disposhandle(I_handle(p)); }
    mm_disposhandle(rl(d + D_items));
    wm_dispose(d, free_storage);
}

/* ---- interaction ------------------------------------------------------------------- */
static int find_item(unsigned d, int h, int v){
    int k, n = item_count(d);
    for (k = 1; k <= n; k++){ unsigned p = item_rec(d, k); Rect r = I_rect(p);
        if ((I_type(p) & 0x7F) == 16) r = mkrect(r.top - 3, r.left - 3, r.bottom + 3, r.right + 3);
        if (pt_in(r, h, v)) return k; }
    return 0;
}
static void edit_key(unsigned d, int ch){
    int k = sw(d + D_editFld) + 1; unsigned p = item_rec(d, k), h; unsigned char buf[512]; int n;
    if (!p) return;
    h = I_handle(p); n = (int)mm_handlesize(h); if (n > 500) n = 500;
    memcpy(buf, g_ram + MA(rl(h)), (size_t)n);
    if (g_sel_start > n) g_sel_start = n; if (g_sel_end > n) g_sel_end = n;
    if (ch == 8){
        if (g_sel_end > g_sel_start){ memmove(buf + g_sel_start, buf + g_sel_end, (size_t)(n - g_sel_end)); n -= g_sel_end - g_sel_start; }
        else if (g_sel_start > 0){ memmove(buf + g_sel_start - 1, buf + g_sel_start, (size_t)(n - g_sel_start)); n--; g_sel_start--; }
        g_sel_end = g_sel_start;
    } else if (ch == 0x1C){ if (g_sel_start > 0) g_sel_start--; g_sel_end = g_sel_start; }
    else if (ch == 0x1D){ if (g_sel_end < n) g_sel_end++; g_sel_start = g_sel_end; }
    else if (ch >= 32 && n < 255){
        if (g_sel_end > g_sel_start){ memmove(buf + g_sel_start, buf + g_sel_end, (size_t)(n - g_sel_end)); n -= g_sel_end - g_sel_start; }
        memmove(buf + g_sel_start + 1, buf + g_sel_start, (size_t)(n - g_sel_start)); buf[g_sel_start++] = (unsigned char)ch; n++;
        g_sel_end = g_sel_start;
    }
    mm_sethandlesize(h, (unsigned)n); memcpy(g_ram + MA(rl(h)), buf, (size_t)n);
    g_caret_on = 1; draw_item(d, k);
}
static void next_edit(unsigned d){
    int n = item_count(d), cur = sw(d + D_editFld) + 1, k, old = cur;
    for (k = 1; k <= n; k++){ int c = (cur + k - 1) % n + 1; unsigned p = item_rec(d, c);
        if ((I_type(p) & 0x7F) == 16){ ww(d + D_editFld, (unsigned)(c - 1)); g_sel_start = 0; g_sel_end = (int)mm_handlesize(I_handle(p));
            if (old >= 1) draw_item(d, old); draw_item(d, c); return; } }
}
static void flash_button(unsigned d, int k){
    unsigned p = item_rec(d, k), c; int i;
    if (!p || (I_type(p) & 0x7F) < 4 || (I_type(p) & 0x7F) > 7) return;
    c = I_handle(p);
    for (i = 0; i < 2; i++){ wb(MA(rl(c)) + 17, 10); ctl_draw(c); hle_idle(60); wb(MA(rl(c)) + 17, 0); ctl_draw(c); hle_idle(30); }
}
static void caret_idle(unsigned d){
    int k = sw(d + D_editFld) + 1;
    if (k >= 1 && rl(LM_Ticks) - g_caret_tick >= 30){ g_caret_tick = rl(LM_Ticks); g_caret_on = !g_caret_on; draw_item(d, k); }
}

/* handle one event for dialog d; returns the item hit or 0 */
static int dialog_event(unsigned d, unsigned ev){
    int what = sw(ev), k;
    if (what == 6 && rl(ev + 2) == d){                                    /* update */
        unsigned save = qd_port();
        qd_setport(d);
        wm_begin_update(d); dlg_draw(d); wm_end_update(d);
        qd_setport(save);
        return 0;
    }
    if (what == 1){
        int gh, gv, h, v; wm_global_origin(d, &gh, &gv);
        h = sw(ev + 12) - gh; v = sw(ev + 10) - gv;
        k = find_item(d, h, v);
        if (!k) return 0;
        {   unsigned p = item_rec(d, k); int type = I_type(p) & 0x7F, dis = I_type(p) & 0x80;
            if (type >= 4 && type <= 7){
                if (ctl_track(I_handle(p), h, v, 0) == 0) return 0;
            } else if (type == 16){
                int old = sw(d + D_editFld) + 1, n = (int)mm_handlesize(I_handle(p));
                ww(d + D_editFld, (unsigned)(k - 1)); g_sel_start = g_sel_end = n;
                if (old >= 1 && old != k) draw_item(d, old);
                draw_item(d, k);
            }
            return dis ? 0 : k;
        }
    }
    if (what == 3 || what == 5){
        int ch = (int)(rl(ev + 2) & 0xFF);
        if (sw(ev + 14) & 0x100) return 0;                                /* command keys belong to menus */
        if (ch == 13 || ch == 3){ int def = sw(d + D_aDef); unsigned p = item_rec(d, def);
            if (p && (I_type(p) & 0x7F) >= 4 && (I_type(p) & 0x7F) <= 7 && rb(MA(rl(I_handle(p))) + 17) != 255){ flash_button(d, def); return def; }
            return 0; }
        if (ch == 9){ next_edit(d); return 0; }
        if (sw(d + D_editFld) >= 0){ k = sw(d + D_editFld) + 1; edit_key(d, ch); { unsigned p = item_rec(d, k); return (I_type(p) & 0x80) ? 0 : k; } }
    }
    return 0;
}

static unsigned g_evbuf;
static int modal_loop(unsigned d, unsigned filter, unsigned itemHitPtr){
    int hit = 0;
    if (!g_evbuf) g_evbuf = mm_newptr(16, 1);
    for (;;){
        int got;
        if (g_quit) return 1;
        if (wm_has_update(d)){                      /* only the dialog is updated while it is up */
            unsigned save = qd_port(); qd_setport(d);
            wm_begin_update(d); dlg_draw(d); wm_end_update(d);
            qd_setport(save);
        }
        got = evt_get(0xFFFF & ~((1 << 6) | (1 << 7)), g_evbuf, 1);
        if (!got){ caret_idle(d); }
        if (filter){
            unsigned ih = itemHitPtr;
            ww(ih, 0);
            if ((call68k(filter, 2, "LLL", d, g_evbuf, ih) >> 8) & 1){ hit = sw(ih); if (hit) return hit; }
        }
        if (got){
            int what = sw(g_evbuf);
            if (what == 1){
                int part; unsigned w = wm_find(sw(g_evbuf + 12), sw(g_evbuf + 10), &part);
                if (w != d){ host_beep(); continue; }
            }
            hit = dialog_event(d, g_evbuf);
            if (hit) return hit;
        } else hle_idle(10);
    }
}

static int run_alert(int id, unsigned filter, int icon){
    unsigned ah = rs_get(0x414C5254u, id), a, items, d; int hit, stage; unsigned ih;
    if (!ah){ host_beep(); return -1; }
    a = MA(rl(ah)); items = copy_ditl(sw(a + 8));
    if (!items) return -1;
    /* stage nibble: bit 3 = item 2 is the default, bit 2 = box drawn, bits 0-1 = beeps */
    stage = 0;
    {   int bits = (int)(rw(a + 10) & 0xF), beeps = bits & 3;
        while (beeps--) host_beep();
        if (!(bits & 4)) return -1; }
    {   unsigned char empty[1] = {0};
        d = make_dialog(0, rect_at(a), empty, 0, 1, 0xFFFFFFFFu, 0, 0, items); }
    if (icon >= 0){                                                         /* prepend the alert icon as a picture of ICON n */
        unsigned h = rs_get(0x49434F4Eu, icon);
        if (h){ unsigned save = qd_port(); wm_show(d, 1); qd_setport(d); qd_plot_icon(d, mkrect(10, 20, 42, 52), MA(rl(h))); qd_setport(save); }
    }
    ww(d + D_aDef, (rw(a + 10) & 8) ? 2 : 1); (void)stage;
    wm_show(d, 1);
    {   unsigned save = qd_port(); qd_setport(d); dlg_draw(d);
        if (icon >= 0){ unsigned h = rs_get(0x49434F4Eu, icon); if (h) qd_plot_icon(d, mkrect(10, 20, 42, 52), MA(rl(h))); }
        qd_setport(save); }
    /* the dialog is fully drawn: drop its pending update */
    wm_validate_all(d);
    ih = mm_newptr(2, 1);
    hit = modal_loop(d, filter, ih);
    mm_disposptr(ih);
    dispose_dialog(d, 1);
    return hit;
}

void dlg_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv;
    t->handled = 1;
    switch (word){
    case 0xA97B: POP(4); break;                                                   /* InitDialogs(resumeProc) */
    case 0xA97C: RETL(10, get_new_dialog(ARGW(8), ARGL(4), ARGL(0))); break;      /* GetNewDialog(id, storage, behind) */
    case 0xA97D: {                                                                /* NewDialog */
        unsigned char title[256]; unsigned tp = ARGL(22); memcpy(title, g_ram + MA(tp), (size_t)rb(tp) + 1);
        RETL(34, make_dialog(ARGL(30), rect_at(ARGL(26)), title, ARGB(20), ARGW(18), ARGL(14), ARGB(12), ARGL(8), ARGL(4) ? ARGL(4) : ARGL(0))); } break;
    case 0xA982: dispose_dialog(ARGL(0), 0); POP(4); break;                       /* CloseDialog */
    case 0xA983: dispose_dialog(ARGL(0), 1); POP(4); break;                       /* DisposDialog */
    case 0xA981: dlg_draw(ARGL(0)); POP(4); break;                                /* DrawDialog */
    case 0xA978: dlg_draw(ARGL(4)); POP(8); break;                                /* UpdtDialog */
    case 0xA979: case 0xA97A: POP(2); break;                                      /* CouldDialog FreeDialog */
    case 0xA989: case 0xA98A: POP(2); break;                                      /* CouldAlert FreeAlert */
    case 0xA98B: {                                                                /* ParamText */
        int i; for (i = 0; i < 4; i++){ unsigned s = ARGL(12 - 4*i);
            if (!g_param[i]) g_param[i] = mm_newhandle(0, 0);
            if (s){ mm_sethandlesize(g_param[i], rb(s) + 1u); memcpy(g_ram + MA(rl(g_param[i])), g_ram + MA(s), (size_t)rb(s) + 1); }
            else mm_sethandlesize(g_param[i], 1), wb(MA(rl(g_param[i])), 0);
            wl(LM_DAStrings + (unsigned)i*4, g_param[i]); }
        POP(16); } break;
    case 0xA985: RETW(6, run_alert(ARGW(4), ARGL(0), -1)); break;                 /* Alert */
    case 0xA986: RETW(6, run_alert(ARGW(4), ARGL(0), 0)); break;                  /* StopAlert */
    case 0xA987: RETW(6, run_alert(ARGW(4), ARGL(0), 1)); break;                  /* NoteAlert */
    case 0xA988: RETW(6, run_alert(ARGW(4), ARGL(0), 2)); break;                  /* CautionAlert */
    case 0xA991: {                                                                /* ModalDialog(filter, VAR itemHit) */
        unsigned d = wm_front(), ih = ARGL(0);
        if (!dlg_is_dialog(d)){ ww(ih, 0); POP(8); break; }
        ww(ih, (unsigned)modal_loop(d, ARGL(4), ih));
        POP(8); } break;
    case 0xA97F: {                                                                /* IsDialogEvent(event) */
        unsigned ev = ARGL(0), w = 0; int what = sw(ev);
        if (what == 6 || what == 8) w = rl(ev + 2);
        else if (what == 1){ int part; w = wm_find(sw(ev + 12), sw(ev + 10), &part); if (part != 3) w = 0; }
        else w = wm_front();
        RETB(4, dlg_is_dialog(w) && (what != 0 || sw(w + D_editFld) >= 0)); } break;
    case 0xA980: {                                                                /* DialogSelect(event, VAR dlg, VAR item) */
        unsigned ev = ARGL(8), w = 0; int what = sw(ev), hit = 0;
        if (what == 6 || what == 8) w = rl(ev + 2);
        else if (what == 1){ int part; w = wm_find(sw(ev + 12), sw(ev + 10), &part); }
        else w = wm_front();
        if (dlg_is_dialog(w)){
            if (what == 0) caret_idle(w);
            else if (what != 8) hit = dialog_event(w, ev);
            wl(ARGL(4), w); ww(ARGL(0), (unsigned)hit);
        }
        RETB(12, hit != 0); } break;
    case 0xA98D: {                                                                /* GetDItem(d, item, VAR type, VAR h, VAR box) */
        unsigned p = item_rec(ARGL(14), ARGW(12));
        if (p){ ww(ARGL(8), rb(p + 12)); wl(ARGL(4), rl(p)); rect_put(ARGL(0), I_rect(p)); }
        POP(16); } break;
    case 0xA98E: {                                                                /* SetDItem(d, item, type, h, box) */
        unsigned p = item_rec(ARGL(12), ARGW(10));
        if (p){ wb(p + 12, ARGUW(8) & 0xFF); wl(p, ARGL(4)); rect_put(p + 4, rect_at(ARGL(0))); }
        POP(16); } break;
    case 0xA98F: {                                                                /* SetIText(h, text) */
        unsigned h = ARGL(4), s = ARGL(0), w;
        if (mm_is_handle(h)){
            mm_sethandlesize(h, rb(s)); memcpy(g_ram + MA(rl(h)), g_ram + MA(s) + 1, (size_t)rb(s));
            for (w = rl(LM_WindowList); w; w = rl(w + 144)) if (dlg_is_dialog(w)){
                int k, n = item_count(w); for (k = 1; k <= n; k++){ unsigned p = item_rec(w, k);
                    if (rl(p) == h && rb(w + W_visible)){ if (sw(w + D_editFld) == k - 1){ g_sel_start = g_sel_end = (int)rb(s); } draw_item(w, k); } } }
        }
        POP(8); } break;
    case 0xA990: { unsigned h = ARGL(4), d = ARGL(0); unsigned n = mm_is_handle(h) ? mm_handlesize(h) : 0; if (n > 255) n = 255;   /* GetIText */
                   wb(d, n); if (n) memcpy(g_ram + MA(d) + 1, g_ram + MA(rl(h)), n); POP(8); } break;
    case 0xA97E: {                                                                /* SelIText(d, item, start, end) */
        unsigned d = ARGL(8); int k = ARGW(6), old = sw(d + D_editFld) + 1; unsigned p = item_rec(d, k);
        if (p && (I_type(p) & 0x7F) == 16){
            int n = (int)mm_handlesize(I_handle(p));
            ww(d + D_editFld, (unsigned)(k - 1));
            g_sel_start = ARGW(2); g_sel_end = ARGW(0);
            if (g_sel_start > n) g_sel_start = n; if (g_sel_end > n) g_sel_end = n; if (g_sel_start < 0) g_sel_start = 0;
            if (old >= 1 && old != k) draw_item(d, old);
            draw_item(d, k);
        }
        POP(10); } break;
    case 0xA984: { int k = find_item(ARGL(4), ARGW(2), ARGW(0)); RETW(8, k - 1); } break;   /* FindDItem(d, pt) */
    case 0xA827: { unsigned p = item_rec(ARGL(2), ARGW(0)); if (p){ Rect r = I_rect(p); if (r.left < 8192){ wm_invalidate(ARGL(2), r); rect_put(p + 4, rect_off(r, 16384, 0)); } } POP(6); } break;   /* HideDItem */
    case 0xA828: { unsigned p = item_rec(ARGL(2), ARGW(0)); if (p){ Rect r = I_rect(p); if (r.left >= 8192){ rect_put(p + 4, rect_off(r, -16384, 0)); draw_item(ARGL(2), ARGW(0)); } } POP(6); } break;   /* ShowDItem */
    case 0xA98C: POP(4); break;                                                   /* ErrorSound */
    case 0xA9CC: POP(0); break;                                                   /* TEInit */
    case 0xA9DA: case 0xA9D8: case 0xA9D9: POP(4); break;                         /* TEIdle TEActivate TEDeactivate */
    default: t->handled = 0;
    }
}
