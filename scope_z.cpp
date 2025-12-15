#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>

#define WC_MAGNIFIERW L"Magnifier"

void DebugLog(const char* msg) {
    FILE* f = fopen("scope_z_debug.log", "a");
    if (f) {
        time_t now = time(0);
        char* timestr = ctime(&now);
        timestr[strlen(timestr)-1] = '\0';
        fprintf(f, "[%s] %s\n", timestr, msg);
        fclose(f);
    }
}

typedef struct { float v[3][3]; } MAGTRANSFORM;
typedef BOOL (WINAPI *MagInitializeFunc)(void);
typedef BOOL (WINAPI *MagUninitializeFunc)(void);
typedef BOOL (WINAPI *MagSetWindowSourceFunc)(HWND, RECT);
typedef BOOL (WINAPI *MagSetWindowTransformFunc)(HWND, MAGTRANSFORM*);

HMODULE hMag = NULL;
MagInitializeFunc pMagInitialize = NULL;
MagUninitializeFunc pMagUninitialize = NULL;
MagSetWindowSourceFunc pMagSetWindowSource = NULL;
MagSetWindowTransformFunc pMagSetWindowTransform = NULL;

HWND hwnd_host = nullptr;
HWND hwnd_mag = nullptr;
int LENS_WIDTH = 300;
int LENS_HEIGHT = 300;
float MAG_FACTOR = 3.0f;
int TOGGLE_KEY = 0x05;
int ZOOM_IN_KEY = 0x26;
bool ZOOM_IN_CTRL = true;
bool ZOOM_IN_SHIFT = false;
bool ZOOM_IN_ALT = false;
int ZOOM_OUT_KEY = 0x28;
bool ZOOM_OUT_CTRL = true;
bool ZOOM_OUT_SHIFT = false;
bool ZOOM_OUT_ALT = false;
int LENS_SHAPE = 0;
bool DOT_ENABLED = false;
int DOT_SIZE = 4;
int DOT_R = 255, DOT_G = 0, DOT_B = 0;

bool running = false;
HHOOK mouse_hook = NULL;
static LARGE_INTEGER perf_freq;
static bool perf_init = false;

void update();

