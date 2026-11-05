/* QuickDraw (1-bit): GrafPorts, pens, patterns, shapes, text, CopyBits, regions
 * traps and PICT version 1 playback.
 *
 * All drawing goes through one pixel path (dc_*): local coordinates are mapped into
 * the port's BitMap through portBits.bounds and clipped to bounds, visRgn and
 * clipRgn, exactly the QuickDraw model. Window ports share the screen bitmap, so
 * a window's visRgn (kept by the Window Manager) is what stops it drawing over
 * other windows.
 */
#include "mac.h"
#include "plat.h"
#include <math.h>

const unsigned char PAT_BLACK[8]  = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
const unsigned char PAT_WHITE[8]  = {0,0,0,0,0,0,0,0};
const unsigned char PAT_GRAY[8]   = {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55};
const unsigned char PAT_LTGRAY[8] = {0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22};
const unsigned char PAT_DKGRAY[8] = {0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD};

int g_scr_w = 512, g_scr_h = 342, g_scr_rb = 64;
static unsigned g_qdg;                   /* address of thePort in the QD globals */
int g_cursor_level = 0;

unsigned qd_globals(void){ return g_qdg; }
unsigned qd_port(void){ return g_qdg ? rl(g_qdg) : 0; }
void qd_setport(unsigned p){ if (g_qdg) wl(g_qdg, p); }

/* ---- drawing context ------------------------------------------------------------- */
typedef struct { unsigned base; int rb; Rect bounds, clip; RgnView vis, clp, mask; int fast, screen, hasmask; int fgbit, bkbit; } DC;

static void dc_bits(DC *d, unsigned bm){
    d->base = MA(rl(bm)); d->rb = rw(bm+4) & 0x3FFF; d->bounds = rect_at(bm+6);
    d->clip = d->bounds; d->fast = 1; d->hasmask = 0;
    d->screen = d->base >= SCREEN_BASE && d->base < SCREEN_BASE + 0x40000;
    d->fgbit = 1; d->bkbit = 0;
}
static int color_bit(unsigned c){ return c != 30; }      /* whiteColor -> 0, any other old-style colour -> black */
static void dc_port(DC *d, unsigned port){
    dc_bits(d, port + GP_portBits);
    d->vis = rgn_view(rl(port + GP_visRgn)); d->clp = rgn_view(rl(port + GP_clipRgn));
    d->clip = rect_sect(rect_sect(d->bounds, d->vis.bbox), d->clp.bbox);
    d->fast = d->vis.rect && d->clp.rect;
    d->fgbit = color_bit(rl(port + GP_fgColor)); d->bkbit = color_bit(rl(port + GP_bkColor));
}
static __inline int dc_ok(const DC *d, int x, int y){
    if (!pt_in(d->clip, x, y)) return 0;
    if (!d->fast && !(rv_in(&d->vis, x, y) && rv_in(&d->clp, x, y))) return 0;
    if (d->hasmask && !rv_in(&d->mask, x, y)) return 0;
    return 1;
}
static __inline unsigned dc_addr(const DC *d, int x, int y, int *bit){
    int dx = x - d->bounds.left; *bit = 7 - (dx & 7);
    return d->base + (unsigned)(y - d->bounds.top) * (unsigned)d->rb + (unsigned)(dx >> 3);
}
static __inline int dc_get(const DC *d, int x, int y){ int b; unsigned a = dc_addr(d, x, y, &b); return (g_ram[MA(a)] >> b) & 1; }
/* apply transfer mode (0..7 source modes; pattern modes map onto them) */
static __inline void dc_apply(const DC *d, int x, int y, int s, int mode){
    int b; unsigned a; unsigned char v, m;
    if (!dc_ok(d, x, y)) return;
    a = MA(dc_addr(d, x, y, &b)); v = g_ram[a]; m = (unsigned char)(1 << b);
    mode &= 7;
    if (mode >= 4){ s = !s; mode -= 4; }
    switch (mode){
    case 0: { int c = s ? d->fgbit : d->bkbit; v = c ? (unsigned char)(v | m) : (unsigned char)(v & ~m); } break;
    case 1: if (s) v |= m; break;                         /* or  */
    case 2: if (s) v ^= m; break;                         /* xor */
    case 3: if (s) v &= (unsigned char)~m; break;         /* bic */
    }
    if (v != g_ram[a]){ g_ram[a] = v; if (d->screen) hle_dirty(); }
}
static __inline int pat_bit(const DC *d, const unsigned char *pat, int x, int y){
    int px = (x - d->bounds.left) & 7, py = (y - d->bounds.top) & 7;
    return (pat[py] >> (7 - px)) & 1;
}
static void dc_fill(const DC *d, Rect r, const unsigned char *pat, int mode){
    int x, y; r = rect_sect(r, d->clip);
    for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++) dc_apply(d, x, y, pat_bit(d, pat, x, y), mode);
}
static void pat_of(unsigned a, unsigned char p[8]){ int i; for (i = 0; i < 8; i++) p[i] = (unsigned char)rb(a + (unsigned)i); }

/* ---- shapes ---------------------------------------------------------------------- */
void qd_fill_rect(unsigned port, Rect r, const unsigned char pat[8], int mode){ DC d; dc_port(&d, port); dc_fill(&d, r, pat, mode); }

static int pen_visible(unsigned port){ return sw(port + GP_pnVis) >= 0; }
static void pen_pat(unsigned port, unsigned char p[8]){ pat_of(port + GP_pnPat, p); }

void qd_frame_rect(unsigned port, Rect r){
    DC d; unsigned char p[8]; int pw = sw(port + GP_pnSize + 2), ph = sw(port + GP_pnSize), mode = sw(port + GP_pnMode);
    if (rgn_recording()){ rgn_record_rect(r); return; }
    if (!pen_visible(port) || rect_empty(r)) return;
    dc_port(&d, port); pen_pat(port, p);
    if (pw*2 >= r.right - r.left || ph*2 >= r.bottom - r.top){ dc_fill(&d, r, p, mode); return; }
    dc_fill(&d, mkrect(r.top, r.left, r.top + ph, r.right), p, mode);
    dc_fill(&d, mkrect(r.bottom - ph, r.left, r.bottom, r.right), p, mode);
    dc_fill(&d, mkrect(r.top + ph, r.left, r.bottom - ph, r.left + pw), p, mode);
    dc_fill(&d, mkrect(r.top + ph, r.right - pw, r.bottom - ph, r.right), p, mode);
}

void qd_line(unsigned port, int h0, int v0, int h1, int v1){
    DC d; unsigned char p[8]; int pw = sw(port + GP_pnSize + 2), ph = sw(port + GP_pnSize), mode = sw(port + GP_pnMode);
    int dx = abs(h1 - h0), dy = abs(v1 - v0), sx = h0 < h1 ? 1 : -1, sy = v0 < v1 ? 1 : -1, err = dx - dy, x = h0, y = v0;
    if (rgn_recording()){ rgn_record_line(h0, v0, h1, v1); return; }
    if (!pen_visible(port) || pw <= 0 || ph <= 0) return;
    dc_port(&d, port); pen_pat(port, p);
    if (pw == 1 && ph == 1){
        for (;;){ dc_apply(&d, x, y, pat_bit(&d, p, x, y), mode);
            if (x == h1 && y == v1) break;
            { int e2 = 2*err; if (e2 > -dy){ err -= dy; x += sx; } if (e2 < dx){ err += dx; y += sy; } } }
        return;
    }
    {   /* thick pen: union of pen rectangles along the path, each pixel once */
        int t = (v0 < v1 ? v0 : v1), l = (h0 < h1 ? h0 : h1), b = (v0 > v1 ? v0 : v1) + ph, r = (h0 > h1 ? h0 : h1) + pw;
        int w = r - l, hgt = b - t, i, j; unsigned char *m = (unsigned char*)calloc((size_t)w * (size_t)hgt, 1);
        for (;;){
            for (j = 0; j < ph; j++) for (i = 0; i < pw; i++) m[(size_t)(y - t + j) * (size_t)w + (size_t)(x - l + i)] = 1;
            if (x == h1 && y == v1) break;
            { int e2 = 2*err; if (e2 > -dy){ err -= dy; x += sx; } if (e2 < dx){ err += dx; y += sy; } }
        }
        for (j = 0; j < hgt; j++) for (i = 0; i < w; i++) if (m[(size_t)j*(size_t)w + (size_t)i]) dc_apply(&d, l+i, t+j, pat_bit(&d, p, l+i, t+j), mode);
        free(m);
    }
}

