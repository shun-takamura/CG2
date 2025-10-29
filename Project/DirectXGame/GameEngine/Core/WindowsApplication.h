#pragma once
#include <Windows.h>
#include <stdint.h>
#include <cassert>

class WindowsApplication{
public:
    // 初期化
    void Initialize(const wchar_t* title, int32_t width, int32_t height);

    // メッセージ処理
    bool ProcessMessage();

    // 終了処理
    void Finalize();

    // ウィンドウハンドル取得
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetInstanceHandle() const { return wc_.hInstance; }

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};
};

