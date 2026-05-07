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
    ViewportWindow(SRVManager* srvManager)
        : IImGuiWindow("Viewport"), srvManager_(srvManager) {
        SetInitialSize(ImVec2(960, 540));  // 16:9 の初期サイズ
    }

    /// <summary>
    /// RenderTextureをセット
    /// </summary>
    void SetRenderTexture(RenderTexture* renderTexture) { renderTexture_ = renderTexture; }

    /// <summary>
    /// 描画されたゲーム画像のスクリーン上の位置（左上）
    /// </summary>
    ImVec2 GetImageScreenPos() const { return imageScreenPos_; }

    /// <summary>
    /// 描画されたゲーム画像のサイズ
    /// </summary>
    ImVec2 GetImageScreenSize() const { return imageScreenSize_; }

    /// <summary>
    /// 画像領域内にマウスがあるか
    /// </summary>
    bool IsHovered() const { return isHovered_; }

protected:
    void OnDraw() override;

private:
    RenderTexture* renderTexture_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // ゲーム画像のスクリーン上の位置とサイズ（Gizmo描画で使用）
    ImVec2 imageScreenPos_ = ImVec2(0, 0);
    ImVec2 imageScreenSize_ = ImVec2(0, 0);
    bool isHovered_ = false;
};
