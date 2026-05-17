#pragma once
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
};

/// <summary>
/// Particle コンポーネント
/// 指定した GPU Particle グループへバースト発射する。
/// </summary>
struct EffectParticleComponent {
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
};

/// <summary>
/// Light コンポーネント
/// PointLight / SpotLight のどちらかを動的に確保して使う。
/// </summary>
struct EffectLightComponent {
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
/// エフェクト定義（1ファイル=1 EffectDef）
/// </summary>
struct EffectDef {
    std::string name;
    float       totalDuration = 1.0f;

    std::vector<EffectPrimitiveComponent> primitives;
    std::vector<EffectParticleComponent>  particles;
    std::vector<EffectLightComponent>     lights;
};

namespace EffectDefIO {
    /// <summary>
    /// 1ファイルからEffectDefを読み込む。失敗時はfalse。
    /// 未指定フィールドは EffectDef のデフォルト値を維持。
    /// </summary>
    bool LoadFromFile(const std::string& filePath, EffectDef& out);
}
