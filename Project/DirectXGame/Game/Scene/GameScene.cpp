#include "GameScene.h"
#include "Components/Gameplay.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyContext.h"
#include "Effect/EffectManager.h"
#include "Object3DInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Spline/SplineCurveActor.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include "Components/EntityTag.h"
#include "LogBuffer.h"
#include "Primitive/PrimitiveInstance.h"
#include "Primitive/DebugDraw.h"
#include <algorithm>
#include <cmath>

namespace {
	// 進行方向（正規化済み）→ オイラー角（ラジアン）。LH系・前方+Z 前提。
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
	auto spline = std::make_unique<SplineCurveActor>();
	Gameplay::Of(spline).SetTag(static_cast<EntityTag>(tagInt));

	std::string base = "Spline";
	std::string name = base;
	int suffix = 1;
	while (std::any_of(dynamicSplines_.begin(), dynamicSplines_.end(),
		[&name](const std::unique_ptr<SplineCurveActor>& s) { return s->GetName() == name; })) {
		name = base + " (" + std::to_string(suffix++) + ")";
	}
	spline->SetName(name);

	for (auto& p : spline->MutablePoints()) {
		p.x += worldPos.x;
		p.y += worldPos.y;
		p.z += worldPos.z;
	}

	dynamicSplines_.push_back(std::move(spline));
}

void GameScene::RemoveDynamicSpline(const std::string& name) {
	auto it = std::find_if(dynamicSplines_.begin(), dynamicSplines_.end(),
		[&name](const std::unique_ptr<SplineCurveActor>& s) { return s->GetName() == name; });
	if (it != dynamicSplines_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SplineCurveActor>(it->release()));
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

void GameScene::InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos) {
	const PrefabDef* def = PrefabManager::GetInstance()->Find(prefabName);
	if (!def) {
		LogBuffer::Instance().Add("Prefab not found: " + prefabName, LogBuffer::Level::Warning);
		return;
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
			BulletParams& bp = Gameplay::Of(e).GetBulletParams();
			bp.enabled        = true;
			bp.speed          = def->bulletSpeed;
			bp.lifetime       = def->bulletLifetime;
			bp.homingStrength = def->bulletHomingStrength;
			bp.strongHomingStrength = def->bulletStrongHomingStrength;
			bp.colliderGrowth = def->bulletColliderGrowth;
			bp.penetrate            = def->bulletPenetrate;
			bp.penetrateDamageRate  = def->bulletPenetrateDamageRate;
			bp.penetrateEffect      = def->bulletPenetrateEffect;
		}
		if (def->hasMelee) {
			MeleeParams& mp = Gameplay::Of(e).GetMeleeParams();
			mp.enabled         = true;
			mp.startup         = def->meleeStartup;
			mp.activeDuration  = def->meleeActiveDuration;
			mp.offset          = def->meleeOffset;
			mp.comboWindow     = def->meleeComboWindow;
			mp.recovery        = def->meleeRecovery;
			mp.cleanWindow     = def->meleeCleanWindow;
			mp.cleanMultiplier = def->meleeCleanMultiplier;
			mp.lateMultiplier  = def->meleeLateMultiplier;
		}
		if (def->hasCarrier) {
			CarrierParams& cp = Gameplay::Of(e).GetCarrierParams();
			cp.enabled           = true;
			cp.childLifetimeSec  = def->carrierChildLifetimeSec;
			cp.childWanderRadius = def->carrierChildWanderRadius;
			cp.childMoveSpeed    = def->carrierChildMoveSpeed;
		}
		if (def->hasCharge) {
			ChargeParams& chp = Gameplay::Of(e).GetChargeParams();
			chp.enabled    = true;
			chp.stage1Time = def->chargeStage1Time;
			chp.stage2Time = def->chargeStage2Time;
			chp.fireRate   = def->chargeFireRate;
		}
		if (def->hasPrecision) {
			PrecisionParams& pp = Gameplay::Of(e).GetPrecisionParams();
			pp.enabled   = true;
			pp.speedAdd  = def->precisionSpeedAdd;
			pp.homingAdd = def->precisionHomingAdd;
		}
		// エフェクトスロットを丸ごとコピー
		if (!def->effects.empty()) {
			Gameplay::Of(e).GetEffects() = def->effects;
		}
		// 弾プレハブスロットを丸ごとコピー
		if (!def->bulletPrefabs.empty()) {
			Gameplay::Of(e).GetBulletPrefabs() = def->bulletPrefabs;
		}
	};

	const std::string base = def->name.empty() ? std::string("PrefabInstance") : def->name;

	if (def->kind == PrefabKind::Primitive) {
		AddDynamicPrimitive(def->primitiveParams.primitiveType, worldPos);
		if (dynamicPrimitives_.empty()) return;

		auto& back = dynamicPrimitives_.back();
		std::string name = base;
		int suffix = 1;
		while (std::any_of(dynamicPrimitives_.begin(), dynamicPrimitives_.end() - 1,
			[&name](const std::unique_ptr<PrimitiveInstance>& o) { return o->GetName() == name; })) {
			name = base + " (" + std::to_string(suffix++) + ")";
		}
		back->SetName(name);
		Gameplay::Of(back).SetTag(def->tag);
		back->ApplyPrefabParams(def->primitiveParams);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		back->SetTranslate(worldPos);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back.get());
	} else if (def->kind == PrefabKind::Animated || def->isAnimated) {
		AddDynamicAnimated(def->modelDir, def->modelFile, worldPos);
		if (dynamicAnimated_.empty()) return;

		auto& back = dynamicAnimated_.back();
		std::string name = base;
		int suffix = 1;
		while (std::any_of(dynamicAnimated_.begin(), dynamicAnimated_.end() - 1,
			[&name](const std::unique_ptr<AnimatedObject3DInstance>& o) { return o->GetName() == name; })) {
			name = base + " (" + std::to_string(suffix++) + ")";
		}
		back->SetName(name);
		Gameplay::Of(back).SetTag(def->tag);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back.get());
	} else {
		AddDynamicObject(def->modelDir, def->modelFile, worldPos);
		if (object3DInstances_.empty()) return;

		auto& back = object3DInstances_.back();
		std::string name = base;
		int suffix = 1;
		while (std::any_of(object3DInstances_.begin(), object3DInstances_.end() - 1,
			[&name](const std::unique_ptr<Object3DInstance>& o) { return o->GetName() == name; })) {
			name = base + " (" + std::to_string(suffix++) + ")";
		}
		back->SetName(name);
		Gameplay::Of(back).SetTag(def->tag);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(Gameplay::Of(back).GetCollider());
		applyHPAndDamage(back.get());
	}
}

