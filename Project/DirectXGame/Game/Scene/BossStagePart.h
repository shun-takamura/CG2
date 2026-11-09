#pragma once
#include <memory>
#include <string>
#include "Vector2.h"
#include "Vector3.h"

class Camera;
class IImGuiEditable;
class EnemyController;
class BossBrainCommand;

/// <summary>
/// BossStagePart が StagePlayScene / GameScene に対して必要とする操作だけを切り出した
/// ホストインターフェース。StagePlayScene が private override で実装する（RailStagePart と同方式）。
/// </summary>
class IBossStageHost {
public:
	virtual ~IBossStageHost() = default;
	// プレハブをスプラインなしで指定座標にスポーンして返す（ボス本体に流用）。
	virtual IImGuiEditable* SpawnPrefabAt(const std::string& prefabName, const Vector3& pos) = 0;
	virtual void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) = 0;
	virtual void UpdateEnemyControllers(float dt, IImGuiEditable* player, float stageTimeSec) = 0;
	virtual void SweepDeadEntities() = 0;
	virtual IImGuiEditable* GetPlayer() const = 0;
	// Enemy/Boss/Terrain タグの動的エンティティ＋敵コントローラを一括破棄（Reset/シーン再入用）。
	virtual void ClearBossRuntimeState() = 0;
};

/// <summary>
/// StagePlayScene の Boss フェーズ（ボス戦・地上3Dアクション）専用ロジックを所有するコンポーネント。
/// ボス生成・ボスAI駆動・地上移動・ロックオン追従カメラを担当する。
/// 縦スライス最小構成：ボスは静止、1攻撃パターン、カメラはロックオン追従1種のみ。
/// </summary>
class BossStagePart {
public:
	BossStagePart();
	~BossStagePart();

	void Initialize(IBossStageHost* host, Camera* camera);

	// Phase::Boss 突入時に1回：ボス本体をスポーンし、ボスAIコントローラを登録、プレイヤーを地上開始位置へ。
	void Enter();
	// Boss フェーズ中に毎フレーム：ボスAI（敵コントローラ）更新＋撃破掃除＋ボス生存監視。
	void Update(float worldDt);
	// Rail の camera-local 配置の代わりに、カメラ相対の地上XZ平面をワールド空間で移動させる。
	void UpdatePlayerGroundMovement(IImGuiEditable* player, float dt, const Vector2& moveDelta);
	// カメラ前方をXZ平面へ射影した地上基底（forward=奥, right=右）。地上移動・プレイヤー向き・
	// ジャスト回避分身リングの配置など、地面と平行な方向計算をすべてここに一本化する。
	void ComputeGroundBasis(Vector3& outForward, Vector3& outRight) const;
	// STG の回避ダッシュ相当：移動入力方向へ地上速度の初速（dodgeDashSpeed_）を上乗せする。
	// 入力なし（moveDelta≈0）はその場回避。ボス戦の回避は playerVelocity_ ではなくこちらへ入れる。
	void ApplyDashImpulse(const Vector2& moveDelta);
	// 地上速度をゼロ化。近接派生でワープ／攻撃中は地上移動を止めるため、開始時に呼んで
	// 残留速度が派生終了後に持ち越されない（勝手に滑り出さない）ようにする。
	void StopGroundVelocity() { groundVelocity_ = { 0.0f, 0.0f }; }
	// 被弾ノックバック。attackerPos から離れる水平方向へ飛ばし、一定時間は移動入力を無視する。
	// heavy=true は近接系（大きく）、false は弾（小さく）。
	void ApplyKnockback(const Vector3& attackerPos, bool heavy);
	// プレイヤー周回・yaw/pitch 駆動の三人称カメラ。生入力（右スティック -1..1／マウス相対カウント）を受け、
	// 感度・反転は内部で適用する。reticle は画面中央固定運用のため、発射方向＝カメラ forward になる。
	void UpdateCamera(float dt, float stickX, float stickY, float mouseDx, float mouseDy);
	// ターゲットカメラ：一瞬ボス方向へスナップ（短押し）。ボスが無ければ無視。
	void RequestTargetSnap();
	// カメラモード切替（Free ⇔ LockOn。長押し）。
	void ToggleCamMode();
	// camYaw_/camPitch_ を現在のカメラ実姿勢に合わせて上書きする。近接派生カメラ（乗っ取り）から
	// 通常カメラへ制御を返す瞬間に呼ぶことで、古い角度への瞬間戻りを防ぐ（距離は camDistance_ に即切替）。
	void SyncYawPitchFromCamera();
	// ----- 着陸（Phase::Landing）：谷の出口から海の上の闘技場へ飛んで降りる -----
	// 今のカメラと自機の位置から、ボス戦の最初のカメラ・自機の開始位置までを曲線でつなぐ。
	// カメラと自機の位置はこのクラスが書く（自機の向きは GetLandingPlayerYaw をホストが適用）。
	void BeginLanding();
	// 進める。終わったら true（ホストは Phase::Boss へ）
	bool UpdateLanding(float dt);
	bool IsLandingActive() const { return landingActive_; }
	// 自機が足場に着いた後か（ホストは立ち姿勢にする）
	bool HasLandingTouchedDown() const { return landingTouchedDown_; }
	// 着いたフレームだけ true（羽根をしまう・水しぶき）
	bool ConsumeLandingTouchdown();
	float GetLandingPlayerYaw() const { return landingPlayerYaw_; }
	// 自機の開始位置（足場の上）とボスの出る位置。アリーナの前（arenaYaw_）の向きに並ぶ
	Vector3 GetPlayerStartPos() const;
	Vector3 GetBossStartPos() const;
	// ----- 撃破 → STAGE CLEAR → リザルト（演出は次週。今は「クリアした」と分かるところまで）-----
	// 撃破の爆発を見せる間 → 文字が出る → しばらく表示。入力・被弾はホストが止める
	void BeginClear();
	// 進める（UI 時間）。リザルトへ移る時に true
	bool UpdateClear(float dt);
	bool IsClearing() const { return clearTimer_ >= 0.0f; }
	// STAGE CLEAR の文字の不透明度（0..1）
	float GetClearTextAlpha() const;
	// Boss 離脱・シーン再入時のクリア（スポーン物・状態を破棄）。
	void Reset();
	// true の間、ボスは次の行動を選ばない（今の攻撃は最後までやる）。自機の死亡演出の間（19 番 G3）
	void SetBossStandDown(bool standDown);

