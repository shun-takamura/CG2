#include "BaseScene.h"
#include "ModelManager.h"
#include "DirectXCore.h"

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

bool BaseScene::IsDynamicObject(IImGuiEditable* editable) const {
	(void)editable;
	return false;
}
