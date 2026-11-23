#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// ロード時間の計測（起動フェーズの区間と、テクスチャロードのベンチマーク結果）。
///
/// 毎フレームの区間計測（Profiler / PEPPER_SCOPE）とは別系統にしている。
/// ロードはフレームループの外（起動時・シーン切替時）で起きるので、フレーム単位の集計に乗らないため。
///
/// 実時間と一緒に「その区間でプロセスが使った CPU 時間（全スレッド合計）」も取る。
/// DirectStorage は GDeflate の解凍を GPU に任せるので、速さだけでなく CPU が空くことも数字で示せる。
/// シングルトンにする理由：ロードの各段階の時間を、起動からシーン切り替えまで 1 か所に記録するため。
/// </summary>
class LoadProfiler {
public:
	struct PhaseRecord {
		std::string name;
		double wallMs = 0.0;
		double cpuMs = 0.0;
		uint64_t cpuCycles = 0;
	};

	struct BenchResult {
		std::string mode;          // "FS" / "Pack+CPU" / "Pack+DStorage"
		int      textureCount = 0; // 実際に読み込んだ枚数（計測前から読み込み済みのものは含めない）
		uint64_t diskBytes = 0;    // ストレージから読んだ量（pack は圧縮後サイズ）
		uint64_t rawBytes = 0;     // 解凍後（GPU に載る DDS）のサイズ
		double   wallMs = 0.0;     // 読み込み開始 → GPU で使える状態になるまで
		double   cpuMs = 0.0;      // 同区間のプロセス CPU 時間（全スレッド合計）
		uint64_t cpuCycles = 0;   // 同区間のプロセス CPU サイクル数（全スレッド合計。cpuMs より高分解能）

		/// <summary>解凍後のサイズを実時間で割った読み込みの速さ</summary>
		/// <returns>速さ [MB/s]（実時間がほぼ 0 なら 0）</returns>
		double ThroughputMBps() const;
	};

	/// <summary>唯一のインスタンス</summary>
	/// <returns>インスタンス</returns>
	static LoadProfiler* GetInstance();

	/// <summary>区間計測を始める。同名の Begin/End を対にする（入れ子可）</summary>
	/// <param name="name">区間の名前</param>
	void BeginPhase(const std::string& name);
	/// <summary>同名で一番内側の開いている区間を閉じ、実時間と CPU 時間を記録する（開いていなければ何もしない）</summary>
	/// <param name="name">区間の名前</param>
	void EndPhase(const std::string& name);

	/// <summary>ベンチマークの結果を足す</summary>
	/// <param name="result">結果</param>
	void AddBenchResult(const BenchResult& result);

	/// <summary>閉じた区間の記録</summary>
	/// <returns>記録の一覧</returns>
	const std::vector<PhaseRecord>& GetPhases() const { return phases_; }
	/// <summary>ベンチマークの結果</summary>
	/// <returns>結果の一覧</returns>
	const std::vector<BenchResult>& GetBenchResults() const { return benchResults_; }

	/// <summary>
	/// ベンチ結果を CSV に 1 行ずつ追記する（ファイルが無いときだけヘッダを書く。列構成が違う旧ファイルは .old.csv へ退避する）
	/// </summary>
	/// <param name="path">CSV のパス（フォルダが無ければ作る）</param>
	void AppendBenchCsv(const std::string& path) const;

	/// <summary>区間とベンチ結果を profile.log の LOAD 行とデバッグ出力の [LOAD] 行に出す</summary>
	void LogSummary() const;

	/// <summary>
	/// 計測モードの切り替え。計測用の実行中は、シーンロード区間の最後で GPU 完了まで待って公平に比べる。
	/// 通常プレイでは余計な同期を入れないよう既定 false
	/// </summary>
	/// <param name="enable">計測モードなら true</param>
	void SetMeasurementMode(bool enable) { measurementMode_ = enable; }
	/// <summary>計測モードか</summary>
	/// <returns>計測モードなら true</returns>
	bool IsMeasurementMode() const { return measurementMode_; }

	/// <summary>今の実時間（steady_clock）</summary>
	/// <returns>時刻 [ms]（差分で使う）</returns>
	static double NowMs();
	/// <summary>プロセスの累計 CPU 時間（Kernel+User、全スレッド）</summary>
	/// <returns>CPU 時間 [ms]（取れなければ 0）</returns>
	static double ProcessCpuMs();
	/// <summary>
	/// プロセスの累計 CPU サイクル数（全スレッド）。GetProcessTimes はタイマー刻み（約15.6ms）単位で
	/// しか増えず数十msの区間では粗すぎるため併用する。周波数が変動するので ms 換算せず比率で使う
	/// </summary>
	/// <returns>サイクル数（取れなければ 0）</returns>
	static uint64_t ProcessCpuCycles();

	/// <summary>ビルド構成名（CSV に残して Debug の数値が混ざらないようにする）</summary>
	/// <returns>"Debug" / "Development" / "Release"</returns>
	static const char* BuildConfigName();

private:
	LoadProfiler() = default;
	~LoadProfiler() = default;
	LoadProfiler(const LoadProfiler&) = delete;
	LoadProfiler& operator=(const LoadProfiler&) = delete;

	struct OpenPhase {
		std::string name;
		double wallStartMs = 0.0;
		double cpuStartMs = 0.0;
		uint64_t cyclesStart = 0;
	};
	std::vector<OpenPhase>   open_;
	std::vector<PhaseRecord> phases_;
	std::vector<BenchResult> benchResults_;
	bool measurementMode_ = false;
};

/// スコープの間だけ区間計測する
class ScopedLoadPhase {
public:
	/// <summary>区間計測を始める</summary>
	/// <param name="name">区間の名前</param>
	explicit ScopedLoadPhase(std::string name) : name_(std::move(name)) {
		LoadProfiler::GetInstance()->BeginPhase(name_);
	}
	/// <summary>区間計測を閉じる</summary>
	~ScopedLoadPhase() { LoadProfiler::GetInstance()->EndPhase(name_); }
	ScopedLoadPhase(const ScopedLoadPhase&) = delete;
	ScopedLoadPhase& operator=(const ScopedLoadPhase&) = delete;

private:
	std::string name_;
};

#define LOAD_PHASE_CONCAT_INNER(a, b) a##b
#define LOAD_PHASE_CONCAT(a, b) LOAD_PHASE_CONCAT_INNER(a, b)
#define LOAD_PHASE(name) ScopedLoadPhase LOAD_PHASE_CONCAT(loadPhase_, __LINE__)(name)
