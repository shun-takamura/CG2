#include "BaseScene.h"
#include "ModelManager.h"
#include "DirectXCore.h"
#include "Primitive/PrimitiveInstance.h"
#include "CameraPreviewSprite.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include <algorithm>

BaseScene::BaseScene() = default;

BaseScene::~BaseScene() {
	// QRをONのまま破棄されたら検出状態を残さない
	if (useQRCodeReader_) {
		QRCodeReader::GetInstance()->Reset();
	}
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
	return dxCore_->GetScaledDeltaTime() * sceneTimeScale_;
}

// ====================================================================
// 動的エンティティ操作のデフォルト実装（受け付けるシーンが override する）
// ヘッダ内インラインだと TU 間で COMDAT 衝突を起こすケースがあるので cpp に置く
// ====================================================================

void BaseScene::AddDynamicObject(const std::string& dirPath, const std::string& filename,
	const Vector3& worldPos) {
	(void)dirPath; (void)filename; (void)worldPos;
}

void BaseScene::RemoveDynamicObject(const std::string& name) {
	(void)name;
}

void BaseScene::AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) {
	(void)texturePath; (void)clientX; (void)clientY;
}

void BaseScene::RemoveDynamicSprite(const std::string& name) {
	(void)name;
}

void BaseScene::AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
	const Vector3& worldPos) {
	(void)dirPath; (void)filename; (void)worldPos;
}

void BaseScene::RemoveDynamicAnimated(const std::string& name) {
	(void)name;
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
	(void)tagInt; (void)worldPos;
}

void BaseScene::RemoveDynamicSpline(const std::string& name) {
	(void)name;
}

void BaseScene::InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos) {
	(void)prefabName; (void)worldPos;
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
	return false;
}
#endif

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