/* filled/framed ovals, round rects and arcs share one inside test */
static int in_oval(Rect r, int x, int y){
    double cx = (r.left + r.right) / 2.0, cy = (r.top + r.bottom) / 2.0, a = (r.right - r.left) / 2.0, b = (r.bottom - r.top) / 2.0, dx, dy;
    if (a <= 0 || b <= 0) return 0;
    dx = (x + 0.5 - cx) / a; dy = (y + 0.5 - cy) / b;
    return dx*dx + dy*dy < 1.0;
}
static int in_round(Rect r, int ow, int oh, int x, int y){
    int w = r.right - r.left, h = r.bottom - r.top;
    if (!pt_in(r, x, y)) return 0;
    if (ow > w) ow = w; if (oh > h) oh = h;
    if (ow < 2 || oh < 2) return 1;
    if (x >= r.left + ow/2 && x < r.right - ow/2) return 1;
    if (y >= r.top + oh/2 && y < r.bottom - oh/2) return 1;
    {   Rect c; int cl = x < r.left + ow/2 ? r.left : r.right - ow, ct = y < r.top + oh/2 ? r.top : r.bottom - oh;
        c = mkrect(ct, cl, ct + oh, cl + ow); return in_oval(c, x, y); }
}
static int in_arc(Rect r, int start, int arc, int x, int y){
    double cx = (r.left + r.right) / 2.0, cy = (r.top + r.bottom) / 2.0, w = (r.right - r.left), h = (r.bottom - r.top), ang;
    if (!in_oval(r, x, y)) return 0;
    if (arc >= 360 || arc <= -360) return 1;
    ang = atan2((x + 0.5 - cx) / w, -(y + 0.5 - cy) / h) * 180.0 / 3.14159265358979;   /* clockwise from 12 o'clock */
    if (arc < 0){ start += arc; arc = -arc; }
    ang -= start; while (ang < 0) ang += 360; while (ang >= 360) ang -= 360;
    return ang < arc;
}
typedef int (*InFn)(void *ctx, int x, int y);
typedef struct { Rect r; int a, b, kind; } ShapeCtx;
static int shape_in(ShapeCtx *s, int x, int y){
    if (s->kind == 0) return in_oval(s->r, x, y);
    if (s->kind == 1) return in_round(s->r, s->a, s->b, x, y);
    return in_arc(s->r, s->a, s->b, x, y);
}
/* fill = 1 paint with pat; 0 = frame with the pen (inner border pen-size thick) */
static void shape_draw(unsigned port, ShapeCtx *s, int fill, const unsigned char *pat, int mode){
    DC d; unsigned char p[8]; int x, y, pw = sw(port + GP_pnSize + 2), ph = sw(port + GP_pnSize);
    Rect r = s->r;
    dc_port(&d, port);
    if (!fill){ if (!pen_visible(port)) return; pen_pat(port, p); pat = p; mode = sw(port + GP_pnMode); }
    r = rect_sect(r, d.clip);
    for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++){
        if (!shape_in(s, x, y)) continue;
        if (!fill && shape_in(s, x - pw, y) && shape_in(s, x + pw, y) && shape_in(s, x, y - ph) && shape_in(s, x, y + ph)
            && (s->kind != 2 || 1)) continue;
        dc_apply(&d, x, y, pat_bit(&d, pat, x, y), mode);
    }
}
void qd_oval(unsigned port, Rect r, int fill, const unsigned char *pat, int mode){
    ShapeCtx s; s.r = r; s.kind = 0;
    if (!fill && rgn_recording()){ rgn_record_oval(r); return; }
    shape_draw(port, &s, fill, pat, mode);
}
void qd_frame_round(unsigned port, Rect r, int ow, int oh, int fill, const unsigned char *pat, int mode){
    ShapeCtx s; s.r = r; s.a = ow; s.b = oh; s.kind = 1;
    if (!fill && rgn_recording()){ rgn_record_round(r, ow, oh); return; }
    shape_draw(port, &s, fill, pat, mode);
}
static void qd_arc(unsigned port, Rect r, int start, int arc, int fill, const unsigned char *pat, int mode){
    ShapeCtx s; s.r = r; s.a = start; s.b = arc; s.kind = 2;
    shape_draw(port, &s, fill, pat, mode);
}
static void rgn_draw(unsigned port, unsigned rgn, int fill, const unsigned char *pat, int mode){
    DC d; RgnView v = rgn_view(rgn); unsigned char p[8]; int x, y; Rect r;
    dc_port(&d, port);
    if (!fill){
        int pw = sw(port + GP_pnSize + 2), ph = sw(port + GP_pnSize);
        if (!pen_visible(port)) return; pen_pat(port, p); mode = sw(port + GP_pnMode);
        r = rect_sect(v.bbox, d.clip);
        for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++)
            if (rv_in(&v, x, y) && !(rv_in(&v, x-pw, y) && rv_in(&v, x+pw, y) && rv_in(&v, x, y-ph) && rv_in(&v, x, y+ph)))
                dc_apply(&d, x, y, pat_bit(&d, p, x, y), mode);
        return;
    }
    r = rect_sect(v.bbox, d.clip);
    for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++)
        if (rv_in(&v, x, y)) dc_apply(&d, x, y, pat_bit(&d, pat, x, y), mode);
}

/* ---- text --------------------------------------------------------------------------- */
typedef struct { DC *d; int mode; } TextCtx;
static void text_plot(int x, int y, void *c){ TextCtx *t = (TextCtx*)c; dc_apply(t->d, x, y, 1, t->mode); }
static int port_face(unsigned port){ return (int)rb(port + GP_txFace); }
int qd_text_width(unsigned port, const unsigned char *s, int n){
    int w = 0, i, font = sw(port + GP_txFont), size = sw(port + GP_txSize), face = port_face(port);
    for (i = 0; i < n; i++) w += font_char_width(font, size, face, s[i]);
    return w;
}
void qd_font_info(unsigned port, int *ascent, int *descent, int *widMax, int *leading){
    FontMetrics m; font_metrics(sw(port + GP_txFont), sw(port + GP_txSize), port_face(port), &m);
    *ascent = m.ascent; *descent = m.descent; *widMax = m.widMax; *leading = m.leading;
}
void qd_draw_text(unsigned port, const unsigned char *s, int n){
    DC d; TextCtx tc; int i, h = sw(port + GP_pnLoc + 2), v = sw(port + GP_pnLoc);
    int font = sw(port + GP_txFont), size = sw(port + GP_txSize), face = port_face(port), mode = sw(port + GP_txMode);
    dc_port(&d, port); tc.d = &d; tc.mode = mode & 7;
    if (getenv(HLE_ENV("TEXTLOG"))){ char s2[80]; int k; for (k = 0; k < n && k < 79; k++) s2[k] = (char)s[k]; s2[k] = 0;
        logf_("[text] port=%06X base=%06X bounds=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) vis=(%d,%d,%d,%d)%s at %d,%d font=%d/%d mode=%d '%s'\n", port, d.base,
              d.bounds.top, d.bounds.left, d.bounds.bottom, d.bounds.right, d.clip.top, d.clip.left, d.clip.bottom, d.clip.right,
              d.vis.bbox.top, d.vis.bbox.left, d.vis.bbox.bottom, d.vis.bbox.right, d.fast ? "" : " complex", h, v, font, size, mode, s2); }
    if ((mode & 7) == 0 || (mode & 7) == 4){         /* srcCopy: background of the text box is erased */
        FontMetrics m; int w = qd_text_width(port, s, n);
        font_metrics(font, size, face, &m);
        dc_fill(&d, mkrect(v - m.ascent, h, v + m.descent, h + w), (mode & 7) == 4 ? PAT_BLACK : PAT_WHITE, 8);
        tc.mode = (mode & 7) == 4 ? 3 : 1;
    }
    if (sw(port + GP_pnVis) < 0) n = 0;
    for (i = 0; i < n; i++) h += font_draw_char(font, size, face, s[i], h, v, text_plot, &tc);
    if (getenv(HLE_ENV("TEXTLOG"))){ int x, y, c = 0; for (y = d.clip.top; y < d.clip.bottom; y++) for (x = d.clip.left; x < d.clip.right; x++) c += dc_get(&d, x, y);
        logf_("[text]   -> %d black pixels in clip, pnVis=%d rb=%d\n", c, sw(port + GP_pnVis), d.rb); }
    if (sw(port + GP_pnVis) < 0) h += qd_text_width(port, s, 0);
    ww(port + GP_pnLoc + 2, (unsigned)h);
}

