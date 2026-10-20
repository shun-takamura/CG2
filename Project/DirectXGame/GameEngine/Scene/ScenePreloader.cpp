#include "ScenePreloader.h"
#include "DirectXCore.h"
#include "DStorageManager.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "AnimatedMeshCache.h"
#include "SessionLogger.h"
#include "ConvertString.h"
#include "Profiling/LoadProfiler.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <climits>
#include <filesystem>
#include <format>
#include <unordered_set>
#include <objbase.h>

namespace {
	// メインスレッドで GPU フェーズに使ってよい 1 フレームの時間（最低 1 件は進める）
	constexpr double kGpuBudgetMs = 3.0;
	constexpr const char* kManifestDir = "Resources/Json/Preload/";
}

ScenePreloader* ScenePreloader::GetInstance() {
	static ScenePreloader instance;
	return &instance;
}

void ScenePreloader::Initialize(DirectXCore* dxCore) {
	dxCore_ = dxCore;
}

void ScenePreloader::Finalize() {
	JoinWorker();
	textureBatches_.clear();
	pendingMeshes_.clear();
	manifest_.Clear();
	stage_ = Stage::Idle;
	dxCore_ = nullptr;
}

//====================
// 先読み
//====================

void ScenePreloader::Begin(const std::string& sceneName) {
	if (!dxCore_) return;
	if (IsBusy()) CompleteNow();

	sceneName_ = sceneName;
	manifest_.Clear();
	nextTexture_ = 0;
	nextMesh_ = 0;
	pendingMeshes_.clear();
	textureBatches_.clear();
	if (!LoadManifest(ManifestPath(sceneName), manifest_) || manifest_.Empty()) {
		// マニフェストが無い（まだ一度も Debug でそのシーンを読んでいない）：何もしない。シーンは今までどおり同期で読む
		stage_ = Stage::Done;
		return;
	}
	beginMs_ = LoadProfiler::NowMs();
	StartWorker(Stage::CpuTextures);
}

void ScenePreloader::Update() {
	switch (stage_) {
	case Stage::CpuTextures:
		if (workerDone_) {
			JoinWorker();
			stage_ = Stage::GpuTextures;
		}
		break;
	case Stage::GpuTextures:
		UpdateGpuTextures(kGpuBudgetMs);
		FinishCompletedTextureBatches(false);
		if (nextTexture_ >= manifest_.textures.size() && nextMesh_ >= pendingMeshes_.size()) stage_ = Stage::WaitTextures;
		break;
	case Stage::WaitTextures:
		FinishCompletedTextureBatches(false);
		if (textureBatches_.empty()) StartModels();
		break;
	case Stage::CpuModels:
		if (workerDone_) {
			JoinWorker();
			stage_ = Stage::GpuModels;
		}
		break;
	case Stage::GpuModels: {
		auto* mm = ModelManager::GetInstance();
		const double start = LoadProfiler::NowMs();
		while (mm->GetPendingGPUCount() > 0) {
			mm->FlushGPUUpload(dxCore_, 1);
			if (LoadProfiler::NowMs() - start >= kGpuBudgetMs) break;
		}
		if (mm->GetPendingGPUCount() == 0) Finish();
		break;
	}
	default:
		break;
	}
}

void ScenePreloader::CompleteNow() {
	if (!IsBusy()) return;

	JoinWorker();
	if (stage_ == Stage::CpuTextures || stage_ == Stage::GpuTextures || stage_ == Stage::WaitTextures) {
		// 残りのテクスチャをまとめて送り、1 回だけ待って仕上げる
		UpdateGpuTextures(1e9);
		FinishCompletedTextureBatches(true);
		// モデルの CPU フェーズはまだ始めていない：シーンの Initialize が LoadModel で読む
	}
	else {
		// モデルの CPU フェーズは JoinWorker で終わっている。GPU フェーズを全部済ませる
		ModelManager::GetInstance()->FlushGPUUpload(dxCore_, INT_MAX);
	}
	Finish();
}

float ScenePreloader::GetProgress() const {
	switch (stage_) {
	case Stage::Idle:        return 0.0f;
	case Stage::CpuTextures: return 0.1f;
	case Stage::GpuTextures:
		return 0.1f + 0.4f * (manifest_.textures.empty() ? 1.0f
			: static_cast<float>(nextTexture_) / static_cast<float>(manifest_.textures.size()));
	case Stage::WaitTextures: return 0.5f;
	case Stage::CpuModels:    return 0.6f;
	case Stage::GpuModels:    return 0.8f;
	default:                  return 1.0f;
	}
}

