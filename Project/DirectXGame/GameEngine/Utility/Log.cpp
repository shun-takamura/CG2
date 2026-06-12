#include "Log.h"
#include <Windows.h>
#include "SessionLogger.h"

void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
    // 一般INFO（KPI・診断など）はセッションログへ集約する
    SessionLogger::Instance().Write(
        SessionLogger::Category::Session, SessionLogger::Level::Info, message);
}