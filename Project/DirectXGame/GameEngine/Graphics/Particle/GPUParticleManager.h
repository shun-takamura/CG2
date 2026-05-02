#pragma once
#include "DirectXCore.h"
#include "SRVManager.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>

class Camera;

// GPU Particle 管理クラス
// - 1024個のParticleをDEFAULT heapに保持
// - 初期化はComputeShaderで行う
// - 描画はStructuredBufferをVSで参照してインスタンシング描画
class GPUParticleManager
{
public:
    static const uint32_t kMaxParticles = 1024;

    void Initialize(DirectXCore* dxCore, SRVManager* srvManager, const std::string& textureFilePath);
    void Finalize();

    // 描画前の毎フレーム更新（PerViewの書き込み）
    void Update(const Camera* camera);

    // 描画。最初の呼び出し時に初期化CSのDispatchも行う
    void Draw();

private:
    // C++ <-> HLSL でメンバ並びを一致させる
    struct ParticleCS
    {
        Vector3 translate;
        Vector3 scale;
        float lifeTime;
        Vector3 velocity;
        float currentTime;
        Vector4 color;
    };

    struct PerView
    {
        Matrix4x4 viewProjection;
        Matrix4x4 billboardMatrix;
    };

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // パーティクル本体（DEFAULT heap）
    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    uint32_t uavIndex_ = 0;
    uint32_t srvIndex_ = 0;
    bool initializedOnGPU_ = false;

    // 初期化CS
    Microsoft::WRL::ComPtr<ID3D12RootSignature> initRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initPSO_;

    // 描画
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;

    // 板ポリ6頂点
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // PerView CBV (UPLOAD)
    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
    PerView* perViewData_ = nullptr;

    // Material CBV（既存Particle.PS.hlslの要件を満たすため）
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // テクスチャ
    std::string textureFilePath_;
    uint32_t textureSrvIndex_ = 0;

    void CreateParticleResource();
    void CreateInitializePipeline();
    void CreateDrawPipeline();
    void CreateVertexData();
    void CreatePerView();
    void CreateMaterial();
    void DispatchInitializeCS();
};
