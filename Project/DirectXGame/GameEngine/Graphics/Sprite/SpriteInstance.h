#pragma once
#include "SpriteManager.h"
#include "Vector2.h"
#include "Vector4.h"
#include "Material.h"
#include "SpriteVertexData.h"
#include "Transform.h"
#include "TransformationMatrix.h"
#include "BufferHelper.h"

class SpriteManager;

class SpriteInstance{

	SpriteManager* spriteManager_ = nullptr;

    // ===============================
    // バッファリソース
    // ===============================
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

    // ===============================
    // バッファ内データへのCPU側ポインタ
    // ===============================
    SpriteVertexData* vertexData_ = nullptr;
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

public:

    void CreateVertexResource();
    void CreateIndexResource();
    void CreateMaterialResource();
    void CreateTransformationMatrixResource();

    void CreateVertexBuffer();
    void CreateMaterialBuffer();
    void CreateTransformationMatrixBuffer();

	void Initialize(SpriteManager *spriteManager, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle);
    void Initialize(SpriteManager* spriteManager, const std::string& filePath);
    void Update(const Transform& transform);
    void Draw();
};

