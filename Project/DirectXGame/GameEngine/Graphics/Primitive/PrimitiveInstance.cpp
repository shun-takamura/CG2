#include "PrimitiveInstance.h"
#include "PrimitiveGenerator.h"
#include "MathUtility.h"
#include "EditorDropPayload.h"   // SPRITE_DROP_PAYLOAD_TYPE / SpriteDropPayload
#include "EngineTime.h"
#include "imgui.h"

const char* PrimitiveInstance::PrimitiveTypeToString(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Plane:    return "Plane";
    case PrimitiveType::Box:      return "Box";
    case PrimitiveType::Sphere:   return "Sphere";
    case PrimitiveType::Ring:     return "Ring";
    case PrimitiveType::Cylinder: return "Cylinder";
    case PrimitiveType::Helix:    return "Helix";
    case PrimitiveType::Hemisphere: return "Hemisphere";
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
        meshData = PrimitiveGenerator::CreateRing(ringParams_);
        break;
    case PrimitiveType::Cylinder:
        meshData = PrimitiveGenerator::CreateCylinder(cylinderParams_);
        break;
    case PrimitiveType::Helix:
        meshData = PrimitiveGenerator::CreateHelix(helixParams_);
        break;
    case PrimitiveType::Hemisphere:
        meshData = PrimitiveGenerator::CreateHemisphere();
        break;
    default:
        meshData = PrimitiveGenerator::CreatePlane();
        break;
    }

    mesh_.Initialize(meshData);

    // 不透明物体としてシーンに馴染ませるため、通常ブレンド・深度書き込みONに変更
    mesh_.SetBlendMode(PrimitivePipeline::kBlendModeNormal);
    mesh_.SetDepthWrite(true);

    // Ring / Cylinder は中心側の白アーティファクト回避のため WrapU+ClampV をデフォルトに
    if (type == PrimitiveType::Ring || type == PrimitiveType::Cylinder) {
        samplerMode_ = 1;
    } else {
        samplerMode_ = 0;
    }
    mesh_.SetSamplerMode(samplerMode_);

    // デフォルトテクスチャを適用
    SetTexture(GetDefaultTexturePath());
}

void PrimitiveInstance::RegenerateGeometry() {
    MeshData meshData;
    switch (primitiveType_) {
    case PrimitiveType::Ring:
        meshData = PrimitiveGenerator::CreateRing(ringParams_);
        break;
    case PrimitiveType::Cylinder:
        meshData = PrimitiveGenerator::CreateCylinder(cylinderParams_);
        break;
    case PrimitiveType::Helix:
        meshData = PrimitiveGenerator::CreateHelix(helixParams_);
        break;
    default:
        return; // 他の形状はパラメータを持たないので何もしない
    }
    mesh_.Initialize(meshData);
}

void PrimitiveInstance::Update() {
    // Inspector で形状パラメータが変わっていれば、まずここで再生成（前フレームの
    // コマンドリストは既に Close+Submit 済みなので安全）
    if (regenPending_) {
        regenPending_ = false;
        RegenerateGeometry();
    }

    // TimeGroup 連動デルタタイムを使って UV スクロール等を進める。
    // 供給元（シーン）が無ければ 0 にフォールバック
    float dt = EngineTime::ScaledDeltaTime(timeGroup_, 0.0f);
    mesh_.Update(camera_, dt);
}

void PrimitiveInstance::Draw() {
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
    mesh_.Draw();
}

void PrimitiveInstance::DrawIdPass() {
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
    mesh_.DrawIdPass(static_cast<uint32_t>(objectId_));
}

void PrimitiveInstance::SetTexture(const std::string& filePath) {
    textureFilePath_ = filePath;
    mesh_.SetTexture(filePath);
}

