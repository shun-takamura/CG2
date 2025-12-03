#pragma once
#include "Vector3.h"
#include "Vector2.h"
#include "Vector2Int.h"
#include "SpriteManager.h"
#include "Material.h"
#include "VertexData.h"
#include "Transform.h"
#include "TransformationMatrix.h"
#include "ConvertString.h"

class SpriteManager;

class SpriteInstance {

    Vector3 position_ = { 0.0f,0.0f,0.0f };
    Vector3 rotation_ = { 0.0f,0.0f,0.0f };
    Vector2Int size_ = { 320,180 };

    Transform transform{
        {1.0f,1.0f,1.0f},// s
        {0.0f,0.0f,0.0f},// r
        {0.0f,0.0f,0.0f} // t
    };

	SpriteManager* spriteManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

    // ===============================
    // バッファ内データへのCPU側ポインタ
    // ===============================
    VertexData* vertexData_ = nullptr;
    uint32_t* indexData_ = nullptr;

    // ===============================
    // D3D12 のビュー
    // ===============================
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW  indexBufferView_{};

    // マテリアル用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    // ===============================
    // 座標変換行列リソース
    // ===============================
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_ = nullptr;
    // バッファ内のデータを指すポインタ
    TransformationMatrix* transformationMatrixData_ = nullptr;

    // SRV の GPU ハンドル（PS の t0 に渡す用）
    D3D12_GPU_DESCRIPTOR_HANDLE textureGpuHandle_{};
    int textureNum_ = 0;

    void CreateVertexBuffer();
    void CreateMaterialBuffer();
    void CreateTransformationMatrixBuffer();
    //DirectX::ScratchImage LoadTexture(const std::string& filePath);
   

public:

    void Initialize(SpriteManager* spriteManager, const std::string& filePath);
    void Update();
    void Draw();
    ~SpriteInstance();

    // ゲッターロボ
    const Vector3& GetPosition()const { return position_; }
    const Vector3& GetRotation()const { return rotation_; }
    const Vector4& GetColor()const { return materialData_->color; }
    const Vector2Int& GetSize()const { return size_; }

    // セッター
    void SetPosition(const Vector3& position) { position_ = position; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetSize(const Vector2Int& size) { size_ = size; }
};

