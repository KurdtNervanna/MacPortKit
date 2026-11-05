/* Resource Manager over host resource-fork files.
 *
 * Open files form a chain searched from the current file back to the System file,
 * as on a Mac. Loaded resources are real handles in emulated memory. The game's own
 * file is the original, read-only fork; resources it changes (high scores, options)
 * are kept in an overlay file in the save folder and layered over it at startup.
 */
#include "mac.h"
#include "plat.h"

#define MAXFILES 16
typedef struct {
    unsigned type; int id; unsigned char name[256]; int attrs;
    unsigned char *data; unsigned size;     /* host copy of the stored data */
    unsigned handle; int changed, removed, overlay;
} Res;
typedef struct {
    int used, refnum, readonly, dirty, attrs;
    char path[600];                         /* where it is written (overlay for the app) */
    Res *r; int n, cap;
} RFile;

static RFile g_f[MAXFILES];
static int g_chain[MAXFILES], g_nchain;     /* chain[0] = newest (top) ... last = System */
static int g_cur = -1;                      /* index into g_f of the current file */
static int g_app = -1, g_sys = -1;
static int g_resload = 1;
static int g_next_ref = 20;

static void res_err(int e){ ww(LM_ResErr, (unsigned)e); }

static unsigned be32(const unsigned char *p){ return (unsigned)p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3]; }
static unsigned be16(const unsigned char *p){ return (unsigned)(p[0]<<8 | p[1]); }

static Res *add_entry(RFile *f){
    if (f->n == f->cap){ f->cap = f->cap ? f->cap*2 : 64; f->r = (Res*)realloc(f->r, sizeof(Res)*(size_t)f->cap); }
    memset(&f->r[f->n], 0, sizeof(Res));
    return &f->r[f->n++];
}
static Res *find_in(RFile *f, unsigned type, int id){
    int i; for (i = 0; i < f->n; i++) if (!f->r[i].removed && f->r[i].type == type && f->r[i].id == id) return &f->r[i];
    return 0;
}

/* parse a raw resource fork into entries (later entries override earlier ones) */
static int parse_fork(RFile *f, const unsigned char *b, unsigned len, int overlay){
    unsigned doff, moff, mlen, tlo, nlo, nt, i, j;
    if (len < 16) return len == 0;
    doff = be32(b); moff = be32(b+4); mlen = be32(b+12);
    if (moff + mlen > len || moff + 30 > len) return 0;
    f->attrs = be16(b + moff + 22);
    tlo = be16(b + moff + 24); nlo = be16(b + moff + 26);
    nt = (be16(b + moff + tlo) + 1) & 0xFFFF;
    if (be16(b + moff + tlo) == 0xFFFF) nt = 0;
    for (i = 0; i < nt; i++){
        const unsigned char *te = b + moff + tlo + 2 + i*8;
        unsigned type = be32(te), cnt = be16(te+4) + 1, rlo = be16(te+6);
        for (j = 0; j < cnt; j++){
            const unsigned char *re = b + moff + tlo + rlo + j*12;
            int id = (short)be16(re); unsigned nmo = be16(re+2), at = re[4], off = (unsigned)re[5]<<16 | re[6]<<8 | re[7];
            unsigned p = doff + off, sz = be32(b + p);
            Res *r = find_in(f, type, id);
            if (!r) r = add_entry(f); else { free(r->data); }
            r->type = type; r->id = id; r->attrs = (int)at; r->overlay = overlay;
            r->name[0] = 0;
            if (nmo != 0xFFFF){ const unsigned char *nm = b + moff + nlo + nmo; memcpy(r->name, nm, (size_t)nm[0] + 1); }
            r->size = sz; r->data = (unsigned char*)malloc(sz ? sz : 1); memcpy(r->data, b + p + 4, sz);
        }
    }
    return 1;
}
/* the resource fork inside an AppleDouble or MacBinary file; raw forks pass through */
static void unwrap_fork(unsigned char *b, unsigned *len){
    unsigned n = *len, off = 0, flen = 0, i;
    if (n > 26 && be32(b) == 0x00051607u){                                     /* AppleDouble */
        unsigned ne = be16(b + 24);
        for (i = 0; i < ne && 26 + i*12 + 12 <= n; i++)
            if (be32(b + 26 + i*12) == 2){ off = be32(b + 30 + i*12); flen = be32(b + 34 + i*12); }
    } else if (n > 128 && b[0] == 0 && b[1] > 0 && b[1] < 64 && b[74] == 0 && b[82] == 0){   /* MacBinary */
        unsigned dlen = be32(b + 83);
        flen = be32(b + 87); off = 128 + (dlen + 127) / 128 * 128;
    } else return;
    if (off + flen > n || !flen) return;
    memmove(b, b + off, flen); *len = flen;
}
static unsigned char *slurp(const char *path, unsigned *len){
    FILE *fp = fopen(path, "rb"); long n; unsigned char *b;
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = (unsigned char*)malloc((size_t)n + 1); *len = (unsigned)fread(b, 1, (size_t)n, fp); fclose(fp);
    unwrap_fork(b, len);
    return b;
}
/* does this file's resource fork hold a resource of this type? */
int rs_file_has(const char *path, unsigned type){
    unsigned len, moff, mlen, tlo, nt, i; unsigned char *b = slurp(path, &len); int found = 0;
    if (!b) return 0;
    if (len >= 16){
        moff = be32(b + 4); mlen = be32(b + 12);
        if (moff + mlen <= len && mlen >= 30){
            tlo = be16(b + moff + 24); nt = (be16(b + moff + tlo) + 1) & 0xFFFF;
            for (i = 0; i < nt && moff + tlo + 2 + i*8 + 4 <= len; i++) if (be32(b + moff + tlo + 2 + i*8) == type){ found = 1; break; }
        }
    }
    free(b);
    return found;
}

