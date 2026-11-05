/* SDL2 host for macOS and Linux: the 1-bit Mac screen scaled into a resizable
 * window (whole-pixel scaling), SDL input turned into Mac events and keycodes, the
 * game's cursors and icon, audio through an SDL queue, and native file/folder
 * dialogs (osascript on macOS, zenity or kdialog on Linux).
 *
 * Keyboard: Command = Cmd on macOS, Ctrl on Linux; Option = Alt/Option.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "mac.h"
#include "audio.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static int g_w, g_h;
static int g_mh, g_mv, g_down;
#define EQ 256
static HostEvent g_eq[EQ]; static int g_eh, g_et;
static SDL_Cursor *g_cur; static int g_cursor_vis = 1;
static int g_quitreq;
static SDL_AudioDeviceID g_audio; static int g_rate;

static void push(HostEvent *e){ int n = (g_et + 1) % EQ; if (n != g_eh){ g_eq[g_et] = *e; g_et = n; } }

static int mac_keycode(SDL_Scancode s){
    static const struct { SDL_Scancode s; int mk; } t[] = {
        {SDL_SCANCODE_A,0x00},{SDL_SCANCODE_S,0x01},{SDL_SCANCODE_D,0x02},{SDL_SCANCODE_F,0x03},{SDL_SCANCODE_H,0x04},
        {SDL_SCANCODE_G,0x05},{SDL_SCANCODE_Z,0x06},{SDL_SCANCODE_X,0x07},{SDL_SCANCODE_C,0x08},{SDL_SCANCODE_V,0x09},
        {SDL_SCANCODE_B,0x0B},{SDL_SCANCODE_Q,0x0C},{SDL_SCANCODE_W,0x0D},{SDL_SCANCODE_E,0x0E},{SDL_SCANCODE_R,0x0F},
        {SDL_SCANCODE_Y,0x10},{SDL_SCANCODE_T,0x11},{SDL_SCANCODE_1,0x12},{SDL_SCANCODE_2,0x13},{SDL_SCANCODE_3,0x14},
        {SDL_SCANCODE_4,0x15},{SDL_SCANCODE_6,0x16},{SDL_SCANCODE_5,0x17},{SDL_SCANCODE_EQUALS,0x18},{SDL_SCANCODE_9,0x19},
        {SDL_SCANCODE_7,0x1A},{SDL_SCANCODE_MINUS,0x1B},{SDL_SCANCODE_8,0x1C},{SDL_SCANCODE_0,0x1D},{SDL_SCANCODE_RIGHTBRACKET,0x1E},
        {SDL_SCANCODE_O,0x1F},{SDL_SCANCODE_U,0x20},{SDL_SCANCODE_LEFTBRACKET,0x21},{SDL_SCANCODE_I,0x22},{SDL_SCANCODE_P,0x23},
        {SDL_SCANCODE_RETURN,0x24},{SDL_SCANCODE_L,0x25},{SDL_SCANCODE_J,0x26},{SDL_SCANCODE_APOSTROPHE,0x27},{SDL_SCANCODE_K,0x28},
        {SDL_SCANCODE_SEMICOLON,0x29},{SDL_SCANCODE_BACKSLASH,0x2A},{SDL_SCANCODE_COMMA,0x2B},{SDL_SCANCODE_SLASH,0x2C},
        {SDL_SCANCODE_N,0x2D},{SDL_SCANCODE_M,0x2E},{SDL_SCANCODE_PERIOD,0x2F},{SDL_SCANCODE_TAB,0x30},{SDL_SCANCODE_SPACE,0x31},
        {SDL_SCANCODE_GRAVE,0x32},{SDL_SCANCODE_BACKSPACE,0x33},{SDL_SCANCODE_ESCAPE,0x35},
        {SDL_SCANCODE_KP_PERIOD,0x41},{SDL_SCANCODE_KP_MULTIPLY,0x43},{SDL_SCANCODE_KP_PLUS,0x45},{SDL_SCANCODE_NUMLOCKCLEAR,0x47},
        {SDL_SCANCODE_KP_DIVIDE,0x4B},{SDL_SCANCODE_KP_ENTER,0x4C},{SDL_SCANCODE_KP_MINUS,0x4E},{SDL_SCANCODE_KP_EQUALS,0x51},
        {SDL_SCANCODE_KP_0,0x52},{SDL_SCANCODE_KP_1,0x53},{SDL_SCANCODE_KP_2,0x54},{SDL_SCANCODE_KP_3,0x55},{SDL_SCANCODE_KP_4,0x56},
        {SDL_SCANCODE_KP_5,0x57},{SDL_SCANCODE_KP_6,0x58},{SDL_SCANCODE_KP_7,0x59},{SDL_SCANCODE_KP_8,0x5B},{SDL_SCANCODE_KP_9,0x5C},
        {SDL_SCANCODE_F1,0x7A},{SDL_SCANCODE_F2,0x78},{SDL_SCANCODE_F3,0x63},{SDL_SCANCODE_F4,0x76},{SDL_SCANCODE_F5,0x60},
        {SDL_SCANCODE_F6,0x61},{SDL_SCANCODE_F7,0x62},{SDL_SCANCODE_F8,0x64},{SDL_SCANCODE_F9,0x65},{SDL_SCANCODE_F10,0x6D},
        {SDL_SCANCODE_F11,0x67},{SDL_SCANCODE_F12,0x6F},
        {SDL_SCANCODE_LEFT,0x7B},{SDL_SCANCODE_RIGHT,0x7C},{SDL_SCANCODE_DOWN,0x7D},{SDL_SCANCODE_UP,0x7E},
        {SDL_SCANCODE_HOME,0x73},{SDL_SCANCODE_END,0x77},{SDL_SCANCODE_PAGEUP,0x74},{SDL_SCANCODE_PAGEDOWN,0x79},{SDL_SCANCODE_DELETE,0x75},
        {SDL_SCANCODE_LGUI,0x37},{SDL_SCANCODE_RGUI,0x37},{SDL_SCANCODE_LSHIFT,0x38},{SDL_SCANCODE_RSHIFT,0x38},
        {SDL_SCANCODE_CAPSLOCK,0x39},{SDL_SCANCODE_LALT,0x3A},{SDL_SCANCODE_RALT,0x3A},{SDL_SCANCODE_LCTRL,0x3B},{SDL_SCANCODE_RCTRL,0x3B},
    };
    int i;
    for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) if (t[i].s == s) return t[i].mk;
    return 0x7F;
}
#ifdef __APPLE__
#define CMD_MOD KMOD_GUI
#else
#define CMD_MOD KMOD_CTRL
#endif
int host_modifiers(void){
    SDL_Keymod k = SDL_GetModState(); int m = 0;
    if (k & CMD_MOD) m |= 0x100;
    if (k & KMOD_SHIFT) m |= 0x200;
    if (k & KMOD_CAPS) m |= 0x400;
    if (k & KMOD_ALT) m |= 0x800;
#ifdef __APPLE__
    if (k & KMOD_CTRL) m |= 0x1000;
#endif
    return m;
}
void host_keymap(unsigned char km[16]){
    int n, s; const Uint8 *st = SDL_GetKeyboardState(&n);
    memset(km, 0, 16);
    for (s = 0; s < n; s++) if (st[s]){ int k = mac_keycode((SDL_Scancode)s); if (k < 128) km[k >> 3] |= (unsigned char)(1 << (k & 7)); }
}

static void key_event(int type, SDL_Scancode sc, int ch){
    HostEvent e; memset(&e, 0, sizeof e);
    e.type = type; e.mac_key = mac_keycode(sc); e.ch = ch; e.mods = host_modifiers(); e.h = g_mh; e.v = g_mv;
    push(&e);
}
static int special_char(SDL_Keycode k){
    switch (k){
    case SDLK_LEFT: return 0x1C; case SDLK_RIGHT: return 0x1D; case SDLK_UP: return 0x1E; case SDLK_DOWN: return 0x1F;
    case SDLK_HOME: return 0x01; case SDLK_END: return 0x04; case SDLK_PAGEUP: return 0x0B; case SDLK_PAGEDOWN: return 0x0C;
    case SDLK_DELETE: return 0x7F; case SDLK_RETURN: case SDLK_KP_ENTER: return 13; case SDLK_BACKSPACE: return 8;
    case SDLK_TAB: return 9; case SDLK_ESCAPE: return 27;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0x10;
    return -1;
}

static SDL_Scancode g_last_sc; static int g_last_repeat;
int host_pump(void){
    SDL_Event ev;
    while (SDL_PollEvent(&ev)){
        HostEvent e; memset(&e, 0, sizeof e);
        switch (ev.type){
        case SDL_QUIT: g_quitreq = 1; e.type = HE_QUIT; push(&e); break;
        case SDL_MOUSEMOTION: g_mh = ev.motion.x; g_mv = ev.motion.y; break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            if (ev.button.button != SDL_BUTTON_LEFT) break;
            g_mh = ev.button.x; g_mv = ev.button.y; g_down = ev.type == SDL_MOUSEBUTTONDOWN;
            e.type = g_down ? HE_MOUSEDOWN : HE_MOUSEUP; e.h = g_mh; e.v = g_mv; e.mods = host_modifiers(); push(&e);
            break;
        case SDL_KEYDOWN: {
            SDL_Keycode k = ev.key.keysym.sym; int sc = special_char(k), type = ev.key.repeat ? HE_AUTOKEY : HE_KEYDOWN;
            g_last_sc = ev.key.keysym.scancode; g_last_repeat = ev.key.repeat;
            if (sc >= 0){ key_event(type, ev.key.keysym.scancode, sc); break; }
            if ((SDL_GetModState() & (CMD_MOD | KMOD_ALT)) && k > 0 && k < 128){     /* Command/Option + key: the plain character */
                int ch = (int)k; if ((SDL_GetModState() & KMOD_SHIFT) && ch >= 'a' && ch <= 'z') ch -= 32;
                key_event(type, ev.key.keysym.scancode, ch);
            }
            } break;
        case SDL_TEXTINPUT:
            if (SDL_GetModState() & (CMD_MOD | KMOD_ALT)) break;
            {   const unsigned char *t = (const unsigned char*)ev.text.text;
                for (; *t; t++){ int ch = *t; if (ch >= 128){ while (t[1] >= 0x80 && t[1] < 0xC0) t++; ch = '?'; }
                    key_event(g_last_repeat ? HE_AUTOKEY : HE_KEYDOWN, g_last_sc, ch); } }
            break;
        case SDL_KEYUP: key_event(HE_KEYUP, ev.key.keysym.scancode, 0); break;
        case SDL_WINDOWEVENT: if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) hle_dirty(); break;
        }
    }
    return !g_quitreq;
}
int host_next_event(HostEvent *e){ if (g_eh == g_et) return 0; *e = g_eq[g_eh]; g_eh = (g_eh + 1) % EQ; return 1; }
void host_mouse(int *h, int *v, int *down){ *h = g_mh; *v = g_mv; *down = g_down; }
void host_inject(int type, int h, int v, int ch, int mods){
    HostEvent e; memset(&e, 0, sizeof e);
    if (type == HE_MOUSEDOWN || type == HE_MOUSEUP || type == 0){ g_mh = h; g_mv = v; }
    if (type == HE_MOUSEDOWN) g_down = 1; else if (type == HE_MOUSEUP) g_down = 0;
    if (!type) return;
    e.type = type; e.h = g_mh; e.v = g_mv; e.ch = ch; e.mods = mods;
    push(&e);
}
void host_sleep_ms(int ms){ SDL_Delay((Uint32)ms); }
unsigned host_ms(void){ return SDL_GetTicks(); }
void host_beep(void){
    static unsigned char beep[2200]; int i;                   /* a short square wave, like SysBeep's */
    for (i = 0; i < (int)sizeof beep; i++) beep[i] = (unsigned char)(((i / 14) & 1) ? 0xA0 : 0x60);
    audio_push(beep, (int)sizeof beep);
}
void host_message(const char *title, const char *msg){ SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, g_win); }

