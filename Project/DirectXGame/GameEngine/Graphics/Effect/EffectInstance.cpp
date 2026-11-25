#include "EffectInstance.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "PrimitivePipeline.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include "EngineTime.h"
#include "TimeGroup.h"
#include "PepperMacros.h"
#include "Camera.h"
#include "RandomGenerator.h"
#include <algorithm>
#include <random>
#define NOMINMAX
#include<cmath>

namespace {
    // GPU の移動カーブ LUT のサンプル数
    constexpr int kMotionLutSize = 32;
    // 移動カーブ LUT の最後のサンプルの番号（0〜1 に割り当てるための割る数）
    constexpr float kMotionLutLastIndex = 31.0f;
    // 回転軸の長さがこれ未満なら回さない
    constexpr float kMinAxisLength = 1e-5f;
    // 寿命の下限 [秒]（0 割りを防ぐ）
    constexpr float kMinLifetime = 0.0001f;
    // 角速度の大きさがこれ以下なら回転を積まない [rad/s]
    constexpr float kMinAngularSpeed = 1e-6f;
    // ディゾルブの長さの下限 [秒]（0 割りを防ぐ）
    constexpr float kMinDissolveDuration = 0.0001f;
    // 発生半径がこれ以下なら周回半径の比を 1 にする [m]
    constexpr float kMinEmitRadius = 1e-4f;
    // 初速の向きの長さがこれ以下なら真上へ飛ばす
    constexpr float kMinVelocityDirLength = 1e-5f;
    // 色のグラデーションの両端のキー（Start / End）の数
    constexpr size_t kEndpointColorKeyCount = 2;
    // Ribbon の断面の頂点の数（縁・中央・縁）
    constexpr uint32_t kRibbonVertsPerPoint = 3;
    // Ribbon の 1 区間のインデックスの数（断面 3 頂点の間の四角 2 枚 × 三角 2 枚 × 3）
    constexpr uint32_t kRibbonIndicesPerSegment = 12;
    // Ribbon の点の最小数（帯にするには 2 点要る）
    constexpr uint32_t kRibbonMinPoints = 2;
    // Ribbon の点・分割の最大数（頂点バッファを大きくしすぎない）
    constexpr uint32_t kRibbonMaxPoints = 512;
    // 帯を横へ広げる向きが作れないほど短いベクトルの長さ²
    constexpr float kRibbonTinyLengthSq = 1e-10f;
    // 断面の V（縁・中央・縁）
    constexpr float kRibbonEdgeV0 = 0.0f;
    constexpr float kRibbonCenterV = 0.5f;
    constexpr float kRibbonEdgeV1 = 1.0f;
    // 帯の向きが決まらないときに横へ広げる基準の上方向
    const Vector3 kRibbonFallbackUp = { 0.0f, 1.0f, 0.0f };
    const Vector3 kRibbonFallbackSide = { 1.0f, 0.0f, 0.0f };
    // ビームの前（エフェクトのローカル +Z）
    const Vector3 kBeamForward = { 0.0f, 0.0f, 1.0f };
    // smoothstep（3t² − 2t³）の係数
    constexpr float kSmoothstepSquareCoef = 3.0f;
    constexpr float kSmoothstepCubeCoef = 2.0f;
    // ビームの区切りの寄せ方の下限（0 以下だと全部の点が先に寄る）
    constexpr float kMinSegmentPower = 0.1f;

