#pragma once
#include <string>
#include "LogBuffer.h"

/// <summary>
/// デバッグ出力用ユーティリティクラス
/// </summary>
namespace Debug {
    /// <summary>
    /// 情報ログを出力
    /// </summary>
    void Log(const std::string& message);

    /// <summary>
    /// 警告ログを出力
    /// </summary>
    void LogWarning(const std::string& message);

    /// <summary>
    /// エラーログを出力
    /// </summary>
    void LogError(const std::string& message);
}