void host_init(int w, int h, int scale, const char *title){
    g_w = w; g_h = h;
    if (SDL_Init(SDL_INIT_VIDEO)) fatal("Could not start SDL: %s", SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, h * scale,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (getenv(HLE_ENV("HIDDEN")) ? SDL_WINDOW_HIDDEN : 0));
    if (!g_win) fatal("Could not open a window: %s", SDL_GetError());
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(g_ren, w, h);
    SDL_RenderSetIntegerScale(g_ren, SDL_TRUE);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_StartTextInput();
}

void host_present(const unsigned char *bits, int rowbytes){
    void *pix; int pitch, x, y;
    if (!g_tex || SDL_LockTexture(g_tex, 0, &pix, &pitch)) return;
    for (y = 0; y < g_h; y++){
        Uint32 *row = (Uint32*)((unsigned char*)pix + (size_t)y * (size_t)pitch); const unsigned char *src = bits + (size_t)y * (size_t)rowbytes;
        for (x = 0; x < g_w; x++) row[x] = ((src[x >> 3] >> (7 - (x & 7))) & 1) ? 0xFF000000u : 0xFFFFFFFFu;
    }
    SDL_UnlockTexture(g_tex);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, 0, 0);
    SDL_RenderPresent(g_ren);
}

void host_cursor(const unsigned char *d, int visible){
    if (visible >= 0){ g_cursor_vis = visible; SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE); }
    if (d){
        /* SDL uses the Mac's own data/mask convention; double it to match the window scale */
        Uint8 data[128], mask[128]; int x, y; SDL_Cursor *c;
        memset(data, 0, sizeof data); memset(mask, 0, sizeof mask);
        for (y = 0; y < 32; y++) for (x = 0; x < 32; x++){
            int sx = x / 2, sy = y / 2;
            if ((d[sy*2 + (sx>>3)] >> (7 - (sx & 7))) & 1) data[y*4 + (x>>3)] |= (Uint8)(0x80 >> (x & 7));
            if ((d[32 + sy*2 + (sx>>3)] >> (7 - (sx & 7))) & 1) mask[y*4 + (x>>3)] |= (Uint8)(0x80 >> (x & 7));
        }
        c = SDL_CreateCursor(data, mask, 32, 32, ((short)(d[66] << 8 | d[67])) * 2, ((short)(d[64] << 8 | d[65])) * 2);
        if (c){ SDL_SetCursor(c); if (g_cur) SDL_FreeCursor(g_cur); g_cur = c; }
    }
}

