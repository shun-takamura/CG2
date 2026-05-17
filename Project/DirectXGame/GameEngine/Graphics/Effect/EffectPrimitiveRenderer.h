#pragma once
#include "PrimitiveMesh.h"
#include "PrimitivePipeline.h"
#include "BillboardMode.h"
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
    /// </summary>
    void Initialize(int primitiveType, const std::string& texturePath);

    void Update(Camera* camera, float deltaTime);
    void Draw();

    // プレビュー用WVPの更新と描画（同じインスタンスを別カメラで描画する場合に使う）
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);
    void DrawPreview();

    // ===== 動的に書き換えるパラメータ =====
    void SetTranslate(const Vector3& v) { mesh_.GetTransform().translate = v; }
    void SetScale(const Vector3& v)     { mesh_.GetTransform().scale     = v; }
    void SetRotate(const Vector3& v)    { mesh_.GetTransform().rotate    = v; }
    void SetColor(const Vector4& c)     { mesh_.SetColor(c); }

    void SetBlendMode(PrimitivePipeline::BlendMode m) { mesh_.SetBlendMode(m); }
    void SetBillboardMode(BillboardMode m)            { mesh_.SetBillboardMode(m); }
    void SetDepthWrite(bool e)                        { mesh_.SetDepthWrite(e); }
    void SetCullBackface(bool e)                      { mesh_.SetCullBackface(e); }
    void SetAlphaReference(float v)                   { mesh_.SetAlphaReference(v); }
    void SetSamplerMode(int m)                        { mesh_.SetSamplerMode(m); }

private:
    PrimitiveMesh mesh_;
    int primitiveType_ = 0;
};