/* current data of a resource: its handle if loaded, else the stored copy */
static void res_sync(Res *r){
    if (r->handle && mm_is_handle(r->handle) && MA(rl(r->handle))){
        unsigned n = mm_handlesize(r->handle);
        free(r->data); r->data = (unsigned char*)malloc(n ? n : 1); r->size = n;
        memcpy(r->data, g_ram + MA(rl(r->handle)), n);
    }
}

/* serialize a file (all entries, or only overlay/changed ones) as a resource fork */
static int write_fork(RFile *f, const char *path, int only_overlay){
    unsigned char *d = 0, *m; unsigned dlen = 0, dcap = 0, ntypes = 0, i, j, k;
    unsigned types[256]; int counts[256];
    unsigned char names[65536]; unsigned nlen = 0;
    FILE *fp; unsigned mlen, tlo, rlo, moff;
    for (i = 0; i < (unsigned)f->n; i++){
        Res *r = &f->r[i]; if (r->removed || (only_overlay && !r->overlay)) continue;
        res_sync(r);
        for (k = 0; k < ntypes && types[k] != r->type; k++);
        if (k == ntypes && ntypes < 256){ types[ntypes] = r->type; counts[ntypes++] = 0; }
        if (k < 256) counts[k]++;
    }
    mlen = 30 + 2 + ntypes*8;
    for (k = 0; k < ntypes; k++) mlen += (unsigned)counts[k]*12;
    m = (unsigned char*)calloc(1, mlen + 65536);
    tlo = 28; rlo = 2 + ntypes*8;
    {   unsigned rp = tlo + 2 + ntypes*8;      /* reference lists follow the type list */
        m[tlo] = (unsigned char)((ntypes - 1) >> 8); m[tlo+1] = (unsigned char)(ntypes - 1);
        for (k = 0; k < ntypes; k++){
            unsigned char *te = m + tlo + 2 + k*8;
            te[0]=(unsigned char)(types[k]>>24); te[1]=(unsigned char)(types[k]>>16); te[2]=(unsigned char)(types[k]>>8); te[3]=(unsigned char)types[k];
            te[4]=(unsigned char)((counts[k]-1)>>8); te[5]=(unsigned char)(counts[k]-1);
            te[6]=(unsigned char)((rp - tlo)>>8); te[7]=(unsigned char)(rp - tlo);
            for (j = 0; j < (unsigned)f->n; j++){
                Res *r = &f->r[j]; unsigned char *re;
                if (r->removed || (only_overlay && !r->overlay) || r->type != types[k]) continue;
                re = m + rp; rp += 12;
                re[0]=(unsigned char)(r->id>>8); re[1]=(unsigned char)r->id;
                if (r->name[0]){ re[2]=(unsigned char)(nlen>>8); re[3]=(unsigned char)nlen; memcpy(names+nlen, r->name, (size_t)r->name[0]+1); nlen += r->name[0]+1u; }
                else { re[2]=0xFF; re[3]=0xFF; }
                re[4]=(unsigned char)(r->attrs & 0xFF);
                re[5]=(unsigned char)(dlen>>16); re[6]=(unsigned char)(dlen>>8); re[7]=(unsigned char)dlen;
                if (dlen + 4 + r->size > dcap){ dcap = (dlen + 4 + r->size) * 2; d = (unsigned char*)realloc(d, dcap); }
                d[dlen]=(unsigned char)(r->size>>24); d[dlen+1]=(unsigned char)(r->size>>16); d[dlen+2]=(unsigned char)(r->size>>8); d[dlen+3]=(unsigned char)r->size;
                memcpy(d + dlen + 4, r->data, r->size); dlen += 4 + r->size;
            }
        }
        (void)rlo;
        memcpy(m + mlen, names, nlen);
        m[24]=(unsigned char)(tlo>>8); m[25]=(unsigned char)tlo; m[26]=(unsigned char)(mlen>>8); m[27]=(unsigned char)mlen;
        m[22]=(unsigned char)(f->attrs>>8); m[23]=(unsigned char)f->attrs;
    }
    mlen += nlen; moff = 256 + dlen;
    fp = fopen(path, "wb"); if (!fp){ free(m); free(d); return -1; }
    {   unsigned char h[256]; memset(h, 0, sizeof h);
        h[0]=0;h[1]=0;h[2]=1;h[3]=0;                                   /* data at 256 */
        h[4]=(unsigned char)(moff>>24); h[5]=(unsigned char)(moff>>16); h[6]=(unsigned char)(moff>>8); h[7]=(unsigned char)moff;
        h[8]=(unsigned char)(dlen>>24); h[9]=(unsigned char)(dlen>>16); h[10]=(unsigned char)(dlen>>8); h[11]=(unsigned char)dlen;
        h[12]=(unsigned char)(mlen>>24); h[13]=(unsigned char)(mlen>>16); h[14]=(unsigned char)(mlen>>8); h[15]=(unsigned char)mlen;
        memcpy(m, h, 16);
        fwrite(h, 1, 256, fp); if (dlen) fwrite(d, 1, dlen, fp); fwrite(m, 1, mlen, fp);
    }
    fclose(fp); free(m); free(d);
    return 0;
}

