#include "PrimitiveMesh.h"
#include "Camera.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include <cassert>
#include <cmath>

void PrimitiveMesh::Initialize(const MeshData& meshData) {
    CreateVertexResource(meshData);
    CreateIndexResource(meshData);
    CreateTransformResource();
    CreateMaterialResource();
}

void PrimitiveMesh::Update(Camera* camera) {
    // 旧API: dxCore のグローバル時間
    const float dt = PrimitivePipeline::GetInstance()->GetDxCore()->GetScaledDeltaTime();
    Update(camera, dt);
}

void PrimitiveMesh::Update(Camera* camera, float deltaTime) {

    // --- UV変換の累積 ---
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
    Matrix4x4 uvMat = MakeIdentity4x4();
    uvMat.m[0][0] = sx;
    uvMat.m[1][1] = sy;
    uvMat.m[3][0] = tx;
    uvMat.m[3][1] = ty;
    materialData_->uvTransform = uvMat;

    // メインカメラ用のワールド行列とWVPを構築・書き込み
    Matrix4x4 worldMatrix = BuildWorldMatrix(camera);
    Matrix4x4 wvpMatrix;
    if (camera) {
        wvpMatrix = Multiply(worldMatrix, camera->GetViewProjectionMatrix());
    } else {
        wvpMatrix = worldMatrix;
    }
    transformData_->WVP = wvpMatrix;
    transformData_->World = worldMatrix;

    // マテリアルの色・alphaReference・samplerMode を更新
    materialData_->color = color_;
    materialData_->alphaReference = alphaReference_;
    materialData_->samplerMode = samplerMode_;

}

void PrimitiveMesh::UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos) {
    if (!transformPreviewData_) return;

    // プレビュー用のワールド行列（billboardはプレビューカメラ基準で再計算される）
    Matrix4x4 worldMatrix = BuildWorldMatrixFromMatrices(viewMatrix, cameraPos);
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
    transformPreviewData_->WVP = wvpMatrix;
    transformPreviewData_->World = worldMatrix;
}

Matrix4x4 PrimitiveMesh::BuildWorldMatrixFromMatrices(const Matrix4x4& viewMatrix, const Vector3& cameraPos) const {
    if (billboardMode_ == BillboardMode::None) {
        return MakeAffineMatrix(transform_);
    }
    Matrix4x4 scaleMat     = MakeScaleMatrix(transform_);
    Matrix4x4 rotateMat    = MakeRotateMatrix(transform_.rotate);
    Matrix4x4 translateMat = MakeTranslateMatrix(transform_);

    Matrix4x4 billboardMat = MakeIdentity4x4();
    if (billboardMode_ == BillboardMode::Full) {
        billboardMat.m[0][0] = viewMatrix.m[0][0];
        billboardMat.m[0][1] = viewMatrix.m[1][0];
        billboardMat.m[0][2] = viewMatrix.m[2][0];
        billboardMat.m[1][0] = viewMatrix.m[0][1];
        billboardMat.m[1][1] = viewMatrix.m[1][1];
        billboardMat.m[1][2] = viewMatrix.m[2][1];
        billboardMat.m[2][0] = viewMatrix.m[0][2];
        billboardMat.m[2][1] = viewMatrix.m[1][2];
        billboardMat.m[2][2] = viewMatrix.m[2][2];
    } else { // YAxis
        float fx = cameraPos.x - transform_.translate.x;
        float fz = cameraPos.z - transform_.translate.z;
        float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-5f) { fx = 0.0f; fz = 1.0f; }
        else             { fx /= len; fz /= len; }
        billboardMat.m[0][0] = fz;   billboardMat.m[0][1] = 0.0f; billboardMat.m[0][2] = -fx;
        billboardMat.m[1][0] = 0.0f; billboardMat.m[1][1] = 1.0f; billboardMat.m[1][2] = 0.0f;
        billboardMat.m[2][0] = fx;   billboardMat.m[2][1] = 0.0f; billboardMat.m[2][2] = fz;
    }

    return Multiply(Multiply(Multiply(scaleMat, rotateMat), billboardMat), translateMat);
}

