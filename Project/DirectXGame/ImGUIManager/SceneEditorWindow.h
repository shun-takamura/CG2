#pragma once
#include "IImGuiWindow.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

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

    ImGuiManager* manager_ = nullptr;

    // 走査・先読みワーカースレッドの結果
    std::vector<ModelEntry> discoveredModels_;
    std::vector<TextureEntry> discoveredTextures_;
    std::vector<AnimatedEntry> discoveredAnimated_;
    std::vector<MaterialEntry> discoveredMaterials_;
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
};
