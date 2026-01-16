#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <array>
#include <unordered_map>
#include <list>
#include <string>
#include "SRVManager.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"

// 前方宣言
class Camera;

// パーティクル1個分のデータ
struct Particle {
    Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
};

// GPU送信用の行列データ
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

// パーティクルグループ
struct ParticleGroup {
    // マテリアルデータ
    std::string textureFilePath;
    uint32_t textureSrvIndex;

    // パーティクルのリスト
    std::list<Particle> particles;

    // インスタンシング用データ
    uint32_t instancingSrvIndex;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
    uint32_t instanceCount;
    ParticleForGPU* instancingData;
};

class ParticleManager
{
public:
    // ブレンドモード
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultily,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    // シングルトン
    static ParticleManager* GetInstance();

    // 初期化
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

    // 終了処理
    void Finalize();

    // パーティクルグループの生成
    void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);

    // パーティクル発生
    void Emit(const std::string& name, const Vector3& position, uint32_t count);

    // 更新
    void Update();

    // 描画
    void Draw();

    // カメラのセット
    void SetCamera(Camera* camera) { camera_ = camera; }

    // ブレンドモードのセット
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    // ゲッター
    DirectXCore* GetDxCore() const { return dxCore_; }
    SRVManager* GetSRVManager() const { return srvManager_; }
    Camera* GetCamera() const { return camera_; }
    BlendMode GetBlendMode() const { return blendMode_; }

private:
    // シングルトン用
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    Camera* camera_ = nullptr;

    BlendMode blendMode_ = kBlendModeAdd;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

    // パーティクルグループコンテナ
    std::unordered_map<std::string, ParticleGroup> particleGroups_;

    // 頂点リソース（板ポリ・全グループ共通）
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアルリソース（全グループ共通）
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // 最大インスタンス数
    static const uint32_t kMaxInstanceCount = 100;

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState(BlendMode mode);
    void CreateVertexData();
    void CreateMaterialResource();
};