#pragma once
#include "DirectXCore.h"
#include "SRVManager.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <cstdint>

class Camera;

// GPU Particle 管理クラス
// - 1024個のParticleをDEFAULT heapに保持
// - 初期化はComputeShader (InitializeParticle.CS.hlsl)
// - Emitter発生はComputeShader (EmitParticle.CS.hlsl)
// - 描画はStructuredBufferをVSで参照してインスタンシング描画
class GPUParticleManager
{
public:
    static const uint32_t kMaxParticles = 1024;

    void Initialize(DirectXCore* dxCore, SRVManager* srvManager, const std::string& textureFilePath);
    void Finalize();

    // 描画前の毎フレーム更新（PerView/PerFrame/Emitterの書き込み）
    void Update(const Camera* camera);

    // 描画。最初の呼び出し時に初期化CSのDispatchも行う
    void Draw();

    // Emitter位置のセッター
    void SetEmitterTranslate(const Vector3& translate);

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

    struct EmitterSphere
    {
        Vector3 translate;
        float radius;
        uint32_t count;
        float frequency;
        float frequencyTime;
        uint32_t emit;
    };

    struct PerFrame
    {
        float time;
        float deltaTime;
        float pad[2];
    };

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // パーティクル本体（DEFAULT heap）
    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    uint32_t particleUavIndex_ = 0;
    uint32_t particleSrvIndex_ = 0;
    bool initializedOnGPU_ = false;

    // FreeList管理（DEFAULT heap, UAVのみ）
    // gFreeListIndex: 次に取り出す位置を指すインデックス（要素1）
    // gFreeList:      空きParticleIndexの一覧（要素kMaxParticles）
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    uint32_t freeListIndexUavIndex_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    uint32_t freeListUavIndex_ = 0;

    // 初期化CS
    Microsoft::WRL::ComPtr<ID3D12RootSignature> initRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initPSO_;

    // Emit CS
    Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPSO_;

    // Update CS
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;

    // Emitter CB (UPLOAD)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource_;
    EmitterSphere* emitterData_ = nullptr;

    // PerFrame CB (UPLOAD)
    Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_;
    PerFrame* perFrameData_ = nullptr;

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

    // CPUで管理する経過時間（PerFrame.timeに送る）
    float elapsedTime_ = 0.0f;

    void CreateParticleResource();
    void CreateFreeListResources();
    void CreateInitializePipeline();
    void CreateEmitPipeline();
    void CreateUpdatePipeline();
    void CreateDrawPipeline();
    void CreateVertexData();
    void CreatePerView();
    void CreatePerFrame();
    void CreateEmitter();
    void CreateMaterial();

    void DispatchInitializeCS();
    void DispatchEmitCS();
    void DispatchUpdateCS();

    // ParticleResourceのstateを切り替えるヘルパ
    void TransitionParticle(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    // ParticleResource用のUAV Barrier（同じUAV間での依存関係を伝えるため）
    void UavBarrierParticle();
};