void host_set_icon(const unsigned char *icl8, const unsigned char *icn){
    static const unsigned char ramp[10] = {0xEE,0xDD,0xBB,0xAA,0x88,0x77,0x55,0x44,0x22,0x11};
    Uint32 pal[256], px[32*32]; int i, x, y; SDL_Surface *s;
    if (!icn || !g_win) return;
    for (i = 0; i < 215; i++){ Uint32 r = 0xFF - (i/36)*0x33, g = 0xFF - ((i/6)%6)*0x33, b = 0xFF - (i%6)*0x33; pal[i] = r<<16 | g<<8 | b; }
    for (i = 0; i < 10; i++){ pal[215+i] = (Uint32)ramp[i] << 16; pal[225+i] = (Uint32)ramp[i] << 8; pal[235+i] = ramp[i]; pal[245+i] = (Uint32)ramp[i] * 0x010101u; }
    pal[255] = 0;
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++){
        int m = (icn[128 + y*4 + (x>>3)] >> (7 - (x & 7))) & 1;
        Uint32 c = icl8 ? pal[icl8[y*32 + x]] : ((((icn[y*4 + (x>>3)] >> (7 - (x & 7))) & 1)) ? 0 : 0xFFFFFF);
        px[y*32 + x] = m ? (c | 0xFF000000u) : 0;
    }
    s = SDL_CreateRGBSurfaceFrom(px, 32, 32, 32, 32*4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (s){ SDL_SetWindowIcon(g_win, s); SDL_FreeSurface(s); }
}

