#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "BillboardMode.h"
#include "PrimitiveGenerator.h"  // RingParams / CylinderParams / HelixParams
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
/// アニメーションカーブ（イージング）。
/// 横軸 x=正規化時間(0..1)、縦軸 y=出力(0..1)。制御点は x 昇順、x/y は 0..1。
/// 既定は (0,0)-(1,1) の直線＝恒等（enabled=false なら線形そのまま）。
/// </summary>
struct EffectCurve {
    bool                 enabled = false;
    std::vector<Vector2> points  = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };

    // t(0..1) を区分線形で評価して返す。enabled=false や点が不足なら t をそのまま返す。
    float Evaluate(float t) const;
};

/// <summary>
/// Primitive コンポーネント
/// 単発の幾何描画。lifetime 中に scale/color を補間する。
/// </summary>
struct EffectPrimitiveComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Primitive"）
    std::string displayName;

    // 形状（PrimitiveInstance::PrimitiveType の値と互換。Plane=0, Box=1, Sphere=2, Ring=3, Cylinder=4, Helix=5, Beam=6, Lightning=7, Hemisphere=8, Frame=9）
    int meshType = 0;

    // ----- 形状ごとのジオメトリパラメータ（meshType が該当する場合のみ使用。PrimitiveInstance と同等） -----
    PrimitiveGenerator::RingParams         ringParams;
    PrimitiveGenerator::CylinderParams     cylinderParams;
    PrimitiveGenerator::HelixParams        helixParams;
    PrimitiveGenerator::BeamParams         beamParams;
    PrimitiveGenerator::LightningBoltParams lightningParams;
    PrimitiveGenerator::FrameParams        frameParams;

    // エフェクト中心からのオフセット
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
    Vector3 rotate = { 0.0f, 0.0f, 0.0f }; // ローカル基準回転（degreeではなくradian）

    // ----- 回転アニメーション（内部はクオータニオンで合成。ジンバルロック回避） -----
    // 出現時ランダム回転：spawn 時に各軸 ±randomRotateRange の範囲で1回サンプルし、基準 rotate に加える。
    bool    randomRotateOnSpawn = false;
    Vector3 randomRotateRange   = { 0.0f, 0.0f, 0.0f }; // 各軸の最大ランダム角（radian）
    // 持続回転：lifetime 中、角速度ベクトル rotateSpeed を毎フレーム積分して回し続ける（rad/s）。
    Vector3 rotateSpeed = { 0.0f, 0.0f, 0.0f };

    // 時間制御（エフェクト再生開始からの絶対秒）
    float startTime = 0.0f;
    float lifetime  = 1.0f;

    // スケールと色の補間
    Vector3 startScale = { 1.0f, 1.0f, 1.0f };
    Vector3 endScale   = { 1.0f, 1.0f, 1.0f };
    Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor   = { 1.0f, 1.0f, 1.0f, 0.0f };

    // Scale 補間のイージングカーブ（enabled 時、t を scaleCurve で再マップしてから Lerp）
    EffectCurve scaleCurve;

    // ----- 位置アニメ（StartPos→EndPos を時間で補間。Plane 等を動かす用途） -----
    // usePositionAnim が true のとき offset の代わりに Lerp(startPos,endPos, posCurve(t)) を使う。
    bool        usePositionAnim = false;
    Vector3     startPos = { 0.0f, 0.0f, 0.0f };
    Vector3     endPos   = { 0.0f, 0.0f, 0.0f };
    EffectCurve posCurve;

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
    float viewAngleFadePower = 0.0f; // 0=無効、雷/レーザーで斜め面をフェードさせたい時 2〜4 推奨

    // ----- UV 設定 -----
    bool    uvAutoScroll = false;
    Vector2 uvScrollSpeed = { 0.0f, 0.0f }; // 1秒あたりのスクロール量
    Vector2 uvOffset      = { 0.0f, 0.0f };
    Vector2 uvScale       = { 1.0f, 1.0f };
    bool    uvFlipU = false;
    bool    uvFlipV = false;

    // ----- Distortion（歪みエフェクト） -----
    // useDistortion が true のときだけ、distortionRT に DistortionMesh.PS でこのプリミティブを描き込む。
    // distortionTexturePath はノーマルマップ風 RG テクスチャ（指定が無ければ歪まない）。
    bool        useDistortion        = false;
    std::string distortionTexturePath;
    float       distortionStrength      = 0.5f; // 0..1。per-instance の歪み強度（texture.a × vertex.color.a と乗算）
    bool        distortionUvAutoScroll  = false;
    Vector2     distortionUvScrollSpeed = { 0.0f, 0.0f };
    Vector2     distortionUvOffset      = { 0.0f, 0.0f };
    Vector2     distortionUvScale       = { 1.0f, 1.0f };
    bool        distortionUvFlipU = false;
    bool        distortionUvFlipV = false;

    // ----- Dissolve（オブジェクト単位のディゾルブ） -----
    // useDissolve かつ dissolveMaskPath が指定されているとき、マスク(グレースケール)を読み、
    // mask.r < threshold のピクセルを discard する（threshold: 0=完全表示 / 1=完全に消えた）。
    // threshold は In（出現:1→0）と Out（消滅:0→1）の max で時間合成する。
    bool        useDissolve = false;       // マスター（マスクをバインドするか）
    std::string dissolveMaskPath;          // In/Out で共用するマスク
    // 出現（In）：コンポーネント出現から InStartTime 後、InDuration 秒かけて threshold を 1→0（ディゾルブで出てくる）
    bool        dissolveInEnable    = false;
    float       dissolveInStartTime = 0.0f;
    float       dissolveInDuration  = 0.5f;
    // 消滅（Out）：OutStartTime 後、OutDuration 秒かけて threshold を 0→1（ディゾルブで消えていく）
    bool        dissolveOutEnable    = false;
    float       dissolveOutStartTime = 0.0f;
    float       dissolveOutDuration  = 0.5f;
    // ディゾルブ進行度(0..1)のイージングカーブ（In/Out 共用。enabled 時に progress を再マップ）
    EffectCurve dissolveCurve;
    // アウトライン（燃えるエッジ）：In/Out 共用。閾値近傍の帯を発光色で塗る。
    bool        dissolveEdgeEnable = false;
    Vector4     dissolveEdgeColor  = { 1.0f, 0.4f, 0.1f, 1.0f };
    float       dissolveEdgeWidth  = 0.05f;
};

