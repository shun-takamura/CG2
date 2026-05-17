#pragma once
#include "IImGuiWindow.h"
#include "Camera.h"
#include "DebugCamera.h"
#include <memory>
#include <string>

class DirectXCore;
class SRVManager;
class RenderTexture;

/// <summary>
/// Effect Editor 用ウィンドウ。
/// 内部に RenderTexture と Orbit/Pan/Zoom 可能なカメラを持ち、
/// 登録済みエフェクトをプレビュー再生・編集する。
/// </summary>
class EffectEditorWindow : public IImGuiWindow {
public:
    EffectEditorWindow(DirectXCore* dxCore, SRVManager* srvManager);
    ~EffectEditorWindow() override;

    void Initialize();
    void Finalize();

    /// <summary>
    /// プレビュー RT への描画パス。Scene 描画後・ImGui::Render の前に呼ぶ。
    /// </summary>
    void Render();

    /// <summary>
    /// エンジン API（EffectManager 等）が使えるよう、DebugCamera を反映済みの Camera を返す。
    /// </summary>
    Camera* GetCamera() { return &camera_; }

protected:
    void OnDraw() override;

private:
    // 必要に応じて RT を再生成（リサイズ対応）
    void EnsureRenderTextureSize(uint32_t width, uint32_t height);

    // マウス入力をカメラに反映
    void HandleMouseInput();

    // グリッド線をLineRendererに積む
    void AddGridLines();

    DirectXCore* dxCore_ = nullptr;
    SRVManager*  srvManager_ = nullptr;

    std::unique_ptr<RenderTexture> renderTexture_;
    uint32_t rtWidth_  = 0;
    uint32_t rtHeight_ = 0;

    // ImGui 側で計測した「望ましいサイズ」。Render() 冒頭で反映する。
    uint32_t pendingWidth_  = 0;
    uint32_t pendingHeight_ = 0;

    // 前フレームから持ち越した古い RT。Render() 冒頭で安全に解放する。
    std::unique_ptr<RenderTexture> oldRenderTexture_;

    DebugCamera debugCamera_;
    Camera      camera_;     // DebugCamera から行列を注入したものをエンジン API に渡す

    // ImGui::Image の表示状態
    ImVec2 imageScreenPos_  = { 0.0f, 0.0f };
    ImVec2 imageScreenSize_ = { 0.0f, 0.0f };
    bool   isHovered_       = false;

    // 再生制御
    std::string selectedEffect_;
    float playPos_[3] = { 0.0f, 0.0f, 0.0f };
};