	bool IsBossAlive() const { return boss_ != nullptr; }
	// ボス本体への参照（撃破で nullptr）。ボスは Enemy タグではないため nearestEnemy_ 等の
	// 照準トラッカーに乗らない＝ジャスト回避の攻撃元(attacker)はこちらを使う必要がある。
	IImGuiEditable* GetBossEntity() const { return boss_; }
	bool IsLockOn() const { return camMode_ == BossCamMode::LockOn; }
	// 床（BossArenaWater の石畳）を合わせるためのアリーナ情報
	float GetGroundY() const { return groundY_; }
	const Vector3& GetArenaCenter() const { return arenaCenter_; }
	// 海の高さと、足場の薄い水を描く円の半径（BossArenaGeometry.json）
	float GetSeaY() const { return seaY_; }
	float GetWaterRadius() const { return waterRadius_; }
	void OnImGuiTuning(bool& changed);

	// 撃破イベントを一度だけ通知する（検出直後の1回だけ true。呼ぶたびに消費される）。
	bool ConsumeBossDefeated();
	// 直近の撃破で得たスコア値（撃破検出時点の Gameplay ScoreValue をキャプチャ）。
	int GetLastBossScoreValue() const { return bossDefeatedScoreValue_; }

private:
	IBossStageHost* host_   = nullptr;
	Camera*         camera_ = nullptr;

	IImGuiEditable* boss_   = nullptr; // スポーンしたボス本体（撃破で null 化）
	BossBrainCommand* brain_ = nullptr; // ボスの AI（参照のみ。EnemyController が持つ。boss_ と一緒に null 化）
	bool  bossSpawned_ = false;

	// 撃破イベント（ConsumeBossDefeated で一度だけ読み出される）
	bool bossJustDefeated_ = false;
	int  bossDefeatedScoreValue_ = 0;

	// アリーナ / プレイヤー地上移動。寸法は BossArenaGeometry.json（gen_boss_arena.py が書く）で上書き
	Vector3 arenaCenter_{ 0.0f, 0.0f, 0.0f };
	float   arenaRadius_ = 30.0f;          // プレイヤーが出られる範囲（中心からの半径）
	float   groundY_     = 0.0f;           // プレイヤー原点（足元）の接地高さ
	float   arenaYaw_    = 0.0f;           // アリーナの前（ボスのいる向き）。谷から入ってくる向きにそろえる
	float   seaY_        = -150.0f;        // 海面
	float   waterRadius_ = 36.0f;          // 足場の薄い水の円（縁石の内側）
	float   camArenaRadius_ = 0.0f;        // カメラが出られる水平の半径（柱の内側）。0 で制限なし
	Vector2 groundVelocity_{ 0.0f, 0.0f }; // XZ 慣性（x=worldX, y=worldZ）
	float   playerMoveSpeed_ = 12.0f;
	float   playerSmoothTime_ = 0.12f;
	float   playerStartBack_  = 6.0f;      // ボス手前（-Z側）にこの距離だけ離して開始
	float   dodgeDashSpeed_   = 40.0f;     // 回避ダッシュの初速（units/sec。groundVelocity_ に加算し慣性で減衰）
	// 被弾ノックバック
	float   knockbackTimer_     = 0.0f;    // >0 の間は移動入力を無視して減速だけさせる
	float   knockLightSpeed_    = 16.0f;   // 弾：初速 [m/s]
	float   knockLightTime_     = 0.2f;    // 弾：入力無効時間 [s]
	float   knockLightHop_      = 0.3f;    // 弾：跳ね上げ高さ [m]
	float   knockHeavySpeed_    = 40.0f;   // 近接：初速 [m/s]
	float   knockHeavyTime_     = 0.6f;    // 近接：入力無効時間 [s]
	float   knockHeavyHop_      = 1.2f;    // 近接：跳ね上げ高さ [m]
	float   knockbackDecayTime_ = 0.35f;   // ノックバック中の減速の時定数 [s]（通常移動より長く滑らせる）
	float   knockbackDuration_  = 0.0f;    // 今回のノックバックの全長（跳ね上げの進行度に使う）
	float   knockbackHop_       = 0.0f;    // 今回の跳ね上げ高さ

