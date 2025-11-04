#include "WindowsApplication.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WindowsApplication::Initialize(const wchar_t* title, int32_t width, int32_t height){
    wc_.lpfnWndProc = WindowProc;
    wc_.lpszClassName = L"CG2WindowClass";
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc_);

    RECT wrc = { 0,0,width,height };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    hwnd_ = CreateWindow(
        wc_.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wrc.right - wrc.left, wrc.bottom - wrc.top,
        nullptr, nullptr, wc_.hInstance, nullptr
    );
    assert(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
}

bool WindowsApplication::ProcessMessage(){
    MSG msg{};
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void WindowsApplication::Finalize(){
    CloseWindow(hwnd_);
}