//====================
// 敵 / 敵弾 / 敵コントローラ
//====================

IImGuiEditable* GameScene::SpawnEnemyOnSpline(const std::string& prefabName,
	SplineCurveActor* spline, float speed, bool removeAtEnd,
	float initialT, int waveEntryIndex) {
	if (!spline || spline->GetPointCount() < 2) {
		LogBuffer::Instance().Add(
			std::string("SpawnEnemyOnSpline: invalid spline for ") + prefabName,
			LogBuffer::Level::Warning);
		return nullptr;
	}

	// プレハブの種別を問わず、各 dynamic 配列のサイズ差分でスポーン対象を特定する
	const size_t prevPrim = dynamicPrimitives_.size();
	const size_t prevAnim = dynamicAnimated_.size();
	const size_t prevObj  = object3DInstances_.size();

	const float clampedInitialT = (initialT < 0.0f) ? 0.0f : (initialT > 1.0f ? 1.0f : initialT);
	const Vector3 startPos = spline->Sample(clampedInitialT);
	InstantiatePrefab(prefabName, startPos);

	IImGuiEditable* spawned = nullptr;
	if (dynamicPrimitives_.size() != prevPrim) {
		spawned = dynamicPrimitives_.back().get();
		// transform CB を即時反映（弾と同じ理由）
		dynamicPrimitives_.back()->Update();
	} else if (dynamicAnimated_.size() != prevAnim) {
		spawned = dynamicAnimated_.back().get();
	} else if (object3DInstances_.size() != prevObj) {
		spawned = object3DInstances_.back().get();
	}

	if (!spawned) {
		LogBuffer::Instance().Add(
			std::string("SpawnEnemyOnSpline: prefab not found or not instantiable: ") + prefabName,
			LogBuffer::Level::Warning);
		return nullptr;
	}

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
			// player_ は派生クラスが持つため、コントローラ経由で player を取得できない。
			// UpdateEnemyControllers で ctx.player を設定済みなのでここでは EnemyController の
			// billboardToPlayer_ フラグが false に上書きされている場合は処理しない。
			// ビルボード回転は UpdateEnemyControllers 内で行う。
		}
	}

	// 終端到達した敵を削除
	for (auto it = movingEnemies_.begin(); it != movingEnemies_.end();) {
		if (it->entity && it->spline && it->t >= 1.0f) {
			// 紐付くコントローラに終端到達を通知
			if (it->controller) it->controller->splineArrived_ = true;
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

void GameScene::UpdateEnemyControllers(float deltaTime, IImGuiEditable* player, float railT) {
	for (auto& ctrl : enemyControllers_) {
		if (!ctrl || !ctrl->entity_) continue;

		// コンテキスト構築（in フィールド）
		// WaveEntry の情報は EnemyController に直接持たせるか、派生クラスで設定する。
		// ここでは ctrl 自身が持つ waveEntry 情報を使うため、派生クラスが triggerT 等を設定済みの前提。
		EnemyContext ctx{};
		ctx.player          = player;
		ctx.scene           = this;
		ctx.railT           = railT;
		ctx.triggerT        = ctrl->triggerT_;
		ctx.shootIntervalT  = ctrl->shootIntervalT_;
		ctx.spawnIntervalSec = ctrl->spawnIntervalSec_;
		ctx.spawnLimit      = ctrl->spawnLimit_;
		ctx.childPrefab     = ctrl->childPrefab_;
		ctx.childSplineId   = ctrl->childSplineId_;
		ctx.splineArrived   = ctrl->splineArrived_;

		ctrl->Update(deltaTime, ctx);

		// ビルボード回転（コントローラが billboardToPlayer_=true を維持している間）
		if (ctrl->billboardToPlayer_ && player && ctrl->entity_) {
			Vector3* pos  = ctrl->entity_->GetEditableTranslate();
			Vector3* ppos = player->GetEditableTranslate();
			if (pos && ppos) {
				const float dx = ppos->x - pos->x;
				const float dy = ppos->y - pos->y;
				const float dz = ppos->z - pos->z;
				const float horiz = std::sqrt(dx * dx + dz * dz);
				const float yaw   = std::atan2(dx, dz);
				const float pitch = -std::atan2(dy, horiz);
				ctrl->entity_->SetRotate({ pitch, yaw, 0.0f });
			}
		}

		// MovingEnemy の billboardToPlayer フラグを ctrl に同期
		for (auto& m : movingEnemies_) {
			if (m.entity == ctrl->entity_) {
				m.billboardToPlayer = ctrl->billboardToPlayer_;
				break;
			}
		}

		// 自由移動（Retreat / Rush）
		if (ctrl->useFreeVelocity_ && ctrl->entity_) {
			if (Vector3* pos = ctrl->entity_->GetEditableTranslate()) {
				pos->x += ctrl->freeVelocity_.x * deltaTime;
				pos->y += ctrl->freeVelocity_.y * deltaTime;
				pos->z += ctrl->freeVelocity_.z * deltaTime;
			}
		}

		// スプライン切り離し
		if (ctrl->requestDetach_) {
			for (auto it = movingEnemies_.begin(); it != movingEnemies_.end(); ++it) {
				if (it->entity == ctrl->entity_) {
					it->removeAtEnd = false;
					it->speed = 0.0f; // 以降はコントローラが位置を制御
					break;
				}
			}
			ctrl->requestDetach_ = false;
		}
	}

	// 完了したコントローラのエンティティを破棄
	for (auto& ctrl : enemyControllers_) {
		if (ctrl && ctrl->IsDone() && ctrl->entity_) {
			DestroyDynamicEntity(ctrl->entity_);
			ctrl->entity_ = nullptr;
		}
	}
	// entity_ が null（破棄済み）または全コマンド完了したコントローラを削除
	enemyControllers_.erase(
		std::remove_if(enemyControllers_.begin(), enemyControllers_.end(),
			[](const std::unique_ptr<EnemyController>& c) {
				return !c || !c->entity_ || c->IsDone();
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
	if (speed          < 0.0f) speed          = 18.0f;
	if (lifetime       < 0.0f) lifetime       = 4.0f;
	if (homingStrength < 0.0f) homingStrength = 0.0f;

	const size_t prevCount = dynamicPrimitives_.size();
	InstantiatePrefab(prefabName, pos);
	if (dynamicPrimitives_.size() != prevCount + 1) {
		LogBuffer::Instance().Add(
			std::string("SpawnEnemyBullet: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	const Vector3 vel{ direction.x * speed, direction.y * speed, direction.z * speed };
	PrimitiveInstance* spawned = dynamicPrimitives_.back().get();
	if (spawned) {
		spawned->Update();
		Gameplay::Of(spawned).GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
			if (!other) return;
			if (Gameplay::Of(other).GetTag() != EntityTag::Player) return;
			for (auto& b : bullets_) {
				if (b.primitive == spawned) b.remainingLifetime = -1.0f;
			}
		};
	}

	BulletRuntime br{};
	br.primitive         = spawned;
	br.velocity          = vel;
	br.speed             = speed;
	br.remainingLifetime = lifetime;
	br.originPos         = pos;
	br.homingTarget      = homingTarget;
	br.homingStrength    = homingStrength;
	if (spawned) br.baseColliderRadius = Gameplay::Of(spawned).GetCollider().radius;
	bullets_.push_back(br);
}

IImGuiEditable* GameScene::SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) {
	const size_t prevPrim = dynamicPrimitives_.size();
	const size_t prevAnim = dynamicAnimated_.size();
	const size_t prevObj  = object3DInstances_.size();
	InstantiatePrefab(prefabName, pos);
	if (dynamicPrimitives_.size() != prevPrim) {
		// CB を即座にスポーン位置で確定させる（フレーム終わりに走る Update まで原点で描画されないように）
		dynamicPrimitives_.back()->Update();
		return dynamicPrimitives_.back().get();
	}
	if (dynamicAnimated_.size()   != prevAnim) return dynamicAnimated_.back().get();
	if (object3DInstances_.size() != prevObj)  return object3DInstances_.back().get();
	return nullptr;
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
		if (d < 1e-4f) {
			// 完全重なり: 微小ランダム方向に逃がす
			selfPos->x += 0.01f;
			selfPos->z += 0.01f;
			return;
		}
		const float push = (minDist - d) * 0.5f; // 半分だけ自分を押す（相手は別フレームで自分を押す）
		selfPos->x += dx / d * push;
		selfPos->y += dy / d * push;
		selfPos->z += dz / d * push;
	};

	for (auto& p : dynamicPrimitives_)  repelAgainst(p.get());
	for (auto& a : dynamicAnimated_)    repelAgainst(a.get());
	for (auto& o : object3DInstances_)  repelAgainst(o.get());
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
		if (ctrl && ctrl->entity_ == e) ctrl->entity_ = nullptr;
	}
	for (auto& b : bullets_) {
		if (b.homingTarget == e) b.homingTarget = nullptr;
		if (b.penetrate) b.hitCooldowns.erase(e);
	}
	for (auto& m : melees_) {
		if (m.owner == e) m.owner = nullptr;
		m.hitTargets.erase(e);
	}

	// e が動的プリミティブなら、それを primitive として参照する弾・近接も掃除
	for (const auto& p : dynamicPrimitives_) {
		if (static_cast<IImGuiEditable*>(p.get()) == e) {
			PrimitiveInstance* prim = p.get();
			for (auto& b : bullets_) {
				if (b.primitive == prim) b.remainingLifetime = -1.0f;
			}
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
	int attackPower) {
	const size_t prevCount = dynamicPrimitives_.size();
	InstantiatePrefab(prefabName, pos);
	if (dynamicPrimitives_.size() != prevCount + 1) {
		// プレハブが Primitive 種別でなかった、または見つからなかった
		LogBuffer::Instance().Add(
			std::string("SpawnPlayerBullet: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	// velocity = direction * speed（direction は呼び出し側が正規化済みの前提）
	const Vector3 vel{ direction.x * speed, direction.y * speed, direction.z * speed };

	// 弾プレハブの貫通設定を読む
	bool        penetrate = false;
	float       penetrateDamageRate = 0.2f;
	std::string penetrateEffect;
	if (const PrefabDef* bdef = PrefabManager::GetInstance()->Find(prefabName); bdef && bdef->hasBullet) {
		penetrate           = bdef->bulletPenetrate;
		penetrateDamageRate = bdef->bulletPenetrateDamageRate;
		penetrateEffect     = bdef->bulletPenetrateEffect;
	}

	const Vector3 bulletEuler = DirectionToEuler(direction);

	int penetrateDamage = 0;
	PrimitiveInstance* spawned = dynamicPrimitives_.back().get();
	if (spawned) {
		// 進行方向に弾の向きを合わせる
		spawned->SetRotate(bulletEuler);
		spawned->Update();

		// プレイヤー攻撃力 × プレハブの倍率で最終ダメージを確定（発射時に焼き込む）
		DamageDealer& dd = Gameplay::Of(spawned).GetDamageDealer();
		if (dd.enabled) {
			dd.damage = static_cast<int>(static_cast<float>(attackPower) * dd.multiplier);
			penetrateDamage = dd.damage;
			// 貫通弾は CollisionManager の毎フレーム自動ダメージを無効化し、onCollision で damageRate 制御する
			if (penetrate) {
				dd.enabled = false;
			}
		}

		// 敵に当たったときの処理。実ダメージ適用は通常弾=CollisionManager / 貫通弾=ここで手動。
		Gameplay::Of(spawned).GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
			if (!other) return;
			const EntityTag tag = Gameplay::Of(other).GetTag();
			if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;

			Vector3 hitPos{ 0.0f, 0.0f, 0.0f };
			if (const Vector3* t = spawned->GetEditableTranslate()) {
				hitPos = *t;
			}

			for (auto& b : bullets_) {
				if (b.primitive != spawned) continue;
				if (b.penetrate) {
					// 貫通：damageRate クールタイムが切れていればダメージ + 専用エフェクト
					auto it = b.hitCooldowns.find(other);
					const float cd = (it != b.hitCooldowns.end()) ? it->second : 0.0f;
					if (cd <= 0.0f) {
						if (Gameplay::Of(other).GetHP().enabled) {
							Gameplay::Of(other).GetHP().TakeDamage(b.penetrateDamage);
						}
						if (!b.penetrateEffect.empty()) {
							EffectManager::GetInstance()->Play(b.penetrateEffect, hitPos);
						}
						b.hitCooldowns[other] = b.penetrateDamageRate;
					}
					// 貫通弾は消えない
				} else {
					// 通常弾：ヒットエフェクト発火 + 死亡フラグ（実ダメージは CollisionManager 側）
					EffectManager::GetInstance()->Play("Hit_Small", hitPos);
					b.remainingLifetime = -1.0f;
				}
				break;
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
	br.baseColliderRadius = spawned ? Gameplay::Of(spawned).GetCollider().radius : 0.0f;
	br.colliderGrowthPerMeter = colliderGrowthPerMeter;
	br.homingTarget = homingTarget;
	br.homingStrength = homingStrength;
	br.maxTravelDistance = maxTravelDistance;
	br.penetrate = penetrate;
	br.penetrateDamageRate = penetrateDamageRate;
	br.penetrateEffect = penetrateEffect;
	br.penetrateDamage = penetrateDamage;

	// 弾追従エフェクト（trail スロット、ループ前提）を再生。弾消滅時に Stop する。
	if (spawned) {
		const std::string trailEff = Gameplay::Of(spawned).FindEffect("trail");
		if (!trailEff.empty()) {
			br.trailEffectHandle = EffectManager::GetInstance()->Play(trailEff, pos);
			// エフェクトの向きを弾の進行方向に合わせる
			EffectManager::GetInstance()->SetRotation(br.trailEffectHandle, bulletEuler);
		}
	}
	bullets_.push_back(br);
}

void GameScene::SpawnPlayerMelee(IImGuiEditable* owner,
	const Vector3& right, const Vector3& up, const Vector3& forward,
	const std::string& prefabName,
	int attackPower) {
	// 近接パラメータをプレハブから読む（無ければデフォルトで動かす）
	float activeDuration = 0.20f;
	Vector3 offset{ 0.0f, 0.0f, 2.0f };
	float cleanWindow = 0.08f;
	float cleanMul = 1.5f;
	float lateMul = 0.7f;
	if (const PrefabDef* mdef = PrefabManager::GetInstance()->Find(prefabName); mdef && mdef->hasMelee) {
		activeDuration = mdef->meleeActiveDuration;
		offset         = mdef->meleeOffset;
		cleanWindow    = mdef->meleeCleanWindow;
		cleanMul       = mdef->meleeCleanMultiplier;
		lateMul        = mdef->meleeLateMultiplier;
	}

	// 判定オフセットを基底ベクトルでワールドへ展開（右/上/前）
	const Vector3 worldOffset{
		right.x * offset.x + up.x * offset.y + forward.x * offset.z,
		right.y * offset.x + up.y * offset.y + forward.y * offset.z,
		right.z * offset.x + up.z * offset.y + forward.z * offset.z,
	};

	// 初期配置位置 = owner 位置 + worldOffset
	Vector3 spawnPos = worldOffset;
	if (owner) {
		if (const Vector3* op = owner->GetEditableTranslate()) {
			spawnPos = { op->x + worldOffset.x, op->y + worldOffset.y, op->z + worldOffset.z };
		}
	}

	const size_t prevCount = dynamicPrimitives_.size();
	InstantiatePrefab(prefabName, spawnPos);
	if (dynamicPrimitives_.size() != prevCount + 1) {
		LogBuffer::Instance().Add(
			std::string("SpawnPlayerMelee: prefab not spawned as primitive: ") + prefabName,
			LogBuffer::Level::Warning);
		return;
	}

	PrimitiveInstance* spawned = dynamicPrimitives_.back().get();
	if (!spawned) return;

	// OBB / Capsule 判定が前方を向くよう、進行（aim）方向に合わせて回転
	spawned->SetRotate(DirectionToEuler(forward));
	spawned->Update(); // 初フレームの CB を確定（弾と同じく (0,0,0) 描画バグ回避）

	// CollisionManager の毎フレーム自動ダメージは使わず、onCollision で本/持続あてを手動適用
	DamageDealer& dd = Gameplay::Of(spawned).GetDamageDealer();
	dd.enabled = false;

	const int cleanDamage = static_cast<int>(static_cast<float>(attackPower) * cleanMul);
	const int lateDamage  = static_cast<int>(static_cast<float>(attackPower) * lateMul);
	const std::string hitEffect = Gameplay::Of(spawned).FindEffect("hit");

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
			if (m.hitTargets.count(other)) return; // この判定では既に当てた敵
			m.hitTargets.insert(other);

			if (Gameplay::Of(other).GetHP().enabled) {
				const int dmg = (m.elapsed <= m.cleanWindow) ? m.cleanDamage : m.lateDamage;
				Gameplay::Of(other).GetHP().TakeDamage(dmg);
			}
			if (!m.hitEffect.empty()) {
				Vector3 hitPos{ 0.0f, 0.0f, 0.0f };
				if (const Vector3* t = spawned->GetEditableTranslate()) hitPos = *t;
				EffectManager::GetInstance()->Play(m.hitEffect, hitPos);
			}
			break;
		}
		// HPゼロの敵は SweepDeadEntities が後で破棄する
	};

	MeleeRuntime mr{};
	mr.primitive = spawned;
	mr.owner = owner;
	mr.worldOffset = worldOffset;
	mr.remainingLifetime = activeDuration;
	mr.elapsed = 0.0f;
	mr.cleanWindow = cleanWindow;
	mr.cleanDamage = cleanDamage;
	mr.lateDamage = lateDamage;
	mr.hitEffect = hitEffect;
	mr.swingEffectHandle = swingHandle;
	melees_.push_back(std::move(mr));
}

void GameScene::SweepDeadEntities() {
	// 死亡エンティティを収集（破棄で配列が変わるので二段階）。Player タグは除外。
	std::vector<IImGuiEditable*> dead;
	dead.reserve(8);
	auto collect = [&](IImGuiEditable* e) {
		if (!e) return;
		if (Gameplay::Of(e).GetTag() == EntityTag::Player) return;
		if (Gameplay::Of(e).GetHP().IsDead()) dead.push_back(e);
	};
	for (auto& p : dynamicPrimitives_) collect(p.get());
	for (auto& o : object3DInstances_) collect(o.get());
	for (auto& a : dynamicAnimated_)   collect(a.get());

	// DestroyDynamicEntity が movingEnemies_ / bullets_.homingTarget も
	// 安全にクリアしてくれるので、こちらを経由して破棄する。
	for (IImGuiEditable* e : dead) {
		DestroyDynamicEntity(e);
	}
}

void GameScene::UpdateBullets(float deltaTime) {
	if (bullets_.empty()) return;

	for (auto& b : bullets_) {
		if (!b.primitive) continue;

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
			const Vector3* tp = b.homingTarget->GetEditableTranslate();
			if (tp) {
				const float dx = tp->x - t->x;
				const float dy = tp->y - t->y;
				const float dz = tp->z - t->z;
				const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (dist > 1e-4f) {
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
					if (nlen > 1e-6f) {
						nx /= nlen; ny /= nlen; nz /= nlen;
						b.velocity = { nx * b.speed, ny * b.speed, nz * b.speed };
					}
				}
			} else {
				// target が消えた（コライダー側で destroy 等）→ ホーミング解除
				b.homingTarget = nullptr;
			}
		}

		// 移動
		t->x += b.velocity.x * deltaTime;
		t->y += b.velocity.y * deltaTime;
		t->z += b.velocity.z * deltaTime;

		// 進行方向に弾とエフェクトの向きを合わせる（ホーミングで方向が変わるため毎フレーム更新）
		if (b.speed > 1e-4f) {
			const Vector3 d{ b.velocity.x / b.speed, b.velocity.y / b.speed, b.velocity.z / b.speed };
			const Vector3 rot = DirectionToEuler(d);
			b.primitive->SetRotate(rot);
			if (b.trailEffectHandle != 0) {
				EffectManager::GetInstance()->SetRotation(b.trailEffectHandle, rot);
			}
#ifdef USE_IMGUI
			// デバッグ：プレイヤー弾の進行方向に線を表示（黄色）
			if (Gameplay::Of(b.primitive).GetTag() == EntityTag::PlayerBullet) {
				DebugDraw::Ray(*t, d, 5.0f, { 1.0f, 1.0f, 0.0f, 1.0f });
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

	// 寿命切れの弾を削除：bullets_ から外し、対応する dynamicPrimitives_ も deferredDeletes_ へ
	for (auto it = bullets_.begin(); it != bullets_.end();) {
		if (it->remainingLifetime > 0.0f && it->primitive) {
			++it;
			continue;
		}
		// 弾消滅と同時に trail エフェクトを停止
		if (it->trailEffectHandle != 0) {
			EffectManager::GetInstance()->Stop(it->trailEffectHandle);
			it->trailEffectHandle = 0;
		}
		// 該当 primitive を dynamicPrimitives_ から取り出す
		PrimitiveInstance* dead = it->primitive;
		auto pit = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
			[dead](const std::unique_ptr<PrimitiveInstance>& p) { return p.get() == dead; });
		if (pit != dynamicPrimitives_.end()) {
			deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(pit->release()));
			dynamicPrimitives_.erase(pit);
		}
		it = bullets_.erase(it);
	}
}

void GameScene::UpdateMelees(float deltaTime) {
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

	// 持続切れの判定を削除：melees_ から外し、対応する dynamicPrimitives_ も deferredDeletes_ へ
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
		PrimitiveInstance* dead = it->primitive;
		if (dead) {
			auto pit = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
				[dead](const std::unique_ptr<PrimitiveInstance>& p) { return p.get() == dead; });
			if (pit != dynamicPrimitives_.end()) {
				deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(pit->release()));
				dynamicPrimitives_.erase(pit);
			}
		}
		it = melees_.erase(it);
	}
}
