#include "Skybox.h"
#include "SkyboxManager.h"
#include "PepperMacros.h"

void Skybox::Initialize(SkyboxManager* skyboxManager, DirectXCore* dxCore, const std::string& cubemapFilePath)
{
    skyboxManager_ = skyboxManager;
    cubemapFilePath_ = cubemapFilePath;
    nextCubemapFilePath_ = cubemapFilePath;  // 初期は両スロット同一

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

void Skybox::SetColor(const Vector4& color)
{
    isColorFading_ = false;          // 進行中のフェードを打ち切って即適用
    currentColor_ = color;
    if (materialData_) {
        materialData_->color = color;
    }
}

void Skybox::SetCubemap(const std::string& cubemapFilePath)
{
    // ブレンドせず即差し替え。両スロットを同じテクスチャにして blendT を無効化。
    TextureManager::GetInstance()->LoadTexture(cubemapFilePath);
    cubemapFilePath_ = cubemapFilePath;
    nextCubemapFilePath_ = cubemapFilePath;
    isBlending_ = false;
    blendTime_ = 0.0f;
    if (materialData_) {
        materialData_->blendT = 0.0f;
    }
}

void Skybox::BlendTo(const std::string& cubemapFilePath, float durationSec)
{
    // 既に同じ空なら何もしない
    if (cubemapFilePath == cubemapFilePath_ && !isBlending_) {
        return;
    }

    // 遷移先を次スロットへロードして配置
    TextureManager::GetInstance()->LoadTexture(cubemapFilePath);
    nextCubemapFilePath_ = cubemapFilePath;

    // 即時遷移指定なら確定して終了
    if (durationSec <= 0.0f) {
        SetCubemap(cubemapFilePath);
        return;
    }

    isBlending_ = true;
    blendTime_ = 0.0f;
    blendDuration_ = durationSec;
    if (materialData_) {
        materialData_->blendT = 0.0f;
    }
}

void Skybox::FadeColor(const Vector4& targetColor, float durationSec)
{
    if (durationSec <= 0.0f) {
        SetColor(targetColor);
        return;
    }
    isColorFading_ = true;
    colorFadeTime_ = 0.0f;
    colorFadeDuration_ = durationSec;
    colorFadeStart_ = currentColor_;
    colorFadeTarget_ = targetColor;
}

void Skybox::Update(float deltaTime)
{
    // --- クロスフェードの進行 ---
    if (isBlending_) {
        blendTime_ += deltaTime;
        float t = (blendDuration_ > 0.0f) ? (blendTime_ / blendDuration_) : 1.0f;
        if (t >= 1.0f) {
            // 遷移完了：次スロットを現在スロットへ確定し、blendTをリセット
            t = 1.0f;
            cubemapFilePath_ = nextCubemapFilePath_;
            isBlending_ = false;
            blendTime_ = 0.0f;
            if (materialData_) {
                materialData_->blendT = 0.0f;
            }
        } else if (materialData_) {
            materialData_->blendT = t;
        }
    }

    // --- 着色フェードの進行 ---
    if (isColorFading_) {
        colorFadeTime_ += deltaTime;
        float t = (colorFadeDuration_ > 0.0f) ? (colorFadeTime_ / colorFadeDuration_) : 1.0f;
        if (t >= 1.0f) {
            t = 1.0f;
            isColorFading_ = false;
        }
        currentColor_.x = colorFadeStart_.x + (colorFadeTarget_.x - colorFadeStart_.x) * t;
        currentColor_.y = colorFadeStart_.y + (colorFadeTarget_.y - colorFadeStart_.y) * t;
        currentColor_.z = colorFadeStart_.z + (colorFadeTarget_.z - colorFadeStart_.z) * t;
        currentColor_.w = colorFadeStart_.w + (colorFadeTarget_.w - colorFadeStart_.w) * t;
        if (materialData_) {
            materialData_->color = currentColor_;
        }
    }

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

    // SRV t0：現在スロットのCubemap
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(cubemapFilePath_)
    );

    // SRV t1：次スロットのCubemap（クロスフェード先。非ブレンド時は現在と同一）
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        3, TextureManager::GetInstance()->GetSrvHandleGPU(nextCubemapFilePath_)
    );

    // インデックスを使用して描画
    PEPPER_COUNT("DrawCall");
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
    UINT materialSize = (sizeof(SkyboxMaterial) + 255) & ~255;

    // リソースを作る
    materialResource_ = dxCore->CreateBufferResource(materialSize);

    // 書き込むためのアドレスを取得
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // 初期値を設定
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };    // 白
    materialData_->blendT = 0.0f;                          // 補間なし（現在スロットのみ表示）
    materialData_->padding[0] = 0.0f;
    materialData_->padding[1] = 0.0f;
    materialData_->padding[2] = 0.0f;
}