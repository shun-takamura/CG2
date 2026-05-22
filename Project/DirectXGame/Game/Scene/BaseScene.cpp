#include "BaseScene.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyContext.h"
#include "Game.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "ModelManager.h"
#include "Object3DInstance.h"
#include "SpriteInstance.h"
#include "AnimatedObject3DInstance.h"
#include "AnimatedModelInstance.h"
#include "Spline/SplineCurveActor.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include "Components/EntityTag.h"
#include "LogBuffer.h"
#include "DirectXCore.h"
#include "Primitive/PrimitiveInstance.h"
#include "CameraPreviewSprite.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "Camera.h"
#include "DebugCamera.h"
#include "LineRenderer.h"
#include "Primitive/DebugDraw.h"
#include "ImGuiManager.h"
#include "ViewportWindow.h"
#include "MathUtility.h"
#include "Transform.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

BaseScene::BaseScene() = default;

BaseScene::~BaseScene() {
	// QRをONのまま破棄されたら検出状態を残さない
	if (useQRCodeReader_) {
		QRCodeReader::GetInstance()->Reset();
	}
}

//====================
// デバッグカメラ
//====================

DebugCamera* BaseScene::GetDebugCamera() {
	if (!debugCamera_) {
		debugCamera_ = std::make_unique<DebugCamera>();
		debugCamera_->Initialize();
	}
	return debugCamera_.get();
}

void BaseScene::AddHighlight(IImGuiEditable* e) {
	if (!e) return;
	if (std::find(highlightedEntities_.begin(), highlightedEntities_.end(), e)
		!= highlightedEntities_.end()) return;
	highlightedEntities_.push_back(e);
}

void BaseScene::RemoveHighlight(IImGuiEditable* e) {
	auto it = std::find(highlightedEntities_.begin(), highlightedEntities_.end(), e);
	if (it != highlightedEntities_.end()) highlightedEntities_.erase(it);
}

void BaseScene::ClearHighlights() {
	highlightedEntities_.clear();
}

void BaseScene::RunIdPass(ID3D12GraphicsCommandList* commandList) {
	(void)commandList;
	if (highlightedEntities_.empty()) return;

	for (IImGuiEditable* e : highlightedEntities_) {
		if (!e) continue;
		// Object3D
		for (const auto& obj : object3DInstances_) {
			if (static_cast<IImGuiEditable*>(obj.get()) == e) {
				obj->DrawIdPass(dxCore_);
				goto next;
			}
		}
		// Animated
		for (const auto& a : dynamicAnimated_) {
			if (static_cast<IImGuiEditable*>(a.get()) == e) {
				a->DrawIdPass(dxCore_);
				goto next;
			}
		}
		// Primitive
		for (const auto& p : dynamicPrimitives_) {
			if (static_cast<IImGuiEditable*>(p.get()) == e) {
				p->DrawIdPass();
				goto next;
			}
		}
	next: ;
	}
}

void BaseScene::UpdateGlobalEffects(Camera* camera, float deltaTime) {
	EffectManager::GetInstance()->SetCamera(camera);
	EffectManager::GetInstance()->Update(deltaTime);
	if (auto* gpu = Game::GetGPUParticleManager()) {
		gpu->Update(camera, deltaTime);
	}
}

void BaseScene::DrawGlobalEffects() {
	if (auto* gpu = Game::GetGPUParticleManager()) {
		gpu->Draw();
	}
	EffectManager::GetInstance()->Draw();
}

void BaseScene::UpdateFrozenEffects() {
	// 実時間 delta（タイムスケール非適用）で進める。
	// ヒットストップ等でシーンが 0 倍速でも、エディタのプレビューは等速で動く。
	const float dt = dxCore_ ? dxCore_->GetDeltaTime() : (1.0f / 60.0f);
	UpdateGlobalEffects(GetCamera(), dt);
}

