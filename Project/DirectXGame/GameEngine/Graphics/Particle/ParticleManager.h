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
#include "EmitParam.h"
#include "BillboardMode.h"
#include "TimeGroup.h"
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
    BillboardMode billboardMode = BillboardMode::Full;
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

    // このグループの時間グループ（World/Player/UI 別倍率）
    TimeGroup timeGroup = TimeGroup::World;
};

/// <summary>
/// CPU で動かすパーティクルのグループを作り、更新・描画する。
/// シングルトンにする理由：パーティクルの共通パイプラインとグループの表を 1 か所で管理するため。
/// </summary>
class ParticleManager
{
public:
    // ブレンドモード
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultiply,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    // シングルトン
    /// <summary>シングルトンを返す</summary>
    /// <returns>唯一のインスタンス</returns>
    static ParticleManager* GetInstance();

    /// <summary>ルートシグネチャ・ブレンドモード別 PSO・板の頂点・共通マテリアルを作る</summary>
    /// <param name="dxCore">デバイスとコマンドリストの取得元</param>
    /// <param name="srvManager">インスタンシング用 SRV を確保する先</param>
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

    /// <summary>全グループのインスタンシングバッファと共通のパイプライン・リソースを解放する</summary>
    void Finalize();

    /// <summary>パーティクルグループを作る（テクスチャ読み込みと最大 1000 個分のインスタンシング SRV 確保）。登録済みの名前は assert</summary>
    /// <param name="name">グループ名</param>
    /// <param name="textureFilePath">粒子のテクスチャのパス</param>
    void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);

    /// <summary>
    /// 指定した名前のパーティクルグループを削除する
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    void RemoveParticleGroup(const std::string& name);

    /// <summary>パーティクル発生（エミッター設定の速度・色、寿命 2 秒。SetVelocityDirection 中はその方向へ飛ばす）</summary>
    /// <param name="name">グループ名（未登録は assert）</param>
    /// <param name="position">発生位置のワールド座標</param>
    /// <param name="count">発生数（グループの上限 1000 個を超えた分は出さない）</param>
    void Emit(const std::string& name, const Vector3& position, uint32_t count);

    /// <summary>パーティクル発生（EmitParam版。スケール・回転・速度は Base＋乱数範囲、寿命は ± 乱数）</summary>
    /// <param name="name">グループ名（未登録は assert）</param>
    /// <param name="param">発生のパラメータ</param>
    void Emit(const std::string& name, const EmitParam& param);

    /// <summary>寿命切れを外し、加速度フィールド・移動・ビルボードを反映してインスタンシングデータを書く</summary>
    /// <param name="deltaTime">呼び出し側からタイムスケール適用済みの経過時間 [秒]（さらにグループの TimeGroup でスケールする）</param>
    void Update(float deltaTime);

    /// <summary>全グループをインスタンシング描画する（ルート [0]=VS t0 インスタンス / [1]=PS b0 マテリアル / [2]=PS t0 テクスチャ）</summary>
    void Draw();
   
    /// <summary>ImGui表示（グループごとの粒子数と TimeGroup の切り替え。Debug だけ）</summary>
    void OnImGui();

    /// <summary>カメラのセット（Update のビルボードと WVP に使う）</summary>
    /// <param name="camera">シーンのカメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>ブレンドモードのセット（全グループ共通）</summary>
    /// <param name="blendMode">ブレンドモード</param>
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    /// <summary>加速度フィールドのセット</summary>
    /// <param name="field">加速度と効く範囲</param>
    void SetAccelerationField(const AccelerationField& field) { accelerationField_ = field; }

    /// <summary>加速度フィールドの有効/無効切り替え</summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetAccelerationFieldEnabled(bool enabled) { isAccelerationFieldEnabled_ = enabled; }

    /// <summary>エミッター設定のセット</summary>
    /// <param name="settings">速度の拡散・色などの設定</param>
    void SetEmitterSettings(const EmitterSettings& settings) { emitterSettings_ = settings; }

    // ゲッター
    /// <summary>DirectXCore を返す</summary>
    /// <returns>Initialize で受け取った DirectXCore</returns>
    DirectXCore* GetDxCore() const { return dxCore_; }
    /// <summary>SRVManager を返す</summary>
    /// <returns>Initialize で受け取った SRVManager</returns>
    SRVManager* GetSRVManager() const { return srvManager_; }
    /// <summary>カメラを返す</summary>
    /// <returns>設定中のカメラ（未設定なら nullptr）</returns>
    Camera* GetCamera() const { return camera_; }
    /// <summary>ブレンドモードを返す</summary>
    /// <returns>今のブレンドモード</returns>
    BlendMode GetBlendMode() const { return blendMode_; }
    /// <summary>加速度フィールドを返す</summary>
    /// <returns>今の加速度フィールド</returns>
    const AccelerationField& GetAccelerationField() const { return accelerationField_; }
    /// <summary>加速度フィールドが有効か</summary>
    /// <returns>有効なら true</returns>
    bool IsAccelerationFieldEnabled() const { return isAccelerationFieldEnabled_; }
    /// <summary>エミッター設定を返す</summary>
    /// <returns>今のエミッター設定</returns>
    const EmitterSettings& GetEmitterSettings() const { return emitterSettings_; }

    // セッター
    /// <summary>速度の拡散スケールを設定する</summary>
    /// <param name="scale">スケール</param>
    void SetVelocityScale(float scale) { emitterSettings_.velocityScale = scale; }

    /// <summary>パーティクルグループのTimeGroup設定</summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="group">時間グループ</param>
    void SetParticleGroupTimeGroup(const std::string& name, TimeGroup group);
    /// <summary>Emit（位置・数版）で出す粒子の拡大率を設定する</summary>
    /// <param name="scale">拡大率（3 軸共通）</param>
    void SetParticleScale(float scale) { particleScale_ = scale; }
    /// <summary>Emit（位置・数版）の飛ぶ方向を固定する（正規化して velocityScale を掛ける）</summary>
    /// <param name="dir">方向（長さ 0 なら速度 0）</param>
    void SetVelocityDirection(const Vector3& dir) {
        customDirection_ = dir;
        useCustomDirection_ = true;
    }
    /// <summary>飛ぶ方向の固定をやめてランダムに戻す</summary>
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

    // 乱数生成器（メンバとして保持。種は Initialize で見た目用の乱数からもらう）
    std::mt19937 randomEngine_;

    /// <summary>AABBと点の当たり判定</summary>
    /// <param name="aabb">箱</param>
    /// <param name="point">点</param>
    /// <returns>点が箱の中（境界含む）なら true</returns>
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
    /// <summary>ルートシグネチャを作る（[0]=VS t0 インスタンシング SRV / [1]=PS b0 マテリアル / [2]=PS t0 テクスチャ 、s0）</summary>
    void CreateRootSignature();
    /// <summary>指定ブレンドモードの PSO を作る（深度は読むだけ・sRGB の RT）</summary>
    /// <param name="mode">ブレンドモード</param>
    void CreateGraphicsPipelineState(BlendMode mode);
    /// <summary>板ポリ（6 頂点）の頂点バッファを作る</summary>
    void CreateVertexData();
    /// <summary>全グループ共通のマテリアル CB を作る</summary>
    void CreateMaterialResource();
};