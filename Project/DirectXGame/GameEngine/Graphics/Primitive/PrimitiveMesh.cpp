#include "PrimitiveMesh.h"
#include "Camera.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include <cassert>

void PrimitiveMesh::Initialize(const MeshData& meshData) {
    CreateVertexResource(meshData);
    CreateIndexResource(meshData);
    CreateTransformResource();
    CreateMaterialResource();
}

void PrimitiveMesh::Update(Camera* camera) {

    // --- UV変換の累積 ---
    const float deltaTime = PrimitivePipeline::GetInstance()->GetDxCore()->GetDeltaTime();
    uvScrollAccumulated_.x += uvScrollSpeed_.x * deltaTime;
    uvScrollAccumulated_.y += uvScrollSpeed_.y * deltaTime;

    // UV変換行列を構築（Scale → Flip → Translate）
    // Scale
    float sx = uvScale_.x;
    float sy = uvScale_.y;
    // Flip適用（flipするとScaleに-1倍）
    if (uvFlipU_) sx = -sx;
    if (uvFlipV_) sy = -sy;

    // Translate（累積スクロール + 手動オフセット + Flipの中心補正）
    float tx = uvScrollAccumulated_.x + uvOffset_.x;
    float ty = uvScrollAccumulated_.y + uvOffset_.y;
    // Flip時はUVが負になるので+1オフセットで0〜1範囲に戻す
    if (uvFlipU_) tx += 1.0f;
    if (uvFlipV_) ty += 1.0f;

    // uvTransform行列に設定
    // | sx  0   0  0 |
    // |  0 sy   0  0 |
    // |  0  0   1  0 |
    // | tx ty   0  1 |
    Matrix4x4 uvMat = MakeIdentity4x4();
    uvMat.m[0][0] = sx;
    uvMat.m[1][1] = sy;
    uvMat.m[3][0] = tx;
    uvMat.m[3][1] = ty;
    materialData_->uvTransform = uvMat;

    // World行列（SRT）
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);

    // WVP行列
    Matrix4x4 wvpMatrix;
    if (camera) {
        const Matrix4x4& viewProjection = camera->GetViewProjectionMatrix();
        wvpMatrix = Multiply(worldMatrix, viewProjection);
    } else {
        wvpMatrix = worldMatrix;
    }

    // 定数バッファに書き込む
    transformData_->WVP = wvpMatrix;
    transformData_->World = worldMatrix;

    // マテリアルの色とalphaReferenceを更新
    materialData_->color = color_;
    materialData_->alphaReference = alphaReference_;

}

void PrimitiveMesh::Draw() {
    // パイプラインの事前設定
    PrimitivePipeline::GetInstance()->PreDraw(blendMode_, depthWrite_);

    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();
    ID3D12GraphicsCommandList* commandList = dxCore->GetCommandList();

    // 頂点バッファとインデックスバッファをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // [0] VS: TransformationMatrix (b0)
    commandList->SetGraphicsRootConstantBufferView(
        0, transformResource_->GetGPUVirtualAddress());

    // [1] PS: Material (b0)
    commandList->SetGraphicsRootConstantBufferView(
        1, materialResource_->GetGPUVirtualAddress());

    // [2] PS: テクスチャ（設定されている場合のみ）
    if (hasTexture_) {
        SRVManager* srvManager = PrimitivePipeline::GetInstance()->GetSRVManager();
        commandList->SetGraphicsRootDescriptorTable(
            2, srvManager->GetGPUDescriptorHandle(textureSrvIndex_));
    }

    // 描画
    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void PrimitiveMesh::SetTexture(const std::string& textureFilePath) {
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath);
    hasTexture_ = true;
}

void PrimitiveMesh::CreateVertexResource(const MeshData& meshData) {
    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();

    vertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
    size_t sizeInBytes = sizeof(MeshVertex) * vertexCount_;

    // バッファ作成
    vertexResource_ = dxCore->CreateBufferResource(sizeInBytes);

    // VBV
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(MeshVertex);

    // マップしてコピー
    MeshVertex* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, meshData.vertices.data(), sizeInBytes);
    vertexResource_->Unmap(0, nullptr);
}

void PrimitiveMesh::CreateIndexResource(const MeshData& meshData) {
    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();

    indexCount_ = static_cast<uint32_t>(meshData.indices.size());
    size_t sizeInBytes = sizeof(uint32_t) * indexCount_;

    // バッファ作成
    indexResource_ = dxCore->CreateBufferResource(sizeInBytes);

    // IBV
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    // マップしてコピー
    uint32_t* indexData = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::memcpy(indexData, meshData.indices.data(), sizeInBytes);
    indexResource_->Unmap(0, nullptr);
}

void PrimitiveMesh::CreateTransformResource() {
    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();

    transformResource_ = dxCore->CreateBufferResource(sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));

    // 単位行列で初期化
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();
}

void PrimitiveMesh::CreateMaterialResource() {
    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();

    materialResource_ = dxCore->CreateBufferResource(sizeof(PrimitiveMaterial));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = color_;
    materialData_->enableLighting = 0;
    materialData_->alphaReference = alphaReference_;
    materialData_->padding[0] = 0.0f;
    materialData_->padding[1] = 0.0f;
    materialData_->uvTransform = MakeIdentity4x4();
}