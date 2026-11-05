/* QuickDraw regions.
 *
 * A region handle holds the standard 10-byte header [rgnSize][rgnBBox]; a size of 10
 * means a plain rectangle, exactly as on a Mac, so code that tests for rectangular
 * regions still works. Any other shape stores a 1-bit mask of its bounding box
 * after the header instead of Apple's inversion-point data: nothing outside
 * QuickDraw reads that part, and masks make every region operation simple and exact.
 */
#include "mac.h"
#include <math.h>

typedef struct { Rect bbox; int w, h, rb; unsigned char *m; } Mask;

static void mask_alloc(Mask *k, Rect r){
    k->bbox = r; k->w = r.right - r.left; k->h = r.bottom - r.top;
    if (k->w < 0) k->w = 0; if (k->h < 0) k->h = 0;
    k->rb = (k->w + 7) >> 3; k->m = (unsigned char*)calloc((size_t)k->rb * (size_t)k->h + 1, 1);
}
static __inline int mk_get(const Mask *k, int x, int y){
    x -= k->bbox.left; y -= k->bbox.top;
    if (x < 0 || y < 0 || x >= k->w || y >= k->h) return 0;
    return (k->m[y*k->rb + (x>>3)] >> (7-(x&7))) & 1;
}
static __inline void mk_xor(Mask *k, int x, int y){
    x -= k->bbox.left; y -= k->bbox.top;
    if (x < 0 || y < 0 || x >= k->w || y >= k->h) return;
    k->m[y*k->rb + (x>>3)] ^= (unsigned char)(0x80 >> (x&7));
}
static __inline void mk_set(Mask *k, int x, int y, int v){
    x -= k->bbox.left; y -= k->bbox.top;
    if (x < 0 || y < 0 || x >= k->w || y >= k->h) return;
    if (v) k->m[y*k->rb + (x>>3)] |= (unsigned char)(0x80 >> (x&7));
    else   k->m[y*k->rb + (x>>3)] &= (unsigned char)~(0x80 >> (x&7));
}

unsigned rgn_new(void){
    unsigned h = mm_newhandle(10, 1);
    if (h) ww(MA(rl(h)), 10);
    return h;
}
Rect rgn_bbox(unsigned h){ unsigned p; if (!h || !(p = MA(rl(h)))) return mkrect(0,0,0,0); return rect_at(p + 2); }
int  rgn_is_rect(unsigned h){ unsigned p; if (!h || !(p = MA(rl(h)))) return 1; return rw(p) == 10; }
void rgn_set_rect(unsigned h, Rect r){
    unsigned p;
    if (!h) return;
    if (rect_empty(r)) r = mkrect(0,0,0,0);
    if (mm_handlesize(h) != 10) mm_sethandlesize(h, 10);
    p = MA(rl(h)); ww(p, 10); rect_put(p + 2, r);
}
RgnView rgn_view(unsigned h){
    RgnView v; unsigned p = h ? MA(rl(h)) : 0;
    v.bbox = p ? rect_at(p + 2) : mkrect(0,0,0,0);
    v.rect = !p || rw(p) == 10;
    v.rb = ((v.bbox.right - v.bbox.left) + 7) >> 3; v.data = p + 10;
    return v;
}
int rgn_contains(unsigned h, int x, int y){ RgnView v = rgn_view(h); return rv_in(&v, x, y); }
int rgn_empty(unsigned h){ return rect_empty(rgn_bbox(h)); }

/* region -> host mask over frame */
static void to_mask(unsigned h, Mask *k){
    RgnView v = rgn_view(h); int x, y;
    Rect r = rect_sect(v.bbox, k->bbox);
    for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++)
        if (rv_in(&v, x, y)) mk_set(k, x, y, 1);
}
/* host mask -> region (tight bbox; plain rect when fully set) */
static void from_mask(unsigned h, Mask *k){
    int x, y, t = 0x7FFF, l = 0x7FFF, b = -0x7FFF, r = -0x7FFF, full = 1; unsigned p, rb, sz;
    for (y = 0; y < k->h; y++) for (x = 0; x < k->w; x++)
        if ((k->m[y*k->rb + (x>>3)] >> (7-(x&7))) & 1){ if (y < t) t = y; if (y >= b) b = y+1; if (x < l) l = x; if (x >= r) r = x+1; }
    if (b <= t){ rgn_set_rect(h, mkrect(0,0,0,0)); return; }
    for (y = t; y < b && full; y++) for (x = l; x < r; x++) if (!((k->m[y*k->rb + (x>>3)] >> (7-(x&7))) & 1)){ full = 0; break; }
    t += k->bbox.top; b += k->bbox.top; l += k->bbox.left; r += k->bbox.left;
    if (full){ rgn_set_rect(h, mkrect(t,l,b,r)); return; }
    rb = (unsigned)((r - l + 7) >> 3); sz = 10 + rb * (unsigned)(b - t);
    if (mm_sethandlesize(h, sz)) { rgn_set_rect(h, mkrect(t,l,b,r)); return; }
    p = MA(rl(h)); ww(p, sz > 0x7FFF ? 0x7FFF : sz); rect_put(p + 2, mkrect(t,l,b,r));
    memset(g_ram + p + 10, 0, rb * (unsigned)(b - t));
    for (y = t; y < b; y++) for (x = l; x < r; x++)
        if (mk_get(k, x, y)) g_ram[p + 10 + (unsigned)(y-t)*rb + (unsigned)((x-l)>>3)] |= (unsigned char)(0x80 >> ((x-l)&7));
}

