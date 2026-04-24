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

// 前方宣言
class Camera;

class PrimitiveMesh {
public:
    PrimitiveMesh() = default;
    ~PrimitiveMesh() = default;

    // 初期化（MeshDataを受け取ってGPUバッファ化）
    void Initialize(const MeshData& meshData);

    // 毎フレーム更新（WVP行列計算）
    void Update(Camera* camera);

    // 描画
    void Draw();

    // テクスチャ設定（パスを指定。未指定時は白テクスチャ相当の扱い）
    void SetTexture(const std::string& textureFilePath);

    // 各種設定
    void SetBlendMode(PrimitivePipeline::BlendMode mode) { blendMode_ = mode; }
    void SetDepthWrite(bool enable) { depthWrite_ = enable; }
    void SetColor(const Vector4& color) { color_ = color; }

    // α値がこれ以下のピクセルはdiscardされる（0.0でdiscardなし）
    void SetAlphaReference(float value) { alphaReference_ = value; }

    // UV変換設定
    void SetUVScroll(const Vector2& scrollPerSec) { uvScrollSpeed_ = scrollPerSec; }
    void SetUVFlipV(bool flip) { uvFlipV_ = flip; }
    void SetUVFlipU(bool flip) { uvFlipU_ = flip; }
    void SetUVScale(const Vector2& scale) { uvScale_ = scale; }
    void SetUVOffset(const Vector2& offset) { uvOffset_ = offset; } // 手動で設定したい場合

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

    // 変換行列バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformationMatrix* transformData_ = nullptr;

    // マテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    PrimitiveMaterial* materialData_ = nullptr;

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

    // 描画設定
    PrimitivePipeline::BlendMode blendMode_ = PrimitivePipeline::kBlendModeAdd;
    bool depthWrite_ = false;

    // テクスチャ（SRVインデックスで管理）
    uint32_t textureSrvIndex_ = 0;
    bool hasTexture_ = false;
};