void PrimitiveInstance::ApplyPrefabParams(const PrimitivePrefabParams& p) {
    // 形状パラメータをコピーしてから再生成（Ring/Cylinder/Helix のみ実体に影響）
    ringParams_     = p.ringParams;
    cylinderParams_ = p.cylinderParams;
    helixParams_    = p.helixParams;
    if (primitiveType_ == PrimitiveType::Ring ||
        primitiveType_ == PrimitiveType::Cylinder ||
        primitiveType_ == PrimitiveType::Helix) {
        RegenerateGeometry();
    }

    // テクスチャ
    if (!p.texturePath.empty()) {
        SetTexture(p.texturePath);
    }

    // マテリアル
    mesh_.SetColor(p.color);
    mesh_.SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(p.blendMode));
    mesh_.SetDepthWrite(p.depthWrite);
    mesh_.SetAlphaReference(p.alphaReference);
    mesh_.SetCullBackface(p.cullBackface);
    mesh_.SetSamplerMode(p.samplerMode);
    mesh_.SetBillboardMode(p.billboardMode);

    // UV
    mesh_.SetUVScroll(p.uvAutoScroll ? p.uvScrollSpeed : Vector2{ 0.0f, 0.0f });
    mesh_.SetUVOffset(p.uvOffset);
    mesh_.SetUVScale(p.uvScale);
    mesh_.SetUVFlipU(p.uvFlipU);
    mesh_.SetUVFlipV(p.uvFlipV);

    // Inspector 側に保持している二重管理メンバも同期
    autoScrollEnabled_ = p.uvAutoScroll;
    scrollSpeed_       = p.uvScrollSpeed;
    manualUVOffset_    = p.uvOffset;
    uvScale_           = p.uvScale;
    flipU_             = p.uvFlipU;
    flipV_             = p.uvFlipV;
    alphaReference_    = p.alphaReference;
    cullBackface_      = p.cullBackface;
    samplerMode_       = p.samplerMode;
    timeGroup_         = p.timeGroup;
}

