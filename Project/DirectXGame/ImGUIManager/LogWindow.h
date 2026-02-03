#pragma once
#include "IImGuiWindow.h"
#include "../debug/LogBuffer.h"

/// <summary>
/// ログ表示ウィンドウ
/// </summary>
class LogWindow : public IImGuiWindow {
public:
    LogWindow() : IImGuiWindow("Log"), autoScroll_(true) {}

protected:
    void OnDraw() override {
#ifdef _DEBUG

        //// クリアボタンとオートスクロール設定
        //if (ImGui::Button("Clear")) {
        //    LogBuffer::Instance().Clear();
        //}
        //ImGui::SameLine();
        //ImGui::Checkbox("Auto Scroll", &autoScroll_);
        //ImGui::Separator();

        //// ログ表示エリア
        //ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, 
        //                  ImGuiWindowFlags_HorizontalScrollbar);

        //const auto& logs = LogBuffer::Instance().GetLogs();
        //for (const auto& log : logs) {
        //    // レベルに応じた色設定
        //    ImVec4 color;
        //    const char* prefix;
        //    switch (log.level) {
        //        case LogBuffer::Level::Info:
        //            color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // 白
        //            prefix = "[Info]";
        //            break;
        //        case LogBuffer::Level::Warning:
        //            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // 黄
        //            prefix = "[Warn]";
        //            break;
        //        case LogBuffer::Level::Error:
        //            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // 赤
        //            prefix = "[Error]";
        //            break;
        //        default:
        //            color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        //            prefix = "[?]";
        //            break;
        //    }

        //    ImGui::TextColored(color, "[%s] %s %s", 
        //                       log.timestamp.c_str(), prefix, log.message.c_str());
        //}

        //// オートスクロール
        //if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        //    ImGui::SetScrollHereY(1.0f);
        //}

        //ImGui::EndChild();

#endif // DEBUG
    }

private:
    bool autoScroll_;
};
