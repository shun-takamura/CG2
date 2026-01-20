#pragma once
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"

// 前方宣言
class ImGuiManager;

/// <summary>
/// 選択オブジェクト編集ウィンドウ
/// </summary>
class InspectorWindow : public IImGuiWindow {
public:
    InspectorWindow(ImGuiManager* manager) 
        : IImGuiWindow("Inspector"), manager_(manager) {}

protected:
    void OnDraw() override;

private:
    ImGuiManager* manager_;
};