void BaseScene::SetUseDebugCamera(bool enabled) {
	if (enabled == useDebugCamera_) return;
	Camera* sc = GetCamera();
	if (!sc) {
		useDebugCamera_ = enabled;
		return;
	}

	if (enabled) {
		// 現在のシーンカメラ状態をスナップ
		sceneCameraSnapshot_.translate   = sc->GetTranslate();
		sceneCameraSnapshot_.rotate      = sc->GetRotate();
		sceneCameraSnapshot_.fovY        = sc->GetFovY();
		sceneCameraSnapshot_.aspectRatio = sc->GetAspectRatio();
		sceneCameraSnapshot_.nearClip    = sc->GetNearClip();
		sceneCameraSnapshot_.farClip     = sc->GetFarClip();

		// デバッグカメラの初期配置：シーンカメラ周辺に置く
		GetDebugCamera(); // 生成
		debugCamera_->SetPivot({ 0.0f, 0.0f, 0.0f });
		// シーンカメラからピボット(原点)までの距離を初期distanceに
		const Vector3& cp = sceneCameraSnapshot_.translate;
		float dist = std::sqrt(cp.x * cp.x + cp.y * cp.y + cp.z * cp.z);
		if (dist < 1.0f) dist = 10.0f;
		debugCamera_->SetDistance(dist);
		debugCamera_->SetFovY(sceneCameraSnapshot_.fovY);
		debugCamera_->SetAspectRatio(sceneCameraSnapshot_.aspectRatio);
		debugCamera_->SetNearFar(sceneCameraSnapshot_.nearClip, sceneCameraSnapshot_.farClip);
		debugCamera_->Update();
	} else {
		// シーンカメラのtransformを復元してUpdateさせる
		sc->SetTranslate(sceneCameraSnapshot_.translate);
		sc->SetRotate(sceneCameraSnapshot_.rotate);
		sc->Update();
	}
	useDebugCamera_ = enabled;
}

void BaseScene::UpdateDebugCameraIfActive() {
	if (!useDebugCamera_) return;
	Camera* sc = GetCamera();
	if (!sc || !debugCamera_) return;

#ifdef USE_IMGUI
	// Scene ビューポートにマウスがホバーしているときだけ入力を受け取る
	auto* vp = ImGuiManager::Instance().GetViewportWindow();
	bool acceptInput = vp && vp->IsHovered();
	if (acceptInput) {
		ImGuiIO& io = ImGui::GetIO();
		const float orbitSpeed = 0.005f;
		const float panSpeed   = 0.01f;
		const float zoomSpeed  = 1.0f;
		// ギズモのハンドルをドラッグ中はカメラ回転を抑止する
		const bool gizmoDragging = ImGuiManager::Instance().GetGizmo().IsDragging();
		if (!gizmoDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			ImVec2 d = io.MouseDelta;
			debugCamera_->Orbit(d.x * orbitSpeed, d.y * orbitSpeed);
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
			ImVec2 d = io.MouseDelta;
			debugCamera_->Pan(-d.x * panSpeed, d.y * panSpeed);
		}
		if (io.MouseWheel != 0.0f) {
			debugCamera_->Zoom(-io.MouseWheel * zoomSpeed);
		}
	}
#endif

	debugCamera_->Update();
	// シーンカメラに行列を注入（subsystemsはこのカメラを参照しているため、すべてデバッグ視点になる）
	sc->SetExternalMatrices(
		debugCamera_->GetViewMatrix(),
		debugCamera_->GetProjectionMatrix(),
		debugCamera_->GetTranslate());
}