static void persist(RFile *f){
    if (!f->dirty) return;
    if (f->readonly && f != &g_f[g_app]) return;
    if (write_fork(f, f->path, f == &g_f[g_app]) == 0) f->dirty = 0;
    else logf_("[rsrc] could not write %s\n", f->path);
}
void rs_flush_all(void){ int i; for (i = 0; i < MAXFILES; i++) if (g_f[i].used) persist(&g_f[i]); }

static int new_file(void){ int i; for (i = 0; i < MAXFILES; i++) if (!g_f[i].used){ memset(&g_f[i], 0, sizeof g_f[i]); g_f[i].used = 1; return i; } return -1; }
static void chain_push(int fi){ int i; for (i = g_nchain; i > 0; i--) g_chain[i] = g_chain[i-1]; g_chain[0] = fi; g_nchain++; }
static void chain_remove(int fi){ int i, j; for (i = j = 0; i < g_nchain; i++) if (g_chain[i] != fi) g_chain[j++] = g_chain[i]; g_nchain = j; }
static int file_by_ref(int ref){ int i; for (i = 0; i < MAXFILES; i++) if (g_f[i].used && g_f[i].refnum == ref) return i; return -1; }

int rs_open_system(const char *path){
    unsigned len; unsigned char *b = slurp(path, &len); int fi;
    if (!b) return 0;
    fi = new_file(); g_f[fi].refnum = 2; g_f[fi].readonly = 1; strncpy(g_f[fi].path, path, 599);
    parse_fork(&g_f[fi], b, len, 0); free(b);
    chain_push(fi); g_sys = fi; g_cur = fi; ww(LM_SysMap, 2);
    return 1;
}
int rs_open_app(const char *path, const char *overlay){
    unsigned len; unsigned char *b = slurp(path, &len); int fi;
    if (!b) return 0;
    fi = new_file(); g_f[fi].refnum = 3; g_f[fi].readonly = 1; strncpy(g_f[fi].path, overlay, 599);
    if (!parse_fork(&g_f[fi], b, len, 0)){ free(b); g_f[fi].used = 0; return 0; }
    free(b);
    if ((b = slurp(overlay, &len)) != 0){ parse_fork(&g_f[fi], b, len, 1); free(b); logf_("[rsrc] settings loaded from %s\n", overlay); }
    chain_push(fi); g_app = fi; g_cur = fi;
    return 1;
}

