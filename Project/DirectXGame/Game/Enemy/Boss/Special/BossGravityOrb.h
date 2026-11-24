#pragma once
#include "Enemy/Boss/Special/BossGravityOrbFx.h"
#include "Enemy/Boss/Special/BossGravityRocks.h"
#include "Vector3.h"
#include <random>
#include <string>

class Camera;
class DirectXCore;
class IImGuiEditable;
class Object3DManager;
class SRVManager;

/// <summary>
/// GravityOrb がシーンに頼むことだけを並べた窓口（StagePlayScene が実装する）
/// </summary>
class IBossGravityOrbHost {
public:
	virtual ~IBossGravityOrbHost() = default;
	/// <summary>自機を返す</summary>
	/// <returns>自機。いなければ nullptr</returns>
	virtual IImGuiEditable* GetGravityOrbPlayer() const = 0;
	/// <summary>闘技場の地面の高さ（自機の足元の高さ）を返す</summary>
	/// <returns>地面の高さ</returns>
	virtual float GetGravityOrbGroundY() const = 0;
	/// <summary>盾に隠れていなかった自機にダメージを与える</summary>
	/// <param name="damageRatio">最大 HP に対するダメージの割合</param>
	virtual void OnGravityOrbPlayerHit(float damageRatio) = 0;
	/// <summary>カメラを揺らす</summary>
	/// <param name="intensity">強さ</param>
	/// <param name="duration">長さ [s]</param>
	virtual void ShakeGravityOrbCamera(float intensity, float duration) = 0;
	/// <summary>空を暗くする／戻す</summary>
	/// <param name="dark">暗くするなら true</param>
	virtual void SetGravityOrbSkyDark(bool dark) = 0;
};

/// <summary>
/// ボスの必殺技 GravityOrb の段取り（鬼武者のかぐや姫の岩 × ムフェト・ジーヴァの落ちる球）。
/// ボスがアリーナの中心へ浮く → がれきが浮く → 円周の岩が浮く → 盾以外が崩れる → 重力が戻って落ちる →
/// 球を溜めて放つ → ゆっくり落ちる → 着地 → 爆発（ここで盾の判定）→ 余韻、を時間で進める。
/// 攻撃のステート（BossAttackGravityOrb）は Begin して、ボスの位置を GetBossPosition から受け取るだけ。
/// 岩は BossGravityRocks、光や画面の演出は BossGravityOrbFx に任せる。調整値は Resources/Json/Tuning/BossGravityOrb.json。
/// </summary>
class BossGravityOrb {
public:
	/// <summary>段取りの段階</summary>
	enum class Phase {
		Idle,
		Rise,       // ボスが中心へ浮く。がれきが浮き始める
		RocksRise,  // 円周の岩が浮く
		Crumble,    // 盾以外の岩が空中で崩れる
		Drop,       // 重力が戻り、破片と盾の岩が落ちる
		Charge,     // 球を溜める
		Fall,       // 放った球がゆっくり落ちる（世界が暗い）
		Impact,     // 球が着地した
		Whiteout,   // 爆発で画面が白い（入った瞬間に盾の判定）
		Aftermath,  // 白が晴れ、盾が崩れて消える。ボスが降りる
		Done,
	};

	/// <summary>段取りの時間と強さ（JSON の "timeline" に保存）</summary>
	struct Tuning {
		float riseTime = 1.6f;
		float rocksRiseTime = 2.2f;
		float crumbleTime = 0.9f;
		float dropTime = 1.2f;
		float chargeTime = 1.6f;
		float fallTime = 3.0f;
		float impactDelay = 0.4f;        // 着地からモノクロ②・閃光まで（着地の小さな爆発の間）[s]
		float impactTime = 0.5f;         // モノクロ②から爆発（盾の判定）まで [s]
		float whiteoutTime = 1.3f;
		float aftermathTime = 2.5f;
		float bossHoverHeight = 9.0f;    // ボスが浮く高さ（地面から）[m]
		float bossBob = 0.3f;            // 浮いている間の上下の揺れ [m]
		float orbOffsetY = 2.5f;         // 球を溜める位置（ボスの位置から上）[m]
		float damageRatio = 0.6f;        // 隠れ損ねたときのダメージ（最大 HP に対する割合）
		float rockLandShake = 0.3f;      // 盾の岩が着地したときの揺れ
		float releaseShake = 0.4f;       // 球を放つ瞬間の揺れ
		float impactShake = 0.8f;        // 着地の揺れ
		float explosionShake = 1.2f;     // 爆発の揺れ
		float shakeTime = 0.4f;          // 揺れの長さ [s]
	};

