#include "GameScene.h"
#include "PepperMacros.h"
#include "Profiling/LoadProfiler.h"
#include "SessionLogger.h"
#include <format>
#include "Components/Gameplay.h"
#include "SoundManager.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyContext.h"
#include "Effect/EffectManager.h"
#include "Camera.h"          // 視錐台の構築に ViewProjection が要る
#include "Object3DInstance.h"
#include "ModelManager.h"
#include "AnimatedObject3DInstance.h"
// 保存/読込で全コンテナを触るため、遅延破棄（DeferDelete）に完全型が要る
#include "AnimatedModelInstance.h"
#include "SpriteInstance.h"
#include "Spline/SplineCurveActor.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include "Components/EntityTag.h"
#include "LogBuffer.h"
#include "Primitive/PrimitiveInstance.h"
#include "Primitive/DebugDraw.h"
#include "Physics/CollisionGeometry.h"
#include "Score/DifficultyProfile.h"
#include "Score/GameSession.h"
#include "MathUtility.h"
#include <algorithm>
#include <cmath>

namespace {
	// 敵を走らせるスプラインに要る制御点の数の下限
	constexpr size_t kMinSplinePointCount = 2;
	// 偏差射撃の速さを測るフレーム時間の下限 [秒]（0 割りを避ける）
	constexpr float kMinAimSampleDeltaTime = 1e-5f;
	// これより速いレールの移動はワープとみなして先読みしない [m/s]
	constexpr float kMaxPlausibleRailSpeed = 150.0f;
	// これより速い自機自身の移動はワープとみなして先読みしない [m/s]
	constexpr float kMaxPlausiblePlayerSpeed = 60.0f;
	// プレハブに bullet 指定が無いときの敵弾の速さ [m/s]
	constexpr float kDefaultEnemyBulletSpeed = 18.0f;
	// プレハブに bullet 指定が無いときの敵弾の寿命 [秒]
	constexpr float kDefaultEnemyBulletLifetime = 4.0f;
	// 距離・速さがこれ以下なら 0 とみなす（0 割りを避ける）
	constexpr float kTinyEpsilon = 1e-4f;
	// 完全に重なったときに自分をずらす量 [m]
	constexpr float kOverlapNudge = 0.01f;
	// 死亡エンティティを集める配列に先に取っておく数
	constexpr int kDeadEntityReserveCount = 8;
	// 線分の長さの 2 乗・分母がこれ以下なら 0 とみなす
	constexpr float kSegmentEpsilon = 1e-8f;
	// 弾の移動線分の長さの 2 乗がこれより小さければ動いていないとみなす
	constexpr float kSegmentLengthSqEpsilon = 1e-10f;
	// 2 次方程式の判別式 b²−4ac の係数
	constexpr float kQuadraticDiscriminantCoeff = 4.0f;
	// 向きの長さがこれ以下なら正規化しない
	constexpr float kSquaredEpsilon = 1e-6f;
	// デバッグ：プレイヤー弾の進む向きに引く線の長さ [m]
	constexpr float kBulletDebugRayLength = 5.0f;
	/// <summary>スポーンした敵・ボスの体力に純度の倍率を掛け、満タンにする（19_GameOverAndDifficulty.md §5）</summary>
	/// <param name="e">スポーンしたエンティティ（敵・ボス以外・HP なしなら何もしない）</param>
	/// <param name="difficulty">純度から決まる倍率（GameSession が持つ物）</param>
	void ApplyDifficultyHp(IImGuiEditable* e, const DifficultyProfile& difficulty) {
		if (!e) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		HP& hp = Gameplay::Of(e).GetHP();
		if (!hp.enabled) return;
		hp.maxHP = DifficultyProfile::ScaleInt(hp.maxHP,
			tag == EntityTag::Boss ? difficulty.GetBossHpScale() : difficulty.GetEnemyHpScale());
		hp.currentHP = hp.maxHP;
	}

	/// <summary>進行方向をオイラー角にする（LH系・前方+Z 前提）</summary>
	/// <param name="dir">進行方向（正規化済み）</param>
	/// <returns>(pitch, yaw, 0) [rad]</returns>
	Vector3 DirectionToEuler(const Vector3& dir) {
		const float yaw = std::atan2(dir.x, dir.z);
		const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
		const float pitch = std::atan2(-dir.y, horizLen);
		return { pitch, yaw, 0.0f };
	}
}

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

//====================
// スプライン動的管理
//====================

void GameScene::AddDynamicSpline(int tagInt, const Vector3& worldPos) {
	SplineCurveActor* spline = AddDynamicSplineActor(static_cast<EntityTag>(tagInt));
	for (auto& p : spline->MutablePoints()) {
		p.x += worldPos.x;
		p.y += worldPos.y;
		p.z += worldPos.z;
	}
}

SplineCurveActor* GameScene::AddDynamicSplineActor(EntityTag tag) {
	auto spline = std::make_unique<SplineCurveActor>();
	Gameplay::Of(spline).SetTag(tag);

	std::string base = "Spline";
	std::string name = base;
	int suffix = 1;
	while (std::any_of(dynamicSplines_.begin(), dynamicSplines_.end(),
		[&name](const std::unique_ptr<SplineCurveActor>& s) { return s->GetName() == name; })) {
		name = base + " (" + std::to_string(suffix++) + ")";
	}
	spline->SetName(name);

	dynamicSplines_.push_back(std::move(spline));
	return dynamicSplines_.back().get();
}

void GameScene::RemoveDynamicSpline(const std::string& name) {
	auto it = std::find_if(dynamicSplines_.begin(), dynamicSplines_.end(),
		[&name](const std::unique_ptr<SplineCurveActor>& s) { return s->GetName() == name; });
	if (it != dynamicSplines_.end()) {
		DeferDelete(std::move(*it));
		dynamicSplines_.erase(it);
	}
}

SplineCurveActor* GameScene::FindDynamicSplineByName(const std::string& name) {
	for (auto& s : dynamicSplines_) {
		if (s && s->GetName() == name) return s.get();
	}
	return nullptr;
}

void GameScene::DrawDynamicSplinesDebug() {
	for (const auto& sp : dynamicSplines_) {
		if (sp) sp->DrawDebug();
	}
}

//====================
// プレハブ生成
//====================

IImGuiEditable* GameScene::InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos) {
	PEPPER_SCOPE("GameScene::InstantiatePrefab");
	const PrefabDef* def = PrefabManager::GetInstance()->Find(prefabName);
	if (!def) {
		LogBuffer::GetInstance()->Add("Prefab not found: " + prefabName, LogBuffer::Level::Warning);
		return nullptr;
	}

	auto applyCollider = [&](Collider& c) {
		c.enabled = true;
		c.shape = def->colliderShape;
		c.offset = def->colliderOffset;
		c.radius = def->colliderRadius;
		c.halfExtents = def->colliderHalfExtents;
		c.capsuleRadius = def->colliderCapsuleRadius;
		c.capsuleHeight = def->colliderCapsuleHeight;
	};

	auto applyHPAndDamage = [&](IImGuiEditable* e) {
		if (!e) return;
		if (def->hasHP) {
			HP& hp = Gameplay::Of(e).GetHP();
			hp.enabled = true;
			hp.maxHP = def->maxHP;
			hp.currentHP = def->maxHP;
		}
		if (def->hasDamageDealer) {
			DamageDealer& dd = Gameplay::Of(e).GetDamageDealer();
			dd.enabled = true;
			dd.damage = def->damage;
			dd.multiplier = def->attackMultiplier;
		}
		if (def->hasAttackPower) {
			Gameplay::Of(e).SetHasAttackPower(true);
			Gameplay::Of(e).SetAttackPower(def->attackPower);
		}
		// 敵撃破スコア（タグに関わらずコピー、StagePlay 側で Enemy/Boss だけ参照する）
		Gameplay::Of(e).SetScoreValue(def->scoreValue);
		if (def->hasBullet) {
			BulletParams bp = Gameplay::Of(e).GetBulletParams();
			bp.enabled        = true;
			bp.speed          = def->bulletSpeed;
			bp.lifetime       = def->bulletLifetime;
			bp.homingStrength = def->bulletHomingStrength;
			bp.strongHomingStrength = def->bulletStrongHomingStrength;
			bp.colliderGrowth = def->bulletColliderGrowth;
			bp.penetrate            = def->bulletPenetrate;
			bp.penetrateDamageRate  = def->bulletPenetrateDamageRate;
			bp.penetrateEffect      = def->bulletPenetrateEffect;
			Gameplay::Of(e).SetBulletParams(bp);
		}
		if (def->hasMelee) {
			MeleeParams mp = Gameplay::Of(e).GetMeleeParams();
			mp.enabled         = true;
			mp.startup         = def->meleeStartup;
			mp.activeDuration  = def->meleeActiveDuration;
			mp.offset          = def->meleeOffset;
			mp.comboWindow     = def->meleeComboWindow;
			mp.recovery        = def->meleeRecovery;
			mp.cleanWindow     = def->meleeCleanWindow;
			mp.cleanMultiplier = def->meleeCleanMultiplier;
			mp.lateMultiplier  = def->meleeLateMultiplier;
			Gameplay::Of(e).SetMeleeParams(mp);
		}
		if (def->hasCarrier) {
			CarrierParams cp = Gameplay::Of(e).GetCarrierParams();
			cp.enabled           = true;
			cp.childLifetimeSec  = def->carrierChildLifetimeSec;
			cp.childWanderRadius = def->carrierChildWanderRadius;
			cp.childMoveSpeed    = def->carrierChildMoveSpeed;
			Gameplay::Of(e).SetCarrierParams(cp);
		}
		if (def->hasMovement) {
			MovementParams mv = Gameplay::Of(e).GetMovementParams();
			mv.enabled            = true;
			mv.movementType       = def->movementType;
			mv.moveSpeed          = def->moveSpeed;
			mv.hoverApproachSpeed = def->hoverApproachSpeed;
			mv.hoverHoldDuration  = def->hoverHoldDuration;
			Gameplay::Of(e).SetMovementParams(mv);
		}
		if (def->hasCharge) {
			ChargeParams chp = Gameplay::Of(e).GetChargeParams();
			chp.enabled    = true;
			chp.stage1Time = def->chargeStage1Time;
			chp.stage2Time = def->chargeStage2Time;
			chp.fireRate   = def->chargeFireRate;
			Gameplay::Of(e).SetChargeParams(chp);
		}
		if (def->hasPrecision) {
			PrecisionParams pp = Gameplay::Of(e).GetPrecisionParams();
			pp.enabled   = true;
			pp.speedAdd  = def->precisionSpeedAdd;
			pp.homingAdd = def->precisionHomingAdd;
			Gameplay::Of(e).SetPrecisionParams(pp);
		}
		if (def->hasWeapon) {
			WeaponParams wp = Gameplay::Of(e).GetWeaponParams();
			wp.enabled         = def->weaponEnabled;
			wp.bone            = def->weaponBone;
			wp.offsetTranslate = def->weaponOffsetTranslate;
			wp.offsetRotate    = def->weaponOffsetRotate;
			wp.offsetScale     = def->weaponOffsetScale;
			wp.modelDir        = def->weaponModelDir;
			wp.modelFile       = def->weaponModelFile;
			Gameplay::Of(e).SetWeaponParams(wp);
		}
		// エフェクトスロットを丸ごとコピー
		if (!def->effects.empty()) {
			Gameplay::Of(e).SetEffects(def->effects);
		}
		// 弾プレハブスロットを丸ごとコピー
		if (!def->bulletPrefabs.empty()) {
			Gameplay::Of(e).SetBulletPrefabs(def->bulletPrefabs);
		}
	};

	const std::string base = def->name.empty() ? std::string("PrefabInstance") : def->name;

	if (def->kind == PrefabKind::Primitive) {
		PrimitiveInstance::PrimitiveType primitiveType{};
		if (!PrimitiveInstance::PrimitiveTypeFromInt(def->primitiveParams.primitiveType, primitiveType)) return nullptr;
		PrimitiveInstance* back = nullptr;
		{ PEPPER_SCOPE("InstantiatePrefab::AddPrimitive"); back = AddDynamicPrimitive(primitiveType, worldPos); }

		PEPPER_SCOPE("InstantiatePrefab::UniqueName");
		back->SetName(MakeUniqueDynamicName(base, GetDynamicPrimitives(), back));
		Gameplay::Of(back).SetTag(def->tag);
		Gameplay::Of(back).SetPrefabName(prefabName);
		back->ApplyPrefabParams(def->primitiveParams);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		back->SetTranslate(worldPos);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back);
		// 定数バッファを今の位置で確定させる（次の Update まで原点に描かれないように）
		back->Update();
		return back;
	} else if (def->kind == PrefabKind::Animated || def->isAnimated) {
		AnimatedObject3DInstance* back = nullptr;
		{ PEPPER_SCOPE("InstantiatePrefab::AddAnimated"); back = AddDynamicAnimated(def->modelDir, def->modelFile, worldPos); }
		if (!back) return nullptr;

		PEPPER_SCOPE("InstantiatePrefab::UniqueName");
		back->SetName(MakeUniqueDynamicName(base, GetDynamicAnimated(), back));
		Gameplay::Of(back).SetTag(def->tag);
		Gameplay::Of(back).SetPrefabName(prefabName);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back);
		return back;
	} else {
		Object3DInstance* back = nullptr;
		{ PEPPER_SCOPE("InstantiatePrefab::AddObject3D"); back = AddDynamicObject(def->modelDir, def->modelFile, worldPos); }
		if (!back) return nullptr;

		PEPPER_SCOPE("InstantiatePrefab::UniqueName");
		back->SetName(MakeUniqueDynamicName(base, GetDynamicObjects(), back));
		Gameplay::Of(back).SetTag(def->tag);
		Gameplay::Of(back).SetPrefabName(prefabName);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back);
		return back;
	}
}

