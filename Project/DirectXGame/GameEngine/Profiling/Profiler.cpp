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
    }
    ++windowFrames_;

    ++frame_;
    stats_.clear();
    indexByName_.clear();

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
            << " gpu_ms=0.000"
            << " file=" << BaseName(w.file)
            << " line=" << w.line;
        SessionLogger::Instance().Write(
            SessionLogger::Category::Profile, SessionLogger::Level::Trace, oss.str());
    }

    windowStats_.clear();
    windowIndexByName_.clear();
    windowFrames_ = 0;
    windowStartTicks_ = NowTicks();
}