void BaseScene::DrawSceneCameraGizmo() {
	if (!useDebugCamera_) return;

	LineRenderer* lr = LineRenderer::GetInstance();
	if (!lr) return;

	// スナップしたシーンカメラの world matrix を構築
	Transform t{};
	t.scale     = { 1.0f, 1.0f, 1.0f };
	t.rotate    = sceneCameraSnapshot_.rotate;
	t.translate = sceneCameraSnapshot_.translate;
	Matrix4x4 worldMat = MakeAffineMatrix(t);

	// 視錐台のローカル座標を構築（カメラ前方を +Z とする LH 系前提）
	const float nz = sceneCameraSnapshot_.nearClip;
	const float fz = sceneCameraSnapshot_.farClip;
	const float fovY = sceneCameraSnapshot_.fovY;
	const float aspect = sceneCameraSnapshot_.aspectRatio;
	// 遠端は実値だと長すぎるので、表示用に縮める
	const float displayFar = std::min(fz, nz + 4.0f);
	const float nh = nz * std::tan(fovY * 0.5f);
	const float nw = nh * aspect;
	const float fh = displayFar * std::tan(fovY * 0.5f);
	const float fw = fh * aspect;

	Vector3 nearLocal[4] = {
		{ -nw, -nh, nz },
		{  nw, -nh, nz },
		{  nw,  nh, nz },
		{ -nw,  nh, nz },
	};
	Vector3 farLocal[4] = {
		{ -fw, -fh, displayFar },
		{  fw, -fh, displayFar },
		{  fw,  fh, displayFar },
		{ -fw,  fh, displayFar },
	};

	auto toWorld = [&](const Vector3& p) {
		return TransformCoordinate(p, worldMat);
		};

	Vector3 nearWorld[4], farWorld[4];
	for (int i = 0; i < 4; ++i) {
		nearWorld[i] = toWorld(nearLocal[i]);
		farWorld[i]  = toWorld(farLocal[i]);
	}
	Vector3 apex = sceneCameraSnapshot_.translate;

	const Vector4 colFrustum = { 1.0f, 0.7f, 0.2f, 1.0f };
	const Vector4 colUp      = { 0.3f, 1.0f, 0.3f, 1.0f };

	// 近平面の四角形
	for (int i = 0; i < 4; ++i) {
		lr->AddLine(nearWorld[i], nearWorld[(i + 1) % 4], colFrustum);
	}
	// 遠平面の四角形
	for (int i = 0; i < 4; ++i) {
		lr->AddLine(farWorld[i], farWorld[(i + 1) % 4], colFrustum);
	}
	// 近⇔遠を接続
	for (int i = 0; i < 4; ++i) {
		lr->AddLine(nearWorld[i], farWorld[i], colFrustum);
	}
	// カメラ位置から近平面の四隅へ
	for (int i = 0; i < 4; ++i) {
		lr->AddLine(apex, nearWorld[i], colFrustum);
	}
	// 上方向マーカー（近平面上辺の中央から少し上に三角形を作る）
	Vector3 topMid = {
		(nearWorld[2].x + nearWorld[3].x) * 0.5f,
		(nearWorld[2].y + nearWorld[3].y) * 0.5f,
		(nearWorld[2].z + nearWorld[3].z) * 0.5f,
	};
	Vector3 upTip = toWorld({ 0.0f, nh * 1.5f, nz });
	lr->AddLine(nearWorld[3], upTip, colUp);
	lr->AddLine(nearWorld[2], upTip, colUp);
	lr->AddLine(topMid, upTip, colUp);
}

void BaseScene::ProcessAsyncLoads()
{
	// 前フレームで Remove したオブジェクトを破棄
	// （ここに来た時点で前フレームの GPU 処理は完了している）
	deferredDeletes_.clear();

	if (!dxCore_) return;
	ModelManager::GetInstance()->FlushGPUUpload(dxCore_, 1);
}

float BaseScene::GetScaledDeltaTime() const
{
	if (!dxCore_) return 0.0f;
	return dxCore_->GetScaledDeltaTime() * timeScales_[static_cast<int>(TimeGroup::World)];
}

float BaseScene::GetScaledDeltaTime(TimeGroup g) const
{
	if (!dxCore_) return 0.0f;
	int i = static_cast<int>(g);
	if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return dxCore_->GetScaledDeltaTime();
	return dxCore_->GetScaledDeltaTime() * timeScales_[i];
}

// ====================================================================
// 動的エンティティ操作のデフォルト実装（受け付けるシーンが override する）
// ヘッダ内インラインだと TU 間で COMDAT 衝突を起こすケースがあるので cpp に置く
// ====================================================================

void BaseScene::AddDynamicObject(const std::string& dirPath, const std::string& filename,
	const Vector3& worldPos) {
	auto obj = std::make_unique<Object3DInstance>();
	obj->Initialize(object3DManager_, dxCore_, dirPath, filename);
	obj->SetCamera(GetCamera());
	obj->SetTranslate(worldPos);
	object3DInstances_.push_back(std::move(obj));
}

void BaseScene::RemoveDynamicObject(const std::string& name) {
	auto it = std::find_if(object3DInstances_.begin(), object3DInstances_.end(),
		[&name](const std::unique_ptr<Object3DInstance>& o) { return o->GetName() == name; });
	if (it != object3DInstances_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(it->release()));
		object3DInstances_.erase(it);
	}
}

