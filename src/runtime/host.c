/*
 * The House of the Dead 2 - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header). Every import goes to the real DLL --
 * DirectDraw 7 / Direct3D 7, DirectSound, DirectInput, winmm -- so what is
 * here is only what is specific to this game: where the image goes, the
 * command line, the calls that reach outside the game, muting, and the fault
 * report. Taken from HotD 1's host (hotd-recomp src/runtime/host.c).
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so the guest's
 * 0x00400000..0x009D5000 is free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <vfw.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"

extern const uint32_t hotd_entry_va;  /* recomp_dispatch.c */

#define IMAGE_BASE 0x00400000u
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

static DWORD g_watchdog_s;
static int   g_headless;
static int   g_fullscreen;   /* --fullscreen: D3DX's own fullscreen context */

/* --shot SECONDS FILE: the game's window (only it), as a .bmp; up to 8. */
static DWORD g_shot_s[8];
static char  g_shot_file[8][MAX_PATH];
static int   g_shots;
static void shot(const char* g_shot_file);
static DWORD WINAPI shot_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_shots; i++) {
        DWORD at = t0 + g_shot_s[i] * 1000, now = GetTickCount();
        if ((int)(at - now) > 0) Sleep(at - now);
        shot(g_shot_file[i]);
    }
    return 0;
}
static void shot(const char* g_shot_file) {
    /* The largest window this process owns: D3DX draws into a device window
     * of its own, not the one the game created. */
    HWND w = NULL;
    long best = 0;
    for (HWND c = GetTopWindow(NULL); c; c = GetNextWindow(c, GW_HWNDNEXT)) {
        DWORD pid;
        RECT cr;
        GetWindowThreadProcessId(c, &pid);
        if (pid != GetCurrentProcessId() || !GetClientRect(c, &cr)) continue;
        char t[128] = "";
        GetWindowTextA(c, t, sizeof t);
        printf("[shot] window %p \"%s\" %ldx%ld%s\n", (void*)c, t, cr.right, cr.bottom,
               IsWindowVisible(c) ? " visible" : "");
        if ((long)cr.right * cr.bottom > best) best = (long)cr.right * cr.bottom, w = c;
    }
    RECT r;
    if (!w || !GetClientRect(w, &r)) { printf("[shot] no game window\n"); return; }
    int cw = r.right, ch = r.bottom;
    HDC wdc = GetDC(w), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cw, ch, 1, 24, BI_RGB } };
    void* bits;
    HBITMAP bm = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bm);
    /* The game StretchBlts each frame to its window DC; PrintWindow would ask
     * it to repaint, which it doesn't. */
    BitBlt(mdc, 0, 0, cw, ch, wdc, 0, 0, SRCCOPY);
    GdiFlush();
    DWORD stride = ((DWORD)cw * 3 + 3) & ~3u, size = stride * ch;
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bi.bmiHeader + size, 0, 0, sizeof fh + sizeof bi.bmiHeader };
    FILE* f = fopen(g_shot_file, "wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, size, 1, f);
        fclose(f);
    }
    printf("[shot] %dx%d -> %s\n", cw, ch, g_shot_file);
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(w, wdc);
}

/* ---- the process is the host, the module is the guest ----------------------
 * GetModuleHandleA(NULL) and GetModuleFileNameA(NULL) would name the host;
 * the game finds its own folder through them. */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[MAX_PATH + 512];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0)) : IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0), size = ARG(2);
    char* out = (char*)(uintptr_t)ARG(1);
    if (h == 0 || h == IMAGE_BASE) {
        uint32_t n = (uint32_t)strlen(g_guest_exe);
        if (size) {
            uint32_t k = n < size ? n : size - 1;
            memcpy(out, g_guest_exe, k);
            out[k] = 0;
            n = k;
        }
        g_eax = n;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) {
    g_eax = (uint32_t)(uintptr_t)g_guest_cmdline;
    g_esp += 4;
}

