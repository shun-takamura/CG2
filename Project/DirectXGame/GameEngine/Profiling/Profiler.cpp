#include "Profiler.h"

#include <Windows.h>
#include <sstream>
#include <iomanip>

#include "SessionLogger.h"

namespace {
    // フルパスからファイル名だけ取り出す（profile.log を読みやすくするため）
    const char* BaseName(const char* path) {
        if (!path) {
            return "";
        }
        const char* base = path;
        for (const char* p = path; *p; ++p) {
            if (*p == '\\' || *p == '/') {
                base = p + 1;
            }
        }
        return base;
    }

    int64_t NowTicks() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }
}

Profiler& Profiler::Instance() {
    static Profiler instance;
    return instance;
}

Profiler::Profiler() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    // tick → ms（1秒 = freq tick なので 1000ms / freq）
    tickToMs_ = 1000.0 / static_cast<double>(freq.QuadPart);
    windowStartTicks_ = NowTicks();
}

void Profiler::BeginSection(const char* name, const char* file, int line) {
    size_t index;
    auto it = indexByName_.find(name);
    if (it == indexByName_.end()) {
        index = stats_.size();
        SectionStat s;
        s.name = name;
        s.file = file;
        s.line = line;
        s.depth = static_cast<int>(activeStack_.size());
        stats_.push_back(std::move(s));
        indexByName_.emplace(name, index);
    } else {
        index = it->second;
    }

    activeStack_.push_back({ NowTicks(), index });
}

void Profiler::EndSection() {
    if (activeStack_.empty()) {
        return;
    }
    const int64_t now = NowTicks();

    const ActiveScope scope = activeStack_.back();
    activeStack_.pop_back();

    SectionStat& s = stats_[scope.index];
    s.cpuMs += static_cast<double>(now - scope.startTicks) * tickToMs_;
    s.calls += 1;
}

void Profiler::AddGpuMs(const char* name, double gpuMs) {
    auto it = indexByName_.find(name);
    if (it != indexByName_.end()) {
        stats_[it->second].gpuMs += gpuMs;
        return;
    }
    // CPU 区間に同名が無い GPU 専用区間（depth=0、cpu/calls は 0 のまま）
    const size_t index = stats_.size();
    SectionStat s;
    s.name = name;
    s.file = nullptr;
    s.line = 0;
    s.depth = 0;
    s.gpuMs = gpuMs;
    stats_.push_back(std::move(s));
    indexByName_.emplace(name, index);
}

void Profiler::Count(const char* name, int64_t n) {
    auto it = counterIndexByName_.find(name);
    if (it == counterIndexByName_.end()) {
        const size_t index = counters_.size();
        counters_.push_back({ name, n });
        counterIndexByName_.emplace(name, index);
    } else {
        counters_[it->second].value += n;
    }
}

void Profiler::SetGauge(const char* name, double value) {
    auto it = gaugeIndexByName_.find(name);
    if (it == gaugeIndexByName_.end()) {
        const size_t index = gauges_.size();
        gauges_.push_back({ name, value });
        gaugeIndexByName_.emplace(name, index);
    } else {
        gauges_[it->second].value = value;  // 最新で上書き
    }
}

void Profiler::EndFrame() {
    // このフレームの各区間をウィンドウ集計へ畳み込む
    for (const auto& s : stats_) {
        size_t index;
        auto it = windowIndexByName_.find(s.name);
        if (it == windowIndexByName_.end()) {
            index = windowStats_.size();
            WindowStat w;
            w.name = s.name;
            w.file = s.file;
            w.line = s.line;
            w.depth = s.depth;
            windowStats_.push_back(std::move(w));
            windowIndexByName_.emplace(s.name, index);
        } else {
            index = it->second;
        }
        WindowStat& w = windowStats_[index];
        w.presentFrames += 1;
        w.totalCalls += s.calls;
        w.sumCpuMs += s.cpuMs;
        if (s.cpuMs > w.maxCpuMs) {
            w.maxCpuMs = s.cpuMs;
        }
        w.sumGpuMs += s.gpuMs;
        if (s.gpuMs > w.maxGpuMs) {
            w.maxGpuMs = s.gpuMs;
        }
    }

    // このフレームの各カウンタをウィンドウ集計へ畳み込む
    for (const auto& c : counters_) {
        size_t index;
        auto it = windowCounterIndexByName_.find(c.name);
        if (it == windowCounterIndexByName_.end()) {
            index = windowCounters_.size();
            windowCounters_.push_back({ c.name, 0, 0, 0 });
            windowCounterIndexByName_.emplace(c.name, index);
        } else {
            index = it->second;
        }
        WindowCounter& wc = windowCounters_[index];
        wc.total += c.value;
        wc.presentFrames += 1;
        if (c.value > wc.maxPerFrame) {
            wc.maxPerFrame = c.value;
        }
    }

    // このフレームの各ゲージをウィンドウ集計へ畳み込む
    for (const auto& g : gauges_) {
        auto it = windowGaugeIndexByName_.find(g.name);
        if (it == windowGaugeIndexByName_.end()) {
            const size_t index = windowGauges_.size();
            windowGauges_.push_back({ g.name, g.value, g.value, g.value, g.value, 1 });
            windowGaugeIndexByName_.emplace(g.name, index);
        } else {
            WindowGauge& wg = windowGauges_[it->second];
            wg.last = g.value;
            wg.minv = (g.value < wg.minv) ? g.value : wg.minv;
            wg.maxv = (g.value > wg.maxv) ? g.value : wg.maxv;
            wg.sum += g.value;
            wg.count += 1;
        }
    }

    ++windowFrames_;

    ++frame_;
    stats_.clear();
    indexByName_.clear();
    counters_.clear();
    counterIndexByName_.clear();
    gauges_.clear();
    gaugeIndexByName_.clear();

    // 1秒経過していればウィンドウを書き出す
    const double elapsedMs = static_cast<double>(NowTicks() - windowStartTicks_) * tickToMs_;
    if (elapsedMs >= kWindowMs) {
        FlushWindow();
    }
}