//====================
// 敵 / 敵弾 / 敵コントローラ
//====================

IImGuiEditable* GameScene::SpawnEnemyOnSpline(const std::string& prefabName,
	SplineCurveActor* spline, float speed, bool removeAtEnd,
	float initialT, int waveEntryIndex) {
	if (!spline || spline->GetPointCount() < kMinSplinePointCount) {
		LogBuffer::GetInstance()->Add(
			std::string("SpawnEnemyOnSpline: invalid spline for ") + prefabName,
			LogBuffer::Level::Warning);
		return nullptr;
	}

	const float clampedInitialT = (initialT < 0.0f) ? 0.0f : (initialT > 1.0f ? 1.0f : initialT);
	const Vector3 startPos = spline->Sample(clampedInitialT);
	IImGuiEditable* spawned = InstantiatePrefab(prefabName, startPos);

	if (!spawned) {
		LogBuffer::GetInstance()->Add(
			std::string("SpawnEnemyOnSpline: prefab not found or not instantiable: ") + prefabName,
			LogBuffer::Level::Warning);
		return nullptr;
	}
	ApplyDifficultyHp(spawned, *GetGameSession()->GetDifficultyProfile());

	MovingEnemy me{};
	me.entity         = spawned;
	me.spline         = spline;
	me.t              = clampedInitialT;
	me.speed          = speed;
	me.removeAtEnd    = removeAtEnd;
	me.waveEntryIndex = waveEntryIndex;
	movingEnemies_.push_back(me);
	return spawned;
}

void GameScene::UpdateMovingEnemies(float deltaTime) {
	PEPPER_SCOPE("GameScene::UpdateMovingEnemies");
	if (movingEnemies_.empty()) return;

	for (auto& m : movingEnemies_) {
		if (!m.entity || !m.spline) continue;
		m.t += m.speed * deltaTime;
		const float clamped = (m.t < 0.0f) ? 0.0f : (m.t > 1.0f ? 1.0f : m.t);
		const Vector3 pos = m.spline->Sample(clamped);
		if (Vector3* t = m.entity->GetEditableTranslate()) {
			*t = pos;
		}

		// ビルボード：プレイヤー方向を向く（カメラ方向ではない）
		if (m.billboardToPlayer) {
			const Vector3* selfPos = m.entity->GetEditableTranslate();
			// player_ は派生クラスが持つため、ここでは回さない。
			// ビルボード回転は UpdateEnemyControllers 内で行う（EnemyController::IsBillboardToPlayer を見る）。
		}
	}

	// 終端到達した敵を削除
	for (auto it = movingEnemies_.begin(); it != movingEnemies_.end();) {
		if (it->entity && it->spline && it->t >= 1.0f) {
			// 紐付くコントローラに終端到達を通知
			if (it->controller) it->controller->NotifySplineArrived();
			if (it->removeAtEnd) {
				DestroyDynamicEntity(it->entity);
			}
			it = movingEnemies_.erase(it);
		} else if (!it->entity) {
			// 衝突等で先に消されたケース
			it = movingEnemies_.erase(it);
		} else {
			++it;
		}
	}
}

void GameScene::UpdateEnemyControllers(float deltaTime, IImGuiEditable* player, float stageTimeSec) {
	PEPPER_SCOPE("GameScene::UpdateEnemyControllers");
	// 視錐台は毎フレーム1回だけ構築して全コントローラで使い回す（敵ごとに作り直さない）。
	// 将来フラスタムカリングを入れる時も「1回構築→多数判定」の同じ形になる。
	Frustum viewFrustum{};
	bool hasFrustum = false;
	if (Camera* cam = GetCamera()) {
		viewFrustum = Frustum::FromViewProjection(cam->GetViewProjectionMatrix());
		hasFrustum = true;
	}

	// 偏差射撃用：プレイヤーのワールド速度を「カメラ（レール）の移動」と「プレイヤー自身の移動」に分ける
	Vector3 railVel{ 0.0f, 0.0f, 0.0f };
	Vector3 ownVel{ 0.0f, 0.0f, 0.0f };
	{
		Camera* cam = GetCamera();
		const Vector3* pp = player ? player->GetEditableTranslate() : nullptr;
		if (cam && pp && deltaTime > kMinAimSampleDeltaTime) {
			const Vector3 cp = cam->GetTranslate();
			if (hasPrevAimSample_) {
				const float inv = 1.0f / deltaTime;
				railVel = { (cp.x - prevAimCameraPos_.x) * inv, (cp.y - prevAimCameraPos_.y) * inv, (cp.z - prevAimCameraPos_.z) * inv };
				ownVel = {
					(pp->x - prevAimPlayerPos_.x) * inv - railVel.x,
					(pp->y - prevAimPlayerPos_.y) * inv - railVel.y,
					(pp->z - prevAimPlayerPos_.z) * inv - railVel.z };
				// Seek やシーン切替のワープで異常値になったフレームは先読みしない
				const float railSq = railVel.x * railVel.x + railVel.y * railVel.y + railVel.z * railVel.z;
				const float ownSq = ownVel.x * ownVel.x + ownVel.y * ownVel.y + ownVel.z * ownVel.z;
				if (railSq > kMaxPlausibleRailSpeed * kMaxPlausibleRailSpeed || ownSq > kMaxPlausiblePlayerSpeed * kMaxPlausiblePlayerSpeed) {
					railVel = { 0.0f, 0.0f, 0.0f };
					ownVel = { 0.0f, 0.0f, 0.0f };
				}
			}
			prevAimCameraPos_ = cp;
			prevAimPlayerPos_ = *pp;
			hasPrevAimSample_ = true;
		}
	}

	for (auto& ctrl : enemyControllers_) {
		if (!ctrl || !ctrl->GetEntity()) continue;
		IImGuiEditable* entity = ctrl->GetEntity();

		// コンテキスト構築（in フィールド）。Wave のエントリの値はコントローラ自身が詰める
		EnemyContext ctx{};
		ctx.player          = player;
		ctx.scene           = this;
		ctx.stageTimeSec    = stageTimeSec;
		ctx.viewFrustum         = viewFrustum;
		ctx.hasViewFrustum      = hasFrustum;
		ctx.playerRailVelocity  = railVel;
		ctx.playerOwnVelocity   = ownVel;
		ctx.shotLeadRate        = enemyShotLeadRate_;

		ctrl->Update(deltaTime, ctx);

		// ビルボード回転（コントローラが自機の方を向かせている間）
		if (ctrl->IsBillboardToPlayer() && player) {
			Vector3* pos  = entity->GetEditableTranslate();
			Vector3* ppos = player->GetEditableTranslate();
			if (pos && ppos) {
				const float dx = ppos->x - pos->x;
				const float dy = ppos->y - pos->y;
				const float dz = ppos->z - pos->z;
				const float horiz = std::sqrt(dx * dx + dz * dz);
				const float yaw   = std::atan2(dx, dz);
				const float pitch = -std::atan2(dy, horiz);
				entity->SetRotate({ pitch, yaw, 0.0f });
			}
		}

		// MovingEnemy の billboardToPlayer フラグを ctrl に同期
		for (auto& m : movingEnemies_) {
			if (m.entity == entity) {
				m.billboardToPlayer = ctrl->IsBillboardToPlayer();
				break;
			}
		}

		// 自由移動（Retreat / Rush）
		if (ctrl->UsesFreeVelocity()) {
			if (Vector3* pos = entity->GetEditableTranslate()) {
				const Vector3& v = ctrl->GetFreeVelocity();
				pos->x += v.x * deltaTime;
				pos->y += v.y * deltaTime;
				pos->z += v.z * deltaTime;
			}
		}

		// スプライン切り離し
		if (ctrl->ConsumeDetachRequest()) {
			for (auto it = movingEnemies_.begin(); it != movingEnemies_.end(); ++it) {
				if (it->entity == entity) {
					it->removeAtEnd = false;
					it->speed = 0.0f; // 以降はコントローラが位置を制御
					break;
				}
			}
		}
	}

	// 完了したコントローラのエンティティを破棄
	for (auto& ctrl : enemyControllers_) {
		if (ctrl && ctrl->IsDone() && ctrl->GetEntity()) {
			DestroyDynamicEntity(ctrl->GetEntity());
			ctrl->ClearEntity();
		}
	}
	// 敵が無い（破棄済み）または全コマンド完了したコントローラを削除
	enemyControllers_.erase(
		std::remove_if(enemyControllers_.begin(), enemyControllers_.end(),
			[](const std::unique_ptr<EnemyController>& c) {
				return !c || !c->GetEntity() || c->IsDone();
			}),
		enemyControllers_.end());

	// ループ中に追加された pending を本体にマージ（ここでなら安全に push_back できる）
	if (!pendingEnemyControllers_.empty()) {
		for (auto& c : pendingEnemyControllers_) {
			enemyControllers_.push_back(std::move(c));
		}
		pendingEnemyControllers_.clear();
	}
}