void ScenePreloader::StartWorker(Stage stage) {
	JoinWorker();
	stage_ = stage;
	workerDone_ = false;

	if (stage == Stage::CpuTextures) {
		worker_ = std::thread([this]() {
			// WIC（png などのデコード）はスレッドごとに COM の初期化が要る
			const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			for (const ShaderEntry& s : manifest_.shaders) {
				dxCore_->LoadShaderBlob(s.path, s.profile.c_str());
			}
			auto* tm = TextureManager::GetInstance();
			for (const TextureEntry& t : manifest_.textures) {
				tm->LoadTextureCPU(t.path, t.linear);
			}
			// アニメーションするモデル（.mesh のヘッダ・.skel・既定の .anim）と、姿勢の切り替えで使う .anim
			auto* amc = AnimatedMeshCache::GetInstance();
			for (const ModelEntry& m : manifest_.animatedModels) {
				if (amc->Contains(m.directory, m.file)) continue;
				if (auto data = AnimatedMeshCache::LoadCPU(m.directory, m.file)) pendingMeshes_.push_back(std::move(data));
			}
			for (const std::string& a : manifest_.animations) amc->GetAnimation(a);
			if (SUCCEEDED(co)) CoUninitialize();
			workerDone_ = true;
		});
	}
	else {
		worker_ = std::thread([this]() {
			auto* mm = ModelManager::GetInstance();
			for (const ModelEntry& m : manifest_.models) {
				mm->PreloadCPU(m.directory, m.file);
			}
			workerDone_ = true;
		});
	}
}

void ScenePreloader::JoinWorker() {
	if (worker_.joinable()) worker_.join();
}

void ScenePreloader::UpdateGpuTextures(double budgetMs) {
	auto* tm = TextureManager::GetInstance();
	auto* amc = AnimatedMeshCache::GetInstance();
	const double start = LoadProfiler::NowMs();
	TextureBatch batch;
	bool overBudget = false;
	while (nextTexture_ < manifest_.textures.size()) {
		const std::string path = manifest_.textures[nextTexture_++].path;
		// DirectStorage 経路は登録だけ（待たない）。それ以外の経路はその場でアップロードまで済む
		if (tm->BeginTextureGPUAsync(path)) batch.finishers.push_back([tm, path]() { tm->FinishTextureGPU(path); });
		if (LoadProfiler::NowMs() - start >= budgetMs) { overBudget = true; break; }
	}
	// アニメーションするモデル：バッファを作って転送を登録。仕上げ（インデックスのバリア）が済んでからキャッシュへ入れる
	while (!overBudget && nextMesh_ < pendingMeshes_.size()) {
		std::shared_ptr<AnimatedMeshData> data = pendingMeshes_[nextMesh_++];
		DirectXCore* dx = dxCore_;
		if (AnimatedMeshCache::CreateGPU(*data, dx, false)) {
			batch.finishers.push_back([amc, data, dx]() { AnimatedMeshCache::FinishGPU(*data, dx); amc->Insert(data); });
		} else {
			amc->Insert(data);
		}
		if (LoadProfiler::NowMs() - start >= budgetMs) break;
	}
	if (!batch.finishers.empty()) {
		batch.fence = DStorageManager::GetInstance()->SubmitWithFence();
		textureBatches_.push_back(std::move(batch));
	}
}

void ScenePreloader::FinishCompletedTextureBatches(bool wait) {
	if (textureBatches_.empty()) return;
	auto* ds = DStorageManager::GetInstance();
	if (wait) ds->SubmitAndWait();

	// fence は送った順に終わるので、前から見て終わっていない所で止める
	size_t done = 0;
	for (; done < textureBatches_.size(); ++done) {
		if (!ds->IsFenceComplete(textureBatches_[done].fence)) break;
		for (const auto& finish : textureBatches_[done].finishers) finish();
	}
	textureBatches_.erase(textureBatches_.begin(), textureBatches_.begin() + done);
}

void ScenePreloader::StartModels() {
	if (manifest_.models.empty()) {
		Finish();
		return;
	}
	StartWorker(Stage::CpuModels);
}

void ScenePreloader::Finish() {
	if (stage_ == Stage::Done || stage_ == Stage::Idle) return;
	stage_ = Stage::Done;
	SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
		std::format("LOAD kind=preload scene={} shaders={} textures={} models={} animated_models={} animations={} wall_ms={:.2f}",
			sceneName_, manifest_.shaders.size(), manifest_.textures.size(), manifest_.models.size(),
			pendingMeshes_.size(), manifest_.animations.size(), LoadProfiler::NowMs() - beginMs_));
	pendingMeshes_.clear();
}

