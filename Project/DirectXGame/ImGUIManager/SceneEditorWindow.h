#pragma once
#include "IImGuiWindow.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

// 前方宣言
class ImGuiManager;

// ============================================
// ドラッグ&ドロップで運ぶペイロード
// SceneEditorWindow（source）→ ViewportWindow（target）
// ============================================
#define MODEL_DROP_PAYLOAD_TYPE     "MODEL_DROP"
#define SPRITE_DROP_PAYLOAD_TYPE    "SPRITE_DROP"
#define ANIMATED_DROP_PAYLOAD_TYPE  "ANIMATED_DROP"
#define PRIMITIVE_DROP_PAYLOAD_TYPE "PRIMITIVE_DROP"
#define MATERIAL_DROP_PAYLOAD_TYPE  "MATERIAL_DROP"
#define PREFAB_DROP_PAYLOAD_TYPE    "PREFAB_DROP"
#define ANIM_DROP_PAYLOAD_TYPE      "ANIM_DROP"
// Effect Editor 用：エフェクトのコンポーネント種別だけを運ぶ
#define EFFECT_COMP_DROP_PAYLOAD_TYPE "EFFECT_COMP_DROP"
// SceneEditor のエフェクト一覧から運ぶリソース名
#define EFFECT_RES_DROP_PAYLOAD_TYPE "EFFECT_RES_DROP"

struct ModelDropPayload {
    char dirPath[256];
    char filename[128];
};

// テクスチャパスをそのまま運ぶ
struct SpriteDropPayload {
    char texturePath[384];
};

// アニメーションモデル（dirPath + filename + 拡張子）
struct AnimatedDropPayload {
    char dirPath[256];
    char filename[128];
};

// プリミティブ（PrimitiveInstance::PrimitiveType を int で運ぶ）
struct PrimitiveDropPayload {
    int primitiveType;
};

// マテリアル（.mat ファイルパス）
struct MaterialDropPayload {
    char materialPath[384];
};

// アニメーション（.anim ファイルパス）
struct AnimDropPayload {
    char animPath[384];
};

// プリファブ名
struct PrefabDropPayload {
    char prefabName[128];
};

// Effect Component（種別だけ運ぶ。0=Primitive, 1=Particle, 2=Light, 3=Sound）
struct EffectComponentDropPayload {
    int kind;
};

// エフェクト名（EffectManager に登録されたエフェクトの name）
struct EffectResDropPayload {
    char effectName[128];
};

/// <summary>
/// シーンエディタウィンドウ
/// Resources/Models 配下を非同期スキャン + Assimp パースし、
/// シーンに動的にオブジェクトを追加・削除する
/// </summary>
class SceneEditorWindow : public IImGuiWindow {
public:
    explicit SceneEditorWindow(ImGuiManager* manager);
    ~SceneEditorWindow() override;

protected:
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
    mutable std::mutex discoveredMutex_;

    std::thread workerThread_;
    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> scanDone_{ false };

    char searchBuf_[256] = {};

    /// <summary>
    /// バックグラウンドスレッドの処理
    /// 1. Resources/Models / Resources/Textures / Resources/Models/Animated をスキャン
    /// 2. .obj は ModelManager::PreloadCPU を呼んで先読み
    ///    （.png / .gltf は表示のみ。実体はドロップ時にロード）
    /// </summary>
    void WorkerFunc();

    /// <summary>
    /// Resources/Json/Effects ディレクトリを毎フレ簡易監視。
    /// ディレクトリの最終書込時刻が変化していたら、EffectManager に再ロードを依頼し、
    /// discoveredEffects_ も更新する（メインスレッド呼び出し前提）。
    /// </summary>
    void RefreshEffectsIfChanged();
};