Vector3 GameScene::GetShotAimPoint(IImGuiEditable* target) {
	const Vector3* tp = target ? target->GetEditableTranslate() : nullptr;
	if (!tp) return { 0.0f, 0.0f, 0.0f };
	if (Gameplay::Of(target).GetTag() != EntityTag::Player) return *tp;
	// シーンが判定の姿勢を決めていればその中心（当たり判定と同じ点を狙う）
	if (Gameplay::Of(target).HasColliderPose()) return Gameplay::Of(target).GetColliderPoseCenter();
	// オフセットは自機の回転（Object3D と同じ Rx·Ry·Rz）で回す。降下中の前傾でも見た目の体の中心に合う
	const Vector3& off = Gameplay::Of(target).GetCollider().offset;
	const Vector3* rot = static_cast<const IImGuiEditable*>(target)->GetEditableRotate();
	if (!rot) return { tp->x + off.x, tp->y + off.y, tp->z + off.z };
	const Matrix4x4 m = Multiply(MakeRotateXMatrix(*rot), Multiply(MakeRotateYMatrix(*rot), MakeRotateZMatrix(*rot)));
	return {
		tp->x + off.x * m.m[0][0] + off.y * m.m[1][0] + off.z * m.m[2][0],
		tp->y + off.x * m.m[0][1] + off.y * m.m[1][1] + off.z * m.m[2][1],
		tp->z + off.x * m.m[0][2] + off.y * m.m[1][2] + off.z * m.m[2][2] };
}

void GameScene::SpawnEnemyBullet(const Vector3& pos, const Vector3& direction,
	float speed, float lifetime, const std::string& prefabName,
	IImGuiEditable* homingTarget, float homingStrength) {
	// 引数が負数ならプレハブの "bullet" セクションから読み込む
	if (const PrefabDef* def = PrefabManager::GetInstance()->Find(prefabName)) {
		if (def->hasBullet) {
			if (speed          < 0.0f) speed          = def->bulletSpeed;
			if (lifetime       < 0.0f) lifetime       = def->bulletLifetime;
			if (homingStrength < 0.0f) homingStrength = def->bulletHomingStrength;
		}
	}
	// プレハブに bullet 指定がなければデフォルトにフォールバック
	if (speed          < 0.0f) speed          = kDefaultEnemyBulletSpeed;
	if (lifetime       < 0.0f) lifetime       = kDefaultEnemyBulletLifetime;
	if (homingStrength < 0.0f) homingStrength = 0.0f;

	PrimitiveInstance* spawned = dynamic_cast<PrimitiveInstance*>(InstantiatePrefab(prefabName, pos));
	if (!spawned) {
		LogBuffer::GetInstance()->Add(
			std::string("SpawnEnemyBullet: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	const Vector3 vel{ direction.x * speed, direction.y * speed, direction.z * speed };
	Gameplay::Of(spawned).GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
		if (!other) return;
		if (Gameplay::Of(other).GetTag() != EntityTag::Player) return;
		if (BulletRuntime* b = FindBullet(spawned)) b->remainingLifetime = -1.0f;
	};

	BulletRuntime br{};
	br.primitive         = spawned;
	br.velocity          = vel;
	br.speed             = speed;
	br.remainingLifetime = lifetime;
	br.originPos         = pos;
	br.homingTarget      = homingTarget;
	br.homingStrength    = homingStrength;
	br.baseColliderRadius = Gameplay::Of(spawned).GetCollider().radius;
	// 敵弾の尾（プレハブの trail スロット、ループ前提）。弾が消えたら Release で尾だけ残して消える
	if (const std::string trailEff = Gameplay::Of(spawned).FindEffect("trail"); !trailEff.empty()) {
		br.trailEffectHandle = EffectManager::GetInstance()->Play(trailEff, pos);
		if (speed > kTinyEpsilon) EffectManager::GetInstance()->SetRotation(br.trailEffectHandle, DirectionToEuler(direction));
	}
	bullets_.emplace(spawned, std::move(br));
}

GameScene::BulletRuntime* GameScene::FindBullet(PrimitiveInstance* primitive) {
	auto it = bullets_.find(primitive);
	return (it != bullets_.end()) ? &it->second : nullptr;
}

void GameScene::ForEachEnemyBullet(const std::function<void(PrimitiveInstance* bullet)>& fn) const {
	for (const auto& [prim, b] : bullets_) {
		if (!prim || b.remainingLifetime < 0.0f) continue;
		if (Gameplay::Of(prim).GetTag() != EntityTag::EnemyAttack) continue;
		fn(prim);
	}
}

bool GameScene::IsBullet(const PrimitiveInstance* primitive) const {
	return bullets_.count(const_cast<PrimitiveInstance*>(primitive)) != 0;
}

void GameScene::KillBullet(const IImGuiEditable* bullet) {
	if (!bullet) return;
	// 上向きのキャスト（PrimitiveInstance* → IImGuiEditable*）で比べるだけなので、消えた弾のポインタでも中身を読まない
	for (auto& [prim, b] : bullets_) {
		if (static_cast<const IImGuiEditable*>(prim) == bullet) {
			b.remainingLifetime = -1.0f;
			return;
		}
	}
}

void GameScene::KillAllEnemyBullets() {
	for (auto& [prim, b] : bullets_) {
		if (prim && Gameplay::Of(prim).GetTag() == EntityTag::EnemyAttack) b.remainingLifetime = -1.0f;
	}
}

void GameScene::ClearCombatRuntime() {
	enemyControllers_.clear();
	pendingEnemyControllers_.clear();
	movingEnemies_.clear();
	bullets_.clear();
	melees_.clear();
}

void GameScene::ForEachMovingEnemyEntry(const std::function<void(IImGuiEditable* entity, int waveEntryIndex)>& fn) const {
	for (const auto& m : movingEnemies_) {
		if (m.entity) fn(m.entity, m.waveEntryIndex);
	}
}

void GameScene::AddStationaryMovingEnemy(IImGuiEditable* entity, int waveEntryIndex) {
	MovingEnemy me{};
	me.entity         = entity;
	me.spline         = nullptr;
	me.t              = 0.0f;
	me.speed          = 0.0f;
	me.removeAtEnd    = false;
	me.waveEntryIndex = waveEntryIndex;
	movingEnemies_.push_back(me);
}

void GameScene::AttachControllerToMovingEnemy(IImGuiEditable* entity, EnemyController* ctrl) {
	for (auto& m : movingEnemies_) {
		if (m.entity == entity) {
			m.controller = ctrl;
			m.billboardToPlayer = ctrl->IsBillboardToPlayer();
			break;
		}
	}
}

EnemyController* GameScene::FindEnemyControllerByWave(int waveEntryIndex) const {
	for (const auto& ctrl : enemyControllers_) {
		if (ctrl && ctrl->GetWaveEntryIndex() == waveEntryIndex) return ctrl.get();
	}
	return nullptr;
}

void GameScene::ForEachEnemyController(const std::function<void(const EnemyController& ctrl)>& fn) const {
	for (const auto& ctrl : enemyControllers_) {
		if (ctrl) fn(*ctrl);
	}
}

IImGuiEditable* GameScene::SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) {
	IImGuiEditable* spawned = InstantiatePrefab(prefabName, pos);
	ApplyDifficultyHp(spawned, *GetGameSession()->GetDifficultyProfile());
	return spawned;
}

void GameScene::ApplyEnemyRepulsion(IImGuiEditable* self) {
	if (!self) return;
	Vector3* selfPos = self->GetEditableTranslate();
	if (!selfPos) return;
	const float selfR = Gameplay::Of(self).GetCollider().radius;

	auto repelAgainst = [&](IImGuiEditable* other) {
		if (!other || other == self) return;
		const EntityTag tag = Gameplay::Of(other).GetTag();
		if (tag != EntityTag::Enemy) return;
		Vector3* op = other->GetEditableTranslate();
		if (!op) return;
		const float otherR = Gameplay::Of(other).GetCollider().radius;
		const float minDist = selfR + otherR;
		const float dx = selfPos->x - op->x;
		const float dy = selfPos->y - op->y;
		const float dz = selfPos->z - op->z;
		float d2 = dx * dx + dy * dy + dz * dz;
		if (d2 >= minDist * minDist) return;
		float d = std::sqrt(d2);
		if (d < kTinyEpsilon) {
			// 完全重なり: 微小ランダム方向に逃がす
			selfPos->x += kOverlapNudge;
			selfPos->z += kOverlapNudge;
			return;
		}
		const float push = (minDist - d) * 0.5f; // 半分だけ自分を押す（相手は別フレームで自分を押す）
		selfPos->x += dx / d * push;
		selfPos->y += dy / d * push;
		selfPos->z += dz / d * push;
	};

	for (auto& p : GetDynamicPrimitives())  repelAgainst(p.get());
	for (auto& a : GetDynamicAnimated())    repelAgainst(a.get());
	for (auto& o : GetDynamicObjects())  repelAgainst(o.get());
}

void GameScene::RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) {
	// UpdateEnemyControllers のループ中に直接 push_back するとイテレータが無効化されるため、
	// 一旦 pending に溜めてループ終了後にマージする
	if (ctrl) pendingEnemyControllers_.push_back(std::move(ctrl));
}

