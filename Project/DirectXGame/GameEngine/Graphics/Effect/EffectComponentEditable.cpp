#include "EffectComponentEditable.h"
#include "EffectEditorWindow.h"
#include "EffectDef.h"
#include "SceneEditorWindow.h"   // SPRITE_DROP_PAYLOAD_TYPE / SpriteDropPayload
#include "imgui.h"
#include <cstdio>
#include <string>

namespace {
    const char* MeshTypeNames[] = { "Plane", "Box", "Sphere", "Ring", "Cylinder", "Helix" };
    const char* BlendModeNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
    const char* BillboardNames[] = { "None", "Full", "YAxis" };
    const char* LightKindNames[] = { "Point", "Spot" };
}

EffectComponentEditable::EffectComponentEditable(EffectEditorWindow* owner, Kind kind, int index)
    : IImGuiEditable(NoAutoRegister{}), owner_(owner), kind_(kind), index_(index) {}

std::string EffectComponentEditable::GetName() const {
    // editBuffer 上の displayName が空ならデフォルト "<Kind> #<index>" を返す
    if (!owner_) return "(no owner)";
    const EffectDef& buf = owner_->GetEditBuffer();
    auto inRange = [](int i, size_t n) { return i >= 0 && i < static_cast<int>(n); };

    const std::string* dn = nullptr;
    switch (kind_) {
    case Kind::Primitive: if (inRange(index_, buf.primitives.size())) dn = &buf.primitives[index_].displayName; break;
    case Kind::Particle:  if (inRange(index_, buf.particles.size()))  dn = &buf.particles[index_].displayName;  break;
    case Kind::Light:     if (inRange(index_, buf.lights.size()))     dn = &buf.lights[index_].displayName;     break;
    case Kind::Sound:     if (inRange(index_, buf.sounds.size()))     dn = &buf.sounds[index_].displayName;     break;
    }
    if (dn && !dn->empty()) return *dn;

    char def[64];
    switch (kind_) {
    case Kind::Primitive: std::snprintf(def, sizeof(def), "Primitive #%d", index_); break;
    case Kind::Particle:  std::snprintf(def, sizeof(def), "Particle #%d",  index_); break;
    case Kind::Light:     std::snprintf(def, sizeof(def), "Light #%d",     index_); break;
    case Kind::Sound:     std::snprintf(def, sizeof(def), "Sound #%d",     index_); break;
    }
    return def;
}

void EffectComponentEditable::SetName(const std::string& name) {
    if (!owner_) return;
    EffectDef& buf = owner_->GetEditBuffer();
    auto inRange = [](int i, size_t n) { return i >= 0 && i < static_cast<int>(n); };
    switch (kind_) {
    case Kind::Primitive: if (inRange(index_, buf.primitives.size())) buf.primitives[index_].displayName = name; break;
    case Kind::Particle:  if (inRange(index_, buf.particles.size()))  buf.particles[index_].displayName  = name; break;
    case Kind::Light:     if (inRange(index_, buf.lights.size()))     buf.lights[index_].displayName     = name; break;
    case Kind::Sound:     if (inRange(index_, buf.sounds.size()))     buf.sounds[index_].displayName     = name; break;
    }
    owner_->MarkEditBufferDirty();
}

std::string EffectComponentEditable::GetTypeName() const {
    switch (kind_) {
    case Kind::Primitive: return "EffectPrimitive";
    case Kind::Particle:  return "EffectParticle";
    case Kind::Light:     return "EffectLight";
    case Kind::Sound:     return "EffectSound";
    }
    return "EffectComponent";
}