void BaseScene::AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) {
	auto sprite = std::make_unique<SpriteInstance>();
	sprite->Initialize(spriteManager_, texturePath);
	sprite->SetPosition({ clientX, clientY });
	dynamicSprites_.push_back(std::move(sprite));
}

void BaseScene::RemoveDynamicSprite(const std::string& name) {
	auto it = std::find_if(dynamicSprites_.begin(), dynamicSprites_.end(),
		[&name](const std::unique_ptr<SpriteInstance>& s) { return s->GetName() == name; });
	if (it != dynamicSprites_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(it->release()));
		dynamicSprites_.erase(it);
	}
}

void BaseScene::AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
	const Vector3& worldPos) {
	auto model = std::make_unique<AnimatedModelInstance>();
	model->Initialize(ModelManager::GetInstance()->GetModelCore(), dirPath, filename);

	auto obj = std::make_unique<AnimatedObject3DInstance>();
	obj->Initialize(object3DManager_, skinningComputeManager_, dxCore_, srvManager_,
		model.get(), filename);
	obj->SetSourcePath(dirPath, filename);
	obj->SetTranslate(worldPos);

	dynamicAnimatedModels_.push_back(std::move(model));
	dynamicAnimated_.push_back(std::move(obj));
}

void BaseScene::RemoveDynamicAnimated(const std::string& name) {
	auto it = std::find_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
		[&name](const std::unique_ptr<AnimatedObject3DInstance>& a) { return a->GetName() == name; });
	if (it != dynamicAnimated_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(it->release()));
		dynamicAnimated_.erase(it);
	}
}

void BaseScene::AddDynamicPrimitive(int primitiveType, const Vector3& worldPos) {
	const int kCount = static_cast<int>(PrimitiveInstance::PrimitiveType::kCount);
	if (primitiveType < 0 || primitiveType >= kCount) return;

	const auto type = static_cast<PrimitiveInstance::PrimitiveType>(primitiveType);

	// 同名衝突を避けて連番を付ける（例: "Box (1)"）
	const char* baseName = PrimitiveInstance::PrimitiveTypeToString(type);
	std::string name = baseName;
	int suffix = 1;
	while (std::any_of(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
		[&name](const std::unique_ptr<PrimitiveInstance>& p) { return p->GetName() == name; })) {
		name = std::string(baseName) + " (" + std::to_string(suffix++) + ")";
	}

	auto prim = std::make_unique<PrimitiveInstance>();
	prim->Initialize(type, name);
	prim->SetCamera(GetCamera());
	prim->SetTranslate(worldPos);
	dynamicPrimitives_.push_back(std::move(prim));
}

void BaseScene::AddDynamicSpline(int tagInt, const Vector3& worldPos) {
	auto spline = std::make_unique<SplineCurveActor>();
	spline->SetTag(static_cast<EntityTag>(tagInt));

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

void BaseScene::RemoveDynamicSpline(const std::string& name) {
	auto it = std::find_if(dynamicSplines_.begin(), dynamicSplines_.end(),
		[&name](const std::unique_ptr<SplineCurveActor>& s) { return s->GetName() == name; });
	if (it != dynamicSplines_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SplineCurveActor>(it->release()));
		dynamicSplines_.erase(it);
	}
}

