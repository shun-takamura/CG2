#pragma once
#include "IImGuiWindow.h"
#include "Camera.h"
#include "DebugCamera.h"
#include "EffectDef.h"
#include "EffectComponentEditable.h"
#include <memory>
#include <string>
#include <vector>

class DirectXCore;
class SRVManager;
class RenderTexture;

/// <summary>
/// Effect Editor 用ウィンドウ。
/// 表示は viewport（プレビュー）と最小限の操作（Effect選択 / Save / Play）。
/// コンポーネント編集は EffectHierarchyWindow → Inspector に集約。
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

    Camera* GetCamera() { return &camera_; }

    // ===== editBuffer アクセス（Editable から呼ばれる） =====
    EffectDef&       GetEditBuffer()       { return editBuffer_; }
    const EffectDef& GetEditBuffer() const { return editBuffer_; }
    void MarkEditBufferDirty() { editBufferDirty_ = true; }

    // ===== コンポーネント操作 =====
    void AddComponent(EffectComponentEditable::Kind kind);
    // 即時削除ではなく、次の OnDraw 冒頭で削除する（OnImGuiInspector 中の self-destroy 回避）
    void RemoveComponent(EffectComponentEditable::Kind kind, int index);

    // Editable 一覧（EffectHierarchyWindow から参照される）
    const std::vector<std::unique_ptr<EffectComponentEditable>>& GetEditables() const { return editables_; }

    const std::string& GetCurrentName() const { return editBuffer_.name; }

    /// <summary>
    /// シーンを一時停止（ゲームプレイ凍結）すべきか。
    /// チェックON かつ ウィンドウが開いているときだけ true。
    /// 閉じれば自動的に false になり、凍結しっぱなしを防ぐ。
    /// </summary>
    bool IsScenePaused() const { return isOpen_ && pauseScene_; }

protected:
    void OnDraw() override;

private:
    void EnsureRenderTextureSize(uint32_t width, uint32_t height);
    void HandleMouseInput();
    void AddGridLines();

    void LoadIntoBuffer(const std::string& defName);
    void NewEffect();
    void SaveCurrent();
    void RebuildEditables();

    DirectXCore* dxCore_ = nullptr;
    SRVManager*  srvManager_ = nullptr;

    std::unique_ptr<RenderTexture> renderTexture_;
    uint32_t rtWidth_  = 0;
    uint32_t rtHeight_ = 0;

    uint32_t pendingWidth_  = 0;
    uint32_t pendingHeight_ = 0;
    std::unique_ptr<RenderTexture> oldRenderTexture_;

    DebugCamera debugCamera_;
    Camera      camera_;

    ImVec2 imageScreenPos_  = { 0.0f, 0.0f };
    ImVec2 imageScreenSize_ = { 0.0f, 0.0f };
    bool   isHovered_       = false;

    std::string selectedEffect_;
    float playPos_[3] = { 0.0f, 0.0f, 0.0f };

    // ON の間、シーンのゲームプレイを凍結する（エフェクト再生は進める）
    bool pauseScene_ = false;

    // ===== 編集状態 =====
    EffectDef editBuffer_;
    bool editBufferDirty_ = false;
    char editNameInput_[128] = {};

    // editBuffer のコンポーネントごとの Editable
    std::vector<std::unique_ptr<EffectComponentEditable>> editables_;

    // 次フレームに反映する削除要求（self-destroy 回避のため）
    struct PendingRemoval {
        EffectComponentEditable::Kind kind;
        int index;
    };
    std::vector<PendingRemoval> pendingRemovals_;
};