Matrix4x4 PrimitiveMesh::BuildWorldMatrix(Camera* camera) const {
    if (billboardMode_ == BillboardMode::None || !camera) {
        return MakeAffineMatrix(transform_);
    }
    // Scale → Rotate（自己回転）→ Billboard → Translate
    Matrix4x4 scaleMat     = MakeScaleMatrix(transform_);
    Matrix4x4 rotateMat    = MakeRotateMatrix(transform_.rotate);
    Matrix4x4 translateMat = MakeTranslateMatrix(transform_);

    Matrix4x4 billboardMat = MakeIdentity4x4();
    if (billboardMode_ == BillboardMode::Full) {
        const Matrix4x4& view = camera->GetViewMatrix();
        billboardMat.m[0][0] = view.m[0][0];
        billboardMat.m[0][1] = view.m[1][0];
        billboardMat.m[0][2] = view.m[2][0];
        billboardMat.m[1][0] = view.m[0][1];
        billboardMat.m[1][1] = view.m[1][1];
        billboardMat.m[1][2] = view.m[2][1];
        billboardMat.m[2][0] = view.m[0][2];
        billboardMat.m[2][1] = view.m[1][2];
        billboardMat.m[2][2] = view.m[2][2];
    } else { // YAxis
        Vector3 camPos = camera->GetTranslate();
        float fx = camPos.x - transform_.translate.x;
        float fz = camPos.z - transform_.translate.z;
        float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-5f) { fx = 0.0f; fz = 1.0f; }
        else             { fx /= len; fz /= len; }
        billboardMat.m[0][0] = fz;   billboardMat.m[0][1] = 0.0f; billboardMat.m[0][2] = -fx;
        billboardMat.m[1][0] = 0.0f; billboardMat.m[1][1] = 1.0f; billboardMat.m[1][2] = 0.0f;
        billboardMat.m[2][0] = fx;   billboardMat.m[2][1] = 0.0f; billboardMat.m[2][2] = fz;
    }

    return Multiply(Multiply(Multiply(scaleMat, rotateMat), billboardMat), translateMat);
}

void PrimitiveMesh::Draw() {
    // パイプラインの事前設定
    PrimitivePipeline::GetInstance()->PreDraw(blendMode_, depthWrite_, cullBackface_);

    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();
    ID3D12GraphicsCommandList* commandList = dxCore->GetCommandList();

    // 頂点バッファとインデックスバッファをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // [0] VS: TransformationMatrix (b0) — メイン用CB
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

void PrimitiveMesh::DrawIdPass(uint32_t objectId) {
    if (!transformResource_) return;

    auto* pp = PrimitivePipeline::GetInstance();
    auto* cmd = pp->GetDxCore()->GetCommandList();

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootSignature(pp->GetIdRootSignature());
    cmd->SetPipelineState(pp->GetIdPipelineState());

    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->IASetIndexBuffer(&indexBufferView_);

    // VS CBV b0 = TransformationMatrix
    cmd->SetGraphicsRootConstantBufferView(0, transformResource_->GetGPUVirtualAddress());
    // PS Root Constant b0 = objectId
    cmd->SetGraphicsRoot32BitConstant(1, objectId, 0);

    cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void PrimitiveMesh::DrawPreview() {
    if (!transformPreviewResource_) return;

    // パイプラインの事前設定
    PrimitivePipeline::GetInstance()->PreDraw(blendMode_, depthWrite_, cullBackface_);

    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();
    ID3D12GraphicsCommandList* commandList = dxCore->GetCommandList();

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // [0] VS: TransformationMatrix (b0) — プレビュー用CB
    commandList->SetGraphicsRootConstantBufferView(
        0, transformPreviewResource_->GetGPUVirtualAddress());

    commandList->SetGraphicsRootConstantBufferView(
        1, materialResource_->GetGPUVirtualAddress());

    if (hasTexture_) {
        SRVManager* srvManager = PrimitivePipeline::GetInstance()->GetSRVManager();
        commandList->SetGraphicsRootDescriptorTable(
            2, srvManager->GetGPUDescriptorHandle(textureSrvIndex_));
    }

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

    // メイン用
    transformResource_ = dxCore->CreateBufferResource(sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();

    // プレビュー用（同じインスタンスを別カメラで描画するため）
    transformPreviewResource_ = dxCore->CreateBufferResource(sizeof(TransformationMatrix));
    transformPreviewResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformPreviewData_));
    transformPreviewData_->WVP = MakeIdentity4x4();
    transformPreviewData_->World = MakeIdentity4x4();
}

void PrimitiveMesh::CreateMaterialResource() {
    DirectXCore* dxCore = PrimitivePipeline::GetInstance()->GetDxCore();

    materialResource_ = dxCore->CreateBufferResource(sizeof(PrimitiveMaterial));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = color_;
    materialData_->enableLighting = 0;
    materialData_->alphaReference = alphaReference_;
    materialData_->samplerMode = samplerMode_;
    materialData_->padding = 0.0f;
    materialData_->uvTransform = MakeIdentity4x4();
}