double GetCurrentTimeMs() {
    if (!perf_init) {
        QueryPerformanceFrequency(&perf_freq);
        perf_init = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / perf_freq.QuadPart;
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && running && wParam == WM_MOUSEWHEEL) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        short delta = GET_WHEEL_DELTA_WPARAM(pMouseStruct->mouseData);

        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        bool zoom_in_mods = (!ZOOM_IN_CTRL || ctrl) && (!ZOOM_IN_SHIFT || shift) && (!ZOOM_IN_ALT || alt);
        bool zoom_out_mods = (!ZOOM_OUT_CTRL || ctrl) && (!ZOOM_OUT_SHIFT || shift) && (!ZOOM_OUT_ALT || alt);

        if (delta > 0 && zoom_in_mods) {
            double start_time = GetCurrentTimeMs();
            MAG_FACTOR = std::min(10.0f, MAG_FACTOR + 0.5f);
            if (hwnd_mag) {
                MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
                pMagSetWindowTransform(hwnd_mag, &matrix);
                update();
            }
            double end_time = GetCurrentTimeMs();
            char msg[100];
            sprintf(msg, "Zoom in latency: %.2f ms", end_time - start_time);
            DebugLog(msg);
            return 1;
        }
        else if (delta < 0 && zoom_out_mods) {
            double start_time = GetCurrentTimeMs();
            MAG_FACTOR = std::max(1.0f, MAG_FACTOR - 0.5f);
            if (hwnd_mag) {
                MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
                pMagSetWindowTransform(hwnd_mag, &matrix);
                update();
            }
            double end_time = GetCurrentTimeMs();
            char msg[100];
            sprintf(msg, "Zoom out latency: %.2f ms", end_time - start_time);
            DebugLog(msg);
            return 1;
        }
    }
    return CallNextHookEx(mouse_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (DOT_ENABLED) {
            HBRUSH brush = CreateSolidBrush(RGB(DOT_R, DOT_G, DOT_B));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(DOT_R, DOT_G, DOT_B));
            SelectObject(hdc, brush);
            SelectObject(hdc, pen);
            int cx = LENS_WIDTH / 2;
            int cy = LENS_HEIGHT / 2;
            Ellipse(hdc, cx - DOT_SIZE, cy - DOT_SIZE, cx + DOT_SIZE, cy + DOT_SIZE);
            DeleteObject(brush);
            DeleteObject(pen);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static int screen_cx = 0;
static int screen_cy = 0;

void update() {
    if (screen_cx == 0) {
        screen_cx = GetSystemMetrics(SM_CXSCREEN) / 2;
        screen_cy = GetSystemMetrics(SM_CYSCREEN) / 2;
    }

    float src_w = (float)LENS_WIDTH / MAG_FACTOR;
    float src_h = (float)LENS_HEIGHT / MAG_FACTOR;

    RECT rect;
    rect.left   = (int)(screen_cx - src_w * 0.5f + 0.5f);
    rect.top    = (int)(screen_cy - src_h * 0.5f + 0.5f);
    rect.right  = (int)(screen_cx + src_w * 0.5f + 0.5f);
    rect.bottom = (int)(screen_cy + src_h * 0.5f + 0.5f);

    if (hwnd_mag && pMagSetWindowSource) {
        pMagSetWindowSource(hwnd_mag, rect);
    }

    if (DOT_ENABLED && hwnd_host) {
        InvalidateRect(hwnd_host, NULL, TRUE);
    }
}

DWORD WINAPI MagnifierThread(LPVOID param) {
    DebugLog("Thread started");

    hMag = LoadLibraryA("Magnification.dll");
    if (!hMag) {
        DebugLog("ERROR: Failed to load Magnification.dll");
        return 1;
    }

    pMagInitialize = (MagInitializeFunc)GetProcAddress(hMag, "MagInitialize");
    pMagUninitialize = (MagUninitializeFunc)GetProcAddress(hMag, "MagUninitialize");
    pMagSetWindowSource = (MagSetWindowSourceFunc)GetProcAddress(hMag, "MagSetWindowSource");
    pMagSetWindowTransform = (MagSetWindowTransformFunc)GetProcAddress(hMag, "MagSetWindowTransform");

    if (!pMagInitialize || !pMagInitialize()) {
        DebugLog("ERROR: MagInitialize failed");
        FreeLibrary(hMag);
        return 1;
    }
    DebugLog("Magnification initialized");

    HINSTANCE instance = GetModuleHandle(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ScopeZ";
    RegisterClassW(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - LENS_WIDTH) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - LENS_HEIGHT) / 2;

    hwnd_host = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP, x, y, LENS_WIDTH, LENS_HEIGHT, NULL, NULL, instance, NULL);

    if (!hwnd_host) {
        DebugLog("ERROR: Failed to create host window");
        pMagUninitialize();
        FreeLibrary(hMag);
        return 1;
    }

    SetLayeredWindowAttributes(hwnd_host, RGB(255, 0, 255), 0, LWA_COLORKEY);

    if (LENS_SHAPE == 0) {
        HRGN region = CreateEllipticRgn(0, 0, LENS_WIDTH, LENS_HEIGHT);
        SetWindowRgn(hwnd_host, region, TRUE);
    }

    hwnd_mag = CreateWindowExW(0, WC_MAGNIFIERW, L"", WS_CHILD | WS_VISIBLE,
        0, 0, LENS_WIDTH, LENS_HEIGHT, hwnd_host, NULL, instance, NULL);

    if (!hwnd_mag) {
        DebugLog("ERROR: Failed to create magnifier window");
        DestroyWindow(hwnd_host);
        pMagUninitialize();
        FreeLibrary(hMag);
        return 1;
    }

    MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
    pMagSetWindowTransform(hwnd_mag, &matrix);
    update();
    ShowWindow(hwnd_host, SW_SHOW);

    mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);

    bool toggled = true;
    bool prev_toggle_state = false;
    MSG msg;

    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        bool current_toggle = (GetAsyncKeyState(TOGGLE_KEY) & 0x8000) != 0;
        if (current_toggle && !prev_toggle_state) {
            toggled = !toggled;
        }
        prev_toggle_state = current_toggle;

        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        bool zoom_in_pressed = (GetAsyncKeyState(ZOOM_IN_KEY) & 0x8000) != 0;
        bool zoom_out_pressed = (GetAsyncKeyState(ZOOM_OUT_KEY) & 0x8000) != 0;

        bool zoom_in_mods = (!ZOOM_IN_CTRL || ctrl) && (!ZOOM_IN_SHIFT || shift) && (!ZOOM_IN_ALT || alt);
        bool zoom_out_mods = (!ZOOM_OUT_CTRL || ctrl) && (!ZOOM_OUT_SHIFT || shift) && (!ZOOM_OUT_ALT || alt);

        static bool prev_zoom_in = false;
        static bool prev_zoom_out = false;

        if (zoom_in_pressed && zoom_in_mods && !prev_zoom_in) {
            MAG_FACTOR = std::min(10.0f, MAG_FACTOR + 0.5f);
            MAGTRANSFORM m = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
            pMagSetWindowTransform(hwnd_mag, &m);
            update();
        }
        if (zoom_out_pressed && zoom_out_mods && !prev_zoom_out) {
            MAG_FACTOR = std::max(1.0f, MAG_FACTOR - 0.5f);
            MAGTRANSFORM m = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
            pMagSetWindowTransform(hwnd_mag, &m);
            update();
        }
        prev_zoom_in = zoom_in_pressed;
        prev_zoom_out = zoom_out_pressed;

        if (toggled) {
            update();
            ShowWindow(hwnd_host, SW_SHOW);
        } else {
            ShowWindow(hwnd_host, SW_HIDE);
        }

        Sleep(1);
    }

    if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
    if (hwnd_mag) DestroyWindow(hwnd_mag);
    if (hwnd_host) DestroyWindow(hwnd_host);
    if (pMagUninitialize) pMagUninitialize();
    if (hMag) FreeLibrary(hMag);

    hwnd_host = hwnd_mag = nullptr;
    mouse_hook = NULL;
    DebugLog("Magnifier stopped");
    running = false;
    return 0;
}