    /// <summary>0〜1 に切り詰める</summary>
    /// <param name="v">値</param>
    /// <returns>切り詰めた値</returns>
    float Saturate(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
    /// <summary>カーブ(0..1)を GPU の移動カーブ LUT 用に 32 サンプルへ焼く（各値は 0〜1 に切り詰める）</summary>
    /// <param name="curve">焼くカーブ</param>
    /// <param name="out">書き込み先（32 要素）</param>
    void BakeCurveLUT(const EffectCurve& curve, float out[kMotionLutSize]) {
        for (int s = 0; s < kMotionLutSize; ++s) {
            out[s] = Saturate(curve.Evaluate(static_cast<float>(s) / kMotionLutLastIndex));
        }
    }
    /// <summary>Vector3 の線形補間</summary>
    /// <param name="a">t=0 の値</param>
    /// <param name="b">t=1 の値</param>
    /// <param name="t">補間係数</param>
    /// <returns>補間した値</returns>
    Vector3 LerpV3(const Vector3& a, const Vector3& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
        };
    }
    /// <summary>Vector4 の線形補間</summary>
    /// <param name="a">t=0 の値</param>
    /// <param name="b">t=1 の値</param>
    /// <param name="t">補間係数</param>
    /// <returns>補間した値</returns>
    Vector4 LerpV4(const Vector4& a, const Vector4& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        };
    }
    /// <summary>float の線形補間</summary>
    /// <param name="a">t=0 の値</param>
    /// <param name="b">t=1 の値</param>
    /// <param name="t">補間係数</param>
    /// <returns>補間した値</returns>
    float LerpF(float a, float b, float t) { return a + (b - a) * t; }

    /// <summary>
    /// 色相回転：輝度軸(1,1,1)まわりに RGB を回す（彩度/明度を保ったまま色相だけ変える）。
    /// GPU 版 UpdateParticle.CS.hlsl の HueRotate と同じ式。
    /// </summary>
    /// <param name="c">元の色（α はそのまま）</param>
    /// <param name="ang">回す角度 [ラジアン]</param>
    /// <returns>色相を回した色</returns>
    Vector4 HueRotate(const Vector4& c, float ang) {
        const float k = 0.57735026f; // 1/sqrt(3)
        const float cosA = std::cos(ang);
        const float sinA = std::sin(ang);
        // cross(k, c) （k=(k,k,k)）
        const float crx = k * c.z - k * c.y;
        const float cry = k * c.x - k * c.z;
        const float crz = k * c.y - k * c.x;
        const float kd = k * (c.x + c.y + c.z); // dot(k, rgb)
        return {
            c.x * cosA + crx * sinA + k * kd * (1.0f - cosA),
            c.y * cosA + cry * sinA + k * kd * (1.0f - cosA),
            c.z * cosA + crz * sinA + k * kd * (1.0f - cosA),
            c.w,
        };
    }

    /// <summary>[-r, r) の一様乱数（見た目用の乱数から引く。同じシードなら同じばらつきになる）</summary>
    /// <param name="r">範囲の半分</param>
    /// <returns>乱数。r が 0 以下なら 0</returns>
    float RandSym(float r) {
        if (r <= 0.0f) return 0.0f;
        return RandomGenerator::GetInstance()->NextVisualFloat(-r, r);
    }

    // オイラー角→クオータニオンは engine math の QuaternionFromEuler を使う（重複解消）。

    /// <summary>axis まわりに v を回す（ロドリゲスの回転公式）</summary>
    /// <param name="v">回すベクトル</param>
    /// <param name="axis">回転軸（正規化しなくてよい）</param>
    /// <param name="ang">回す角度 [ラジアン]</param>
    /// <returns>回したベクトル。軸の長さがほぼ 0 なら v のまま</returns>
    Vector3 RotateAroundAxis(const Vector3& v, const Vector3& axis, float ang) {
        float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (len < kMinAxisLength) return v;
        Vector3 k = { axis.x / len, axis.y / len, axis.z / len };
        float c = std::cos(ang), s = std::sin(ang);
        float kv = k.x * v.x + k.y * v.y + k.z * v.z;
        Vector3 cr = { k.y * v.z - k.z * v.y, k.z * v.x - k.x * v.z, k.x * v.y - k.y * v.x };
        return {
            v.x * c + cr.x * s + k.x * kv * (1.0f - c),
            v.y * c + cr.y * s + k.y * kv * (1.0f - c),
            v.z * c + cr.z * s + k.z * kv * (1.0f - c),
        };
    }

    // ===== SyncFromDef 用：プリミティブの「メッシュ再生成が要る変更か」を判定するための比較 =====
    /// <summary>Vector3 が成分ごとに完全一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool V3Eq(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
    /// <summary>Vector4 が成分ごとに完全一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool V4Eq(const Vector4& a, const Vector4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

    /// <summary>リングの形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool RingEq(const PrimitiveGenerator::RingParams& a, const PrimitiveGenerator::RingParams& b) {
        return a.outerRadius == b.outerRadius && a.innerRadius == b.innerRadius && a.divisions == b.divisions
            && V4Eq(a.innerColor, b.innerColor) && V4Eq(a.outerColor, b.outerColor)
            && a.startAngle == b.startAngle && a.endAngle == b.endAngle
            && a.uvHorizon == b.uvHorizon && a.fadeStart == b.fadeStart && a.fadeEnd == b.fadeEnd
            && a.outerRadiusPerDivision == b.outerRadiusPerDivision;
    }
    /// <summary>円柱の形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool CylEq(const PrimitiveGenerator::CylinderParams& a, const PrimitiveGenerator::CylinderParams& b) {
        return a.topRadius == b.topRadius && a.bottomRadius == b.bottomRadius && a.height == b.height
            && a.divisions == b.divisions && V4Eq(a.topColor, b.topColor) && V4Eq(a.bottomColor, b.bottomColor)
            && a.startAngle == b.startAngle && a.endAngle == b.endAngle;
    }
    /// <summary>らせんの形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool HelixEq(const PrimitiveGenerator::HelixParams& a, const PrimitiveGenerator::HelixParams& b) {
        return a.startHelixRadius == b.startHelixRadius && a.endHelixRadius == b.endHelixRadius
            && a.startTubeRadius == b.startTubeRadius && a.endTubeRadius == b.endTubeRadius
            && a.pitch == b.pitch && a.turns == b.turns
            && a.circleSegments == b.circleSegments && a.lengthSegments == b.lengthSegments
            && V4Eq(a.startColor, b.startColor) && V4Eq(a.endColor, b.endColor);
    }
    /// <summary>ビーム／雷の見た目がすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool AppEq(const PrimitiveGenerator::BeamAppearance& a, const PrimitiveGenerator::BeamAppearance& b) {
        return a.startWidth == b.startWidth && a.endWidth == b.endWidth && a.planeCount == b.planeCount
            && a.fadeStartLength == b.fadeStartLength && a.fadeEndLength == b.fadeEndLength
            && V4Eq(a.startColor, b.startColor) && V4Eq(a.endColor, b.endColor)
            && a.uvWrapByLength == b.uvWrapByLength && a.uvTilesPerUnit == b.uvTilesPerUnit;
    }
    /// <summary>ビームの形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool BeamEq(const PrimitiveGenerator::BeamParams& a, const PrimitiveGenerator::BeamParams& b) {
        return V3Eq(a.startPos, b.startPos) && V3Eq(a.endPos, b.endPos)
            && AppEq(a.appearance, b.appearance) && a.lengthSegments == b.lengthSegments;
    }
    /// <summary>雷の形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool LightningEq(const PrimitiveGenerator::LightningBoltParams& a, const PrimitiveGenerator::LightningBoltParams& b) {
        return V3Eq(a.startPos, b.startPos) && V3Eq(a.endPos, b.endPos) && AppEq(a.appearance, b.appearance)
            && a.generations == b.generations && a.maxOffsetRatio == b.maxOffsetRatio && a.randomSeed == b.randomSeed
            && a.branchProbability == b.branchProbability && a.branchLengthScale == b.branchLengthScale
            && a.branchMaxAngle == b.branchMaxAngle && a.branchWidthScale == b.branchWidthScale
            && a.branchColorScale == b.branchColorScale;
    }

    /// <summary>枠の形状パラメータがすべて一致するか</summary>
    /// <param name="a">比べる値</param>
    /// <param name="b">比べる値</param>
    /// <returns>一致すれば true</returns>
    bool FrameEq(const PrimitiveGenerator::FrameParams& a, const PrimitiveGenerator::FrameParams& b) {
        return a.outerWidth == b.outerWidth && a.outerHeight == b.outerHeight
            && a.innerWidth == b.innerWidth && a.innerHeight == b.innerHeight
            && V4Eq(a.color, b.color);
    }

    /// <summary>
    /// メッシュそのものを作り直す必要がある変更か（meshType / テクスチャ / 該当ジオメトリparams）。
    /// blend / billboard / material / UV / distortion はレンダラの setter でライブ適用できるので含めない。
    /// </summary>
    /// <param name="a">変更前のコンポーネント</param>
    /// <param name="b">変更後のコンポーネント</param>
    /// <returns>作り直しが要れば true</returns>
    bool PrimitiveMeshNeedsRebuild(const EffectPrimitiveComponent& a, const EffectPrimitiveComponent& b) {
        if (a.meshType != b.meshType) return true;
        if (a.texturePath != b.texturePath) return true;
        switch (a.meshType) {
        case EffectMeshType::Ring: return !RingEq(a.ringParams, b.ringParams);
        case EffectMeshType::Cylinder: return !CylEq(a.cylinderParams, b.cylinderParams);
        case EffectMeshType::Helix: return !HelixEq(a.helixParams, b.helixParams);
        case EffectMeshType::Beam: return !BeamEq(a.beamParams, b.beamParams);
        case EffectMeshType::Lightning: return !LightningEq(a.lightningParams, b.lightningParams);
        case EffectMeshType::Frame: return !FrameEq(a.frameParams, b.frameParams);
        default: return false; // Plane/Box/Sphere/Hemisphere は meshType + texture のみ
        }
    }

    /// <summary>
    /// メッシュ再生成不要な静的パラメータ（blend / billboard / material / UV / distortion / dissolve のマスクと縁）を
    /// 既存レンダラへ適用する。spawn 時と SyncFromDef のライブ反映で共用する。
    /// </summary>
    /// <param name="r">適用先のレンダラ</param>
    /// <param name="pc">値の取り出し元のコンポーネント</param>
    void ApplyPrimitiveStaticParams(EffectPrimitiveRenderer& r, const EffectPrimitiveComponent& pc) {
        r.SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(pc.blendMode));
        r.SetBillboardMode(pc.billboardMode);
        r.SetDepthWrite(pc.depthWrite);
        r.SetAlphaReference(pc.alphaReference);
        r.SetCullBackface(pc.cullBackface);
        r.SetSamplerMode(pc.samplerMode);
        r.SetViewAngleFadePower(pc.viewAngleFadePower);
        r.SetUVScroll(pc.uvAutoScroll ? pc.uvScrollSpeed : Vector2{ 0.0f, 0.0f });
        r.SetUVOffset(pc.uvOffset);
        r.SetUVScale(pc.uvScale);
        r.SetUVFlipU(pc.uvFlipU);
        r.SetUVFlipV(pc.uvFlipV);
        // Distortion：useDistortion かつテクスチャ指定があれば反映、なければリセット（空文字で OFF）。
        if (pc.useDistortion && !pc.distortionTexturePath.empty()) {
            r.SetDistortionTexture(pc.distortionTexturePath);
            r.SetDistortionStrength(pc.distortionStrength);
            r.SetDistortionUVScroll(pc.distortionUvAutoScroll ? pc.distortionUvScrollSpeed : Vector2{ 0.0f, 0.0f });
            r.SetDistortionUVOffset(pc.distortionUvOffset);
            r.SetDistortionUVScale(pc.distortionUvScale);
            r.SetDistortionUVFlipU(pc.distortionUvFlipU);
            r.SetDistortionUVFlipV(pc.distortionUvFlipV);
        } else {
            r.SetDistortionTexture("");
        }
        // Dissolve：マスク反映（有効かつパスありのときのみ。閾値は毎フレーム別途 push する）。
        if (pc.useDissolve && !pc.dissolveMaskPath.empty()) {
            r.SetDissolveMask(pc.dissolveMaskPath);
        } else {
            r.SetDissolveMask("");
        }
        // アウトライン（時間非依存なのでここで静的に反映）
        r.SetDissolveEdge(pc.dissolveEdgeEnable, pc.dissolveEdgeColor, pc.dissolveEdgeWidth);
    }

    /// <summary>Vector3 の長さ²</summary>
    /// <param name="v">ベクトル</param>
    /// <returns>長さ²</returns>
    float LengthSq3(const Vector3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
    /// <summary>Vector3 の外積</summary>
    /// <param name="a">左</param>
    /// <param name="b">右</param>
    /// <returns>a × b</returns>
    Vector3 Cross3(const Vector3& a, const Vector3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    /// <summary>Vector3 を正規化する（短すぎれば fallback）</summary>
    /// <param name="v">ベクトル</param>
    /// <param name="fallback">短すぎたときの値</param>
    /// <returns>正規化したベクトル</returns>
    Vector3 Normalized3(const Vector3& v, const Vector3& fallback) {
        const float l2 = LengthSq3(v);
        if (l2 < kRibbonTinyLengthSq) return fallback;
        const float inv = 1.0f / std::sqrt(l2);
        return { v.x * inv, v.y * inv, v.z * inv };
    }
    /// <summary>smoothstep（3t² − 2t³）</summary>
    /// <param name="t">0〜1</param>
    /// <returns>なめらかにした値</returns>
    float Smooth01(float t) {
        t = Saturate(t);
        return t * t * (kSmoothstepSquareCoef - kSmoothstepCubeCoef * t);
    }

    /// <summary>Ribbon の位置 t（0＝先頭〜1＝末尾）の色。Start(0)・中間キー・End(1) を区分線形で補間する</summary>
    /// <param name="rc">Ribbon の定義</param>
    /// <param name="t">帯の上の位置</param>
    /// <returns>色</returns>
    Vector4 EvaluateRibbonColor(const EffectRibbonComponent& rc, float t) {
        t = Saturate(t);
        if (rc.colorKeys.empty()) return LerpV4(rc.startColor, rc.endColor, t);
        float prevLoc = 0.0f;
        Vector4 prevColor = rc.startColor;
        // 中間キーは位置の順に並んでいる前提（エディタは並べ替えて保存しないので、ここで順に探す）
        std::vector<EffectColorKey> keys = rc.colorKeys;
        std::sort(keys.begin(), keys.end(), [](const EffectColorKey& a, const EffectColorKey& b) { return a.location < b.location; });
        for (const auto& k : keys) {
            if (t <= k.location) {
                const float span = k.location - prevLoc;
                const float u = span > kMinLifetime ? (t - prevLoc) / span : 1.0f;
                return LerpV4(prevColor, k.color, u);
            }
            prevLoc = k.location;
            prevColor = k.color;
        }
        const float span = 1.0f - prevLoc;
        const float u = span > kMinLifetime ? (t - prevLoc) / span : 1.0f;
        return LerpV4(prevColor, rc.endColor, u);
    }

    /// <summary>Ribbon のメッシュ（点 capacity 個 × 断面 3 頂点、区間ごとに四角 2 枚）を作る。頂点はすべて透明</summary>
    /// <param name="capacity">点の数</param>
    /// <returns>メッシュ</returns>
    MeshData CreateRibbonMesh(uint32_t capacity) {
        MeshData m;
        m.vertices.resize(static_cast<size_t>(capacity) * kRibbonVertsPerPoint);
        for (auto& v : m.vertices) {
            v.position = { 0.0f, 0.0f, 0.0f };
            v.texcoord = { 0.0f, 0.0f };
            v.normal = { 0.0f, 0.0f, -1.0f };
            v.color = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
        m.indices.reserve(static_cast<size_t>(capacity - 1) * kRibbonIndicesPerSegment);
        for (uint32_t k = 0; k + 1 < capacity; ++k) {
            const uint32_t a = k * kRibbonVertsPerPoint;
            const uint32_t b = a + kRibbonVertsPerPoint;
            // 縁0-中央 の四角
            m.indices.insert(m.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
            // 中央-縁1 の四角
            m.indices.insert(m.indices.end(), { a + 1, b + 1, a + 2, a + 2, b + 1, b + 2 });
        }
        return m;
    }

    /// <summary>Ribbon の点の数（メッシュの大きさ）を def から決める</summary>
    /// <param name="rc">Ribbon の定義</param>
    /// <returns>点の数</returns>
    uint32_t RibbonCapacity(const EffectRibbonComponent& rc) {
        const uint32_t n = (rc.mode == EffectRibbonMode::Beam) ? rc.beamSegments + 1 : rc.maxPoints;
        return (std::clamp)(n, kRibbonMinPoints, kRibbonMaxPoints);
    }

    /// <summary>メッシュを作り直す必要がある Ribbon の変更か（mode / 点の数 / 分割数 / テクスチャ）</summary>
    /// <param name="a">変更前</param>
    /// <param name="b">変更後</param>
    /// <returns>作り直しが要れば true</returns>
    bool RibbonMeshNeedsRebuild(const EffectRibbonComponent& a, const EffectRibbonComponent& b) {
        return a.mode != b.mode || RibbonCapacity(a) != RibbonCapacity(b) || a.texturePath != b.texturePath;
    }

    /// <summary>時間に関係ない Ribbon の材質（ブレンド・UV の流れ）をレンダラへ設定する</summary>
    /// <param name="r">レンダラ</param>
    /// <param name="rc">Ribbon の定義</param>
    void ApplyRibbonStaticParams(EffectPrimitiveRenderer& r, const EffectRibbonComponent& rc) {
        r.SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(rc.blendMode));
        r.SetUVScroll(rc.uvScrollSpeed);
        r.SetDepthWrite(false);
        r.SetCullBackface(false);
    }
}

void EffectInstance::Initialize(const EffectDef& def, const Vector3& worldPos, GPUParticleManager* gpu) {
    def_ = def;
    worldPos_ = worldPos;
    elapsedTime_ = 0.0f;
    finished_ = false;
    gpu_ = gpu;

    primitives_.resize(def_.primitives.size());
    particles_.resize(def_.particles.size());
    lights_.resize(def_.lights.size());
    sounds_.resize(def_.sounds.size());
    ribbons_.resize(def_.ribbons.size());
    for (auto& l : lights_) {
        l.slot = kInvalidLightSlot;
    }
    releasing_ = false;
}

void EffectInstance::RequestRelease() {
    releasing_ = true;
}

Vector3 EffectInstance::RotateByWorld(const Vector3& v) const {
    const bool hasWorldRot = (worldRotation_.x != 0.0f || worldRotation_.y != 0.0f || worldRotation_.z != 0.0f);
    if (!hasWorldRot) return v;
    // ビームの向きは Rx·Ry·Rz（Object3D と同じ。弾の向きを作る DirectionToEuler＝ピッチ→ヨーと合う）。
    // MakeRotateMatrix(Vector3) は Rz·Ry·Rx なので、ピッチとヨーが両方ある向きでずれる（回転順の罠）
    const Matrix4x4 m = Multiply(MakeRotateXMatrix(worldRotation_),
        Multiply(MakeRotateYMatrix(worldRotation_), MakeRotateZMatrix(worldRotation_)));
    return TransformMatrix(v, m);
}

Vector3 EffectInstance::AnchoredOffset(const Vector3& offset, int beamAnchor) const {
    if (beamAnchor != EffectBeamAnchor::BeamEnd) return offset;
    // ビームの先：Ribbon の Beam が 1 本でもあればその長さ（倍率込み）、無ければ渡された長さ
    // （伸びている途中なら今の伸び具合。Ribbon は Primitive の後に進むので 1 フレーム前の値）
    float length = ResolveBeamLength(0.0f);
    for (size_t i = 0; i < def_.ribbons.size(); ++i) {
        const EffectRibbonComponent& rc = def_.ribbons[i];
        if (rc.mode != EffectRibbonMode::Beam) continue;
        const float extend = (i < ribbons_.size()) ? Smooth01(ribbons_[i].extend) : 1.0f;
        length = ResolveBeamLength(rc.defaultLength) * rc.lengthScale * extend;
        break;
    }
    return { offset.x, offset.y, offset.z + length };
}

void EffectInstance::ResetRibbons() {
    for (auto& rt : ribbons_) {
        rt.points.clear();
        rt.clock = 0.0f;
        rt.extend = 0.0f;
        rt.releaseTimer = 0.0f;
        rt.started = false;
        rt.releasing = false;
        rt.finished = false;
        rt.visible = false;
    }
}

template <class GroupDt>
void EffectInstance::UpdateRibbons(Camera* camera, const GroupDt& groupDt) {
    lastCamera_ = camera;
    const Vector3 cameraPos = camera ? camera->GetTranslate() : Vector3{ 0.0f, 0.0f, 0.0f };
    const bool effectEnded = !def_.loop && elapsedTime_ >= def_.totalDuration;

    // 描く点（先頭→末尾）。毎フレーム使い回すので static にしない（インスタンスが複数あるため関数内に置く）
    struct DrawPoint { Vector3 pos; float t; float widthScale; };
    std::vector<DrawPoint> drawPoints;

    for (size_t i = 0; i < def_.ribbons.size() && i < ribbons_.size(); ++i) {
        const EffectRibbonComponent& rc = def_.ribbons[i];
        RibbonRuntime& rt = ribbons_[i];
        rt.visible = false;
        if (rt.finished) continue;

        const float dt = groupDt(rc.timeGroup);
        rt.clock += dt;
        if (!rt.started) {
            // 出る前にエフェクトが終わった／出し終わりになった
            if (effectEnded || releasing_) { rt.finished = true; continue; }
            if (rt.clock < rc.startTime) continue;
            rt.started = true;
        }
        const float local = rt.clock - rc.startTime;

        // メッシュ（mode・点の数・テクスチャが変わったら作り直す）
        const uint32_t capacity = RibbonCapacity(rc);
        if (!rt.renderer || rt.capacity != capacity || rt.meshMode != rc.mode) {
            PEPPER_SCOPE("EffectInstance::CreateRibbon");
            rt.renderer = std::make_unique<EffectPrimitiveRenderer>();
            rt.renderer->InitializeDynamic(CreateRibbonMesh(capacity), rc.texturePath);
            rt.capacity = capacity;
            rt.meshMode = rc.mode;
            rt.vertices.assign(static_cast<size_t>(capacity) * kRibbonVertsPerPoint, MeshVertex{});
            if (rc.mode != EffectRibbonMode::Trail) rt.points.clear();
        }
        ApplyRibbonStaticParams(*rt.renderer, rc);

        // 消え始め：Release・寿命切れ・ループしないエフェクトの終わり
        const bool lifeOver = rc.lifetime > 0.0f && local >= rc.lifetime;
        if (!rt.releasing && (releasing_ || lifeOver || effectEnded)) {
            rt.releasing = true;
            rt.releaseTimer = 0.0f;
        }
        if (rt.releasing) rt.releaseTimer += dt;

        const Vector3 rotatedOffset = RotateByWorld(rc.offset);
        const Vector3 head = { worldPos_.x + rotatedOffset.x, worldPos_.y + rotatedOffset.y, worldPos_.z + rotatedOffset.z };
        const Vector3 forward = Normalized3(RotateByWorld(kBeamForward), kBeamForward);

        drawPoints.clear();
        if (rc.mode == EffectRibbonMode::Beam) {
            // 伸びる → 出し終わりで細くなって消える
            if (!rt.releasing) {
                rt.extend = rc.extendTime > kMinLifetime ? Saturate(rt.extend + dt / rc.extendTime) : 1.0f;
            }
            const float widthScale = rt.releasing
                ? (rc.releaseTime > kMinLifetime ? 1.0f - Saturate(rt.releaseTimer / rc.releaseTime) : 0.0f)
                : 1.0f;
            if (rt.releasing && widthScale <= 0.0f) {
                rt.finished = true;
                continue;
            }
            const float length = ResolveBeamLength(rc.defaultLength) * rc.lengthScale * Smooth01(rt.extend);
            const uint32_t segments = capacity - 1;
            const float segmentPower = (std::max)(kMinSegmentPower, rc.segmentPower);
            for (uint32_t k = 0; k <= segments; ++k) {
                // 区切りは根元ほど細かく（segmentPower 乗）。t は根元からの距離の割合のまま
                const float s = static_cast<float>(k) / static_cast<float>(segments);
                const float t = (segmentPower == 1.0f) ? s : std::pow(s, segmentPower);
                const float d = length * t;
                float w = widthScale;
                // 付け根：flareLength [m] をかけて flareStartScale 倍から広がる
                if (rc.flareLength > 0.0f) {
                    w *= LerpF(rc.flareStartScale, 1.0f, Smooth01(d / rc.flareLength));
                }
                if (rc.wobbleAmplitude != 0.0f) {
                    const float wave = rc.wobbleWaveLength > kMinLifetime ? d / rc.wobbleWaveLength * kTwoPi : 0.0f;
                    w *= 1.0f + rc.wobbleAmplitude * std::sin(rc.wobbleFrequency * rt.clock - wave);
                }
                drawPoints.push_back({ { head.x + forward.x * d, head.y + forward.y * d, head.z + forward.z * d }, t, w });
            }
        } else {
            // Trail：点を古くし、古すぎる点を捨てる
            const float pointLife = rc.pointLifetime > kMinLifetime ? rc.pointLifetime : kMinLifetime;
            for (auto& p : rt.points) p.age += dt;
            while (!rt.points.empty() && rt.points.front().age >= pointLife) rt.points.erase(rt.points.begin());

            if (!rt.releasing) {
                // 最後に置いた点から minPointDistance 以上動いたら点を足す（先頭の今の位置は毎フレーム別に描く）
                const bool needPoint = rt.points.empty() || LengthSq3({
                    head.x - rt.points.back().position.x,
                    head.y - rt.points.back().position.y,
                    head.z - rt.points.back().position.z }) >= rc.minPointDistance * rc.minPointDistance;
                if (needPoint) {
                    rt.points.push_back({ head, 0.0f });
                    // 先頭の分を 1 つ残す
                    while (rt.points.size() + 1 > capacity) rt.points.erase(rt.points.begin());
                }
                drawPoints.push_back({ head, 0.0f, 1.0f });
            } else if (rt.points.empty()) {
                rt.finished = true;
                continue;
            }
            for (auto it = rt.points.rbegin(); it != rt.points.rend(); ++it) {
                // 今の位置と同じ点（置いた直後）は二重に描かない
                if (!drawPoints.empty() && LengthSq3({
                        it->position.x - drawPoints.back().pos.x,
                        it->position.y - drawPoints.back().pos.y,
                        it->position.z - drawPoints.back().pos.z }) < kRibbonTinyLengthSq) continue;
                drawPoints.push_back({ it->position, Saturate(it->age / pointLife), 1.0f });
            }
        }

        // ----- 頂点を作る（カメラの方を向く帯。断面は 縁0・中央・縁1） -----
        const size_t count = (std::min)(drawPoints.size(), static_cast<size_t>(capacity));
        if (count < kRibbonMinPoints) {
            rt.renderer->Update(camera, dt);
            continue;
        }
        float traveled = 0.0f;
        for (size_t k = 0; k < count; ++k) {
            const Vector3& p = drawPoints[k].pos;
            const Vector3& prev = drawPoints[k > 0 ? k - 1 : k].pos;
            const Vector3& next = drawPoints[k + 1 < count ? k + 1 : k].pos;
            if (k > 0) traveled += std::sqrt(LengthSq3({ p.x - prev.x, p.y - prev.y, p.z - prev.z }));
            const Vector3 tangent = Normalized3({ next.x - prev.x, next.y - prev.y, next.z - prev.z }, forward);
            const Vector3 toCamera = Normalized3({ cameraPos.x - p.x, cameraPos.y - p.y, cameraPos.z - p.z }, kRibbonFallbackUp);
            Vector3 side = Cross3(tangent, toCamera);
            if (LengthSq3(side) < kRibbonTinyLengthSq) side = Cross3(tangent, kRibbonFallbackUp);
            side = Normalized3(side, kRibbonFallbackSide);

            const float t = drawPoints[k].t;
            const float width = LerpF(rc.startWidth, rc.endWidth, rc.widthCurve.Evaluate(t)) * drawPoints[k].widthScale;
            const float half = (std::max)(0.0f, width) * 0.5f;
            const Vector4 color = EvaluateRibbonColor(rc, t);
            const Vector4 edgeColor = { color.x, color.y, color.z, color.w * rc.edgeAlpha };
            const Vector4 coreColor = {
                LerpF(color.x, 1.0f, rc.coreWhiteness),
                LerpF(color.y, 1.0f, rc.coreWhiteness),
                LerpF(color.z, 1.0f, rc.coreWhiteness),
                color.w };
            const float u = rc.uvTilesPerUnit > 0.0f ? traveled * rc.uvTilesPerUnit : t;

            MeshVertex* v = &rt.vertices[k * kRibbonVertsPerPoint];
            v[0].position = { p.x + side.x * half, p.y + side.y * half, p.z + side.z * half };
            v[1].position = p;
            v[2].position = { p.x - side.x * half, p.y - side.y * half, p.z - side.z * half };
            v[0].texcoord = { u, kRibbonEdgeV0 };
            v[1].texcoord = { u, kRibbonCenterV };
            v[2].texcoord = { u, kRibbonEdgeV1 };
            v[0].color = edgeColor;
            v[1].color = coreColor;
            v[2].color = edgeColor;
            v[0].normal = v[1].normal = v[2].normal = { -toCamera.x, -toCamera.y, -toCamera.z };
        }
        // 使わない点は最後の点に重ねて透明にする（面積 0 の三角形になる）
        const MeshVertex& last = rt.vertices[(count - 1) * kRibbonVertsPerPoint + 1];
        for (size_t k = count * kRibbonVertsPerPoint; k < rt.vertices.size(); ++k) {
            rt.vertices[k] = last;
            rt.vertices[k].color.w = 0.0f;
        }
        rt.renderer->UpdateVertices(rt.vertices.data(), static_cast<uint32_t>(rt.vertices.size()));
        rt.renderer->Update(camera, dt);
        rt.visible = true;
    }
}

std::string EffectInstance::EffGroupName(const std::string& name) const {
    if (name.empty() || !isPreview_) return name;
    return std::string(GPUParticleManager::kPreviewPrefix) + name;
}

void EffectInstance::CollectPreviewGroupNames(std::vector<std::string>& out) const {
    if (!isPreview_) return;
    for (const auto& pc : def_.particles) {
        if (pc.gpuParticleGroupName.empty()) continue;
        out.push_back(EffGroupName(pc.gpuParticleGroupName));
    }
}

void EffectInstance::Update(Camera* camera, float deltaTime) {
    // deltaTime は unscaled な実 delta（呼び出し側が realDt を渡す）。
    // 各コンポーネントの TimeGroup でスケールして進める。プレビューはシーン時間に依存させず raw のまま。
    // マスタークロック（spawn スケジュール / totalDuration / loop）は World グループに従う（従来互換）。
    auto groupDt = [this, deltaTime](int tg) -> float {
        if (isPreview_) return deltaTime;
        return EngineTime::ScaledDeltaTime(static_cast<TimeGroup>(tg), deltaTime);
    };
    const float masterDt = groupDt(static_cast<int>(TimeGroup::World));
    elapsedTime_ += masterDt;
    orbitClock_  += masterDt; // ループでリセットしない（GPU orbit と同じく連続）

    // ===== Primitive =====
    for (size_t i = 0; i < def_.primitives.size(); ++i) {
        const EffectPrimitiveComponent& pc = def_.primitives[i];
        PrimitiveRuntime& rt = primitives_[i];
        if (rt.finished) continue;
        // 出し終わり：弾の頭の光などはすぐ消す
        if (releasing_) {
            rt.renderer.reset();
            rt.finished = true;
            continue;
        }

        // この成分の TimeGroup でスケールした専用クロックで進める（World 以外なら独立して止まる/進む）。
        const float compDt = groupDt(pc.timeGroup);
        rt.clock += compDt;
        float local = rt.clock - pc.startTime;
        if (local < 0.0f) continue;

        // 開始：レンダラ生成（ループの 2 周目からは前の周のレンダラを使い回す。
        // 作り直すとスラスターのような 30 秒ループが周ごとに 13〜26ms 止まっていた＝14 番 §4.7）
        if (!rt.started) {
            if (!rt.renderer) {
                PEPPER_SCOPE("EffectInstance::CreatePrimitive");
                rt.renderer = std::make_unique<EffectPrimitiveRenderer>();
                rt.renderer->Initialize(pc.meshType, pc.texturePath,
                                        pc.ringParams, pc.cylinderParams, pc.helixParams, pc.beamParams,
                                        pc.lightningParams, pc.frameParams);
            }

            // 向きの初期化：基準 rotate（× 出現時ランダム）をクオータニオンで合成。
            Quaternion baseQ = QuaternionFromEuler(pc.rotate);
            if (pc.randomRotateOnSpawn) {
                Quaternion randQ = QuaternionFromEuler({ RandSym(pc.randomRotateRange.x),
                                                 RandSym(pc.randomRotateRange.y),
                                                 RandSym(pc.randomRotateRange.z) });
                baseQ = Multiply(baseQ, randQ);
            }
            rt.orientation = Normalize(baseQ);

            // blend / billboard / material / UV / distortion（SyncFromDef のライブ反映と共用）
            ApplyPrimitiveStaticParams(*rt.renderer, pc);
            rt.started = true;
        }

        // 寿命は Effect の totalDuration でクランプ（Effect 終了後は残らない）
        float maxLife = max(kMinLifetime, def_.totalDuration - pc.startTime);
        float life = pc.lifetime > kMinLifetime ? pc.lifetime : kMinLifetime;
        if (life > maxLife) life = maxLife;

        float t = Saturate(local / life);
        // Scale はカーブ（イージング）で t を再マップしてから補間（既定 enabled=false なら線形）
        Vector3 scale = LerpV3(pc.startScale, pc.endScale, pc.scaleCurve.Evaluate(t));
        Vector4 color = LerpV4(pc.startColor, pc.endColor, t);
        // Hue 回転（生存中のシームレス色変化）。経過秒に沿って色相を回す（彩度/明度/α保持）。
        if (pc.hueShiftEnable) {
            color = HueRotate(color, pc.hueShiftSpeed * kTwoPi * local);
        }

        // 持続回転：角速度ベクトル rotateSpeed を毎フレーム積分（クオータニオン合成。ジンバルロックなし）。
        {
            const Vector3& w = pc.rotateSpeed;
            float wlen = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
            if (wlen > kMinAngularSpeed) {
                float ang = wlen * compDt;
                Quaternion dq = MakeRotateAxisAngleQuaternion({ w.x / wlen, w.y / wlen, w.z / wlen }, ang);
                rt.orientation = Normalize(Multiply(dq, rt.orientation));
            }
        }

        // エフェクト全体の向き worldRotation_ を適用：
        // offset をその回転で回し、Primitive の向きにも合成する（弾の進行方向追従など）。
        const bool hasWorldRot = (worldRotation_.x != 0.0f || worldRotation_.y != 0.0f || worldRotation_.z != 0.0f);
        // 位置：usePositionAnim なら StartPos→EndPos をカーブで補間、そうでなければ静的 offset。
        Vector3 baseOffset = pc.usePositionAnim
            ? LerpV3(pc.startPos, pc.endPos, pc.posCurve.Evaluate(t))
            : pc.offset;
        Vector3 offset = baseOffset;
        Quaternion finalQ = rt.orientation;
        if (hasWorldRot) {
            Matrix4x4 rotMat = MakeRotateMatrix(worldRotation_);
            offset = TransformMatrix(baseOffset, rotMat);
            finalQ = Multiply(QuaternionFromEuler(worldRotation_), rt.orientation);
        }
        rt.renderer->SetRotateQuaternion(finalQ);
        // ビームの先に付ける分（ビームと同じ向きで前へ進める）
        if (pc.beamAnchor == EffectBeamAnchor::BeamEnd) {
            const Vector3 shift = RotateByWorld(AnchoredOffset({ 0.0f, 0.0f, 0.0f }, pc.beamAnchor));
            offset = { offset.x + shift.x, offset.y + shift.y, offset.z + shift.z };
        }
        Vector3 pos = { worldPos_.x + offset.x, worldPos_.y + offset.y, worldPos_.z + offset.z };

        rt.renderer->SetTranslate(pos);
        rt.renderer->SetScale(scale);
        rt.renderer->SetColor(color);
        // 歪みの強さを寿命に沿って補間（静的な値は ApplyPrimitiveStaticParams で設定済み）
        if (pc.useDistortion && pc.distortionStrengthAnim && !pc.distortionTexturePath.empty()) {
            rt.renderer->SetDistortionStrength(pc.distortionStrength + (pc.distortionEndStrength - pc.distortionStrength) * t);
        }

        // ディゾルブ閾値：コンポーネント出現(local)を基準に、In(出現:1→0) と Out(消滅:0→1) を
        // max で時間合成する（0=完全表示 / 1=完全に消えた）。マスク未設定や In/Out 両方OFFなら無効。
        {
            float th = 0.0f;
            if (pc.dissolveInEnable) {
                float t = local - pc.dissolveInStartTime;
                float d = pc.dissolveInDuration > kMinDissolveDuration ? pc.dissolveInDuration : kMinDissolveDuration;
                // 進行度 progress(0→1) をカーブで再マップ。In は threshold = 1 - progress。
                float p = Saturate(t / d);
                float inTh = (t <= 0.0f) ? 1.0f : 1.0f - pc.dissolveCurve.Evaluate(p);
                if (inTh > th) th = inTh;
            }
            if (pc.dissolveOutEnable) {
                float t = local - pc.dissolveOutStartTime;
                float d = pc.dissolveOutDuration > kMinDissolveDuration ? pc.dissolveOutDuration : kMinDissolveDuration;
                // 進行度 progress(0→1) をカーブで再マップ。Out は threshold = progress。
                float p = Saturate(t / d);
                float outTh = (t <= 0.0f) ? 0.0f : pc.dissolveCurve.Evaluate(p);
                if (outTh > th) th = outTh;
            }
            bool active = pc.useDissolve && !pc.dissolveMaskPath.empty()
                       && (pc.dissolveInEnable || pc.dissolveOutEnable);
            rt.renderer->SetDissolve(active, th);
        }

        rt.renderer->Update(camera, compDt);

        // 寿命終了（totalDuration を超えた場合も含む）
        if (local >= life || elapsedTime_ >= def_.totalDuration) {
            // ループするエフェクトは次の周で使い回すので残す（finished の間は描かない）
            if (!def_.loop) rt.renderer.reset();
            rt.finished = true;
        }
    }

    // ===== Particle =====
    for (size_t i = 0; i < def_.particles.size(); ++i) {
        const EffectParticleComponent& pc = def_.particles[i];
        ParticleRuntime& rt = particles_[i];
        if (!gpu_) { rt.burstFired = true; continue; }
        // 出し終わり：新しく出さない（出ている粒子はそのまま寿命で消える）
        if (releasing_) { rt.burstFired = true; continue; }
        // ビームの先に付ける分（offset は従来どおり回さず、ビームの先へ進む分だけエフェクトの向きで回す）
        const Vector3 anchorShift = (pc.beamAnchor == EffectBeamAnchor::BeamEnd)
            ? RotateByWorld(AnchoredOffset({ 0.0f, 0.0f, 0.0f }, pc.beamAnchor))
            : Vector3{ 0.0f, 0.0f, 0.0f };

        // burst スケジュールも成分の TimeGroup でスケールしたクロックで判定する
        // （Effect グループなら必殺技の World 停止中でも startTime に達して発射できる）。
        rt.clock += groupDt(pc.timeGroup);

        // プレビューインスタンスならグループ名にプレフィックスを付け、シーンと物理バッファを分離する。
        const std::string groupName = EffGroupName(pc.gpuParticleGroupName);

        // orbit 有効時、発生平面(ringNormal)を tumble と同じ回転で回す（帯が首を振り、発生と粒子が同期）。
        // spin（帯上の流れ）の軸＝この「現在のリング法線」。
        Vector3 effRingNormal = pc.ringNormal;
        if (pc.orbitEnabled) {
            effRingNormal = RotateAroundAxis(pc.ringNormal, pc.orbitTumbleAxis, pc.orbitTumbleSpeed * orbitClock_);
        }

        // 周回（orbit）は burst 後も毎フレーム更新（中心はエフェクト位置＋offset に追従）。
        if (gpu_->HasGroup(groupName)) {
            const Vector3 center = {
                worldPos_.x + pc.offset.x + anchorShift.x,
                worldPos_.y + pc.offset.y + anchorShift.y,
                worldPos_.z + pc.offset.z + anchorShift.z };
            // 半径変化は「生成時の円＝emitRadius」に対する比で渡す（シェーダは前フレームとの比をかける）。
            const float radiusEndRatio = (pc.emitRadius > kMinEmitRadius) ? pc.orbitRadiusEnd / pc.emitRadius : 1.0f;
            gpu_->SetGroupOrbit(groupName, pc.orbitEnabled, center,
                                effRingNormal, pc.orbitSpinSpeed,         // spin：帯上を流れる（現在のリング法線まわり）
                                pc.orbitTumbleAxis, pc.orbitTumbleSpeed,  // tumble：帯自体の回転
                                pc.orbitRadiusEnable, radiusEndRatio);
            if (pc.orbitEnabled) {
                gpu_->SetEmitterShape(groupName, pc.emitShape, effRingNormal, pc.ringThickness);
            }
            // テクスチャのライブ反映（エディタで Texture を差し替えたら既存グループにも適用）
            gpu_->SetGroupTexture(groupName, pc.texturePath);
            // ブレンドモードのライブ反映（時間非依存。エディタで切替えたら即反映）
            gpu_->SetGroupBlendMode(groupName, pc.blendMode);
            // 時間グループのライブ反映。シーン用粒子は gpu->Update が EngineTime でこのグループの倍率で進める
            // （プレビュー粒子は UpdatePreviewSim が raw delta で進めるのでこの設定の影響を受けない）。
            gpu_->SetGroupTimeGroup(groupName, static_cast<TimeGroup>(pc.timeGroup));
            // ディゾルブ（粒子ごとの寿命）。時間非依存なので毎フレーム設定でライブ反映も効く。
            gpu_->SetGroupDissolve(groupName, pc.useDissolve, pc.dissolveMaskPath,
                                   pc.dissolveInEnable, pc.dissolveInEnd,
                                   pc.dissolveOutEnable, pc.dissolveOutStart,
                                   pc.dissolveEdgeEnable, pc.dissolveEdgeColor, pc.dissolveEdgeWidth);
            // ブルーム（時間非依存＝毎フレーム設定でライブ反映）
            gpu_->SetGroupBloom(groupName, pc.useBloom, pc.bloomIntensity);
            // 水面に映すか（エフェクト単位。def の reflect）
            gpu_->SetGroupReflect(groupName, def_.reflect);
            // 収束（移動をカーブで制御）。orbit と排他（シェーダは converge を優先）。中心はエフェクト位置＋offset。
            gpu_->SetGroupConverge(groupName, pc.convergeEnable, center);
            // 移動カーブ LUT は converge と orbit 半径で共用（排他）。時間非依存＝毎フレーム設定でライブ反映。
            float motionLut[kMotionLutSize];
            BakeCurveLUT(pc.convergeEnable ? pc.convergeCurve : pc.orbitRadiusCurve, motionLut);
            gpu_->SetGroupMotionLUT(groupName, motionLut);
        }

        if (rt.burstFired) continue;
        if (rt.clock < pc.startTime) continue;

        // グループ名が指定されていて未登録なら、texturePath で自動生成（エディタで完結できるように）
        if (!groupName.empty() && !gpu_->HasGroup(groupName)) {
            gpu_->CreateGroup(groupName,
                pc.texturePath.empty() ? "Resources/Textures/circle.dds" : pc.texturePath);
        }

        // 1回だけバースト発射（duration は将来の拡張用、現状未使用）
        if (gpu_->HasGroup(groupName)) {
            gpu_->SetGroupBillboardMode(groupName, pc.billboardMode);
            Vector3 pos = {
                worldPos_.x + pc.offset.x + anchorShift.x,
                worldPos_.y + pc.offset.y + anchorShift.y,
                worldPos_.z + pc.offset.z + anchorShift.z };

            // 収束モードは初速を持たず、kVelocityConverge で emit 時に spawn 位置を velocity フィールドへ保持させる。
            // この発射フレームでも converge CB を確実に設定しておく（次の Emit+Update CS が同フレームで走るため）。
            if (pc.convergeEnable) {
                gpu_->SetEmitterVelocity(groupName, { 0.0f, 0.0f, 0.0f }, 0.0f, GPUParticleManager::kVelocityConverge);
                float convLut[kMotionLutSize];
                BakeCurveLUT(pc.convergeCurve, convLut);
                gpu_->SetGroupConverge(groupName, true, pos);
                gpu_->SetGroupMotionLUT(groupName, convLut);
            }
            // 初速モード（VelocityMode）。mode に応じた baseVelocity を渡す。
            else if (pc.velocityMode == GPUParticleManager::kVelocityDirectional) {
                float len = std::sqrt(pc.velocityDir.x * pc.velocityDir.x +
                                      pc.velocityDir.y * pc.velocityDir.y +
                                      pc.velocityDir.z * pc.velocityDir.z);
                Vector3 v = (len > kMinVelocityDirLength)
                    ? Vector3{ pc.velocityDir.x / len * pc.velocitySpeed,
                               pc.velocityDir.y / len * pc.velocitySpeed,
                               pc.velocityDir.z / len * pc.velocitySpeed }
                    : Vector3{ 0.0f, pc.velocitySpeed, 0.0f };
                gpu_->SetEmitterVelocity(groupName, v, pc.velocityJitter, GPUParticleManager::kVelocityDirectional);
            } else if (pc.velocityMode == GPUParticleManager::kVelocityRadial) {
                gpu_->SetEmitterVelocity(groupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, GPUParticleManager::kVelocityRadial);
            } else if (pc.velocityMode == GPUParticleManager::kVelocityTangent) {
                // 接線（公転）：speed を baseVelocity.x に。方向はシェーダが ringNormal から接線を計算
                gpu_->SetEmitterVelocity(groupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, GPUParticleManager::kVelocityTangent);
            } else {
                gpu_->SetEmitterVelocity(groupName, { 0.0f, 0.0f, 0.0f }, 0.0f, GPUParticleManager::kVelocityRandom);
            }

            // 発生形状（Sphere / Ring）。orbit 時は回転済みの法線で発生させ、粒子と同期させる。
            gpu_->SetEmitterShape(groupName, pc.emitShape, effRingNormal, pc.ringThickness);

            // 多色グラデーション（Fixed カラーモード）。
            // Start(loc=0)/End(loc=1) を常に両端キーとして使い、colorKeys はその間に挿入する中間キー。
            // → 中間キー0個=Start→End の2色、1個=3色…（Random モードでは無効）。
            if (pc.colorMode == GPUParticleManager::kColorModeFixed) {
                std::vector<std::pair<float, Vector4>> keys;
                keys.reserve(pc.colorKeys.size() + kEndpointColorKeyCount);
                keys.emplace_back(0.0f, pc.startColor);
                for (const auto& k : pc.colorKeys) keys.emplace_back(k.location, k.color);
                keys.emplace_back(1.0f, pc.endColor);
                std::sort(keys.begin(), keys.end(),
                    [](const std::pair<float, Vector4>& a, const std::pair<float, Vector4>& b) { return a.first < b.first; });
                gpu_->SetEmitterGradient(groupName, keys);
            } else {
                gpu_->SetEmitterGradient(groupName, {});
            }
            // Hue 回転（生存中のシームレス色変化）。時間非依存なので毎フレーム設定でライブ反映が効く。
            gpu_->SetGroupHueShift(groupName, pc.hueShiftEnable, pc.hueShiftSpeed, pc.hueShiftRandomPhase);

            // 粒子寿命：ループ時は周期を超えて生き残らせる（境界で全消えしないように）。
            // 非ループ時のみ totalDuration を超えないようにクランプ。
            float particleLife = pc.particleLifeTime;
            if (!def_.loop) {
                float remaining = max(kMinLifetime, def_.totalDuration - pc.startTime);
                particleLife = min(particleLife, remaining);
            }
            Vector3 rotRange = pc.randomRotateOnSpawn ? pc.randomRotateRange : Vector3{ 0.0f, 0.0f, 0.0f };
            gpu_->BurstEmit(groupName, pos, pc.burstCount, pc.emitRadius,
                            static_cast<uint32_t>(pc.colorMode), pc.startColor, pc.endColor,
                            pc.scaleMin, pc.scaleMax, pc.uniformScale, particleLife,
                            pc.startScale, pc.endScale,
                            rotRange, pc.rotateSpeed);
        }
        rt.burstFired = true;
    }

    // ===== Light =====
    LightManager* lm = LightManager::GetInstance();
    for (size_t i = 0; i < def_.lights.size(); ++i) {
        const EffectLightComponent& lc = def_.lights[i];
        LightRuntime& rt = lights_[i];
        if (rt.finished) continue;
        // 出し終わり：すぐ消す
        if (releasing_) {
            if (rt.slot != kInvalidLightSlot) {
                if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
                else           lm->ReleasePointLight(rt.slot);
                rt.slot = kInvalidLightSlot;
            }
            rt.started = true;
            rt.finished = true;
            continue;
        }

        float local = elapsedTime_ - lc.startTime;
        if (local < 0.0f) continue;

        // 開始：スロット確保
        if (!rt.started) {
            if (lc.kind == EffectLightKind::Spot) {
                rt.slot = lm->AcquireSpotLight();
                rt.isSpot = true;
            } else {
                rt.slot = lm->AcquirePointLight();
                rt.isSpot = false;
            }
            rt.started = true;
            // スロット確保失敗時はそのまま終了扱い
            if (rt.slot == kInvalidLightSlot) {
                rt.finished = true;
                continue;
            }
        }

        if (rt.slot == kInvalidLightSlot) continue;

        // 寿命を totalDuration でクランプ
        float maxLife = max(kMinLifetime, def_.totalDuration - lc.startTime);
        float life = lc.lifetime > kMinLifetime ? lc.lifetime : kMinLifetime;
        if (life > maxLife) life = maxLife;

        float t = Saturate(local / life);
        float intensity = LerpF(lc.startIntensity, lc.endIntensity, t);
        Vector3 pos = { worldPos_.x + lc.offset.x, worldPos_.y + lc.offset.y, worldPos_.z + lc.offset.z };

        if (rt.isSpot) {
            lm->SetSpotLightColor(rt.slot, lc.color);
            lm->SetSpotLightPosition(rt.slot, pos);
            lm->SetSpotLightDirection(rt.slot, lc.direction);
            lm->SetSpotLightIntensity(rt.slot, intensity);
            lm->SetSpotLightDistance(rt.slot, lc.range);
            lm->SetSpotLightCosAngle(rt.slot, lc.spotCosAngle);
            lm->SetSpotLightCosFalloffStart(rt.slot, lc.spotCosFalloffStart);
        } else {
            lm->SetPointLightColor(rt.slot, lc.color);
            lm->SetPointLightPosition(rt.slot, pos);
            lm->SetPointLightIntensity(rt.slot, intensity);
            lm->SetPointLightRadius(rt.slot, lc.range);
        }

        // 寿命終了：解放（totalDuration 超過も含む）
        if (local >= life || elapsedTime_ >= def_.totalDuration) {
            if (rt.isSpot) {
                lm->ReleaseSpotLight(rt.slot);
            } else {
                lm->ReleasePointLight(rt.slot);
            }
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }

    // ===== Sound =====
    SoundManager* sm = SoundManager::GetInstance();
    for (size_t i = 0; i < def_.sounds.size(); ++i) {
        const EffectSoundComponent& sc = def_.sounds[i];
        SoundRuntime& rt = sounds_[i];
        if (rt.finished) continue;

        if (!rt.started) {
            if (elapsedTime_ < sc.startTime) continue;
            Vector3 pos = { worldPos_.x + sc.offset.x, worldPos_.y + sc.offset.y, worldPos_.z + sc.offset.z };
            rt.handle = sm->Play3DSound(sc.soundName, pos, { 0.0f, 0.0f, 0.0f }, sc.distanceScale);
            rt.started = true;
            if (rt.handle == 0) {
                // 再生失敗（音源未ロード等）。リトライしない。
                rt.finished = true;
                continue;
            }
        }

        // 毎フレーム位置を追従（エフェクトが動いていなくても、リスナーが動けば必要）
        if (rt.handle != 0) {
            Vector3 pos = { worldPos_.x + sc.offset.x, worldPos_.y + sc.offset.y, worldPos_.z + sc.offset.z };
            sm->UpdateEmitter(rt.handle, pos, { 0.0f, 0.0f, 0.0f });
        }
        // Effect の totalDuration を超えたら音を強制停止する（Effect の時間を絶対とする）
        if (rt.handle != 0 && elapsedTime_ >= def_.totalDuration) {
            sm->Stop3DSound(rt.handle);
            rt.handle = 0;
            rt.finished = true;
        }
    }

    // ===== Ribbon =====
    UpdateRibbons(camera, groupDt);

    // 外部からの停止要求：即終了させる
    if (stopRequested_) {
        Cleanup();
        finished_ = true;
        return;
    }

    // 出し終わり：Ribbon が全部消えたら終わり（ループしない）
    if (releasing_) {
        bool ribbonsDone = true;
        for (const auto& rt : ribbons_) { if (rt.started && !rt.finished) { ribbonsDone = false; break; } }
        if (ribbonsDone) {
            Cleanup();
            finished_ = true;
        }
        return;
    }

    // ループ：totalDuration を超えたら全 runtime を片付けて最初から再生し直す
    if (def_.loop && elapsedTime_ >= def_.totalDuration) {
        ResetForLoop();
        return;
    }

    // 全コンポーネント完了判定
    bool allDone = true;
    for (auto& rt : ribbons_) { if (!rt.finished) { allDone = false; break; } }
    for (auto& rt : primitives_) { if (!rt.finished) { allDone = false; break; } }
    if (allDone) {
        for (auto& rt : particles_) { if (!rt.burstFired) { allDone = false; break; } }
    }
    if (allDone) {
        for (auto& rt : lights_) { if (!rt.finished) { allDone = false; break; } }
    }
    if (allDone) {
        for (auto& rt : sounds_) { if (!rt.started) { allDone = false; break; } }
    }
    if (allDone && elapsedTime_ >= def_.totalDuration) {
        finished_ = true;
    }
}

void EffectInstance::ResetForLoop() {
    // 現在確保中のライト・サウンドはここで解放。Primitive の renderer は残し、次の周の開始で
    // 向き・材質だけ設定し直す（作り直さない）
    LightManager* lm = LightManager::GetInstance();
    for (auto& rt : lights_) {
        if (rt.slot != kInvalidLightSlot) {
            if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
            else           lm->ReleasePointLight(rt.slot);
        }
        rt = LightRuntime{};
        rt.slot = kInvalidLightSlot;
    }
    SoundManager* sm = SoundManager::GetInstance();
    for (auto& rt : sounds_) {
        if (rt.handle != 0) {
            sm->Stop3DSound(rt.handle);
        }
        rt = SoundRuntime{};
    }
    for (auto& rt : primitives_) {
        rt.started = false;
        rt.finished = false;
        rt.clock = 0.0f;
    }
    for (auto& rt : particles_) {
        rt.burstFired = false;
        rt.clock = 0.0f;
    }
    // Ribbon は寿命があるものだけ出し直す（寿命 0＝ずっと出す物は、ループの境目で尾が途切れないよう続ける）
    for (size_t i = 0; i < ribbons_.size() && i < def_.ribbons.size(); ++i) {
        if (def_.ribbons[i].lifetime <= 0.0f) continue;
        RibbonRuntime& rt = ribbons_[i];
        rt.points.clear();
        rt.clock = 0.0f;
        rt.extend = 0.0f;
        rt.releaseTimer = 0.0f;
        rt.started = false;
        rt.releasing = false;
        rt.finished = false;
        rt.visible = false;
    }
    elapsedTime_ = 0.0f;
}

void EffectInstance::SyncFromDef(const EffectDef& newDef) {
    // ----- Primitive -----
    if (newDef.primitives.size() != def_.primitives.size()) {
        // 数が変わった（パーツ追加/削除）→ index ずれ事故を避けるため、この型の runtime を作り直す。
        for (auto& rt : primitives_) rt.renderer.reset();
        primitives_.clear();
        primitives_.resize(newDef.primitives.size());
    } else {
        // 数が同じ → started 済みは、メッシュ変更なら部分リビルド、それ以外はライブ setter で反映。
        for (size_t i = 0; i < newDef.primitives.size(); ++i) {
            PrimitiveRuntime& rt = primitives_[i];
            if (!rt.started || !rt.renderer) continue; // 未spawnは新 def_ から自然に生成される
            if (PrimitiveMeshNeedsRebuild(def_.primitives[i], newDef.primitives[i])) {
                rt.renderer.reset();
                rt.started = false;   // 次 Update で新メッシュとして再生成（elapsedTime_ は保持）
                rt.finished = false;
            } else {
                ApplyPrimitiveStaticParams(*rt.renderer, newDef.primitives[i]);
            }
        }
    }

    // ----- Particle -----
    if (newDef.particles.size() != def_.particles.size()) {
        particles_.clear();
        particles_.resize(newDef.particles.size());
    }

    // ----- Light（確保中スロットを解放してから作り直す）-----
    if (newDef.lights.size() != def_.lights.size()) {
        LightManager* lm = LightManager::GetInstance();
        for (auto& rt : lights_) {
            if (rt.slot != kInvalidLightSlot) {
                if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
                else           lm->ReleasePointLight(rt.slot);
            }
        }
        lights_.clear();
        lights_.resize(newDef.lights.size());
        for (auto& l : lights_) l.slot = kInvalidLightSlot;
    }

    // ----- Sound（鳴動中を止めてから作り直す）-----
    if (newDef.sounds.size() != def_.sounds.size()) {
        SoundManager* sm = SoundManager::GetInstance();
        for (auto& rt : sounds_) {
            if (rt.handle != 0) sm->Stop3DSound(rt.handle);
        }
        sounds_.clear();
        sounds_.resize(newDef.sounds.size());
    }

    // ----- Ribbon（数が変われば作り直す。同じならメッシュが要る変更だけ作り直す。点は残す）-----
    if (newDef.ribbons.size() != def_.ribbons.size()) {
        ribbons_.clear();
        ribbons_.resize(newDef.ribbons.size());
    } else {
        for (size_t i = 0; i < newDef.ribbons.size(); ++i) {
            if (RibbonMeshNeedsRebuild(def_.ribbons[i], newDef.ribbons[i])) {
                ribbons_[i].renderer.reset(); // 次の Update で作り直す
            }
        }
    }

    // 新 def を採用。毎フレーム読みの値（scale/color/timing/offset 等）はこれで即ライブ反映される。
    def_ = newDef;
}

void EffectInstance::Rewind() {
    ResetForLoop();          // ライト/サウンド解放・runtime 片付け・elapsedTime_=0
    ResetRibbons();
    orbitClock_ = 0.0f;
    finished_ = false;
    stopRequested_ = false;
    releasing_ = false;
}

void EffectInstance::Draw() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->Draw();
        }
    }
    for (auto& rt : ribbons_) {
        if (rt.renderer && rt.visible) rt.renderer->Draw();
    }
}

