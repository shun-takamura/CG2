#include "InspectorWindow.h"
#include "ImGuiManager.h"

void InspectorWindow::OnDraw() {
    IImGuiEditable* selected = manager_->GetSelected();

    if (!selected) {
        ImGui::Text("No object selected");
        ImGui::TextDisabled("Select an object from Hierarchy");
        return;
    }

    // オブジェクト情報ヘッダー
    ImGui::Text("Name: %s", selected->GetName().c_str());
    ImGui::Text("Type: %s", selected->GetTypeName().c_str());
    ImGui::Separator();

    // 各オブジェクトが実装した編集UIを描画
    selected->OnImGuiInspector();
}
