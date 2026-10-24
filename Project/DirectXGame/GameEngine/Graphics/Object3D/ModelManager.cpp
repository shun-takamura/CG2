#include "ModelManager.h"
#include <thread>

//ModelManager* ModelManager::instance = nullptr;

void ModelManager::Initialize(DirectXCore* dxCore)
{
	modelCore_ = std::make_unique<ModelCore>();
	modelCore_->Initialize(dxCore);
}

void ModelManager::LoadModel(const std::string& directoryPath, const std::string& filePath)
{
	if (loadObserver_) loadObserver_(directoryPath, filePath);

	// ============================================
	// エントリの取得 or 作成（オーナー権の決定）
	// ============================================
	ModelInstance* target = nullptr;
	bool isOwner = false;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = models.find(filePath);
		if (it == models.end()) {
			// このスレッドがエントリを作成 → CPU ロードのオーナー
			auto newModel = std::make_unique<ModelInstance>();
			target = newModel.get();
			models.insert(std::make_pair(filePath, std::move(newModel)));
			isOwner = true;
		} else {
			target = it->second.get();
		}
	}

	if (isOwner) {
		// ============================================
		// 自分が作成したエントリ → 同期で CPU + GPU フルロード
		// ============================================
		target->LoadCPU(directoryPath, filePath);
		target->InitializeGPU(modelCore_.get(), modelCore_->GetDXCore());
		return;
	}

	// ============================================
	// 既存エントリ → 別スレッド（バックグラウンド先読み）が CPU パース中の可能性あり
	// CPU 完了まで待機してから GPU フェーズを実行する
	// ============================================
	while (!target->IsCPUReady()) {
		std::this_thread::yield();
	}

	if (!target->IsGPUReady()) {
		target->InitializeGPU(modelCore_.get(), modelCore_->GetDXCore());
	}
}

ModelInstance* ModelManager::FindModel(const std::string& filePath)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = models.find(filePath);
	if (it != models.end()) {
		return it->second.get();
	}
	return nullptr;
}

ModelManager* ModelManager::GetInstance()
{
	static ModelManager instance;
	return &instance;
}

void ModelManager::Finalize()
{
	{
		std::lock_guard<std::mutex> lock(gpuQueueMutex_);
		pendingGPU_.clear();
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		models.clear();
	}
	modelCore_.reset();
}

//==============================
// 非同期ロード API
//==============================

void ModelManager::PreloadCPU(const std::string& directoryPath, const std::string& filePath, bool queueGPU)
{
	// 既にエントリがあれば、メインスレッドの所有なのでスキップ
	// （二重ロードや LoadCPU の競合を避けるため、エントリ作成者だけが LoadCPU を呼ぶ）
	ModelInstance* target = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = models.find(filePath);
		if (it != models.end()) {
			return;
		}
		auto newModel = std::make_unique<ModelInstance>();
		target = newModel.get();
		models.insert(std::make_pair(filePath, std::move(newModel)));
	}

	// ロックを外した状態で Assimp パース（GPU 呼び出しなし）
	target->LoadCPU(directoryPath, filePath);

	// GPU アップロード待ちキューに登録
	if (queueGPU) {
		std::lock_guard<std::mutex> lock(gpuQueueMutex_);
		pendingGPU_.push_back(filePath);
	}
}

bool ModelManager::BeginModelGPUAsync(const std::string& filePath)
{
	if (!modelCore_) return false;
	ModelInstance* target = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = models.find(filePath);
		if (it != models.end()) target = it->second.get();
	}
	// CPU フェーズの途中（別スレッドが読んでいる）・済みの物は触らない
	if (!target || !target->IsCPUReady() || target->IsGPUReady() || target->IsGPUPending()) return false;

	// 待ちの列に入っていれば外す（FlushGPUUpload が同期で仕上げないように）
	{
		std::lock_guard<std::mutex> lock(gpuQueueMutex_);
		std::erase(pendingGPU_, filePath);
	}

	DirectXCore* dxCore = modelCore_->GetDXCore();
	if (target->BeginGPUAsync(modelCore_.get(), dxCore)) return true;
	// FS 経路は転送をコマンドリストに積んだだけなので、その場で仕上げてよい
	target->FinishGPU(dxCore);
	return false;
}

void ModelManager::FinishModelGPU(const std::string& filePath)
{
	if (!modelCore_) return;
	ModelInstance* target = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = models.find(filePath);
		if (it != models.end()) target = it->second.get();
	}
	if (target) target->FinishGPU(modelCore_->GetDXCore());
}

bool ModelManager::IsCPUReady(const std::string& filePath) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = models.find(filePath);
	return it != models.end() && it->second->IsCPUReady();
}

bool ModelManager::IsGPUReady(const std::string& filePath) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = models.find(filePath);
	return it != models.end() && it->second->IsGPUReady();
}

void ModelManager::FlushGPUUpload(DirectXCore* dxCore, int maxItems)
{
	if (!dxCore || !modelCore_) return;

	for (int processed = 0; processed < maxItems; ++processed) {
		// キューから 1 件取り出す
		std::string key;
		{
			std::lock_guard<std::mutex> lock(gpuQueueMutex_);
			if (pendingGPU_.empty()) return;
			key = std::move(pendingGPU_.front());
			pendingGPU_.erase(pendingGPU_.begin());
		}

		// 対象モデルを取り出して GPU フェーズを実行
		ModelInstance* target = nullptr;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = models.find(key);
			if (it != models.end()) {
				target = it->second.get();
			}
		}

		// 待たずに送った物（GPUPending）は送った側が fence の後で仕上げる
		if (target && target->IsCPUReady() && !target->IsGPUReady() && !target->IsGPUPending()) {
			target->InitializeGPU(modelCore_.get(), dxCore);
		}
	}
}
