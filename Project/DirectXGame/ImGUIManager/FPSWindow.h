#pragma once
#include "IImGuiWindow.h"

/// <summary>
/// FPS表示ウィンドウ
/// </summary>
class FPSWindow : public IImGuiWindow {
public:

    /// <summary>"FPS" ウィンドウとして作る</summary>
    FPSWindow() : IImGuiWindow("FPS") {}

protected:
    /// <summary>FPS・1 フレームの時間と、直近 100 フレームの FPS のグラフを出す</summary>
    void OnDraw() override {
#ifdef _DEBUG

        float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame Time: %.3f ms", kMsPerSecond / fps);

        // FPS履歴のグラフ表示
        static float fpsHistory[kHistorySize] = {};
        static int fpsHistoryIndex = 0;

        fpsHistory[fpsHistoryIndex] = fps;
        fpsHistoryIndex = (fpsHistoryIndex + 1) % kHistorySize;

        ImGui::PlotLines("##FPSGraph", fpsHistory, kHistorySize, fpsHistoryIndex, 
                         nullptr, 0.0f, kGraphMaxFps, ImVec2(0, kGraphHeight));

#endif // DEBUG
    }

private:
    // 1 秒のミリ秒数
    static constexpr float kMsPerSecond = 1000.0f;
    // FPS のグラフに残すフレーム数
    static constexpr int kHistorySize = 100;
    // FPS のグラフの上限 [FPS] と高さ [px]
    static constexpr float kGraphMaxFps = 120.0f;
    static constexpr int kGraphHeight = 50;
};