extern "C" __declspec(dllexport) void StartMagnifier(int lens_size, float zoom_factor, int toggle_key, int zoom_in_key, int zoom_in_ctrl, int zoom_in_shift, int zoom_in_alt, int zoom_out_key, int zoom_out_ctrl, int zoom_out_shift, int zoom_out_alt, int lens_shape, int dot_enabled, int dot_size, int dot_r, int dot_g, int dot_b, int fps) {
    if (running) return;

    LENS_WIDTH = lens_size;
    LENS_HEIGHT = lens_size;
    MAG_FACTOR = zoom_factor;
    TOGGLE_KEY = toggle_key;
    ZOOM_IN_KEY = zoom_in_key;
    ZOOM_IN_CTRL = zoom_in_ctrl != 0;
    ZOOM_IN_SHIFT = zoom_in_shift != 0;
    ZOOM_IN_ALT = zoom_in_alt != 0;
    ZOOM_OUT_KEY = zoom_out_key;
    ZOOM_OUT_CTRL = zoom_out_ctrl != 0;
    ZOOM_OUT_SHIFT = zoom_out_shift != 0;
    ZOOM_OUT_ALT = zoom_out_alt != 0;
    LENS_SHAPE = lens_shape;
    DOT_ENABLED = dot_enabled != 0;
    DOT_SIZE = dot_size;
    DOT_R = dot_r;
    DOT_G = dot_g;
    DOT_B = dot_b;

    screen_cx = screen_cy = 0;
    running = true;

    CreateThread(NULL, 0, MagnifierThread, NULL, 0, NULL);
}

extern "C" __declspec(dllexport) void StopMagnifier() {
    if (running) {
        running = false;
        Sleep(100);
    }
}

extern "C" __declspec(dllexport) void UpdateSettings(int lens_size, float zoom_factor, int lens_shape, int dot_enabled, int dot_size, int dot_r, int dot_g, int dot_b, int fps) {
    LENS_WIDTH = lens_size;
    LENS_HEIGHT = lens_size;
    MAG_FACTOR = zoom_factor;
    LENS_SHAPE = lens_shape;
    DOT_ENABLED = dot_enabled != 0;
    DOT_SIZE = dot_size;
    DOT_R = dot_r;
    DOT_G = dot_g;
    DOT_B = dot_b;

    screen_cx = screen_cy = 0;

    if (hwnd_mag && pMagSetWindowTransform) {
        MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
        pMagSetWindowTransform(hwnd_mag, &matrix);
    }

    if (hwnd_host) {
        int x = (GetSystemMetrics(SM_CXSCREEN) - LENS_WIDTH) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - LENS_HEIGHT) / 2;
        SetWindowPos(hwnd_host, NULL, x, y, LENS_WIDTH, LENS_HEIGHT, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwnd_mag, NULL, 0, 0, LENS_WIDTH, LENS_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);

        if (LENS_SHAPE == 0) {
            HRGN region = CreateEllipticRgn(0, 0, LENS_WIDTH, LENS_HEIGHT);
            SetWindowRgn(hwnd_host, region, TRUE);
        } else {
            SetWindowRgn(hwnd_host, NULL, TRUE);
        }

        update();
        InvalidateRect(hwnd_host, NULL, TRUE);
    }
}

extern "C" __declspec(dllexport) float GetCurrentZoom() {
    return MAG_FACTOR;
}