void BaseScene::InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos) {
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
			HP& hp = e->GetHP();
			hp.enabled = true;
			hp.maxHP = def->maxHP;
			hp.currentHP = def->maxHP;
		}
		if (def->hasDamageDealer) {
			DamageDealer& dd = e->GetDamageDealer();
			dd.enabled = true;
			dd.damage = def->damage;
			dd.multiplier = def->attackMultiplier;
		}
		if (def->hasAttackPower) {
			e->SetHasAttackPower(true);
			e->SetAttackPower(def->attackPower);
		}
		// 敵撃破スコア（タグに関わらずコピー、StagePlay 側で Enemy/Boss だけ参照する）
		e->SetScoreValue(def->scoreValue);
		if (def->hasBullet) {
			BulletParams& bp = e->GetBulletParams();
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
		if (def->hasCarrier) {
			CarrierParams& cp = e->GetCarrierParams();
			cp.enabled           = true;
			cp.childLifetimeSec  = def->carrierChildLifetimeSec;
			cp.childWanderRadius = def->carrierChildWanderRadius;
			cp.childMoveSpeed    = def->carrierChildMoveSpeed;
		}
		if (def->hasCharge) {
			ChargeParams& chp = e->GetChargeParams();
			chp.enabled    = true;
			chp.stage1Time = def->chargeStage1Time;
			chp.stage2Time = def->chargeStage2Time;
			chp.fireRate   = def->chargeFireRate;
		}
		if (def->hasPrecision) {
			PrecisionParams& pp = e->GetPrecisionParams();
			pp.enabled   = true;
			pp.speedAdd  = def->precisionSpeedAdd;
			pp.homingAdd = def->precisionHomingAdd;
		}
		// エフェクトスロットを丸ごとコピー
		if (!def->effects.empty()) {
			e->GetEffects() = def->effects;
		}
		// 弾プレハブスロットを丸ごとコピー
		if (!def->bulletPrefabs.empty()) {
			e->GetBulletPrefabs() = def->bulletPrefabs;
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
		back->SetTag(def->tag);
		back->ApplyPrefabParams(def->primitiveParams);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		back->SetTranslate(worldPos);
		if (def->hasCollider) applyCollider(back->GetCollider());
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
		back->SetTag(def->tag);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(back->GetCollider());
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
		back->SetTag(def->tag);
		back->SetScale(def->defaultScale);
		back->SetRotate(def->defaultRotate);
		if (def->hasCollider) applyCollider(back->GetCollider());
		applyHPAndDamage(back.get());
	}
}

SplineCurveActor* BaseScene::FindDynamicSplineByName(const std::string& name) {
	for (auto& s : dynamicSplines_) {
		if (s && s->GetName() == name) return s.get();
	}
	return nullptr;
}

IImGuiEditable* BaseScene::SpawnEnemyOnSpline(const std::string& prefabName,
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

void BaseScene::UpdateMovingEnemies(float deltaTime) {
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

void BaseScene::UpdateEnemyControllers(float deltaTime, IImGuiEditable* player, float railT) {
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

void BaseScene::SpawnEnemyBullet(const Vector3& pos, const Vector3& direction,
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
		spawned->GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
			if (!other) return;
			if (other->GetTag() != EntityTag::Player) return;
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
	if (spawned) br.baseColliderRadius = spawned->GetCollider().radius;
	bullets_.push_back(br);
}

IImGuiEditable* BaseScene::SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) {
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

void BaseScene::ApplyEnemyRepulsion(IImGuiEditable* self) {
	if (!self) return;
	Vector3* selfPos = self->GetEditableTranslate();
	if (!selfPos) return;
	const float selfR = self->GetCollider().radius;

	auto repelAgainst = [&](IImGuiEditable* other) {
		if (!other || other == self) return;
		const EntityTag tag = other->GetTag();
		if (tag != EntityTag::Enemy) return;
		Vector3* op = other->GetEditableTranslate();
		if (!op) return;
		const float otherR = other->GetCollider().radius;
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

void BaseScene::RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) {
	// UpdateEnemyControllers のループ中に直接 push_back するとイテレータが無効化されるため、
	// 一旦 pending に溜めてループ終了後にマージする
	if (ctrl) pendingEnemyControllers_.push_back(std::move(ctrl));
}

void BaseScene::DestroyDynamicEntity(IImGuiEditable* e) {
	if (!e) return;

	// movingEnemies_ から該当ポインタを無効化（衝突で先に死んだケース等）
	for (auto& m : movingEnemies_) {
		if (m.entity == e) m.entity = nullptr;
	}
	// 敵コントローラから該当ポインタを無効化（dangling 参照防止）
	for (auto& ctrl : enemyControllers_) {
		if (ctrl && ctrl->entity_ == e) ctrl->entity_ = nullptr;
	}
	// ホーミング対象として参照している弾があれば無効化（dangling 参照防止）
	// 貫通弾の多段ヒット記録からも削除する
	for (auto& b : bullets_) {
		if (b.homingTarget == e) b.homingTarget = nullptr;
		if (b.penetrate) b.hitCooldowns.erase(e);
	}

	// Primitive
	{
		auto it = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
			[e](const std::unique_ptr<PrimitiveInstance>& p) { return p.get() == e; });
		if (it != dynamicPrimitives_.end()) {
			// 関連する弾エントリも掃除
			PrimitiveInstance* prim = it->get();
			for (auto& b : bullets_) {
				if (b.primitive == prim) b.remainingLifetime = -1.0f;
			}
			deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(it->release()));
			dynamicPrimitives_.erase(it);
			return;
		}
	}
	// Object3D
	{
		auto it = std::find_if(object3DInstances_.begin(), object3DInstances_.end(),
			[e](const std::unique_ptr<Object3DInstance>& p) { return p.get() == e; });
		if (it != object3DInstances_.end()) {
			deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(it->release()));
			object3DInstances_.erase(it);
			return;
		}
	}
	// Animated
	{
		auto it = std::find_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
			[e](const std::unique_ptr<AnimatedObject3DInstance>& p) { return p.get() == e; });
		if (it != dynamicAnimated_.end()) {
			deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(it->release()));
			dynamicAnimated_.erase(it);
			return;
		}
	}
}

