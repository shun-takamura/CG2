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
    /// <summary>依存を受け取り、初期サイズ 640×480 のウィンドウを作る（RT は Initialize で作る）</summary>
    /// <param name="dxCore">デバイス・コマンドリスト・深度の取得元</param>
    /// <param name="srvManager">プレビュー RT の SRV を確保する先</param>
    /// <param name="selection">エディタの選択状態（ImGuiManager が実装）</param>
    EffectEditorWindow(DirectXCore* dxCore, SRVManager* srvManager, IEditorSelection* selection);
    /// <summary>Finalize で RT と PostEffect を解放する</summary>
    ~EffectEditorWindow() override;

    /// <summary>プレビュー用カメラの初期化・512×512 の RT 作成・空のエフェクトで編集を始める（PostEffect は遅延生成）</summary>
    void Initialize();
    /// <summary>PostEffect とすべてのプレビュー RT（破棄待ちも含む）を解放する</summary>
    void Finalize();

    /// <summary>
    /// プレビュー RT への描画パス。Scene 描画後・ImGui::Render の前に呼ぶ。
    /// </summary>
    void Render();

    /// <summary>プレビュー用のカメラを返す</summary>
    /// <returns>エディタ所有のカメラ</returns>
    Camera* GetCamera() { return &camera_; }

    // ===== editBuffer アクセス（Editable から呼ばれる） =====
    /// <summary>編集中のエフェクト定義を返す</summary>
    /// <returns>editBuffer</returns>
    EffectDef&       GetEditBuffer()       { return editBuffer_; }
    /// <summary>編集中のエフェクト定義を返す（読み取り専用）</summary>
    /// <returns>editBuffer</returns>
    const EffectDef& GetEditBuffer() const { return editBuffer_; }
    /// <summary>editBuffer を未保存の変更ありにする</summary>
    void MarkEditBufferDirty() { editBufferDirty_ = true; }

    // ===== コンポーネント操作 =====
    /// <summary>既定値のコンポーネントを末尾に追加して Editable を作り直す</summary>
    /// <param name="kind">追加する種類</param>
    /// <param name="meshType">Primitive の時の形状（0=Plane..7=Lightning）。他の種類では無視</param>
    void AddComponent(EffectComponentEditable::Kind kind, int meshType = 0);
    /// <summary>削除を予約する。即時削除ではなく、次の OnDraw 冒頭で削除する（OnImGuiInspector 中の self-destroy 回避）</summary>
    /// <param name="kind">削除する種類</param>
    /// <param name="index">種類ごとの配列内の位置</param>
    void RemoveComponent(EffectComponentEditable::Kind kind, int index);
    /// <summary>指定コンポーネントの複製を末尾に追加する予約をする（次フレーム冒頭で処理。Particle はグループ名をユニーク化）</summary>
    /// <param name="kind">複製する種類</param>
    /// <param name="index">種類ごとの配列内の位置</param>
    void DuplicateComponent(EffectComponentEditable::Kind kind, int index);

    /// <summary>Editable 一覧を返す（EffectHierarchyWindow から参照される）</summary>
    /// <returns>editBuffer のコンポーネントごとの Editable</returns>
    const std::vector<std::unique_ptr<EffectComponentEditable>>& GetEditables() const { return editables_; }

    /// <summary>編集中のエフェクト名を返す</summary>
    /// <returns>editBuffer の name</returns>
    const std::string& GetCurrentName() const { return editBuffer_.name; }

protected:
    /// <summary>保留中の削除・複製を処理し、定義選択/保存/再生/シークの UI とプレビュー画像を描く（Debug だけ）</summary>
    void OnDraw() override;