void EffectInstance::DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos) {
    if (!def_.reflect) return;
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->DrawReflection(mirroredView, mirroredViewProjection, mirroredCameraPos);
        }
    }
    for (auto& rt : ribbons_) {
        if (rt.renderer && rt.visible) rt.renderer->DrawReflection(mirroredView, mirroredViewProjection, mirroredCameraPos);
    }
}

void EffectInstance::DrawDistortionPass() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            rt.renderer->DrawDistortionPass();
        }
    }
}

void EffectInstance::DrawDistortionPassPreview() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            rt.renderer->DrawDistortionPassPreview();
        }
    }
}

bool EffectInstance::HasActiveDistortionSource() const {
    for (const auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            return true;
        }
    }
    return false;
}

void EffectInstance::DrawBloomPass() {
    for (size_t i = 0; i < primitives_.size() && i < def_.primitives.size(); ++i) {
        const auto& rt = primitives_[i];
        const auto& pc = def_.primitives[i];
        if (pc.useBloom && rt.renderer && rt.started && !rt.finished) {
            rt.renderer->DrawBloomPass(pc.bloomIntensity);
        }
    }
    for (size_t i = 0; i < ribbons_.size() && i < def_.ribbons.size(); ++i) {
        const auto& rt = ribbons_[i];
        if (def_.ribbons[i].useBloom && rt.renderer && rt.visible) rt.renderer->DrawBloomPass(def_.ribbons[i].bloomIntensity);
    }
}

