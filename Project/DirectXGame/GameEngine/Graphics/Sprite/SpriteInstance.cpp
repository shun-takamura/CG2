#include "SpriteInstance.h"
#include "MathUtility.h"

void SpriteInstance::Initialize(SpriteManager* spriteManager, const std::string& filePath)
{
    spriteManager_ = spriteManager;
    textureFilePath_ = filePath;

    // テクスチャを読み込む（重複読み込みはTextureManager側で回避）
    TextureManager::GetInstance()->LoadTexture(filePath);
    
    // その番号からGPUハンドルを取得して保持
    textureGpuHandle_ = TextureManager::GetInstance()->GetSrvHandleGPU(filePath);

    CreateVertexBuffer();
    CreateMaterialBuffer();
    CreateTransformationMatrixBuffer();
}

void SpriteInstance::Update()
{
    float left = 0.0f - anchorPoint_.x;
    float right = 1.0f - anchorPoint_.x;
    float top= 0.0f - anchorPoint_.y;
    float bottom = 1.0f - anchorPoint_.y;

    if (isFlipX_) {
        left = -left;
        right = -right;
    }

    if (isFlipY_) {
        top = -top;
        bottom = -bottom;
    }

    const DirectX::TexMetadata& metadata =
        TextureManager::GetInstance()->GetMetaData(textureFilePath_);
    float tex_left = textureLeftTop_.x / metadata.width;
    float tex_right = (textureLeftTop_.x+ textureSize_.x) / metadata.width;
    float tex_top = textureLeftTop_.y / metadata.height;
    float tex_bottom = (textureLeftTop_.y + textureSize_.y) / metadata.height;

    // indexに格納するから同一頂点のデータをわざわざ用意する必要はない
    // 左下
    vertexData_[0].position = { left,bottom,0.0f,1.0f };
    vertexData_[0].texcoord = { tex_left,tex_bottom };
    vertexData_[0].normal = { 0.0f,0.0f,-1.0f };

    // 左上
    vertexData_[1].position = { left,top,0.0f,1.0f };
    vertexData_[1].texcoord = { tex_left,tex_top };
    vertexData_[1].normal = { 0.0f,0.0f,-1.0f };

    // 右下
    vertexData_[2].position = { right,bottom,0.0f,1.0f };
    vertexData_[2].texcoord = { tex_right,tex_bottom };
    vertexData_[2].normal = { 0.0f,0.0f,-1.0f };

    // 右上
    vertexData_[3].position = { right,top,0.0f,1.0f };
    vertexData_[3].texcoord = { tex_right,tex_top };
    vertexData_[3].normal = { 0.0f,0.0f,-1.0f };

    // インデックスリソースにデータを書き込む
    indexData_[0] = 0;
    indexData_[1] = 1;
    indexData_[2] = 2;
    indexData_[3] = 1;
    indexData_[4] = 3;
    indexData_[5] = 2;

    transform.scale = { size_.x,size_.y,1.0f };
    transform.rotate = { 0.0f,0.0f,rotation_ };
    transform.translate = { position_.x,position_.y,0.0f };

    // 行列
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform);
    Matrix4x4 viewMatrix = MakeIdentity4x4();
    Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
        0.0f, 0.0f,
        WindowsApplication::kClientWidth,
        WindowsApplication::kClientHeight,
        0.0f, 100.0f
    );
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    // GPUに転送
    transformationMatrixData_->World = worldMatrix;
    transformationMatrixData_->WVP = wvpMatrix;
}

void SpriteInstance::Draw()
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = spriteManager_->GetDxCore()->GetCommandList();

    // ===============================
    // 1. Vertex / Index Buffer を設定
    // ===============================
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // ==================================================
    // 2. マテリアル ConstantBuffer を設定（PixelShader b0）
    // ==================================================
    commandList->SetGraphicsRootConstantBufferView(
        0,
        materialResource_->GetGPUVirtualAddress()
    );

    // ====================================================
    // 3. Transform ConstantBuffer を設定（VertexShader b0）
    // ====================================================
    commandList->SetGraphicsRootConstantBufferView(
        1,
        transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // ===================================
    // 4. SRV（Texture）の DescriptorTable
    // ===================================
    commandList->SetGraphicsRootDescriptorTable(
        2,
        TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    // ===============================
    // 5. 描画（DrawCall）
    // ===============================
    commandList->DrawIndexedInstanced(
        6,   // index count (四角形 → 三角形2つ → 6 index)
        1,   // instance count
        0,   // start index
        0,   // base vertex
        0    // start instance
    );
}

SpriteInstance::~SpriteInstance()
{
    if (vertexResource_ && vertexData_) {
        vertexResource_->Unmap(0, nullptr);
        vertexData_ = nullptr;
    }

    if (indexResource_ && indexData_) {
        indexResource_->Unmap(0, nullptr);
        indexData_ = nullptr;
    }

    if (materialResource_ && materialData_) {
        materialResource_->Unmap(0, nullptr);
        materialData_ = nullptr;
    }

    if (transformationMatrixResource_ && transformationMatrixData_) {
        transformationMatrixResource_->Unmap(0, nullptr);
        transformationMatrixData_ = nullptr;
    }
}

void SpriteInstance::AdjustTextureSize()
{
    // テクスチャのメタデータを取得
    const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetaData(textureFilePath_);

    textureSize_.x = static_cast<float>(metadata.width);
    textureSize_.y = static_cast<float>(metadata.height);

    // 画像サイズをテクスチャサイズに合わせる
    size_ = textureSize_;
}

void SpriteInstance::CreateVertexBuffer()
{

    // 4頂点のバッファを作るので *4
    vertexResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(VertexData) * 4);

    // 三角形2枚分のバッファを作るので *6
    indexResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(uint32_t) * 6);

    // GPU書き込み用アドレスの取得
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

    // 座標セット（左上・右上・左下・右下）
    vertexData_[0].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexData_[1].position = { static_cast<float>(size_.x), 0.0f, 0.0f, 1.0f };
    vertexData_[2].position = { 0.0f, static_cast<float>(size_.y), 0.0f, 1.0f };
    vertexData_[3].position = { static_cast<float>(size_.x), static_cast<float>(size_.y), 0.0f, 1.0f };

    // UV
    vertexData_[0].texcoord = { 0.0f, 0.0f };
    vertexData_[1].texcoord = { 1.0f, 0.0f };
    vertexData_[2].texcoord = { 0.0f, 1.0f };
    vertexData_[3].texcoord = { 1.0f, 1.0f };

    // 法線方向
    vertexData_[0].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[1].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[2].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[3].normal = { 0.0f,0.0f,-1.0f };

    // バッファビュー設定
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();// リソースの先頭アドレスから使用
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;// 使用するリソースのサイズは頂点4個のサイズ
    vertexBufferView_.StrideInBytes = sizeof(VertexData);// 1頂点あたりのサイズ

    indexBufferView_.BufferLocation =indexResource_->GetGPUVirtualAddress();// リソースの先頭アドレスから使う
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;// 使用するリソースのサイズはインデックスの6つ分のサイズ
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT; // インデックスはuint32_tとする

}

void SpriteInstance::CreateMaterialBuffer()
{
    materialResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = { 1,1,1,1 };
    materialData_->enableLighting = false;
    materialData_->uvTransform = MakeIdentity4x4();
}

void SpriteInstance::CreateTransformationMatrixBuffer()
{
    transformationMatrixResource_ = spriteManager_->GetDxCore()->CreateBufferResource(sizeof(TransformationMatrix));
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WVP = MakeIdentity4x4();
}