private:
    /// <summary>サイズが変わった時だけプレビュー用 RT 一式を作り直す。古い RT は GPU 参照中の可能性があるので次の Render 冒頭で解放する</summary>
    /// <param name="width">RT の幅 [px]</param>
    /// <param name="height">RT の高さ [px]</param>
    void EnsureRenderTextureSize(uint32_t width, uint32_t height);
    /// <summary>自前 PostEffect を遅延生成する（初回利用時。起動時は TextureManager 未準備でクラッシュするため）</summary>
    void EnsurePostEffect();
    /// <summary>プレビュー上にマウスがある時、左ドラッグで周回・中ドラッグで平行移動・ホイールで寄り引き</summary>
    void HandleMouseInput();
    /// <summary>プレビューパスの LineRenderer に XZ 平面のグリッド（±10m、1m 刻み、軸は色付き）を積む</summary>
    void AddGridLines();

    /// <summary>登録済みの定義を editBuffer にコピーして編集を始める。見つからなければ新規にする</summary>
    /// <param name="defName">EffectManager に登録された定義名</param>
    void LoadIntoBuffer(const std::string& defName);
    /// <summary>"Untitled"・長さ 1 秒の空の定義で editBuffer を置き換える</summary>
    void NewEffect();
    /// <summary>名前欄の名前で保存する。編集元と同じ名前なら上書き、違えば衝突時に EffectManager がサフィックスを付ける</summary>
    void SaveCurrent();
    /// <summary>editBuffer の全コンポーネント分の Editable を作り直し、選択中だった kind+index を選び直す</summary>
    void RebuildEditables();

    DirectXCore* dxCore_ = nullptr;
    SRVManager*  srvManager_ = nullptr;
    // エディタ選択状態の抽象（ImGuiManager が実装）。ImGuiManager 具象型への依存を断つため注入。
    IEditorSelection* selection_ = nullptr;

    // エディタ自前の PostEffect（distortion プレビュー合成に使う。Game への依存を断つため自前所有）。
    std::unique_ptr<PostEffect> postEffect_;
    // プレビューに歪みを適用するか（自前化により Game の PostEffect 設定に依存しない）。
    bool previewDistortion_ = true;
    // プレビューにブルームを適用するか
    bool previewBloom_ = true;

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

    // Bloom プレビュー用：発光 RT（float）と、ブルーム合成後の最終表示先
    std::unique_ptr<RenderTexture> previewBloomRT_;
    std::unique_ptr<RenderTexture> previewBloomOutputRT_;
    std::unique_ptr<RenderTexture> oldPreviewBloomRT_;
    std::unique_ptr<RenderTexture> oldPreviewBloomOutputRT_;
    // 直近の Render() でブルーム合成までやったか
    bool lastFrameBloomApplied_ = false;

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

    // ===== プレビューで動かす（弾の尾・ビームの確認用） =====
    // 動き方（0=止まる / 1=直線（範囲まで進んだら撃ち直す） / 2=円を回る）
    int   previewMotion_ = 0;
    float previewMotionSpeed_ = 30.0f;    // 速さ [m/s]（650 のような実際の弾速も入れられる）
    float previewMotionRange_ = 20.0f;    // 直線：進む距離 / 円：半径 [m]
    float previewMotionYawDeg_ = 0.0f;    // 向き（直線の進む向き・ビームの向き）[度]
    float previewMotionPitchDeg_ = 0.0f;
    float previewMotionTravel_ = 0.0f;    // 直線：進んだ距離 / 円：回った角度
    float previewBeamLength_ = -1.0f;     // ビームの長さ [m]（負なら Ribbon の Default Length）

    /// <summary>プレビューの今の位置と向きを動き方から決め、EffectManager に渡す</summary>
    /// <param name="dt">プレビューの経過時間 [秒]</param>
    void UpdatePreviewMotion(float dt);
    /// <summary>プレビューで動かす欄の ImGui</summary>
    void DrawPreviewMotionUI();
    Vector3 previewPos_{ 0.0f, 0.0f, 0.0f };     // 動かした後の位置
    Vector3 previewRotate_{ 0.0f, 0.0f, 0.0f };  // 動かした後の向き（オイラー角）

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
