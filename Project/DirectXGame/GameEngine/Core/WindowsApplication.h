#pragma once
#include <Windows.h>
#include <stdint.h>
#include <cassert>

class WindowsApplication{
public:

    // UI の仮想解像度。スプライト・文字・マウスの座標はすべてこの大きさ（16:9）で書く。
    // 描画解像度（DirectXCore::GetRenderWidth）やウィンドウの大きさが変わっても変わらない
    static constexpr int32_t kUIWidth = 1600;
    static constexpr int32_t kUIHeight = 900;

    // 起動時のウィンドウのクライアント領域の大きさ
    static constexpr int32_t kClientWidth = kUIWidth;
    static constexpr int32_t kClientHeight = kUIHeight;

    // ゲームの絵をウィンドウ（スワップチェーン）の中に 16:9 のまま最大に収めた枠。
    // 余った所は黒帯になる。単位はクライアント（＝スワップチェーン）のピクセル。
    struct OutputRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = static_cast<float>(kUIWidth);
        float height = static_cast<float>(kUIHeight);
    };
    static OutputRect ComputeOutputRect(int32_t clientWidth, int32_t clientHeight);

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

    // 枠なしフルスクリーン（WS_POPUP でモニターいっぱい）とウィンドウの切り替え。
    // DXGI の排他フルスクリーンは使わない（Alt+Enter は DirectXCore で DXGI から外してある）。
    // 大きさが変わると WM_SIZE → ConsumePendingResize の流れでスワップチェーンが作り直される。
    // Alt+Enter と F11 でも切り替わる（WindowProc）。
    void SetFullscreen(bool fullscreen);
    void ToggleFullscreen() { SetFullscreen(!fullscreen_); }
    bool IsFullscreen() const { return fullscreen_; }

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};

    int32_t clientWidth_ = kClientWidth;
    int32_t clientHeight_ = kClientHeight;

    // WM_SIZE で立つフラグ。ProcessMessage の外でメインループから消費される。
    bool pendingResize_ = false;
    int32_t pendingWidth_ = kClientWidth;
    int32_t pendingHeight_ = kClientHeight;

    // 枠なしフルスクリーン中か。ウィンドウに戻すときのスタイルと位置（最大化も含む）を覚えておく
    bool fullscreen_ = false;
    LONG windowedStyle_ = 0;
    WINDOWPLACEMENT windowedPlacement_{ sizeof(WINDOWPLACEMENT) };
};