/* A shim runs holding the machine, and a window call can wait on another
 * thread answering messages: run it with the machine let go. */
#define UNLOCKED(call) do { uint32_t esp_ = g_esp; mach_leave(); \
    call; DWORD err_ = GetLastError(); mach_enter(); \
    g_esp = esp_; SetLastError(err_); } while (0)

static void shim_MessageBoxA(void) {
    const char* text = ARG(1) ? (const char*)(uintptr_t)ARG(1) : "";
    const char* cap = ARG(2) ? (const char*)(uintptr_t)ARG(2) : "";
    printf("[host] MessageBoxA \"%s\": %s\n", cap, text);
    /* "Attention: <device> will be used" (Yes/No, per device; sub_004B23A0
     * asks again until one is taken): yes, the first one. */
    if (!strcmp(cap, "Attention")) g_eax = IDYES;
    else if (g_headless) {
        uint32_t b = ARG(3) & MB_TYPEMASK;
        g_eax = (b == MB_YESNO || b == MB_YESNOCANCEL) ? IDNO
              : (b == MB_RETRYCANCEL || b == MB_OKCANCEL) ? IDCANCEL
              : b == MB_ABORTRETRYIGNORE ? IDABORT : IDOK;
    } else {
        HWND w = (HWND)(uintptr_t)ARG(0);
        UINT type = ARG(3);
        int r;
        UNLOCKED(r = MessageBoxA(w, text, cap, type));
        g_eax = r;
    }
    g_esp += 4 + 4 * 4;
}

static void shim_OutputDebugStringA(void) {
    if (ARG(0)) printf("[dbg] %s", (const char*)(uintptr_t)ARG(0));
    g_esp += 4 + 1 * 4;
}

/* The game ends itself with ExitProcess, which skips the host's way out. */
static void shim_ExitProcess(void) {
    UINT code = ARG(0);
    printf("[host] ExitProcess(%u) from sub_%08X\n", code, g_cur_func);
    recomp_trace_flush();
    ExitProcess(code);
}

/* ---- the CD ------------------------------------------------------------------
 * The game checks for its CD in a real CD-ROM drive: your disc, or your disc
 * image mounted. The host only finds that drive (CD_DRIVE=E, or the first
 * CD-ROM drive with HOD2.ICO on it) and reads its files from the game folder. */
static char g_game_dir[MAX_PATH];
static char g_cd = 0;

static const char* cd_path(const char* in, char* out, size_t n) {
    if (in && g_cd && (in[0] & ~0x20) == g_cd && in[1] == ':') {
        const char* rest = in + 2;
        while (*rest == '\\' || *rest == '/') rest++;
        _snprintf(out, n - 1, "%s\\%s", g_game_dir, rest);
        out[n - 1] = 0;
        return out;
    }
    return in;
}

static void cd_pick(const char* game_dir) {
    char env[8], root[] = "?:\\", probe[MAX_PATH];
    lstrcpynA(g_game_dir, game_dir, MAX_PATH);
    if (GetEnvironmentVariableA("CD_DRIVE", env, sizeof env)) g_cd = (char)(env[0] & ~0x20);
    for (char c = 'D'; c <= 'Z' && !g_cd; c++) {
        root[0] = c;
        _snprintf(probe, sizeof probe, "%sHOD2.ICO", root);
        if (GetDriveTypeA(root) == DRIVE_CDROM && GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) g_cd = c;
    }
    if (g_cd) printf("  CD drive: %c: (files read from %s)\n", g_cd, g_game_dir);
    else printf("  no disc found: put the CD in the drive or mount your disc image (or set CD_DRIVE)\n");
}

