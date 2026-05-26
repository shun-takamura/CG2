#include "EffectHierarchyWindow.h"
#include "ImGuiManager.h"
#include "Effect/EffectEditorWindow.h"
#include "Effect/EffectComponentEditable.h"
#include "imgui.h"

EffectHierarchyWindow::EffectHierarchyWindow(ImGuiManager* mgr, EffectEditorWindow* editor)
    : IImGuiWindow("EffectHierarchy"), mgr_(mgr), editor_(editor) {
    SetInitialSize(ImVec2(260, 360));

}

void EffectHierarchyWindow::OnDraw() {
#ifdef _DEBUG
    if (!editor_) {
        ImGui::Text("(no editor)");
        return;
    }

    ImGui::Text("Editing: %s", editor_->GetCurrentName().c_str());
    ImGui::Separator();

    const auto& editables = editor_->GetEditables();
    IImGuiEditable* selected = mgr_ ? mgr_->GetSelected() : nullptr;

    auto drawGroup = [&](const char* label, EffectComponentEditable::Kind kind) {
        // グループのコンポーネント数
        int count = 0;
        for (auto& e : editables) if (e->GetKind() == kind) ++count;

        char header[64];
        std::snprintf(header, sizeof(header), "%s (%d)", label, count);

        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& e : editables) {
                if (e->GetKind() != kind) continue;
                bool isSelected = (e.get() == selected);
                std::string name = e->GetName();

                ImGui::PushID(e.get());

                // ×ボタン分の幅を残して Selectable を伸ばす
                const float availW = ImGui::GetContentRegionAvail().x;
                const float xButtonW = ImGui::GetFrameHeight();
                if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - xButtonW - 4.0f, 0))) {
                    if (mgr_) mgr_->SetSelected(e.get());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    editor_->RemoveComponent(e->GetKind(), e->GetIndex());
                }
                ImGui::PopID();
            }
        }
        };

    drawGroup("Primitives", EffectComponentEditable::Kind::Primitive);
    drawGroup("Particles",  EffectComponentEditable::Kind::Particle);
    drawGroup("Lights",     EffectComponentEditable::Kind::Light);
    drawGroup("Sounds",     EffectComponentEditable::Kind::Sound);
#endif
}
