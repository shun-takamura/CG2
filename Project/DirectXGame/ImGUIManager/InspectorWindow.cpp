#include "InspectorWindow.h"
#include "ImGuiManager.h"
#include "Components/EntityTag.h"
#include "Components/CollisionMatrix.h"
#include "Components/SphereCollider.h"
#include "Components/Prefab.h"
#include "Components/PrefabManager.h"
#include "Object3DInstance.h"
#include "AnimatedObject3DInstance.h"
#include "PrimitiveInstance.h"
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
                Collider& c = selected->GetCollider();
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

    // 各オブジェクトが実装した編集UIを描画
    selected->OnImGuiInspector();

    // ----- Save as Prefab（Object3D / AnimatedObject3D / Primitive 対応） -----
    {
        const std::string typeName = selected->GetTypeName();
        const bool isObj3D    = (typeName == "Object3D");
        const bool isAnimated = (typeName == "AnimatedObject3D");
        const bool isPrimitive = (typeName == "Primitive");
        if (isObj3D || isAnimated || isPrimitive) {
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
                static char prefabNameBuf[128] = "";
                static IImGuiEditable* lastSelected = nullptr;
                if (selected != lastSelected) {
                    std::snprintf(prefabNameBuf, sizeof(prefabNameBuf), "%s",
                        selected->GetName().c_str());
                    lastSelected = selected;
                }
                ImGui::InputText("Prefab Name", prefabNameBuf, sizeof(prefabNameBuf));
                if (ImGui::Button("Save as Prefab")) {
                    PrefabDef def{};
                    def.name = prefabNameBuf;
                    def.tag = selected->GetTag();

                    if (isObj3D) {
                        def.kind = PrefabKind::Object3D;
                        def.isAnimated = false;
                        auto* obj = static_cast<Object3DInstance*>(selected);
                        def.modelDir = obj->GetDirectoryPath();
                        def.modelFile = obj->GetModelFileName();
                        def.defaultScale = obj->GetScale();
                        def.defaultRotate = obj->GetRotate();
                    } else if (isAnimated) {
                        def.kind = PrefabKind::Animated;
                        def.isAnimated = true;
                        auto* obj = static_cast<AnimatedObject3DInstance*>(selected);
                        def.modelDir = obj->GetDirectoryPath();
                        def.modelFile = obj->GetModelFileName();
                        def.defaultScale = obj->GetScale();
                        def.defaultRotate = obj->GetRotate();
                    } else { // isPrimitive
                        def.kind = PrefabKind::Primitive;
                        def.isAnimated = false;
                        auto* prim = static_cast<PrimitiveInstance*>(selected);
                        const auto& tr = prim->GetMesh().GetTransform();
                        def.defaultScale = tr.scale;
                        def.defaultRotate = tr.rotate;

                        auto& pp = def.primitiveParams;
                        pp.primitiveType = static_cast<int>(prim->GetPrimitiveType());
                        pp.texturePath   = prim->GetTextureFilePath();
                        pp.color         = prim->GetMesh().GetColor();
                        pp.blendMode     = static_cast<int>(prim->GetMesh().GetBlendMode());
                        pp.depthWrite    = prim->GetMesh().GetDepthWrite();
                        pp.alphaReference = prim->GetAlphaReference();
                        pp.cullBackface  = prim->GetCullBackface();
                        pp.samplerMode   = prim->GetSamplerMode();
                        pp.uvAutoScroll  = prim->GetAutoScroll();
                        pp.uvScrollSpeed = prim->GetScrollSpeed();
                        pp.uvOffset      = prim->GetManualUVOffset();
                        pp.uvScale       = prim->GetUVScale();
                        pp.uvFlipU       = prim->GetFlipU();
                        pp.uvFlipV       = prim->GetFlipV();
                        pp.billboardMode = prim->GetMesh().GetBillboardMode();
                        pp.timeGroup     = prim->GetTimeGroup();
                        pp.ringParams     = prim->GetRingParams();
                        pp.cylinderParams = prim->GetCylinderParams();
                        pp.helixParams    = prim->GetHelixParams();
                    }

                    const auto& col = selected->GetCollider();
                    if (col.enabled) {
                        def.hasCollider = true;
                        def.colliderShape = col.shape;
                        def.colliderOffset = col.offset;
                        def.colliderRadius = col.radius;
                        def.colliderHalfExtents = col.halfExtents;
                        def.colliderCapsuleRadius = col.capsuleRadius;
                        def.colliderCapsuleHeight = col.capsuleHeight;
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
    }

#endif // DEBUG
}
