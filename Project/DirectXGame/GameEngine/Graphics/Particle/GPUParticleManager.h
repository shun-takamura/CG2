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
#include <array>

class Camera;

// GPU Particle 管理クラス
// - グループ名でN個のパーティクルプールを管理
// - 各グループ 1024個のParticleをDEFAULT heapに保持
// - 初期化/Emit/Update は ComputeShader で行い、描画はStructuredBufferをVSで参照
class GPUParticleManager
{
public:
    static const uint32_t kMaxParticles = 1024;

    // ブレンドモード（EffectDef の int 値と互換。None=0, Normal=1, Add=2, Subtract=3, Multiply=4, Screen=5）
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultiply,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    // プレビュー専用グループ名のプレフィックス。EffectInstance がプレビュー時に
    // グループ名へ付加し、シーン用とは物理バッファを分離する（隔離の要）。
    static constexpr const char* kPreviewPrefix = "$preview$";
    // 名前がプレビュー専用グループ（上記プレフィックス付き）かどうか。
    static bool IsPreviewName(const std::string& name);

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
                   float particleLifeTime,
                   float lifeScaleStart = 1.0f, float lifeScaleEnd = 1.0f,
                   const Vector3& rotRandomRange = { 0.0f, 0.0f, 0.0f },
                   const Vector3& rotateSpeed = { 0.0f, 0.0f, 0.0f });

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
    /// 発生形状を設定。mode: 0=Sphere(従来) / 1=Ring（normal まわりの円周 + thickness の散らばり）。
    /// </summary>
    void SetEmitterShape(const std::string& name, int mode, const Vector3& ringNormal, float ringThickness);

    /// <summary>
    /// 周回（orbit）を設定。enabled なら粒子を center まわりに axis で angularSpeed[rad/s] 回す（外に出さない）。
    /// center は毎フレ更新（プレイヤー追従など）して良い。
    /// </summary>
    void SetGroupOrbit(const std::string& name, bool enabled, const Vector3& center,
                       const Vector3& spinAxis, float spinSpeed,
                       const Vector3& tumbleAxis, float tumbleSpeed);

    /// <summary>
    /// 多色グラデーションを設定。locations(0..1) と colors の組を最大 kMaxGradientKeys 個。
    /// 2個未満なら無効化（粒子の start/end 2色補間に戻る）。CPU 側で location 昇順にソートして渡す。
    /// </summary>
    void SetEmitterGradient(const std::string& name, const std::vector<std::pair<float, Vector4>>& keys);

    /// <summary>
    /// グループの「粒子ごとの寿命ディゾルブ」を設定。各粒子が自分の寿命比率(0..1)に応じて
    /// In(出現:[0,inEnd]) / Out(消滅:[outStart,1]) でマスク discard される。maskPath 空 or enable=false で無効。
    /// </summary>
    void SetGroupDissolve(const std::string& name, bool enable, const std::string& maskPath,
                          bool inEnable, float inEnd, bool outEnable, float outStart,
                          bool edgeEnable, const Vector4& edgeColor, float edgeWidth);

    /// <summary>
    /// 既存グループのテクスチャを差し替える（パスが変われば SRV を貼り直す）。
    /// グループ生成時のテクスチャを後から変更したいとき（エディタの D&D 等）に使う。
    /// </summary>
    void SetGroupTexture(const std::string& name, const std::string& texturePath);

    // ===== ブレンド / ビルボード / TimeGroup =====
    /// <summary>
    /// グループの描画ブレンドモードを設定（None=0..Screen=5）。加算では黒系粒子が映らないため Normal 等を選ぶ。
    /// </summary>
    void SetGroupBlendMode(const std::string& name, int mode);
    void SetGroupBillboardMode(const std::string& name, BillboardMode mode);
    void SetGroupTimeGroup(const std::string& name, TimeGroup group);

    // ===== 毎フレーム =====
    // シーン用グループのみを更新する（プレビュー用グループはスキップ）。
    void Update(const Camera* camera, float deltaTime);
    // シーン用グループのみをシミュレート＋描画する（プレビュー用は DrawPreview 側）。
    void Draw();

    // ===== プレビュー（エディタ専用・シーンから独立） =====
    // プレビュー用グループの emit/PerFrame をエディタの unscaled delta で進める。
    // 併せて前フレームに遅延予約したプレビューグループを安全に解放する。
    void UpdatePreviewSim(float deltaTime);

    // keepNames に無いプレビュー用グループを遅延解放キューへ移す（次フレーム頭で実解放）。
    // 「今プレビュー中のエフェクトが使うグループ」だけを残し、working set を有界化する。
    void RecyclePreviewGroups(const std::vector<std::string>& keepNames);

    // プレビュー用 PerView（カメラ）を更新（メインの Update とは独立）
    void UpdatePreviewView(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);

    // プレビュー用グループを独立シミュレート＋描画（プレビュー用 PerView を使う）
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
        Vector2 lifeScale;   // 寿命に沿ったサイズ倍率（x=開始, y=終了）
        Vector4 orientation; // 3D姿勢クオータニオン（ビルボードNone時に使用）
        Vector3 angularVel;  // 各軸の角速度（rad/s）
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
        float shapeMode;        // 0=Sphere(従来), 1=Ring
        Vector3 ringNormal;     // Ring の法線
        float ringThickness;    // Ring の太さ
        float lifeScaleStart;   // 寿命開始時のサイズ倍率
        float lifeScaleEnd;     // 寿命終了時のサイズ倍率
        float pad2;             // 16B境界合わせ
        Vector4 rotRandomRange; // .xyz=出現時ランダム初期姿勢の各軸最大角（rad）
        Vector4 rotateSpeed;    // .xyz=各軸の角速度（rad/s）
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

    // 周回（orbit）。Update CS の b2。spin=帯上を流れる(法線軸) / tumble=帯自体の回転(別軸)。
    struct ParticleOrbit
    {
        float   enabled = 0.0f;
        float   spinSpeed = 0.0f;
        float   tumbleSpeed = 0.0f;
        float   pad0 = 0.0f;
        Vector3 center = { 0.0f, 0.0f, 0.0f };
        float   pad1 = 0.0f;
        Vector3 spinAxis = { 0.0f, 0.0f, 1.0f };
        float   pad2 = 0.0f;
        Vector3 tumbleAxis = { 0.0f, 1.0f, 0.0f };
        float   pad3 = 0.0f;
    };

    struct PerFrame
    {
        float time;
        float deltaTime;
        float pad[2];
    };

    // ディゾルブ（描画 PS の b2、粒子ごとの寿命比率ベース）。GPUParticle.PS.hlsl と一致させること。
    struct DissolveParticle
    {
        int   enable = 0;       // 0=無効 / 1=有効
        int   inEnable = 0;     // 出現 0/1
        int   outEnable = 0;    // 消滅 0/1
        int   edgeEnable = 0;   // アウトライン 0/1
        float inEnd = 0.3f;     // 寿命比率: ここまでに出現完了
        float outStart = 0.7f;  // 寿命比率: ここから消え始める
        float edgeWidth = 0.05f;
        float pad0 = 0.0f;
        Vector4 edgeColor = { 1.0f, 0.4f, 0.1f, 1.0f };
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

        // Orbit CB（Update CS b2。周回運動）
        Microsoft::WRL::ComPtr<ID3D12Resource> orbitResource;
        ParticleOrbit* orbitData = nullptr;

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

        // ディゾルブ（描画 PS b2）＋マスク(t1)
        Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource;
        DissolveParticle* dissolveData = nullptr;
        std::string dissolveMaskPath;
        uint32_t dissolveMaskSrvIndex = 0;
        bool     hasDissolveMask = false;

        // 状態
        BlendMode     blendMode    = kBlendModeAdd; // 描画ブレンド（既定 Add＝後方互換）
        BillboardMode billboardMode = BillboardMode::Full;
        TimeGroup     timeGroup     = TimeGroup::World;
        bool          continuousEnabled = false;
        float         elapsedTime = 0.0f;

        // バースト要求（Update で CB.emit に転写）。
        // CPU 側で持つ理由：CB は persistent-mapped で、GPU が Emit CS を実行する前に CPU が書き換えるとレースになる。
        bool          pendingBurst = false;

        // プレビュー専用グループ（名前が kPreviewPrefix 付き）。シーン用とは
        // 更新経路（unscaled delta）も描画経路（プレビュー PerView）も分ける。
        bool          isPreview = false;
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
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> drawPSOs_;

    // 共通リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // ディゾルブマスク未設定時に t1 へバインドする既定テクスチャ（white1x1）
    uint32_t whiteSrvIndex_ = 0;

    // 描画時に必要な共通行列（毎フレーム算出）
    Matrix4x4 viewProjectionMatrix_ = {};
    Matrix4x4 fullBillboardMatrix_ = {};
    Vector3   cameraPosition_ = { 0.0f, 0.0f, 0.0f };

    // グループ群
    std::unordered_map<std::string, GPUParticleGroup> groups_;

    // 遅延解放待ちのプレビューグループ（フレーム途中の GPU 使用中バッファ解放を避けるため
    // RecyclePreviewGroups で一旦ここへ退避し、次フレーム頭の UpdatePreviewSim で実解放する）。
    std::vector<GPUParticleGroup> previewGroupPendingDelete_;

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

    // 1グループ分の emit フラグ確定 + PerFrame(dt) 書き込み（Update / UpdatePreviewSim 共用）。
    void UpdateGroupSim(GPUParticleGroup& g, float dt);
    // 1グループを Init/Emit/Update CS でシミュレートし、指定 PerView CB で描画（Draw / DrawPreview 共用）。
    void SimulateAndDrawGroup(GPUParticleGroup& g, ID3D12Resource* perViewCB);

    void TransitionParticle(GPUParticleGroup& g, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
};
