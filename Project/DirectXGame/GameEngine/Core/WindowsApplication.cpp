#include "WindowsApplication.h"

#ifdef USE_IMGUI

#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif // USE_IMGUI

LRESULT WindowsApplication::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){

#ifdef USE_IMGUI

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

#endif // USE_IMGUI

    // インスタンス取り出し（CreateWindow 時に GWLP_USERDATA に格納）
    auto* self = reinterpret_cast<WindowsApplication*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    // ウィンドウサイズ変更通知
    // 実際の Swapchain リサイズはレンダリングコマンド進行中だと危険なので、
    // ここではフラグだけ立て、メインループの安全な箇所で消費する。
    if (msg == WM_SIZE && self) {
        // SIZE_MINIMIZED 時は width/height = 0 になるため無視
        if (wparam != SIZE_MINIMIZED) {
            int32_t w = LOWORD(lparam);
            int32_t h = HIWORD(lparam);
            if (w > 0 && h > 0 &&
                (w != self->clientWidth_ || h != self->clientHeight_)) {
                self->pendingWidth_ = w;
                self->pendingHeight_ = h;
                self->pendingResize_ = true;
            }
        }
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WindowsApplication::Initialize(const wchar_t* title){
    wc_.lpfnWndProc = WindowProc;
    wc_.lpszClassName = L"CG2WindowClass";
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc_);

    DWORD windowStyle = WS_OVERLAPPEDWINDOW;

    RECT wrc = { 0,0,kClientWidth,kClientHeight };
    AdjustWindowRect(&wrc, windowStyle, false);

    hwnd_ = CreateWindow(
        wc_.lpszClassName,
        title,
        windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wrc.right - wrc.left, wrc.bottom - wrc.top,
        nullptr, nullptr, wc_.hInstance, nullptr
    );
    assert(hwnd_);

    // WindowProc から this を引けるよう紐づけ
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(hwnd_, SW_SHOW);
}

bool WindowsApplication::ProcessMessage(){
    MSG msg{};
    // 1フレームで溜まったメッセージを全て捌く。
    // if で1件ずつだとキーボードのオートリピート等でキューが詰まり、
    // WM_MOUSEMOVE が後ろに滞留してマウス座標（＝レティクル）が遅延する。
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

bool WindowsApplication::ConsumePendingResize(int32_t& width, int32_t& height) {
    if (!pendingResize_) return false;
    width = pendingWidth_;
    height = pendingHeight_;
    clientWidth_ = pendingWidth_;
    clientHeight_ = pendingHeight_;
    pendingResize_ = false;
    return true;
}

void WindowsApplication::Finalize(){
    CloseWindow(hwnd_);
    CoUninitialize();
}
