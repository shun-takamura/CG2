#include "Debug.h"
#include <dxgidebug.h>
#include <Windows.h>
#include <wrl.h>

#pragma comment(lib, "dxguid.lib")

namespace Debug {

    void Log(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Info);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Info] " + message + "\n").c_str());
    }

    void LogWarning(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Warning);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Warning] " + message + "\n").c_str());
    }

    void LogError(const std::string& message) {
        // ImGui用のバッファに追加
        LogBuffer::Instance().Add(message, LogBuffer::Level::Error);

        // Visual Studioの出力ウィンドウにも出力
        OutputDebugStringA(("[Error] " + message + "\n").c_str());
    }

}
