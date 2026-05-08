#include "HierarchyWindow.h"
#include "ImGuiManager.h"
#include "SceneManager.h"
#include "BaseScene.h"

void HierarchyWindow::OnDraw() {
#ifdef _DEBUG

    const auto& editables = manager_->GetEditables();
    IImGuiEditable* selected = manager_->GetSelected();

    // 現在のシーン（× ボタンで Remove を呼ぶ対象）
    BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();

    // 検索フィルター
    static char searchBuffer[256] = "";
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();

    // オブジェクト数を表示
    ImGui::Text("Objects: %d", static_cast<int>(editables.size()));
    ImGui::Separator();

    // 削除ボタンの幅を確保した上で行を描画する
    const float kRemoveButtonWidth = ImGui::GetFrameHeight();

    int idCounter = 0;
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

        ImGui::PushID(idCounter++);

        const bool isSelected = (editable == selected);
        const bool isRemovable = scene && scene->IsDynamicObject(editable);
        std::string displayName = "[" + typeName + "] " + name;

        // Selectable は × ボタンの分の幅を残して描画
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float selectableWidth = isRemovable
            ? availWidth - kRemoveButtonWidth - ImGui::GetStyle().ItemSpacing.x
            : 0.0f; // 0 を渡すと自動

        if (ImGui::Selectable(displayName.c_str(), isSelected,
            ImGuiSelectableFlags_None, ImVec2(selectableWidth, 0))) {
            manager_->SetSelected(editable);
        }

        // 動的オブジェクトのみ × ボタンを右端に表示
        if (isRemovable) {
            ImGui::SameLine();
            if (ImGui::Button("x", ImVec2(kRemoveButtonWidth, 0))) {
                if (scene) {
                    // 型名で正しい Remove メソッドへディスパッチ
                    if (typeName == "Sprite") {
                        scene->RemoveDynamicSprite(name);
                    } else if (typeName == "AnimatedObject3D") {
                        scene->RemoveDynamicAnimated(name);
                    } else if (typeName == "Primitive") {
                        scene->RemoveDynamicPrimitive(name);
                    } else {
                        scene->RemoveDynamicObject(name);
                    }
                }
            }
        }

        ImGui::PopID();
    }

#endif // DEBUG
}
