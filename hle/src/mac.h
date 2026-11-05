/* Shared declarations for the generic Mac Toolbox HLE.
 *
 * The game's own 68k code runs on Musashi; every A-line trap it executes lands in
 * trap.c, which implements the Mac Toolbox/OS call in C on top of the emulated
 * 16 MB address space below. Toolbox data structures (GrafPorts, windows, menus,
 * regions, handles) live in emulated memory in their real Mac layouts, because the
 * game reads and writes their fields directly.
 */
#ifndef HLE_MAC_H
#define HLE_MAC_H
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- emulated memory ---------------------------------------------------------- */
#define MEM_SIZE    0x01000000u
extern unsigned char g_ram[MEM_SIZE];
#define MA(a) ((a) & (MEM_SIZE - 1))      /* 24-bit addressing, as on a classic Mac */
static __inline unsigned rb(unsigned a){ return g_ram[MA(a)]; }
static __inline unsigned rw(unsigned a){ a = MA(a); return a < MEM_SIZE-1 ? (unsigned)(g_ram[a]<<8 | g_ram[a+1]) : (rb(a)<<8 | rb(a+1)); }
static __inline unsigned rl(unsigned a){ return rw(a)<<16 | rw(a+2); }
static __inline void wb(unsigned a, unsigned v){ g_ram[MA(a)] = (unsigned char)v; }
static __inline void ww(unsigned a, unsigned v){ wb(a, v>>8); wb(a+1, v); }
static __inline void wl(unsigned a, unsigned v){ ww(a, v>>16); ww(a+2, v); }
static __inline short sw(unsigned a){ return (short)rw(a); }

/* memory map */
#define HEAP_BASE   0x00100000u   /* application heap: handles + pointers         */
#define HEAP_END    0x00D00000u
#define STACK_TOP   0x00E00000u   /* stack grows down from here                    */
#define A5_BASE     0x00E80000u   /* app globals below, jump table above           */
#define SCREEN_BASE 0x00F00000u   /* 1-bit main screen                             */
#define THUNK_BASE  0x00FE0000u   /* GetTrapAddress results: [A-line][RTS] per trap */
#define MAGIC_ALINE 0x00FFF000u   /* A-line exception vector target                */
#define MAGIC_RET   0x00FFF010u   /* return address for HLE -> 68k callbacks       */

/* low-memory globals we maintain */
#define LM_Ticks     0x16A
#define LM_KeyMap    0x174
#define LM_Time      0x20C
#define LM_RndSeed   0x156
#define LM_ScrnBase  0x824
#define LM_MBState   0x172
#define LM_Mouse     0x830
#define LM_CurrentA5 0x904
#define LM_CurStackBase 0x908
#define LM_CurApName 0x910
#define LM_MemErr    0x220
#define LM_ResErr    0xA60
#define LM_CurMap    0xA5A
#define LM_TopMapHndl 0xA50
#define LM_SysMap    0xA58
#define LM_ApplZone  0x2AA
#define LM_ApplLimit 0x130
#define LM_HeapEnd   0x114
#define LM_MemTop    0x108
#define LM_BufPtr    0x10C
#define LM_ROM85     0x28E
#define LM_ScreenRow 0x106
#define LM_WindowList 0x9D6
#define LM_WMgrPort  0x9DE
#define LM_GrayRgn   0x9EE
#define LM_MBarHeight 0xBAA
#define LM_TheMenu   0xA26
#define LM_MenuList  0xA1C
#define LM_DoubleTime 0x2F0
#define LM_CaretTime 0x2F4
#define LM_SysEvtMask 0x144
#define LM_DlgFont   0xAFA
#define LM_ApFontID  0x984
#define LM_SFSaveDisk 0x214
#define LM_CurDirStore 0x398
#define LM_PrintErr  0x944
#define LM_ResLoad   0xA5E
#define LM_DAStrings 0xAA0     /* ParamText ^0..^3 handles */

