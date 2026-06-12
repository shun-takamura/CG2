#include "Scene.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "ModelManager.h"
#include "Object3DInstance.h"
#include "SpriteInstance.h"
#include "AnimatedObject3DInstance.h"
#include "AnimatedModelInstance.h"
#include "DirectXCore.h"
#include "Primitive/PrimitiveInstance.h"
#include "CameraPreviewSprite.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "Camera.h"
#include "DebugCamera.h"
#include "LineRenderer.h"
#include "MathUtility.h"
#include "Transform.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "ImGuiManager.h"
#include "ViewportWindow.h"
#include "imgui.h"
#endif

Scene::Scene() = default;

Scene::~Scene() {
	// QRをONのまま破棄されたら検出状態を残さない
	if (useQRCodeReader_) {
		QRCodeReader::GetInstance()->Reset();
	}
}

//====================
// デバッグカメラ
//====================

DebugCamera* Scene::GetDebugCamera() {
	if (!debugCamera_) {
		debugCamera_ = std::make_unique<DebugCamera>();
		debugCamera_->Initialize();
	}
	return debugCamera_.get();
}

void Scene::AddHighlight(IImGuiEditable* e) {
	if (!e) return;
	if (std::find(highlightedEntities_.begin(), highlightedEntities_.end(), e)
		!= highlightedEntities_.end()) return;
	highlightedEntities_.push_back(e);
}

void Scene::RemoveHighlight(IImGuiEditable* e) {
	auto it = std::find(highlightedEntities_.begin(), highlightedEntities_.end(), e);
	if (it != highlightedEntities_.end()) highlightedEntities_.erase(it);
}

void Scene::ClearHighlights() {
	highlightedEntities_.clear();
}

void Scene::RunIdPass(ID3D12GraphicsCommandList* commandList) {
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

void Scene::UpdateGlobalEffects(Camera* camera, float deltaTime) {
	EffectManager::GetInstance()->SetCamera(camera);
	EffectManager::GetInstance()->Update(deltaTime);
	if (auto* gpu = EffectManager::GetInstance()->GetGPUParticleManager()) {
		gpu->Update(camera, deltaTime);
	}
}

void Scene::DrawGlobalEffects() {
	if (auto* gpu = EffectManager::GetInstance()->GetGPUParticleManager()) {
		gpu->Draw();
	}
	EffectManager::GetInstance()->Draw();
}

void Scene::UpdateFrozenEffects() {
	// 実時間 delta（タイムスケール非適用）で進める。
	// ヒットストップ等でシーンが 0 倍速でも、エディタのプレビューは等速で動く。
	const float dt = dxCore_ ? dxCore_->GetDeltaTime() : (1.0f / 60.0f);
	UpdateGlobalEffects(GetCamera(), dt);
}

void Scene::SetUseDebugCamera(bool enabled) {
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

void Scene::UpdateDebugCameraIfActive() {
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

void Scene::DrawSceneCameraGizmo() {
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

void Scene::ProcessAsyncLoads()
{
	// 前フレームで Remove したオブジェクトを破棄
	// （ここに来た時点で前フレームの GPU 処理は完了している）
	deferredDeletes_.clear();

	if (!dxCore_) return;
	ModelManager::GetInstance()->FlushGPUUpload(dxCore_, 1);
}

float Scene::GetScaledDeltaTime() const
{
	if (!dxCore_) return 0.0f;
	return dxCore_->GetScaledDeltaTime() * timeScales_[static_cast<int>(TimeGroup::World)];
}

float Scene::GetScaledDeltaTime(TimeGroup g) const
{
	if (!dxCore_) return 0.0f;
	int i = static_cast<int>(g);
	if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return dxCore_->GetScaledDeltaTime();
	return dxCore_->GetScaledDeltaTime() * timeScales_[i];
}

// ====================================================================
// 動的エンティティ操作のデフォルト実装
// ヘッダ内インラインだと TU 間で COMDAT 衝突を起こすケースがあるので cpp に置く
// ====================================================================

void Scene::AddDynamicObject(const std::string& dirPath, const std::string& filename,
	const Vector3& worldPos) {
	auto obj = std::make_unique<Object3DInstance>();
	obj->Initialize(object3DManager_, dxCore_, dirPath, filename);
	obj->SetCamera(GetCamera());
	obj->SetTranslate(worldPos);
	object3DInstances_.push_back(std::move(obj));
}

void Scene::RemoveDynamicObject(const std::string& name) {
	auto it = std::find_if(object3DInstances_.begin(), object3DInstances_.end(),
		[&name](const std::unique_ptr<Object3DInstance>& o) { return o->GetName() == name; });
	if (it != object3DInstances_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(it->release()));
		object3DInstances_.erase(it);
	}
}

void Scene::AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) {
	auto sprite = std::make_unique<SpriteInstance>();
	sprite->Initialize(spriteManager_, texturePath);
	sprite->SetPosition({ clientX, clientY });
	dynamicSprites_.push_back(std::move(sprite));
}

void Scene::RemoveDynamicSprite(const std::string& name) {
	auto it = std::find_if(dynamicSprites_.begin(), dynamicSprites_.end(),
		[&name](const std::unique_ptr<SpriteInstance>& s) { return s->GetName() == name; });
	if (it != dynamicSprites_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(it->release()));
		dynamicSprites_.erase(it);
	}
}

void Scene::AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
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

void Scene::RemoveDynamicAnimated(const std::string& name) {
	auto it = std::find_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
		[&name](const std::unique_ptr<AnimatedObject3DInstance>& a) { return a->GetName() == name; });
	if (it != dynamicAnimated_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(it->release()));
		dynamicAnimated_.erase(it);
	}
}

