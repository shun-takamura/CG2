#include "InspectorWindow.h"
#include "ImGuiManager.h"
#include "Components/EntityTag.h"
#include "Components/CollisionMatrix.h"
#include "Components/SphereCollider.h"
#include "Components/Prefab.h"
#include "Components/PrefabManager.h"
#include "Object3DInstance.h"
#include "LogBuffer.h"

#include <cstring>
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

    // ----- コライダー（タグが衝突可能、かつ 3D エンティティの場合のみ表示） -----
    {
        const EntityTag tag = selected->GetTag();
        if (CollisionMatrix::IsCollidableTag(tag) && selected->GetEditableTranslate()) {
            if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                SphereCollider& c = selected->GetCollider();
                ImGui::Checkbox("OnCollision", &c.enabled);
                if (c.enabled) {
                    ImGui::DragFloat("Radius", &c.radius, 0.05f, 0.0f, 100.0f, "%.2f");
                    ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
                    ImGui::Checkbox("Show Debug", &c.showDebug);
                }
            }
            ImGui::Separator();
        }
    }

    // 各オブジェクトが実装した編集UIを描画
    selected->OnImGuiInspector();

    // ----- Save as Prefab（Object3D のみ） -----
    if (selected->GetTypeName() == "Object3D") {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char prefabNameBuf[128] = "";
            // 初期値として現在のオブジェクト名を入れておく
            static IImGuiEditable* lastSelected = nullptr;
            if (selected != lastSelected) {
                std::snprintf(prefabNameBuf, sizeof(prefabNameBuf), "%s",
                    selected->GetName().c_str());
                lastSelected = selected;
            }
            ImGui::InputText("Prefab Name", prefabNameBuf, sizeof(prefabNameBuf));
            if (ImGui::Button("Save as Prefab")) {
                auto* obj = static_cast<Object3DInstance*>(selected);
                PrefabDef def{};
                def.name = prefabNameBuf;
                def.modelDir = obj->GetDirectoryPath();
                def.modelFile = obj->GetModelFileName();
                def.tag = obj->GetTag();
                def.defaultScale = obj->GetScale();
                def.defaultRotate = obj->GetRotate();
                const auto& col = obj->GetCollider();
                if (col.enabled) {
                    def.hasCollider = true;
                    def.colliderRadius = col.radius;
                    def.colliderOffset = col.offset;
                }
                std::string path = std::string(PrefabManager::GetPrefabDir())
                    + "/" + def.name + ".json";
                bool ok = PrefabManager::Save(def, path);
                LogBuffer::Instance().Add(
                    ok ? ("Prefab saved: " + path)
                       : ("Prefab save FAILED: " + path),
                    ok ? LogBuffer::Level::Info : LogBuffer::Level::Error);
                if (ok) {
                    PrefabManager::GetInstance()->Rescan();
                }
            }
        }
    }

#endif // DEBUG
}