namespace {
	// 進行方向（正規化済み）→ オイラー角（ラジアン）。LH系・前方+Z 前提。
	Vector3 DirectionToEuler(const Vector3& dir) {
		const float yaw = std::atan2(dir.x, dir.z);
		const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
		const float pitch = std::atan2(-dir.y, horizLen);
		return { pitch, yaw, 0.0f };
	}
}

void BaseScene::SpawnPlayerBullet(const Vector3& pos, const Vector3& direction,
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

	// 重要：UpdateDynamicPrimitives はこのフレームの早い段階で既に走り終わっている。
	// 今 spawn した primitive は今フレームの Update がスキップされるため、
	// transform CB が初期値 (0,0,0) のまま Draw されてしまう（弾がワールド原点で巨大に見える原因）。
	// ここで明示的に Update を1回呼んで CB をスポーン位置で確定させる。
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
		DamageDealer& dd = spawned->GetDamageDealer();
		if (dd.enabled) {
			dd.damage = static_cast<int>(static_cast<float>(attackPower) * dd.multiplier);
			penetrateDamage = dd.damage;
			// 貫通弾は CollisionManager の毎フレーム自動ダメージを無効化し、onCollision で damageRate 制御する
			if (penetrate) {
				dd.enabled = false;
			}
		}

		// 敵に当たったときの処理。実ダメージ適用は通常弾=CollisionManager / 貫通弾=ここで手動。
		spawned->GetCollider().onCollision = [this, spawned](IImGuiEditable* other) {
			if (!other) return;
			const EntityTag tag = other->GetTag();
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
						if (other->GetHP().enabled) {
							other->GetHP().TakeDamage(b.penetrateDamage);
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
			// HPゼロの敵は BaseScene::SweepDeadEntities が後で破棄する
		};
	}

	BulletRuntime br{};
	br.primitive = spawned;
	br.velocity = vel;
	br.speed = speed;
	br.remainingLifetime = lifetime;
	br.originPos = pos;
	br.baseColliderRadius = spawned ? spawned->GetCollider().radius : 0.0f;
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
		const std::string trailEff = spawned->FindEffect("trail");
		if (!trailEff.empty()) {
			br.trailEffectHandle = EffectManager::GetInstance()->Play(trailEff, pos);
			// エフェクトの向きを弾の進行方向に合わせる
			EffectManager::GetInstance()->SetRotation(br.trailEffectHandle, bulletEuler);
		}
	}
	bullets_.push_back(br);
}

