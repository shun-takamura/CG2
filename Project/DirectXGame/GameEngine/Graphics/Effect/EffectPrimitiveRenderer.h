#pragma once
#include "PrimitiveMesh.h"
#include "PrimitivePipeline.h"
#include "PrimitiveGenerator.h"  // RingParams / CylinderParams / HelixParams
#include "BillboardMode.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <string>

class Camera;

/// <summary>
/// エフェクト1個分のPrimitive描画を担う軽量Renderer。
/// PrimitiveInstanceと違い、Hierarchy/Inspector/Tag/Colliderを持たない。
/// EffectInstance内部で動的生成→寿命でreset()される短命なもの想定。
/// </summary>
class EffectPrimitiveRenderer {
public:
    EffectPrimitiveRenderer() = default;
    ~EffectPrimitiveRenderer() = default;

    /// <summary>
    /// 指定タイプのメッシュで初期化。texturePath が空ならデフォルト白テクスチャ。
    /// Ring/Cylinder/Helix の場合は対応する params でジオメトリを生成する
    /// （該当しないタイプでは params は無視される）。
    /// </summary>
    void Initialize(int primitiveType, const std::string& texturePath,
                    const PrimitiveGenerator::RingParams& ringParams = {},
                    const PrimitiveGenerator::CylinderParams& cylinderParams = {},
                    const PrimitiveGenerator::HelixParams& helixParams = {},
                    const PrimitiveGenerator::BeamParams& beamParams = {},
                    const PrimitiveGenerator::LightningBoltParams& lightningParams = {},
                    const PrimitiveGenerator::FrameParams& frameParams = {});

    void Update(Camera* camera, float deltaTime);
    void Draw();

    // プレビュー用WVPの更新と描画（同じインスタンスを別カメラで描画する場合に使う）
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);
    void DrawPreview();

    // ===== 動的に書き換えるパラメータ =====
    void SetTranslate(const Vector3& v) { mesh_.GetTransform().translate = v; }
    void SetScale(const Vector3& v)     { mesh_.GetTransform().scale     = v; }
    void SetRotate(const Vector3& v)    { mesh_.GetTransform().rotate    = v; }
    void SetRotateQuaternion(const Quaternion& q) { mesh_.SetRotateQuaternion(q); }
    void SetColor(const Vector4& c)     { mesh_.SetColor(c); }

    void SetBlendMode(PrimitivePipeline::BlendMode m) { mesh_.SetBlendMode(m); }
    void SetBillboardMode(BillboardMode m)            { mesh_.SetBillboardMode(m); }
    void SetDepthWrite(bool e)                        { mesh_.SetDepthWrite(e); }
    void SetCullBackface(bool e)                      { mesh_.SetCullBackface(e); }
    void SetAlphaReference(float v)                   { mesh_.SetAlphaReference(v); }
    void SetSamplerMode(int m)                        { mesh_.SetSamplerMode(m); }
    void SetViewAngleFadePower(float p)               { mesh_.SetViewAngleFadePower(p); }

    void SetUVScroll(const Vector2& v) { mesh_.SetUVScroll(v); }
    void SetUVOffset(const Vector2& v) { mesh_.SetUVOffset(v); }
    void SetUVScale(const Vector2& v)  { mesh_.SetUVScale(v); }
    void SetUVFlipU(bool b)            { mesh_.SetUVFlipU(b); }
    void SetUVFlipV(bool b)            { mesh_.SetUVFlipV(b); }
    void SetTexture(const std::string& path) { mesh_.SetTexture(path); }

    // ===== Dissolve（オブジェクト単位のディゾルブ）=====
    void SetDissolveMask(const std::string& path) { mesh_.SetDissolveMask(path); }
    void SetDissolve(bool enable, float threshold) { mesh_.SetDissolve(enable, threshold); }
    void SetDissolveEdge(bool enable, const Vector4& color, float width) { mesh_.SetDissolveEdge(enable, color, width); }

    // ===== Distortion =====
    /// <summary>
    /// 歪み源ノーマルマップを設定（空文字でリセット）。SRV は TextureManager 経由でキャッシュ。
    /// </summary>
    void SetDistortionTexture(const std::string& path);

    void SetDistortionUVScroll(const Vector2& v) { mesh_.SetDistortionUVScroll(v); }
    void SetDistortionUVOffset(const Vector2& v) { mesh_.SetDistortionUVOffset(v); }
    void SetDistortionUVScale(const Vector2& v)  { mesh_.SetDistortionUVScale(v); }
    void SetDistortionUVFlipU(bool b)            { mesh_.SetDistortionUVFlipU(b); }
    void SetDistortionUVFlipV(bool b)            { mesh_.SetDistortionUVFlipV(b); }
    void SetDistortionStrength(float s)          { mesh_.SetDistortionStrength(s); }

    bool HasDistortionTexture() const { return hasDistortionTexture_; }
    void DrawDistortionPass();
    void DrawDistortionPassPreview();

private:
    PrimitiveMesh mesh_;
    int primitiveType_ = 0;

    // Distortion 用ノーマルマップの SRV
    uint32_t distortionTextureSrvIndex_ = 0;
    bool     hasDistortionTexture_ = false;
};