/* ---- CopyBits --------------------------------------------------------------------- */
void qd_copybits(unsigned srcBits, unsigned dstBits, Rect sr, Rect dr, int mode, unsigned maskRgn){
    DC s, d; unsigned port = qd_port(); int x, y, sw_ = sr.right - sr.left, sh = sr.bottom - sr.top, dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    unsigned char *tmp; Rect cr;
    if (sw_ <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    dc_bits(&s, srcBits);
    if (port && (dstBits == port + GP_portBits || (MA(rl(dstBits)) == MA(rl(port + GP_portBits)) && rect_at(dstBits+6).top == sw(port+GP_portBits+6)
                                                   && rect_at(dstBits+6).left == sw(port+GP_portBits+8)))){
        dc_port(&d, port);
        d.bounds = rect_at(dstBits + 6); d.base = MA(rl(dstBits)); d.rb = rw(dstBits + 4) & 0x3FFF;
        d.clip = rect_sect(rect_sect(d.bounds, d.vis.bbox), d.clp.bbox);
        d.screen = d.base >= SCREEN_BASE && d.base < SCREEN_BASE + 0x40000;
    } else dc_bits(&d, dstBits);
    if (port){ d.fgbit = color_bit(rl(port + GP_fgColor)); d.bkbit = color_bit(rl(port + GP_bkColor)); }
    if (getenv(HLE_ENV("CBLOG"))) logf_("[cb] port=%06X src=%06X(%d,%d,%d,%d) dst=%06X(%d,%d,%d,%d) sr=(%d,%d,%d,%d) dr=(%d,%d,%d,%d) clip=(%d,%d,%d,%d)%s mode=%d fg=%d bk=%d\n",
        port, s.base, s.bounds.top, s.bounds.left, s.bounds.bottom, s.bounds.right, d.base, d.bounds.top, d.bounds.left, d.bounds.bottom, d.bounds.right,
        sr.top, sr.left, sr.bottom, sr.right, dr.top, dr.left, dr.bottom, dr.right, d.clip.top, d.clip.left, d.clip.bottom, d.clip.right, d.fast ? "" : " complex", mode, d.fgbit, d.bkbit);
    if (maskRgn){ d.mask = rgn_view(maskRgn); d.hasmask = 1; }
    /* read the source first (src and dst may overlap) */
    tmp = (unsigned char*)malloc((size_t)sw_ * (size_t)sh);
    for (y = 0; y < sh; y++) for (x = 0; x < sw_; x++){
        int sx = sr.left + x, sy = sr.top + y;
        tmp[y*sw_ + x] = (unsigned char)(pt_in(s.bounds, sx, sy) ? dc_get(&s, sx, sy) : 0);
    }
    cr = rect_sect(dr, d.clip);
    for (y = cr.top; y < cr.bottom; y++) for (x = cr.left; x < cr.right; x++){
        int ix = (x - dr.left) * sw_ / dw, iy = (y - dr.top) * sh / dh;
        int m = mode & 0x3F;
        if (m >= 32 && m < 40) m = m == 36 ? 1 : 0;       /* blend/transparent etc. on 1-bit */
        dc_apply(&d, x, y, tmp[iy*sw_ + ix], m & 7);
    }
    free(tmp);
}

void qd_plot_icon(unsigned port, Rect r, unsigned data){
    unsigned char bm[14]; unsigned tmp = mm_newptr(14, 1); (void)bm;
    wl(tmp, data); ww(tmp+4, 4); rect_put(tmp+6, mkrect(0,0,32,32));
    qd_copybits(tmp, port + GP_portBits, mkrect(0,0,32,32), r, 0, 0);
    mm_disposptr(tmp);
}

/* ---- PICT version 1 playback --------------------------------------------------------- */
static int unpack_bits(const unsigned char **pp, const unsigned char *end, unsigned char *dst, int n){
    const unsigned char *p = *pp; int o = 0;
    while (o < n && p < end){
        int c = (signed char)*p++;
        if (c >= 0){ int k = c + 1; while (k-- && o < n && p < end) dst[o++] = *p++; }
        else if (c != -128){ int k = -c + 1; unsigned char v = p < end ? *p++ : 0; while (k-- && o < n) dst[o++] = v; }
    }
    *pp = p; return o;
}
void qd_draw_picture(unsigned port, unsigned pich, Rect dst){
    const unsigned char *b, *p, *end; unsigned len; Rect frame; int ph, pv, ovw = 0, ovh = 0, txh = 0, txv = 0;
    unsigned char save[GP_SIZE]; unsigned char lastRect[8]; Rect lr = mkrect(0,0,0,0), lrr = lr, lov = lr, larc = lr; int la0 = 0, la1 = 0;
    double sx, sy;
    if (!pich || !MA(rl(pich))) return;
    len = mm_handlesize(pich); b = g_ram + MA(rl(pich)); end = b + len; (void)lastRect;
    frame = mkrect((short)(b[2]<<8|b[3]), (short)(b[4]<<8|b[5]), (short)(b[6]<<8|b[7]), (short)(b[8]<<8|b[9]));
    if (rect_empty(frame)) return;
    sx = (double)(dst.right - dst.left) / (frame.right - frame.left);
    sy = (double)(dst.bottom - dst.top) / (frame.bottom - frame.top);
    memcpy(save, g_ram + MA(port), GP_SIZE);
    /* pictures start with a fresh pen and text state */
    ww(port + GP_pnSize, 1); ww(port + GP_pnSize + 2, 1); ww(port + GP_pnMode, 8); memcpy(g_ram + MA(port + GP_pnPat), PAT_BLACK, 8);
    ww(port + GP_txFont, 0); wb(port + GP_txFace, 0); ww(port + GP_txMode, 1); ww(port + GP_txSize, 0);
    memcpy(g_ram + MA(port + GP_fillPat), PAT_BLACK, 8);
#define MX(x) (dst.left + (int)floor(((x) - frame.left) * sx + 0.5))
#define MY(y) (dst.top + (int)floor(((y) - frame.top) * sy + 0.5))
#define RD16(q) ((short)((q)[0]<<8 | (q)[1]))
#define RRECT(q) mkrect(MY(RD16(q)), MX(RD16((q)+2)), MY(RD16((q)+4)), MX(RD16((q)+6)))
    ph = pv = 0;
    p = b + 10;
    while (p < end){
        int op = *p++;
        unsigned char fill[8];
        switch (op){
        case 0x00: break;
        case 0x11: p++; break;
        case 0xFF: p = end; break;
        case 0x01: { int sz = RD16(p); p += sz; } break;                                    /* clipRgn: ignored (dst clips) */
        case 0x02: memcpy(g_ram + MA(port + GP_bkPat), p, 8); p += 8; break;
        case 0x03: ww(port + GP_txFont, (unsigned)RD16(p)); p += 2; break;
        case 0x04: wb(port + GP_txFace, *p); p += 1; break;
        case 0x05: ww(port + GP_txMode, (unsigned)RD16(p)); p += 2; break;
        case 0x06: p += 4; break;
        case 0x07: ww(port + GP_pnSize, (unsigned)RD16(p)); ww(port + GP_pnSize + 2, (unsigned)RD16(p+2)); p += 4; break;
        case 0x08: ww(port + GP_pnMode, (unsigned)RD16(p)); p += 2; break;
        case 0x09: memcpy(g_ram + MA(port + GP_pnPat), p, 8); p += 8; break;
        case 0x0A: memcpy(g_ram + MA(port + GP_fillPat), p, 8); p += 8; break;
        case 0x0B: ovh = RD16(p); ovw = RD16(p+2); p += 4; break;
        case 0x0C: p += 4; break;
        case 0x0D: ww(port + GP_txSize, (unsigned)RD16(p)); p += 2; break;
        case 0x0E: wl(port + GP_fgColor, (unsigned)(RD16(p)<<16 | (p[2]<<8|p[3]))); p += 4; break;
        case 0x0F: wl(port + GP_bkColor, (unsigned)(RD16(p)<<16 | (p[2]<<8|p[3]))); p += 4; break;
        case 0x10: p += 8; break;
        case 0x20: pv = RD16(p); ph = RD16(p+2); { int nv = RD16(p+4), nh = RD16(p+6); qd_line(port, MX(ph), MY(pv), MX(nh), MY(nv)); ph = nh; pv = nv; } p += 8; break;
        case 0x21: { int nv = RD16(p), nh = RD16(p+2); qd_line(port, MX(ph), MY(pv), MX(nh), MY(nv)); ph = nh; pv = nv; } p += 4; break;
        case 0x22: pv = RD16(p); ph = RD16(p+2); { int nh = ph + (signed char)p[4], nv = pv + (signed char)p[5]; qd_line(port, MX(ph), MY(pv), MX(nh), MY(nv)); ph = nh; pv = nv; } p += 6; break;
        case 0x23: { int nh = ph + (signed char)p[0], nv = pv + (signed char)p[1]; qd_line(port, MX(ph), MY(pv), MX(nh), MY(nv)); ph = nh; pv = nv; } p += 2; break;
        case 0x28: case 0x29: case 0x2A: case 0x2B: {
            int n;
            if (op == 0x28){ txv = RD16(p); txh = RD16(p+2); p += 4; }
            else if (op == 0x29){ txh += *p++; }
            else if (op == 0x2A){ txv += *p++; }
            else { txh += *p++; txv += *p++; }
            n = *p++;
            ww(port + GP_pnLoc, (unsigned)MY(txv)); ww(port + GP_pnLoc + 2, (unsigned)MX(txh));
            qd_draw_text(port, p, n); p += n; } break;
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: {
            Rect r; int verb = op & 7;
            if (op < 0x38){ r = RRECT(p); p += 8; lr = r; } else r = lr;
            pat_of(port + GP_fillPat, fill);
            if (verb == 0) qd_frame_rect(port, r);
            else if (verb == 1){ unsigned char pp[8]; pen_pat(port, pp); qd_fill_rect(port, r, pp, sw(port + GP_pnMode)); }
            else if (verb == 2){ unsigned char bp[8]; pat_of(port + GP_bkPat, bp); qd_fill_rect(port, r, bp, 8); }
            else if (verb == 3) qd_fill_rect(port, r, PAT_BLACK, 10);
            else { if (op == 0x34){ memcpy(fill, p, 8); p += 8; } qd_fill_rect(port, r, fill, 8); }
            } break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: {
            Rect r; int verb = op & 7; unsigned char pp[8];
            if (op < 0x48){ r = RRECT(p); p += 8; lrr = r; } else r = lrr;
            if (verb == 0) qd_frame_round(port, r, (int)(ovw*sx), (int)(ovh*sy), 0, 0, 0);
            else if (verb == 1){ pen_pat(port, pp); qd_frame_round(port, r, (int)(ovw*sx), (int)(ovh*sy), 1, pp, sw(port + GP_pnMode)); }
            else if (verb == 2){ pat_of(port + GP_bkPat, pp); qd_frame_round(port, r, (int)(ovw*sx), (int)(ovh*sy), 1, pp, 8); }
            else if (verb == 3) qd_frame_round(port, r, (int)(ovw*sx), (int)(ovh*sy), 1, PAT_BLACK, 10);
            else { if (op == 0x44){ memcpy(pp, p, 8); p += 8; } else pat_of(port + GP_fillPat, pp); qd_frame_round(port, r, (int)(ovw*sx), (int)(ovh*sy), 1, pp, 8); }
            } break;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: {
            Rect r; int verb = op & 7; unsigned char pp[8];
            if (op < 0x58){ r = RRECT(p); p += 8; lov = r; } else r = lov;
            if (verb == 0) qd_oval(port, r, 0, 0, 0);
            else if (verb == 1){ pen_pat(port, pp); qd_oval(port, r, 1, pp, sw(port + GP_pnMode)); }
            else if (verb == 2){ pat_of(port + GP_bkPat, pp); qd_oval(port, r, 1, pp, 8); }
            else if (verb == 3) qd_oval(port, r, 1, PAT_BLACK, 10);
            else { if (op == 0x54){ memcpy(pp, p, 8); p += 8; } else pat_of(port + GP_fillPat, pp); qd_oval(port, r, 1, pp, 8); }
            } break;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64:
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: {
            Rect r; int verb = op & 7; unsigned char pp[8];
            if (op < 0x68){ r = RRECT(p); p += 8; larc = r; } else r = larc;
            la0 = RD16(p); la1 = RD16(p+2); p += 4;
            if (verb == 0){ pen_pat(port, pp); qd_arc(port, r, la0, la1, 0, pp, sw(port + GP_pnMode)); }
            else if (verb == 1){ pen_pat(port, pp); qd_arc(port, r, la0, la1, 1, pp, sw(port + GP_pnMode)); }
            else if (verb == 2){ pat_of(port + GP_bkPat, pp); qd_arc(port, r, la0, la1, 1, pp, 8); }
            else if (verb == 3) qd_arc(port, r, la0, la1, 1, PAT_BLACK, 10);
            else { if (op == 0x64){ memcpy(pp, p, 8); p += 8; } else pat_of(port + GP_fillPat, pp); qd_arc(port, r, la0, la1, 1, pp, 8); }
            } break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: {
            int sz = RD16(p), n = (sz - 10) / 4, i; const unsigned char *pt = p + 10;
            if (op == 0x70) for (i = 1; i < n; i++) qd_line(port, MX(RD16(pt+i*4-2)), MY(RD16(pt+i*4-4)), MX(RD16(pt+i*4+2)), MY(RD16(pt+i*4)));
            else {   /* filled polygon via a recorded region */
                unsigned rg = rgn_new(); unsigned char pp[8];
                rgn_record_begin();
                for (i = 1; i < n; i++) rgn_record_line(MX(RD16(pt+i*4-2)), MY(RD16(pt+i*4-4)), MX(RD16(pt+i*4+2)), MY(RD16(pt+i*4)));
                rgn_record_end(rg);
                if (op == 0x71){ pen_pat(port, pp); rgn_draw(port, rg, 1, pp, sw(port + GP_pnMode)); }
                else if (op == 0x72){ pat_of(port + GP_bkPat, pp); rgn_draw(port, rg, 1, pp, 8); }
                else if (op == 0x73) rgn_draw(port, rg, 1, PAT_BLACK, 10);
                else { pat_of(port + GP_fillPat, pp); rgn_draw(port, rg, 1, pp, 8); }
                mm_disposhandle(rg);
            }
            p += sz; } break;
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: { int sz = RD16(p); p += sz; } break;
        case 0x90: case 0x91: case 0x98: case 0x99: {
            int rbytes = RD16(p) & 0x3FFF, packed = op >= 0x98, hasrgn = op & 1, y, h, mode;
            Rect bnd = mkrect(RD16(p+2), RD16(p+4), RD16(p+6), RD16(p+8)), srt, drt;
            unsigned tmp, bm; const unsigned char *q;
            p += 10;
            srt = mkrect(RD16(p), RD16(p+2), RD16(p+4), RD16(p+6)); p += 8;
            drt = RRECT(p); p += 8;
            mode = RD16(p); p += 2;
            if (hasrgn){ int sz = RD16(p); p += sz; }
            h = bnd.bottom - bnd.top;
            tmp = mm_newptr((unsigned)(rbytes * h) + 1, 1); bm = mm_newptr(14, 1);
            q = p;
            for (y = 0; y < h; y++){
                if (!packed || rbytes < 8){ memcpy(g_ram + tmp + (unsigned)(y*rbytes), q, (size_t)rbytes); q += rbytes; }
                else { int bc = rbytes > 250 ? (q[0]<<8 | q[1]) : q[0]; const unsigned char *r0;
                       q += rbytes > 250 ? 2 : 1; r0 = q;
                       unpack_bits(&r0, q + bc, g_ram + tmp + (unsigned)(y*rbytes), rbytes); q += bc; }
            }
            p = q;
            wl(bm, tmp); ww(bm+4, (unsigned)rbytes); rect_put(bm+6, bnd);
            qd_copybits(bm, port + GP_portBits, srt, drt, mode, 0);
            mm_disposptr(tmp); mm_disposptr(bm);
            } break;
        case 0xA0: p += 2; break;
        case 0xA1: { int sz = RD16(p+2); p += 4 + sz; } break;
        default:
            logf_("[pict] unknown v1 opcode %02X\n", op); p = end;
        }
    }
    /* restore the caller's port state (pictures don't change it) */
    {   unsigned keepVis = rl(port + GP_visRgn), keepClip = rl(port + GP_clipRgn);
        memcpy(g_ram + MA(port), save, GP_SIZE); wl(port + GP_visRgn, keepVis); wl(port + GP_clipRgn, keepClip); }
#undef MX
#undef MY
#undef RD16
#undef RRECT
}

/* ---- ports -------------------------------------------------------------------------- */
void qd_init_port(unsigned port){
    unsigned g = g_qdg, vis = rl(port + GP_visRgn), clip = rl(port + GP_clipRgn);
    memset(g_ram + MA(port), 0, GP_SIZE);
    if (g) memcpy(g_ram + MA(port + GP_portBits), g_ram + MA(g - 122), 14);
    rect_put(port + GP_portRect, mkrect(0, 0, g_scr_h, g_scr_w));
    if (!mm_is_handle(vis)) vis = rgn_new();
    if (!mm_is_handle(clip)) clip = rgn_new();
    rgn_set_rect(vis, mkrect(0, 0, g_scr_h, g_scr_w));
    rgn_set_rect(clip, mkrect(-32767, -32767, 32767, 32767));
    wl(port + GP_visRgn, vis); wl(port + GP_clipRgn, clip);
    memcpy(g_ram + MA(port + GP_bkPat), PAT_WHITE, 8);
    memcpy(g_ram + MA(port + GP_fillPat), PAT_BLACK, 8);
    ww(port + GP_pnSize, 1); ww(port + GP_pnSize + 2, 1); ww(port + GP_pnMode, 8);
    memcpy(g_ram + MA(port + GP_pnPat), PAT_BLACK, 8);
    ww(port + GP_txMode, 1);
    wl(port + GP_fgColor, 33); wl(port + GP_bkColor, 30);
    qd_setport(port);
}

static void init_graf(unsigned g){
    unsigned sb = g - 122; int i;
    g_qdg = g;
    wl(app_a5(), g);
    wl(g, 0);
    wl(g - 126, 1);                                           /* randSeed */
    wl(sb, SCREEN_BASE); ww(sb + 4, (unsigned)g_scr_rb); rect_put(sb + 6, mkrect(0, 0, g_scr_h, g_scr_w));
    {   static const unsigned short arrow[34] = {          /* standard arrow cursor */
            0x0000,0x4000,0x6000,0x7000,0x7800,0x7C00,0x7E00,0x7F00,0x7F80,0x7C00,0x6C00,0x4600,0x0600,0x0300,0x0300,0x0000,
            0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,0xFF80,0xFFC0,0xFFE0,0xFE00,0xEF00,0xCF00,0x8780,0x0780,0x0380,
            0x0001,0x0001 };
        for (i = 0; i < 34; i++) ww(g - 108 + (unsigned)i*2, arrow[i]); }
    memcpy(g_ram + MA(g - 40), PAT_DKGRAY, 8); memcpy(g_ram + MA(g - 32), PAT_LTGRAY, 8);
    memcpy(g_ram + MA(g - 24), PAT_GRAY, 8);   memcpy(g_ram + MA(g - 16), PAT_BLACK, 8);
    memcpy(g_ram + MA(g - 8), PAT_WHITE, 8);
    wl(LM_ScrnBase, SCREEN_BASE); ww(LM_ScreenRow, (unsigned)g_scr_rb);
}

/* ---- the traps ------------------------------------------------------------------------ */
static Rect argrect(unsigned a){ return rect_at(a); }

void qd_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv; unsigned P = qd_port(); unsigned char pat[8];
    t->handled = 1;
    switch (word){
    case 0xA86E: init_graf(ARGL(0)); POP(4); break;                               /* InitGraf */
    case 0xA86F: wl(ARGL(0) + GP_visRgn, 0); wl(ARGL(0) + GP_clipRgn, 0);       /* OpenPort: always fresh regions */
                 qd_init_port(ARGL(0)); POP(4); break;
    case 0xA86D: qd_init_port(ARGL(0)); POP(4); break;                            /* InitPort */
    case 0xA87D: { unsigned p = ARGL(0); mm_disposhandle(rl(p + GP_visRgn)); mm_disposhandle(rl(p + GP_clipRgn)); POP(4); } break;  /* ClosePort */
    case 0xA873: qd_setport(ARGL(0)); POP(4); break;                              /* SetPort */
    case 0xA874: wl(ARGL(0), P); POP(4); break;                                   /* GetPort */
    case 0xA872: POP(2); break;                                                   /* GrafDevice */
    case 0xA875: memcpy(g_ram + MA(P + GP_portBits), g_ram + MA(ARGL(0)), 14); POP(4); break;   /* SetPortBits */
    case 0xA876: { Rect r = rect_at(P + GP_portRect); r.right = (short)(r.left + ARGW(2)); r.bottom = (short)(r.top + ARGW(0)); rect_put(P + GP_portRect, r); POP(4); } break;  /* PortSize */
    case 0xA877: { Rect pr = rect_at(P + GP_portRect), b = rect_at(P + GP_portBits + 6); int gl = ARGW(2), gt = ARGW(0);   /* MovePortTo */
                   int dh = (pr.left - gl) - b.left, dv = (pr.top - gt) - b.top; rect_put(P + GP_portBits + 6, rect_off(b, dh, dv)); POP(4); } break;
    case 0xA878: { Rect pr = rect_at(P + GP_portRect); int dh = ARGW(2) - pr.left, dv = ARGW(0) - pr.top;   /* SetOrigin */
                   rect_put(P + GP_portRect, rect_off(pr, dh, dv));
                   rect_put(P + GP_portBits + 6, rect_off(rect_at(P + GP_portBits + 6), dh, dv));
                   rgn_offset(rl(P + GP_visRgn), dh, dv); POP(4); } break;
    case 0xA879: rgn_copy(ARGL(0), rl(P + GP_clipRgn)); POP(4); break;            /* SetClip */
    case 0xA87A: rgn_copy(rl(P + GP_clipRgn), ARGL(0)); POP(4); break;            /* GetClip */
    case 0xA87B: rgn_set_rect(rl(P + GP_clipRgn), argrect(ARGL(0))); POP(4); break;   /* ClipRect */
    case 0xA87C: memcpy(g_ram + MA(P + GP_bkPat), g_ram + MA(ARGL(0)), 8); POP(4); break;   /* BackPat */
    case 0xA870: { unsigned pt = ARGL(0); Rect b = rect_at(P + GP_portBits + 6);  /* LocalToGlobal */
                   ww(pt, (unsigned)(sw(pt) - b.top)); ww(pt+2, (unsigned)(sw(pt+2) - b.left)); POP(4); } break;
    case 0xA871: { unsigned pt = ARGL(0); Rect b = rect_at(P + GP_portBits + 6);  /* GlobalToLocal */
                   ww(pt, (unsigned)(sw(pt) + b.top)); ww(pt+2, (unsigned)(sw(pt+2) + b.left)); POP(4); } break;
    case 0xA87E: { unsigned d = ARGL(0); ww(d, (unsigned)(sw(d) + ARGW(4))); ww(d+2, (unsigned)(sw(d+2) + ARGW(6))); POP(8); } break;   /* AddPt */
    case 0xA87F: { unsigned d = ARGL(0); ww(d, (unsigned)(sw(d) - ARGW(4))); ww(d+2, (unsigned)(sw(d+2) - ARGW(6))); POP(8); } break;   /* SubPt */
    case 0xA880: { unsigned d = ARGL(4); ww(d+2, (unsigned)ARGW(2)); ww(d, (unsigned)ARGW(0)); POP(8); } break;   /* SetPt(pt,h,v) */
    case 0xA881: RETB(8, ARGL(0) == ARGL(4)); break;                              /* EqualPt */
    case 0xA861: {                                                                /* Random */
        unsigned g = g_qdg - 126, s = rl(g); long long n = (long long)(s ? s : 1) * 16807; int r;
        s = (unsigned)(n % 0x7FFFFFFF); wl(g, s); r = (int)(s & 0xFFFF); if (r == 0x8000) r = 0;
        RETW(0, (unsigned)r); } break;
    case 0xA862: wl(P + GP_fgColor, ARGL(0)); POP(4); break;                      /* ForeColor */
    case 0xA863: wl(P + GP_bkColor, ARGL(0)); POP(4); break;                      /* BackColor */
    case 0xA864: POP(2); break;                                                   /* ColorBit */
    case 0xA865: { int h = ARGW(2), v = ARGW(0); DC d; dc_port(&d, P);           /* GetPixel */
                   RETB(4, pt_in(d.bounds, h, v) ? dc_get(&d, h, v) : 0); } break;
    case 0xA866: { unsigned dst = ARGL(4), s = ARGL(0); int n = (int)rb(s), i;    /* StuffHex */
                   for (i = 0; i + 1 < n; i += 2){ char hx[3] = { (char)rb(s+1+i), (char)rb(s+2+i), 0 }; wb(dst + (unsigned)(i/2), (unsigned)strtoul(hx, 0, 16)); }
                   POP(8); } break;
    /* text */
    case 0xA883: { unsigned char c = (unsigned char)ARGUW(0); qd_draw_text(P, &c, 1); POP(2); } break;   /* DrawChar */
    case 0xA884: { unsigned s = ARGL(0); qd_draw_text(P, g_ram + MA(s) + 1, (int)rb(s)); POP(4); } break; /* DrawString */
    case 0xA885: qd_draw_text(P, g_ram + MA(ARGL(4)) + ARGW(2), ARGW(0)); POP(8); break;                  /* DrawText */
    case 0xA886: RETW(8, qd_text_width(P, g_ram + MA(ARGL(4)) + ARGW(2), ARGW(0))); break;               /* TextWidth */
    case 0xA88C: { unsigned s = ARGL(0); RETW(4, qd_text_width(P, g_ram + MA(s) + 1, (int)rb(s))); } break;  /* StringWidth */
    case 0xA88D: { unsigned char c = (unsigned char)ARGUW(0); RETW(2, qd_text_width(P, &c, 1)); } break;   /* CharWidth */
    case 0xA887: ww(P + GP_txFont, ARGUW(0)); POP(2); break;                     /* TextFont */
    case 0xA888: wb(P + GP_txFace, ARGB(0)); POP(2); break;                      /* TextFace */
    case 0xA889: ww(P + GP_txMode, ARGUW(0)); POP(2); break;                     /* TextMode */
    case 0xA88A: ww(P + GP_txSize, ARGUW(0)); POP(2); break;                     /* TextSize */
    case 0xA88E: wl(P + GP_spExtra, ARGL(0)); POP(4); break;                     /* SpaceExtra */
    case 0xA88B: { int a, d, w, l; unsigned fi = ARGL(0); qd_font_info(P, &a, &d, &w, &l);   /* GetFontInfo */
                   ww(fi, (unsigned)a); ww(fi+2, (unsigned)d); ww(fi+4, (unsigned)w); ww(fi+6, (unsigned)l); POP(4); } break;
    case 0xA8FE: POP(0); break;                                                   /* InitFonts */
    case 0xA8FF: { static const char *nm[] = {"Chicago","Geneva","New York","Geneva","Monaco","Venice"}; int f = ARGW(4);   /* GetFontName */
                   pstr_put(ARGL(0), f >= 0 && f < 6 ? nm[f] : (f == 20 ? "Times" : f == 21 ? "Helvetica" : f == 22 ? "Courier" : "")); POP(6); } break;
    case 0xA900: { char n[64]; int f = 0; pstr_get(ARGL(4), n, sizeof n);       /* GetFNum */
                   if (!plat_stricmp(n,"Chicago")) f = 0; else if (!plat_stricmp(n,"Geneva")) f = 3; else if (!plat_stricmp(n,"New York")) f = 2;
                   else if (!plat_stricmp(n,"Monaco")) f = 4; else if (!plat_stricmp(n,"Venice")) f = 5; else if (!plat_stricmp(n,"Times")) f = 20;
                   else if (!plat_stricmp(n,"Helvetica")) f = 21; else if (!plat_stricmp(n,"Courier")) f = 22;
                   ww(ARGL(0), (unsigned)f); POP(8); } break;
    case 0xA902: { unsigned len; RETB(4, rs_sysdata(0x464F4E54u, ARGW(2) * 128 + ARGW(0), &len) != 0); } break;   /* RealFont */
    case 0xA903: POP(2); break;                                                   /* SetFontLock */
    case 0xA901: RETL(4, 0); break;                                               /* FMSwapFont */
    /* pen */
    case 0xA891: { int h = ARGW(2), v = ARGW(0); qd_line(P, sw(P + GP_pnLoc + 2), sw(P + GP_pnLoc), h, v);   /* LineTo */
                   ww(P + GP_pnLoc, (unsigned)v); ww(P + GP_pnLoc + 2, (unsigned)h); POP(4); } break;
    case 0xA892: { int h0 = sw(P + GP_pnLoc + 2), v0 = sw(P + GP_pnLoc), h = h0 + ARGW(2), v = v0 + ARGW(0);   /* Line */
                   qd_line(P, h0, v0, h, v); ww(P + GP_pnLoc, (unsigned)v); ww(P + GP_pnLoc + 2, (unsigned)h); POP(4); } break;
    case 0xA893: ww(P + GP_pnLoc, ARGUW(0)); ww(P + GP_pnLoc + 2, ARGUW(2)); POP(4); break;   /* MoveTo */
    case 0xA894: ww(P + GP_pnLoc, (unsigned)(sw(P + GP_pnLoc) + ARGW(0))); ww(P + GP_pnLoc + 2, (unsigned)(sw(P + GP_pnLoc + 2) + ARGW(2))); POP(4); break;  /* Move */
    case 0xA896: ww(P + GP_pnVis, (unsigned)(sw(P + GP_pnVis) - 1)); POP(0); break;   /* HidePen */
    case 0xA897: ww(P + GP_pnVis, (unsigned)(sw(P + GP_pnVis) + 1)); POP(0); break;   /* ShowPen */
    case 0xA898: memcpy(g_ram + MA(ARGL(0)), g_ram + MA(P + GP_pnLoc), 18); POP(4); break;   /* GetPenState */
    case 0xA899: memcpy(g_ram + MA(P + GP_pnLoc), g_ram + MA(ARGL(0)), 18); POP(4); break;   /* SetPenState */
    case 0xA89A: wl(ARGL(0), rl(P + GP_pnLoc)); POP(4); break;                   /* GetPen */
    case 0xA89B: ww(P + GP_pnSize + 2, ARGUW(2)); ww(P + GP_pnSize, ARGUW(0)); POP(4); break;   /* PenSize(w,h) */
    case 0xA89C: ww(P + GP_pnMode, ARGUW(0)); POP(2); break;                     /* PenMode */
    case 0xA89D: memcpy(g_ram + MA(P + GP_pnPat), g_ram + MA(ARGL(0)), 8); POP(4); break;   /* PenPat */
    case 0xA89E: ww(P + GP_pnSize, 1); ww(P + GP_pnSize + 2, 1); ww(P + GP_pnMode, 8); memcpy(g_ram + MA(P + GP_pnPat), PAT_BLACK, 8); POP(0); break;  /* PenNormal */
    /* rects */
    case 0xA8A1: qd_frame_rect(P, argrect(ARGL(0))); POP(4); break;              /* FrameRect */
    case 0xA8A2: pen_pat(P, pat); qd_fill_rect(P, argrect(ARGL(0)), pat, sw(P + GP_pnMode)); POP(4); break;   /* PaintRect */
    case 0xA8A3: pat_of(P + GP_bkPat, pat); qd_fill_rect(P, argrect(ARGL(0)), pat, 8); POP(4); break;        /* EraseRect */
    case 0xA8A4: qd_fill_rect(P, argrect(ARGL(0)), PAT_BLACK, 10); POP(4); break;                            /* InverRect */
    case 0xA8A5: pat_of(ARGL(0), pat); qd_fill_rect(P, argrect(ARGL(4)), pat, 8); POP(8); break;             /* FillRect */
    case 0xA8A6: RETB(8, !memcmp(g_ram + MA(ARGL(4)), g_ram + MA(ARGL(0)), 8)); break;                       /* EqualRect */
    case 0xA8A7: rect_put(ARGL(8), mkrect(ARGW(4), ARGW(6), ARGW(0), ARGW(2))); POP(12); break;               /* SetRect(r,l,t,r,b) */
    case 0xA8A8: { unsigned r = ARGL(4); rect_put(r, rect_off(rect_at(r), ARGW(2), ARGW(0))); POP(8); } break;   /* OffsetRect */
    case 0xA8A9: { unsigned a = ARGL(4); Rect r = rect_at(a); int dh = ARGW(2), dv = ARGW(0);                 /* InsetRect */
                   r.left += dh; r.right -= dh; r.top += dv; r.bottom -= dv; if (rect_empty(r)) r = mkrect(0,0,0,0); rect_put(a, r); POP(8); } break;
    case 0xA8AA: { Rect r = rect_sect(rect_at(ARGL(8)), rect_at(ARGL(4))); rect_put(ARGL(0), r); RETB(12, !rect_empty(r)); } break;   /* SectRect */
    case 0xA8AB: { Rect a = rect_at(ARGL(8)), b = rect_at(ARGL(4)), r;                                       /* UnionRect */
                   r = mkrect(a.top<b.top?a.top:b.top, a.left<b.left?a.left:b.left, a.bottom>b.bottom?a.bottom:b.bottom, a.right>b.right?a.right:b.right);
                   rect_put(ARGL(0), r); POP(12); } break;
    case 0xA8AC: { int v1 = ARGW(8), h1 = ARGW(10), v2 = ARGW(4), h2 = ARGW(6);                              /* Pt2Rect */
                   rect_put(ARGL(0), mkrect(v1<v2?v1:v2, h1<h2?h1:h2, v1>v2?v1:v2, h1>h2?h1:h2)); POP(12); } break;
    case 0xA8AD: RETB(8, pt_in(rect_at(ARGL(0)), ARGW(6), ARGW(4))); break;                                  /* PtInRect(pt, r) */
    case 0xA8AE: RETB(4, rect_empty(rect_at(ARGL(0)))); break;                                               /* EmptyRect */
    /* round rects, ovals, arcs */
    case 0xA8B0: qd_frame_round(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 0, 0, 0); POP(8); break;
    case 0xA8B1: pen_pat(P, pat); qd_frame_round(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, pat, sw(P + GP_pnMode)); POP(8); break;
    case 0xA8B2: pat_of(P + GP_bkPat, pat); qd_frame_round(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, pat, 8); POP(8); break;
    case 0xA8B3: qd_frame_round(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, PAT_BLACK, 10); POP(8); break;
    case 0xA8B4: pat_of(ARGL(0), pat); qd_frame_round(P, argrect(ARGL(8)), ARGW(6), ARGW(4), 1, pat, 8); POP(12); break;
    case 0xA8B7: qd_oval(P, argrect(ARGL(0)), 0, 0, 0); POP(4); break;
    case 0xA8B8: pen_pat(P, pat); qd_oval(P, argrect(ARGL(0)), 1, pat, sw(P + GP_pnMode)); POP(4); break;
    case 0xA8B9: pat_of(P + GP_bkPat, pat); qd_oval(P, argrect(ARGL(0)), 1, pat, 8); POP(4); break;
    case 0xA8BA: qd_oval(P, argrect(ARGL(0)), 1, PAT_BLACK, 10); POP(4); break;
    case 0xA8BB: pat_of(ARGL(0), pat); qd_oval(P, argrect(ARGL(4)), 1, pat, 8); POP(8); break;
    case 0xA8BE: pen_pat(P, pat); qd_arc(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 0, pat, sw(P + GP_pnMode)); POP(8); break;   /* FrameArc */
    case 0xA8BF: pen_pat(P, pat); qd_arc(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, pat, sw(P + GP_pnMode)); POP(8); break;   /* PaintArc */
    case 0xA8C0: pat_of(P + GP_bkPat, pat); qd_arc(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, pat, 8); POP(8); break;          /* EraseArc */
    case 0xA8C1: qd_arc(P, argrect(ARGL(4)), ARGW(2), ARGW(0), 1, PAT_BLACK, 10); POP(8); break;                              /* InvertArc */
    case 0xA8C2: pat_of(ARGL(0), pat); qd_arc(P, argrect(ARGL(8)), ARGW(6), ARGW(4), 1, pat, 8); POP(12); break;            /* FillArc */
    /* regions */
    case 0xA8D8: RETL(0, rgn_new()); break;                                       /* NewRgn */
    case 0xA8D9: mm_disposhandle(ARGL(0)); POP(4); break;                         /* DisposeRgn */
    case 0xA8DA: rgn_record_begin(); ww(P + GP_pnVis, (unsigned)(sw(P + GP_pnVis) - 1)); POP(0); break;   /* OpenRgn */
    case 0xA8DB: { int v = sw(P + GP_pnVis); ww(P + GP_pnVis, (unsigned)(v + 1)); rgn_record_end(ARGL(0)); POP(4); } break;   /* CloseRgn */
    case 0xA8DC: rgn_copy(ARGL(4), ARGL(0)); POP(8); break;                       /* CopyRgn */
    case 0xA8DD: rgn_set_rect(ARGL(0), mkrect(0,0,0,0)); POP(4); break;           /* SetEmptyRgn */
    case 0xA8DE: rgn_set_rect(ARGL(8), mkrect(ARGW(4), ARGW(6), ARGW(0), ARGW(2))); POP(12); break;   /* SetRectRgn */
    case 0xA8DF: rgn_set_rect(ARGL(4), argrect(ARGL(0))); POP(8); break;          /* RectRgn */
    case 0xA8E0: rgn_offset(ARGL(4), ARGW(2), ARGW(0)); POP(8); break;            /* OffsetRgn */
    case 0xA8E1: rgn_inset(ARGL(4), ARGW(2), ARGW(0)); POP(8); break;             /* InsetRgn */
    case 0xA8E2: RETB(4, rgn_empty(ARGL(0))); break;                              /* EmptyRgn */
    case 0xA8E3: { unsigned a = ARGL(4), b = ARGL(0), na = mm_handlesize(a), nb = mm_handlesize(b);   /* EqualRgn */
                   RETB(8, na == nb && !memcmp(g_ram + MA(rl(a)), g_ram + MA(rl(b)), na)); } break;
    case 0xA8E4: rgn_op(ARGL(0), ARGL(8), ARGL(4), 0); POP(12); break;            /* SectRgn */
    case 0xA8E5: rgn_op(ARGL(0), ARGL(8), ARGL(4), 1); POP(12); break;            /* UnionRgn */
    case 0xA8E6: rgn_op(ARGL(0), ARGL(8), ARGL(4), 2); POP(12); break;            /* DiffRgn */
    case 0xA8E7: rgn_op(ARGL(0), ARGL(8), ARGL(4), 3); POP(12); break;            /* XorRgn */
    case 0xA8E8: RETB(8, rgn_contains(ARGL(0), ARGW(6), ARGW(4))); break;         /* PtInRgn(pt, rgn) */
    case 0xA8E9: { Rect r = argrect(ARGL(4)); unsigned h = ARGL(0); RgnView v = rgn_view(h); int x, y, hit = 0;   /* RectInRgn */
                   Rect s = rect_sect(r, v.bbox);
                   for (y = s.top; y < s.bottom && !hit; y++) for (x = s.left; x < s.right; x++) if (rv_in(&v, x, y)){ hit = 1; break; }
                   RETB(8, hit); } break;
    case 0xA8D2: rgn_draw(P, ARGL(0), 0, 0, 0); POP(4); break;                    /* FrameRgn */
    case 0xA8D3: pen_pat(P, pat); rgn_draw(P, ARGL(0), 1, pat, sw(P + GP_pnMode)); POP(4); break;   /* PaintRgn */
    case 0xA8D4: pat_of(P + GP_bkPat, pat); rgn_draw(P, ARGL(0), 1, pat, 8); POP(4); break;          /* EraseRgn */
    case 0xA8D5: rgn_draw(P, ARGL(0), 1, PAT_BLACK, 10); POP(4); break;                               /* InverRgn */
    case 0xA8D6: pat_of(ARGL(0), pat); rgn_draw(P, ARGL(4), 1, pat, 8); POP(8); break;               /* FillRgn */
    /* bits */
    case 0xA8EC: qd_copybits(ARGL(18), ARGL(14), rect_at(ARGL(10)), rect_at(ARGL(6)), ARGW(4), ARGL(0)); POP(22); break;   /* CopyBits */
    case 0xA8EF: {                                                                /* ScrollRect(r, dh, dv, updateRgn) */
        Rect r = argrect(ARGL(8)); int dh = ARGW(6), dv = ARGW(4); unsigned upd = ARGL(0);
        DC d; Rect cr; int x, y, w, h; unsigned char *tmp;
        dc_port(&d, P); cr = rect_sect(r, d.clip);
        w = cr.right - cr.left; h = cr.bottom - cr.top;
        if (w > 0 && h > 0){
            tmp = (unsigned char*)malloc((size_t)w*(size_t)h);
            for (y = 0; y < h; y++) for (x = 0; x < w; x++) tmp[y*w+x] = (unsigned char)dc_get(&d, cr.left + x, cr.top + y);
            pat_of(P + GP_bkPat, pat);
            dc_fill(&d, cr, pat, 8);
            for (y = 0; y < h; y++) for (x = 0; x < w; x++){
                int nx = cr.left + x + dh, ny = cr.top + y + dv;
                if (pt_in(cr, nx, ny)) dc_apply(&d, nx, ny, tmp[y*w+x], 0);
            }
            free(tmp);
        }
        if (upd){ unsigned moved = rgn_new(); rgn_set_rect(upd, cr); rgn_set_rect(moved, rect_sect(rect_off(cr, dh, dv), cr)); rgn_op(upd, upd, moved, 2); mm_disposhandle(moved); }
        POP(12); } break;
    case 0xA8F6: qd_draw_picture(P, ARGL(4), argrect(ARGL(0))); POP(8); break;    /* DrawPicture */
    case 0xA8F5: mm_disposhandle(ARGL(0)); POP(4); break;                         /* KillPicture */
    case 0xA94B: { unsigned h = ARGL(0); if (h && MA(rl(h))) qd_plot_icon(P, argrect(ARGL(4)), MA(rl(h))); POP(8); } break;   /* PlotIcon */
    case 0xA8F8: { unsigned pt = ARGL(8); Rect s = rect_at(ARGL(4)), d = rect_at(ARGL(0));   /* ScalePt */
                   if (s.right > s.left && s.bottom > s.top){
                       ww(pt+2, (unsigned)(sw(pt+2) * (d.right - d.left) / (s.right - s.left)));
                       ww(pt, (unsigned)(sw(pt) * (d.bottom - d.top) / (s.bottom - s.top))); }
                   POP(12); } break;
    case 0xA8F9: { unsigned pt = ARGL(8); Rect s = rect_at(ARGL(4)), d = rect_at(ARGL(0));   /* MapPt */
                   if (s.right > s.left && s.bottom > s.top){
                       ww(pt+2, (unsigned)(d.left + (sw(pt+2) - s.left) * (d.right - d.left) / (s.right - s.left)));
                       ww(pt, (unsigned)(d.top + (sw(pt) - s.top) * (d.bottom - d.top) / (s.bottom - s.top))); }
                   POP(12); } break;
    case 0xA8FA: { unsigned r = ARGL(8); Rect s = rect_at(ARGL(4)), d = rect_at(ARGL(0)), x = rect_at(r);   /* MapRect */
                   if (s.right > s.left && s.bottom > s.top){
                       x.left = (short)(d.left + (x.left - s.left) * (d.right - d.left) / (s.right - s.left));
                       x.right = (short)(d.left + (x.right - s.left) * (d.right - d.left) / (s.right - s.left));
                       x.top = (short)(d.top + (x.top - s.top) * (d.bottom - d.top) / (s.bottom - s.top));
                       x.bottom = (short)(d.top + (x.bottom - s.top) * (d.bottom - d.top) / (s.bottom - s.top));
                       rect_put(r, x); }
                   POP(12); } break;
    case 0xA8EA: POP(4); break;                                                   /* SetStdProcs */
    /* Color QuickDraw is reported absent; these keep the stack right if called anyway */
    case 0xAA00: wl(ARGL(0) + GP_visRgn, 0); wl(ARGL(0) + GP_clipRgn, 0); qd_init_port(ARGL(0)); POP(4); break;   /* OpenCPort */
    case 0xAA03: RETL(0, 0); break;                                               /* NewPixMap */
    case 0xAA06: case 0xAA0A: case 0xAA0B: POP(4); break;                         /* SetPortPix PenPixPat BackPixPat */
    case 0xAA0C: RETL(2, 0); break;                                               /* GetPixPat */
    case 0xAA0E: case 0xAA0F: POP(8); break;                                      /* FillCRect FillCOval */
    case 0xAA14: case 0xAA15: POP(4); break;                                      /* RGBForeColor RGBBackColor */
    default: t->handled = 0;
    }
}
