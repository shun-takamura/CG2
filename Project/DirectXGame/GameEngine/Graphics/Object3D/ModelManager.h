#pragma once
#include <map>
#include <memory> 
#include <iostream>
#include"ModelInstance.h"
#include"DirectXCore.h"

class ModelManager
{

	//static ModelManager* instance;

	std::unique_ptr<ModelCore> modelCore_;

	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(ModelManager&) = default;
	ModelManager& operator=(ModelManager&) = default;

	// モデルデータの配列
	std::map<std::string, std::unique_ptr<ModelInstance>>models;

public:

	void Initialize(DirectXCore* dxCore);

	void LoadModel(const std::string& filePath);

	/// <summary>
	/// モデルの検索
	/// </summary>
	/// <param name="filePath">モデルのファイルパス</param>
	/// <returns></returns>
	ModelInstance* FindModel(const std::string& filePath);

	// シングルトンインスタンスの取得
	static ModelManager* GetInstance();

	// 終了
	void Finalize();

};