	/// <summary>岩を割って描画を用意し、調整値を読む（読み込み時に 1 回）</summary>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager, IBossGravityOrbHost* host);
	/// <summary>中断して GPU リソースを解放する</summary>
	void Finalize();

	/// <summary>技を始める</summary>
	/// <param name="bossPosition">今のボスの位置</param>
	/// <param name="arenaCenter">アリーナの中心（ここへ浮いて、岩の円の中心にする）</param>
	/// <param name="arenaRadius">アリーナの半径（岩をこの中へ収める。0 以下で制限なし）</param>
	/// <returns>始められたら true（シーンの窓口が無ければ false）</returns>
	bool Begin(const Vector3& bossPosition, const Vector3& arenaCenter, float arenaRadius);
	/// <summary>途中でやめて、出している物を全部消す（リトライ・ボス戦を抜けたとき）</summary>
	void Abort();

	/// <summary>段取りを進める（技が終わった後も、消えていく破片や閃光のために毎フレーム呼ぶ）</summary>
	/// <param name="dt">経過時間 [s]（World）</param>
	void Update(float dt);
	/// <summary>割れていない岩に重なった自機を押し出す（自機を動かした直後に呼ぶ）</summary>
	/// <param name="playerPos">自機の足元の位置（押し出した位置を書き戻す）</param>
	void ResolvePlayerCollision(Vector3& playerPos) const;
	/// <summary>岩を描く（3D の不透明物の描画の中で呼ぶ）</summary>
	/// <param name="camera">カメラ</param>
	void Draw(const Camera& camera);
	/// <summary>画面を覆う閃光を描く（スプライトの描画の中で呼ぶ）</summary>
	void DrawOverlay(float screenW, float screenH) const;

	/// <summary>技の途中か</summary>
	/// <returns>Begin から Done までの間なら true</returns>
	bool IsRunning() const { return phase_ != Phase::Idle && phase_ != Phase::Done; }
	/// <summary>技が終わったか（ステートが次へ進んでよい）</summary>
	/// <returns>終わったら true</returns>
	bool IsFinished() const { return phase_ == Phase::Done; }
	/// <summary>今のボスの位置（攻撃のステートが毎フレーム書き込む）</summary>
	/// <returns>ボスの位置</returns>
	Vector3 GetBossPosition() const { return bossPosition_; }
	/// <summary>今の段階</summary>
	/// <returns>段階</returns>
	Phase GetPhase() const { return phase_; }
	/// <summary>溜め中か（ボスの予兆の点滅に使う）</summary>
	/// <returns>溜め中なら true</returns>
	bool IsCharging() const { return phase_ == Phase::Charge; }
	/// <summary>段階の経過時間と長さから、段階の進み具合（0〜1）</summary>
	/// <returns>進み具合</returns>
	float GetPhaseProgress() const;

	/// <summary>ImGui の「今すぐ使う」が押されたか（ボスの行動選びが 1 回だけ受け取る）</summary>
	/// <returns>押されていたら true</returns>
	bool ConsumeDebugTrigger();

	/// <summary>調整値を読む</summary>
	void LoadTuning();
	/// <summary>調整値を保存する</summary>
	void SaveTuning() const;
	/// <summary>調整値の ImGui</summary>
	void OnImGui();

private:
	void EnterPhase(Phase phase);
	float PhaseDuration(Phase phase) const;
	void UpdateBossPosition();
	void UpdateOrb();
	/// <summary>爆発の瞬間：盾に隠れていなければダメージ</summary>
	void JudgePlayer();
	void Shake(float intensity) const;

	IBossGravityOrbHost* host_ = nullptr;
	Tuning tuning_;
	BossGravityRocks rocks_;
	BossGravityOrbFx fx_;
	std::mt19937 rng_{ std::random_device{}() };

	Phase phase_ = Phase::Idle;
	float phaseTimer_ = 0.0f;
	Vector3 center_{ 0.0f, 0.0f, 0.0f };  // 岩の円の中心・球の着弾地点（地面の高さ）
	float groundY_ = 0.0f;
	Vector3 bossStart_{ 0.0f, 0.0f, 0.0f };
	Vector3 bossPosition_{ 0.0f, 0.0f, 0.0f };
	Vector3 orbStart_{ 0.0f, 0.0f, 0.0f };
	Vector3 orbPosition_{ 0.0f, 0.0f, 0.0f };
	float bobTime_ = 0.0f;
	bool lastShielded_ = false;  // 最後の判定の結果（ImGui の確認用）
	bool impactBurstDone_ = false;  // 着地の段階でモノクロ②・閃光を出したか
	bool debugTrigger_ = false;
};