void Scene::AddDynamicPrimitive(int primitiveType, const Vector3& worldPos) {
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

void Scene::DestroyDynamicEntity(IImGuiEditable* e) {
	if (!e) return;

	// Primitive
	{
		auto it = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
			[e](const std::unique_ptr<PrimitiveInstance>& p) { return p.get() == e; });
		if (it != dynamicPrimitives_.end()) {
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

bool Scene::IsDynamicEntityAlive(IImGuiEditable* e) const {
	if (!e) return false;
	for (const auto& p : dynamicPrimitives_) if (p.get() == e) return true;
	for (const auto& o : object3DInstances_) if (o.get() == e) return true;
	for (const auto& a : dynamicAnimated_)   if (a.get() == e) return true;
	return false;
}

bool Scene::SaveSceneToJson(const std::string& filePath) {
	(void)filePath;
	return false;
}

bool Scene::LoadSceneFromJson(const std::string& filePath) {
	(void)filePath;
	return false;
}

void Scene::RemoveDynamicPrimitive(const std::string& name) {
	auto it = std::find_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
		[&name](const std::unique_ptr<PrimitiveInstance>& p) { return p->GetName() == name; });
	if (it != dynamicPrimitives_.end()) {
		// GPU 使用中対策で 1 フレーム遅延破棄
		deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(it->release()));
		dynamicPrimitives_.erase(it);
	}
}

#ifdef USE_IMGUI
bool Scene::IsDynamicObject(IImGuiEditable* editable) const {
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
	return false;
}
#endif

void Scene::UpdateDynamicObjects() {
	for (auto& o : object3DInstances_) {
		o->SetCamera(GetCamera());
		o->Update();
	}
}

void Scene::DrawDynamicObjects() {
	for (auto& o : object3DInstances_) {
		o->Draw(dxCore_);
	}
}

void Scene::DrawShadowCasters() {
	// 静的 Object3D
	for (auto& o : object3DInstances_) {
		o->DrawShadowPass(dxCore_);
	}
	// スキニングモデル（スキニングは事前にDispatch済みの前提）
	for (auto& a : dynamicAnimated_) {
		a->DrawShadowPass(dxCore_);
	}
}

void Scene::UpdateDynamicAnimated(float deltaTime) {
	for (auto& a : dynamicAnimated_) {
		a->Update(deltaTime);
	}
}

void Scene::DispatchDynamicAnimatedSkinning() {
	for (auto& a : dynamicAnimated_) {
		a->DispatchSkinning(dxCore_);
	}
}

void Scene::DrawDynamicAnimated() {
	for (auto& a : dynamicAnimated_) {
		a->Draw(dxCore_);
	}
}

void Scene::DrawDynamicAnimatedSkeletonDebug() {
#ifdef USE_IMGUI
	for (auto& a : dynamicAnimated_) {
		a->DrawSkeletonDebug(dxCore_);
	}
#endif
}

void Scene::UpdateDynamicSprites() {
	for (auto& s : dynamicSprites_) {
		s->Update();
	}
}

void Scene::DrawDynamicSprites() {
	for (auto& s : dynamicSprites_) {
		s->Draw();
	}
}

void Scene::UpdateDynamicPrimitives() {
	for (auto& p : dynamicPrimitives_) {
		// Camera が後から差し替わった場合に追従させる
		p->SetCamera(GetCamera());
		p->Update();
	}
}

void Scene::DrawDynamicPrimitives() {
	for (auto& p : dynamicPrimitives_) {
		p->Draw();
	}
}

// ====================================================================
// シーン共通サービス
// ====================================================================

void Scene::UseCameraCapture(bool enabled) {
	useCameraCapture_ = enabled;
	if (enabled && !cameraPreview_) {
		cameraPreview_ = std::make_unique<CameraPreviewSprite>();
		cameraPreview_->Initialize(spriteManager_);
	}
}

void Scene::UseQRCodeReader(bool enabled) {
	// trueからfalseに変わったタイミングで検出状態をリセット
	if (useQRCodeReader_ && !enabled) {
		QRCodeReader::GetInstance()->Reset();
	}
	useQRCodeReader_ = enabled;
}

void Scene::UpdateSceneServices() {
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

void Scene::DrawSceneServices() {
	if (useCameraCapture_ && cameraPreview_) {
		cameraPreview_->Draw();
	}
}
