#pragma once
#include <memory>
#include <string>
#include "Vector2.h"
#include "Vector3.h"

class Camera;
class IImGuiEditable;
class EnemyController;

/// <summary>
/// BossStagePart が StagePlayScene / GameScene に対して必要とする操作だけを切り出した
/// ホストインターフェース。StagePlayScene が private override で実装する（RailStagePart と同方式）。
/// </summary>
class IBossStageHost {
public:
	virtual ~IBossStageHost() = default;
	// プレハブをスプラインなしで指定座標にスポーンして返す（ボス本体・地面に流用）。
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

	// Phase::Boss 突入時に1回：地面・ボス本体をスポーンし、ボスAIコントローラを登録、プレイヤーを地上開始位置へ。
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
	// プレイヤー周回・yaw/pitch 駆動の三人称カメラ。生入力（右スティック -1..1／マウス相対カウント）を受け、
	// 感度・反転は内部で適用する。reticle は画面中央固定運用のため、発射方向＝カメラ forward になる。
	void UpdateCamera(float dt, float stickX, float stickY, float mouseDx, float mouseDy);
	// ターゲットカメラ：一瞬ボス方向へスナップ（短押し）。ボスが無ければ無視。
	void RequestTargetSnap();
	// カメラモード切替（Free ⇔ LockOn。長押し）。
	void ToggleCamMode();
	// Boss 離脱・シーン再入時のクリア（スポーン物・状態を破棄）。
	void Reset();

	bool IsBossAlive() const { return boss_ != nullptr; }
	bool IsLockOn() const { return camMode_ == BossCamMode::LockOn; }
	void OnImGuiTuning(bool& changed);

	// 撃破イベントを一度だけ通知する（検出直後の1回だけ true。呼ぶたびに消費される）。
	bool ConsumeBossDefeated();
	// 直近の撃破で得たスコア値（撃破検出時点の Gameplay ScoreValue をキャプチャ）。
	int GetLastBossScoreValue() const { return bossDefeatedScoreValue_; }

private:
	IBossStageHost* host_   = nullptr;
	Camera*         camera_ = nullptr;

	IImGuiEditable* boss_   = nullptr; // スポーンしたボス本体（撃破で null 化）
	IImGuiEditable* ground_ = nullptr;
	bool  bossSpawned_ = false;

	// 撃破イベント（ConsumeBossDefeated で一度だけ読み出される）
	bool bossJustDefeated_ = false;
	int  bossDefeatedScoreValue_ = 0;

	// アリーナ / プレイヤー地上移動
	Vector3 arenaCenter_{ 0.0f, 0.0f, 0.0f };
	float   arenaRadius_ = 30.0f;          // プレイヤーが出られる範囲（中心からの半径）
	float   groundY_     = 0.0f;           // プレイヤー原点（足元）の接地高さ
	Vector2 groundVelocity_{ 0.0f, 0.0f }; // XZ 慣性（x=worldX, y=worldZ）
	float   playerMoveSpeed_ = 12.0f;
	float   playerSmoothTime_ = 0.12f;
	float   playerStartBack_  = 6.0f;      // ボス手前（-Z側）にこの距離だけ離して開始
	float   dodgeDashSpeed_   = 40.0f;     // 回避ダッシュの初速（units/sec。groundVelocity_ に加算し慣性で減衰）

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
	float lockSpringGain_ = 2.5f;   // ロックオンのボス方向への引き（小さいほどラグ/低精度）

	// ボス方向の yaw/pitch を算出（プレイヤー focus からボス中心へ）。boss_ が無ければ現状維持。
	bool ComputeBossYawPitch(float& outYaw, float& outPitch) const;

	std::string bossPrefab_   = "boss";
	std::string groundPrefab_ = "boss_ground";
};
