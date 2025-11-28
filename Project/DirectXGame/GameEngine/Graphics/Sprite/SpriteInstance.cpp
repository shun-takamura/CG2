#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "BufferHelper.h"
#include "MathUtility.h"

void SpriteInstance::CreateVertexResource()
{
    // 4頂点分のバッファ
    vertexResource_ = spriteManager_->CreateBufferResource(
        sizeof(SpriteVertexData) * 4
    );

    // GPU → CPU 編集用マップ
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    // 初期値
    vertexData_[0].pos = { -0.5f,  0.5f, 0.0f, 1.0f };
    vertexData_[1].pos = { 0.5f,  0.5f, 0.0f, 1.0f };
    vertexData_[2].pos = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData_[3].pos = { 0.5f, -0.5f, 0.0f, 1.0f };

    vertexData_[0].texcoord = { 0.0f, 0.0f };
    vertexData_[1].texcoord = { 1.0f, 0.0f };
    vertexData_[2].texcoord = { 0.0f, 1.0f };
    vertexData_[3].texcoord = { 1.0f, 1.0f };

    // VBV設定
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(SpriteVertexData) * 4;
    vertexBufferView_.StrideInBytes = sizeof(SpriteVertexData);
}


void SpriteInstance::CreateIndexResource()
{
    uint32_t indices[6] = { 0,1,2, 2,1,3 };

    indexResource_ = spriteManager_->CreateBufferResource(sizeof(uint32_t) * 6);
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));
    memcpy(indexData_, indices, sizeof(indices));

    // IBV 設定
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}


void SpriteInstance::CreateMaterialResource()
{
    materialResource_ = spriteManager_->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = Vector4(1, 1, 1, 1);
    materialData_->enableLighting = false;
    materialData_->uvTransform = MakeIdentity4x4();
}

void SpriteInstance::CreateTransformationMatrixResource()
{
    transformationMatrixResource_ =
        spriteManager_->CreateBufferResource(sizeof(TransformationMatrix));

    transformationMatrixResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_)
    );

    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WVP = MakeIdentity4x4();
}

void SpriteInstance::CreateVertexBuffer()
{

    // 4頂点のバッファを作るので *4
    vertexResource = CreateBufferResource(sizeof(VertexData) * 4);

    // CPUアクセス準備
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 座標セット（左上・右上・左下・右下）
    vertexData[0].position = { 0, 0, 0, 1 };
    vertexData[1].position = { size.x, 0, 0, 1 };
    vertexData[2].position = { 0, size.y, 0, 1 };
    vertexData[3].position = { size.x, size.y, 0, 1 };

    // UV
    vertexData[0].texcoord = { 0, 0 };
    vertexData[1].texcoord = { 1, 0 };
    vertexData[2].texcoord = { 0, 1 };
    vertexData[3].texcoord = { 1, 1 };

    // バッファビュー設定
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView.StrideInBytes = sizeof(VertexData);
}

void SpriteInstance::CreateMaterialBuffer()
{
    materialResource = CreateBufferResource(sizeof(Material));
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

    materialData->color = { 1,1,1,1 };
    materialData->enableLighting = false;
    materialData->uvTransform = MakeIdentity4x4();
}

void SpriteInstance::CreateTransformationMatrixBuffer()
{
    transformationMatrixResource = CreateBufferResource(sizeof(TransformationMatrix));
    transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));

    transformationMatrixData->World = MakeIdentity4x4();
    transformationMatrixData->WVP = MakeIdentity4x4();
}

void SpriteInstance::Initialize(SpriteManager* spriteManager, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle)
{
    spriteManager_ = spriteManager;
    textureGpuHandle_ = textureHandle;
    auto* dxCore = spriteManager_->GetDxCore();

    // =============== 1. Vertex Buffer 作成 ===============
    vertexResource_ = CreateBufferResource(
        dxCore->GetDevice(),
        sizeof(SpriteVertexData) * 4
    );

    // Vertex マップ
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    // =============== 2. Index Buffer 作成 ===============
    indexResource_ = CreateBufferResource(
        dxCore->GetDevice(),
        sizeof(uint32_t) * 6
    );

    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

    // =============== 3. Material ConstantBuffer 作成 ===============
    materialResource_ = CreateBufferResource(
        dxCore->GetDevice(),
        sizeof(Material)
    );

    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // =============== 4. TransformationMatrix ConstantBuffer 作成 ===============
    transformationMatrixResource_ = CreateBufferResource(
        dxCore->GetDevice(),
        sizeof(TransformationMatrix)
    );

    // マップして CPU から編集できるようにする
    transformationMatrixResource_->Map(
        0, nullptr,
        reinterpret_cast<void**>(&transformationMatrixData_)
    );

    // 初期値（単位行列）を書いておく
    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WVP = MakeIdentity4x4();

    // 初期値をセット
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->enableLighting = false;
    materialData_->uvTransform = MakeIdentity4x4();
}

void SpriteInstance::Initialize(SpriteManager* spriteManager, const std::string& filePath)
{
    spriteManager_ = spriteManager;

    // 1. テクスチャ読み込み → GPU ハンドル取得
    //textureSrvHandleGPU_ = spriteManager_->LoadTextureAndCreateSrv(filePath);

    // 2. 頂点バッファの作成（あなたの現在のコードのままでOK）
    CreateVertexResource();
    CreateIndexResource();

    // 3. マテリアルバッファ（ConstantBuffer）
    CreateMaterialResource();

    // 4. 座標変換行列バッファ（ConstantBuffer）
    CreateTransformationMatrixResource();

    // 5. 初期値セット
    materialData_->color = Vector4(1, 1, 1, 1);
    materialData_->enableLighting = false;
    materialData_->uvTransform = MakeIdentity4x4();

    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WVP = MakeIdentity4x4();
}
void SpriteInstance::Update(const Transform& transform)
{
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform);

    Matrix4x4 viewMatrix = MakeIdentity4x4();

    Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
        0.0f, 0.0f,
        WindowsApplication::kClientWidth,
        WindowsApplication::kClientHeight,
        0.0f, 100.0f
    );

    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformationMatrixData_->World = worldMatrix;
    transformationMatrixData_->WVP = wvpMatrix;
}

void SpriteInstance::Draw()
{
    auto* cmd = spriteManager_->GetDxCore()->GetCommandList();

    // ===============================
    // 1. Vertex / Index Buffer を設定
    // ===============================
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->IASetIndexBuffer(&indexBufferView_);

    // ===============================
    // 2. マテリアル ConstantBuffer を設定（PixelShader b0）
    // ===============================
    cmd->SetGraphicsRootConstantBufferView(
        0,
        materialResource_->GetGPUVirtualAddress()
    );

    // ===============================
    // 3. Transform ConstantBuffer を設定（VertexShader b0）
    // ===============================
    cmd->SetGraphicsRootConstantBufferView(
        1,
        transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // ===============================
    // 4. SRV（Texture）の DescriptorTable
    // ===============================
    cmd->SetGraphicsRootDescriptorTable(
        2,
        textureGpuHandle_
    );

    // ===============================
    // 5. 描画！（DrawCall）
    // ===============================
    cmd->DrawIndexedInstanced(
        6,   // index count (四角形 → 三角形2つ → 6 index)
        1,   // instance count
        0,   // start index
        0,   // base vertex
        0    // start instance
    );
}
