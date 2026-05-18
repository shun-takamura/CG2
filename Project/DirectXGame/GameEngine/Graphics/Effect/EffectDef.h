#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "BillboardMode.h"
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// エフェクト1コンポーネントの種別
/// </summary>
enum class EffectComponentType {
    Primitive,
    Particle,
    Light,
};

/// <summary>
/// LightComponent の種別
/// </summary>
enum class EffectLightKind {
    Point,
    Spot,
};

/// <summary>
/// Primitive コンポーネント
/// 単発の幾何描画。lifetime 中に scale/color を補間する。
/// </summary>
struct EffectPrimitiveComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Primitive"）
    std::string displayName;

    // 形状（PrimitiveInstance::PrimitiveType の値と互換。Plane=0, Box=1, Sphere=2, Ring=3, Cylinder=4, Helix=5）
    int meshType = 0;

    // エフェクト中心からのオフセット
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
    Vector3 rotate = { 0.0f, 0.0f, 0.0f }; // ローカル回転（degreeではなくradian）

    // 時間制御（エフェクト再生開始からの絶対秒）
    float startTime = 0.0f;
    float lifetime  = 1.0f;

    // スケールと色の補間
    Vector3 startScale = { 1.0f, 1.0f, 1.0f };
    Vector3 endScale   = { 1.0f, 1.0f, 1.0f };
    Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor   = { 1.0f, 1.0f, 1.0f, 0.0f };

    // テクスチャ（空文字は white1x1 扱い）
    std::string texturePath;

    // ブレンドモード（PrimitivePipeline::BlendMode と互換。None=0, Normal=1, Add=2, Subtract=3, Multiply=4, Screen=5）
    int blendMode = 2; // Add

    // ビルボード
    BillboardMode billboardMode = BillboardMode::Full;

    // ----- 静的マテリアル設定（PrimitiveInstance Inspector と同じ項目） -----
    bool  depthWrite = false;
    float alphaReference = 0.0f;
    bool  cullBackface = false;
    int   samplerMode = 0; // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll

    // ----- UV 設定 -----
    bool    uvAutoScroll = false;
    Vector2 uvScrollSpeed = { 0.0f, 0.0f }; // 1秒あたりのスクロール量
    Vector2 uvOffset      = { 0.0f, 0.0f };
    Vector2 uvScale       = { 1.0f, 1.0f };
    bool    uvFlipU = false;
    bool    uvFlipV = false;
};

/// <summary>
/// Particle コンポーネント
/// 指定した GPU Particle グループへバースト発射する。
/// </summary>
struct EffectParticleComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Particle"）
    std::string displayName;

    // 参照する GPU Particle グループ名（GPUParticleManager の登録名）
    std::string gpuParticleGroupName;

    // エフェクト中心からのオフセット
    Vector3 offset = { 0.0f, 0.0f, 0.0f };

    // 時間制御
    float startTime = 0.0f;
    float duration  = 0.3f;     // バースト持続時間（emit 受付期間）

    // 発生数
    uint32_t burstCount = 16;

    // ビルボード
    BillboardMode billboardMode = BillboardMode::Full;

    // ----- 色設定 -----
    // 0=Random（startColor/endColor無視）、1=Fixed（startColor→endColorで補間）
    int     colorMode = 0;
    Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor   = { 1.0f, 1.0f, 1.0f, 0.0f };

    // ----- スケール（粒子サイズ） -----
    Vector2 scaleMin = { 0.1f, 0.1f }; // 最小サイズ（幅, 高さ）
    Vector2 scaleMax = { 0.5f, 0.5f }; // 最大サイズ（幅, 高さ）
    bool    uniformScale = true;        // true なら 幅=高さ を強制（Xレンジを共用）
};

/// <summary>
/// Light コンポーネント
/// PointLight / SpotLight のどちらかを動的に確保して使う。
/// </summary>
struct EffectLightComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Light"）
    std::string displayName;

    EffectLightKind kind = EffectLightKind::Point;

    // エフェクト中心からのオフセット
    Vector3 offset = { 0.0f, 0.0f, 0.0f };

    // Spot の照射方向
    Vector3 direction = { 0.0f, -1.0f, 0.0f };

    // 時間制御
    float startTime = 0.0f;
    float lifetime  = 0.4f;

    // ライト本体
    Vector4 color          = { 1.0f, 1.0f, 1.0f, 1.0f };
    float   startIntensity = 5.0f;
    float   endIntensity   = 0.0f;
    float   range          = 5.0f;

    // Spot 専用
    float spotCosAngle        = 0.5f;
    float spotCosFalloffStart = 0.7f;
};

/// <summary>
/// Sound コンポーネント
/// 指定された SoundManager 登録名の音声を 3D 位置で再生する。
/// </summary>
struct EffectSoundComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Sound"）
    std::string displayName;

    // SoundManager::LoadFile で登録した name キー
    std::string soundName;

    // エフェクト中心からのオフセット
    Vector3 offset = { 0.0f, 0.0f, 0.0f };

    // 時間制御
    float startTime = 0.0f;

    // 3D 減衰スケール（大きいほど遠くまで聴こえる）
    float distanceScale = 20.0f;

    // ボリュームスケール（今は SoundManager の SourceVoice ボリュームを設定する形）
    float volume = 1.0f;
};

/// <summary>
/// エフェクト定義（1ファイル=1 EffectDef）
/// </summary>
struct EffectDef {
    std::string name;
    float       totalDuration = 1.0f;

    std::vector<EffectPrimitiveComponent> primitives;
    std::vector<EffectParticleComponent>  particles;
    std::vector<EffectLightComponent>     lights;
    std::vector<EffectSoundComponent>     sounds;
};

namespace EffectDefIO {
    /// <summary>
    /// 1ファイルからEffectDefを読み込む。失敗時はfalse。
    /// 未指定フィールドは EffectDef のデフォルト値を維持。
    /// </summary>
    bool LoadFromFile(const std::string& filePath, EffectDef& out);

    /// <summary>
    /// EffectDef を JSON ファイルに保存する。失敗時はfalse。
    /// </summary>
    bool SaveToFile(const std::string& filePath, const EffectDef& def);
}