/* ---- geometry --------------------------------------------------------------------- */
typedef struct { short top, left, bottom, right; } Rect;
static __inline Rect rect_at(unsigned a){ Rect r; r.top=sw(a); r.left=sw(a+2); r.bottom=sw(a+4); r.right=sw(a+6); return r; }
static __inline void rect_put(unsigned a, Rect r){ ww(a,r.top); ww(a+2,r.left); ww(a+4,r.bottom); ww(a+6,r.right); }
static __inline Rect mkrect(int t,int l,int b,int r){ Rect x; x.top=(short)t; x.left=(short)l; x.bottom=(short)b; x.right=(short)r; return x; }
static __inline int rect_empty(Rect r){ return r.bottom<=r.top || r.right<=r.left; }
static __inline Rect rect_sect(Rect a, Rect b){ Rect r;
    r.top = a.top>b.top?a.top:b.top; r.left = a.left>b.left?a.left:b.left;
    r.bottom = a.bottom<b.bottom?a.bottom:b.bottom; r.right = a.right<b.right?a.right:b.right;
    if (rect_empty(r)) r.top=r.left=r.bottom=r.right=0; return r; }
static __inline Rect rect_off(Rect r, int dh, int dv){ r.top+=dv; r.bottom+=dv; r.left+=dh; r.right+=dh; return r; }
static __inline int pt_in(Rect r, int h, int v){ return h>=r.left && h<r.right && v>=r.top && v<r.bottom; }

/* ---- CPU / traps (trap.c) --------------------------------------------------------- */
#include "../musashi/m68k.h"
static __inline unsigned reg(int r){ return m68k_get_reg(NULL, (m68k_register_t)r); }
static __inline void setreg(int r, unsigned v){ m68k_set_reg((m68k_register_t)r, v); }
unsigned call68k(unsigned addr, int resbytes, const char *sig, ...);  /* Pascal call; sig: L=long W=word B=byte */
void     trap_init(void);
extern int g_quit;
extern int g_trace;
void     fatal(const char *fmt, ...);
void     logf_(const char *fmt, ...);
unsigned app_a5(void);

/* ---- memory manager (mem.c) -------------------------------------------------------- */
unsigned mm_newptr(unsigned size, int clear);
unsigned mm_newhandle(unsigned size, int clear);
void     mm_disposptr(unsigned p);
void     mm_disposhandle(unsigned h);
unsigned mm_handlesize(unsigned h);
int      mm_sethandlesize(unsigned h, unsigned size);
unsigned mm_ptrsize(unsigned p);
unsigned mm_recover(unsigned p);
unsigned mm_freemem(void);
unsigned mm_newhandle_from(const void *data, unsigned size);
void     mm_init(void);
int      mm_is_handle(unsigned h);

/* ---- resources (rsrc.c) ------------------------------------------------------------- */
int      rs_open_app(const char *path, const char *overlay);
int      rs_open_system(const char *path);
unsigned rs_get(unsigned type, int id);            /* searches the chain from CurMap */
unsigned rs_get1(unsigned type, int id);           /* current file only             */
unsigned rs_get_named(unsigned type, const unsigned char *pname);
const unsigned char *rs_sysdata(unsigned type, int id, unsigned *len);   /* System file, host copy */
void     rs_trap(unsigned word, void *t);
int      rs_handle_info(unsigned h, unsigned *type, int *id, unsigned char *name);
void     rs_flush_all(void);
int      rs_file_has(const char *path, unsigned type);

/* ---- QuickDraw (qd.c, rgn.c, font.c) ----------------------------------------------- */
extern int g_scr_w, g_scr_h, g_scr_rb;
unsigned qd_port(void);                 /* thePort */
void     qd_setport(unsigned p);
unsigned qd_globals(void);              /* address of thePort in the QD globals */
void     qd_init_port(unsigned port);
void     qd_trap(unsigned word, void *t);
void     qd_fill_rect(unsigned port, Rect r, const unsigned char pat[8], int mode);
void     qd_frame_rect(unsigned port, Rect r);
void     qd_line(unsigned port, int h0, int v0, int h1, int v1);
void     qd_draw_text(unsigned port, const unsigned char *s, int n);
int      qd_text_width(unsigned port, const unsigned char *s, int n);
void     qd_font_info(unsigned port, int *ascent, int *descent, int *widMax, int *leading);
void     qd_draw_picture(unsigned port, unsigned pich, Rect dst);
void     qd_copybits(unsigned srcBits, unsigned dstBits, Rect sr, Rect dr, int mode, unsigned maskRgn);
void     qd_frame_round(unsigned port, Rect r, int ow, int oh, int fill, const unsigned char *pat, int mode);
void     qd_oval(unsigned port, Rect r, int fill, const unsigned char *pat, int mode);
void     qd_plot_icon(unsigned port, Rect r, unsigned data);
void     qd_draw_screen_dirty(void);
extern const unsigned char PAT_BLACK[8], PAT_WHITE[8], PAT_GRAY[8], PAT_LTGRAY[8], PAT_DKGRAY[8];
/* port field offsets (GrafPort) */
#define GP_portBits  2
#define GP_portRect 16
#define GP_visRgn   24
#define GP_clipRgn  28
#define GP_bkPat    32
#define GP_fillPat  40
#define GP_pnLoc    48
#define GP_pnSize   52
#define GP_pnMode   56
#define GP_pnPat    58
#define GP_pnVis    66
#define GP_txFont   68
#define GP_txFace   70
#define GP_txMode   72
#define GP_txSize   74
#define GP_spExtra  76
#define GP_fgColor  80
#define GP_bkColor  84
#define GP_picSave  92
#define GP_rgnSave  96
#define GP_polySave 100
#define GP_grafProcs 104
#define GP_SIZE    108

