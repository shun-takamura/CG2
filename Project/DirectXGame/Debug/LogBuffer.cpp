#include "LogBuffer.h"
#include <chrono>
#include <iomanip>
#include <sstream>

LogBuffer& LogBuffer::Instance() {
    static LogBuffer instance;
    return instance;
}

void LogBuffer::Add(const std::string& message, Level level) {
    std::lock_guard<std::mutex> lock(mutex_);

    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.timestamp = GetCurrentTimestamp();

    logs_.push_back(entry);

    // 最大数を超えたら古いログを削除
    while (logs_.size() > maxLogs_) {
        logs_.erase(logs_.begin());
    }
}

void LogBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.clear();
}

std::string LogBuffer::GetCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm localTime;
    localtime_s(&localTime, &time);

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}
