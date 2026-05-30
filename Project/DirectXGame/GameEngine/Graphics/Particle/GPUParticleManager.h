#pragma once
#include "DirectXCore.h"
#include "SRVManager.h"
#include"Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "BillboardMode.h"
#include "TimeGroup.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <utility>

class Camera;

// GPU Particle 管理クラス
// - グループ名でN個のパーティクルプールを管理
// - 各グループ 1024個のParticleをDEFAULT heapに保持
// - 初期化/Emit/Update は ComputeShader で行い、描画はStructuredBufferをVSで参照
class GPUParticleManager
{
public:
    static const uint32_t kMaxParticles = 1024;

    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
    void Finalize();

    // ===== グループ管理 =====
    /// <summary>
    /// 新しいパーティクルグループを生成。同名が既にあれば何もしない。
    /// </summary>
    void CreateGroup(const std::string& name, const std::string& texturePath);
    void RemoveGroup(const std::string& name);
    bool HasGroup(const std::string& name) const;

    // ===== 発射API =====
    /// <summary>
    /// 1回だけバースト発射（次フレームの Emit CS で N個を一括生成）
    /// </summary>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius = 0.5f);

    /// <summary>
    /// 色指定付きのバースト発射。colorMode=0でRandom（startColor/endColor無視）、=1でstartColor→endColor補間
    /// </summary>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor);

    /// <summary>
    /// 色 + サイズ範囲指定のバースト。uniformScale=true で幅=高さ（Xレンジを共用）。
    /// </summary>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                   const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale);

    /// <summary>
    /// 粒子寿命まで指定するバージョン。Effect の totalDuration でクランプしたいとき等に使う。
    /// </summary>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                   const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale,
                   float particleLifeTime);

    /// <summary>
    /// 連続発射のON/OFFと頻度設定
    /// </summary>
    void SetContinuousEmit(const std::string& name, bool enabled, float frequency = 0.5f, uint32_t countPerEmit = 10, float radius = 1.0f);
    void SetEmitterTranslate(const std::string& name, const Vector3& translate);

    /// <summary>
    /// 初速モードを設定。mode: 0=全方向ランダム(従来) / 1=baseVelocity固定 / 2=放射(中心から外、baseVelocity.x=速さ)。
    /// </summary>
    void SetEmitterVelocity(const std::string& name, const Vector3& baseVelocity, float jitter, int mode = 1);

    /// <summary>
    /// 多色グラデーションを設定。locations(0..1) と colors の組を最大 kMaxGradientKeys 個。
    /// 2個未満なら無効化（粒子の start/end 2色補間に戻る）。CPU 側で location 昇順にソートして渡す。
    /// </summary>
    void SetEmitterGradient(const std::string& name, const std::vector<std::pair<float, Vector4>>& keys);

    // ===== ビルボード / TimeGroup =====
    void SetGroupBillboardMode(const std::string& name, BillboardMode mode);
    void SetGroupTimeGroup(const std::string& name, TimeGroup group);

    // ===== 毎フレーム =====
    void Update(const Camera* camera, float deltaTime);
    void Draw();

    // プレビュー用 PerView を更新（メインの Update とは独立）
    void UpdatePreviewView(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);

    // プレビュー用 PerView を使って描画（Emit/Update CS は走らない、SRV化された粒子データを描画するだけ）
    void DrawPreview();

    // ImGui デバッグUI
    void OnImGui();

