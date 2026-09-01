#pragma once
#include <Windows.h>
#include <stdint.h>
#include <cassert>

class WindowsApplication{
public:

    // ゲーム内部の描画解像度。エディタの Viewport やオフスクリーンRTは常にこのサイズ。
    // ウィンドウ実サイズとは別物。
    static const int32_t kClientWidth = 1600;
    static const int32_t kClientHeight = 900;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="title">タイトルバーに出す文字列</param>
    /// <param name="iconResourceId">
    /// exe に埋め込んだ ICON リソースの ID。0 なら Windows の既定アイコンを使う。
    /// タイトルバーと Alt+Tab の表示に反映される。
    /// （エクスプローラーやタスクバーに出る exe 自体のアイコンは、
    ///   .rc に ICON を書いてビルドした時点で Windows が自動的に拾うので、
    ///   こちらの指定とは独立している）
    /// </param>
    void Initialize(const wchar_t* title, int iconResourceId = 0);

    // メッセージ処理
    bool ProcessMessage();

    // 終了処理
    void Finalize();

    // ゲッターロボ
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetInstanceHandle() const { return wc_.hInstance; }

    // 現在のクライアント領域サイズ（リサイズ後の実サイズ）
    int32_t GetClientWidth() const { return clientWidth_; }
    int32_t GetClientHeight() const { return clientHeight_; }

    // 直近の WM_SIZE で受け取った新サイズを取り出す。
    // 戻り値 true のとき width/height に新サイズが書かれ、内部フラグがクリアされる。
    // 主にメインループで dxCore_->Resize() を呼ぶトリガに使う。
    bool ConsumePendingResize(int32_t& width, int32_t& height);

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};

    int32_t clientWidth_ = kClientWidth;
    int32_t clientHeight_ = kClientHeight;

    // WM_SIZE で立つフラグ。ProcessMessage の外でメインループから消費される。
    bool pendingResize_ = false;
    int32_t pendingWidth_ = kClientWidth;
    int32_t pendingHeight_ = kClientHeight;
};

