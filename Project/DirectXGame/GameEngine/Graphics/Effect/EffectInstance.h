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
    /// <param name="def">再生するエフェクト定義</param>
    /// <param name="worldPos">エフェクト中心のワールド座標</param>
    /// <param name="gpu">Particle コンポーネントの発射先</param>
    void Initialize(const EffectDef& def, const Vector3& worldPos, GPUParticleManager* gpu);

    /// <summary>
    /// 時間を進め、各コンポーネントの出現・補間・発射・ライト/サウンドの確保と解放を行う。
    /// 終わりに停止要求・ループ・全完了を判定する
    /// </summary>
    /// <param name="camera">Primitive のビルボードと WVP に使うカメラ</param>
    /// <param name="deltaTime">unscaled な実経過時間 [秒]（成分ごとの TimeGroup で中でスケールする。プレビューはそのまま）</param>
    void Update(Camera* camera, float deltaTime);
    /// <summary>出現中で未完了の Primitive をシーンへ描く</summary>
    void Draw();
    /// <summary>水面の反射 RT へ Primitive を描く（def の reflect が false なら何もしない）</summary>
    /// <param name="mirroredView">水面で折り返したビュー行列</param>
    /// <param name="mirroredViewProjection">水面で折り返したビュープロジェクション行列</param>
    /// <param name="mirroredCameraPos">水面で折り返したカメラ位置</param>
    void DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos);

    /// <summary>
    /// 編集中の def をライブ反映する（EffectEditor プレビュー用）。
    /// メッシュの作り直しが要る primitive だけ部分リビルドし、elapsedTime_ は保持する。
    /// </summary>
    /// <param name="newDef">反映する編集中の定義</param>
    void SyncFromDef(const EffectDef& newDef);

    /// <summary>
    /// t=0 へ巻き戻す（runtime を片付けて最初から再生し直す）。「Restart」の実体。
    /// </summary>
    void Rewind();

    /// <summary>useDistortion 有効な各 primitive renderer を distortionRT へ描画する</summary>
    void DrawDistortionPass();
    /// <summary>DrawDistortionPass のプレビュー版（preview WVP CB を使う）</summary>
    void DrawDistortionPassPreview();

    /// <summary>このインスタンスが描画すべき distortion source を1つ以上持っているか</summary>
    /// <returns>歪みテクスチャ付きの Primitive が出現中なら true</returns>
    bool HasActiveDistortionSource() const;

    /// <summary>useBloom 有効な各 primitive renderer を発光 RT へ描画する（パーティクルは GPUParticleManager 側）</summary>
    void DrawBloomPass();
    /// <summary>DrawBloomPass のプレビュー版（preview WVP CB を使う）</summary>
    void DrawBloomPassPreview();
    /// <summary>発光させる primitive を1つ以上描画中か</summary>
    /// <returns>useBloom の Primitive が出現中なら true</returns>
    bool HasActiveBloomSource() const;

    /// <summary>
    /// プレビュー RT 用：同じインスタンスを別カメラで描画するため、各Primitive renderer の
    /// プレビュー WVP を再計算する（位置/スケール/色は Update でメイン用に既に確定済み）。
    /// </summary>
    /// <param name="viewMatrix">プレビューカメラのビュー行列</param>
    /// <param name="viewProjectionMatrix">プレビューカメラのビュープロジェクション行列</param>
    /// <param name="cameraPos">プレビューカメラの位置</param>
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);
    /// <summary>出現中の Primitive をプレビュー WVP で描く</summary>
    void DrawPreview();

    /// <summary>
    /// 全コンポーネントが完了したか
    /// </summary>
    /// <returns>完了していれば true</returns>
    bool IsFinished() const { return finished_; }

    /// <summary>
    /// Timeline 表示用の経過時間
    /// </summary>
    /// <returns>再生開始（ループなら今の周の開始）からの経過 [秒]</returns>
    float GetElapsedTime() const  { return elapsedTime_; }
    /// <summary>Timeline 表示用の全体の長さ</summary>
    /// <returns>def の totalDuration [秒]</returns>
    float GetTotalDuration() const { return def_.totalDuration; }

    /// <summary>
    /// 残った確保中ライト等を解放（マネージャ側から呼ぶ）。
    /// 3D サウンドも止める。Primitive の renderer はデストラクタ任せ（Draw 中に呼ばれうるため）
    /// </summary>
    void Cleanup();

    // ----- ハンドル経由の外部制御 -----
    /// <summary>EffectManager が振ったハンドルを覚える</summary>
    /// <param name="h">ハンドル</param>
    void SetHandle(uint64_t h) { handle_ = h; }
    /// <summary>ハンドルを返す</summary>
    /// <returns>EffectManager が振ったハンドル（未設定なら 0）</returns>
    uint64_t GetHandle() const { return handle_; }

    /// <summary>
    /// 弾丸など、エフェクト中心を毎フレーム動かしたいケース用。
    /// 次の Update から新しい worldPos が使われる。
    /// </summary>
    /// <param name="pos">エフェクト中心のワールド座標</param>
    void SetWorldPosition(const Vector3& pos) { worldPos_ = pos; }

    /// <summary>
    /// エフェクト全体の向き（オイラー角・ラジアン）。各コンポーネントの offset を
    /// この回転で回し、各 Primitive の向きにも加算する。弾の進行方向追従などに使う。
    /// </summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetWorldRotation(const Vector3& rotate) { worldRotation_ = rotate; }

    /// <summary>
    /// 外部から終了を要求。次の Update で loop を抜けて finished にする。
    /// </summary>
    void RequestStop() { stopRequested_ = true; }

    /// <summary>
    /// プレビュー（エディタ）インスタンスとして扱うか。true の場合、GPUパーティクルの
    /// グループ名にプレフィックスを付けてシーンと物理バッファを分離する。
    /// </summary>
    /// <param name="p">プレビューなら true</param>
    void SetPreview(bool p) { isPreview_ = p; }
    /// <summary>プレビュー（エディタ）インスタンスか</summary>
    /// <returns>プレビューなら true</returns>
    bool IsPreview() const { return isPreview_; }

    /// <summary>
    /// このインスタンスが使用する GPUパーティクルグループ名（プレビューならプレフィックス込み）を
    /// out に追加する。EffectManager がプレビューグループのリサイクル keep set を作るのに使う。
    /// プレビューでなければ何もしない。
    /// </summary>
    /// <param name="out">追加先</param>
    void CollectPreviewGroupNames(std::vector<std::string>& out) const;

private:
    /// <summary>
    /// loop 用：elapsedTime と各 runtime をリセット（現在確保中のリソースは解放）。
    /// </summary>
    void ResetForLoop();

    /// <summary>
    /// GPUパーティクルグループ名を解決する。プレビューインスタンスなら kPreviewPrefix を付け、
    /// シーン用グループと物理バッファを分離する。
    /// </summary>
    /// <param name="name">定義上のグループ名</param>
    /// <returns>実際に使うグループ名。空名はそのまま返す</returns>
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
