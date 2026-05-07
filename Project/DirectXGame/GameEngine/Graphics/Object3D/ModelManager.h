#pragma once
#include <map>
#include <memory>
#include <iostream>
#include <mutex>
#include <vector>
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

	// マップアクセスを保護（バックグラウンドスレッドからの PreloadCPU と
	// メインスレッドからの LoadModel/FindModel/FlushGPUUpload が衝突しないように）
	mutable std::mutex mutex_;

	// CPU 完了 → GPU アップロード待ちのキー一覧
	std::vector<std::string> pendingGPU_;
	std::mutex gpuQueueMutex_;

public:

	void Initialize(DirectXCore* dxCore);

	void LoadModel(const std::string& directoryPath, const std::string& filePath);

	ModelCore* GetModelCore() const { return modelCore_.get(); }

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

	//==============================
	// 非同期ロード API
	//==============================

	/// <summary>
	/// CPU フェーズのみのモデル先読み（バックグラウンドスレッドから呼ぶ）
	/// </summary>
	void PreloadCPU(const std::string& directoryPath, const std::string& filePath);

	/// <summary>
	/// CPU パース完了済みかどうか（スレッド安全）
	/// </summary>
	bool IsCPUReady(const std::string& filePath) const;

	/// <summary>
	/// GPU リソースまで作成済みかどうか（スレッド安全）
	/// </summary>
	bool IsGPUReady(const std::string& filePath) const;

	/// <summary>
	/// CPU 完了済みモデルの GPU リソースを毎フレーム maxItems 件分作成（メインスレッドから呼ぶ）
	/// </summary>
	void FlushGPUUpload(DirectXCore* dxCore, int maxItems = 1);

};