/* The game's file opens, logged: the milestones a run is read by. */
static void shim_CreateFileA(void) {
    char buf[MAX_PATH * 2];
    const char* name = cd_path((const char*)(uintptr_t)ARG(0), buf, sizeof buf);
    /* D3DX 7 caches device caps in %windir%\d3dx.dat and trusts it: another
     * game's cache names modes this PC lacks ("GetCapsAtVideoMode - Couldn't
     * obtain info"). The game keeps its own, in its folder. */
    const char* tail = name ? strrchr(name, '\\') : NULL;
    if (tail && !_stricmp(tail, "\\d3dx.dat")) {
        _snprintf(buf, sizeof buf - 1, "%s\\d3dx.dat", g_game_dir);
        name = buf;
    }
    g_eax = (uint32_t)(uintptr_t)CreateFileA(name, ARG(1), ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3),
                                             ARG(4), ARG(5), (HANDLE)(uintptr_t)ARG(6));
    printf("[host] open %s -> %s\n", name ? name : "(null)",
           g_eax == (uint32_t)(uintptr_t)INVALID_HANDLE_VALUE ? "missing" : "ok");
    g_esp += 4 + 7 * 4;
}

/* A C++ throw, logged: the object's type and first words (D3DX's carry an
 * HRESULT), then raised for real so the lifted handlers see it. */
static void shim_RaiseException(void) {
    DWORD code = ARG(0), flags = ARG(1), n = ARG(2);
    const ULONG_PTR* a = (const ULONG_PTR*)(uintptr_t)ARG(3);
    if (code == 0xE06D7363u && n >= 3) {
        const uint32_t* obj = (const uint32_t*)a[1];
        const uint32_t* ti = (const uint32_t*)a[2];
        const uint32_t* cta = ti ? (const uint32_t*)(uintptr_t)ti[3] : NULL;
        const uint32_t* ct = cta && cta[0] ? (const uint32_t*)(uintptr_t)cta[1] : NULL;
        const char* name = ct ? (const char*)(uintptr_t)(ct[1] + 8) : "?";
        printf("[throw] %s from sub_%08X: %08X %08X %08X %08X\n", name, g_cur_func,
               obj ? obj[0] : 0, obj ? obj[1] : 0, obj ? obj[2] : 0, obj ? obj[3] : 0);
        if (obj && !IsBadStringPtrA((LPCSTR)obj, 300) && ((const char*)obj)[0] >= ' ')
            printf("[throw]   \"%.300s\"\n", (const char*)obj);
        else if (obj && obj[1] > 0x400000 && obj[1] < 0x10000000 && !IsBadStringPtrA((LPCSTR)(uintptr_t)obj[1], 200))
            printf("[throw]   \"%s\"\n", (const char*)(uintptr_t)obj[1]);
        recomp_dump_trace("throw");
    }
    UNLOCKED(RaiseException(code, flags, n, a));
    g_esp += 4 + 4 * 4;
}

/* Fullscreen flips fail unless the game's window is in front; started from a
 * console it is not, so it is brought forward when shown. */
static void shim_ShowWindow(void) {
    HWND w = (HWND)(uintptr_t)ARG(0);
    int cmd = (int)ARG(1);
    BOOL r;
    /* Windows refuses SetForegroundWindow to a process started in the
     * background; a tapped Alt lifts the lock (the usual workaround). */
    /* --headless: shown (a hidden or off-screen window draws nothing --shot
     * can copy) but see-through, click-through and never activated. */
    if (g_headless && cmd != SW_HIDE) {
        SetWindowLongA(w, GWL_EXSTYLE, GetWindowLongA(w, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
        SetLayeredWindowAttributes(w, 0, 1, LWA_ALPHA);
        cmd = SW_SHOWNOACTIVATE;
    }
    UNLOCKED(r = ShowWindow(w, cmd);
             if (cmd != SW_HIDE && !g_headless && g_fullscreen) {
                 keybd_event(VK_MENU, 0, 0, 0);
                 SetForegroundWindow(w);
                 keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
             });
    g_eax = r;
    g_esp += 4 + 2 * 4;
}

/* The game pauses (draws nothing) while its window is inactive. Windowed, it
 * is told it is always active, as HotD 1's online play does. */
static WNDPROC g_game_proc;
static HWND g_game_wnd;
static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATEAPP: case WM_NCACTIVATE: w = TRUE; break;
    case WM_ACTIVATE: w = MAKEWPARAM(WA_ACTIVE, HIWORD(w)); break;
    case WM_KILLFOCUS: return 0;
    }
    return CallWindowProcA(g_game_proc, h, m, w, l);
}

