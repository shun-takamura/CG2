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
	// ロックオン追従カメラ（ボスとプレイヤーを画面に収める）。
	void UpdateCamera();
	// Boss 離脱・シーン再入時のクリア（スポーン物・状態を破棄）。
	void Reset();

	bool IsBossAlive() const { return boss_ != nullptr; }
	void OnImGuiTuning(bool& changed);

private:
	IBossStageHost* host_   = nullptr;
	Camera*         camera_ = nullptr;

	IImGuiEditable* boss_   = nullptr; // スポーンしたボス本体（撃破で null 化）
	IImGuiEditable* ground_ = nullptr;
	bool  bossSpawned_ = false;

	// アリーナ / プレイヤー地上移動
	Vector3 arenaCenter_{ 0.0f, 0.0f, 0.0f };
	float   arenaRadius_ = 30.0f;          // プレイヤーが出られる範囲（中心からの半径）
	float   groundY_     = 0.0f;           // プレイヤー原点（足元）の接地高さ
	Vector2 groundVelocity_{ 0.0f, 0.0f }; // XZ 慣性（x=worldX, y=worldZ）
	float   playerMoveSpeed_ = 12.0f;
	float   playerSmoothTime_ = 0.12f;
	float   playerStartBack_  = 6.0f;      // ボス手前（-Z側）にこの距離だけ離して開始

	// ボス配置
	float   bossHeight_  = 2.0f;   // ボス中心の接地高さ（≒コライダー半径）
	float   bossForward_ = 12.0f;  // アリーナ中心から +Z にこの距離

	// ロックオン追従カメラ
	float   camDistance_ = 18.0f;  // プレイヤー背後（ボス逆方向）へ引く距離
	float   camHeight_   = 8.0f;   // 持ち上げ高さ
	float   camLookLerp_ = 0.15f;  // 注視点追従の平滑（0=即時,1=固まる方向へ）
	Vector3 camLookCur_{ 0.0f, 0.0f, 0.0f }; // 平滑済み注視点

	std::string bossPrefab_   = "boss";
	std::string groundPrefab_ = "boss_ground";
};
