#pragma once
#include "EffectDef.h"
#include "EffectPrimitiveRenderer.h"
#include "Vector3.h"
#include "Matrix4x4.h"
#include "Quaternion.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

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

    /// <summary>
    /// 編集中の def をライブ反映する（EffectEditor プレビュー用）。
    /// 値（scale/color/timing 等の毎フレーム読みフィールド）は def_ 差し替えで即反映。
    /// コンポーネント数が変わった型はその型の runtime を作り直す。
    /// meshType / ジオメトリ params が変わった primitive runtime だけレンダラを部分リビルド。
    /// elapsedTime_ は保持する。
    /// </summary>
    void SyncFromDef(const EffectDef& newDef);

    /// <summary>
    /// t=0 へ巻き戻す（runtime を片付けて最初から再生し直す）。「Restart」の実体。
    /// </summary>
    void Rewind();

    // useDistortion 有効な各 primitive renderer を distortionRT へ描画する
    void DrawDistortionPass();
    // 上記のプレビュー版（preview WVP CB を使う）
    void DrawDistortionPassPreview();

    // このインスタンスが描画すべき distortion source を1つ以上持っているか
    bool HasActiveDistortionSource() const;

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

    // ----- ハンドル経由の外部制御 -----
    void SetHandle(uint64_t h) { handle_ = h; }
    uint64_t GetHandle() const { return handle_; }

    /// <summary>
    /// 弾丸など、エフェクト中心を毎フレーム動かしたいケース用。
    /// 次の Update から新しい worldPos が使われる。
    /// </summary>
    void SetWorldPosition(const Vector3& pos) { worldPos_ = pos; }

    /// <summary>
    /// エフェクト全体の向き（オイラー角・ラジアン）。各コンポーネントの offset を
    /// この回転で回し、各 Primitive の向きにも加算する。弾の進行方向追従などに使う。
    /// </summary>
    void SetWorldRotation(const Vector3& rotate) { worldRotation_ = rotate; }

    /// <summary>
    /// 外部から終了を要求。次の Update で loop を抜けて finished にする。
    /// </summary>
    void RequestStop() { stopRequested_ = true; }

    /// <summary>
    /// プレビュー（エディタ）インスタンスとして扱うか。true の場合、GPUパーティクルの
    /// グループ名にプレフィックスを付けてシーンと物理バッファを分離する。
    /// </summary>
    void SetPreview(bool p) { isPreview_ = p; }
    bool IsPreview() const { return isPreview_; }

    /// <summary>
    /// このインスタンスが使用する GPUパーティクルグループ名（プレビューならプレフィックス込み）を
    /// out に追加する。EffectManager がプレビューグループのリサイクル keep set を作るのに使う。
    /// </summary>
    void CollectPreviewGroupNames(std::vector<std::string>& out) const;

private:
    /// <summary>
    /// loop 用：elapsedTime と各 runtime をリセット（現在確保中のリソースは解放）。
    /// </summary>
    void ResetForLoop();

    /// <summary>
    /// GPUパーティクルグループ名を解決する。プレビューインスタンスなら kPreviewPrefix を付け、
    /// シーン用グループと物理バッファを分離する。空名はそのまま返す。
    /// </summary>
    std::string EffGroupName(const std::string& name) const;

    EffectDef def_{};
    Vector3 worldPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 worldRotation_ = { 0.0f, 0.0f, 0.0f }; // エフェクト全体の向き（オイラー角・ラジアン）
    float elapsedTime_ = 0.0f;
    float orbitClock_ = 0.0f; // ループでリセットしない累積時間（orbit の発生平面回転を粒子と同期させる用）
    bool finished_ = false;
    bool stopRequested_ = false;
    uint64_t handle_ = 0;
    GPUParticleManager* gpu_ = nullptr;
    bool isPreview_ = false; // プレビュー（エディタ）インスタンスなら true

    struct PrimitiveRuntime {
        std::unique_ptr<EffectPrimitiveRenderer> renderer;
        bool started = false;
        bool finished = false;
        float clock = 0.0f; // この成分の TimeGroup でスケールした専用経過時間（spawn/アニメ進行に使う）
        // 持続回転を積分した現在の向き（基準rotate＋出現時ランダムを spawn 時に初期化）。
        Quaternion orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    };
    std::vector<PrimitiveRuntime> primitives_;

    struct ParticleRuntime {
        bool burstFired = false;
        float clock = 0.0f; // この成分の TimeGroup でスケールした専用経過時間（burst スケジュールに使う）
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
