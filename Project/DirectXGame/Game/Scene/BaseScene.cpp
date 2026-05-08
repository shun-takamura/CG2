#include "BaseScene.h"
#include "ModelManager.h"
#include "DirectXCore.h"
#include "Primitive/PrimitiveInstance.h"
#include <algorithm>

BaseScene::~BaseScene() = default;

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

void BaseScene::AddDynamicObject(const std::string& dirPath, const std::string& filename) {
	(void)dirPath; (void)filename;
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

void BaseScene::AddDynamicAnimated(const std::string& dirPath, const std::string& filename) {
	(void)dirPath; (void)filename;
}

void BaseScene::RemoveDynamicAnimated(const std::string& name) {
	(void)name;
}

void BaseScene::AddDynamicPrimitive(int primitiveType) {
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
	dynamicPrimitives_.push_back(std::move(prim));
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

bool BaseScene::IsDynamicObject(IImGuiEditable* editable) const {
	for (const auto& p : dynamicPrimitives_) {
		if (static_cast<IImGuiEditable*>(p.get()) == editable) return true;
	}
	return false;
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