static Rect runion(Rect a, Rect b){
    if (rect_empty(a)) return b; if (rect_empty(b)) return a;
    return mkrect(a.top<b.top?a.top:b.top, a.left<b.left?a.left:b.left, a.bottom>b.bottom?a.bottom:b.bottom, a.right>b.right?a.right:b.right);
}
void rgn_op(unsigned dst, unsigned a, unsigned b, int op){
    Rect ra = rgn_bbox(a), rbx = rgn_bbox(b);
    Mask ka, kb; int i, n;
    if (rgn_is_rect(a) && rgn_is_rect(b)){
        if (op == 0){ rgn_set_rect(dst, rect_sect(ra, rbx)); return; }
        if (op == 1 && rect_empty(rbx)){ rgn_set_rect(dst, ra); return; }
        if (op == 1 && rect_empty(ra)){ rgn_set_rect(dst, rbx); return; }
        if (op == 2 && rect_empty(rect_sect(ra, rbx))){ rgn_set_rect(dst, ra); return; }
    }
    {   Rect fr = runion(ra, rbx);
        mask_alloc(&ka, fr); mask_alloc(&kb, fr);
        to_mask(a, &ka); to_mask(b, &kb);
        n = ka.rb * ka.h;
        for (i = 0; i < n; i++){
            if (op == 0) ka.m[i] &= kb.m[i];
            else if (op == 1) ka.m[i] |= kb.m[i];
            else if (op == 2) ka.m[i] &= (unsigned char)~kb.m[i];
            else ka.m[i] ^= kb.m[i];
        }
        from_mask(dst, &ka); free(ka.m); free(kb.m);
    }
}
void rgn_copy(unsigned src, unsigned dst){
    unsigned n = mm_handlesize(src);
    if (src == dst || !src || !dst) return;
    mm_sethandlesize(dst, n);
    memmove(g_ram + MA(rl(dst)), g_ram + MA(rl(src)), n);
}
void rgn_offset(unsigned h, int dh, int dv){
    unsigned p; Rect r; if (!h || !(p = MA(rl(h)))) return;
    r = rect_at(p + 2); if (rect_empty(r)) return;
    rect_put(p + 2, rect_off(r, dh, dv));
}
void rgn_inset(unsigned h, int dh, int dv){
    Rect r = rgn_bbox(h);
    if (rgn_is_rect(h)){ r.top += dv; r.bottom -= dv; r.left += dh; r.right -= dh; rgn_set_rect(h, r); return; }
    {   /* erode/dilate the mask by (dh,dv) */
        Rect fr = r; Mask a, o; int x, y, i, j;
        if (dh < 0 || dv < 0){ fr.top += dv; fr.bottom -= dv; fr.left += dh; fr.right -= dh; }
        mask_alloc(&a, fr); mask_alloc(&o, fr); to_mask(h, &a);
        for (y = fr.top; y < fr.bottom; y++) for (x = fr.left; x < fr.right; x++){
            int v = dh >= 0 && dv >= 0;
            for (j = -abs(dv); j <= abs(dv); j++) for (i = -abs(dh); i <= abs(dh); i++){
                int s = mk_get(&a, x+i, y+j);
                if (dh >= 0 && dv >= 0){ if (!s) v = 0; } else if (s) v = 1;
            }
            mk_set(&o, x, y, v);
        }
        from_mask(h, &o); free(a.m); free(o.m);
    }
}

