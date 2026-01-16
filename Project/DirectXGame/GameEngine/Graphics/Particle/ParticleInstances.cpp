#include "ParticleInstances.h"
#include "ParticleManager.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "TextureManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "VertexData.h"
#include "Material.h"
#include <cassert>

void ParticleInstances::Initialize(ParticleManager* particleManager, const std::string& textureFilePath, uint32_t maxParticleCount)
{
    particleManager_ = particleManager;
    dxCore_ = particleManager->GetDxCore();
    srvManager_ = particleManager->GetSRVManager();
    camera_ = particleManager->GetCamera();
    textureFilePath_ = textureFilePath;
    maxParticleCount_ = maxParticleCount;

    // パーティクル配列を確保
    particles_.resize(maxParticleCount);
    for (auto& particle : particles_) {
        particle.isActive = false;
    }

    // 各リソース作成
    CreateVertexData();
    CreateInstancingResource();
    CreateSRV();
    CreateMaterialResource();

    // テクスチャ読み込み
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);
}

void ParticleInstances::Update()
{
    // カメラ取得
    if (!camera_) {
        camera_ = particleManager_->GetCamera();
    }

    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

    // ビルボード用の回転行列（カメラの逆回転）
    Matrix4x4 billboardMatrix = MakeIdentity4x4();
    billboardMatrix.m[0][0] = viewMatrix.m[0][0];
    billboardMatrix.m[0][1] = viewMatrix.m[1][0];
    billboardMatrix.m[0][2] = viewMatrix.m[2][0];
    billboardMatrix.m[1][0] = viewMatrix.m[0][1];
    billboardMatrix.m[1][1] = viewMatrix.m[1][1];
    billboardMatrix.m[1][2] = viewMatrix.m[2][1];
    billboardMatrix.m[2][0] = viewMatrix.m[0][2];
    billboardMatrix.m[2][1] = viewMatrix.m[1][2];
    billboardMatrix.m[2][2] = viewMatrix.m[2][2];

    uint32_t activeCount = 0;

    for (auto& particle : particles_) {
        if (!particle.isActive) continue;

        // 時間更新
        particle.currentTime += 1.0f / 60.0f;

        // 寿命チェック
        if (particle.currentTime >= particle.lifeTime) {
            particle.isActive = false;
            continue;
        }

        // 移動
        particle.transform.translate.x += particle.velocity.x;
        particle.transform.translate.y += particle.velocity.y;
        particle.transform.translate.z += particle.velocity.z;

        // 行列計算
        Matrix4x4 scaleMatrix = MakeScaleMatrix(particle.transform);
        Matrix4x4 translateMatrix = MakeTranslateMatrix(particle.transform);

        // ビルボード適用
        Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, billboardMatrix), translateMatrix);
        Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);

        // GPU用データに書き込み
        instancingData_[activeCount].WVP = wvpMatrix;
        instancingData_[activeCount].World = worldMatrix;

        activeCount++;
    }
}

void ParticleInstances::Draw()
{
    // アクティブなパーティクル数をカウント
    uint32_t activeCount = 0;
    for (const auto& particle : particles_) {
        if (particle.isActive) {
            activeCount++;
        }
    }

    if (activeCount == 0) return;

    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();

    // 頂点バッファをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // インスタンシング用SRVをセット（RootParameter[0]）
    srvManager_->SetGraphicsRootDescriptorTable(0, srvIndex_);

    // マテリアルCBVをセット（RootParameter[1]）
    commandList->SetGraphicsRootConstantBufferView(1, materialResource_->GetGPUVirtualAddress());

    // テクスチャSRVをセット（RootParameter[2]）
    srvManager_->SetGraphicsRootDescriptorTable(2, textureIndex_);

    // 描画（インスタンシング）
    commandList->DrawInstanced(6, activeCount, 0, 0);
}

void ParticleInstances::Emit(const Vector3& position, uint32_t count)
{
    uint32_t emitted = 0;

    for (auto& particle : particles_) {
        if (emitted >= count) break;
        if (particle.isActive) continue;

        // パーティクル初期化
        particle.transform.scale = { 1.0f, 1.0f, 1.0f };
        particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
        particle.transform.translate = position;

        // ランダムな速度
        particle.velocity = {
            ((rand() % 100) / 100.0f - 0.5f) * 0.1f,
            ((rand() % 100) / 100.0f) * 0.1f,
            ((rand() % 100) / 100.0f - 0.5f) * 0.1f
        };

        particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        particle.lifeTime = 2.0f;
        particle.currentTime = 0.0f;
        particle.isActive = true;

        emitted++;
    }
}

ParticleInstances::~ParticleInstances()
{
    if (instancingResource_) {
        instancingResource_->Unmap(0, nullptr);
    }
    instancingResource_.Reset();
    vertexResource_.Reset();
    materialResource_.Reset();
}

void ParticleInstances::CreateVertexData()
{
    // 板ポリ（6頂点）
    vertexResource_ = dxCore_->CreateBufferResource(sizeof(VertexData) * 6);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 左上
    vertexData[0].position = { -0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 0.0f };
    vertexData[0].normal = { 0.0f, 0.0f, 1.0f };

    // 右上
    vertexData[1].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[1].texcoord = { 1.0f, 0.0f };
    vertexData[1].normal = { 0.0f, 0.0f, 1.0f };

    // 左下
    vertexData[2].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[2].texcoord = { 0.0f, 1.0f };
    vertexData[2].normal = { 0.0f, 0.0f, 1.0f };

    // 左下
    vertexData[3].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[3].texcoord = { 0.0f, 1.0f };
    vertexData[3].normal = { 0.0f, 0.0f, 1.0f };

    // 右上
    vertexData[4].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[4].normal = { 0.0f, 0.0f, 1.0f };

    // 右下
    vertexData[5].position = { 0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[5].texcoord = { 1.0f, 1.0f };
    vertexData[5].normal = { 0.0f, 0.0f, 1.0f };

    vertexResource_->Unmap(0, nullptr);

    // VertexBufferView作成
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void ParticleInstances::CreateInstancingResource()
{
    // インスタンシング用バッファ作成
    instancingResource_ = dxCore_->CreateBufferResource(sizeof(ParticleForGPU) * maxParticleCount_);

    // Map（常時マップしておく）
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // 初期化
    for (uint32_t i = 0; i < maxParticleCount_; ++i) {
        instancingData_[i].WVP = MakeIdentity4x4();
        instancingData_[i].World = MakeIdentity4x4();
    }
}

void ParticleInstances::CreateSRV()
{
    // SRVManagerからインデックスを確保
    srvIndex_ = srvManager_->Allocate();

    // SRVManagerの関数を使ってSRV作成
    srvManager_->CreateSRVForStructuredBuffer(
        srvIndex_,
        instancingResource_.Get(),
        maxParticleCount_,
        sizeof(ParticleForGPU)
    );
}

void ParticleInstances::CreateMaterialResource()
{
    // マテリアル用バッファ作成
    materialResource_ = dxCore_->CreateBufferResource(sizeof(Material));

    Material* materialData = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = false;
    materialData->uvTransform = MakeIdentity4x4();

    materialResource_->Unmap(0, nullptr);
}