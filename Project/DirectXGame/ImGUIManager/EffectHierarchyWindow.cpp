#include "EffectHierarchyWindow.h"
#include "IEditorSelection.h"
#include "Effect/EffectEditorWindow.h"
#include "Effect/EffectComponentEditable.h"
#include "imgui.h"

namespace {
    // ウィンドウの初めの大きさ [px]
    constexpr int kInitialWidth = 260;
    constexpr int kInitialHeight = 360;
    // グループの見出しの文字列のバッファの大きさ [byte]
    constexpr int kGroupHeaderBufferSize = 64;
    // Selectable の右に並べるボタンの数（複製 + / 削除 x）と、その分に足す余白 [px]
    constexpr float kTrailingButtonCount = 2.0f;
    constexpr float kTrailingButtonMargin = 8.0f;
}

EffectHierarchyWindow::EffectHierarchyWindow(IEditorSelection* selection, EffectEditorWindow* editor)
    : IImGuiWindow("EffectHierarchy"), selection_(selection), editor_(editor) {
    SetInitialSize(ImVec2(kInitialWidth, kInitialHeight));

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
    IImGuiEditable* selected = selection_ ? selection_->GetSelected() : nullptr;

    auto drawGroup = [&](const char* label, EffectComponentEditable::Kind kind) {
        // グループのコンポーネント数
        int count = 0;
        for (auto& e : editables) if (e->GetKind() == kind) ++count;

        char header[kGroupHeaderBufferSize];
        std::snprintf(header, sizeof(header), "%s (%d)", label, count);

        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& e : editables) {
                if (e->GetKind() != kind) continue;
                bool isSelected = (e.get() == selected);
                std::string name = e->GetName();

                ImGui::PushID(e.get());

                // 複製(+)・削除(x) ボタン分の幅を残して Selectable を伸ばす
                const float availW = ImGui::GetContentRegionAvail().x;
                const float btnW = ImGui::GetFrameHeight();
                if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - btnW * kTrailingButtonCount - kTrailingButtonMargin, 0))) {
                    if (selection_) selection_->SetSelected(e.get());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    editor_->DuplicateComponent(e->GetKind(), e->GetIndex());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("複製（同じ設定をもう1つ追加。方向違いの翼などに）");
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
    drawGroup("Ribbons",    EffectComponentEditable::Kind::Ribbon);
#endif
}