/* ---- shapes ---------------------------------------------------------------------- */
static void xor_oval(Mask *k, Rect r){
    double cx = (r.left + r.right) / 2.0, cy = (r.top + r.bottom) / 2.0, a = (r.right - r.left) / 2.0, b = (r.bottom - r.top) / 2.0;
    int x, y;
    if (a <= 0 || b <= 0) return;
    for (y = r.top; y < r.bottom; y++){
        double dy = (y + 0.5 - cy) / b, dx;
        if (dy*dy >= 1) continue;
        dx = a * sqrt(1 - dy*dy);
        for (x = r.left; x < r.right; x++) if (fabs(x + 0.5 - cx) < dx) mk_xor(k, x, y);
    }
}
static void xor_round(Mask *k, Rect r, int ow, int oh){
    int x, y, w = r.right - r.left, h = r.bottom - r.top;
    if (ow > w) ow = w; if (oh > h) oh = h;
    if (ow <= 1 || oh <= 1){ for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++) mk_xor(k, x, y); return; }
    for (y = r.top; y < r.bottom; y++) for (x = r.left; x < r.right; x++){
        /* inside unless in a corner square and outside that corner's oval */
        double cx, cy, a = ow / 2.0, b = oh / 2.0, dx, dy; int in = 1;
        if (x < r.left + ow/2 || x >= r.right - ow/2 || y < r.top + oh/2 || y >= r.bottom - oh/2){
            cx = (x < r.left + ow/2) ? r.left + a : (x >= r.right - ow/2 ? r.right - a : x + 0.5);
            cy = (y < r.top + oh/2) ? r.top + b : (y >= r.bottom - oh/2 ? r.bottom - b : y + 0.5);
            dx = (x + 0.5 - cx) / a; dy = (y + 0.5 - cy) / b;
            in = dx*dx + dy*dy < 1.0;
        }
        if (in) mk_xor(k, x, y);
    }
}
void rgn_from_oval(unsigned h, Rect r){ Mask k; mask_alloc(&k, r); xor_oval(&k, r); from_mask(h, &k); free(k.m); }
void rgn_from_round(unsigned h, Rect r, int ow, int oh){ Mask k; mask_alloc(&k, r); xor_round(&k, r, ow, oh); from_mask(h, &k); free(k.m); }

/* ---- OpenRgn recording ------------------------------------------------------------- */
typedef struct { int kind; Rect r; int a, b, h0, v0, h1, v1; } RecOp;
static RecOp *g_rec; static int g_nrec, g_caprec, g_recording;
void rgn_record_begin(void){ g_nrec = 0; g_recording = 1; }
int  rgn_recording(void){ return g_recording; }
static RecOp *rec_add(int kind){
    if (g_nrec == g_caprec){ g_caprec = g_caprec ? g_caprec*2 : 64; g_rec = (RecOp*)realloc(g_rec, sizeof(RecOp)*(size_t)g_caprec); }
    memset(&g_rec[g_nrec], 0, sizeof(RecOp)); g_rec[g_nrec].kind = kind; return &g_rec[g_nrec++];
}
void rgn_record_line(int h0, int v0, int h1, int v1){ RecOp *o = rec_add(0); o->h0=h0; o->v0=v0; o->h1=h1; o->v1=v1; }
void rgn_record_rect(Rect r){ rec_add(1)->r = r; }
void rgn_record_oval(Rect r){ rec_add(2)->r = r; }
void rgn_record_round(Rect r, int ow, int oh){ RecOp *o = rec_add(3); o->r = r; o->a = ow; o->b = oh; }
void rgn_record_end(unsigned h){
    Rect fr = mkrect(0,0,0,0); int i, y, x, nx; Mask k; double xs[512];
    g_recording = 0;
    for (i = 0; i < g_nrec; i++){
        RecOp *o = &g_rec[i];
        Rect r = o->kind ? o->r : mkrect(o->v0<o->v1?o->v0:o->v1, o->h0<o->h1?o->h0:o->h1, (o->v0>o->v1?o->v0:o->v1)+1, (o->h0>o->h1?o->h0:o->h1)+1);
        fr = runion(fr, r);
    }
    if (rect_empty(fr)){ rgn_set_rect(h, fr); return; }
    mask_alloc(&k, fr);
    for (i = 0; i < g_nrec; i++){
        RecOp *o = &g_rec[i];
        if (o->kind == 1){ for (y = o->r.top; y < o->r.bottom; y++) for (x = o->r.left; x < o->r.right; x++) mk_xor(&k, x, y); }
        else if (o->kind == 2) xor_oval(&k, o->r);
        else if (o->kind == 3) xor_round(&k, o->r, o->a, o->b);
    }
    /* lines: even-odd scanline fill at pixel centres */
    for (y = fr.top; y < fr.bottom; y++){
        double yc = y + 0.5;
        nx = 0;
        for (i = 0; i < g_nrec && nx < 510; i++){
            RecOp *o = &g_rec[i];
            if (o->kind || o->v0 == o->v1) continue;
            if ((yc >= o->v0 && yc < o->v1) || (yc >= o->v1 && yc < o->v0))
                xs[nx++] = o->h0 + (yc - o->v0) * (double)(o->h1 - o->h0) / (double)(o->v1 - o->v0);
        }
        { int a, b; for (a = 1; a < nx; a++){ double v = xs[a]; for (b = a; b > 0 && xs[b-1] > v; b--) xs[b] = xs[b-1]; xs[b] = v; } }
        for (i = 0; i + 1 < nx; i += 2){
            int x0 = (int)ceil(xs[i] - 0.5), x1 = (int)ceil(xs[i+1] - 0.5);
            for (x = x0; x < x1; x++) mk_xor(&k, x, y);
        }
    }
    from_mask(h, &k); free(k.m);
}