static unsigned load(Res *r){
    if (r->handle && mm_is_handle(r->handle) && MA(rl(r->handle))) return r->handle;
    if (!r->handle || !mm_is_handle(r->handle)) r->handle = mm_newhandle(r->size, 0);
    else mm_sethandlesize(r->handle, r->size);
    if (!r->handle){ res_err(-108); return 0; }
    memcpy(g_ram + MA(rl(r->handle)), r->data, r->size);
    wb(r->handle, (rb(r->handle) & 0x1F) | 0x20 | ((r->attrs & 0x10) ? 0x80 : 0));   /* resource bit, locked attr */
    return r->handle;
}
static unsigned get_from(int fi, unsigned type, int id){
    Res *r = find_in(&g_f[fi], type, id);
    if (!r) return 0;
    if (!g_resload){ if (!r->handle) r->handle = mm_newhandle(0, 0); return r->handle; }
    return load(r);
}
static int cur_pos(void){ int i; for (i = 0; i < g_nchain; i++) if (g_chain[i] == g_cur) return i; return 0; }
unsigned rs_get(unsigned type, int id){
    int i; unsigned h;
    for (i = cur_pos(); i < g_nchain; i++) if ((h = get_from(g_chain[i], type, id)) != 0){ res_err(0); return h; }
    res_err(-192);
    return 0;
}
unsigned rs_get1(unsigned type, int id){ unsigned h = g_cur >= 0 ? get_from(g_cur, type, id) : 0; res_err(h ? 0 : -192); return h; }
unsigned rs_get_named(unsigned type, const unsigned char *pname){
    int i, j;
    for (i = cur_pos(); i < g_nchain; i++){ RFile *f = &g_f[g_chain[i]];
        for (j = 0; j < f->n; j++) if (!f->r[j].removed && f->r[j].type == type && f->r[j].name[0] == pname[0]
                                       && !plat_strnicmp((const char*)f->r[j].name + 1, (const char*)pname + 1, pname[0])){ res_err(0); return load(&f->r[j]); } }
    res_err(-192); return 0;
}
const unsigned char *rs_sysdata(unsigned type, int id, unsigned *len){
    Res *r = g_sys >= 0 ? find_in(&g_f[g_sys], type, id) : 0;
    if (!r){ r = g_app >= 0 ? find_in(&g_f[g_app], type, id) : 0; }
    if (!r) return 0;
    *len = r->size; return r->data;
}
static Res *by_handle(unsigned h, int *fi){
    int i, j; if (!h) return 0;
    for (i = 0; i < MAXFILES; i++) if (g_f[i].used) for (j = 0; j < g_f[i].n; j++)
        if (g_f[i].r[j].handle == h && !g_f[i].r[j].removed){ if (fi) *fi = i; return &g_f[i].r[j]; }
    return 0;
}
int rs_handle_info(unsigned h, unsigned *type, int *id, unsigned char *name){
    Res *r = by_handle(h, 0); if (!r) return 0;
    if (type) *type = r->type; if (id) *id = r->id; if (name) memcpy(name, r->name, (size_t)r->name[0] + 1);
    return 1;
}

