#include "PrimitiveInstance.h"
#include "PrimitiveGenerator.h"
#include "SceneEditorWindow.h"   // SPRITE_DROP_PAYLOAD_TYPE / SpriteDropPayload
#include "imgui.h"

const char* PrimitiveInstance::PrimitiveTypeToString(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Plane:    return "Plane";
    case PrimitiveType::Box:      return "Box";
    case PrimitiveType::Sphere:   return "Sphere";
    case PrimitiveType::Ring:     return "Ring";
    case PrimitiveType::Cylinder: return "Cylinder";
    default:                      return "Unknown";
    }
}

void PrimitiveInstance::Initialize(PrimitiveType type, const std::string& name) {
    primitiveType_ = type;

    // 名前未指定ならタイプ名を使用
    name_ = name.empty() ? PrimitiveTypeToString(type) : name;

    // タイプに応じたメッシュデータを生成
    MeshData meshData;
    switch (type) {
    case PrimitiveType::Plane:
        meshData = PrimitiveGenerator::CreatePlane();
        break;
    case PrimitiveType::Box:
        meshData = PrimitiveGenerator::CreateBox();
        break;
    case PrimitiveType::Sphere:
        meshData = PrimitiveGenerator::CreateSphere();
        break;
    case PrimitiveType::Ring:
        meshData = PrimitiveGenerator::CreateRing();
        break;
    case PrimitiveType::Cylinder:
        meshData = PrimitiveGenerator::CreateCylinder();
        break;
    default:
        meshData = PrimitiveGenerator::CreatePlane();
        break;
    }

    mesh_.Initialize(meshData);

    // 不透明物体としてシーンに馴染ませるため、通常ブレンド・深度書き込みONに変更
    mesh_.SetBlendMode(PrimitivePipeline::kBlendModeNormal);
    mesh_.SetDepthWrite(true);

    // デフォルトテクスチャを適用
    SetTexture(GetDefaultTexturePath());
}

void PrimitiveInstance::Update() {
    mesh_.Update(camera_);
}

void PrimitiveInstance::Draw() {
    mesh_.Draw();
}

void PrimitiveInstance::SetTexture(const std::string& filePath) {
    textureFilePath_ = filePath;
    mesh_.SetTexture(filePath);
}

void PrimitiveInstance::OnImGuiInspector() {
#ifdef USE_IMGUI

    Transform& transform = mesh_.GetTransform();

    // タイプ表示
    ImGui::Text("Type: %s", PrimitiveTypeToString(primitiveType_));
    ImGui::Separator();

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform.translate.x, 0.1f);

        // 回転をDegreeで表示
        Vector3 rotateDegree = {
            transform.rotate.x * (180.0f / 3.14159265f),
            transform.rotate.y * (180.0f / 3.14159265f),
            transform.rotate.z * (180.0f / 3.14159265f)
        };
        if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
            transform.rotate.x = rotateDegree.x * (3.14159265f / 180.0f);
            transform.rotate.y = rotateDegree.y * (3.14159265f / 180.0f);
            transform.rotate.z = rotateDegree.z * (3.14159265f / 180.0f);
        }

        ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f);
    }

    // Material（色・ブレンドモード・深度書き込み）
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        Vector4 color = mesh_.GetColor();
        if (ImGui::ColorEdit4("Color", &color.x)) {
            mesh_.SetColor(color);
        }

        // ブレンドモード
        const char* blendItems[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
        int blendIndex = static_cast<int>(mesh_.GetBlendMode());
        if (ImGui::Combo("BlendMode", &blendIndex, blendItems, IM_ARRAYSIZE(blendItems))) {
            mesh_.SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(blendIndex));
        }

        // 深度書き込み
        bool depthWrite = mesh_.GetDepthWrite();
        if (ImGui::Checkbox("DepthWrite", &depthWrite)) {
            mesh_.SetDepthWrite(depthWrite);
        }
    }

    // UV（自動スクロール / 手動オフセット / Scale / Flip）
    if (ImGui::CollapsingHeader("UV", ImGuiTreeNodeFlags_DefaultOpen)) {
        // --- 自動スクロール ---
        if (ImGui::Checkbox("Auto Scroll", &autoScrollEnabled_)) {
            // ON/OFF 切替時に PrimitiveMesh の uvScrollSpeed_ を反映
            mesh_.SetUVScroll(autoScrollEnabled_ ? scrollSpeed_ : Vector2{ 0.0f, 0.0f });
        }

        // 速度入力（OFF の間も値は保持し、ON のときだけ反映）
        if (!autoScrollEnabled_) ImGui::BeginDisabled();
        if (ImGui::DragFloat2("Scroll Speed", &scrollSpeed_.x, 0.01f)) {
            if (autoScrollEnabled_) {
                mesh_.SetUVScroll(scrollSpeed_);
            }
        }
        if (!autoScrollEnabled_) ImGui::EndDisabled();

        ImGui::Separator();

        // --- 手動オフセット（Transform のように DragFloat2） ---
        if (ImGui::DragFloat2("Manual Offset", &manualUVOffset_.x, 0.01f)) {
            mesh_.SetUVOffset(manualUVOffset_);
        }

        // --- スケール ---
        if (ImGui::DragFloat2("UV Scale", &uvScale_.x, 0.01f)) {
            mesh_.SetUVScale(uvScale_);
        }

        // --- Flip ---
        if (ImGui::Checkbox("Flip U", &flipU_)) {
            mesh_.SetUVFlipU(flipU_);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Flip V", &flipV_)) {
            mesh_.SetUVFlipV(flipV_);
        }

        // --- リセット ---
        if (ImGui::Button("Reset UV")) {
            autoScrollEnabled_ = false;
            scrollSpeed_      = { 0.0f, 0.0f };
            manualUVOffset_   = { 0.0f, 0.0f };
            uvScale_          = { 1.0f, 1.0f };
            flipU_ = false;
            flipV_ = false;
            mesh_.SetUVScroll(scrollSpeed_);
            mesh_.SetUVOffset(manualUVOffset_);
            mesh_.SetUVScale(uvScale_);
            mesh_.SetUVFlipU(false);
            mesh_.SetUVFlipV(false);
        }
    }

    // Texture（Sprite ドラッグ&ドロップ受け入れ）
    if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Current:");
        ImGui::SameLine();

        // ドロップ受け取りボタン（パスを表示する Selectable をターゲット化）
        const std::string current = textureFilePath_.empty() ? "(none)" : textureFilePath_;
        ImGui::Button(current.c_str(), ImVec2(-FLT_MIN, 0));

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                SetTexture(p->texturePath);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::TextDisabled("drop a Sprite here to apply");

        // デフォルトテクスチャに戻すボタン
        if (ImGui::Button("Reset to default")) {
            SetTexture(GetDefaultTexturePath());
        }
    }

#endif // USE_IMGUI
}
