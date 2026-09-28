#pragma once
#include "LoadProfiler.h"
#include <string>
#include <vector>

class DirectXCore;

/// <summary>
/// テクスチャだけのロード時間を、現在のロード方式（FS / Pack+CPU / Pack+DStorage）で測る。
///
/// 計測区間は「読み込み開始 → GPU でテクスチャが使える状態になるまで」。
/// FS / Pack+CPU は転送をメインのコマンドリストに積むだけなので、最後に必ず実行と完了待ちを入れて
/// DStorage（完了まで待つ）と条件を揃える。これを入れないと FS が不当に速く見える。
/// </summary>
namespace TextureLoadBenchmark {

	// 3 方式で同じ集合を読むための一覧ファイル（1 行 1 パス、# 始まりはコメント）
	inline constexpr const char* kDefaultListPath = "Resources/Bench/texture_bench.txt";

	// 一覧を読む。無ければ pack の目次から .dds を列挙して書き出す（以降の FS 計測も同じ集合を使える）
	std::vector<std::string> LoadOrCreateList(const std::string& listPath);

	// 現在のロード方式名（[KPI] と同じ表記）
	std::string CurrentModeName();

	// paths を現在のロード方式で読み込み、GPU 完了まで待って計測する。
	// 計測前から読み込み済みのテクスチャは対象外（枚数・サイズにも含めない）。
	LoadProfiler::BenchResult Run(DirectXCore* dxCore, const std::vector<std::string>& paths);
}