/* collect the distinct entries visible through the chain (or one file) */
static int collect(int one, unsigned type, Res **out, int max){
    int i, j, k, n = 0, from = cur_pos(), to = one ? from + 1 : g_nchain;
    for (i = from; i < to && i < g_nchain; i++){ RFile *f = &g_f[g_chain[i]];
        for (j = 0; j < f->n && n < max; j++){ Res *r = &f->r[j]; int dup = 0;
            if (r->removed || r->type != type) continue;
            for (k = 0; k < n; k++) if (out[k]->type == r->type && out[k]->id == r->id){ dup = 1; break; }
            if (!dup) out[n++] = r; } }
    return n;
}
static int collect_types(int one, unsigned *out, int max){
    int i, j, k, n = 0, from = cur_pos(), to = one ? from + 1 : g_nchain;
    for (i = from; i < to && i < g_nchain; i++){ RFile *f = &g_f[g_chain[i]];
        for (j = 0; j < f->n && n < max; j++){ int dup = 0; if (f->r[j].removed) continue;
            for (k = 0; k < n; k++) if (out[k] == f->r[j].type){ dup = 1; break; }
            if (!dup) out[n++] = f->r[j].type; } }
    return n;
}

extern int fs_rsrc_path(unsigned pname, int vref, char *out, int create);

