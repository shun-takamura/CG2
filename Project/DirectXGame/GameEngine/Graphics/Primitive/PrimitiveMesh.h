#pragma once
#include "PrimitivePipeline.h"
#include "MeshData.h"
#include "PrimitiveMaterial.h"
#include "Material.h"
#include "Transform.h"
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include "Vector2.h"
#include "BillboardMode.h"

// 前方宣言
class Camera;

class PrimitiveMesh {
public:
    PrimitiveMesh() = default;
    ~PrimitiveMesh() = default;

    // 初期化（MeshDataを受け取ってGPUバッファ化）
    void Initialize(const MeshData& meshData);

    // 毎フレーム更新（WVP行列計算）
    // 旧版: dxCore のグローバル時間で UV スクロールが進む
    void Update(Camera* camera);
    // 新版: 呼び出し側が渡した deltaTime で UV スクロールが進む（TimeGroup 連動用）
    void Update(Camera* camera, float deltaTime);

    // プレビュー用の WVP を別CBに書き込む（メインの Update とは独立）。
    // 同じインスタンスを Scene RT と Effect Preview RT の両方に描画するときに使う。
    // billboard 用にカメラ位置とビュー行列、WVP合成にViewProjection行列が必要。
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);

    // 描画（メイン用 CB を bind）
    void Draw();

    // ID Pass：idMaskRT に objectId を書き込む
    void DrawIdPass(uint32_t objectId);

    // Distortion Pass：DistortionRT にノーマルマップ由来の歪み情報を書き込む
    // 通常テクスチャとは独立した UV 変換（distortionUv*）で描画される
    void DrawDistortionPass(uint32_t normalMapSrvIndex);

    // 上記のプレビュー版。VS CBV には transformPreviewResource_（プレビューカメラの WVP）を bind する。
    void DrawDistortionPassPreview(uint32_t normalMapSrvIndex);

    // プレビュー用 CB を bind して描画
    void DrawPreview();

    // テクスチャ設定（パスを指定。未指定時は白テクスチャ相当の扱い）
    void SetTexture(const std::string& textureFilePath);

    // 各種設定
    void SetBlendMode(PrimitivePipeline::BlendMode mode) { blendMode_ = mode; }
    void SetDepthWrite(bool enable) { depthWrite_ = enable; }
    void SetCullBackface(bool enable) { cullBackface_ = enable; }
    void SetColor(const Vector4& color) { color_ = color; }

    // α値がこれ以下のピクセルはdiscardされる（0.0でdiscardなし）
    void SetAlphaReference(float value) { alphaReference_ = value; }

    // サンプラーモード（0=WrapAll, 1=WrapU+ClampV, 2=ClampAll）
    void SetSamplerMode(int mode) { samplerMode_ = mode; }
    int  GetSamplerMode() const { return samplerMode_; }

    // ビルボードモード（None / Full / YAxis）
    void SetBillboardMode(BillboardMode mode) { billboardMode_ = mode; }
    BillboardMode GetBillboardMode() const { return billboardMode_; }

    // UV変換設定
    void SetUVScroll(const Vector2& scrollPerSec) { uvScrollSpeed_ = scrollPerSec; }
    void SetUVFlipV(bool flip) { uvFlipV_ = flip; }
    void SetUVFlipU(bool flip) { uvFlipU_ = flip; }
    void SetUVScale(const Vector2& scale) { uvScale_ = scale; }
    void SetUVOffset(const Vector2& offset) { uvOffset_ = offset; } // 手動で設定したい場合

    // Distortion 用 UV 変換設定（通常テクスチャの UV と完全に独立）
    void SetDistortionUVScroll(const Vector2& scrollPerSec) { distortionUVScrollSpeed_ = scrollPerSec; }
    void SetDistortionUVFlipU(bool flip) { distortionUVFlipU_ = flip; }
    void SetDistortionUVFlipV(bool flip) { distortionUVFlipV_ = flip; }
    void SetDistortionUVScale(const Vector2& scale) { distortionUVScale_ = scale; }
    void SetDistortionUVOffset(const Vector2& offset) { distortionUVOffset_ = offset; }
    // per-instance の歪み強度（0..1）。 distortionMaterialData_->color.a に書かれ、シェーダーで texture.a × vertex.color.a × strength として使われる。
    void SetDistortionStrength(float s) { distortionStrength_ = s; }

    // Transformアクセス（translate/rotate/scaleを外部から書き換え可能）
    Transform& GetTransform() { return transform_; }
    const Transform& GetTransform() const { return transform_; }

    // ゲッター
    PrimitivePipeline::BlendMode GetBlendMode() const { return blendMode_; }
    bool GetDepthWrite() const { return depthWrite_; }
    const Vector4& GetColor() const { return color_; }

