/* Font Manager: draws the classic Mac bitmap fonts ('FONT' resources) from the
 * System file, so text looks exactly as it did (Chicago, Geneva, Monaco...).
 * Font ID = family * 128 + point size; a missing size falls back to the nearest
 * size of the same family, then to Geneva / Chicago.
 */
#include "mac.h"

typedef struct {
    int ok, first, last, widMax, kernMax, fRectW, fRectH, ascent, descent, leading, rowWords;
    const unsigned char *image, *loc, *ow;
} Font;

static unsigned be16(const unsigned char *p){ return (unsigned)(p[0]<<8 | p[1]); }

static int parse(int resid, Font *f){
    unsigned len; const unsigned char *d = rs_sysdata(0x464F4E54u, resid, &len);   /* 'FONT' */
    int n, owt;
    memset(f, 0, sizeof *f);
    if (!d || len < 26) return 0;
    f->first = (short)be16(d+2); f->last = (short)be16(d+4); f->widMax = (short)be16(d+6); f->kernMax = (short)be16(d+8);
    f->fRectW = (short)be16(d+12); f->fRectH = (short)be16(d+14); owt = (short)be16(d+16);
    f->ascent = (short)be16(d+18); f->descent = (short)be16(d+20); f->leading = (short)be16(d+22); f->rowWords = (short)be16(d+24);
    n = f->last - f->first + 3;
    f->image = d + 26;
    f->loc = f->image + f->rowWords * 2 * f->fRectH;
    f->ow = d + 16 + owt * 2;
    if ((unsigned)(f->ow - d) + (unsigned)(n - 1)*2 > len) return 0;    /* the -1 terminator is often omitted */
    f->ok = 1;
    return 1;
}

static const int g_sizes[] = { 9, 10, 12, 14, 18, 20, 24 };
static Font g_cache[64]; static int g_cache_id[64], g_ncache;
static const Font *get_font(int fam, int size){
    int i, id, best = -1, bestd = 999, k;
    if (fam == 1) fam = 3;                        /* applFont -> Geneva */
    if (fam == 0 && size == 0) size = 12;
    if (size <= 0) size = 12;
    for (k = 0; k < 3; k++){
        int f = k == 0 ? fam : (k == 1 ? 3 : 0);
        for (i = -1; i < (int)(sizeof g_sizes / sizeof g_sizes[0]); i++){
            int s = i < 0 ? size : g_sizes[i], d = s - size; unsigned len;
            if (d < 0) d = -d * 2 + 1;            /* prefer larger over smaller at equal distance */
            id = f * 128 + s;
            if (rs_sysdata(0x464F4E54u, id, &len) && d < bestd){ bestd = d; best = id; }
        }
        if (best >= 0) break;
    }
    if (best < 0) return 0;
    for (i = 0; i < g_ncache; i++) if (g_cache_id[i] == best) return &g_cache[i];
    if (g_ncache == 64) g_ncache = 0;
    if (!parse(best, &g_cache[g_ncache])) return 0;
    g_cache_id[g_ncache] = best;
    return &g_cache[g_ncache++];
}

static int char_index(const Font *f, int ch){
    int idx;
    if (ch < f->first || ch > f->last) idx = f->last - f->first + 1;      /* missing symbol */
    else idx = ch - f->first;
    if (be16(f->ow + idx*2) == 0xFFFF) idx = f->last - f->first + 1;
    return idx;
}

int font_metrics(int font, int size, int face, FontMetrics *m){
    const Font *f = get_font(font, size);
    if (!f){ m->ascent = 9; m->descent = 2; m->widMax = 8; m->leading = 1; m->height = 12; return 0; }
    m->ascent = f->ascent; m->descent = f->descent; m->widMax = f->widMax + ((face & 1) ? 1 : 0);
    m->leading = f->leading; m->height = f->ascent + f->descent + f->leading;
    return 1;
}
int font_char_width(int font, int size, int face, int ch){
    const Font *f = get_font(font, size); int w;
    if (!f) return 7;
    w = be16(f->ow + char_index(f, ch & 0xFF)*2) & 0xFF;
    if (face & 1) w++;                 /* bold   */
    if (face & 0x20) w--;              /* condense */
    if (face & 0x40) w++;              /* extend */
    if (face & 0x08) w++;              /* outline */
    if (face & 0x10) w += 2;           /* shadow */
    return w;
}
int font_draw_char(int font, int size, int face, int ch, int h, int v, void (*plot)(int, int, void*), void *ctx){
    const Font *f = get_font(font, size); int idx, entry, off, x0, x1, row, col, adv;
    if (!f) return 7;
    idx = char_index(f, ch & 0xFF);
    entry = (int)be16(f->ow + idx*2); off = (entry >> 8) + f->kernMax; adv = entry & 0xFF;
    x0 = (int)be16(f->loc + idx*2); x1 = (int)be16(f->loc + idx*2 + 2);
    for (row = 0; row < f->fRectH; row++){
        const unsigned char *rp = f->image + row * f->rowWords * 2;
        int y = v - f->ascent + row, sh = (face & 2) ? (f->ascent - row) / 2 : 0;
        for (col = x0; col < x1; col++){
            if ((rp[col >> 3] >> (7 - (col & 7))) & 1){
                int x = h + off + (col - x0) + sh;
                plot(x, y, ctx);
                if (face & 1) plot(x + 1, y, ctx);
            }
        }
    }
    if (face & 4){ int x; for (x = 0; x < adv + ((face & 1) ? 1 : 0); x++) plot(h + x, v + 1, ctx); }
    return font_char_width(font, size, face, ch);
}
