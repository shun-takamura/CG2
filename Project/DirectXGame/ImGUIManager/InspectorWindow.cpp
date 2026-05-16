#include "InspectorWindow.h"
#include "ImGuiManager.h"
#include "Components/EntityTag.h"

#include <cstring>

void InspectorWindow::OnDraw() {
#ifdef _DEBUG

    IImGuiEditable* selected = manager_->GetSelected();

    if (!selected) {
        ImGui::Text("No object selected");
        ImGui::TextDisabled("Select an object from Hierarchy");
        return;
    }

    // ----- 名前編集 -----
    {
        char nameBuf[256];
        const std::string current = selected->GetName();
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", current.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
            selected->SetName(nameBuf);
        }
    }

    ImGui::Text("Type: %s", selected->GetTypeName().c_str());

    // ----- Tag 選択 -----
    {
        EntityTag currentTag = selected->GetTag();
        std::string currentName(GetTagName(currentTag));
        if (ImGui::BeginCombo("Tag", currentName.c_str())) {
            for (int i = 0; i < static_cast<int>(EntityTag::Count); ++i) {
                EntityTag t = static_cast<EntityTag>(i);
                bool sel = (t == currentTag);
                if (ImGui::Selectable(std::string(GetTagName(t)).c_str(), sel)) {
                    selected->SetTag(t);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // ----- Debug表示ON/OFF -----
    {
        bool visible = selected->IsVisibleInEditor();
        if (ImGui::Checkbox("Visible (Debug)", &visible)) {
            selected->SetVisibleInEditor(visible);
        }
    }

    ImGui::Separator();

    // 各オブジェクトが実装した編集UIを描画
    selected->OnImGuiInspector();

#endif // DEBUG
}
