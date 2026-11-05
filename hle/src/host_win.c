/* Windows host: a window showing the 1-bit Mac screen scaled up (integer scale,
 * letterboxed), Windows mouse/keyboard turned into Mac events and keycodes, the
 * game's cursors as real Windows cursors, and the file dialogs.
 *
 * Keyboard: Ctrl = Command, Alt = Option.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include "mac.h"

static HWND g_hwnd;
static int g_w, g_h, g_scale = 2;
static unsigned char *g_fb;                    /* last presented 1-bit frame */
static int g_fb_rb;
static struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; } g_bmi;
static int g_mh, g_mv, g_down;
static unsigned char g_held[256];
#define EQ 256
static HostEvent g_eq[EQ]; static int g_eh, g_et;
static HCURSOR g_cursor, g_arrow; static int g_cursor_vis = 1;
static int g_quitreq;

static void push(HostEvent *e){ int n = (g_et + 1) % EQ; if (n != g_eh){ g_eq[g_et] = *e; g_et = n; } }

static int mac_keycode(int vk, int ext){
    static const struct { int vk, mk; } t[] = {
        {'A',0x00},{'S',0x01},{'D',0x02},{'F',0x03},{'H',0x04},{'G',0x05},{'Z',0x06},{'X',0x07},{'C',0x08},{'V',0x09},
        {'B',0x0B},{'Q',0x0C},{'W',0x0D},{'E',0x0E},{'R',0x0F},{'Y',0x10},{'T',0x11},{'1',0x12},{'2',0x13},{'3',0x14},
        {'4',0x15},{'6',0x16},{'5',0x17},{VK_OEM_PLUS,0x18},{'9',0x19},{'7',0x1A},{VK_OEM_MINUS,0x1B},{'8',0x1C},{'0',0x1D},
        {VK_OEM_6,0x1E},{'O',0x1F},{'U',0x20},{VK_OEM_4,0x21},{'I',0x22},{'P',0x23},{VK_RETURN,0x24},{'L',0x25},{'J',0x26},
        {VK_OEM_7,0x27},{'K',0x28},{VK_OEM_1,0x29},{VK_OEM_5,0x2A},{VK_OEM_COMMA,0x2B},{VK_OEM_2,0x2C},{'N',0x2D},{'M',0x2E},
        {VK_OEM_PERIOD,0x2F},{VK_TAB,0x30},{VK_SPACE,0x31},{VK_OEM_3,0x32},{VK_BACK,0x33},{VK_ESCAPE,0x35},
        {VK_CONTROL,0x37},{VK_SHIFT,0x38},{VK_CAPITAL,0x39},{VK_MENU,0x3A},
        {VK_DECIMAL,0x41},{VK_MULTIPLY,0x43},{VK_ADD,0x45},{VK_CLEAR,0x47},{VK_DIVIDE,0x4B},{VK_SUBTRACT,0x4E},
        {VK_NUMPAD0,0x52},{VK_NUMPAD1,0x53},{VK_NUMPAD2,0x54},{VK_NUMPAD3,0x55},{VK_NUMPAD4,0x56},{VK_NUMPAD5,0x57},
        {VK_NUMPAD6,0x58},{VK_NUMPAD7,0x59},{VK_NUMPAD8,0x5B},{VK_NUMPAD9,0x5C},
        {VK_F1,0x7A},{VK_F2,0x78},{VK_F3,0x63},{VK_F4,0x76},{VK_F5,0x60},{VK_F6,0x61},{VK_F7,0x62},{VK_F8,0x64},
        {VK_F9,0x65},{VK_F10,0x6D},{VK_F11,0x67},{VK_F12,0x6F},
        {VK_LEFT,0x7B},{VK_RIGHT,0x7C},{VK_DOWN,0x7D},{VK_UP,0x7E},{VK_HOME,0x73},{VK_END,0x77},{VK_PRIOR,0x74},{VK_NEXT,0x79},{VK_DELETE,0x75},
    };
    int i;
    if (vk == VK_RETURN && ext) return 0x4C;                    /* keypad Enter */
    for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) if (t[i].vk == vk) return t[i].mk;
    return 0x7F;
}
int host_modifiers(void){
    int m = 0;
    if (g_held[VK_CONTROL]) m |= 0x100;
    if (g_held[VK_SHIFT]) m |= 0x200;
    if (GetKeyState(VK_CAPITAL) & 1) m |= 0x400;
    if (g_held[VK_MENU]) m |= 0x800;
    return m;
}
void host_keymap(unsigned char km[16]){
    int vk; memset(km, 0, 16);
    for (vk = 1; vk < 256; vk++) if (g_held[vk]){ int k = mac_keycode(vk, 0); if (k < 128) km[k >> 3] |= (unsigned char)(1 << (k & 7)); }
}