void BaseScene::SweepDeadEntities() {
	// 死亡エンティティを収集（破棄で配列が変わるので二段階）。Player タグは除外。
	std::vector<IImGuiEditable*> dead;
	dead.reserve(8);
	auto collect = [&](IImGuiEditable* e) {
		if (!e) return;
		if (e->GetTag() == EntityTag::Player) return;
		if (e->GetHP().IsDead()) dead.push_back(e);
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

void BaseScene::UpdateBullets(float deltaTime) {
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
			if (b.primitive->GetTag() == EntityTag::PlayerBullet) {
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
			b.primitive->GetCollider().radius =
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

bool BaseScene::SaveSceneToJson(const std::string& filePath) {
	(void)filePath;
	return false;
}

bool BaseScene::LoadSceneFromJson(const std::string& filePath) {
	(void)filePath;
	return false;
}

void BaseScene::RemoveDynamicPrimitive(const std::string& name) {
	auto it = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
		[&name](const std::unique_ptr<PrimitiveInstance>& p) { return p->GetName() == name; });
	if (it != dynamicPrimitives_.end()) {
		// GPU 使用中対策で 1 フレーム遅延破棄
		deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(it->release()));
		dynamicPrimitives_.erase(it);
	}
}

#ifdef USE_IMGUI
bool BaseScene::IsDynamicObject(IImGuiEditable* editable) const {
	for (const auto& p : dynamicPrimitives_) {
		if (static_cast<IImGuiEditable*>(p.get()) == editable) return true;
	}
	for (const auto& o : object3DInstances_) {
		if (static_cast<IImGuiEditable*>(o.get()) == editable) return true;
	}
	for (const auto& s : dynamicSprites_) {
		if (static_cast<IImGuiEditable*>(s.get()) == editable) return true;
	}
	for (const auto& a : dynamicAnimated_) {
		if (static_cast<IImGuiEditable*>(a.get()) == editable) return true;
	}
	for (const auto& sp : dynamicSplines_) {
		if (static_cast<IImGuiEditable*>(sp.get()) == editable) return true;
	}
	return false;
}
#endif

void BaseScene::UpdateDynamicObjects() {
	for (auto& o : object3DInstances_) {
		o->SetCamera(GetCamera());
		o->Update();
	}
}

void BaseScene::DrawDynamicObjects() {
	for (auto& o : object3DInstances_) {
		o->Draw(dxCore_);
	}
}

void BaseScene::UpdateDynamicAnimated(float deltaTime) {
	for (auto& a : dynamicAnimated_) {
		a->Update(deltaTime);
	}
}

void BaseScene::DispatchDynamicAnimatedSkinning() {
	for (auto& a : dynamicAnimated_) {
		a->DispatchSkinning(dxCore_);
	}
}

void BaseScene::DrawDynamicAnimated() {
	for (auto& a : dynamicAnimated_) {
		a->Draw(dxCore_);
	}
}

void BaseScene::DrawDynamicAnimatedSkeletonDebug() {
#ifdef USE_IMGUI
	for (auto& a : dynamicAnimated_) {
		a->DrawSkeletonDebug(dxCore_);
	}
#endif
}

void BaseScene::UpdateDynamicSprites() {
	for (auto& s : dynamicSprites_) {
		s->Update();
	}
}

void BaseScene::DrawDynamicSprites() {
	for (auto& s : dynamicSprites_) {
		s->Draw();
	}
}

void BaseScene::DrawDynamicSplinesDebug() {
	for (const auto& sp : dynamicSplines_) {
		if (sp) sp->DrawDebug();
	}
}

void BaseScene::UpdateDynamicPrimitives() {
	for (auto& p : dynamicPrimitives_) {
		// Camera が後から差し替わった場合に追従させる
		p->SetCamera(GetCamera());
		p->Update();
	}
}

void BaseScene::DrawDynamicPrimitives() {
	for (auto& p : dynamicPrimitives_) {
		p->Draw();
	}
}

// ====================================================================
// シーン共通サービス
// ====================================================================

void BaseScene::UseCameraCapture(bool enabled) {
	useCameraCapture_ = enabled;
	if (enabled && !cameraPreview_) {
		cameraPreview_ = std::make_unique<CameraPreviewSprite>();
		cameraPreview_->Initialize(spriteManager_);
	}
}

void BaseScene::UseQRCodeReader(bool enabled) {
	// trueからfalseに変わったタイミングで検出状態をリセット
	if (useQRCodeReader_ && !enabled) {
		QRCodeReader::GetInstance()->Reset();
	}
	useQRCodeReader_ = enabled;
}

void BaseScene::UpdateSceneServices() {
	if (useCameraCapture_ && cameraPreview_) {
		cameraPreview_->Update();
	}
	if (useQRCodeReader_) {
		auto* cam = CameraCapture::GetInstance();
		const auto& frame = cam->GetFrameData();
		if (!frame.empty()) {
			QRCodeReader::GetInstance()->Decode(
				frame.data(),
				cam->GetFrameWidth(),
				cam->GetFrameHeight()
			);
		}
	}
}

void BaseScene::DrawSceneServices() {
	if (useCameraCapture_ && cameraPreview_) {
		cameraPreview_->Draw();
	}
}
