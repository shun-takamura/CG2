#include "InspectorWindow.h"
#include "ImGuiManager.h"
#include "Physics/Collider.h"

#include <cstdio>
#include <string>

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

    // ----- グループ選択 -----
    // グループの意味づけ（ゲームのタグ等）はホストがフックで注入する。
    // 未配線ならグループは1つしかないのでコンボ自体を出さない。
    {
        const int groupCount = manager_->GetEntityGroupCount();
        if (groupCount > 1) {
            const int current = manager_->GetEntityGroup(selected);
            if (ImGui::BeginCombo("Group", manager_->GetEntityGroupName(current))) {
                for (int i = 0; i < groupCount; ++i) {
                    const bool sel = (i == current);
                    if (ImGui::Selectable(manager_->GetEntityGroupName(i), sel)) {
                        manager_->SetEntityGroup(selected, i);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
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

    // ----- コライダー（translate を持つ 3D エンティティのみ） -----
    // 実体の在り処はホストが決める（未配線なら CollisionSystem のサイドテーブル）。
    if (selected->GetEditableTranslate()) {
        if (Collider* col = manager_->GetEntityCollider(selected)) {
            Collider& c = *col;
            if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("OnCollision", &c.enabled);
                if (c.enabled) {
                    // 形状コンボ
                    const char* shapeNames[] = { "Sphere", "OBB", "Capsule" };
                    int shapeIdx = static_cast<int>(c.shape);
                    if (ImGui::Combo("Shape", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                        c.shape = static_cast<ColliderShape>(shapeIdx);
                    }

                    // 共通: オフセット
                    ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);

                    // 形状別パラメータ
                    switch (c.shape) {
                    case ColliderShape::Sphere:
                        ImGui::DragFloat("Radius", &c.radius, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    case ColliderShape::OBB:
                        ImGui::DragFloat3("Half Extents", &c.halfExtents.x, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    case ColliderShape::Capsule:
                        ImGui::DragFloat("Capsule Radius", &c.capsuleRadius, 0.05f, 0.0f, 100.0f, "%.2f");
                        ImGui::DragFloat("Capsule Height", &c.capsuleHeight, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    }

                    ImGui::Checkbox("Show Debug", &c.showDebug);
                }
            }
            ImGui::Separator();
        }
    }

    // ----- 各エンティティが自前で実装した編集 UI -----
    // Object3D / Primitive / Sprite の Transform や、エフェクトの
    // プリミティブ・パーティクル・ライト・サウンドの各パラメータはここで出る。
    selected->OnImGuiInspector();

    // ----- ホストが登録したセクション -----
    // ゲーム固有のコンポーネント欄はここから差し込まれる。
    // CollapsingHeader は登録側が自前で出す（エンジンは包まない）。
    for (const auto& section : manager_->GetInspectorSections()) {
        if (section.draw) section.draw(selected);
    }

#endif // DEBUG
}
