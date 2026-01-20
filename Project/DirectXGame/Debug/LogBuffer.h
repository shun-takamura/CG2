#pragma once
#include <string>
#include <vector>
#include <mutex>

/// <summary>
/// ログメッセージを保持するバッファクラス（シングルトン）
/// </summary>
class LogBuffer {
public:
    /// <summary>
    /// ログレベル
    /// </summary>
    enum class Level {
        Info,
        Warning,
        Error
    };

    /// <summary>
    /// ログエントリ
    /// </summary>
    struct LogEntry {
        Level level;
        std::string message;
        std::string timestamp;
    };

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static LogBuffer& Instance();

    /// <summary>
    /// ログを追加
    /// </summary>
    void Add(const std::string& message, Level level = Level::Info);

    /// <summary>
    /// ログをクリア
    /// </summary>
    void Clear();

    /// <summary>
    /// ログを取得
    /// </summary>
    const std::vector<LogEntry>& GetLogs() const { return logs_; }

    /// <summary>
    /// 最大ログ数を設定
    /// </summary>
    void SetMaxLogs(size_t max) { maxLogs_ = max; }

private:
    LogBuffer() = default;
    ~LogBuffer() = default;
    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;

    std::vector<LogEntry> logs_;
    size_t maxLogs_ = 1000;
    mutable std::mutex mutex_;

    std::string GetCurrentTimestamp() const;
};
