#pragma once
#include "IImGuiWindow.h"
#include "EditorDropPayload.h"  // D&D ペイロード定義一式（エンジン所有）
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

// 前方宣言
class ImGuiManager;

/// <summary>
/// シーンエディタウィンドウ
/// Resources/Models 配下を非同期スキャン + Assimp パースし、
/// シーンに動的にオブジェクトを追加・削除する
/// </summary>
class SceneEditorWindow : public IImGuiWindow {
public:
    /// <summary>"Scene Editor" ウィンドウとして作り、Debug ではアセットを走査するスレッドを始める</summary>
    /// <param name="manager">アクティブシーンとアセットブラウザの追加セクションを持つ ImGuiManager</param>
    explicit SceneEditorWindow(ImGuiManager* manager);
    /// <summary>走査スレッドに止まるよう伝えて終わるまで待つ</summary>
    ~SceneEditorWindow() override;

protected:
    /// <summary>
    /// アセットブラウザ（モデル・スプライト・マテリアル・プリミティブ・アニメーション・エフェクト）をフォルダのツリーで出し、
    /// 各項目をビューポートへのドラッグ元にする。シーン JSON の保存・読込と、アセット・シーンの変更の監視もここで回す
    /// </summary>
    void OnDraw() override;

private:
    struct ModelEntry {
        std::string displayName;  // "Stage/stage.obj"
        std::string dirPath;      // "Resources/Models/Stage"
        std::string filename;     // "stage.obj"
    };
    struct TextureEntry {
        std::string displayName;  // "white1x1.png"
        std::string filePath;     // "Resources/Textures/white1x1.dds"
    };
    struct AnimatedEntry {
        std::string displayName;  // "Animated/character.gltf"
        std::string dirPath;      // "Resources/Models/Animated"
        std::string filename;     // "character.gltf"
    };
    struct MaterialEntry {
        std::string displayName;  // "Enemy/enemy.mat"
        std::string filePath;     // "Resources/Models/Enemy/enemy.mat"
    };
    struct AnimEntry {
        std::string displayName;  // "Animated/Walk/walk.anim"
        std::string filePath;     // "Resources/Models/Animated/Walk/walk.anim"
    };
    struct EffectEntry {
        std::string displayName;  // "ChargeStage1"（= EffectDef::name）
        std::string filePath;     // "Resources/Json/Effects/ChargeStage1.json"
    };

    ImGuiManager* manager_ = nullptr;

    // 走査・先読みワーカースレッドの結果
    std::vector<ModelEntry> discoveredModels_;
    std::vector<TextureEntry> discoveredTextures_;
    std::vector<AnimatedEntry> discoveredAnimated_;
    std::vector<MaterialEntry> discoveredMaterials_;
    std::vector<AnimEntry> discoveredAnims_;
    std::vector<EffectEntry> discoveredEffects_;
    // Effects ディレクトリの最終変更時刻（ホットリロード判定用）
    std::filesystem::file_time_type effectsLastWriteTime_{};
    bool effectsInitialized_ = false;
    // Models / Textures ディレクトリの最終変更時刻（同上）
    std::filesystem::file_time_type modelsLastWriteTime_{};
    std::filesystem::file_time_type texturesLastWriteTime_{};
    bool modelsWatchInitialized_ = false;
    // 編集対象のシーン JSON（Blender からの書き出し先と揃えておく）とその監視状態
    char scenePathBuf_[256] = "Resources/Json/Scenes/StagePlay.json";
    std::filesystem::file_time_type sceneLastWriteTime_{};
    bool sceneWatchInitialized_ = false;
    bool autoReloadScene_ = true;
    // 自動保存（エディタで動かしたら JSON へ書き戻す。Blender 側の自動同期と対で双方向になる）
    bool autoSaveScene_ = true;
    size_t sceneContentHash_ = 0;   // 保存対象の内容ハッシュ（前回保存/読込時点）
    bool sceneHashInitialized_ = false;
    float sceneDirtyDebounce_ = -1.0f;  // >0 の間デバウンス計時中。0 以下で保存実行
    mutable std::mutex discoveredMutex_;

    std::thread workerThread_;
    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> scanDone_{ false };

    char searchBuf_[256] = {};

    /// <summary>
    /// 走査スレッドの本体。アセットを列挙してから、モデルを CPU で先読みする（初回だけ。失敗は無視する）
    /// </summary>
    void WorkerFunc();

    /// <summary>
    /// Resources/Models・Resources/Textures を列挙して discovered* を差し替える。
    /// GPU に触らずファイル列挙のみ行うので、ワーカースレッドからでもメインスレッドからでも呼べる。
    /// </summary>
    void ScanModelsAndTextures();

    /// <summary>
    /// Resources/Json/Effects ディレクトリを毎フレ簡易監視。
    /// ディレクトリの最終書込時刻が変化していたら、EffectManager に再ロードを依頼し、
    /// discoveredEffects_ も更新する（メインスレッド呼び出し前提）。
    /// </summary>
    void RefreshEffectsIfChanged();

    /// <summary>
    /// Resources/Models・Resources/Textures を毎フレ簡易監視し、変化時だけ再列挙する
    /// （Effects と同じ方式）。クックしたてのアセットを exe 再起動なしで一覧へ出すため。
    /// 注意: ディレクトリの最終書込時刻はエントリの追加/削除でしか動かないので、
    ///       「新しいモデルを足した」は拾えるが「同名ファイルを上書きした」は拾えない。
    ///       後者は Rescan Models ボタンで手動更新する。
    /// </summary>
    void RefreshModelsIfChanged();

    /// <summary>
    /// 編集対象のシーン JSON を毎フレ簡易監視し、書き換わっていたら読み直す。
    /// Blender から Export したら手動操作なしでゲーム画面へ反映されるようにするためのもの。
    /// 自前の Save/Load 直後は MarkSceneFileSynced() で記録時刻を進め、自己トリガを防ぐ。
    /// </summary>
    void RefreshSceneIfChanged();

    /// <summary>
    /// エディタでシーンを編集したら、少し待って（デバウンス）JSON へ自動保存する。
    /// Blender の自動同期と対になって双方向リアルタイム編集を成立させる。
    /// 保存後は MarkSceneFileSynced で自分の書き込みを再読込しない（反響ループ防止）。
    /// </summary>
    /// <param name="dt">経過時間 [秒]（デバウンス 0.3 秒を数える）</param>
    void AutoSaveSceneIfDirty(float dt);

    /// <summary>現在のシーンファイルの更新時刻を「既知」として記録する（自己トリガ抑止）。</summary>
    void MarkSceneFileSynced();

    /// <summary>自動保存の差分基準を現在のシーン内容に合わせる（読込直後に呼び、読込を編集と誤検出しない）。</summary>
    void ResetSceneContentBaseline();
};