static void client_to_mac(int x, int y, int *h, int *v){
    RECT rc; int s, ox, oy; GetClientRect(g_hwnd, &rc);
    s = g_scale; ox = (rc.right - g_w * s) / 2; oy = (rc.bottom - g_h * s) / 2;
    *h = (x - ox) / s; *v = (y - oy) / s;
    if (*h < 0) *h = 0; if (*v < 0) *v = 0; if (*h >= g_w) *h = g_w - 1; if (*v >= g_h) *v = g_h - 1;
}
static void fit_scale(void){
    RECT rc; int s; GetClientRect(g_hwnd, &rc);
    s = rc.right / g_w; if (rc.bottom / g_h < s) s = rc.bottom / g_h; if (s < 1) s = 1;
    g_scale = s;
}

static void key_event(int type, int vk, int ext, int ch){
    HostEvent e; memset(&e, 0, sizeof e);
    e.type = type; e.mac_key = mac_keycode(vk, ext); e.ch = ch; e.mods = host_modifiers(); e.h = g_mh; e.v = g_mv;
    push(&e);
}
/* characters for keys that produce none through WM_CHAR */
static int special_char(int vk){
    switch (vk){
    case VK_LEFT: return 0x1C; case VK_RIGHT: return 0x1D; case VK_UP: return 0x1E; case VK_DOWN: return 0x1F;
    case VK_HOME: return 0x01; case VK_END: return 0x04; case VK_PRIOR: return 0x0B; case VK_NEXT: return 0x0C;
    case VK_DELETE: return 0x7F; case VK_CLEAR: return 0x1B;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return 0x10;
    return -1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l){
    static int last_vk, last_ext, last_repeat;
    switch (m){
    case WM_CLOSE: g_quitreq = 1; { HostEvent e; memset(&e, 0, sizeof e); e.type = HE_QUIT; push(&e); } return 0;
    case WM_SIZE: if (g_w) fit_scale(); InvalidateRect(h, 0, TRUE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; int ox, oy;
        GetClientRect(h, &rc); ox = (rc.right - g_w * g_scale) / 2; oy = (rc.bottom - g_h * g_scale) / 2;
        if (g_fb){
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, ox, oy, g_w * g_scale, g_h * g_scale, 0, 0, g_w, g_h, g_fb, (BITMAPINFO*)&g_bmi, DIB_RGB_COLORS, SRCCOPY);
        }
        {   HBRUSH b = (HBRUSH)GetStockObject(BLACK_BRUSH); RECT r;
            r = rc; r.right = ox; FillRect(dc, &r, b); r = rc; r.left = ox + g_w * g_scale; FillRect(dc, &r, b);
            r = rc; r.bottom = oy; FillRect(dc, &r, b); r = rc; r.top = oy + g_h * g_scale; FillRect(dc, &r, b); }
        EndPaint(h, &ps); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT){ SetCursor(g_cursor_vis ? (g_cursor ? g_cursor : g_arrow) : NULL); return TRUE; }
        break;
    case WM_MOUSEMOVE: client_to_mac((short)LOWORD(l), (short)HIWORD(l), &g_mh, &g_mv); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        HostEvent e; memset(&e, 0, sizeof e); SetCapture(h);
        client_to_mac((short)LOWORD(l), (short)HIWORD(l), &g_mh, &g_mv); g_down = 1;
        e.type = HE_MOUSEDOWN; e.h = g_mh; e.v = g_mv; e.mods = host_modifiers(); push(&e); } return 0;
    case WM_LBUTTONUP: {
        HostEvent e; memset(&e, 0, sizeof e); ReleaseCapture();
        client_to_mac((short)LOWORD(l), (short)HIWORD(l), &g_mh, &g_mv); g_down = 0;
        e.type = HE_MOUSEUP; e.h = g_mh; e.v = g_mv; e.mods = host_modifiers(); push(&e); } return 0;
    case WM_SYSKEYDOWN: case WM_KEYDOWN: {
        int vk = (int)w, ext = (l >> 24) & 1, repeat = (l >> 30) & 1, sc;
        if (vk == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) break;              /* Alt+F4 closes */
        if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
        g_held[vk & 0xFF] = 1;
        last_vk = vk; last_ext = ext; last_repeat = repeat;
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_CAPITAL) return 0;
        sc = special_char(vk);
        if (sc >= 0){ key_event(repeat ? HE_AUTOKEY : HE_KEYDOWN, vk, ext, sc); return 0; }
        if (g_held[VK_CONTROL] || g_held[VK_MENU]){                          /* Command/Option + key: the plain character */
            BYTE ks[256]; WORD out = 0; memset(ks, 0, sizeof ks);
            if (g_held[VK_SHIFT]) ks[VK_SHIFT] = 0x80;
            if (ToAscii((UINT)vk, (UINT)((l >> 16) & 0xFF), ks, &out, 0) == 1){ key_event(repeat ? HE_AUTOKEY : HE_KEYDOWN, vk, ext, out & 0xFF); return 0; }
            if (vk == VK_RETURN){ key_event(HE_KEYDOWN, vk, ext, 13); return 0; }
        }
        if (m == WM_SYSKEYDOWN) return 0;
        } break;
    case WM_CHAR: case WM_SYSCHAR:
        if (g_held[VK_CONTROL] || g_held[VK_MENU]) return 0;
        {   int ch = (int)w; if (ch == '\r' || ch == 8 || ch == 9 || ch == 27 || ch >= 32){
                if (ch >= 128) ch = '?';
                key_event(last_repeat ? HE_AUTOKEY : HE_KEYDOWN, last_vk, last_ext, ch); } }
        return 0;
    case WM_SYSKEYUP: case WM_KEYUP: {
        int vk = (int)w; if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
        g_held[vk & 0xFF] = 0;
        if (vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU) key_event(HE_KEYUP, vk, (l >> 24) & 1, 0);
        } return 0;
    case WM_KILLFOCUS: memset(g_held, 0, sizeof g_held); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

