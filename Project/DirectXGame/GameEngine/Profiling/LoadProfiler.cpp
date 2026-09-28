#include "LoadProfiler.h"

#include "Log.h"
#include "SessionLogger.h"

#include <Windows.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>

namespace {
	constexpr double kBytesPerMB = 1024.0 * 1024.0;
}

double LoadProfiler::BenchResult::ThroughputMBps() const {
	return (wallMs > 1e-6) ? (static_cast<double>(rawBytes) / kBytesPerMB) / (wallMs / 1000.0) : 0.0;
}

LoadProfiler* LoadProfiler::GetInstance() {
	static LoadProfiler instance;
	return &instance;
}

double LoadProfiler::NowMs() {
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

double LoadProfiler::ProcessCpuMs() {
	FILETIME ftCreate, ftExit, ftKernel, ftUser;
	if (!GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser)) return 0.0;
	ULARGE_INTEGER k{}, u{};
	k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
	u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
	return static_cast<double>(k.QuadPart + u.QuadPart) / 10000.0; // 100ns → ms
}

uint64_t LoadProfiler::ProcessCpuCycles() {
	ULONG64 cycles = 0;
	if (!QueryProcessCycleTime(GetCurrentProcess(), &cycles)) return 0;
	return static_cast<uint64_t>(cycles);
}

const char* LoadProfiler::BuildConfigName() {
#if defined(_DEBUG)
	return "Debug";
#elif defined(USE_PEPPER)
	return "Development";
#else
	return "Release";
#endif
}

void LoadProfiler::BeginPhase(const std::string& name) {
	open_.push_back({ name, NowMs(), ProcessCpuMs(), ProcessCpuCycles() });
}

void LoadProfiler::EndPhase(const std::string& name) {
	// 後ろから探す（入れ子で同名が重なっても内側から閉じる）
	for (auto it = open_.rbegin(); it != open_.rend(); ++it) {
		if (it->name != name) continue;
		PhaseRecord rec;
		rec.name = name;
		rec.wallMs = NowMs() - it->wallStartMs;
		rec.cpuMs = ProcessCpuMs() - it->cpuStartMs;
		rec.cpuCycles = ProcessCpuCycles() - it->cyclesStart;
		phases_.push_back(std::move(rec));
		open_.erase(std::next(it).base());
		return;
	}
}

void LoadProfiler::AddBenchResult(const BenchResult& result) {
	benchResults_.push_back(result);
}

void LoadProfiler::AppendBenchCsv(const std::string& path) const {
	if (benchResults_.empty()) return;

	std::error_code ec;
	const std::filesystem::path p(path);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	constexpr const char* kHeader =
		"timestamp,config,mode,textures,disk_mb,raw_mb,wall_ms,cpu_ms,cpu_mcycles,throughput_mbps";
	// 列構成が違う旧ファイルは混ぜずに退避する（集計スクリプトが列ずれで誤読しないように）
	if (std::filesystem::exists(p, ec)) {
		std::string firstLine;
		if (std::ifstream ifs(p); ifs) std::getline(ifs, firstLine);
		if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
		if (firstLine != kHeader) {
			std::filesystem::path old = p;
			old.replace_extension(".old.csv");
			std::filesystem::rename(p, old, ec);
		}
	}
	const bool writeHeader = !std::filesystem::exists(p, ec);

	std::ofstream ofs(p, std::ios::app);
	if (!ofs) {
		Log("[LOAD] failed to open " + path + "\n");
		return;
	}
	if (writeHeader) {
		ofs << kHeader << "\n";
	}

	const std::time_t now = std::time(nullptr);
	std::tm lt{};
	localtime_s(&lt, &now);
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &lt);

	for (const BenchResult& r : benchResults_) {
		ofs << std::format("{},{},{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.1f},{:.1f}\n",
			stamp, BuildConfigName(), r.mode, r.textureCount,
			static_cast<double>(r.diskBytes) / kBytesPerMB,
			static_cast<double>(r.rawBytes) / kBytesPerMB,
			r.wallMs, r.cpuMs, static_cast<double>(r.cpuCycles) / 1.0e6, r.ThroughputMBps());
	}
}

void LoadProfiler::LogSummary() const {
	// profile.log（P.E.P.P.E.R.）にも key=value 形式で残す。フレーム区間の PROFILE 行とは別の LOAD 行
	auto& logger = SessionLogger::Instance();
	for (const PhaseRecord& p : phases_) {
		logger.Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=phase name={} wall_ms={:.2f} cpu_ms={:.2f} cpu_mcycles={:.1f}",
				p.name, p.wallMs, p.cpuMs, static_cast<double>(p.cpuCycles) / 1.0e6));
	}
	for (const BenchResult& r : benchResults_) {
		logger.Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=bench config={} mode={} textures={} disk_mb={:.2f} raw_mb={:.2f} wall_ms={:.2f} cpu_ms={:.2f} cpu_mcycles={:.1f} throughput_mbps={:.1f}",
				BuildConfigName(), r.mode, r.textureCount,
				static_cast<double>(r.diskBytes) / kBytesPerMB, static_cast<double>(r.rawBytes) / kBytesPerMB,
				r.wallMs, r.cpuMs, static_cast<double>(r.cpuCycles) / 1.0e6, r.ThroughputMBps()));
	}

	for (const PhaseRecord& p : phases_) {
		Log(std::format("[LOAD] phase={} wall={:.1f}ms cpu={:.1f}ms cycles={:.1f}M\n",
			p.name, p.wallMs, p.cpuMs, static_cast<double>(p.cpuCycles) / 1.0e6));
	}
	for (const BenchResult& r : benchResults_) {
		Log(std::format(
			"[LOAD] bench config={} mode={} textures={} disk={:.1f}MB raw={:.1f}MB wall={:.1f}ms cpu={:.1f}ms cycles={:.1f}M ({:.1f}MB/s)\n",
			BuildConfigName(), r.mode, r.textureCount,
			static_cast<double>(r.diskBytes) / kBytesPerMB,
			static_cast<double>(r.rawBytes) / kBytesPerMB,
			r.wallMs, r.cpuMs, static_cast<double>(r.cpuCycles) / 1.0e6, r.ThroughputMBps()));
	}
}