/* ---- dialogs through the desktop's own tools ------------------------------------------- */
static int run_capture(const char *cmd, char *out, int outlen){
    FILE *p = popen(cmd, "r"); int n;
    if (!p) return 0;
    n = (int)fread(out, 1, (size_t)outlen - 1, p);
    pclose(p);
    if (n <= 0) return 0;
    out[n] = 0; out[strcspn(out, "\r\n")] = 0;
    return out[0] != 0;
}
static void shell_quote(char *dst, size_t n, const char *s){         /* '...' quoting for sh */
    size_t o = 0; if (n < 3) return; dst[o++] = '\'';
    for (; *s && o + 5 < n; s++){ if (*s == '\''){ memcpy(dst + o, "'\\''", 4); o += 4; } else dst[o++] = *s; }
    dst[o++] = '\''; dst[o] = 0;
}
static void as_quote(char *dst, size_t n, const char *s){             /* "..." for AppleScript */
    size_t o = 0; dst[o++] = '"';
    for (; *s && o + 3 < n; s++){ if (*s == '"' || *s == '\\') dst[o++] = '\\'; dst[o++] = *s; }
    dst[o++] = '"'; dst[o] = 0;
}
static int have(const char *tool){ char c[256]; snprintf(c, sizeof c, "command -v %s >/dev/null 2>&1", tool); return system(c) == 0; }