void EffectInstance::DrawBloomPassPreview() {
    for (size_t i = 0; i < primitives_.size() && i < def_.primitives.size(); ++i) {
        const auto& rt = primitives_[i];
        const auto& pc = def_.primitives[i];
        if (pc.useBloom && rt.renderer && rt.started && !rt.finished) {
            rt.renderer->DrawBloomPassPreview(pc.bloomIntensity);
        }
    }
    for (size_t i = 0; i < ribbons_.size() && i < def_.ribbons.size(); ++i) {
        const auto& rt = ribbons_[i];
        if (def_.ribbons[i].useBloom && rt.renderer && rt.visible) rt.renderer->DrawBloomPassPreview(def_.ribbons[i].bloomIntensity);
    }
}

bool EffectInstance::HasActiveBloomSource() const {
    for (size_t i = 0; i < primitives_.size() && i < def_.primitives.size(); ++i) {
        const auto& rt = primitives_[i];
        if (def_.primitives[i].useBloom && rt.renderer && rt.started && !rt.finished) return true;
    }
    for (size_t i = 0; i < ribbons_.size() && i < def_.ribbons.size(); ++i) {
        if (def_.ribbons[i].useBloom && ribbons_[i].renderer && ribbons_[i].visible) return true;
    }
    return false;
}

