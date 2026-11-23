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

	/// <summary>
	/// 計測する .dds の一覧を読む。無ければ .dds を列挙し、pack モードの時だけ一覧ファイルに書き出す
	/// （以降の FS 計測も同じ集合を使える。FS の走査結果は集合が違いうるので保存しない）
	/// </summary>
	/// <param name="listPath">一覧ファイルのパス（1 行 1 パス、# 始まりはコメント）</param>
	/// <returns>テクスチャのパスの一覧</returns>
	std::vector<std::string> LoadOrCreateList(const std::string& listPath);

	/// <summary>現在のロード方式名（[KPI] と同じ表記）</summary>
	/// <returns>"FS" / "Pack+CPU" / "Pack+DStorage"</returns>
	std::string CurrentModeName();

	/// <summary>
	/// paths を現在のロード方式で読み込み、GPU 完了まで待って計測する。
	/// 計測前から読み込み済みのテクスチャは対象外（枚数・サイズにも含めない）
	/// </summary>
	/// <param name="dxCore">DirectXCore（nullptr ならロード方式名だけ入れて返す）</param>
	/// <param name="paths">読み込むテクスチャのパス</param>
	/// <returns>計測結果</returns>
	LoadProfiler::BenchResult Run(DirectXCore* dxCore, const std::vector<std::string>& paths);
}
