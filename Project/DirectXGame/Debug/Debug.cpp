#include "Debug.h"
#include <dxgidebug.h>
#include <Windows.h>
#include <wrl.h>
#include "SessionLogger.h"

#pragma comment(lib, "dxguid.lib")

namespace Debug {

    void Log(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Info);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Info] " + message + "\n").c_str());

        // セッションログ（session.log）にも残す
        SessionLogger::Instance().Write(
            SessionLogger::Category::Session, SessionLogger::Level::Info, message);
    }

    void LogWarning(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Warning);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Warning] " + message + "\n").c_str());

        // 警告以上はエラーログ（error.log）へ
        SessionLogger::Instance().Write(
            SessionLogger::Category::Error, SessionLogger::Level::Warn, message);
    }

    void LogError(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Error);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Error] " + message + "\n").c_str());

        // エラーログ（error.log）へ
        SessionLogger::Instance().Write(
            SessionLogger::Category::Error, SessionLogger::Level::Error, message);
    }

}