/* regions (rgn.c): handle -> [size.w][bbox][mask rows...]; size 10 = rectangle */
unsigned rgn_new(void);
void     rgn_set_rect(unsigned h, Rect r);
Rect     rgn_bbox(unsigned h);
int      rgn_is_rect(unsigned h);
int      rgn_contains(unsigned h, int x, int y);
void     rgn_op(unsigned dst, unsigned a, unsigned b, int op);   /* 0 sect 1 union 2 diff 3 xor */
void     rgn_copy(unsigned src, unsigned dst);
void     rgn_offset(unsigned h, int dh, int dv);
void     rgn_inset(unsigned h, int dh, int dv);
int      rgn_empty(unsigned h);
void     rgn_record_begin(void);
int      rgn_recording(void);
void     rgn_record_line(int h0, int v0, int h1, int v1);
void     rgn_record_rect(Rect r);
void     rgn_record_oval(Rect r);
void     rgn_record_round(Rect r, int ow, int oh);
void     rgn_record_end(unsigned h);
void     rgn_from_oval(unsigned h, Rect r);
void     rgn_from_round(unsigned h, Rect r, int ow, int oh);
/* host-side mask views used by the rasterizer */
typedef struct { Rect bbox; int rect; unsigned data; int rb; } RgnView;
RgnView  rgn_view(unsigned h);
static __inline int rv_in(const RgnView *v, int x, int y){
    if (!pt_in(v->bbox, x, y)) return 0; if (v->rect) return 1;
    { int dx = x - v->bbox.left, dy = y - v->bbox.top;
      return (g_ram[MA(v->data + (unsigned)dy*v->rb + (unsigned)(dx>>3))] >> (7-(dx&7))) & 1; } }

/* fonts (font.c) */
typedef struct { int ascent, descent, widMax, leading, height; } FontMetrics;
int      font_metrics(int font, int size, int face, FontMetrics *m);
int      font_char_width(int font, int size, int face, int ch);
/* draws one char with its origin at (h,v); calls plot(x,y) for each set pixel */
int      font_draw_char(int font, int size, int face, int ch, int h, int v, void (*plot)(int x, int y, void *ctx), void *ctx);

/* ---- windows / menus / dialogs / controls / events (wm.c, menu.c, dlg.c, ctl.c, evt.c) ---- */
void     wm_trap(unsigned word, void *t);
void     wm_init(void);
void     wm_compose(void);                /* redraw structure/visRgns after a change */
unsigned wm_front(void);
unsigned wm_find(int h, int v, int *part);
void     wm_select(unsigned w);
void     wm_draw_frame(unsigned w);
void     wm_invalidate(unsigned w, Rect r_local);
int      wm_update_pending(unsigned *w);
void     wm_global_origin(unsigned port, int *gh, int *gv);   /* local (0,0) of a port in screen coords */
void     wm_dialog_draw(unsigned d);
unsigned wm_new_window(unsigned storage, Rect r, const unsigned char *title, int visible, int procID,
                       unsigned behind, int goAway, unsigned refCon, int dialog);
