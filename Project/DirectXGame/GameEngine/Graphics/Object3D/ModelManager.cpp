#include "ModelManager.h"

//ModelManager* ModelManager::instance = nullptr;

void ModelManager::Initialize(DirectXCore* dxCore)
{
	modelCore_ = std::make_unique<ModelCore>();
	modelCore_->Initialize(dxCore);
}

void ModelManager::LoadModel(const std::string& filePath)
{
	// 読み込み済みモデルを検索
	if (models.contains(filePath)) {
		// 読み込み済みなら早期リターン
		return;
	}

	// モデルの生成とファイル読み込み、初期化
	std::unique_ptr<ModelInstance>model = std::make_unique<ModelInstance>();
	model->Initialize(modelCore_.get(), "Resources", filePath);

	// モデルをmapコンテナに格納
	models.insert(std::make_pair(filePath, std::move(model)));

}

ModelInstance* ModelManager::FindModel(const std::string& filePath)
{
	// 読み込み済みモデルを検索
	if (models.contains(filePath)) {

		// 読み込みモデルを戻り値としてreturn
		return models.at(filePath).get();
	}

	// ファイル名一致なし
	return nullptr;
}

ModelManager* ModelManager::GetInstance()
{
	static ModelManager instance;
	return &instance;
}

void ModelManager::Finalize()
{
	// モデルとModelCoreをクリア
	models.clear();
	modelCore_.reset();
}