static void shim_CreateWindowExA(void) {
    uint32_t a[12];
    for (int i = 0; i < 12; i++) a[i] = ARG(i);
    if (g_headless) a[3] &= ~WS_VISIBLE;
    HWND h;
    UNLOCKED(h = CreateWindowExA(a[0], (LPCSTR)(uintptr_t)a[1], (LPCSTR)(uintptr_t)a[2], a[3],
                                 a[4], a[5], a[6], a[7], (HWND)(uintptr_t)a[8],
                                 (HMENU)(uintptr_t)a[9], (HINSTANCE)(uintptr_t)a[10], (LPVOID)(uintptr_t)a[11]));
    DWORD err = h ? 0 : GetLastError();
    if (h && !a[8] && !g_game_proc && !g_fullscreen) {
        /* The game's procedure is guest code: Windows' call to it faults on
         * the non-executable page and native32 runs the lifted body. */
        g_game_wnd = h;
        g_game_proc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)host_wndproc);
        PostMessageA(h, WM_ACTIVATEAPP, TRUE, 0);
    }
    SetLastError(err);
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

static int g_cur_x = -1, g_cur_y = -1;   /* --click's cursor, client pixels */

/* Scripted input for test runs: --key S DIK MS holds a key (a DIK_ scan
 * code) for MS milliseconds from S seconds after start; --click S X Y MS
 * holds the trigger with the gun at X,Y of the 640x480 screen (outside it
 * reloads). The game reads the keyboard with GetDeviceState and
 * GetDeviceData and the mouse buttons with GetDeviceState, and aims with GetCursorPos. Every device of
 * a DirectInput shares one vtable (GetDeviceState is slot 9, GetDeviceData
 * 10), so the devices are told apart by pointer. Headless, the devices
 * never acquire, so their own state is replaced, not added to. */
static struct { DWORD at, ms; int dik, x, y; } g_keys[1024];   /* dik 0: the trigger */
static int g_nkeys;
static DWORD g_t0;   /* run start, for --key, --click and the log */
static volatile uint8_t g_trigger, g_held[256];
static volatile LONG g_kevents[64];   /* queued key changes: dik | 0x100 when down */
static volatile LONG g_kev_head, g_kev_tail;
static void key_set(int i, int down) {
    if (!g_keys[i].dik) {
        if (down) g_cur_x = g_keys[i].x, g_cur_y = g_keys[i].y;
        g_trigger = down ? 0x80 : 0;
        return;
    }
    g_held[g_keys[i].dik] = down ? 0x80 : 0;
    LONG t = g_kev_tail;
    g_kevents[t & 63] = g_keys[i].dik | (down ? 0x100 : 0);
    InterlockedExchange(&g_kev_tail, t + 1);
}
static DWORD WINAPI key_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = g_t0;
    for (int i = 0; i < g_nkeys; i++) {   /* given in time order */
        for (int down = 1; down >= 0; down--) {
            DWORD at = t0 + g_keys[i].at + (down ? 0 : g_keys[i].ms), now = GetTickCount();
            if ((int)(at - now) > 0) Sleep(at - now);
            key_set(i, down);
        }
    }
    return 0;
}

