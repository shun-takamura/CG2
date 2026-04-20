#include "Skybox.h"
#include "SkyboxManager.h"

void Skybox::Initialize(SkyboxManager* skyboxManager, DirectXCore* dxCore, const std::string& cubemapFilePath)
{
    skyboxManager_ = skyboxManager;
    cubemapFilePath_ = cubemapFilePath;

    camera_ = skyboxManager_->GetDefaultCamera();

    // Cubemapテクスチャをロード
    TextureManager::GetInstance()->LoadTexture(cubemapFilePath_);

    // 頂点データ生成
    CreateVertexData();

    // バッファリソース作成
    CreateVertexBuffer(dxCore);
    CreateIndexBuffer(dxCore);
    CreateTransformationMatrixResource(dxCore);
    CreateMaterialResource(dxCore);

    // Transform初期化（Skyboxは原点・回転なし・スケール1）
    transform_ = {
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };
}

void Skybox::Update()
{
    // Skyboxの位置をカメラに合わせる（追従）
    if (camera_) {
        transform_.translate = camera_->GetTranslate();
    }

    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
    Matrix4x4 worldViewProjectionMatrix;

    if (camera_) {
        const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
        worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
    } else {
        worldViewProjectionMatrix = worldMatrix;
    }

    transformationMatrixData_->WVP = worldViewProjectionMatrix;
}

void Skybox::Draw(DirectXCore* dxCore)
{
    // 頂点バッファビューをセット
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // インデックスバッファビューをセット
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

    // 座標変換行列CBufferの場所を設定（b0 VS）
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // マテリアルCBufferの場所を設定（b0 PS）
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, materialResource_->GetGPUVirtualAddress()
    );

    // SRVのDescriptorTableの先頭を設定（Cubemapテクスチャ）
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(cubemapFilePath_)
    );

    // インデックスを使用して描画
    dxCore->GetCommandList()->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void Skybox::CreateVertexData()
{
    // 8頂点の立方体（-1〜1）
    // v0: 左下手前
    vertices_[0].position = { -1.0f, -1.0f, -1.0f, 1.0f };
    // v1: 右下手前
    vertices_[1].position = { 1.0f, -1.0f, -1.0f, 1.0f };
    // v2: 左下奥
    vertices_[2].position = { -1.0f, -1.0f,  1.0f, 1.0f };
    // v3: 右下奥
    vertices_[3].position = { 1.0f, -1.0f,  1.0f, 1.0f };
    // v4: 左上手前
    vertices_[4].position = { -1.0f,  1.0f, -1.0f, 1.0f };
    // v5: 右上手前
    vertices_[5].position = { 1.0f,  1.0f, -1.0f, 1.0f };
    // v6: 左上奥
    vertices_[6].position = { -1.0f,  1.0f,  1.0f, 1.0f };
    // v7: 右上奥
    vertices_[7].position = { 1.0f,  1.0f,  1.0f, 1.0f };

    // インデックスデータ（内側から見る向き）
    // 前面（Z=-1）
    indices_[0] = 0; indices_[1] = 4; indices_[2] = 1;
    indices_[3] = 1; indices_[4] = 4; indices_[5] = 5;
    // 後面（Z=+1）
    indices_[6] = 3; indices_[7] = 7; indices_[8] = 2;
    indices_[9] = 2; indices_[10] = 7; indices_[11] = 6;
    // 左面（X=-1）
    indices_[12] = 2; indices_[13] = 6; indices_[14] = 0;
    indices_[15] = 0; indices_[16] = 6; indices_[17] = 4;
    // 右面（X=+1）
    indices_[18] = 1; indices_[19] = 5; indices_[20] = 3;
    indices_[21] = 3; indices_[22] = 5; indices_[23] = 7;
    // 上面（Y=+1）
    indices_[24] = 4; indices_[25] = 6; indices_[26] = 5;
    indices_[27] = 5; indices_[28] = 6; indices_[29] = 7;
    // 下面（Y=-1）
    indices_[30] = 2; indices_[31] = 0; indices_[32] = 3;
    indices_[33] = 3; indices_[34] = 0; indices_[35] = 1;
}

void Skybox::CreateVertexBuffer(DirectXCore* dxCore)
{
    // 頂点バッファリソース作成
    UINT vertexBufferSize = sizeof(VertexData_Skybox) * 8;
    vertexBuffer_ = dxCore->CreateBufferResource(vertexBufferSize);

    // 頂点バッファにデータを書き込む
    VertexData_Skybox* vertexData = nullptr;
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices_, vertexBufferSize);
    vertexBuffer_->Unmap(0, nullptr);

    // 頂点バッファビューの設定
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = vertexBufferSize;
    vertexBufferView_.StrideInBytes = sizeof(VertexData_Skybox);
}

void Skybox::CreateIndexBuffer(DirectXCore* dxCore)
{
    // インデックスバッファリソース作成
    UINT indexBufferSize = sizeof(uint32_t) * 36;
    indexBuffer_ = dxCore->CreateBufferResource(indexBufferSize);

    // インデックスバッファにデータを書き込む
    uint32_t* indexData = nullptr;
    indexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::memcpy(indexData, indices_, indexBufferSize);
    indexBuffer_->Unmap(0, nullptr);

    // インデックスバッファビューの設定
    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = indexBufferSize;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Skybox::CreateTransformationMatrixResource(DirectXCore* dxCore)
{
    // サイズを256アライメントに調整
    UINT transformationMatrixSize = (sizeof(SkyboxTransformationMatrix) + 255) & ~255;

    // リソースを作る
    transformationMatrixResource_ = dxCore->CreateBufferResource(transformationMatrixSize);

    // 書き込むためのアドレスを取得
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

    // 単位行列を書き込む
    transformationMatrixData_->WVP = MakeIdentity4x4();
}

void Skybox::CreateMaterialResource(DirectXCore* dxCore)
{
    // サイズを256アライメントに調整
    UINT materialSize = (sizeof(Material) + 255) & ~255;

    // リソースを作る
    materialResource_ = dxCore->CreateBufferResource(materialSize);

    // 書き込むためのアドレスを取得
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // 初期値を設定
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };    // 白
    materialData_->enableLighting = 0;                     // ライティング無効（Skyboxでは使わない）
    materialData_->uvTransform = MakeIdentity4x4();        // 単位行列
    materialData_->shininess = 0.0f;                       // 未使用
}