void EffectComponentEditable::OnImGuiInspector() {
#ifdef USE_IMGUI
    if (!owner_) {
        ImGui::Text("(no owner)");
        return;
    }

    // editBuffer 上の対応要素を引く（範囲外なら削除済み扱い）
    EffectDef& buf = owner_->GetEditBuffer();
    bool dirty = false;

    if (kind_ == Kind::Primitive) {
        if (index_ < 0 || index_ >= static_cast<int>(buf.primitives.size())) { ImGui::Text("(removed)"); return; }
        auto& c = buf.primitives[index_];

        // ===== Effect Timeline（エフェクト固有の時間制御） =====
        if (ImGui::CollapsingHeader("Effect Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Combo("Mesh", &c.meshType, MeshTypeNames, IM_ARRAYSIZE(MeshTypeNames));
            dirty |= ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
            dirty |= ImGui::DragFloat3("Rotate (rad)", &c.rotate.x, 0.01f);
            dirty |= ImGui::DragFloat("Start Time", &c.startTime, 0.01f, 0.0f, 60.0f);
            dirty |= ImGui::DragFloat("Lifetime",   &c.lifetime,  0.01f, 0.0f, 60.0f);
            dirty |= ImGui::DragFloat3("Start Scale", &c.startScale.x, 0.05f);
            dirty |= ImGui::DragFloat3("End Scale",   &c.endScale.x,   0.05f);
            dirty |= ImGui::ColorEdit4("Start Color", &c.startColor.x);
            dirty |= ImGui::ColorEdit4("End Color",   &c.endColor.x);
            int bb = static_cast<int>(c.billboardMode);
            if (ImGui::Combo("Billboard", &bb, BillboardNames, IM_ARRAYSIZE(BillboardNames))) {
                c.billboardMode = static_cast<BillboardMode>(bb);
                dirty = true;
            }
        }

        // ===== Geometry（Ring / Cylinder / Helix のみ。PrimitiveInstance Inspector と同等） =====
        if (c.meshType == 3 || c.meshType == 4 || c.meshType == 5) {
            if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (c.meshType == 3) { // Ring
                    auto& rp = c.ringParams;
                    dirty |= ImGui::DragFloat("Outer Radius", &rp.outerRadius, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Inner Radius", &rp.innerRadius, 0.01f, 0.0f, 100.0f);
                    int div = static_cast<int>(rp.divisions);
                    if (ImGui::DragInt("Divisions", &div, 1.0f, 3, 256)) {
                        rp.divisions = static_cast<uint32_t>(div);
                        dirty = true;
                    }
                    dirty |= ImGui::ColorEdit4("Inner Color", &rp.innerColor.x);
                    dirty |= ImGui::ColorEdit4("Outer Color", &rp.outerColor.x);
                    dirty |= ImGui::SliderAngle("Start Angle", &rp.startAngle, 0.0f, 360.0f);
                    dirty |= ImGui::SliderAngle("End Angle",   &rp.endAngle,   0.0f, 360.0f);
                } else if (c.meshType == 4) { // Cylinder
                    auto& cp = c.cylinderParams;
                    dirty |= ImGui::DragFloat("Top Radius",    &cp.topRadius,    0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Bottom Radius", &cp.bottomRadius, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Height",        &cp.height,       0.01f, 0.0f, 100.0f);
                    int div = static_cast<int>(cp.divisions);
                    if (ImGui::DragInt("Divisions", &div, 1.0f, 3, 256)) {
                        cp.divisions = static_cast<uint32_t>(div);
                        dirty = true;
                    }
                    dirty |= ImGui::ColorEdit4("Top Color",    &cp.topColor.x);
                    dirty |= ImGui::ColorEdit4("Bottom Color", &cp.bottomColor.x);
                    dirty |= ImGui::SliderAngle("Start Angle", &cp.startAngle, 0.0f, 360.0f);
                    dirty |= ImGui::SliderAngle("End Angle",   &cp.endAngle,   0.0f, 360.0f);
                } else { // Helix
                    auto& hp = c.helixParams;
                    dirty |= ImGui::DragFloat("Start Helix Radius", &hp.startHelixRadius, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("End Helix Radius",   &hp.endHelixRadius,   0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Start Tube Radius",  &hp.startTubeRadius,  0.005f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("End Tube Radius",    &hp.endTubeRadius,    0.005f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Pitch", &hp.pitch, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Turns", &hp.turns, 0.05f, 0.0f, 100.0f);
                    int circleSeg = static_cast<int>(hp.circleSegments);
                    if (ImGui::DragInt("Circle Segments", &circleSeg, 1.0f, 3, 64)) {
                        hp.circleSegments = static_cast<uint32_t>(circleSeg);
                        dirty = true;
                    }
                    int lenSeg = static_cast<int>(hp.lengthSegments);
                    if (ImGui::DragInt("Length Segments", &lenSeg, 1.0f, 1, 1024)) {
                        hp.lengthSegments = static_cast<uint32_t>(lenSeg);
                        dirty = true;
                    }
                    dirty |= ImGui::ColorEdit4("Start Color", &hp.startColor.x);
                    dirty |= ImGui::ColorEdit4("End Color",   &hp.endColor.x);
                }
                ImGui::TextDisabled("(変更は Restart で反映)");
            }
        }

        // ===== Material（PrimitiveInstance Inspector と同等） =====
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Combo("BlendMode", &c.blendMode, BlendModeNames, IM_ARRAYSIZE(BlendModeNames));
            dirty |= ImGui::Checkbox("DepthWrite", &c.depthWrite);
            dirty |= ImGui::SliderFloat("Alpha Discard", &c.alphaReference, 0.0f, 1.0f, "%.3f");
            dirty |= ImGui::Checkbox("Cull Backface", &c.cullBackface);
            const char* samplerItems[] = { "Wrap U / Wrap V", "Wrap U / Clamp V", "Clamp U / Clamp V" };
            dirty |= ImGui::Combo("Sampler", &c.samplerMode, samplerItems, IM_ARRAYSIZE(samplerItems));
        }

        // ===== UV =====
        if (ImGui::CollapsingHeader("UV", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Checkbox("Auto Scroll", &c.uvAutoScroll);
            if (!c.uvAutoScroll) ImGui::BeginDisabled();
            dirty |= ImGui::DragFloat2("Scroll Speed", &c.uvScrollSpeed.x, 0.01f);
            if (!c.uvAutoScroll) ImGui::EndDisabled();

            ImGui::Separator();
            dirty |= ImGui::DragFloat2("Manual Offset", &c.uvOffset.x, 0.01f);
            dirty |= ImGui::DragFloat2("UV Scale", &c.uvScale.x, 0.01f);
            dirty |= ImGui::Checkbox("Flip U", &c.uvFlipU);
            ImGui::SameLine();
            dirty |= ImGui::Checkbox("Flip V", &c.uvFlipV);

            if (ImGui::Button("Reset UV")) {
                c.uvAutoScroll = false;
                c.uvScrollSpeed = { 0.0f, 0.0f };
                c.uvOffset = { 0.0f, 0.0f };
                c.uvScale  = { 1.0f, 1.0f };
                c.uvFlipU = false;
                c.uvFlipV = false;
                dirty = true;
            }
        }

        // ===== Texture =====
        if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Current:");
            ImGui::SameLine();
            const std::string curr = c.texturePath.empty() ? "(default white)" : c.texturePath;
            ImGui::Button(curr.c_str(), ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                    const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                    c.texturePath = p->texturePath;
                    dirty = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TextDisabled("drop a Sprite from SceneEditor here");
            if (ImGui::Button("Reset to default")) {
                c.texturePath.clear();
                dirty = true;
            }
        }

        // ===== Distortion（歪みエフェクト） =====
        if (ImGui::CollapsingHeader("Distortion", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Checkbox("Use Distortion", &c.useDistortion);

            if (c.useDistortion) {
                ImGui::Indent();

                // --- ノーマルマップ（Sprite ドラッグ&ドロップ） ---
                ImGui::Text("Normal Map:");
                const std::string nmcurr = c.distortionTexturePath.empty() ? "(none)" : c.distortionTexturePath;
                ImGui::Button(nmcurr.c_str(), ImVec2(-FLT_MIN, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                        const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                        c.distortionTexturePath = p->texturePath;
                        dirty = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextDisabled("drop a Sprite from SceneEditor here");
                if (ImGui::Button("Reset Normal Map")) {
                    c.distortionTexturePath.clear();
                    dirty = true;
                }

                ImGui::Separator();

                // --- 強度（per-instance） ---
                dirty |= ImGui::SliderFloat("Strength##Distortion", &c.distortionStrength, 0.0f, 1.0f, "%.3f");

                ImGui::Separator();

                // --- 自動スクロール ---
                dirty |= ImGui::Checkbox("Auto Scroll##Distortion", &c.distortionUvAutoScroll);
                if (!c.distortionUvAutoScroll) ImGui::BeginDisabled();
                dirty |= ImGui::DragFloat2("Scroll Speed##Distortion", &c.distortionUvScrollSpeed.x, 0.01f);
                if (!c.distortionUvAutoScroll) ImGui::EndDisabled();

                ImGui::Separator();

                dirty |= ImGui::DragFloat2("Manual Offset##Distortion", &c.distortionUvOffset.x, 0.01f);
                dirty |= ImGui::DragFloat2("UV Scale##Distortion", &c.distortionUvScale.x, 0.01f);
                dirty |= ImGui::Checkbox("Flip U##Distortion", &c.distortionUvFlipU);
                ImGui::SameLine();
                dirty |= ImGui::Checkbox("Flip V##Distortion", &c.distortionUvFlipV);

                if (ImGui::Button("Reset UV##Distortion")) {
                    c.distortionUvAutoScroll = false;
                    c.distortionUvScrollSpeed = { 0.0f, 0.0f };
                    c.distortionUvOffset = { 0.0f, 0.0f };
                    c.distortionUvScale  = { 1.0f, 1.0f };
                    c.distortionUvFlipU = false;
                    c.distortionUvFlipV = false;
                    c.distortionStrength = 0.5f;
                    dirty = true;
                }

                ImGui::Unindent();
            }
        }
    }
    else if (kind_ == Kind::Particle) {
        if (index_ < 0 || index_ >= static_cast<int>(buf.particles.size())) { ImGui::Text("(removed)"); return; }
        auto& c = buf.particles[index_];
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", c.gpuParticleGroupName.c_str());
        if (ImGui::InputText("Group Name", nameBuf, sizeof(nameBuf))) {
            c.gpuParticleGroupName = nameBuf;
            dirty = true;
        }
        ImGui::TextDisabled("(GPUParticleManager::CreateGroup() の登録名)");
        dirty |= ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
        dirty |= ImGui::DragFloat("Start Time", &c.startTime, 0.01f, 0.0f, 60.0f);
        dirty |= ImGui::DragFloat("Duration",   &c.duration,  0.01f, 0.0f, 60.0f);
        int burst = static_cast<int>(c.burstCount);
        if (ImGui::DragInt("Burst Count", &burst, 1.0f, 0, 1024)) {
            c.burstCount = static_cast<uint32_t>(burst < 0 ? 0 : burst);
            dirty = true;
        }
        int bb = static_cast<int>(c.billboardMode);
        if (ImGui::Combo("Billboard", &bb, BillboardNames, IM_ARRAYSIZE(BillboardNames))) {
            c.billboardMode = static_cast<BillboardMode>(bb);
            dirty = true;
        }

        // ===== Color =====
        ImGui::Separator();
        const char* colorModeNames[] = { "Random", "Fixed" };
        dirty |= ImGui::Combo("Color Mode", &c.colorMode, colorModeNames, IM_ARRAYSIZE(colorModeNames));
        if (c.colorMode == 1) {
            dirty |= ImGui::ColorEdit4("Start Color", &c.startColor.x);
            dirty |= ImGui::ColorEdit4("End Color",   &c.endColor.x);
        } else {
            ImGui::TextDisabled("(Random: 色は GPU 側でランダム生成)");
        }

        // ===== Scale Range =====
        ImGui::Separator();
        dirty |= ImGui::Checkbox("Uniform Scale (W=H)", &c.uniformScale);
        dirty |= ImGui::DragFloat2("Scale Min (W/H)", &c.scaleMin.x, 0.01f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloat2("Scale Max (W/H)", &c.scaleMax.x, 0.01f, 0.0f, 100.0f);
        if (c.uniformScale) {
            ImGui::TextDisabled("(Uniform: Min/Max の X 範囲のみ使われ、W=H に固定)");
        }
    }
    else if (kind_ == Kind::Light) {
        if (index_ < 0 || index_ >= static_cast<int>(buf.lights.size())) { ImGui::Text("(removed)"); return; }
        auto& c = buf.lights[index_];
        int kind = static_cast<int>(c.kind);
        if (ImGui::Combo("Kind", &kind, LightKindNames, IM_ARRAYSIZE(LightKindNames))) {
            c.kind = static_cast<EffectLightKind>(kind);
            dirty = true;
        }
        dirty |= ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
        if (c.kind == EffectLightKind::Spot) {
            dirty |= ImGui::DragFloat3("Direction", &c.direction.x, 0.01f);
        }
        dirty |= ImGui::DragFloat("Start Time", &c.startTime, 0.01f, 0.0f, 60.0f);
        dirty |= ImGui::DragFloat("Lifetime",   &c.lifetime,  0.01f, 0.0f, 60.0f);
        dirty |= ImGui::ColorEdit4("Color", &c.color.x);
        dirty |= ImGui::DragFloat("Start Intensity", &c.startIntensity, 0.05f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloat("End Intensity",   &c.endIntensity,   0.05f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloat("Range", &c.range, 0.1f, 0.0f, 100.0f);
        if (c.kind == EffectLightKind::Spot) {
            dirty |= ImGui::DragFloat("Cos Angle",         &c.spotCosAngle,        0.01f, 0.0f, 1.0f);
            dirty |= ImGui::DragFloat("Cos Falloff Start", &c.spotCosFalloffStart, 0.01f, 0.0f, 1.0f);
        }
    }
    else if (kind_ == Kind::Sound) {
        if (index_ < 0 || index_ >= static_cast<int>(buf.sounds.size())) { ImGui::Text("(removed)"); return; }
        auto& c = buf.sounds[index_];
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", c.soundName.c_str());
        if (ImGui::InputText("Sound Name", nameBuf, sizeof(nameBuf))) {
            c.soundName = nameBuf;
            dirty = true;
        }
        ImGui::TextDisabled("(SoundManager::LoadFile() の登録キー)");
        dirty |= ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
        dirty |= ImGui::DragFloat("Start Time", &c.startTime, 0.01f, 0.0f, 60.0f);
        dirty |= ImGui::DragFloat("Distance Scale", &c.distanceScale, 0.1f, 0.1f, 200.0f);
        dirty |= ImGui::DragFloat("Volume", &c.volume, 0.01f, 0.0f, 4.0f);
    }

    if (dirty) {
        owner_->MarkEditBufferDirty();
    }
#endif
}