void rs_trap(unsigned word, void *tv){
    Trap *t = (Trap*)tv; static Res *list[4096]; static unsigned tl[512];
    t->handled = 1;
    switch (word){
    case 0xA995: RETW(0, 3); break;                                               /* InitResources */
    case 0xA996: POP(0); break;                                                   /* RsrcZoneInit */
    case 0xA9A0: RETL(6, rs_get(ARGL(2), ARGW(0))); break;                        /* GetResource */
    case 0xA81F: RETL(6, rs_get1(ARGL(2), ARGW(0))); break;                       /* Get1Resource */
    case 0xA9A1: { unsigned char nm[256]; unsigned s = ARGL(0); memcpy(nm, g_ram + MA(s), (size_t)rb(s) + 1);   /* GetNamedResource */
                   RETL(8, rs_get_named(ARGL(4), nm)); } break;
    case 0xA820: { unsigned char nm[256]; unsigned s = ARGL(0); memcpy(nm, g_ram + MA(s), (size_t)rb(s) + 1);   /* Get1NamedResource */
                   RETL(8, rs_get_named(ARGL(4), nm)); } break;
    case 0xA9A2: { Res *r = by_handle(ARGL(0), 0); if (r) load(r); res_err(r ? 0 : -192); POP(4); } break;    /* LoadResource */
    case 0xA9A3: { int fi; unsigned h = ARGL(0); Res *r = by_handle(h, &fi);     /* ReleaseResource */
                   if (r){ if (r->changed){ res_sync(r); } r->handle = 0; mm_disposhandle(h); res_err(0); } else res_err(-192);
                   POP(4); } break;
    case 0xA992: { unsigned h = ARGL(0); Res *r = by_handle(h, 0);               /* DetachResource */
                   if (r){ res_sync(r); r->handle = 0; wb(h, rb(h) & 0xDF); res_err(0); } else res_err(-192); POP(4); } break;
    case 0xA9A4: { int fi = -1; Res *r = by_handle(ARGL(0), &fi); res_err(r ? 0 : -192); RETW(4, r ? g_f[fi].refnum : -1); } break;  /* HomeResFile */
    case 0xA9A5: { Res *r = by_handle(ARGL(0), 0); RETL(4, r ? (r->handle && MA(rl(r->handle)) ? mm_handlesize(r->handle) : r->size) : (unsigned)-1); } break;  /* SizeRsrc */
    case 0xA821: { Res *r = by_handle(ARGL(0), 0); RETL(4, r ? r->size : (unsigned)-1); } break;   /* MaxSizeRsrc */
    case 0xA9A6: { Res *r = by_handle(ARGL(0), 0); res_err(r ? 0 : -192); RETW(4, r ? r->attrs : 0); } break;   /* GetResAttrs */
    case 0xA9A7: { Res *r = by_handle(ARGL(2), 0); if (r) r->attrs = ARGW(0) & 0xFF; POP(6); } break;            /* SetResAttrs */
    case 0xA9A8: { Res *r = by_handle(ARGL(12), 0); unsigned pid = ARGL(8), pty = ARGL(4), pnm = ARGL(0);       /* GetResInfo */
                   if (r){ if (pid) ww(pid, (unsigned)r->id); if (pty) wl(pty, r->type); if (pnm) memcpy(g_ram + MA(pnm), r->name, (size_t)r->name[0] + 1); res_err(0); }
                   else res_err(-192);
                   POP(16); } break;
    case 0xA9A9: { int fi; Res *r = by_handle(ARGL(6), &fi); unsigned pnm = ARGL(0);                            /* SetResInfo */
                   if (r){ r->id = ARGW(4); if (pnm) memcpy(r->name, g_ram + MA(pnm), (size_t)rb(pnm) + 1); r->changed = 1; r->overlay = 1; g_f[fi].dirty = 1; }
                   POP(10); } break;
    case 0xA9AA: { int fi; Res *r = by_handle(ARGL(0), &fi);                      /* ChangedResource */
                   if (r){ r->changed = 1; r->overlay = 1; g_f[fi].dirty = 1; res_err(0); } else res_err(-192); POP(4); } break;
    case 0xA9AB: { unsigned h = ARGL(10), type = ARGL(6); int id = ARGW(4); unsigned pnm = ARGL(0); Res *r;   /* AddResource */
                   if (g_cur < 0 || !mm_is_handle(h)){ res_err(-194); POP(14); break; }
                   r = find_in(&g_f[g_cur], type, id); if (r) r->removed = 1;
                   r = add_entry(&g_f[g_cur]);
                   r->type = type; r->id = id; r->handle = h; r->changed = 1; r->overlay = 1;
                   if (pnm) memcpy(r->name, g_ram + MA(pnm), (size_t)rb(pnm) + 1);
                   wb(h, rb(h) | 0x20); res_sync(r); g_f[g_cur].dirty = 1; res_err(0);
                   POP(14); } break;
    case 0xA9AD: { int fi; Res *r = by_handle(ARGL(0), &fi);                      /* RmveResource */
                   if (r){ r->removed = 1; wb(r->handle, rb(r->handle) & 0xDF); r->handle = 0; g_f[fi].dirty = 1; res_err(0); } else res_err(-192); POP(4); } break;
    case 0xA9B0: { int fi; Res *r = by_handle(ARGL(0), &fi);                      /* WriteResource */
                   if (r){ res_sync(r); persist(&g_f[fi]); } POP(4); } break;
    case 0xA999: { int fi = file_by_ref(ARGW(0)); if (fi >= 0) persist(&g_f[fi]); POP(2); } break;   /* UpdateResFile */
    case 0xA9AF: RETW(0, rw(LM_ResErr)); break;                                   /* ResError */
    case 0xA994: RETW(0, g_cur >= 0 ? g_f[g_cur].refnum : 0); break;              /* CurResFile */
    case 0xA998: { int fi = file_by_ref(ARGW(0)); if (ARGW(0) == 0) fi = g_sys;  /* UseResFile */
                   if (fi >= 0){ g_cur = fi; ww(LM_CurMap, (unsigned)g_f[fi].refnum); res_err(0); } else res_err(-193); POP(2); } break;
    case 0xA99B: g_resload = ARGB(0) != 0; wb(LM_ResLoad, (unsigned)g_resload); POP(2); break;   /* SetResLoad */
    case 0xA993: POP(2); break;                                                   /* SetResPurge */
    case 0xA99C: RETW(4, collect(0, ARGL(0), list, 4096)); break;                 /* CountResources */
    case 0xA80D: RETW(4, collect(1, ARGL(0), list, 4096)); break;                 /* Count1Resources */
    case 0xA99D: case 0xA80E: { int n = collect(word == 0xA80E, ARGL(2), list, 4096), i = ARGW(0);   /* GetIndResource */
                   RETL(6, (i >= 1 && i <= n) ? load(list[i-1]) : 0); } break;
    case 0xA99E: RETW(0, collect_types(0, tl, 512)); break;                       /* CountTypes */
    case 0xA81C: RETW(0, collect_types(1, tl, 512)); break;                       /* Count1Types */
    case 0xA99F: case 0xA80F: { int n = collect_types(word == 0xA80F, tl, 512), i = ARGW(0);    /* GetIndType */
                   wl(ARGL(2), (i >= 1 && i <= n) ? tl[i-1] : 0); POP(6); } break;
    case 0xA9C1: { int id; do id = 128 + rand() % 32000; while (rs_get1(ARGL(0), id)); RETW(4, id); } break;   /* UniqueID */
    case 0xA810: { int id; do id = 128 + rand() % 32000; while (rs_get1(ARGL(0), id)); RETW(4, id); } break;   /* Unique1ID */
    case 0xA9B1: { char path[600]; FILE *fp;                                      /* CreateResFile */
                   if (fs_rsrc_path(ARGL(0), 0, path, 1) == 0){
                       if ((fp = fopen(path, "rb")) != 0) fclose(fp);
                       else { RFile tmp; memset(&tmp, 0, sizeof tmp); write_fork(&tmp, path, 0); }
                       res_err(0);
                   } else res_err(-43);
                   POP(4); } break;
    case 0xA997: case 0xA9C4: {                                                   /* OpenResFile / OpenRFPerm */
        unsigned pname = word == 0xA997 ? ARGL(0) : ARGL(4); int vref = word == 0xA997 ? 0 : ARGW(2), pop = word == 0xA997 ? 4 : 8;
        char path[600], nm[256]; unsigned len; unsigned char *b; int fi, i;
        pstr_get(pname, nm, sizeof nm);
        if (!plat_stricmp(nm, HLE_APP_NAME) || !plat_stricmp(nm, HLE_APP_NAME)){ RETW(pop, 3); res_err(0); break; }
        if (fs_rsrc_path(pname, vref, path, 0) != 0 || (b = slurp(path, &len)) == 0){ res_err(-43); RETW(pop, -1); break; }
        for (i = 0; i < MAXFILES; i++) if (g_f[i].used && !plat_stricmp(g_f[i].path, path)){   /* already open */
            free(b); g_cur = i; res_err(0); RETW(pop, g_f[i].refnum); i = -1; break; }
        if (i == -1) break;
        fi = new_file(); if (fi < 0){ free(b); res_err(-42); RETW(pop, -1); break; }
        g_f[fi].refnum = g_next_ref++; strncpy(g_f[fi].path, path, 599);
        if (!parse_fork(&g_f[fi], b, len, 0)){ free(b); g_f[fi].used = 0; res_err(-39); RETW(pop, -1); break; }
        free(b); chain_push(fi); g_cur = fi; ww(LM_CurMap, (unsigned)g_f[fi].refnum); res_err(0);
        RETW(pop, g_f[fi].refnum); } break;
    case 0xA99A: { int fi = file_by_ref(ARGW(0)), i;                              /* CloseResFile */
                   if (fi >= 0 && fi != g_sys && fi != g_app){
                       persist(&g_f[fi]);
                       for (i = 0; i < g_f[fi].n; i++){ if (g_f[fi].r[i].handle) mm_disposhandle(g_f[fi].r[i].handle); free(g_f[fi].r[i].data); }
                       free(g_f[fi].r); chain_remove(fi); g_f[fi].used = 0;
                       if (g_cur == fi) g_cur = g_nchain ? g_chain[0] : -1;
                       res_err(0);
                   } else res_err(fi < 0 ? -193 : 0);
                   POP(2); } break;
    case 0xA9F6: { int fi = file_by_ref(ARGW(0)); RETW(2, fi >= 0 ? g_f[fi].attrs : 0); } break;   /* GetResFileAttrs */
    case 0xA9F7: { int fi = file_by_ref(ARGW(2)); if (fi >= 0) g_f[fi].attrs = ARGW(0); POP(4); } break;   /* SetResFileAttrs */
    case 0xA9AC: case 0xA9AE: POP(word == 0xA9AC ? 10 : 6); break;                /* AddReference / RmveReference */
    default: t->handled = 0;
    }
}
