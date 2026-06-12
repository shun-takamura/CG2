#include "SessionLogger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

#include "CrashHandler.h"

namespace {
    // カテゴリ → ファイル名 / レコード上の表記
    const char* CategoryFileName(SessionLogger::Category c) {
        switch (c) {
        case SessionLogger::Category::Input:   return "input.log";
        case SessionLogger::Category::State:   return "state.log";
        case SessionLogger::Category::Event:   return "event.log";
        case SessionLogger::Category::Gfx:     return "gfx.log";
        case SessionLogger::Category::Error:   return "error.log";
        case SessionLogger::Category::Session: return "session.log";
        default:                               return "unknown.log";
        }
    }

    const char* CategoryTag(SessionLogger::Category c) {
        switch (c) {
        case SessionLogger::Category::Input:   return "INPUT";
        case SessionLogger::Category::State:   return "STATE";
        case SessionLogger::Category::Event:   return "EVENT";
        case SessionLogger::Category::Gfx:     return "GFX";
        case SessionLogger::Category::Error:   return "ERROR";
        case SessionLogger::Category::Session: return "SESSION";
        default:                               return "UNKNOWN";
        }
    }

    const char* LevelTag(SessionLogger::Level l) {
        switch (l) {
        case SessionLogger::Level::Critical: return "CRITICAL";
        case SessionLogger::Level::Error:    return "ERROR";
        case SessionLogger::Level::Warn:     return "WARN";
        case SessionLogger::Level::Info:     return "INFO";
        case SessionLogger::Level::Debug:    return "DEBUG";
        case SessionLogger::Level::Trace:    return "TRACE";
        default:                             return "?";
        }
    }

    // HH:MM:SS.mmm
    std::string TimeStamp() {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm lt;
        localtime_s(&lt, &t);

        std::ostringstream oss;
        oss << std::put_time(&lt, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    // YYYY-MM-DD_HHMMSS
    std::string FolderStamp() {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm lt;
        localtime_s(&lt, &t);
        std::ostringstream oss;
        oss << std::put_time(&lt, "%Y-%m-%d_%H%M%S");
        return oss.str();
    }
}

SessionLogger& SessionLogger::Instance() {
    static SessionLogger instance;
    return instance;
}

SessionLogger::~SessionLogger() {
    Finalize();
}

void SessionLogger::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    // デフォルトの最低レベル: Debugビルドは全開(Trace)、Releaseは深刻なものだけ(Critical)
#ifdef _DEBUG
    const Level defaultLevel = Level::Trace;
#else
    const Level defaultLevel = Level::Critical;
#endif
    minLevels_.fill(defaultLevel);

    sessionDir_ = std::string("Logs/") + FolderStamp();

    std::error_code ec;
    std::filesystem::create_directories(sessionDir_, ec);

    for (size_t i = 0; i < kCategoryCount; ++i) {
        const std::string path =
            sessionDir_ + "/" + CategoryFileName(static_cast<Category>(i));
        files_[i].open(path, std::ios::out | std::ios::trunc);
    }

    // クラッシュダンプの出力先を、このセッションフォルダに向ける
    CrashHandler::SetDumpDir(sessionDir_);

    initialized_ = true;
}

void SessionLogger::Write(Category category, Level level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    const size_t idx = static_cast<size_t>(category);
    // 「出力する最低レベル」より深刻でない(=値が大きい)ものは捨てる
    if (static_cast<int>(level) > static_cast<int>(minLevels_[idx])) {
        return;
    }

    std::ofstream& out = files_[idx];
    if (!out.is_open()) {
        return;
    }

    // 末尾の改行は1行1レコードの整形のため取り除く
    std::string body = message;
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
        body.pop_back();
    }

    out << TimeStamp() << ' ' << CategoryTag(category) << ' '
        << LevelTag(level) << ' ' << body << '\n';
    out.flush();
}

void SessionLogger::SetCategoryLevel(Category category, Level minLevel) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevels_[static_cast<size_t>(category)] = minLevel;
}

void SessionLogger::Finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }
    for (auto& f : files_) {
        if (f.is_open()) {
            f.flush();
            f.close();
        }
    }
    initialized_ = false;
}
