#pragma once
#include "IImGuiWindow.h"

/// <summary>
/// FPS表示ウィンドウ
/// </summary>
class FPSWindow : public IImGuiWindow {
public:

    FPSWindow() : IImGuiWindow("FPS") {}

protected:
    void OnDraw() override {
#ifdef DEBUG

        float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / fps);

        // FPS履歴のグラフ表示
        static float fpsHistory[100] = {};
        static int fpsHistoryIndex = 0;

        fpsHistory[fpsHistoryIndex] = fps;
        fpsHistoryIndex = (fpsHistoryIndex + 1) % 100;

        ImGui::PlotLines("##FPSGraph", fpsHistory, 100, fpsHistoryIndex, 
                         nullptr, 0.0f, 120.0f, ImVec2(0, 50));

#endif // DEBUG
    }
};
