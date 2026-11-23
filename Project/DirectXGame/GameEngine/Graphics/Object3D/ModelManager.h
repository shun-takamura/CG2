#pragma once
#include <functional>
#include <map>
#include <memory>
#include <iostream>
#include <mutex>
#include <vector>
#include"ModelInstance.h"
#include"DirectXCore.h"

/// <summary>
/// モデル（.mesh / .mat）を読み込み、パスで引けるように持つ。
/// シングルトンにする理由：読み込んだモデルをパスごとに共有し、同じモデルを二重に読み込まないため。
/// </summary>
class ModelManager
{

	std::unique_ptr<ModelCore> modelCore_;

	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	// モデルデータの配列
	std::map<std::string, std::unique_ptr<ModelInstance>>models;

	// マップアクセスを保護（バックグラウンドスレッドからの PreloadCPU と
	// メインスレッドからの LoadModel/FindModel/FlushGPUUpload が衝突しないように）
	mutable std::mutex mutex_;

	// CPU 完了 → GPU アップロード待ちのキー一覧
	std::vector<std::string> pendingGPU_;
	mutable std::mutex gpuQueueMutex_;

	// LoadModel が呼ばれたら知らせる先（シーンの読み込みの記録用。空なら何もしない）
	std::function<void(const std::string& directoryPath, const std::string& filePath)> loadObserver_;

public:

	/// <summary>モデル読み込みで共有する ModelCore を作る</summary>
	/// <param name="dxCore">DirectXCore</param>
	void Initialize(DirectXCore* dxCore);

	/// <summary>
	/// モデルを同期で読み込む（CPU + GPU）。読み込み済みなら何もしない。
	/// 先読み中のモデルは CPU フェーズの完了を待ってから GPU フェーズを行う
	/// </summary>
	/// <param name="directoryPath">モデルのあるディレクトリ</param>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	void LoadModel(const std::string& directoryPath, const std::string& filePath);

	/// <summary>共有の ModelCore を返す</summary>
	/// <returns>ModelCore（Initialize 前は nullptr）</returns>
	ModelCore* GetModelCore() const { return modelCore_.get(); }

	/// <summary>
	/// モデルの検索
	/// </summary>
	/// <param name="filePath">モデルのファイルパス</param>
	/// <returns>読み込み済みのモデル。無ければ nullptr</returns>
	ModelInstance* FindModel(const std::string& filePath);

	/// <summary>シングルトンインスタンスの取得</summary>
	/// <returns>唯一のインスタンス</returns>
	static ModelManager* GetInstance();

	/// <summary>読み込み済みモデル数（P.E.P.P.E.R. 計測用）</summary>
	/// <returns>登録済みのモデル数</returns>
	size_t GetModelCount() const { std::lock_guard<std::mutex> lock(mutex_); return models.size(); }

	/// <summary>終了。待ちの列と全モデルと ModelCore を解放する</summary>
	void Finalize();

	//==============================
	// 非同期ロード API
	//==============================

	/// <summary>
	/// CPU フェーズのみのモデル先読み（バックグラウンドスレッドから呼ぶ）。
	/// queueGPU = false なら FlushGPUUpload の待ちの列に入れない（BeginModelGPUAsync で呼び出し側が送る。
	/// Scene::ProcessAsyncLoads が先に同期で仕上げて DirectStorage を待つのを避ける）
	/// </summary>
	/// <param name="directoryPath">モデルのあるディレクトリ</param>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	/// <param name="queueGPU">FlushGPUUpload の待ちの列に入れるなら true</param>
	void PreloadCPU(const std::string& directoryPath, const std::string& filePath, bool queueGPU = true);

	/// <summary>
	/// CPU フェーズ済みのモデルの GPU フェーズを待たずに始める（メインスレッドから呼ぶ）。
	/// true = DirectStorage に積んだ。呼び出し側が DStorageManager::SubmitWithFence で送り、fence が終わってから FinishModelGPU を呼ぶ。
	/// false = 待つ物は無い（FS 経路はこの中で仕上げまで済む／済み／CPU フェーズ前／無い）
	/// </summary>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	/// <returns>DirectStorage に積んだら true</returns>
	bool BeginModelGPUAsync(const std::string& filePath);
	/// <summary>BeginModelGPUAsync で送った物を仕上げる（fence が終わってから。メインスレッドから呼ぶ）</summary>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	void FinishModelGPU(const std::string& filePath);

	/// <summary>
	/// CPU パース完了済みかどうか（スレッド安全）
	/// </summary>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	/// <returns>CPU フェーズ済みなら true（未登録は false）</returns>
	bool IsCPUReady(const std::string& filePath) const;

	/// <summary>
	/// GPU リソースまで作成済みかどうか（スレッド安全）
	/// </summary>
	/// <param name="filePath">モデルのファイル名（検索のキー）</param>
	/// <returns>GPU リソースまで済みなら true（未登録は false）</returns>
	bool IsGPUReady(const std::string& filePath) const;

	/// <summary>
	/// CPU 完了済みモデルの GPU リソースを毎フレーム maxItems 件分作成（メインスレッドから呼ぶ）
	/// </summary>
	/// <param name="dxCore">バッファを作るのに使う</param>
	/// <param name="maxItems">このフレームで仕上げる最大件数</param>
	void FlushGPUUpload(DirectXCore* dxCore, int maxItems = 1);

	/// <summary>GPU アップロード待ちの件数（スレッド安全）</summary>
	/// <returns>待ちの列の件数</returns>
	size_t GetPendingGPUCount() const { std::lock_guard<std::mutex> lock(gpuQueueMutex_); return pendingGPU_.size(); }

	/// <summary>LoadModel が呼ばれるたびに知らせる（キャッシュに当たっても）。空の関数で外す</summary>
	/// <param name="observer">LoadModel のたびに呼ぶ関数（ディレクトリ, ファイル名）</param>
	void SetLoadObserver(std::function<void(const std::string& directoryPath, const std::string& filePath)> observer) {
		loadObserver_ = std::move(observer);
	}

};