	// ボス配置
	float   bossHeight_  = 2.0f;   // ボス中心の接地高さ（≒コライダー半径）
	float   bossForward_ = 12.0f;  // アリーナ中心から +Z にこの距離

	// ----- カメラ（プレイヤー周回・yaw/pitch 駆動の3モード）-----
	enum class BossCamMode { Free, LockOn };
	BossCamMode camMode_ = BossCamMode::Free;
	float camYaw_   = 0.0f;   // 周回方位角（rad）。カメラ forward の水平向き
	float camPitch_ = 0.15f;  // 俯角（rad）。下向き＋
	// 一瞬スナップ（ターゲットカメラ）
	float snapTimer_    = -1.0f; // >=0 で進行中
	float snapDuration_ = 0.25f;
	float snapFromYaw_ = 0.0f, snapFromPitch_ = 0.0f, snapToYaw_ = 0.0f, snapToPitch_ = 0.0f;
	// チューニング
	float lookSensStick_ = 2.5f;    // 右スティック全倒し時 rad/s
	float lookSensMouse_ = 0.0028f; // マウス1カウントあたり rad
	float pitchMin_ = -0.4f, pitchMax_ = 1.1f; // 俯角クランプ
	float camDistance_    = 16.0f;  // プレイヤー背後距離（周回半径）
	float camFocusHeight_ = 2.5f;   // 注視点（プレイヤー上方）高さ＝体の中心
	// 地面めり込み回避：見上げてカメラが地面下に入る角度では、プレイヤーへ寄せて地面の上に留める
	float camGroundClearance_ = 0.5f;  // 地面からカメラまでの最低余白 [m]
	float camMinDistance_     = 2.0f;  // 寄せるときの最短距離 [m]（これでも潜るなら高さを持ち上げる）
	float camDistanceReturnTime_ = 0.1f; // 寄せた距離を元へ戻すときの時定数 [s]
	float camEffectiveDistance_ = -1.0f; // 実際に使っている距離（<0 は未初期化）
	float lockSpringGain_ = 2.5f;   // ロックオンのボス方向への引き（小さいほどラグ/低精度）

	// 闘技場の寸法を読む（無ければ既定値のまま）
	void LoadArenaGeometry();
	// ボス戦の最初のカメラ（自機の開始位置からボスの出る位置を向く）。Enter と着陸の終点で同じ姿勢にする
	void ComputeStartCamera(Vector3& outEye, float& outYaw, float& outPitch) const;

	// ----- 撃破 → クリア -----
	float clearTimer_     = -1.0f; // < 0 で未開始
	float clearDelay_     = 1.5f;  // 撃破の爆発を見せる [s]
	float clearTextFade_  = 0.5f;  // 文字が出るまで [s]
	float clearHold_      = 3.0f;  // 文字を見せる [s]（この後リザルトへ）

	// ----- 着陸 -----
	bool  landingActive_      = false;
	bool  landingTouchedDown_ = false;
	bool  landingTouchdownEvent_ = false;
	float landingTimer_       = 0.0f;
	float landingDuration_    = 7.0f;   // 全体 [s]
	float landingPlayerRatio_ = 0.8f;   // 自機が着くまでの割合（残りはカメラが落ち着くだけ）
	float landingStartSpeed_  = 17.0f;  // レールの終わりの速さ [m/s]（谷の点は 3 秒で 50m）
	float landingCamLift_     = 45.0f;  // カメラが闘技場を見下ろしに上がる高さ（終点の手前の制御点）[m]
	float landingCamBack_     = 70.0f;  // 同・終点から後ろへ [m]
	float landingPlayerLift_  = 18.0f;  // 自機が上から降りてくる高さ [m]
	float landingPlayerBack_  = 20.0f;  // 同・後ろへ [m]
	float landingLookAhead_   = 120.0f; // 始めに見ている点（進む向きの先）[m]
	float landingPlayerYaw_   = 0.0f;
	Vector3 landingCam_[4]{};           // カメラの 3 次ベジェ
	Vector3 landingPlayer_[4]{};        // 自機の 3 次ベジェ
	Vector3 landingLookStart_{};        // 始めに見ている点
	Vector3 landingFocusEnd_{};         // 終わりに見る点（自機の体の中心）

	// ボス方向の yaw/pitch を算出（プレイヤー focus からボス中心へ）。boss_ が無ければ現状維持。
	bool ComputeBossYawPitch(float& outYaw, float& outPitch) const;

	std::string bossPrefab_   = "boss";
};