private:
    // C++ <-> HLSL でメンバ並びを一致させる
    struct ParticleCS
    {
        Vector3 translate;
        Vector3 scale;
        float lifeTime;
        Vector3 velocity;
        float currentTime;
        Vector4 color;       // 現在色（Update CS が毎フレーム計算）
        Vector4 startColor;  // 補間始点
        Vector4 endColor;    // 補間終点
    };

    struct PerView
    {
        Matrix4x4 viewProjection;
        Matrix4x4 billboardMatrix;     // Full ビルボード用
        Vector3   cameraPosition;      // YAxis ビルボード用
        uint32_t  billboardMode;       // 0=None, 1=Full, 2=YAxis
    };

    struct EmitterSphere
    {
        Vector3 translate;
        float radius;
        uint32_t count;
        float frequency;
        float frequencyTime;
        uint32_t emit;
        uint32_t colorMode;     // 0=Random, 1=Fixed
        Vector3 baseVelocity;   // velocityMode!=0 のときの初速方向（16-byte align も兼ねる）
        Vector4 startColor;
        Vector4 endColor;
        Vector2 scaleMin;       // 80..88
        Vector2 scaleMax;       // 88..96
        uint32_t uniformScale;  // 96..100
        float particleLifeTime; // 100..104（emit時に各粒子に設定される寿命）
        float velocityMode;     // 0=ランダム(従来), 1=baseVelocity+ジッタ
        float velocityJitter;   // velocityMode!=0 のときの速度ゆらぎ量
    };

    static const uint32_t kMaxGradientKeys = 8;
    // 多色グラデーション（Update CS の b1）。keyCount>=2 で有効、それ未満は粒子の start/end 2色補間。
    struct ParticleGradient
    {
        uint32_t keyCount = 0;
        float    pad[3] = { 0.0f, 0.0f, 0.0f };
        Vector4  keyColor[kMaxGradientKeys] = {};
        Vector4  keyLoc[kMaxGradientKeys]   = {}; // .x に位置(0..1)。昇順
    };

    struct PerFrame
    {
        float time;
        float deltaTime;
        float pad[2];
    };

    // 1グループ分のリソース束
    struct GPUParticleGroup
    {
        // パーティクル本体（DEFAULT heap）
        Microsoft::WRL::ComPtr<ID3D12Resource> particleResource;
        uint32_t particleUavIndex = 0;
        uint32_t particleSrvIndex = 0;
        bool initializedOnGPU = false;

        // FreeList
        Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource;
        uint32_t freeListIndexUavIndex = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource;
        uint32_t freeListUavIndex = 0;

        // Emitter CB
        Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource;
        EmitterSphere* emitterData = nullptr;

        // Gradient CB（Update CS b1。多色グラデーション）
        Microsoft::WRL::ComPtr<ID3D12Resource> gradientResource;
        ParticleGradient* gradientData = nullptr;

        // PerFrame CB（TimeGroup によって dt が異なるため per-group）
        Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource;
        PerFrame* perFrameData = nullptr;

        // PerView CB（メイン用）
        Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource;
        PerView* perViewData = nullptr;

        // PerView CB（プレビュー用、同じ粒子をプレビュー RT に別カメラで描画する）
        Microsoft::WRL::ComPtr<ID3D12Resource> perViewPreviewResource;
        PerView* perViewPreviewData = nullptr;

        // テクスチャ
        std::string textureFilePath;
        uint32_t textureSrvIndex = 0;

        // 状態
        BillboardMode billboardMode = BillboardMode::Full;
        TimeGroup     timeGroup     = TimeGroup::World;
        bool          continuousEnabled = false;
        float         elapsedTime = 0.0f;

        // バースト要求（Update で CB.emit に転写）。
        // CPU 側で持つ理由：CB は persistent-mapped で、GPU が Emit CS を実行する前に CPU が書き換えるとレースになる。
        bool          pendingBurst = false;
    };

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // 共通パイプライン
    Microsoft::WRL::ComPtr<ID3D12RootSignature> initRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;

    // 共通リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // 描画時に必要な共通行列（毎フレーム算出）
    Matrix4x4 viewProjectionMatrix_ = {};
    Matrix4x4 fullBillboardMatrix_ = {};
    Vector3   cameraPosition_ = { 0.0f, 0.0f, 0.0f };

    // グループ群
    std::unordered_map<std::string, GPUParticleGroup> groups_;

    // 内部ヘルパ
    void CreateInitializePipeline();
    void CreateEmitPipeline();
    void CreateUpdatePipeline();
    void CreateDrawPipeline();
    void CreateVertexData();
    void CreateMaterial();

    void CreateGroupResources(GPUParticleGroup& g, const std::string& texturePath);
    void ReleaseGroupResources(GPUParticleGroup& g);

    void DispatchInitializeCS(GPUParticleGroup& g);
    void DispatchEmitCS(GPUParticleGroup& g);
    void DispatchUpdateCS(GPUParticleGroup& g);

    void TransitionParticle(GPUParticleGroup& g, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
};