/// <summary>
/// グラデーションの色キー（位置 0..1 と色）。Fixed カラーモードで複数置くと多色補間になる。
/// </summary>
struct EffectColorKey {
    float   location = 0.0f;                     // 0..1
    Vector4 color    = { 1.0f, 1.0f, 1.0f, 1.0f };
};

/// <summary>
/// Particle コンポーネント
/// 指定した GPU Particle グループへバースト発射する。
/// </summary>
struct EffectParticleComponent {
    // Inspector / Hierarchy で表示する名前（空ならデフォルト "Particle"）
    std::string displayName;

    // 参照する GPU Particle グループ名（GPUParticleManager の登録名）。
    // 未登録の名前なら再生時に texturePath で自動生成する。
    std::string gpuParticleGroupName;

    // グループ自動生成時に使うテクスチャ（既存グループがある場合は無視される）
    std::string texturePath = "Resources/Textures/circle.dds";

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
    // Fixed カラーモードで使う多色グラデーション。2個以上で start/end の代わりに多色補間。
    // 空 or 1個なら従来どおり startColor→endColor の2色補間。
    std::vector<EffectColorKey> colorKeys;

    // ----- スケール（粒子サイズ） -----
    Vector2 scaleMin = { 0.1f, 0.1f }; // 最小サイズ（幅, 高さ）
    Vector2 scaleMax = { 0.5f, 0.5f }; // 最大サイズ（幅, 高さ）
    bool    uniformScale = true;        // true なら 幅=高さ を強制（Xレンジを共用）
    // 寿命に沿ったサイズ倍率（Size over Lifetime）。発生時ランダムサイズ × lerp(startScale, endScale, 寿命比率)。
    // 1.0/1.0 で従来どおり変化なし。
    float   startScale = 1.0f;
    float   endScale   = 1.0f;

