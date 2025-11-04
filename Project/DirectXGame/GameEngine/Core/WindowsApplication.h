#pragma once
#include <Windows.h>
#include <stdint.h>
#include <cassert>

class WindowsApplication{
public:
    
    static const int32_t kClientWidth = 1280;
    static const int32_t kClientHeight = 720;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // 初期化
    void Initialize(const wchar_t* title);

    // メッセージ処理
    bool ProcessMessage();

    // 終了処理
    void Finalize();

    // ゲッターロボ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetInstanceHandle() const { return wc_.hInstance; }

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};
};