typedef HRESULT (WINAPI *state_t)(void*, DWORD, void*);
typedef HRESULT (WINAPI *data_t)(void*, DWORD, void*, DWORD*, DWORD);
static state_t g_get_state;
static data_t  g_get_data;
static void *g_kbd, *g_mouse;
static HRESULT WINAPI hook_GetDeviceState(void* dev, DWORD cb, void* out) {
    HRESULT hr = g_get_state(dev, cb, out);
    if (dev == g_mouse && out && cb >= 16 && g_nkeys) {   /* DIMOUSESTATE: buttons at 12 */
        memset(out, 0, cb);
        ((uint8_t*)out)[12] = g_trigger;
        hr = 0;
    } else if (dev == g_kbd && out && g_nkeys) {
        memcpy(out, (const void*)g_held, cb < 256 ? cb : 256);
        hr = 0;
    }
    return hr;
}
static HRESULT WINAPI hook_GetDeviceData(void* dev, DWORD cb, void* rg, DWORD* n, DWORD flags) {
    if (dev != g_kbd || !n || !g_nkeys) return g_get_data(dev, cb, rg, n, flags);
    DWORD want = *n;
    *n = 0;   /* ponytail: DIGDD_PEEK not honoured */
    while (*n < want && g_kev_head != g_kev_tail) {
        LONG ev = g_kevents[g_kev_head & 63];
        if (rg) {   /* DIDEVICEOBJECTDATA: offset, data, time stamp, sequence */
            DWORD* e = (DWORD*)((uint8_t*)rg + *n * cb);
            memset(e, 0, cb);
            e[0] = ev & 0xFF, e[1] = ev & 0x100 ? 0x80 : 0, e[2] = GetTickCount();
        }
        InterlockedIncrement(&g_kev_head);
        (*n)++;
    }
    return 0;
}
static void* vt_hook(void** vt, int n, void* fn) {
    DWORD old;
    void* was = vt[n];
    VirtualProtect(&vt[n], sizeof vt[n], PAGE_READWRITE, &old);
    vt[n] = fn;
    VirtualProtect(&vt[n], sizeof vt[n], old, &old);
    return was;
}
static HRESULT di_device(const GUID* g, void** out, HRESULT hr) {
    static const GUID mouse = { 0x6f1d2b60, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
    static const GUID kbd = { 0x6f1d2b61, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
    printf("[dinput] CreateDevice %08lX -> %08lX at %lu ms\n", g->Data1, hr, GetTickCount() - g_t0);
    if (hr == 0 && *out) {
        void** vt = **(void****)out;
        if (!memcmp(g, &kbd, sizeof kbd)) g_kbd = *out;
        if (!memcmp(g, &mouse, sizeof mouse)) g_mouse = *out;
        if (vt[9] != (void*)hook_GetDeviceState) {
            g_get_state = (state_t)vt_hook(vt, 9, hook_GetDeviceState);
            g_get_data = (data_t)vt_hook(vt, 10, hook_GetDeviceData);
        }
    }
    return hr;
}
typedef HRESULT (WINAPI *di_create_dev_ex_t)(void*, const GUID*, const GUID*, void**, void*);
static di_create_dev_ex_t g_di_create_dev_ex;
static HRESULT WINAPI hook_DI_CreateDeviceEx(void* di, const GUID* g, const GUID* iid, void** out, void* outer) {
    return di_device(g, out, g_di_create_dev_ex(di, g, iid, out, outer));
}

/* DirectInput checks the instance handle; the game passes its own image base,
 * which Windows never loaded. The host's stands in. The game makes its
 * devices with IDirectInput7::CreateDeviceEx (slot 9). */
static void shim_DirectInputCreateEx(void) {
    typedef HRESULT (WINAPI *create_t)(HINSTANCE, DWORD, const GUID*, void**, void*);
    static create_t create;
    if (!create) create = (create_t)GetProcAddress(LoadLibraryA("dinput.dll"), "DirectInputCreateEx");
    uint32_t ver = ARG(1), iid = ARG(2), out = ARG(3), outer = ARG(4);
    HRESULT hr;
    UNLOCKED(hr = create(GetModuleHandleA(NULL), ver, (const GUID*)(uintptr_t)iid,
                         (void**)(uintptr_t)out, (void*)(uintptr_t)outer));
    if (hr == 0 && MEM32(out)) {
        void** vt = **(void****)(uintptr_t)out;
        if (vt[9] != (void*)hook_DI_CreateDeviceEx) g_di_create_dev_ex = (di_create_dev_ex_t)vt_hook(vt, 9, hook_DI_CreateDeviceEx);
    }
    g_eax = (uint32_t)hr;
    g_esp += 4 + 5 * 4;
}

/* The gun aims with the cursor; once --click has placed it, it stays there. */
static void shim_GetCursorPos(void) {
    POINT* p = (POINT*)(uintptr_t)ARG(0);
    BOOL r;
    if (g_cur_x >= 0 && g_game_wnd && p) {
        RECT rc;
        GetClientRect(g_game_wnd, &rc);
        p->x = g_cur_x * (rc.right ? rc.right : 640) / 640, p->y = g_cur_y * (rc.bottom ? rc.bottom : 480) / 480;
        ClientToScreen(g_game_wnd, p);
        r = TRUE;
    } else UNLOCKED(r = GetCursorPos(p));
    g_eax = r;
    g_esp += 4 + 4;
}

static native32_shim_t g_shims[] = {
    { "GetCursorPos", shim_GetCursorPos },
    { "ShowWindow", shim_ShowWindow },
    { "CreateWindowExA", shim_CreateWindowExA },
    { "DirectInputCreateEx", shim_DirectInputCreateEx },
    { "RaiseException", shim_RaiseException },
    { "CreateFileA", shim_CreateFileA },
    { "MessageBoxA", shim_MessageBoxA },
    { "OutputDebugStringA", shim_OutputDebugStringA },
    { "ExitProcess", shim_ExitProcess },
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
    { "GetCommandLineA", shim_GetCommandLineA },
};

/* ---- muted by default -------------------------------------------------------
 * Every run is silent until sound is being tested (--sound). */
static const GUID kCLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID kIID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID kIID_IAudioSessionManager =
    { 0xBFA971F1, 0x4D5E, 0x40BB, { 0x93, 0x5E, 0x96, 0x70, 0x39, 0xBF, 0xBE, 0xE4 } };

/* 1: muted; 2: there is no audio device; 0: failed. */
static int mute_process(void) {
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioSessionManager* mgr = NULL;
    ISimpleAudioVolume* vol = NULL;
    HRESULT hr = CoCreateInstance(&kCLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                  &kIID_IMMDeviceEnumerator, (void**)&en);
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) { IMMDeviceEnumerator_Release(en); return 2; }
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate(dev, &kIID_IAudioSessionManager, CLSCTX_ALL, NULL, (void**)&mgr);
    if (SUCCEEDED(hr)) hr = IAudioSessionManager_GetSimpleAudioVolume(mgr, &GUID_NULL, FALSE, &vol);
    if (SUCCEEDED(hr)) hr = ISimpleAudioVolume_SetMute(vol, TRUE, NULL);
    if (vol) ISimpleAudioVolume_Release(vol);
    if (mgr) IAudioSessionManager_Release(mgr);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
    return SUCCEEDED(hr);
}

/* ---- reports ---------------------------------------------------------------- */
recomp_func_t recomp_lookup_manual(uint32_t va) {
    (void)va;
    return NULL;
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* Added after native32's own handler, so callbacks are resolved first and only
 * real faults get here. */
static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* --sample: the lifted function running, ten times a second, for a while. */
static int g_sample;
static DWORD WINAPI sampler(LPVOID unused) {
    (void)unused;
    for (int i = 0; i < 400; i++) {
        Sleep(100);
        printf("[sample] %5.1fs sub_%08X %s\n", i / 10.0, g_cur_func, g_cur_import ? g_cur_import : "");
    }
    return 0;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stdout);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

#include <ddraw.h>
static HRESULT WINAPI modes_cb(LPDDSURFACEDESC2 d, LPVOID c) {
    if (d->ddpfPixelFormat.dwRGBBitCount == 16) (*(int*)c)++;
    return DDENUMRET_OK;
}
/* How many 16-bit modes DirectDraw offers this process (0: Windows' 8/16-bit
 * mode emulation is off for it, and the game cannot start). */
static int modes16(void) {
    LPDIRECTDRAW7 dd;
    int n = 0;
    typedef HRESULT (WINAPI *create_t)(GUID*, LPVOID*, REFIID, IUnknown*);
    create_t create = (create_t)GetProcAddress(LoadLibraryA("ddraw.dll"), "DirectDrawCreateEx");
    if (!create || create(NULL, (void**)&dd, &IID_IDirectDraw7, NULL)) return -1;
    IDirectDraw7_EnumDisplayModes(dd, 0, NULL, &n, modes_cb);
    IDirectDraw7_Release(dd);
    return n;
}

/* The D3DX context's mode (run_lift.py PATCHES): windowed unless --fullscreen. */
uint32_t hod2_context_mode(uint32_t field, uint32_t mode) {
    if (mode == 1 && !g_fullscreen) mode = 0;
    MEM32(field) = mode;
    return mode;
}

/* MSVC's longjmp (run_lift.py PATCHES). jmp_buf: ebp, ebx, edi, esi, esp at
 * the setjmp call (on its return address), return address. The lifted frame
 * that owns that stack resumes at the landing pad after its setjmp call,
 * with setjmp "returning" val. */
extern const uint32_t hotd_setjmp_resume[][2];   /* recomp_dispatch.c */
void hotd_longjmp(uint32_t jb, uint32_t val) {
    uint32_t esp = MEM32(jb + 0x10) + 4;          /* the return address popped */
    recomp_ehframe_t* f = recomp_eh_owner(esp);
    uint32_t resume = 0;
    /* By the owning function: a lifted call pushes no real return address
     * (RECOMP_RETADDR=0), so the buffer's is 0. Each owner here calls setjmp
     * once (sub_004AC240). */
    for (int i = 0; f && hotd_setjmp_resume[i][0]; i++)
        if (hotd_setjmp_resume[i][0] == f->func) resume = hotd_setjmp_resume[i][1];
    if (!resume) {
        fprintf(stderr, "[longjmp] buffer 0x%08X (esp 0x%08X, to 0x%08X): no lifted setjmp caller owns it\n",
                jb, esp, MEM32(jb + 0x14));
        TerminateProcess(GetCurrentProcess(), 3);
    }
    g_ebp = MEM32(jb), g_ebx = MEM32(jb + 4), g_edi = MEM32(jb + 8), g_esi = MEM32(jb + 0xC);
    g_esp = esp;
    g_eax = val ? val : 1;
    recomp_eh_resume(f, resume);
}

/* The intro movie's poll (run_lift.py PATCHES): over at once if this PC has
 * no Indeo 3.2 decoder to play it with. */
uint32_t hod2_movie_poll(uint32_t playing) {
    static int codec = -1;
    if (codec < 0) {
        HIC h = ICOpen(ICTYPE_VIDEO, mmioFOURCC('i', 'v', '3', '2'), ICMODE_DECOMPRESS);
        codec = h != NULL;
        if (h) ICClose(h);
        printf("[host] intro movie: %s\n", codec ? "Indeo 3.2 present, playing" : "no Indeo 3.2 decoder, skipped");
    }
    return codec ? playing : 0;
}

int main(int argc, char** argv) {
    const char* exe = "work\\Hod2.exe";
    const char* game = "game";
    char exe_full[MAX_PATH], game_full[MAX_PATH], guest_args[512] = "";
    int run = 0, sound = 0;
    setvbuf(stdout, NULL, _IONBF, 0);   /* a fault ends the run with TerminateProcess */
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--sound")) sound = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--fullscreen")) g_fullscreen = 1;
        else if (!strcmp(argv[i], "--sample")) g_sample = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && g_shots < 8) {
            g_shot_s[g_shots] = strtoul(argv[++i], NULL, 0);
            GetFullPathNameA(argv[++i], MAX_PATH, g_shot_file[g_shots++], NULL);
        }
        else if (!strcmp(argv[i], "--key") && i + 3 < argc && g_nkeys < 1024) {
            g_keys[g_nkeys].at = (DWORD)(atof(argv[++i]) * 1000);
            g_keys[g_nkeys].dik = (int)strtoul(argv[++i], NULL, 0) & 0xFF;
            g_keys[g_nkeys++].ms = strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--click") && i + 4 < argc && g_nkeys < 1024) {
            g_keys[g_nkeys].at = (DWORD)(atof(argv[++i]) * 1000);
            g_keys[g_nkeys].x = atoi(argv[++i]);
            g_keys[g_nkeys].y = atoi(argv[++i]);
            g_keys[g_nkeys++].ms = strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--args") && i + 1 < argc) _snprintf(guest_args, sizeof guest_args - 1, " %s", argv[++i]);
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else {
            printf("usage: hotd2 [--run] [--headless] [--sound] [--exe work\\Hod2.exe] [--game game]\n"
                   "             [--args \"<game switches>\"] [--watchdog S] [--callbacks] [--native-trace]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(exe, MAX_PATH, exe_full, NULL);
    GetFullPathNameA(game, MAX_PATH, game_full, NULL);
    cd_pick(game_full);
    _snprintf(g_guest_exe, sizeof g_guest_exe - 1, "%s\\Hod2.exe", game_full);
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\"%s", g_guest_exe, guest_args);

    /* The game needs 640x480x16; a 32-bit desktop offers DirectDraw no
     * 16-bit modes, so run again under Windows' DWM8And16BitMitigation +
     * 16BITCOLOR layers (verified on testbox: title screen). */
    if (run) {
        int rc = native32_require_color_depth(16);
        if (rc >= 0) return rc;
    }
    if (!modes16())
        fprintf(stderr, "  warning: DirectDraw offers this process no 16-bit display modes;\n"
                        "  the game needs 640x480x16 (is the host named hotd2.exe? CMakeLists.txt)\n");
    native32_init();
    AddVectoredExceptionHandler(0, crash);
    printf("House of the Dead 2 recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    uint32_t span = native32_map(exe_full, IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", exe, IMAGE_BASE, IMAGE_BASE + span);
    if (native32_bind(IMAGE_BASE, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]))) return 1;
    printf("  guest command line %s\n", g_guest_cmdline);
    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", hotd_entry_va);
        return 0;
    }
    /* The game opens its data relative to its working directory. */
    if (!SetCurrentDirectoryA(game_full)) { fprintf(stderr, "cannot enter %s\n", game_full); return 1; }
    CoInitialize(NULL);
    int m = sound ? 1 : mute_process();
    if (!m) {
        fprintf(stderr, "cannot mute the process audio session; pass --sound to run with sound\n");
        return 1;
    }
    printf("  audio: %s\n", sound ? "on (--sound)" : m == 2 ? "muted (no audio device)" : "muted (--sound to hear it)");
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_sample) CloseHandle(CreateThread(NULL, 0, sampler, NULL, 0, NULL));
    if (g_shots) CloseHandle(CreateThread(NULL, 0, shot_thread, NULL, 0, NULL));
    g_t0 = GetTickCount();
    if (g_nkeys) CloseHandle(CreateThread(NULL, 0, key_thread, NULL, 0, NULL));
    printf("  entering 0x%08X\n\n", hotd_entry_va);
    native32_call_guest(hotd_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}

