#pragma once

#include <string>
#include <vector>

/// <summary>
/// ウェーブの1エントリ。
/// 「time 秒後に、splineName のスプラインの始点から prefab を spawn し、speed [t/sec] で進ませる」。
/// </summary>
struct WaveEntry {
	float       time = 0.0f;        // ウェーブ開始からの発火秒
	std::string prefab;             // PrefabManager に登録された名前（例: "dummy_enemy"）
	std::string splineName;         // シーン内 SplineCurveActor の名前（EnemyPathSpline タグ想定）
	float       speed = 0.1f;       // スプラインの t/sec
	bool        removeAtEnd = true; // t>=1 で自動消滅するか（false ならスプライン終端で停止）
};

/// <summary>
/// 1 wave（=1 ファイル）の定義。
/// </summary>
struct WaveDef {
	std::string name;
	std::vector<WaveEntry> entries;
};

namespace WaveDefIO {
	/// <summary>
	/// ファイルから WaveDef を読み込む。失敗時は false。
	/// </summary>
	bool LoadFromFile(const std::string& filePath, WaveDef& out);

	/// <summary>
	/// WaveDef をファイルに保存する。失敗時は false。
	/// </summary>
	bool SaveToFile(const std::string& filePath, const WaveDef& def);
}