void host_init(int w, int h, int scale, const char *title){
    WNDCLASSA wc; RECT r; DWORD style = WS_OVERLAPPEDWINDOW;
    g_w = w; g_h = h; g_scale = scale;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "MacHLEWindow";
    wc.hCursor = LoadCursor(0, IDC_ARROW); wc.style = CS_DBLCLKS;
    wc.hIcon = LoadIconA(GetModuleHandleA(0), MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    g_arrow = LoadCursor(0, IDC_ARROW);
    r.left = 0; r.top = 0; r.right = w * scale; r.bottom = h * scale;
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA("MacHLEWindow", title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    memset(&g_bmi, 0, sizeof g_bmi);
    g_bmi.h.biSize = sizeof(BITMAPINFOHEADER); g_bmi.h.biWidth = w; g_bmi.h.biHeight = -h;
    g_bmi.h.biPlanes = 1; g_bmi.h.biBitCount = 1; g_bmi.h.biCompression = BI_RGB; g_bmi.h.biClrUsed = 2;
    g_bmi.pal[0].rgbRed = g_bmi.pal[0].rgbGreen = g_bmi.pal[0].rgbBlue = 255;
    if (!getenv(HLE_ENV("HIDDEN"))) ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
}

void host_present(const unsigned char *bits, int rowbytes){
    int y, drb = ((g_w + 31) / 32) * 4;
    if (!g_fb){ g_fb = (unsigned char*)malloc((size_t)drb * (size_t)g_h); g_fb_rb = drb; }
    for (y = 0; y < g_h; y++) memcpy(g_fb + (size_t)y * (size_t)drb, bits + (size_t)y * (size_t)rowbytes, (size_t)(rowbytes < drb ? rowbytes : drb));
    InvalidateRect(g_hwnd, 0, FALSE);
}
int host_pump(void){
    MSG msg;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)){ TranslateMessage(&msg); DispatchMessageA(&msg); }
    return !g_quitreq;
}
/* scripted input (tests): type 0 = just move the mouse */
void host_inject(int type, int h, int v, int ch, int mods){
    HostEvent e; memset(&e, 0, sizeof e);
    if (type == HE_MOUSEDOWN || type == HE_MOUSEUP || type == 0){ g_mh = h; g_mv = v; }
    if (type == HE_MOUSEDOWN) g_down = 1; else if (type == HE_MOUSEUP) g_down = 0;
    if (!type) return;
    e.type = type; e.h = g_mh; e.v = g_mv; e.ch = ch; e.mods = mods;
    if (type == HE_KEYDOWN){ e.mac_key = 0; if (mods & 0x100) g_held[VK_CONTROL] = 0; }
    push(&e);
}
int host_next_event(HostEvent *e){ if (g_eh == g_et) return 0; *e = g_eq[g_eh]; g_eh = (g_eh + 1) % EQ; return 1; }
void host_mouse(int *h, int *v, int *down){ *h = g_mh; *v = g_mv; *down = g_down; }
void host_sleep_ms(int ms){ Sleep((DWORD)ms); }
unsigned host_ms(void){ return (unsigned)GetTickCount(); }
void host_beep(void){ MessageBeep(MB_OK); }
void host_message(const char *title, const char *msg){ MessageBoxA(g_hwnd, msg, title, MB_OK | MB_ICONERROR); }