//====================
// 動的エンティティ破棄（ゲーム状態の dangling 掃除 → 基底に委譲）
//====================

void GameScene::DestroyDynamicEntity(IImGuiEditable* e) {
	if (!e) return;

	// e がターゲットとして参照されている箇所を無効化（ゲーム状態）
	for (auto& m : movingEnemies_) {
		if (m.entity == e) m.entity = nullptr;
	}
	for (auto& ctrl : enemyControllers_) {
		if (ctrl && ctrl->GetEntity() == e) ctrl->ClearEntity();
	}
	for (auto& [prim, b] : bullets_) {
		if (b.homingTarget == e) b.homingTarget = nullptr;
		b.hitCooldowns.erase(e);
	}
	for (auto& m : melees_) {
		if (m.owner == e) m.owner = nullptr;
		m.hitTargets.erase(e);
	}

	// e が動的プリミティブなら、それを primitive として参照する弾・近接も掃除
	for (const auto& p : GetDynamicPrimitives()) {
		if (static_cast<IImGuiEditable*>(p.get()) == e) {
			PrimitiveInstance* prim = p.get();
			if (auto it = bullets_.find(prim); it != bullets_.end()) it->second.remainingLifetime = -1.0f;
			for (auto& m : melees_) {
				if (m.primitive == prim) { m.remainingLifetime = -1.0f; m.primitive = nullptr; }
			}
			break;
		}
	}

	// 実体の除去はエンジン基底に委譲
	Scene::DestroyDynamicEntity(e);
}

#ifdef USE_IMGUI
bool GameScene::IsDynamicObject(IImGuiEditable* editable) const {
	if (Scene::IsDynamicObject(editable)) return true;
	for (const auto& sp : dynamicSplines_) {
		if (static_cast<IImGuiEditable*>(sp.get()) == editable) return true;
	}
	return false;
}
#endif

//====================
// プレイヤー弾 / 近接
//====================