//====================
// 記録
//====================

void ScenePreloader::BeginRecording() {
	if (!dxCore_) return;
	{
		std::lock_guard<std::mutex> lock(recordMutex_);
		recorded_.Clear();
		recording_ = true;
	}
	// 同じ物が何度も読まれる（キャッシュに当たる）ので、最初の 1 回だけ記録する
	auto seen = std::make_shared<std::unordered_set<std::string>>();
	TextureManager::GetInstance()->SetLoadObserver([this, seen](const std::string& path, bool linear) {
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (recording_ && seen->insert("t:" + path).second) recorded_.textures.push_back({ path, linear });
	});
	ModelManager::GetInstance()->SetLoadObserver([this, seen](const std::string& dir, const std::string& file) {
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (recording_ && seen->insert("m:" + file).second) recorded_.models.push_back({ dir, file });
	});
	AnimatedMeshCache::GetInstance()->SetLoadObserver([this, seen](const std::string& dir, const std::string& file) {
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (recording_ && seen->insert("a:" + dir + "/" + file).second) recorded_.animatedModels.push_back({ dir, file });
	});
	AnimatedMeshCache::GetInstance()->SetAnimationObserver([this, seen](const std::string& path) {
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (recording_ && seen->insert("c:" + path).second) recorded_.animations.push_back(path);
	});
	dxCore_->SetShaderLoadObserver([this, seen](const std::wstring& path, const wchar_t* profile) {
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (recording_ && seen->insert("s:" + ConvertString(path) + "|" + ConvertString(std::wstring(profile))).second) {
			recorded_.shaders.push_back({ path, profile });
		}
	});
}

void ScenePreloader::EndRecording(const std::string& sceneName) {
	if (!dxCore_) return;
	TextureManager::GetInstance()->SetLoadObserver(nullptr);
	ModelManager::GetInstance()->SetLoadObserver(nullptr);
	AnimatedMeshCache::GetInstance()->SetLoadObserver(nullptr);
	AnimatedMeshCache::GetInstance()->SetAnimationObserver(nullptr);
	dxCore_->SetShaderLoadObserver(nullptr);

	Manifest recorded;
	{
		std::lock_guard<std::mutex> lock(recordMutex_);
		if (!recording_) return;
		recording_ = false;
		recorded = std::move(recorded_);
		recorded_.Clear();
	}
#ifdef _DEBUG
	if (recorded.Empty()) return;
	// 上書きせず、今のマニフェストに無い物だけ足す（雲の上から始めた時と HUB から来た時で読む物が少し違うため。
	// 一から作り直すときはファイルを消す）
	const std::string path = ManifestPath(sceneName);
	Manifest merged;
	LoadManifest(path, merged);
	const size_t before = merged.Count();
	std::unordered_set<std::string> have;
	for (const ShaderEntry& s : merged.shaders) have.insert("s:" + ConvertString(s.path) + "|" + ConvertString(s.profile));
	for (const TextureEntry& t : merged.textures) have.insert("t:" + t.path);
	for (const ModelEntry& m : merged.models) have.insert("m:" + m.file);
	for (const ModelEntry& m : merged.animatedModels) have.insert("a:" + m.directory + "/" + m.file);
	for (const std::string& a : merged.animations) have.insert("c:" + a);
	for (const ShaderEntry& s : recorded.shaders) {
		if (have.insert("s:" + ConvertString(s.path) + "|" + ConvertString(s.profile)).second) merged.shaders.push_back(s);
	}
	for (const TextureEntry& t : recorded.textures) {
		if (have.insert("t:" + t.path).second) merged.textures.push_back(t);
	}
	for (const ModelEntry& m : recorded.models) {
		if (have.insert("m:" + m.file).second) merged.models.push_back(m);
	}
	for (const ModelEntry& m : recorded.animatedModels) {
		if (have.insert("a:" + m.directory + "/" + m.file).second) merged.animatedModels.push_back(m);
	}
	for (const std::string& a : recorded.animations) {
		if (have.insert("c:" + a).second) merged.animations.push_back(a);
	}
	if (merged.Count() != before || before == 0) {
		SaveManifest(path, sceneName, merged);
	}
#else
	(void)sceneName;
#endif
}

//====================
// マニフェスト（Resources/Json/Preload/<シーン名>.json）
//====================