void host_cursor(const unsigned char *d, int visible){
    if (visible >= 0) g_cursor_vis = visible;
    if (d){
        /* Mac cursor: 16 rows data, 16 rows mask, hotspot (v,h); scaled to the window */
        int s = g_scale > 2 ? 2 : g_scale, cw = 32, x, y; unsigned char andm[32*4], xorm[32*4];
        memset(andm, 0xFF, sizeof andm); memset(xorm, 0, sizeof xorm);
        for (y = 0; y < 16 * s && y < 32; y++) for (x = 0; x < 16 * s && x < 32; x++){
            int sx = x / s, sy = y / s, dbit = (d[sy*2 + (sx>>3)] >> (7 - (sx & 7))) & 1, mbit = (d[32 + sy*2 + (sx>>3)] >> (7 - (sx & 7))) & 1;
            int a = !mbit, xo = mbit ? !dbit : dbit;
            if (!a) andm[y*4 + (x>>3)] &= (unsigned char)~(0x80 >> (x & 7));
            if (xo) xorm[y*4 + (x>>3)] |= (unsigned char)(0x80 >> (x & 7));
        }
        if (g_cursor) DestroyCursor(g_cursor);
        g_cursor = CreateCursor(GetModuleHandleA(0), ((short)(d[66] << 8 | d[67])) * s, ((short)(d[64] << 8 | d[65])) * s, cw, 32, andm, xorm);
    } else if (visible == 1 && !d && g_cursor && 0){ }
    {   POINT p; GetCursorPos(&p); SetCursorPos(p.x, p.y); }               /* refresh WM_SETCURSOR */
}