void EffectInstance::UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos) {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->UpdatePreviewWVP(viewMatrix, viewProjectionMatrix, cameraPos);
        }
    }
    for (auto& rt : ribbons_) {
        if (rt.renderer && rt.visible) rt.renderer->UpdatePreviewWVP(viewMatrix, viewProjectionMatrix, cameraPos);
    }
}

void EffectInstance::DrawPreview() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->DrawPreview();
        }
    }
    for (auto& rt : ribbons_) {
        if (rt.renderer && rt.visible) rt.renderer->DrawPreview();
    }
}

void EffectInstance::Cleanup() {
    // ライトのスロット予約はその場で返す（GPU描画が参照しないため安全）。
    LightManager* lm = LightManager::GetInstance();
    for (auto& rt : lights_) {
        if (rt.slot != kInvalidLightSlot) {
            if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
            else           lm->ReleasePointLight(rt.slot);
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }
    // 再生中の3D サウンドも停止
    SoundManager* sm = SoundManager::GetInstance();
    for (auto& rt : sounds_) {
        if (rt.handle != 0) {
            sm->Stop3DSound(rt.handle);
            rt.handle = 0;
        }
        rt.finished = true;
    }
    // Primitive renderer の破棄は EffectInstance のデストラクタ任せ。
    // この Cleanup() が ImGui コールバック等の Draw 中に呼ばれる可能性があるため、
    // renderer のリソース解放はマネージャ側で1フレーム遅延させる前提。
}
