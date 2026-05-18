#pragma once
#include "EffectDef.h"
#include "EffectPrimitiveRenderer.h"
#include "Vector3.h"
#include "Matrix4x4.h"
#include <cstdint>
#include <memory>
#include <vector>

class Camera;
class GPUParticleManager;

/// <summary>
/// 再生中のエフェクト1個分のインスタンス。
/// EffectDefを参照しつつ、各コンポーネントのライフサイクルを管理する。
/// </summary>
class EffectInstance {
public:
    /// <summary>
    /// 初期化。defはインスタンスにコピーして所有する（呼び出し元のdef破棄に依存しない）。
    /// </summary>
    void Initialize(const EffectDef& def, const Vector3& worldPos, GPUParticleManager* gpu);

    void Update(Camera* camera, float deltaTime);
    void Draw();

    // プレビュー RT 用：同じインスタンスを別カメラで描画するため、各Primitive renderer の
    // プレビュー WVP を再計算する（位置/スケール/色は Update でメイン用に既に確定済み）。
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);
    void DrawPreview();

    /// <summary>
    /// 全コンポーネントが完了したか
    /// </summary>
    bool IsFinished() const { return finished_; }

    /// <summary>
    /// Timeline 表示用
    /// </summary>
    float GetElapsedTime() const  { return elapsedTime_; }
    float GetTotalDuration() const { return def_.totalDuration; }

    /// <summary>
    /// 残った確保中ライト等を解放（マネージャ側から呼ぶ）。
    /// </summary>
    void Cleanup();

private:
    EffectDef def_{};
    Vector3 worldPos_ = { 0.0f, 0.0f, 0.0f };
    float elapsedTime_ = 0.0f;
    bool finished_ = false;
    GPUParticleManager* gpu_ = nullptr;

    struct PrimitiveRuntime {
        std::unique_ptr<EffectPrimitiveRenderer> renderer;
        bool started = false;
        bool finished = false;
    };
    std::vector<PrimitiveRuntime> primitives_;

    struct ParticleRuntime {
        bool burstFired = false;
    };
    std::vector<ParticleRuntime> particles_;

    struct LightRuntime {
        bool started = false;
        bool finished = false;
        uint32_t slot = 0xFFFFFFFFu; // kInvalidLightSlot 相当
        bool isSpot = false;
    };
    std::vector<LightRuntime> lights_;

    struct SoundRuntime {
        bool started = false;
        bool finished = false;
        uint32_t handle = 0;  // SoundManager::Play3DSound のハンドル（0=無効）
    };
    std::vector<SoundRuntime> sounds_;
};
