#include "HierarchyWindow.h"
#include "ImGuiManager.h"

void HierarchyWindow::OnDraw() {
#ifdef _DEBUG

    const auto& editables = manager_->GetEditables();
    IImGuiEditable* selected = manager_->GetSelected();

    // 検索フィルター
    static char searchBuffer[256] = "";
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();

    // オブジェクト数を表示
    ImGui::Text("Objects: %d", static_cast<int>(editables.size()));
    ImGui::Separator();

    // オブジェクト一覧
    for (IImGuiEditable* editable : editables) {
        if (!editable) continue;

        std::string name = editable->GetName();
        std::string typeName = editable->GetTypeName();

        // 検索フィルター適用
        if (searchBuffer[0] != '\0') {
            if (name.find(searchBuffer) == std::string::npos &&
                typeName.find(searchBuffer) == std::string::npos) {
                continue;
            }
        }

        // 選択状態を判定
        bool isSelected = (editable == selected);

        // 表示名: [型名] オブジェクト名
        std::string displayName = "[" + typeName + "] " + name;

        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
            manager_->SetSelected(editable);
        }
    }

#endif // DEBUG
}