std::string ScenePreloader::ManifestPath(const std::string& sceneName) {
	return kManifestDir + sceneName + ".json";
}

bool ScenePreloader::LoadManifest(const std::string& path, Manifest& out) {
	if (!std::filesystem::exists(path)) return false;
	const auto parsed = JsonParser::ParseFile(path);
	if (!parsed.success || !parsed.value.IsObject()) return false;
	const JsonValue& root = parsed.value;

	if (const JsonValue* a = root.Find("shaders"); a && a->IsArray()) {
		for (const JsonValue& e : a->AsArray()) {
			if (!e.IsObject()) continue;
			out.shaders.push_back({ ConvertString(e["path"].AsString(std::string())), ConvertString(e["profile"].AsString(std::string())) });
		}
	}
	if (const JsonValue* a = root.Find("textures"); a && a->IsArray()) {
		for (const JsonValue& e : a->AsArray()) {
			if (!e.IsObject()) continue;
			out.textures.push_back({ e["path"].AsString(std::string()), e["linear"].AsBool(false) });
		}
	}
	if (const JsonValue* a = root.Find("models"); a && a->IsArray()) {
		for (const JsonValue& e : a->AsArray()) {
			if (!e.IsObject()) continue;
			out.models.push_back({ e["dir"].AsString(std::string()), e["file"].AsString(std::string()) });
		}
	}
	if (const JsonValue* a = root.Find("animatedModels"); a && a->IsArray()) {
		for (const JsonValue& e : a->AsArray()) {
			if (!e.IsObject()) continue;
			out.animatedModels.push_back({ e["dir"].AsString(std::string()), e["file"].AsString(std::string()) });
		}
	}
	if (const JsonValue* a = root.Find("animations"); a && a->IsArray()) {
		for (const JsonValue& e : a->AsArray()) {
			if (e.IsString()) out.animations.push_back(e.AsString());
		}
	}
	// 空のパスは読まない
	std::erase_if(out.shaders, [](const ShaderEntry& s) { return s.path.empty() || s.profile.empty(); });
	std::erase_if(out.textures, [](const TextureEntry& t) { return t.path.empty(); });
	std::erase_if(out.models, [](const ModelEntry& m) { return m.file.empty(); });
	std::erase_if(out.animatedModels, [](const ModelEntry& m) { return m.file.empty(); });
	std::erase_if(out.animations, [](const std::string& a) { return a.empty(); });
	return true;
}

bool ScenePreloader::SaveManifest(const std::string& path, const std::string& sceneName, const Manifest& manifest) {
	JsonValue root = JsonValue::MakeObject();
	root["scene"] = JsonValue(sceneName);
	root["note"] = JsonValue(std::string("Debug でこのシーンを読んだときに自動で書き出す（ScenePreloader）。手で直さない"));

	JsonValue shaders = JsonValue::MakeArray();
	for (const ShaderEntry& s : manifest.shaders) {
		JsonValue e = JsonValue::MakeObject();
		e["path"] = JsonValue(ConvertString(s.path));
		e["profile"] = JsonValue(ConvertString(s.profile));
		shaders.Push(std::move(e));
	}
	root["shaders"] = std::move(shaders);

	JsonValue textures = JsonValue::MakeArray();
	for (const TextureEntry& t : manifest.textures) {
		JsonValue e = JsonValue::MakeObject();
		e["path"] = JsonValue(t.path);
		e["linear"] = JsonValue(t.linear);
		textures.Push(std::move(e));
	}
	root["textures"] = std::move(textures);

	JsonValue models = JsonValue::MakeArray();
	for (const ModelEntry& m : manifest.models) {
		JsonValue e = JsonValue::MakeObject();
		e["dir"] = JsonValue(m.directory);
		e["file"] = JsonValue(m.file);
		models.Push(std::move(e));
	}
	root["models"] = std::move(models);

	JsonValue animatedModels = JsonValue::MakeArray();
	for (const ModelEntry& m : manifest.animatedModels) {
		JsonValue e = JsonValue::MakeObject();
		e["dir"] = JsonValue(m.directory);
		e["file"] = JsonValue(m.file);
		animatedModels.Push(std::move(e));
	}
	root["animatedModels"] = std::move(animatedModels);

	JsonValue animations = JsonValue::MakeArray();
	for (const std::string& a : manifest.animations) animations.Push(JsonValue(a));
	root["animations"] = std::move(animations);

	std::error_code ec;
	std::filesystem::create_directories(kManifestDir, ec);
	return JsonWriter::WriteFile(path, root);
}