    // ----- 発生 -----
    float   emitRadius = 0.5f;          // 発生位置の散らばり半径（Ring では円の半径）
    float   particleLifeTime = 1.0f;    // 各粒子の寿命（秒）
    int     emitShape = 0;              // 0=Sphere / 1=Ring（円周）
    Vector3 ringNormal = { 0.0f, 0.0f, 1.0f }; // Ring 平面の法線
    float   ringThickness = 0.05f;      // Ring の太さ（円周まわりの散らばり）

    // ----- 初速制御 -----
    int     velocityMode = 0;           // 0=全方向ランダム / 1=方向固定 / 2=放射(中心から外) / 3=接線(公転の初速)
    Vector3 velocityDir = { 0.0f, 1.0f, 0.0f }; // mode=1 の向き
    float   velocitySpeed = 3.0f;       // mode=1/2/3 の初速の大きさ
    float   velocityJitter = 0.0f;      // 速度ゆらぎ量

    // ----- 回転（3D姿勢）：ビルボード None のとき板を3D回転させる（破片のようなタンブル）-----
    // 内部はクオータニオン。Full/YAxis ビルボード時は無視（常にカメラを向く）。
    bool    randomRotateOnSpawn = false;             // 出現時に各軸 ±範囲のランダム初期姿勢
    Vector3 randomRotateRange   = { 0.0f, 0.0f, 0.0f }; // 各軸の最大ランダム角（radian）
    Vector3 rotateSpeed         = { 0.0f, 0.0f, 0.0f }; // 各軸の角速度（rad/s）。出ている間回し続ける

    // ----- 周回（orbit）：粒子を中心まわりに回し続ける（外に出ない）-----
    // spin=帯上を流れる（リング法線軸）/ tumble=帯自体の回転（別軸）。両方を粒子が受ける。
    bool    orbitEnabled    = false;
    float   orbitSpinSpeed  = 1.0f;     // 帯上を流れる速度 rad/s（軸＝Ring Normal）
    float   orbitTumbleSpeed = 0.0f;    // 帯自体の回転速度 rad/s
    Vector3 orbitTumbleAxis  = { 0.0f, 1.0f, 0.0f }; // 帯回転の軸

    // ----- Dissolve（粒子ごとの寿命ディゾルブ） -----
    // 各粒子が自分の寿命比率(0..1)に応じて、In(出現:[0,inEnd]) / Out(消滅:[outStart,1]) で
    // マスクを discard する。useDissolve かつ dissolveMaskPath 指定で有効。
    bool        useDissolve = false;
    std::string dissolveMaskPath;
    bool        dissolveInEnable = false;
    float       dissolveInEnd    = 0.3f;  // 寿命比率: ここまでに出現完了
    bool        dissolveOutEnable = false;
    float       dissolveOutStart  = 0.7f; // 寿命比率: ここから消え始める
    // アウトライン（燃えるエッジ）
    bool        dissolveEdgeEnable = false;
    Vector4     dissolveEdgeColor  = { 1.0f, 0.4f, 0.1f, 1.0f };
    float       dissolveEdgeWidth  = 0.05f;
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

    // true ならエフェクト終了時に自動的に最初から再生し直す（弾丸の trail 等）。
    // 寿命を外から制御したい場合は EffectManager::Stop(handle) で止める。
    bool        loop = false;

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