void     wm_dispose(unsigned w, int free_storage);
void     wm_show(unsigned w, int show);
void     wm_activate_events(unsigned oldfront, unsigned newfront);
void     wm_begin_update(unsigned w);
void     wm_end_update(unsigned w);
void     wm_validate_all(unsigned w);
int      wm_has_update(unsigned w);
unsigned wm_first(void);
void     menu_trap(unsigned word, void *t);
void     menu_draw_bar(void);
int      menu_select(int h, int v);
int      menu_key(int ch);
void     dlg_trap(unsigned word, void *t);
int      dlg_is_dialog(unsigned w);
void     dlg_draw(unsigned d);
void     ctl_trap(unsigned word, void *t);
void     ctl_draw_all(unsigned w);
void     ctl_draw(unsigned c);
unsigned ctl_find(unsigned w, int h, int v, int *part);
int      ctl_track(unsigned c, int h, int v, unsigned action);
unsigned ctl_new(unsigned w, Rect r, const unsigned char *title, int vis, int val, int min, int max, int proc, unsigned refCon);
void     ctl_dispose(unsigned c);
void     evt_trap(unsigned word, void *t);
void     evt_post(int what, unsigned msg, int h, int v, int mods);
int      evt_get(int mask, unsigned evrec, int remove);
int      evt_mouse_down(void);
void     evt_mouse(int *h, int *v);
int      evt_wait_click_release(void);

/* ---- files, sound, misc ------------------------------------------------------------- */
void     fs_os_trap(unsigned word);
void     fs_init(const char *save_dir, const char *app_dir);
const char *fs_save_dir(void);
void     snd_play_handle(unsigned h);
void     snd_driver_write(unsigned pb);
void     snd_stop(void);
void     pack_trap(unsigned word, void *t);

/* ---- host (host_win.c) -------------------------------------------------------------- */
enum { HE_NONE, HE_MOUSEDOWN, HE_MOUSEUP, HE_KEYDOWN, HE_KEYUP, HE_AUTOKEY, HE_QUIT };
typedef struct { int type, mac_key, ch, mods, h, v; } HostEvent;
void     host_init(int w, int h, int scale, const char *title);
void     host_present(const unsigned char *bits1, int rowbytes);
int      host_pump(void);                       /* 0 = window closed */
int      host_next_event(HostEvent *e);
void     host_mouse(int *h, int *v, int *down);
void     host_keymap(unsigned char km[16]);
int      host_modifiers(void);
void     host_sleep_ms(int ms);
unsigned host_ms(void);
void     host_beep(void);
void     host_cursor(const unsigned char *data68 /* NULL = arrow */, int visible);
int      host_file_dialog(int save, const char *dir, const char *initial, char *out, int outlen, const char *title);
void     host_message(const char *title, const char *msg);
int      host_pick_folder(char *out, int outlen);
void     host_set_icon(const unsigned char *icl8, const unsigned char *icn);
void     host_inject(int type, int h, int v, int ch, int mods);

/* the idle/present pump the emulator calls between slices and inside blocking traps */
void     hle_idle(int sleep_ms);
void     hle_dirty(void);
extern int g_ticks_base;

/* Pascal string helpers */
static __inline void pstr_get(unsigned a, char *out, int max){ int n = a ? (int)rb(a) : 0, i; if (n > max-1) n = max-1;
    for (i=0;i<n;i++) out[i] = (char)rb(a+1+(unsigned)i); out[n] = 0; }
static __inline void pstr_put(unsigned a, const char *s){ int n=(int)strlen(s), i; if (n>255) n=255; wb(a,(unsigned)n);
    for (i=0;i<n;i++) wb(a+1+(unsigned)i,(unsigned char)s[i]); }

/* trap call frame used by the Toolbox (Pascal) handlers */
typedef struct { unsigned word, pc, sp; int pop; int handled; } Trap;
#define ARGW(o)  ((short)rw(t->sp+(o)))
#define ARGUW(o) rw(t->sp+(o))
#define ARGL(o)  rl(t->sp+(o))
#define ARGB(o)  rb(t->sp+(o))
#define POP(n)   (t->pop=(n))
#define RETW(n,v) do{ ww(t->sp+(n),(unsigned)(v)); t->pop=(n); }while(0)
#define RETL(n,v) do{ wl(t->sp+(n),(unsigned)(v)); t->pop=(n); }while(0)
#define RETB(n,v) do{ ww(t->sp+(n),(v)?0x0100u:0u); t->pop=(n); }while(0)
#define TRAP_DONE (t->handled=1)

#endif
