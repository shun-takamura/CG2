#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"

// 前方宣言
class ParticleManager;
class DirectXCore;
class SRVManager;
class Camera;

// パーティクル1個分のデータ
struct Particle {
    Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
    bool isActive;
};

// GPU送信用の行列データ
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

class ParticleInstances
{
public:
    // 初期化
    void Initialize(ParticleManager* particleManager, const std::string& textureFilePath, uint32_t maxParticleCount = 100);

    // 更新
    void Update();

    // 描画
    void Draw();

    // パーティクル発生
    void Emit(const Vector3& position, uint32_t count);

    // セッター
    void SetCamera(Camera* camera) { camera_ = camera; }

    // デストラクタ
    ~ParticleInstances();

private:
    ParticleManager* particleManager_ = nullptr;
    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    Camera* camera_ = nullptr;

    // パーティクルデータ
    std::vector<Particle> particles_;
    uint32_t maxParticleCount_ = 0;

    // インスタンシング用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;
    uint32_t srvIndex_ = 0;

    // 頂点リソース（板ポリ）
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアル
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // テクスチャ
    std::string textureFilePath_;
    uint32_t textureIndex_ = 0;

private:
    // 板ポリ頂点データ作成
    void CreateVertexData();

    // インスタンシング用リソース作成
    void CreateInstancingResource();

    // SRV作成
    void CreateSRV();

    // マテリアルリソース作成
    void CreateMaterialResource();
};