void PrimitiveInstance::OnImGuiInspector() {
#ifdef USE_IMGUI

    Transform& transform = mesh_.GetTransform();

    // タイプ表示
    ImGui::Text("Type: %s", PrimitiveTypeToString(primitiveType_));

    // TimeGroup（UVスクロール等の進行倍率に影響）
    {
        const char* items[] = { "World", "Player", "UI", "Effect" };
        int idx = static_cast<int>(timeGroup_);
        if (ImGui::Combo("Time Group", &idx, items, IM_ARRAYSIZE(items))) {
            timeGroup_ = static_cast<TimeGroup>(idx);
        }
    }

    // Billboard（面プリミティブのみ：Plane / Ring）
    if (primitiveType_ == PrimitiveType::Plane || primitiveType_ == PrimitiveType::Ring) {
        const char* billboardItems[] = { "None", "Full", "YAxis" };
        int bIdx = static_cast<int>(mesh_.GetBillboardMode());
        if (ImGui::Combo("Billboard", &bIdx, billboardItems, IM_ARRAYSIZE(billboardItems))) {
            mesh_.SetBillboardMode(static_cast<BillboardMode>(bIdx));
        }
    }
    ImGui::Separator();

    // Geometry（Ring / Cylinder / Helix のみ）
    if (primitiveType_ == PrimitiveType::Ring ||
        primitiveType_ == PrimitiveType::Cylinder ||
        primitiveType_ == PrimitiveType::Helix) {
        if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool regen = false;

            if (primitiveType_ == PrimitiveType::Ring) {
                regen |= ImGui::DragFloat("Outer Radius", &ringParams_.outerRadius, 0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Inner Radius", &ringParams_.innerRadius, 0.01f, 0.0f, 100.0f);
                int div = static_cast<int>(ringParams_.divisions);
                if (ImGui::DragInt("Divisions", &div, 1.0f, 3, 256)) {
                    ringParams_.divisions = static_cast<uint32_t>(div);
                    regen = true;
                }
                regen |= ImGui::ColorEdit4("Inner Color", &ringParams_.innerColor.x);
                regen |= ImGui::ColorEdit4("Outer Color", &ringParams_.outerColor.x);
                regen |= ImGui::SliderAngle("Start Angle", &ringParams_.startAngle, 0.0f, 360.0f);
                regen |= ImGui::SliderAngle("End Angle",   &ringParams_.endAngle,   0.0f, 360.0f);
            } else if (primitiveType_ == PrimitiveType::Cylinder) {
                regen |= ImGui::DragFloat("Top Radius",    &cylinderParams_.topRadius,    0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Bottom Radius", &cylinderParams_.bottomRadius, 0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Height",        &cylinderParams_.height,       0.01f, 0.0f, 100.0f);
                int div = static_cast<int>(cylinderParams_.divisions);
                if (ImGui::DragInt("Divisions", &div, 1.0f, 3, 256)) {
                    cylinderParams_.divisions = static_cast<uint32_t>(div);
                    regen = true;
                }
                regen |= ImGui::ColorEdit4("Top Color",    &cylinderParams_.topColor.x);
                regen |= ImGui::ColorEdit4("Bottom Color", &cylinderParams_.bottomColor.x);
                regen |= ImGui::SliderAngle("Start Angle", &cylinderParams_.startAngle, 0.0f, 360.0f);
                regen |= ImGui::SliderAngle("End Angle",   &cylinderParams_.endAngle,   0.0f, 360.0f);
            } else { // Helix
                regen |= ImGui::DragFloat("Start Helix Radius", &helixParams_.startHelixRadius, 0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("End Helix Radius",   &helixParams_.endHelixRadius,   0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Start Tube Radius",  &helixParams_.startTubeRadius,  0.005f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("End Tube Radius",    &helixParams_.endTubeRadius,    0.005f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Pitch", &helixParams_.pitch, 0.01f, 0.0f, 100.0f);
                regen |= ImGui::DragFloat("Turns", &helixParams_.turns, 0.05f, 0.0f, 100.0f);

                int circleSeg = static_cast<int>(helixParams_.circleSegments);
                if (ImGui::DragInt("Circle Segments", &circleSeg, 1.0f, 3, 64)) {
                    helixParams_.circleSegments = static_cast<uint32_t>(circleSeg);
                    regen = true;
                }
                int lenSeg = static_cast<int>(helixParams_.lengthSegments);
                if (ImGui::DragInt("Length Segments", &lenSeg, 1.0f, 1, 1024)) {
                    helixParams_.lengthSegments = static_cast<uint32_t>(lenSeg);
                    regen = true;
                }

                regen |= ImGui::ColorEdit4("Start Color", &helixParams_.startColor.x);
                regen |= ImGui::ColorEdit4("End Color",   &helixParams_.endColor.x);
            }

            if (regen) {
                // 即時再生成すると ImGui Draw 中にリソース解放→D3D12 ERROR #921 になるため
                // フラグを立てて、次フレームの Update 冒頭で安全に再生成する。
                regenPending_ = true;
            }
        }
    }

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform.translate.x, 0.1f);

        // 回転をDegreeで表示（内部はラジアン保存）
        Vector3 rotateDegree = RadToDeg(transform.rotate);
        if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
            transform.rotate = DegToRad(rotateDegree);
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

        // Discard 閾値（このα値以下はピクセル破棄）
        if (ImGui::SliderFloat("Alpha Discard", &alphaReference_, 0.0f, 1.0f, "%.3f")) {
            mesh_.SetAlphaReference(alphaReference_);
        }

        // 背面カリング
        if (ImGui::Checkbox("Cull Backface", &cullBackface_)) {
            mesh_.SetCullBackface(cullBackface_);
        }

        // サンプラーモード（Ring/Cylinder の中心アーティファクト対策）
        const char* samplerItems[] = { "Wrap U / Wrap V", "Wrap U / Clamp V", "Clamp U / Clamp V" };
        if (ImGui::Combo("Sampler", &samplerMode_, samplerItems, IM_ARRAYSIZE(samplerItems))) {
            mesh_.SetSamplerMode(samplerMode_);
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
