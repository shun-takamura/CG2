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
#include <random>

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

// AABB
struct AABB {
    Vector3 min; //!< 最小点
    Vector3 max; //!< 最大点
};

// 加速度フィールド
struct AccelerationField {
    Vector3 acceleration; //!< 加速度
    AABB area;            //!< 範囲
};


// GPU送信用の行列データ
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

// エミッター設定
struct EmitterSettings {
    float emitRate = 10.0f;         // 1秒間に発生させる数
    float velocityScale = 1.0f;     // 速度の拡散スケール
    bool useRandomColor = false;    // ランダムカラーを使うか
    Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 基本色
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

    /// <summary>
    /// 指定した名前のパーティクルグループを削除する
    /// </summary>
    void RemoveParticleGroup(const std::string& name);

    // パーティクル発生
    void Emit(const std::string& name, const Vector3& position, uint32_t count);

    // 更新
    void Update();

    // 描画
    void Draw();
   
    // ImGui表示
    void OnImGui();

    // カメラのセット
    void SetCamera(Camera* camera) { camera_ = camera; }

    // ブレンドモードのセット
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    // 加速度フィールドのセット
    void SetAccelerationField(const AccelerationField& field) { accelerationField_ = field; }

    // 加速度フィールドの有効/無効切り替え
    void SetAccelerationFieldEnabled(bool enabled) { isAccelerationFieldEnabled_ = enabled; }

    // エミッター設定のセット/ゲット
    void SetEmitterSettings(const EmitterSettings& settings) { emitterSettings_ = settings; }

    // ゲッター
    DirectXCore* GetDxCore() const { return dxCore_; }
    SRVManager* GetSRVManager() const { return srvManager_; }
    Camera* GetCamera() const { return camera_; }
    BlendMode GetBlendMode() const { return blendMode_; }
    const AccelerationField& GetAccelerationField() const { return accelerationField_; }
    bool IsAccelerationFieldEnabled() const { return isAccelerationFieldEnabled_; }
    EmitterSettings& GetEmitterSettings() { return emitterSettings_; }

    // セッター
    void SetVelocityScale(float scale) { emitterSettings_.velocityScale = scale; }
    void SetParticleScale(float scale) { particleScale_ = scale; }
    void SetVelocityDirection(const Vector3& dir) {
        customDirection_ = dir;
        useCustomDirection_ = true;
    }
    void ResetVelocityDirection() { useCustomDirection_ = false; }

private:
    // シングルトン用
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    float particleScale_ = 1.0f;
    bool useCustomDirection_ = false;
    Vector3 customDirection_{ 0.0f, 0.0f, 0.0f };

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    Camera* camera_ = nullptr;

    // エミッター設定
    EmitterSettings emitterSettings_;

    // エミット用の蓄積時間
    float emitAccumulator_ = 0.0f;

    // 乱数生成器（メンバとして保持）
    std::random_device seedGenerator_;
    std::mt19937 randomEngine_{ seedGenerator_() };

    // AABBと点の当たり判定
    bool IsCollision(const AABB& aabb, const Vector3& point);

    // 加速度フィールド
    AccelerationField accelerationField_;
    bool isAccelerationFieldEnabled_ = false;

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
    static const uint32_t kMaxInstanceCount = 1000;

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState(BlendMode mode);
    void CreateVertexData();
    void CreateMaterialResource();
};