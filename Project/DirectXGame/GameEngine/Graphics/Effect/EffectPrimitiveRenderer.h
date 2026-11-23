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
/// エフェクトのメッシュの形（EffectDef の meshType / EffectPrimitiveRenderer::Initialize の primitiveType に入る値）。
/// エフェクトの JSON に数字で保存しているので、並びを変えない（足すときは Count の手前に）
/// </summary>
namespace EffectMeshType {
    enum : int {
        Plane,
        Box,
        Sphere,
        Ring,
        Cylinder,
        Helix,
        Beam,
        Lightning,
        Hemisphere,
        Frame,
        Count,
    };
}

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
    /// <param name="primitiveType">形状（0=Plane, 1=Box, 2=Sphere, 3=Ring, 4=Cylinder, 5=Helix, 6=Beam, 7=Lightning, 8=Hemisphere, 9=Frame）</param>
    /// <param name="texturePath">テクスチャのパス（空なら white1x1）</param>
    /// <param name="ringParams">Ring の形状パラメータ</param>
    /// <param name="cylinderParams">Cylinder の形状パラメータ</param>
    /// <param name="helixParams">Helix の形状パラメータ</param>
    /// <param name="beamParams">Beam の形状パラメータ</param>
    /// <param name="lightningParams">Lightning の形状パラメータ</param>
    /// <param name="frameParams">Frame の形状パラメータ</param>
    void Initialize(int primitiveType, const std::string& texturePath,
                    const PrimitiveGenerator::RingParams& ringParams = {},
                    const PrimitiveGenerator::CylinderParams& cylinderParams = {},
                    const PrimitiveGenerator::HelixParams& helixParams = {},
                    const PrimitiveGenerator::BeamParams& beamParams = {},
                    const PrimitiveGenerator::LightningBoltParams& lightningParams = {},
                    const PrimitiveGenerator::FrameParams& frameParams = {});

    /// <summary>メッシュの行列・UV スクロールを進める</summary>
    /// <param name="camera">ビルボードと WVP に使うカメラ</param>
    /// <param name="deltaTime">経過時間 [秒]</param>
    void Update(Camera* camera, float deltaTime);
    /// <summary>シーンへ描く</summary>
    void Draw();

    /// <summary>プレビュー用 WVP を更新する（同じインスタンスを別カメラで描画する場合に使う）</summary>
    /// <param name="viewMatrix">プレビューカメラのビュー行列</param>
    /// <param name="viewProjectionMatrix">プレビューカメラのビュープロジェクション行列</param>
    /// <param name="cameraPos">プレビューカメラの位置</param>
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);
    /// <summary>プレビュー用 WVP で描く</summary>
    void DrawPreview();

    /// <summary>水面の反射 RT へ描く（鏡像の視点）</summary>
    /// <param name="mirroredView">水面で折り返したビュー行列</param>
    /// <param name="mirroredViewProjection">水面で折り返したビュープロジェクション行列</param>
    /// <param name="mirroredCameraPos">水面で折り返したカメラ位置</param>
    void DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos) {
        mesh_.DrawReflection(mirroredView, mirroredViewProjection, mirroredCameraPos);
    }

    // ===== 動的に書き換えるパラメータ =====
    /// <summary>位置を設定する</summary>
    /// <param name="v">ワールド座標</param>
    void SetTranslate(const Vector3& v) { mesh_.GetTransform().translate = v; }
    /// <summary>拡大率を設定する</summary>
    /// <param name="v">軸ごとの拡大率</param>
    void SetScale(const Vector3& v)     { mesh_.GetTransform().scale     = v; }
    /// <summary>向きをオイラー角で設定する</summary>
    /// <param name="v">オイラー角 [ラジアン]</param>
    void SetRotate(const Vector3& v)    { mesh_.GetTransform().rotate    = v; }
    /// <summary>向きをクォータニオンで設定する</summary>
    /// <param name="q">向き</param>
    void SetRotateQuaternion(const Quaternion& q) { mesh_.SetRotateQuaternion(q); }
    /// <summary>色を設定する</summary>
    /// <param name="c">RGBA</param>
    void SetColor(const Vector4& c)     { mesh_.SetColor(c); }

    /// <summary>ブレンドモードを設定する</summary>
    /// <param name="m">ブレンドモード</param>
    void SetBlendMode(PrimitivePipeline::BlendMode m) { mesh_.SetBlendMode(m); }
    /// <summary>ビルボードの種類を設定する</summary>
    /// <param name="m">ビルボードの種類</param>
    void SetBillboardMode(BillboardMode m)            { mesh_.SetBillboardMode(m); }
    /// <summary>深度を書くか設定する</summary>
    /// <param name="e">書くなら true</param>
    void SetDepthWrite(bool e)                        { mesh_.SetDepthWrite(e); }
    /// <summary>背面カリングするか設定する</summary>
    /// <param name="e">カリングするなら true</param>
    void SetCullBackface(bool e)                      { mesh_.SetCullBackface(e); }
    /// <summary>この α 未満のピクセルを捨てる閾値を設定する</summary>
    /// <param name="v">閾値（0 で捨てない）</param>
    void SetAlphaReference(float v)                   { mesh_.SetAlphaReference(v); }
    /// <summary>サンプラーを設定する</summary>
    /// <param name="m">0=WrapAll, 1=WrapU+ClampV, 2=ClampAll</param>
    void SetSamplerMode(int m)                        { mesh_.SetSamplerMode(m); }
    /// <summary>斜めから見た面を薄くする強さを設定する</summary>
    /// <param name="p">べき乗の指数（0 で無効）</param>
    void SetViewAngleFadePower(float p)               { mesh_.SetViewAngleFadePower(p); }

    /// <summary>UV のスクロール速度を設定する</summary>
    /// <param name="v">1 秒あたりのスクロール量</param>
    void SetUVScroll(const Vector2& v) { mesh_.SetUVScroll(v); }
    /// <summary>UV のオフセットを設定する</summary>
    /// <param name="v">オフセット</param>
    void SetUVOffset(const Vector2& v) { mesh_.SetUVOffset(v); }
    /// <summary>UV の拡大率を設定する</summary>
    /// <param name="v">拡大率</param>
    void SetUVScale(const Vector2& v)  { mesh_.SetUVScale(v); }
    /// <summary>U を反転するか設定する</summary>
    /// <param name="b">反転するなら true</param>
    void SetUVFlipU(bool b)            { mesh_.SetUVFlipU(b); }
    /// <summary>V を反転するか設定する</summary>
    /// <param name="b">反転するなら true</param>
    void SetUVFlipV(bool b)            { mesh_.SetUVFlipV(b); }
    /// <summary>テクスチャを差し替える</summary>
    /// <param name="path">テクスチャのパス</param>
    void SetTexture(const std::string& path) { mesh_.SetTexture(path); }

    // ===== Dissolve（オブジェクト単位のディゾルブ）=====
    /// <summary>ディゾルブのマスクを設定する</summary>
    /// <param name="path">グレースケールのマスクのパス（空で外す）</param>
    void SetDissolveMask(const std::string& path) { mesh_.SetDissolveMask(path); }
    /// <summary>ディゾルブの有効化と閾値を設定する</summary>
    /// <param name="enable">有効にするなら true</param>
    /// <param name="threshold">閾値（0=完全表示 / 1=完全に消えた）</param>
    void SetDissolve(bool enable, float threshold) { mesh_.SetDissolve(enable, threshold); }
    /// <summary>ディゾルブの燃える縁を設定する</summary>
    /// <param name="enable">縁を出すなら true</param>
    /// <param name="color">縁の色</param>
    /// <param name="width">縁の幅（閾値の単位）</param>
    void SetDissolveEdge(bool enable, const Vector4& color, float width) { mesh_.SetDissolveEdge(enable, color, width); }

    // ===== Distortion =====
    /// <summary>
    /// 歪み源ノーマルマップを設定（空文字でリセット）。SRV は TextureManager 経由でキャッシュ。
    /// </summary>
    /// <param name="path">ノーマルマップ風 RG テクスチャのパス（空で歪みなし）</param>
    void SetDistortionTexture(const std::string& path);

    /// <summary>歪みテクスチャの UV スクロール速度を設定する</summary>
    /// <param name="v">1 秒あたりのスクロール量</param>
    void SetDistortionUVScroll(const Vector2& v) { mesh_.SetDistortionUVScroll(v); }
    /// <summary>歪みテクスチャの UV オフセットを設定する</summary>
    /// <param name="v">オフセット</param>
    void SetDistortionUVOffset(const Vector2& v) { mesh_.SetDistortionUVOffset(v); }
    /// <summary>歪みテクスチャの UV 拡大率を設定する</summary>
    /// <param name="v">拡大率</param>
    void SetDistortionUVScale(const Vector2& v)  { mesh_.SetDistortionUVScale(v); }
    /// <summary>歪みテクスチャの U を反転するか設定する</summary>
    /// <param name="b">反転するなら true</param>
    void SetDistortionUVFlipU(bool b)            { mesh_.SetDistortionUVFlipU(b); }
    /// <summary>歪みテクスチャの V を反転するか設定する</summary>
    /// <param name="b">反転するなら true</param>
    void SetDistortionUVFlipV(bool b)            { mesh_.SetDistortionUVFlipV(b); }
    /// <summary>歪みの強さを設定する</summary>
    /// <param name="s">強さ（0〜1。texture.a × vertex.color.a と乗算）</param>
    void SetDistortionStrength(float s)          { mesh_.SetDistortionStrength(s); }

    /// <summary>歪みテクスチャを持っているか</summary>
    /// <returns>設定済みなら true</returns>
    bool HasDistortionTexture() const { return hasDistortionTexture_; }
    /// <summary>歪み RT へ描く。歪みテクスチャが無ければ何もしない</summary>
    void DrawDistortionPass();
    /// <summary>DrawDistortionPass のプレビュー版（プレビュー WVP を使う）</summary>
    void DrawDistortionPassPreview();

    // ===== Bloom =====
    /// <summary>発光 RT へ「見た目 × intensity」を描く</summary>
    /// <param name="intensity">発光の強さ</param>
    void DrawBloomPass(float intensity)        { mesh_.DrawBloomPass(intensity); }
    /// <summary>DrawBloomPass のプレビュー版（プレビュー WVP を使う）</summary>
    /// <param name="intensity">発光の強さ</param>
    void DrawBloomPassPreview(float intensity) { mesh_.DrawBloomPassPreview(intensity); }

private:
    PrimitiveMesh mesh_;
    int primitiveType_ = 0;

    // Distortion 用ノーマルマップの SRV
    uint32_t distortionTextureSrvIndex_ = 0;
    bool     hasDistortionTexture_ = false;
};