private:
    // GPUリソースの作成
    void CreateVertexResource(const MeshData& meshData);
    void CreateIndexResource(const MeshData& meshData);
    void CreateTransformResource();
    void CreateMaterialResource();

    // 指定カメラに対するワールド行列を構築（billboardMode に応じて回転に補正がかかる）
    Matrix4x4 BuildWorldMatrix(Camera* camera) const;

    // ビュー行列とカメラ位置から billboard 補正込みのワールド行列を構築
    Matrix4x4 BuildWorldMatrixFromMatrices(const Matrix4x4& viewMatrix, const Vector3& cameraPos) const;

    // GPU送信用の変換行列構造体
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t indexCount_ = 0;

    // 変換行列バッファ（メイン用）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformationMatrix* transformData_ = nullptr;

    // 変換行列バッファ（プレビュー用）。同じワールド座標を別カメラで描画するための WVP 専用 CB。
    // ワールド行列は同じものを格納（プレビュー描画でも World 用途に使えるよう）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformPreviewResource_;
    TransformationMatrix* transformPreviewData_ = nullptr;

    // マテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    PrimitiveMaterial* materialData_ = nullptr;

    // Distortion 用マテリアル CB（distortion パスでのみ bind される）
    // 通常テクスチャと独立した uvTransform を持つ
    Microsoft::WRL::ComPtr<ID3D12Resource> distortionMaterialResource_;
    PrimitiveMaterial* distortionMaterialData_ = nullptr;

    // Transform
    Transform transform_{
        { 1.0f, 1.0f, 1.0f }, // scale
        { 0.0f, 0.0f, 0.0f }, // rotate
        { 0.0f, 0.0f, 0.0f }  // translate
    };

    // 色
    Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };

    // UV変換関連
    Vector2 uvScrollSpeed_ = { 0.0f, 0.0f };    // 1秒あたりのスクロール量
    Vector2 uvScrollAccumulated_ = { 0.0f, 0.0f }; // スクロール累積値
    Vector2 uvOffset_ = { 0.0f, 0.0f };         // 手動設定分
    Vector2 uvScale_ = { 1.0f, 1.0f };
    bool uvFlipU_ = false;
    bool uvFlipV_ = false;
    float alphaReference_ = 0.0f; // デフォルト：discardしない
    int   samplerMode_ = 0;       // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll

    // Distortion 用 UV 変換関連（通常テクスチャと別の蓄積・状態を持つ）
    Vector2 distortionUVScrollSpeed_     = { 0.0f, 0.0f };
    Vector2 distortionUVScrollAccumulated_ = { 0.0f, 0.0f };
    Vector2 distortionUVOffset_          = { 0.0f, 0.0f };
    Vector2 distortionUVScale_           = { 1.0f, 1.0f };
    bool    distortionUVFlipU_ = false;
    bool    distortionUVFlipV_ = false;
    float   distortionStrength_ = 1.0f; // per-instance 歪み強度（distortionMaterialData_->color.a へ書く）

    // 描画設定
    PrimitivePipeline::BlendMode blendMode_ = PrimitivePipeline::kBlendModeAdd;
    bool depthWrite_ = false;
    bool cullBackface_ = false; // true で背面カリング、false で両面描画

    // ビルボードモード
    BillboardMode billboardMode_ = BillboardMode::None;

    // テクスチャ（SRVインデックスで管理）
    uint32_t textureSrvIndex_ = 0;
    bool hasTexture_ = false;
};