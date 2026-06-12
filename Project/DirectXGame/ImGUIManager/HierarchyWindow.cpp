#include "HierarchyWindow.h"
#include "ImGuiManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Components/EntityTag.h"

#include <array>
#include <vector>
#include <cstdio>

void HierarchyWindow::OnDraw() {
#ifdef _DEBUG

    const auto& editables = manager_->GetEditables();
    IImGuiEditable* selected = manager_->GetSelected();
    Scene* scene = SceneManager::GetInstance()->GetCurrentScene();

    // 検索フィルター
    static char searchBuffer[256] = "";
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();

    ImGui::Text("Objects: %d", static_cast<int>(editables.size()));
    ImGui::Separator();

    // ===== タグごとにグルーピング =====
    constexpr int kTagCount = static_cast<int>(EntityTag::Count);
    std::array<std::vector<IImGuiEditable*>, kTagCount> grouped{};
    for (IImGuiEditable* e : editables) {
        if (!e) continue;
        const int idx = static_cast<int>(e->GetTag());
        if (idx >= 0 && idx < kTagCount) {
            grouped[idx].push_back(e);
        }
    }

    const float kRowButtonWidth = ImGui::GetFrameHeight();
    int idCounter = 0;

    auto drawRow = [&](IImGuiEditable* editable) {
        std::string name = editable->GetName();
        std::string typeName = editable->GetTypeName();

        // 検索フィルター
        if (searchBuffer[0] != '\0') {
            if (name.find(searchBuffer) == std::string::npos &&
                typeName.find(searchBuffer) == std::string::npos) {
                return;
            }
        }

        ImGui::PushID(idCounter++);

        // 目玉トグル（Debug表示ON/OFF）
        bool visible = editable->IsVisibleInEditor();
        if (ImGui::Checkbox("##visible", &visible)) {
            editable->SetVisibleInEditor(visible);
        }
        ImGui::SameLine();

        const bool isSelected = (editable == selected);
        const bool isRemovable = scene && scene->IsDynamicObject(editable);
        std::string displayName = "[" + typeName + "] " + name;

        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float selectableWidth = isRemovable
            ? availWidth - kRowButtonWidth - ImGui::GetStyle().ItemSpacing.x
            : 0.0f;

        if (ImGui::Selectable(displayName.c_str(), isSelected,
            ImGuiSelectableFlags_None, ImVec2(selectableWidth, 0))) {
            manager_->SetSelected(editable);
        }

        if (isRemovable) {
            ImGui::SameLine();
            if (ImGui::Button("x", ImVec2(kRowButtonWidth, 0))) {
                if (scene) {
                    if (typeName == "Sprite") {
                        scene->RemoveDynamicSprite(name);
                    } else if (typeName == "AnimatedObject3D") {
                        scene->RemoveDynamicAnimated(name);
                    } else if (typeName == "Primitive") {
                        scene->RemoveDynamicPrimitive(name);
                    } else if (typeName == "Spline") {
                        scene->RemoveDynamicSpline(name);
                    } else {
                        scene->RemoveDynamicObject(name);
                    }
                }
            }
        }

        ImGui::PopID();
    };

    // タグ順に CollapsingHeader を描画
    for (int i = 0; i < kTagCount; ++i) {
        const auto& list = grouped[i];
        if (list.empty()) continue;

        EntityTag tag = static_cast<EntityTag>(i);
        float r, g, b, a;
        GetTagColor(tag, r, g, b, a);

        char header[128];
        std::snprintf(header, sizeof(header), "%s (%d)##tag_%d",
            std::string(GetTagName(tag)).c_str(),
            static_cast<int>(list.size()),
            i);

        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(r * 0.40f, g * 0.40f, b * 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(r * 0.55f, g * 0.55f, b * 0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(r * 0.70f, g * 0.70f, b * 0.70f, 1.0f));

        // None 以外はデフォルトで開く
        bool defaultOpen = (tag != EntityTag::None);
        ImGui::SetNextItemOpen(defaultOpen, ImGuiCond_FirstUseEver);
        const bool opened = ImGui::CollapsingHeader(header);
        ImGui::PopStyleColor(3);

        if (opened) {
            ImGui::Indent();
            for (IImGuiEditable* e : list) {
                drawRow(e);
            }
            ImGui::Unindent();
        }
    }

#endif // DEBUG
}
