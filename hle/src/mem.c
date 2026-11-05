/* Emulated RAM, Musashi's memory interface, and the Memory Manager.
 *
 * Heap blocks carry a 12-byte header: [owner handle][logical size][capacity].
 * Handles are master pointers allocated from their own table; as on a 24-bit Mac
 * the master pointer's high byte holds the lock/purge/resource flags, which is
 * harmless because every access is masked to 24 bits (MA).
 */
#include "mac.h"

unsigned char g_ram[MEM_SIZE];

unsigned int m68k_read_memory_8 (unsigned int a){ return rb(a); }
unsigned int m68k_read_memory_16(unsigned int a){ return rw(a); }
unsigned int m68k_read_memory_32(unsigned int a){ return rl(a); }
void m68k_write_memory_8 (unsigned int a, unsigned int v){ wb(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v){ ww(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v){ wl(a, v); }
unsigned int m68k_read_disassembler_8 (unsigned int a){ return rb(a); }
unsigned int m68k_read_disassembler_16(unsigned int a){ return rw(a); }
unsigned int m68k_read_disassembler_32(unsigned int a){ return rl(a); }

#define MP_BASE  HEAP_BASE                 /* master pointers */
#define MP_END   (HEAP_BASE + 0x20000u)
#define BLK_BASE MP_END                    /* blocks */
#define HDR 12u

static unsigned g_bump = BLK_BASE, g_mp_next = MP_BASE, g_mp_free = 0;
#define MAXFREE 4096
static struct { unsigned addr, cap; } g_free[MAXFREE];
static int g_nfree;

void mm_init(void){ g_bump = BLK_BASE; g_mp_next = MP_BASE; g_mp_free = 0; g_nfree = 0; }

static unsigned blk_alloc(unsigned size, int clear){
    unsigned cap = (size + 3u) & ~3u, blk = 0; int i, best = -1;
    if (cap < 4) cap = 4;
    for (i = 0; i < g_nfree; i++)                       /* best fit from the free list */
        if (g_free[i].cap >= cap && (best < 0 || g_free[i].cap < g_free[best].cap)) best = i;
    if (best >= 0){
        blk = g_free[best].addr; cap = g_free[best].cap;
        g_free[best] = g_free[--g_nfree];
    } else {
        if (g_bump + HDR + cap > HEAP_END){ wl(LM_MemErr, (unsigned)-108 & 0xFFFF); return 0; }
        blk = g_bump + HDR; g_bump += HDR + cap;
    }
    wl(blk-12, 0); wl(blk-8, size); wl(blk-4, cap);
    if (clear) memset(g_ram + blk, 0, cap);
    ww(LM_MemErr, 0);
    return blk;
}
static void blk_free(unsigned blk){
    blk = MA(blk);
    if (blk < BLK_BASE + HDR || blk >= HEAP_END) return;
    if (blk - HDR + rl(blk-4) + HDR == g_bump){ g_bump = blk - HDR; return; }   /* top of heap */
    if (g_nfree < MAXFREE){ g_free[g_nfree].addr = blk; g_free[g_nfree].cap = rl(blk-4); g_nfree++; }
}

unsigned mm_newptr(unsigned size, int clear){ return blk_alloc(size, clear); }
void     mm_disposptr(unsigned p){ blk_free(p); }
unsigned mm_ptrsize(unsigned p){ p = MA(p); return (p >= BLK_BASE+HDR && p < HEAP_END) ? rl(p-8) : 0; }

int mm_is_handle(unsigned h){ h = MA(h); return h >= MP_BASE && h < g_mp_next && !(h & 3); }

unsigned mm_newhandle(unsigned size, int clear){
    unsigned mp, blk = blk_alloc(size, clear);
    if (!blk) return 0;
    if (g_mp_free){ mp = g_mp_free; g_mp_free = rl(mp); }
    else { if (g_mp_next >= MP_END){ blk_free(blk); return 0; } mp = g_mp_next; g_mp_next += 4; }
    wl(mp, blk); wl(blk-12, mp);
    return mp;
}
unsigned mm_newhandle_from(const void *data, unsigned size){
    unsigned h = mm_newhandle(size, 0);
    if (h && size) memcpy(g_ram + MA(rl(h)), data, size);
    return h;
}
void mm_disposhandle(unsigned h){
    unsigned blk;
    if (!mm_is_handle(h)) return;
    h = MA(h); blk = MA(rl(h));
    if (blk) blk_free(blk);
    wl(h, g_mp_free); g_mp_free = h;
}
unsigned mm_handlesize(unsigned h){
    unsigned blk; if (!mm_is_handle(h)) return 0;
    blk = MA(rl(h)); return blk ? rl(blk-8) : 0;
}
int mm_sethandlesize(unsigned h, unsigned size){
    unsigned blk, nb, flags, old;
    if (!mm_is_handle(h)) return -109;                 /* nilHandleErr */
    h = MA(h); flags = rl(h) & 0xFF000000u; blk = MA(rl(h));
    if (blk && rl(blk-4) >= size){ wl(blk-8, size); return 0; }
    nb = blk_alloc(size + size/4, 0);                   /* grow with slack */
    if (!nb) return -108;
    wl(nb-8, size); wl(nb-12, h);
    old = blk ? rl(blk-8) : 0;
    if (blk){ memmove(g_ram + nb, g_ram + blk, old < size ? old : size); blk_free(blk); }
    if (flags & 0x80000000u) logf_("[mem] locked handle %06X moved to grow to %u\n", h, size);
    wl(h, nb | flags);
    return 0;
}
unsigned mm_recover(unsigned p){
    p = MA(p);
    if (p < BLK_BASE + HDR || p >= HEAP_END) return 0;
    return rl(p-12);
}
unsigned mm_freemem(void){ return HEAP_END - g_bump; }
