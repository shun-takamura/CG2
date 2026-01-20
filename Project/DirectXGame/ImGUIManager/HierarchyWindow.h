#pragma once
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include <vector>

// 前方宣言
class ImGuiManager;

/// <summary>
/// オブジェクト一覧表示ウィンドウ
/// </summary>
class HierarchyWindow : public IImGuiWindow {
public:
    HierarchyWindow(ImGuiManager* manager) 
        : IImGuiWindow("Hierarchy"), manager_(manager) {}

protected:
    void OnDraw() override;

private:
    ImGuiManager* manager_;
};