int host_file_dialog(int save, const char *dir, const char *initial, char *out, int outlen, const char *title){
    char cmd[4096], qd[1200], qi[600], qt[600], path[1200];
    g_down = 0;
#ifdef __APPLE__
    char sq[1300];
    as_quote(qd, sizeof qd, dir); as_quote(qi, sizeof qi, initial && *initial ? initial : "Untitled"); as_quote(qt, sizeof qt, title && *title ? title : (save ? "Save as:" : "Open:"));
    if (save) snprintf(path, sizeof path, "POSIX path of (choose file name with prompt %s default name %s default location (POSIX file %s))", qt, qi, qd);
    else snprintf(path, sizeof path, "POSIX path of (choose file with prompt %s default location (POSIX file %s))", qt, qd);
    shell_quote(sq, sizeof sq, path);
    snprintf(cmd, sizeof cmd, "osascript -e %s 2>/dev/null", sq);
#else
    shell_quote(qt, sizeof qt, title && *title ? title : (save ? "Save as" : "Open"));
    snprintf(path, sizeof path, "%s/%s", dir, initial && *initial ? initial : "");
    shell_quote(qd, sizeof qd, path);
    if (have("zenity")) snprintf(cmd, sizeof cmd, "zenity --file-selection %s --title=%s --filename=%s 2>/dev/null", save ? "--save --confirm-overwrite" : "", qt, qd);
    else if (have("kdialog")) snprintf(cmd, sizeof cmd, "kdialog %s %s --title %s 2>/dev/null", save ? "--getsavefilename" : "--getopenfilename", qd, qt);
    else { host_message(HLE_APP_NAME, "Install zenity or kdialog to open and save schedules."); return 0; }
    (void)qi;
#endif
    return run_capture(cmd, out, outlen);
}

int host_pick_folder(char *out, int outlen){
    char cmd[1024];
#ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "osascript -e 'POSIX path of (choose folder with prompt \"Folder with the " HLE_APP_NAME " game files\")' 2>/dev/null");
#else
    if (have("zenity")) snprintf(cmd, sizeof cmd, "zenity --file-selection --directory --title='Folder with the " HLE_APP_NAME " game files' 2>/dev/null");
    else if (have("kdialog")) snprintf(cmd, sizeof cmd, "kdialog --getexistingdirectory ~ --title 'Folder with the " HLE_APP_NAME " game files' 2>/dev/null");
    else {
        host_message(HLE_APP_NAME, "Pass the game folder on the command line:\n\n    <program> <folder>\n\nor copy the files to ~/.local/share/" HLE_APP_SLUG "/game");
        return 0;
    }
#endif
    if (!run_capture(cmd, out, outlen)) return 0;
    {   size_t n = strlen(out); if (n > 1 && out[n-1] == '/') out[n-1] = 0; }
    return 1;
}

/* ---- audio: 8-bit unsigned mono through an SDL queue ------------------------------------ */
int audio_init(int rate){
    SDL_AudioSpec want, have_;
    if (getenv(HLE_ENV("NOSOUND"))) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    SDL_zero(want);
    want.freq = rate; want.format = AUDIO_U8; want.channels = 1; want.samples = 512;
    g_audio = SDL_OpenAudioDevice(0, 0, &want, &have_, 0);
    if (!g_audio) return 0;
    g_rate = rate;
    SDL_PauseAudioDevice(g_audio, 0);
    return 1;
}
void audio_push(const unsigned char *s, int n){
    if (!g_audio) return;
    if (SDL_GetQueuedAudioSize(g_audio) > (Uint32)(g_rate / 2)) SDL_ClearQueuedAudio(g_audio);   /* cap latency */
    SDL_QueueAudio(g_audio, s, (Uint32)n);
}
void audio_service(void){ }
void audio_shutdown(void){ if (g_audio){ SDL_CloseAudioDevice(g_audio); g_audio = 0; } }