void GameScene::SpawnPlayerBullet(const Vector3& pos, const Vector3& direction,
	float speed, float lifetime, float colliderGrowthPerMeter,
	IImGuiEditable* homingTarget, float homingStrength,
	float maxTravelDistance,
	const std::string& prefabName,
	int attackPower,
	float sizeScale) {
	PrimitiveInstance* spawned = dynamic_cast<PrimitiveInstance*>(InstantiatePrefab(prefabName, pos));
	if (!spawned) {
		// プレハブが Primitive 種別でなかった、または見つからなかった
		LogBuffer::GetInstance()->Add(
			std::string("SpawnPlayerBullet: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	// velocity = direction * speed（direction は呼び出し側が正規化済みの前提）
	const Vector3 vel{ direction.x * speed, direction.y * speed, direction.z * speed };

	// 弾プレハブの貫通設定を読む（bullet の指定が無ければ PrefabDef の既定値）
	const PrefabDef* bdef = PrefabManager::GetInstance()->Find(prefabName);
	const PrefabDef& bullet = (bdef && bdef->hasBullet) ? *bdef : PrefabDef::Defaults();
	const bool        penetrate           = bullet.bulletPenetrate;
	const float       penetrateDamageRate = bullet.bulletPenetrateDamageRate;
	const std::string penetrateEffect     = bullet.bulletPenetrateEffect;

	const Vector3 bulletEuler = DirectionToEuler(direction);

	int hitDamage = 0;
	{
		// 進行方向に弾の向きを合わせる
		spawned->SetRotate(bulletEuler);
		// サイズ倍率を見た目とコライダー両方に適用（baseColliderRadius は下で倍率後の半径を拾う）
		if (sizeScale != 1.0f) {
			if (const PrefabDef* sdef = PrefabManager::GetInstance()->Find(prefabName)) {
				const Vector3& s = sdef->defaultScale;
				spawned->SetScale({ s.x * sizeScale, s.y * sizeScale, s.z * sizeScale });
			}
			Collider& bc = Gameplay::Of(spawned).GetCollider();
			bc.radius        *= sizeScale;
			bc.halfExtents.x *= sizeScale;
			bc.halfExtents.y *= sizeScale;
			bc.halfExtents.z *= sizeScale;
			bc.capsuleRadius *= sizeScale;
			bc.capsuleHeight *= sizeScale;
		}
		spawned->Update();

		// プレイヤー攻撃力 × プレハブの倍率で最終ダメージを確定（発射時に焼き込む）
		DamageDealer& dd = Gameplay::Of(spawned).GetDamageDealer();
		if (dd.enabled) {
			dd.damage = static_cast<int>(static_cast<float>(attackPower) * dd.multiplier);
			hitDamage = dd.damage;
			// CollisionManager の自動ダメージは重なっている相手すべてに入る（ボスの腕と本体に同時に重なると二重に入る）。
			// 自機の弾は onCollision で「1 発が同じ持ち主に当たるのは 1 回」に制御するので切る。
			// damage の値は残す（必殺技の吸収などが読む）
			dd.enabled = false;
		}

		// 敵に当たったときの処理。ダメージもここで入れる
		Gameplay::Of(spawned).GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
			if (!other) return;
			const EntityTag tag = Gameplay::Of(other).GetTag();
			if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
			BulletRuntime* b = FindBullet(spawned);
			if (!b) return;

			Vector3 hitPos{ 0.0f, 0.0f, 0.0f };
			if (const Vector3* t = spawned->GetEditableTranslate()) {
				hitPos = *t;
			}
			HP& hp = Gameplay::Of(other).GetHP();

			if (b->penetrate) {
				// 貫通：持ち主ごとのクールタイムが切れていればダメージ + ヒットエフェクト（弾は消えない）
				IImGuiEditable* root = Gameplay::DamageRootOf(other);
				auto it = b->hitCooldowns.find(root);
				if (it != b->hitCooldowns.end() && it->second > 0.0f) return;
				if (hp.enabled) hp.TakeDamage(b->hitDamage);
				PlayHitEffects(spawned, other, hitPos);
				b->hitCooldowns[root] = b->penetrateDamageRate;
			} else {
				// 通常弾：最初の 1 体にだけ入れて消える（同じフレームに重なった別の判定は無視）
				if (b->hasHit) return;
				b->hasHit = true;
				if (hp.enabled) hp.TakeDamage(b->hitDamage);
				PlayHitEffects(spawned, other, hitPos);
				b->remainingLifetime = -1.0f;
			}
			// HPゼロの敵は SweepDeadEntities が後で破棄する
		};
	}

	BulletRuntime br{};
	br.primitive = spawned;
	br.velocity = vel;
	br.speed = speed;
	br.remainingLifetime = lifetime;
	br.originPos = pos;
	br.baseColliderRadius = Gameplay::Of(spawned).GetCollider().radius;
	br.colliderGrowthPerMeter = colliderGrowthPerMeter;
	br.homingTarget = homingTarget;
	br.homingStrength = homingStrength;
	br.maxTravelDistance = maxTravelDistance;
	br.penetrate = penetrate;
	br.penetrateDamageRate = penetrateDamageRate;
	br.penetrateEffect = penetrateEffect;
	br.hitDamage = hitDamage;

	// 弾追従エフェクト（trail スロット、ループ前提）を再生。弾消滅時に Stop する。
	if (const std::string trailEff = Gameplay::Of(spawned).FindEffect("trail"); !trailEff.empty()) {
		br.trailEffectHandle = EffectManager::GetInstance()->Play(trailEff, pos);
		// エフェクトの向きを弾の進行方向に合わせる
		EffectManager::GetInstance()->SetRotation(br.trailEffectHandle, bulletEuler);
	}
	bullets_.emplace(spawned, std::move(br));
}

void GameScene::SpawnPlayerMelee(IImGuiEditable* owner,
	const Vector3& right, const Vector3& up, const Vector3& forward,
	const std::string& prefabName,
	int attackPower,
	const Vector3& extraWorldOffset,
	float sizeScale) {
	// 近接パラメータをプレハブから読む（melee の指定が無ければ PrefabDef の既定値で動かす）
	Vector3 baseScale{ 1.0f, 1.0f, 1.0f };
	const PrefabDef* mdef = PrefabManager::GetInstance()->Find(prefabName);
	if (mdef) baseScale = mdef->defaultScale;
	const PrefabDef& melee = (mdef && mdef->hasMelee) ? *mdef : PrefabDef::Defaults();
	const float activeDuration = melee.meleeActiveDuration;
	const Vector3 offset       = melee.meleeOffset;
	const float cleanWindow    = melee.meleeCleanWindow;
	const float cleanMul       = melee.meleeCleanMultiplier;
	const float lateMul        = melee.meleeLateMultiplier;

	// 判定オフセットを基底ベクトルでワールドへ展開（右/上/前）
	const Vector3 worldOffset{
		right.x * offset.x + up.x * offset.y + forward.x * offset.z,
		right.y * offset.x + up.y * offset.y + forward.y * offset.z,
		right.z * offset.x + up.z * offset.y + forward.z * offset.z,
	};

	// owner 相対の総オフセット = aim 基底展開 + extraWorldOffset（体の中心への持ち上げ等）。
	// UpdateMelees の毎フレーム追従も同じ値を使う必要があるため、これを MeleeRuntime に保存する。
	const Vector3 followOffset{
		worldOffset.x + extraWorldOffset.x,
		worldOffset.y + extraWorldOffset.y,
		worldOffset.z + extraWorldOffset.z,
	};

	// 初期配置位置 = owner 位置 + followOffset
	Vector3 spawnPos = followOffset;
	if (owner) {
		if (const Vector3* op = owner->GetEditableTranslate()) {
			spawnPos = { op->x + followOffset.x, op->y + followOffset.y, op->z + followOffset.z };
		}
	}

	PrimitiveInstance* spawned = dynamic_cast<PrimitiveInstance*>(InstantiatePrefab(prefabName, spawnPos));
	if (!spawned) {
		LogBuffer::GetInstance()->Add(
			std::string("SpawnPlayerMelee: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	// サイズ倍率（STG=1.0 / ボス戦=小さめ 等）を見た目とコライダー両方に適用。
	if (sizeScale != 1.0f) {
		spawned->SetScale({ baseScale.x * sizeScale, baseScale.y * sizeScale, baseScale.z * sizeScale });
		Collider& mc = Gameplay::Of(spawned).GetCollider();
		mc.radius        *= sizeScale;
		mc.halfExtents.x *= sizeScale;
		mc.halfExtents.y *= sizeScale;
		mc.halfExtents.z *= sizeScale;
		mc.capsuleRadius *= sizeScale;
		mc.capsuleHeight *= sizeScale;
	}

	// OBB / Capsule 判定が前方を向くよう、進行（aim）方向に合わせて回転
	spawned->SetRotate(DirectionToEuler(forward));
	spawned->Update(); // 初フレームの CB を確定（弾と同じく (0,0,0) 描画バグ回避）

	// CollisionManager の毎フレーム自動ダメージは使わず、onCollision で本/持続あてを手動適用
	DamageDealer& dd = Gameplay::Of(spawned).GetDamageDealer();
	dd.enabled = false;

	const int cleanDamage = static_cast<int>(static_cast<float>(attackPower) * cleanMul);
	const int lateDamage  = static_cast<int>(static_cast<float>(attackPower) * lateMul);

	// 振りエフェクト（"swing" スロット）：発生時に判定位置で再生し、aim 方向へ向ける。
	// 以降は UpdateMelees が判定に追従させ、判定の寿命切れで Stop する。
	uint64_t swingHandle = 0;
	{
		const std::string swingEffect = Gameplay::Of(spawned).FindEffect("swing");
		if (!swingEffect.empty()) {
			swingHandle = EffectManager::GetInstance()->Play(swingEffect, spawnPos);
			EffectManager::GetInstance()->SetRotation(swingHandle, DirectionToEuler(forward));
		}
	}

	// 敵に当たったときの処理。同じ敵には1回だけ、当たった瞬間の経過時間で本/持続あてを切替。
	Gameplay::Of(spawned).GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
		if (!other) return;
		const EntityTag tag = Gameplay::Of(other).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;

		for (auto& m : melees_) {
			if (m.primitive != spawned) continue;
			// この判定では既に当てた持ち主（ボスの腕と本体に同時に触れても 1 回）
			if (!m.hitTargets.insert(Gameplay::DamageRootOf(other)).second) return;

			const bool clean = (m.elapsed <= m.cleanWindow);
			if (Gameplay::Of(other).GetHP().enabled) {
				const int dmg = clean ? m.cleanDamage : m.lateDamage;
				Gameplay::Of(other).GetHP().TakeDamage(dmg);
			}
			OnPlayerMeleeHit(other, m.prefabName, clean);
			// ヒット表現：攻撃側の "hit" ＋ 被弾側（敵）の "hurt" を判定位置で両方再生
			Vector3 hitPos{ 0.0f, 0.0f, 0.0f };
			if (const Vector3* t = spawned->GetEditableTranslate()) hitPos = *t;
			PlayHitEffects(spawned, other, hitPos);
			break;
		}
		// HPゼロの敵は SweepDeadEntities が後で破棄する
	};

	MeleeRuntime mr{};
	mr.primitive = spawned;
	mr.owner = owner;
	mr.worldOffset = followOffset; // 追従にも持ち上げ分を含める（足元へ戻らないように）
	mr.remainingLifetime = activeDuration;
	mr.elapsed = 0.0f;
	mr.cleanWindow = cleanWindow;
	mr.cleanDamage = cleanDamage;
	mr.lateDamage = lateDamage;
	mr.swingEffectHandle = swingHandle;
	mr.prefabName = prefabName;
	melees_.push_back(std::move(mr));
}

void GameScene::PlayHitEffects(IImGuiEditable* attacker, IImGuiEditable* target, const Vector3& pos) {
	auto* em = EffectManager::GetInstance();
	if (!em) return;
	// 攻撃側プレハブの着弾エフェクト（"hit" スロット）
	if (attacker) {
		const std::string atkHit = Gameplay::Of(attacker).FindEffect("hit");
		if (!atkHit.empty()) em->Play(atkHit, pos);
	}
	// 被弾側プレハブの被弾エフェクト（"hurt" スロット）
	if (target) {
		const EntityTag targetTag = Gameplay::Of(target).GetTag();
		if (targetTag == EntityTag::Enemy || targetTag == EntityTag::Boss) {
			SoundManager::GetInstance()->Play2DSound("se_hit");
		}
		const std::string hurt = Gameplay::Of(target).FindEffect("hurt");
		if (!hurt.empty()) em->Play(hurt, pos);
	}
}

void GameScene::SweepDeadEntities() {
	PEPPER_SCOPE("GameScene::SweepDeadEntities");
	// 死亡エンティティを収集（破棄で配列が変わるので二段階）。Player タグは除外。
	std::vector<IImGuiEditable*> dead;
	dead.reserve(kDeadEntityReserveCount);
	auto collect = [&](IImGuiEditable* e) {
		if (!e) return;
		if (Gameplay::Of(e).GetTag() == EntityTag::Player) return;
		if (Gameplay::Of(e).GetHP().IsDead()) dead.push_back(e);
	};
	for (auto& p : GetDynamicPrimitives()) collect(p.get());
	for (auto& o : GetDynamicObjects()) collect(o.get());
	for (auto& a : GetDynamicAnimated())   collect(a.get());

	// DestroyDynamicEntity が movingEnemies_ / bullets_.homingTarget も
	// 安全にクリアしてくれるので、こちらを経由して破棄する。
	for (IImGuiEditable* e : dead) {
		const EntityTag deadTag = Gameplay::Of(e).GetTag();
		if (deadTag == EntityTag::Enemy || deadTag == EntityTag::Boss) {
			SoundManager::GetInstance()->Play2DSound("se_explode");
		}
		// 死亡エフェクト（"death" スロット）を破棄直前の位置で再生
		if (auto* em = EffectManager::GetInstance()) {
			const std::string death = Gameplay::Of(e).FindEffect("death");
			if (!death.empty()) {
				const Vector3* t = e->GetEditableTranslate();
				em->Play(death, t ? *t : Vector3{ 0.0f, 0.0f, 0.0f });
			}
		}
		DestroyDynamicEntity(e);
	}
}

namespace {
	/// <summary>
	/// 線分 p1q1 と p2q2 の最短距離を求める（Christer Ericson）。
	/// 弾の移動線分とカプセルの芯の交差に使う。
	/// </summary>
	/// <param name="p1">1 本目の始点</param>
	/// <param name="q1">1 本目の終点</param>
	/// <param name="p2">2 本目の始点</param>
	/// <param name="q2">2 本目の終点</param>
	/// <param name="s">最も近づく所の p1q1 上の割合（0..1）の書き込み先</param>
	/// <returns>最短距離の二乗</returns>
	float ClosestSegmentSegment(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, float& s) {
		const Vector3 d1{ q1.x - p1.x, q1.y - p1.y, q1.z - p1.z };
		const Vector3 d2{ q2.x - p2.x, q2.y - p2.y, q2.z - p2.z };
		const Vector3 r{ p1.x - p2.x, p1.y - p2.y, p1.z - p2.z };
		auto dot = [](const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
		const float a = dot(d1, d1);
		const float e = dot(d2, d2);
		const float f = dot(d2, r);
		float t = 0.0f;
		s = 0.0f;
		if (a <= kSegmentEpsilon && e <= kSegmentEpsilon) {
			// 両方とも点
		} else if (a <= kSegmentEpsilon) {
			t = (std::clamp)(f / e, 0.0f, 1.0f);
		} else {
			const float c = dot(d1, r);
			if (e <= kSegmentEpsilon) {
				s = (std::clamp)(-c / a, 0.0f, 1.0f);
			} else {
				const float b = dot(d1, d2);
				const float denom = a * e - b * b;
				s = denom > kSegmentEpsilon ? (std::clamp)((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) {
					t = 0.0f;
					s = (std::clamp)(-c / a, 0.0f, 1.0f);
				} else if (t > 1.0f) {
					t = 1.0f;
					s = (std::clamp)((b - c) / a, 0.0f, 1.0f);
				}
			}
		}
		const Vector3 c1{ p1.x + d1.x * s, p1.y + d1.y * s, p1.z + d1.z * s };
		const Vector3 c2{ p2.x + d2.x * t, p2.y + d2.y * t, p2.z + d2.z * t };
		const Vector3 diff{ c1.x - c2.x, c1.y - c2.y, c1.z - c2.z };
		return dot(diff, diff);
	}
}

float GameScene::FindFirstEnemyHitOnSegment(const Vector3& from, const Vector3& to, float bulletRadius) const {
	const Vector3 d{ to.x - from.x, to.y - from.y, to.z - from.z };
	const float a = d.x * d.x + d.y * d.y + d.z * d.z;
	if (a < kSegmentLengthSqEpsilon) return -1.0f;

	// 接触判定より少し内側（半径和の 90%）で交差を取る。
	// 敵の更新は弾の後に走り、同フレームで最大 ~1m 動くので、止める位置は「入った点」ではなく
	// 「線分が敵中心に最も近づく点」にして、敵が動いても重なりが外れないようにする。
	constexpr float kInsideRate = 0.9f;
	float bestEntry = -1.0f;
	float bestStop = -1.0f;

	auto test = [&](IImGuiEditable* e, const Vector3& pos) {
		if (!e) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Collider& col = Gameplay::Of(e).GetCollider();
		if (!col.enabled) return;

		// カプセル（谷の障害物の岩・梁）：弾の移動線分と芯の線分が最も近づく点で止める
		if (col.shape == ColliderShape::Capsule) {
			CollisionGeometry::WorldData w;
			if (!CollisionGeometry::TryGetWorldData(e, col, w)) return;
			const float half = col.capsuleHeight * 0.5f;
			const Vector3 a0{ w.center.x + w.axes[1].x * half, w.center.y + w.axes[1].y * half, w.center.z + w.axes[1].z * half };
			const Vector3 a1{ w.center.x - w.axes[1].x * half, w.center.y - w.axes[1].y * half, w.center.z - w.axes[1].z * half };
			const float r = (col.capsuleRadius + bulletRadius) * kInsideRate;
			float s = 0.0f;
			if (ClosestSegmentSegment(from, to, a0, a1, s) > r * r) return;
			if (bestEntry >= 0.0f && s >= bestEntry) return;
			bestEntry = s;
			bestStop = s;
			return;
		}
		if (col.shape != ColliderShape::Sphere) return;

		const Vector3 c{ pos.x + col.offset.x, pos.y + col.offset.y, pos.z + col.offset.z };
		const float r = (col.radius + bulletRadius) * kInsideRate;
		const Vector3 f{ from.x - c.x, from.y - c.y, from.z - c.z };
		const float cq = f.x * f.x + f.y * f.y + f.z * f.z - r * r;
		if (cq <= 0.0f) {
			// 始点で既に重なっている（通常判定に任せる）
			bestEntry = 0.0f;
			bestStop = 0.0f;
			return;
		}
		const float bq = 2.0f * (f.x * d.x + f.y * d.y + f.z * d.z);
		const float disc = bq * bq - kQuadraticDiscriminantCoeff * a * cq;
		if (disc < 0.0f) return;
		const float entry = (-bq - std::sqrt(disc)) / (2.0f * a);
		if (entry < 0.0f || entry > 1.0f) return;
		if (bestEntry >= 0.0f && entry >= bestEntry) return;
		const float closest = -bq / (2.0f * a);
		bestEntry = entry;
		bestStop = (std::clamp)(closest, entry, 1.0f);
	};

	for (const auto& p : GetDynamicPrimitives()) {
		if (p) if (const Vector3* pos = p->GetEditableTranslate()) test(p.get(), *pos);
	}
	for (const auto& o : GetDynamicObjects()) {
		if (o) if (const Vector3* pos = o->GetEditableTranslate()) test(o.get(), *pos);
	}
	for (const auto& an : GetDynamicAnimated()) {
		if (an) test(an.get(), an->GetTranslate());
	}
	return bestStop;
}

void GameScene::CollectEnemiesOnSegment(const Vector3& from, const Vector3& to, float radius, std::vector<PlayerBeamHit>& out) const {
	const Vector3 d{ to.x - from.x, to.y - from.y, to.z - from.z };
	const float lenSq = d.x * d.x + d.y * d.y + d.z * d.z;
	if (lenSq < kSegmentLengthSqEpsilon) return;

	auto test = [&](IImGuiEditable* e, const Vector3& pos) {
		if (!e) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Collider& col = Gameplay::Of(e).GetCollider();
		if (!col.enabled) return;
		if (Gameplay::Of(e).GetHP().IsDead()) return;

		float s = 0.0f;
		if (col.shape == ColliderShape::Capsule) {
			// カプセル：線分と芯の線分が最も近づく所
			CollisionGeometry::WorldData w;
			if (!CollisionGeometry::TryGetWorldData(e, col, w)) return;
			const float half = col.capsuleHeight * 0.5f;
			const Vector3 a0{ w.center.x + w.axes[1].x * half, w.center.y + w.axes[1].y * half, w.center.z + w.axes[1].z * half };
			const Vector3 a1{ w.center.x - w.axes[1].x * half, w.center.y - w.axes[1].y * half, w.center.z - w.axes[1].z * half };
			const float r = col.capsuleRadius + radius;
			if (ClosestSegmentSegment(from, to, a0, a1, s) > r * r) return;
		} else if (col.shape == ColliderShape::Sphere) {
			// 球：中心から線分までの距離
			const Vector3 c{ pos.x + col.offset.x, pos.y + col.offset.y, pos.z + col.offset.z };
			s = (std::clamp)(((c.x - from.x) * d.x + (c.y - from.y) * d.y + (c.z - from.z) * d.z) / lenSq, 0.0f, 1.0f);
			const Vector3 p{ from.x + d.x * s - c.x, from.y + d.y * s - c.y, from.z + d.z * s - c.z };
			const float r = col.radius + radius;
			if (p.x * p.x + p.y * p.y + p.z * p.z > r * r) return;
		} else {
			return;
		}
		PlayerBeamHit hit;
		hit.target = e;
		hit.damageRoot = Gameplay::DamageRootOf(e);
		hit.point = { from.x + d.x * s, from.y + d.y * s, from.z + d.z * s };
		out.push_back(hit);
	};

	for (const auto& p : GetDynamicPrimitives()) {
		if (p) if (const Vector3* pos = p->GetEditableTranslate()) test(p.get(), *pos);
	}
	for (const auto& o : GetDynamicObjects()) {
		if (o) if (const Vector3* pos = o->GetEditableTranslate()) test(o.get(), *pos);
	}
	for (const auto& an : GetDynamicAnimated()) {
		if (an) test(an.get(), an->GetTranslate());
	}
}

void GameScene::ApplyPlayerBeamHit(const PlayerBeamHit& hit, int damage, const std::string& hitEffect, bool firstHit) {
	if (!hit.target) return;
	HP& hp = Gameplay::Of(hit.target).GetHP();
	if (hp.enabled) hp.TakeDamage(damage);
	auto* em = EffectManager::GetInstance();
	if (em && !hitEffect.empty()) em->Play(hitEffect, hit.point);
	// 被弾エフェクトと音は当たり始めだけ（毎回出すと何十回も重なる）
	if (firstHit) PlayHitEffects(nullptr, hit.target, hit.point);
}

void GameScene::UpdateBullets(float worldDeltaTime, float playerBulletDeltaTime) {
	PEPPER_SCOPE("GameScene::UpdateBullets");
	if (bullets_.empty()) return;

	for (auto& [prim, b] : bullets_) {
		if (!b.primitive) continue;
		const float deltaTime = (playerBulletDeltaTime >= 0.0f && Gameplay::Of(b.primitive).GetTag() == EntityTag::PlayerBullet)
			? playerBulletDeltaTime : worldDeltaTime;

		// 貫通弾の多段ヒット用クールタイムを減算
		if (b.penetrate) {
			for (auto& kv : b.hitCooldowns) {
				kv.second -= deltaTime;
			}
		}

		Vector3* t = b.primitive->GetEditableTranslate();
		if (!t) {
			b.remainingLifetime -= deltaTime;
			continue;
		}

		// ----- ホーミング（target が生きていれば velocity を target 方向へ補正） -----
		// 弾速の大きさは保持し、方向だけを target に向けて指数減衰で寄せる。
		if (b.homingTarget && b.homingStrength > 0.0f && b.speed > 0.0f) {
			if (b.homingTarget->GetEditableTranslate()) {
				// 自機へ曲がる敵弾は足元でなく体の中心へ
				const Vector3 tp = GetShotAimPoint(b.homingTarget);
				const float dx = tp.x - t->x;
				const float dy = tp.y - t->y;
				const float dz = tp.z - t->z;
				const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (dist > kTinyEpsilon) {
					const float wantedX = dx / dist;
					const float wantedY = dy / dist;
					const float wantedZ = dz / dist;
					const float curX = b.velocity.x / b.speed;
					const float curY = b.velocity.y / b.speed;
					const float curZ = b.velocity.z / b.speed;
					const float alpha = 1.0f - std::exp(-b.homingStrength * deltaTime);
					float nx = curX + (wantedX - curX) * alpha;
					float ny = curY + (wantedY - curY) * alpha;
					float nz = curZ + (wantedZ - curZ) * alpha;
					const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (nlen > kSquaredEpsilon) {
						nx /= nlen; ny /= nlen; nz /= nlen;
						b.velocity = { nx * b.speed, ny * b.speed, nz * b.speed };
					}
				}
			} else {
				// target が消えた（コライダー側で destroy 等）→ ホーミング解除
				b.homingTarget = nullptr;
			}
		}

		// 移動。プレイヤー弾は高速（650m/s ≒ 11m/フレーム）で、毎フレームの球重なり判定だけだと
		// 敵をすり抜ける。今フレームの移動線分と敵の球の交差を先に調べ、最初の接触点で止める。
		// 止めた位置は球の内側なので、直後の CollisionManager が重なりを検出して通常のヒット処理が走る。
		// 貫通弾は止めない（多段ヒットはクールタイム制御のため、通過中に重なるフレームがあれば十分）。
		const Vector3 from = *t;
		Vector3 to{
			from.x + b.velocity.x * deltaTime,
			from.y + b.velocity.y * deltaTime,
			from.z + b.velocity.z * deltaTime,
		};
		if (!b.penetrate && Gameplay::Of(b.primitive).GetTag() == EntityTag::PlayerBullet) {
			const float s = FindFirstEnemyHitOnSegment(from, to, Gameplay::Of(b.primitive).GetCollider().radius);
			if (s >= 0.0f) {
				to = { from.x + (to.x - from.x) * s, from.y + (to.y - from.y) * s, from.z + (to.z - from.z) * s };
			}
		}
		*t = to;

		// 進行方向に弾とエフェクトの向きを合わせる（ホーミングで方向が変わるため毎フレーム更新）
		if (b.speed > kTinyEpsilon) {
			const Vector3 d{ b.velocity.x / b.speed, b.velocity.y / b.speed, b.velocity.z / b.speed };
			const Vector3 rot = DirectionToEuler(d);
			b.primitive->SetRotate(rot);
			if (b.trailEffectHandle != 0) {
				EffectManager::GetInstance()->SetRotation(b.trailEffectHandle, rot);
			}
#ifdef USE_IMGUI
			// デバッグ：プレイヤー弾の進行方向に線を表示（黄色）
			if (Gameplay::Of(b.primitive).GetTag() == EntityTag::PlayerBullet) {
				DebugDraw::Ray(*t, d, kBulletDebugRayLength, { 1.0f, 1.0f, 0.0f, 1.0f });
			}
#endif
		}

		// trail エフェクトを弾位置に追従
		if (b.trailEffectHandle != 0) {
			EffectManager::GetInstance()->SetPosition(b.trailEffectHandle, *t);
		}

		// 進行距離（colliderGrowth と maxTravelDistance の両方で使う）
		float traveled = 0.0f;
		if (b.colliderGrowthPerMeter > 0.0f || b.maxTravelDistance > 0.0f) {
			const float dx = t->x - b.originPos.x;
			const float dy = t->y - b.originPos.y;
			const float dz = t->z - b.originPos.z;
			traveled = std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		// 進行距離に応じて collider 半径を拡大（STG 的「遠距離ほど判定太く」）
		if (b.colliderGrowthPerMeter > 0.0f) {
			Gameplay::Of(b.primitive).GetCollider().radius =
				b.baseColliderRadius + traveled * b.colliderGrowthPerMeter;
		}

		// 最大到達距離（aim plane）を超えたら消滅
		if (b.maxTravelDistance > 0.0f && traveled >= b.maxTravelDistance) {
			b.remainingLifetime = -1.0f;
		} else {
			b.remainingLifetime -= deltaTime;
		}
	}

	// 寿命切れの弾を削除：bullets_ から外し、対応するプリミティブもシーンの一覧から外して遅延破棄する
	for (auto it = bullets_.begin(); it != bullets_.end();) {
		BulletRuntime& b = it->second;
		if (b.remainingLifetime > 0.0f && b.primitive) {
			++it;
			continue;
		}
		// 弾消滅と同時に trail エフェクトを停止
		if (b.trailEffectHandle != 0) {
			// 尾（Ribbon の Trail）は残して、古くなって消えてから終わる。頭の光はすぐ消える
			EffectManager::GetInstance()->Release(b.trailEffectHandle);
			b.trailEffectHandle = 0;
		}
		ReleaseDynamicEntity(it->first);
		it = bullets_.erase(it);
	}
}

void GameScene::UpdateMelees(float deltaTime) {
	PEPPER_SCOPE("GameScene::UpdateMelees");
	if (melees_.empty()) return;

	for (auto& m : melees_) {
		m.elapsed += deltaTime;
		m.remainingLifetime -= deltaTime;

		// owner（自機）に追従：毎フレーム owner 位置 + worldOffset へ配置（判定＋振りエフェクト）
		if (m.owner) {
			if (const Vector3* op = m.owner->GetEditableTranslate()) {
				const Vector3 pos{ op->x + m.worldOffset.x, op->y + m.worldOffset.y, op->z + m.worldOffset.z };
				if (m.primitive) {
					if (Vector3* t = m.primitive->GetEditableTranslate()) {
						t->x = pos.x; t->y = pos.y; t->z = pos.z;
					}
				}
				if (m.swingEffectHandle != 0) {
					EffectManager::GetInstance()->SetPosition(m.swingEffectHandle, pos);
				}
			}
		}
	}

	// 持続切れの判定を削除：melees_ から外し、対応するプリミティブもシーンの一覧から外して遅延破棄する
	for (auto it = melees_.begin(); it != melees_.end();) {
		if (it->remainingLifetime > 0.0f && it->primitive) {
			++it;
			continue;
		}
		// 判定の寿命切れ：振りエフェクトを停止
		if (it->swingEffectHandle != 0) {
			EffectManager::GetInstance()->Stop(it->swingEffectHandle);
			it->swingEffectHandle = 0;
		}
		ReleaseDynamicEntity(it->primitive);
		it = melees_.erase(it);
	}
}

//====================
// シーンの保存 / 読込
//   JSON の形は SceneSerializer が持ち、ここは「コンテナ ⇄ SceneData」の対応だけを持つ。
//   以前は StagePlayScene と DemoScene に丸ごと重複していて、
//   シーン JSON に何か足すたびに 2 箇所へ同じ変更を入れる必要があった。
//====================

void GameScene::ClearDynamicEntities() {
	// 破棄は次フレームに回す（コマンドリスト記録中のリソースを先に消さないため）
	ClearAllDynamicEntities();
	// 取っておいたストリーミングのエントリも配置の一部なので一緒に捨てる（読み込み済みのグループは覚えたまま）
	pendingStreamEntities_.clear();
	streamCreated_.clear();
	for (auto& sp : dynamicSplines_) {
		DeferDelete(std::move(sp));
	}
	dynamicSplines_.clear();
}

bool GameScene::ShouldSkipOnSave(const IImGuiEditable* entity, EntityTag tag) const {
	(void)entity; (void)tag;
	return false;
}

bool GameScene::ShouldSkipOnLoad(const SceneEntityDesc& desc) const {
	(void)desc;
	return false;
}

void GameScene::CollectSceneData(SceneData& out) const {
	out.sceneName = GetSceneJsonName();
	out.entities.clear();

	// プレハブ由来なら "Prefab" として出す。dir/file へ潰すと HP や弾パラメータ等の
	// プレハブ属性が往復で失われるため。
	auto fillCommon = [](SceneEntityDesc& d, const IImGuiEditable* e, EntityTag tag,
	                     const std::string& prefabName) {
		d.name = e->GetName();
		d.tag = tag;
		d.streamGroup = Gameplay::Of(e).GetStreamGroup();
		if (!prefabName.empty()) {
			d.kind = SceneEntityDesc::Kind::Prefab;
			d.prefabName = prefabName;
		}
	};

	for (const auto& o : GetDynamicObjects()) {
		if (!o) continue;
		const EntityTag tag = Gameplay::Of(o).GetTag();
		if (ShouldSkipOnSave(o.get(), tag)) continue;
		SceneEntityDesc d;
		d.kind = SceneEntityDesc::Kind::Object3D;
		fillCommon(d, o.get(), tag, Gameplay::Of(o).GetPrefabName());
		d.dir = o->GetDirectoryPath();
		d.file = o->GetModelFileName();
		d.scale = o->GetScale();
		d.rotate = o->GetRotate();
		d.translate = o->GetTranslate();
		out.entities.push_back(std::move(d));
	}

	for (const auto& a : GetDynamicAnimated()) {
		if (!a) continue;
		const EntityTag tag = Gameplay::Of(a).GetTag();
		if (ShouldSkipOnSave(a.get(), tag)) continue;
		SceneEntityDesc d;
		d.kind = SceneEntityDesc::Kind::AnimatedObject3D;
		fillCommon(d, a.get(), tag, Gameplay::Of(a).GetPrefabName());
		d.dir = a->GetDirectoryPath();
		d.file = a->GetModelFileName();
		d.scale = a->GetScale();
		d.rotate = a->GetRotate();
		d.translate = a->GetTranslate();
		out.entities.push_back(std::move(d));
	}

	for (const auto& p : GetDynamicPrimitives()) {
		if (!p) continue;
		const EntityTag tag = Gameplay::Of(p).GetTag();
		if (ShouldSkipOnSave(p.get(), tag)) continue;
		SceneEntityDesc d;
		d.kind = SceneEntityDesc::Kind::Primitive;
		fillCommon(d, p.get(), tag, Gameplay::Of(p).GetPrefabName());
		d.primitiveType = static_cast<int>(p->GetPrimitiveType());
		d.texture = p->GetTextureFilePath();
		const Transform& tr = p->GetMesh().GetTransform();
		d.scale = tr.scale;
		d.rotate = tr.rotate;
		d.translate = tr.translate;
		out.entities.push_back(std::move(d));
	}

	for (const auto& s : GetDynamicSprites()) {
		if (!s) continue;
		const EntityTag tag = Gameplay::Of(s).GetTag();
		if (ShouldSkipOnSave(s.get(), tag)) continue;
		SceneEntityDesc d;
		d.kind = SceneEntityDesc::Kind::Sprite;
		d.name = s->GetName();
		d.tag = tag;
		d.streamGroup = Gameplay::Of(s).GetStreamGroup();
		d.texture = s->GetTextureFilePath();
		d.spritePos = s->GetPosition();
		out.entities.push_back(std::move(d));
	}

	for (const auto& sp : dynamicSplines_) {
		if (!sp) continue;
		const EntityTag tag = Gameplay::Of(sp).GetTag();
		if (ShouldSkipOnSave(sp.get(), tag)) continue;
		SceneEntityDesc d;
		d.kind = SceneEntityDesc::Kind::Spline;
		d.name = sp->GetName();
		d.tag = tag;
		d.streamGroup = Gameplay::Of(sp).GetStreamGroup();
		d.points = sp->GetPoints();
		out.entities.push_back(std::move(d));
	}

	// まだ作っていないストリーミングのエントリ。書き出さないと、読む前に保存したとき配置から消える
	for (const auto& d : pendingStreamEntities_) out.entities.push_back(d);
}

void GameScene::ApplySceneData(const SceneData& data) {
	for (int i = 0; i < kSceneKindCount; ++i) { sceneKindCount_[i] = 0; sceneKindMs_[i] = 0.0; }
	for (const auto& d : data.entities) ApplySceneEntity(d);
	LogSceneEntityStats();
}

void GameScene::ApplySceneEntity(const SceneEntityDesc& d) {
	if (ShouldSkipOnLoad(d)) return;
	// 区間ごとに読む物：まだそのグループを読んでいなければ取っておく
	if (!d.streamGroup.empty() && !IsStreamGroupLoaded(d.streamGroup) && ShouldStreamOnLoad(d)) {
		pendingStreamEntities_.push_back(d);
		return;
	}

	const double entityStart = LoadProfiler::NowMs();
	const int kindIndex = static_cast<int>(d.kind);
	IImGuiEditable* created = nullptr;
	switch (d.kind) {
	case SceneEntityDesc::Kind::Object3D: {
		Object3DInstance* back = AddDynamicObject(d.dir, d.file, d.translate);
		if (!back) break;
		created = back;
		back->SetName(d.name);
		Gameplay::Of(back).SetTag(d.tag);
		back->SetScale(d.scale);
		back->SetRotate(d.rotate);
		break;
	}
	case SceneEntityDesc::Kind::AnimatedObject3D: {
		AnimatedObject3DInstance* back = AddDynamicAnimated(d.dir, d.file, d.translate);
		if (!back) break;
		created = back;
		back->SetName(d.name);
		Gameplay::Of(back).SetTag(d.tag);
		back->SetScale(d.scale);
		back->SetRotate(d.rotate);
		break;
	}
	case SceneEntityDesc::Kind::Primitive: {
		PrimitiveInstance::PrimitiveType primitiveType{};
		if (!PrimitiveInstance::PrimitiveTypeFromInt(d.primitiveType, primitiveType)) break;
		PrimitiveInstance* back = AddDynamicPrimitive(primitiveType, d.translate);
		created = back;
		back->SetName(d.name);
		Gameplay::Of(back).SetTag(d.tag);
		back->SetScale(d.scale);
		back->SetRotate(d.rotate);
		if (!d.texture.empty()) back->SetTexture(d.texture);
		break;
	}
	case SceneEntityDesc::Kind::Sprite: {
		SpriteInstance* back = AddDynamicSprite(d.texture, d.spritePos.x, d.spritePos.y);
		if (!back) break;
		created = back;
		back->SetName(d.name);
		Gameplay::Of(back).SetTag(d.tag);
		break;
	}
	case SceneEntityDesc::Kind::Spline: {
		AddDynamicSpline(static_cast<int>(d.tag));
		if (dynamicSplines_.empty()) break;
		auto& back = dynamicSplines_.back();
		created = back.get();
		back->SetName(d.name);
		back->MutablePoints().clear();
		for (const auto& p : d.points) back->AddPoint(p);
		break;
	}
	case SceneEntityDesc::Kind::Prefab: {
		// プレハブから作り直すことで HP/弾パラメータ等の属性ごと復元する。
		// タグはプレハブ定義が持つので JSON 側では上書きしない。
		if (IImGuiEditable* back = InstantiatePrefab(d.prefabName, d.translate)) {
			created = back;
			back->SetName(d.name);
			back->SetScale(d.scale);
			back->SetRotate(d.rotate);
		}
		break;
	}
	}
	if (created && !d.streamGroup.empty()) {
		Gameplay::Of(created).SetStreamGroup(d.streamGroup);
		streamCreated_.push_back({ created, d });
	}
	if (kindIndex >= 0 && kindIndex < kSceneKindCount) {
		++sceneKindCount_[kindIndex];
		sceneKindMs_[kindIndex] += LoadProfiler::NowMs() - entityStart;
	}
}

void GameScene::LogSceneEntityStats() {
	// 種類ごとの数と時間（シーン切り替えの重さの内訳。16 番 F7）
	static constexpr const char* kKindNames[kSceneKindCount] = { "object3d", "animated", "primitive", "sprite", "spline", "prefab" };
	std::string line = "LOAD kind=scene_entities";
	for (int i = 0; i < kSceneKindCount; ++i) {
		line += std::format(" {}={}/{:.2f}ms", kKindNames[i], sceneKindCount_[i], sceneKindMs_[i]);
	}
	SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info, line);
}

bool GameScene::BeginSceneLoadFromJson(const std::string& filePath) {
	pendingSceneData_ = SceneData{};
	pendingSceneIndex_ = 0;
	sceneLoadPending_ = false;
	std::string error;
	if (!SceneSerializer::ReadFile(filePath, pendingSceneData_, &error)) {
		LogBuffer::GetInstance()->Add(
			"Scene load FAILED: " + filePath + " (" + error + ")", LogBuffer::Level::Error);
		return false;
	}
	ClearDynamicEntities();
	OnBeforeSceneLoad();
	for (int i = 0; i < kSceneKindCount; ++i) { sceneKindCount_[i] = 0; sceneKindMs_[i] = 0.0; }
	sceneLoadPending_ = true;
	return true;
}

bool GameScene::ApplySceneLoadStep(double budgetMs) {
	if (!sceneLoadPending_) return true;
	const double start = LoadProfiler::NowMs();
	const auto& entities = pendingSceneData_.entities;
	while (pendingSceneIndex_ < entities.size()) {
		ApplySceneEntity(entities[pendingSceneIndex_++]);
		if (LoadProfiler::NowMs() - start >= budgetMs) break;
	}
	if (pendingSceneIndex_ < entities.size()) return false;
	LogSceneEntityStats();
	pendingSceneData_ = SceneData{};
	sceneLoadPending_ = false;
	return true;
}

size_t GameScene::LoadStreamGroup(const std::string& group) {
	PEPPER_SCOPE("GameScene::LoadStreamGroup");
	if (group.empty()) return 0;
	const double start = LoadProfiler::NowMs();
	// 先に読み込み済みにする（ApplySceneEntity が取っておかずに作るように）
	loadedStreamGroups_.insert(group);

	std::vector<SceneEntityDesc> targets;
	std::erase_if(pendingStreamEntities_, [&](SceneEntityDesc& d) {
		if (d.streamGroup != group) return false;
		targets.push_back(std::move(d));
		return true;
	});
	for (const auto& d : targets) ApplySceneEntity(d);

	SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
		std::format("LOAD kind=stream group={} count={} wall_ms={:.2f}", group, targets.size(), LoadProfiler::NowMs() - start));
	return targets.size();
}

size_t GameScene::UnloadStreamGroup(const std::string& group) {
	if (group.empty()) return 0;
	// エディタで消された物を触らないよう、まだシーンにあるかをポインタの比較だけで確かめる
	auto alive = [&](const IImGuiEditable* e) {
		auto in = [&](const auto& list) {
			return std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.get() == e; });
		};
		return in(GetDynamicObjects()) || in(GetDynamicAnimated()) || in(GetDynamicPrimitives()) ||
		       in(GetDynamicSprites()) || in(dynamicSplines_);
	};
	size_t count = 0;
	std::erase_if(streamCreated_, [&](StreamCreated& c) {
		if (c.desc.streamGroup != group) return false;
		if (alive(c.entity)) {
			DestroyDynamicEntity(c.entity);
			++count;
		}
		pendingStreamEntities_.push_back(std::move(c.desc));
		return true;
	});
	loadedStreamGroups_.erase(group);
	return count;
}

void GameScene::PreloadPrefabModels(const std::vector<std::string>& prefabs) {
	auto load = [](const PrefabDef* def) {
		if (!def || def->kind != PrefabKind::Object3D || def->modelFile.empty()) return;
		ModelManager::GetInstance()->LoadModel(def->modelDir, def->modelFile);
	};
	for (const std::string& name : prefabs) {
		const PrefabDef* def = PrefabManager::GetInstance()->Find(name);
		load(def);
		// 撃つ弾のプレハブも 1 段たどる（敵が初めて撃った時に読み込みで止まらないように）
		if (!def) continue;
		for (const auto& kv : def->bulletPrefabs) load(PrefabManager::GetInstance()->Find(kv.second));
	}
}

size_t GameScene::GetPendingStreamCount(const std::string& group) const {
	return static_cast<size_t>(std::count_if(pendingStreamEntities_.begin(), pendingStreamEntities_.end(),
		[&](const SceneEntityDesc& d) { return d.streamGroup == group; }));
}

bool GameScene::SaveSceneToJson(const std::string& filePath) {
	SceneData data;
	CollectSceneData(data);
	const bool ok = SceneSerializer::WriteFile(filePath, data);
	LogBuffer::GetInstance()->Add(
		ok ? ("Scene saved: " + filePath) : ("Scene save FAILED: " + filePath),
		ok ? LogBuffer::Level::Info : LogBuffer::Level::Error);
	return ok;
}

std::string GameScene::SerializeSceneToString() const {
	SceneData data;
	CollectSceneData(data);
	return SceneSerializer::ToString(data);
}

bool GameScene::LoadSceneFromJson(const std::string& filePath) {
	// 先に読み切る。パースに失敗した時点で return するので、
	// 書き込み途中のファイルを掴んでも現在のシーンは壊れない。
	auto* lp = LoadProfiler::GetInstance();
	SceneData data;
	std::string error;
	lp->BeginPhase("SceneJson/Read");
	const bool read = SceneSerializer::ReadFile(filePath, data, &error);
	lp->EndPhase("SceneJson/Read");
	if (!read) {
		LogBuffer::GetInstance()->Add(
			"Scene load FAILED: " + filePath + " (" + error + ")", LogBuffer::Level::Error);
		return false;
	}

	lp->BeginPhase("SceneJson/Clear");
	ClearDynamicEntities();
	OnBeforeSceneLoad();
	lp->EndPhase("SceneJson/Clear");
	lp->BeginPhase("SceneJson/Apply");
	ApplySceneData(data);
	lp->EndPhase("SceneJson/Apply");
	lp->BeginPhase("SceneJson/AfterLoad");
	OnAfterSceneLoad();
	lp->EndPhase("SceneJson/AfterLoad");

	LogBuffer::GetInstance()->Add("Scene loaded: " + filePath, LogBuffer::Level::Info);
	return true;
}
