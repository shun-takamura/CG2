#include "TextureLoadBenchmark.h"

#include "AssetLocator.h"
#include "DStorageManager.h"
#include "DirectXCore.h"
#include "TextureManager.h"
#include "Log.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

namespace TextureLoadBenchmark {

std::vector<std::string> LoadOrCreateList(const std::string& listPath) {
	std::vector<std::string> paths;

	if (std::ifstream ifs(listPath); ifs) {
		std::string line;
		while (std::getline(ifs, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			paths.push_back(line);
		}
		return paths;
	}

	auto* loc = AssetLocator::GetInstance();
	paths = loc->ListByExtension(".dds");
	std::sort(paths.begin(), paths.end());

	if (!loc->IsPackMode()) {
		// FS の走査結果は pack と集合が違いうるので、比較用の一覧としては保存しない
		Log("[LOAD] " + listPath + " not found; using FS scan (run once with --use-pack to create the shared list)\n");
		return paths;
	}

	std::error_code ec;
	const std::filesystem::path p(listPath);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	if (std::ofstream ofs(p); ofs) {
		ofs << "# テクスチャロード計測の対象（pack の目次から自動生成）。3 方式で同じ集合を読むために使う\n";
		for (const std::string& s : paths) ofs << s << "\n";
		Log(std::format("[LOAD] created {} ({} textures)\n", listPath, paths.size()));
	}
	return paths;
}

std::string CurrentModeName() {
	auto* loc = AssetLocator::GetInstance();
	if (!loc->IsPackMode()) return "FS";
	auto* ds = DStorageManager::GetInstance();
	return (ds->IsInitialized() && ds->GetPackFile()) ? "Pack+DStorage" : "Pack+CPU";
}

LoadProfiler::BenchResult Run(DirectXCore* dxCore, const std::vector<std::string>& paths) {
	LoadProfiler::BenchResult result;
	result.mode = CurrentModeName();
	if (!dxCore) return result;

	auto* loc = AssetLocator::GetInstance();
	auto* ds = DStorageManager::GetInstance();
	auto* tm = TextureManager::GetInstance();

	// 対象の確定とサイズ集計（計測区間の外で行う）
	std::vector<std::string> targets;
	targets.reserve(paths.size());
	for (const std::string& path : paths) {
		if (tm->IsGPUReady(path)) continue;
		uint64_t disk = 0, raw = 0;
		if (loc->IsPackMode()) {
			uint64_t offset = 0, compressed = 0, uncompressed = 0;
			uint8_t compression = 0;
			if (!loc->GetPackEntryInfoEx(path, offset, uncompressed, compressed, compression)) continue;
			disk = compressed;
			raw = uncompressed;
		} else {
			std::error_code ec;
			const auto size = std::filesystem::file_size(path, ec);
			if (ec) continue;
			disk = raw = size;
		}
		result.diskBytes += disk;
		result.rawBytes += raw;
		targets.push_back(path);
	}
	result.textureCount = static_cast<int>(targets.size());

	// それまでに積まれていた初期化コマンドを先に片付け、計測区間に混ぜない
	dxCore->FlushCommandList();

	const bool wasInBatch = ds->IsInBatch();
	const double wall0 = LoadProfiler::NowMs();
	const double cpu0 = LoadProfiler::ProcessCpuMs();
	const uint64_t cycles0 = LoadProfiler::ProcessCpuCycles();

	// DStorage はバッチでまとめて Enqueue → 1 回だけ待つ（シーンロードと同じ使い方）
	ds->BeginBatch();
	for (const std::string& path : targets) {
		tm->LoadTexture(path);
	}
	ds->EndBatchAndWait();
	// FS / Pack+CPU の転送（と DStorage 側の状態遷移バリア）を実行して GPU 完了まで待つ
	dxCore->FlushCommandList();

	result.wallMs = LoadProfiler::NowMs() - wall0;
	result.cpuMs = LoadProfiler::ProcessCpuMs() - cpu0;
	result.cpuCycles = LoadProfiler::ProcessCpuCycles() - cycles0;

	if (wasInBatch) ds->BeginBatch(); // 呼び出し元がバッチ中だったら元に戻す
	return result;
}

} // namespace TextureLoadBenchmark
