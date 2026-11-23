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
    /// <summary>クライアント領域の中に UI の 16:9 を最大に収めた枠を求める（端はピクセル境界にそろえる）</summary>
    /// <param name="clientWidth">クライアント領域の幅 [px]</param>
    /// <param name="clientHeight">クライアント領域の高さ [px]</param>
    /// <returns>枠（大きさが 0 以下なら既定の 1600×900 の枠）</returns>
    static OutputRect ComputeOutputRect(int32_t clientWidth, int32_t clientHeight);

    /// <summary>
    /// ウィンドウプロシージャ。ImGui に先に渡し、Alt+Enter / F11 でフルスクリーンを切り替え、
    /// WM_SIZE は新しい大きさを控えるだけにする（作り直しはメインループで ConsumePendingResize から）
    /// </summary>
    /// <param name="hwnd">ウィンドウ</param>
    /// <param name="msg">メッセージ</param>
    /// <param name="wparam">メッセージの引数</param>
    /// <param name="lparam">メッセージの引数</param>
    /// <returns>メッセージの処理結果</returns>
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    /// <summary>ウィンドウクラスを登録してクライアント領域 1600×900 のウィンドウを作り、表示する</summary>
    /// <param name="title">タイトルバーに出す文字列</param>
    /// <param name="iconResourceId">
    /// exe に埋め込んだ ICON リソースの ID。0 なら Windows の既定アイコンを使う。
    /// タイトルバーと Alt+Tab の表示に反映される。
    /// （エクスプローラーやタスクバーに出る exe 自体のアイコンは、
    ///   .rc に ICON を書いてビルドした時点で Windows が自動的に拾うので、
    ///   こちらの指定とは独立している）
    /// </param>
    void Initialize(const wchar_t* title, int iconResourceId = 0);

    /// <summary>溜まったウィンドウメッセージをすべて処理する</summary>
    /// <returns>WM_QUIT を受け取ったら false（＝終了）、それ以外は true</returns>
    bool ProcessMessage();

    /// <summary>ウィンドウを閉じる（COM の終了は Framework::Finalize の最後で行う）</summary>
    void Finalize();

    /// <summary>ウィンドウハンドル</summary>
    /// <returns>ウィンドウハンドル（Initialize 前は nullptr）</returns>
    HWND GetHwnd() const { return hwnd_; }
    /// <summary>exe のインスタンスハンドル</summary>
    /// <returns>インスタンスハンドル</returns>
    HINSTANCE GetInstanceHandle() const { return wc_.hInstance; }

    /// <summary>現在のクライアント領域の幅（リサイズ後の実サイズ）</summary>
    /// <returns>幅 [px]</returns>
    int32_t GetClientWidth() const { return clientWidth_; }
    /// <summary>現在のクライアント領域の高さ（リサイズ後の実サイズ）</summary>
    /// <returns>高さ [px]</returns>
    int32_t GetClientHeight() const { return clientHeight_; }

    /// <summary>
    /// 直近の WM_SIZE で受け取った新サイズを取り出し、内部フラグをクリアする。
    /// 主にメインループで dxCore_->Resize() を呼ぶトリガに使う
    /// </summary>
    /// <param name="width">新しい幅 [px] を書く先（true の時だけ書く）</param>
    /// <param name="height">新しい高さ [px] を書く先（true の時だけ書く）</param>
    /// <returns>新サイズがあれば true</returns>
    bool ConsumePendingResize(int32_t& width, int32_t& height);

    /// <summary>
    /// 枠なしフルスクリーン（WS_POPUP でモニターいっぱい）とウィンドウを切り替える。
    /// DXGI の排他フルスクリーンは使わない（Alt+Enter は DirectXCore で DXGI から外してある）。
    /// 大きさが変わると WM_SIZE → ConsumePendingResize の流れでスワップチェーンが作り直される。
    /// Alt+Enter と F11 でも切り替わる（WindowProc）。ウィンドウに戻す時は元のスタイルと位置（最大化も）に戻す
    /// </summary>
    /// <param name="fullscreen">フルスクリーンにするなら true</param>
    void SetFullscreen(bool fullscreen);
    /// <summary>フルスクリーンとウィンドウを入れ替える</summary>
    void ToggleFullscreen() { SetFullscreen(!fullscreen_); }
    /// <summary>枠なしフルスクリーン中か</summary>
    /// <returns>フルスクリーンなら true</returns>
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

