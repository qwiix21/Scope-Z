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
        timestr[strlen(timestr)-1] = '\0'; // Убираем \n
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
int EXIT_KEY = 0x23;
int LENS_SHAPE = 0;
bool DOT_ENABLED = false;
int DOT_SIZE = 4;
int DOT_R = 255, DOT_G = 0, DOT_B = 0;
int FPS = 60;
bool running = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
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

void update() {
    int w = LENS_WIDTH / (int)MAG_FACTOR;
    int h = LENS_HEIGHT / (int)MAG_FACTOR;
    int cx = GetSystemMetrics(SM_CXSCREEN) / 2;
    int cy = GetSystemMetrics(SM_CYSCREEN) / 2;
    
    RECT rect;
    rect.left = cx - w / 2;
    rect.top = cy - h / 2;
    rect.right = rect.left + w;
    rect.bottom = rect.top + h;
    pMagSetWindowSource(hwnd_mag, rect);
}

DWORD WINAPI MagnifierThread(LPVOID param) {
    DebugLog("Thread started");
    
    if (!running) {
        DebugLog("Thread cancelled before start");
        return 0;
    }
    
    hMag = LoadLibraryA("Magnification.dll");
    if (!hMag) {
        DebugLog("ERROR: Failed to load Magnification.dll");
        running = false;
        return 1;
    }
    DebugLog("Magnification.dll loaded");
    
    pMagInitialize = (MagInitializeFunc)GetProcAddress(hMag, "MagInitialize");
    pMagUninitialize = (MagUninitializeFunc)GetProcAddress(hMag, "MagUninitialize");
    pMagSetWindowSource = (MagSetWindowSourceFunc)GetProcAddress(hMag, "MagSetWindowSource");
    pMagSetWindowTransform = (MagSetWindowTransformFunc)GetProcAddress(hMag, "MagSetWindowTransform");
    
    if (!pMagInitialize) {
        DebugLog("ERROR: MagInitialize not found");
        running = false;
        return 1;
    }
    
    if (!pMagInitialize()) {
        DebugLog("ERROR: MagInitialize failed");
        running = false;
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
        running = false;
        return 1;
    }
    DebugLog("Host window created");
    
    SetLayeredWindowAttributes(hwnd_host, RGB(255, 0, 255), 0, LWA_COLORKEY);
    
    if (LENS_SHAPE == 0) {
        HRGN region = CreateEllipticRgn(0, 0, LENS_WIDTH, LENS_HEIGHT);
        SetWindowRgn(hwnd_host, region, TRUE);
    }

    hwnd_mag = CreateWindowExW(0, WC_MAGNIFIERW, L"", WS_CHILD | WS_VISIBLE,
        0, 0, LENS_WIDTH, LENS_HEIGHT, hwnd_host, NULL, instance, NULL);
        
    if (!hwnd_mag) {
        DebugLog("ERROR: Failed to create magnifier window");
        running = false;
        return 1;
    }
    DebugLog("Magnifier window created");

    MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
    pMagSetWindowTransform(hwnd_mag, &matrix);
    update();
    ShowWindow(hwnd_host, SW_SHOW);
    DebugLog("Magnifier started successfully");

    bool toggled = true;
    bool old_state = false;
    MSG msg;

    while (running) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        HWND foreground = GetForegroundWindow();
        if (foreground != hwnd_host) {
            if (GetAsyncKeyState(EXIT_KEY) & 0x8000) {
                DebugLog("Exit key pressed");
                break;
            }
            
            bool current = (GetAsyncKeyState(TOGGLE_KEY) & 0x8000) != 0;
            if (current && !old_state) {
                toggled = !toggled;
                DebugLog(toggled ? "Magnifier toggled ON" : "Magnifier toggled OFF");
            }
            old_state = current;
        }
        
        if (toggled) {
            update();
            if (DOT_ENABLED) InvalidateRect(hwnd_host, NULL, TRUE);
            ShowWindow(hwnd_host, SW_SHOW);
        } else {
            ShowWindow(hwnd_host, SW_HIDE);
        }
        
        Sleep(std::max(1, 1000 / FPS));
    }

    if (hwnd_mag) DestroyWindow(hwnd_mag);
    if (hwnd_host) DestroyWindow(hwnd_host);
    if (pMagUninitialize) pMagUninitialize();
    if (hMag) FreeLibrary(hMag);
    DebugLog("Magnifier stopped");
    running = false;
    return 0;
}

extern "C" __declspec(dllexport) void StartMagnifier(int lens_size, float zoom_factor, int toggle_key, int exit_key, int lens_shape, int dot_enabled, int dot_size, int dot_r, int dot_g, int dot_b, int fps) {
    if (running) {
        DebugLog("StartMagnifier called while already running");
        return;
    }
    
    DebugLog("StartMagnifier called with parameters");
    LENS_WIDTH = lens_size;
    LENS_HEIGHT = lens_size;
    MAG_FACTOR = zoom_factor;
    TOGGLE_KEY = toggle_key;
    EXIT_KEY = exit_key;
    LENS_SHAPE = lens_shape;
    DOT_ENABLED = dot_enabled != 0;
    DOT_SIZE = dot_size;
    DOT_R = dot_r;
    DOT_G = dot_g;
    DOT_B = dot_b;
    FPS = std::max(1, std::min(fps, 240));
    running = true;
    
    HANDLE thread = CreateThread(NULL, 0, MagnifierThread, NULL, 0, NULL);
    if (!thread) {
        DebugLog("ERROR: Failed to create magnifier thread");
        running = false;
    } else {
        CloseHandle(thread);
    }
}

extern "C" __declspec(dllexport) void StopMagnifier() {
    if (running) {
        DebugLog("StopMagnifier called");
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
    FPS = fps;
    
    if (hwnd_mag) {
        MAGTRANSFORM matrix = { {{ MAG_FACTOR, 0.0f, 0.0f }, { 0.0f, MAG_FACTOR, 0.0f }, { 0.0f, 0.0f, 1.0f }} };
        pMagSetWindowTransform(hwnd_mag, &matrix);
        SetWindowPos(hwnd_host, NULL, 0, 0, LENS_WIDTH, LENS_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);
        SetWindowPos(hwnd_mag, NULL, 0, 0, LENS_WIDTH, LENS_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);
        
        if (LENS_SHAPE == 0) {
            HRGN region = CreateEllipticRgn(0, 0, LENS_WIDTH, LENS_HEIGHT);
            SetWindowRgn(hwnd_host, region, TRUE);
        } else {
            SetWindowRgn(hwnd_host, NULL, TRUE);
        }
    }
}
