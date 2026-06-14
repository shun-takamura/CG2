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
class PostEffect;
class IEditorSelection;

/// <summary>
/// Effect Editor 用ウィンドウ。
/// 表示は viewport（プレビュー）と最小限の操作（Effect選択 / Save / Play）。
/// コンポーネント編集は EffectHierarchyWindow → Inspector に集約。
/// </summary>
class EffectEditorWindow : public IImGuiWindow {
public:
    EffectEditorWindow(DirectXCore* dxCore, SRVManager* srvManager, IEditorSelection* selection);
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
    // Primitive を追加する場合は meshType（0=Plane..7=Lightning）で形状を確定する。
    void AddComponent(EffectComponentEditable::Kind kind, int meshType = 0);
    // 即時削除ではなく、次の OnDraw 冒頭で削除する（OnImGuiInspector 中の self-destroy 回避）
    void RemoveComponent(EffectComponentEditable::Kind kind, int index);
    // 指定コンポーネントの複製を末尾に追加（同じく次フレーム冒頭で安全に処理）
    void DuplicateComponent(EffectComponentEditable::Kind kind, int index);

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
    // 自前 PostEffect を遅延生成（初回利用時。起動時は TextureManager 未準備でクラッシュするため）。
    void EnsurePostEffect();
    void HandleMouseInput();
    void AddGridLines();

    void LoadIntoBuffer(const std::string& defName);
    void NewEffect();
    void SaveCurrent();
    void RebuildEditables();

    DirectXCore* dxCore_ = nullptr;
    SRVManager*  srvManager_ = nullptr;
    // エディタ選択状態の抽象（ImGuiManager が実装）。ImGuiManager 具象型への依存を断つため注入。
    IEditorSelection* selection_ = nullptr;

    // エディタ自前の PostEffect（distortion プレビュー合成に使う。Game への依存を断つため自前所有）。
    std::unique_ptr<PostEffect> postEffect_;
    // プレビューに歪みを適用するか（自前化により Game の PostEffect 設定に依存しない）。
    bool previewDistortion_ = true;

    std::unique_ptr<RenderTexture> renderTexture_;
    uint32_t rtWidth_  = 0;
    uint32_t rtHeight_ = 0;

    uint32_t pendingWidth_  = 0;
    uint32_t pendingHeight_ = 0;
    std::unique_ptr<RenderTexture> oldRenderTexture_;

    // Distortion プレビュー用：歪みマップ書き込み先と、最終合成出力先
    std::unique_ptr<RenderTexture> previewDistortionRT_;
    std::unique_ptr<RenderTexture> previewOutputRT_;
    std::unique_ptr<RenderTexture> oldPreviewDistortionRT_;
    std::unique_ptr<RenderTexture> oldPreviewOutputRT_;
    // 直近の Render() で歪み合成までやったか（OnDraw で表示先を切り替えるフラグ）
    bool lastFrameDistortionApplied_ = false;

    DebugCamera debugCamera_;
    Camera      camera_;

    ImVec2 imageScreenPos_  = { 0.0f, 0.0f };
    ImVec2 imageScreenSize_ = { 0.0f, 0.0f };
    bool   isHovered_       = false;

    std::string selectedEffect_;
    float playPos_[3] = { 0.0f, 0.0f, 0.0f };

    // プレビュー再生中のエフェクトハンドル（Timeline をシーン内の他エフェクトと混同しないため）
    uint64_t previewHandle_ = 0;

    // ===== 専用タイムライン（エディタ所有の再生制御） =====
    bool  previewPaused_ = false; // 一時停止（ON で dt=0、編集や姿勢確認はできる）
    float previewSpeed_  = 1.0f;  // 再生速度倍率（unscaled 実 delta に乗算）

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

    // 次フレームに反映する複製要求
    std::vector<PendingRemoval> pendingDuplications_;
};