/* window icon from the game's own 'icl8' (Mac system palette) and 'ICN#' mask */
void host_set_icon(const unsigned char *icl8, const unsigned char *icn){
    static const unsigned char ramp[10] = {0xEE,0xDD,0xBB,0xAA,0x88,0x77,0x55,0x44,0x22,0x11};
    unsigned pal[256]; int i, x, y; BITMAPV5HEADER bh; void *bits; HBITMAP color, mask; ICONINFO ii; HICON ic;
    unsigned char andm[32*4];
    if (!icn) return;
    for (i = 0; i < 215; i++){ int r = 0xFF - (i/36)*0x33, g = 0xFF - ((i/6)%6)*0x33, b = 0xFF - (i%6)*0x33; pal[i] = (unsigned)(r<<16 | g<<8 | b); }
    for (i = 0; i < 10; i++){ pal[215+i] = (unsigned)ramp[i] << 16; pal[225+i] = (unsigned)ramp[i] << 8; pal[235+i] = ramp[i]; pal[245+i] = (unsigned)(ramp[i]*0x010101); }
    pal[255] = 0;
    memset(&bh, 0, sizeof bh);
    bh.bV5Size = sizeof bh; bh.bV5Width = 32; bh.bV5Height = -32; bh.bV5Planes = 1; bh.bV5BitCount = 32; bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
    color = CreateDIBSection(0, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, 0, 0);
    if (!color) return;
    memset(andm, 0xFF, sizeof andm);
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++){
        int m = (icn[128 + y*4 + (x>>3)] >> (7 - (x & 7))) & 1;
        unsigned c = icl8 ? pal[icl8[y*32 + x]] : (((icn[y*4 + (x>>3)] >> (7 - (x & 7))) & 1) ? 0 : 0xFFFFFF);
        ((unsigned*)bits)[y*32 + x] = m ? (c | 0xFF000000u) : 0;
        if (m) andm[y*4 + (x>>3)] &= (unsigned char)~(0x80 >> (x & 7));
    }
    mask = CreateBitmap(32, 32, 1, 1, andm);
    ii.fIcon = TRUE; ii.xHotspot = ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    ic = CreateIconIndirect(&ii);
    if (ic){ SendMessageA(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)ic); SendMessageA(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ic); }
    DeleteObject(color); DeleteObject(mask);
}

int host_pick_folder(char *out, int outlen){
    BROWSEINFOA bi; LPITEMIDLIST id; char buf[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = g_hwnd; bi.lpszTitle = "Folder with the " HLE_APP_NAME " game files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    CoInitialize(0);
    id = SHBrowseForFolderA(&bi);
    if (!id) return 0;
    if (!SHGetPathFromIDListA(id, buf)){ CoTaskMemFree(id); return 0; }
    CoTaskMemFree(id);
    strncpy(out, buf, (size_t)outlen - 1); out[outlen - 1] = 0;
    return 1;
}

int host_file_dialog(int save, const char *dir, const char *initial, char *out, int outlen, const char *title){
    OPENFILENAMEA o; char buf[700];
    memset(&o, 0, sizeof o); strncpy(buf, initial ? initial : "", sizeof buf - 1); buf[sizeof buf - 1] = 0;
    o.lStructSize = sizeof o; o.hwndOwner = g_hwnd; o.lpstrFilter = HLE_APP_NAME " documents\0*.*\0";
    o.lpstrFile = buf; o.nMaxFile = sizeof buf; o.lpstrInitialDir = dir; o.lpstrTitle = title && *title ? title : 0;
    o.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    memset(g_held, 0, sizeof g_held); g_down = 0;
    if (!(save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o))) return 0;
    strncpy(out, buf, (size_t)outlen - 1); out[outlen - 1] = 0;
    return 1;
}