void Profiler::Flush() {
    FlushWindow();
}

void Profiler::FlushWindow() {
    if (windowFrames_ == 0) {
        return;
    }

    const double windowSec =
        static_cast<double>(NowTicks() - windowStartTicks_) * tickToMs_ / 1000.0;

    for (const auto& w : windowStats_) {
        const double avgMs =
            (w.presentFrames > 0) ? (w.sumCpuMs / w.presentFrames) : 0.0;
        const double gpuAvgMs =
            (w.presentFrames > 0) ? (w.sumGpuMs / w.presentFrames) : 0.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3)
            << "frame=" << frame_
            << " window_s=" << std::setprecision(2) << windowSec
            << " frames=" << windowFrames_
            << " section=" << w.name
            << " depth=" << w.depth
            << " present=" << w.presentFrames
            << " calls=" << w.totalCalls
            << " sum_ms=" << std::setprecision(3) << w.sumCpuMs
            << " avg_ms=" << avgMs
            << " max_ms=" << w.maxCpuMs
            << " gpu_sum_ms=" << w.sumGpuMs
            << " gpu_avg_ms=" << gpuAvgMs
            << " gpu_max_ms=" << w.maxGpuMs
            << " file=" << BaseName(w.file)
            << " line=" << w.line;
        SessionLogger::Instance().Write(
            SessionLogger::Category::Profile, SessionLogger::Level::Trace, oss.str());
    }

    // カウンタ行（DrawCall 回数など。時間ではないので別フォーマット）
    for (const auto& wc : windowCounters_) {
        const double avgPerFrame =
            (windowFrames_ > 0) ? (static_cast<double>(wc.total) / windowFrames_) : 0.0;

        std::ostringstream oss;
        oss << "frame=" << frame_
            << " window_s=" << std::fixed << std::setprecision(2) << windowSec
            << " frames=" << windowFrames_
            << " counter=" << wc.name
            << " total=" << wc.total
            << " avg_per_frame=" << std::setprecision(2) << avgPerFrame
            << " max_per_frame=" << wc.maxPerFrame;
        SessionLogger::Instance().Write(
            SessionLogger::Category::Profile, SessionLogger::Level::Trace, oss.str());
    }

    // ゲージ行（RAM/VRAM/オブジェクト数など。現在値のレベル）
    for (const auto& g : windowGauges_) {
        const double avg = (g.count > 0) ? (g.sum / g.count) : 0.0;
        std::ostringstream oss;
        oss << "frame=" << frame_
            << " window_s=" << std::fixed << std::setprecision(2) << windowSec
            << " frames=" << windowFrames_
            << " gauge=" << g.name
            << " cur=" << std::setprecision(3) << g.last
            << " min=" << g.minv
            << " max=" << g.maxv
            << " avg=" << avg;
        SessionLogger::Instance().Write(
            SessionLogger::Category::Profile, SessionLogger::Level::Trace, oss.str());
    }

    windowStats_.clear();
    windowIndexByName_.clear();
    windowCounters_.clear();
    windowCounterIndexByName_.clear();
    windowGauges_.clear();
    windowGaugeIndexByName_.clear();
    windowFrames_ = 0;
    windowStartTicks_ = NowTicks();
}
