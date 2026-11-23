#pragma once
#include "IImGuiWindow.h"

// 前方宣言
class RenderTexture;
class SRVManager;

/// <summary>
/// ビューポートウィンドウ（3Dシーンを表示）
/// </summary>
class ViewportWindow : public IImGuiWindow {
public:
    /// <summary>"Scene" ウィンドウとして作る（初めの大きさ 960×540）</summary>
    /// <param name="srvManager">ゲーム画像のテクスチャを引く SRVManager</param>
    ViewportWindow(SRVManager* srvManager)
        : IImGuiWindow("Scene"), srvManager_(srvManager) {
        SetInitialSize(ImVec2(kInitialWidth, kInitialHeight));  // 16:9 の初期サイズ
    }

    /// <summary>
    /// 表示するゲーム画像の RenderTexture をセットする
    /// </summary>
    /// <param name="renderTexture">ゲーム画像（nullptr なら何も映さない）</param>
    void SetRenderTexture(RenderTexture* renderTexture) { renderTexture_ = renderTexture; }

    /// <summary>
    /// 描画されたゲーム画像のスクリーン上の位置（左上）
    /// </summary>
    /// <returns>左上の位置 [px]</returns>
    ImVec2 GetImageScreenPos() const { return imageScreenPos_; }

    /// <summary>
    /// 描画されたゲーム画像のサイズ
    /// </summary>
    /// <returns>大きさ [px]</returns>
    ImVec2 GetImageScreenSize() const { return imageScreenSize_; }

    /// <summary>
    /// 画像領域内にマウスがあるか
    /// </summary>
    /// <returns>マウスがあれば true</returns>
    bool IsHovered() const { return isHovered_; }

protected:
    /// <summary>
    /// 再生/停止ボタンと、縦横比を保ったゲーム画像を出す。
    /// アセットブラウザからのドロップを受け付けてシーンに配置する（3D は Y=0 平面に 0.5 刻みでスナップ）
    /// </summary>
    void OnDraw() override;

private:
    // ウィンドウの初めの大きさ [px]（16:9）
    static constexpr int kInitialWidth = 960;
    static constexpr int kInitialHeight = 540;

    RenderTexture* renderTexture_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // ゲーム画像のスクリーン上の位置とサイズ（Gizmo描画で使用）
    ImVec2 imageScreenPos_ = ImVec2(0, 0);
    ImVec2 imageScreenSize_ = ImVec2(0, 0);
    bool isHovered_ = false;
};
