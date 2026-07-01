#pragma once
#include "Scene.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// 前方宣言
class IImGuiEditable;
class SplineCurveActor;
class EnemyController;

/// <summary>
/// ゲーム用シーン基底。エンジン足場 Scene に、本作のゲームロジック
/// （プレイヤー弾・近接、敵・敵弾・敵コントローラ、プレハブ生成、スプライン）を加える。
/// 具体シーン（Title / StagePlay / Hub / Result / Demo）はこれを継承する。
/// </summary>
class GameScene : public Scene {
public:
	GameScene();
	~GameScene() override;

	//====================
	// プレハブ生成（Scene の no-op を override）
	//====================
	/// <summary>
	/// プレハブ名でシーンに動的配置（PrefabManager から PrefabDef を引いて生成）。
	/// </summary>
	void InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {}) override;

	//====================
	// スプライン動的管理（Scene の no-op を override）
	//====================
	void AddDynamicSpline(int tagInt, const Vector3& worldPos = {}) override;
	void RemoveDynamicSpline(const std::string& name) override;
	/// <summary>シーン内の SplineCurveActor を名前で取得（無ければ nullptr）。</summary>
	SplineCurveActor* FindDynamicSplineByName(const std::string& name);

	//====================
	// プレイヤー弾 / 近接
	//====================
	/// <summary>
	/// プレイヤー弾を1発スポーン。Primitive プレハブから生成し、velocity と寿命を関連付ける。
	/// </summary>
	virtual void SpawnPlayerBullet(const Vector3& pos, const Vector3& direction,
		float speed = 80.0f, float lifetime = 2.0f,
		float colliderGrowthPerMeter = 0.0f,
		IImGuiEditable* homingTarget = nullptr,
		float homingStrength = 0.0f,
		float maxTravelDistance = 0.0f,
		const std::string& prefabName = "TemporaryPlayerBullet",
		int attackPower = 0);

	/// <summary>
	/// プレイヤー近接攻撃の判定を1つスポーン。owner に追従させて持続させる。
	/// </summary>
	virtual void SpawnPlayerMelee(IImGuiEditable* owner,
		const Vector3& right, const Vector3& up, const Vector3& forward,
		const std::string& prefabName,
		int attackPower);

	//====================
	// 敵 / 敵弾 / 敵コントローラ
	//====================
	/// <summary>
	/// 動的エンティティを破棄キューへ移送する。ゲーム状態（弾・敵）の dangling 参照を掃除した上で
	/// 基底 Scene::DestroyDynamicEntity に実体除去を委譲する。
	/// </summary>
	void DestroyDynamicEntity(IImGuiEditable* e) override;

	/// <summary>
	/// 指定プレハブから敵を1体スポーンし、指定スプラインに沿って進ませる。
	/// </summary>
	virtual IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName,
		SplineCurveActor* spline, float speed = 0.1f, bool removeAtEnd = true,
		float initialT = 0.0f,
		int   waveEntryIndex = -1);

	/// <summary>敵弾をスポーンする。EnemyAttack タグ付き。</summary>
	void SpawnEnemyBullet(const Vector3& pos, const Vector3& direction,
		float speed = -1.0f, float lifetime = -1.0f,
		const std::string& prefabName = "EnemyBullet",
		IImGuiEditable* homingTarget = nullptr,
		float homingStrength = -1.0f);

	/// <summary>指定プレハブをスプラインなしで指定座標に直接スポーンする。</summary>
	IImGuiEditable* SpawnEnemyAt(const std::string& prefabName, const Vector3& pos);

	/// <summary>指定エンティティと他 Enemy との衝突半径オーバーラップを解消する（相互排斥）。</summary>
	void ApplyEnemyRepulsion(IImGuiEditable* self);

	/// <summary>EnemyController を登録する（コマンドからのランタイム追加用）。</summary>
	void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl);

#ifdef USE_IMGUI
	/// <summary>動的オブジェクト判定。エンジン管理分に加えてスプラインも確認する。</summary>
	bool IsDynamicObject(IImGuiEditable* editable) const override;
#endif

protected:
	//====================
	// 弾 / 近接 / 敵の更新（派生シーンの Update から呼ぶ）
	//====================
	void UpdateBullets(float deltaTime);
	void SweepDeadEntities();
	void UpdateMelees(float deltaTime);
	void UpdateMovingEnemies(float deltaTime);
	void UpdateEnemyControllers(float deltaTime, IImGuiEditable* player, float stageTimeSec);

	/// <summary>
	/// 攻撃命中時のエフェクト再生。攻撃側プレハブの "hit"（着弾エフェクト）と
	/// 被弾側プレハブの "hurt"（被弾エフェクト）を同じ位置で両方再生する。
	/// 未設定のスロットはスキップする。attacker / target が nullptr の側もスキップ。
	/// </summary>
	void PlayHitEffects(IImGuiEditable* attacker, IImGuiEditable* target, const Vector3& pos);

	/// <summary>動的スプラインの DebugDraw キュー積み。</summary>
	void DrawDynamicSplinesDebug();

	struct BulletRuntime {
		PrimitiveInstance* primitive = nullptr; // dynamicPrimitives_ 内の生ポインタ
		Vector3 velocity{ 0.0f, 0.0f, 0.0f };
		float speed = 0.0f;
		float remainingLifetime = 0.0f;
		Vector3 originPos{ 0.0f, 0.0f, 0.0f };
		float baseColliderRadius = 0.0f;
		float colliderGrowthPerMeter = 0.0f;
		IImGuiEditable* homingTarget = nullptr;
		float homingStrength = 0.0f;
		float maxTravelDistance = 0.0f;
		bool        penetrate = false;
		float       penetrateDamageRate = 0.2f;
		std::string penetrateEffect;
		int         penetrateDamage = 0;
		std::unordered_map<IImGuiEditable*, float> hitCooldowns;
		uint64_t    trailEffectHandle = 0;
	};
	std::vector<BulletRuntime> bullets_;

	struct MeleeRuntime {
		PrimitiveInstance* primitive = nullptr;
		IImGuiEditable* owner = nullptr;
		Vector3 worldOffset{ 0.0f, 0.0f, 0.0f };
		float remainingLifetime = 0.0f;
		float elapsed = 0.0f;
		float cleanWindow = 0.08f;
		int   cleanDamage = 0;
		int   lateDamage = 0;
		uint64_t    swingEffectHandle = 0;
		std::unordered_set<IImGuiEditable*> hitTargets;
	};
	std::vector<MeleeRuntime> melees_;

	struct MovingEnemy {
		IImGuiEditable* entity = nullptr;
		SplineCurveActor* spline = nullptr;
		float t = 0.0f;
		float speed = 0.1f;
		bool  removeAtEnd       = true;
		bool  billboardToPlayer = false;
		int   waveEntryIndex    = -1;
		class EnemyController* controller = nullptr;
	};
	std::vector<MovingEnemy> movingEnemies_;

	std::vector<std::unique_ptr<EnemyController>> enemyControllers_;
	// イテレータ無効化防止：UpdateEnemyControllers 中の RegisterEnemyController はここに溜まり、
	// ループ終了後に enemyControllers_ にマージされる。
	std::vector<std::unique_ptr<EnemyController>> pendingEnemyControllers_;

	// 動的スプライン（プレハブ含む）
	std::vector<std::unique_ptr<SplineCurveActor>> dynamicSplines_;
};
