#include "EffectComponentEditable.h"
#include "EffectEditorWindow.h"
#include "EffectDef.h"
#include "MathUtility.h"
#include "EditorDropPayload.h"   // SPRITE_DROP_PAYLOAD_TYPE / SpriteDropPayload
#include "imgui.h"
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>

namespace {
    const char* MeshTypeNames[] = { "Plane", "Box", "Sphere", "Ring", "Cylinder", "Helix", "Beam", "Lightning", "Hemisphere", "Frame" };
    const char* BlendModeNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
    const char* BillboardNames[] = { "None", "Full", "YAxis" };
    // TimeGroup（World/Player/UI/Effect）。Effect は必殺技の全停止中でも動く（既定 1.0）。
    const char* TimeGroupNames[] = { "World", "Player", "UI", "Effect" };
    const char* LightKindNames[] = { "Point", "Spot" };

    // グリッド吸着の分割数（1/kCurveGridDiv 刻みに吸着）
    constexpr int kCurveGridDiv = 10;

    // アニメーションカーブのグラフ編集UI。横軸=正規化時間(0..1)、縦軸=出力(0..1)。
    // 左ドラッグで点移動／右クリックで中間点削除／Add Pointで追加／Snapでグリッド吸着。
    // 端点(最初/最後)は x を 0/1 に固定し y のみ可動。dirty を返す。
    bool DrawCurveEditor(const char* id, EffectCurve& curve) {
        bool dirty = false;
        ImGui::PushID(id);

        dirty |= ImGui::Checkbox("Use Curve", &curve.enabled);
        if (!curve.enabled) { ImGui::PopID(); return dirty; }

        static bool snap = true;
        ImGui::SameLine(); ImGui::Checkbox("Snap", &snap);
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Point")) {
            // 最大ギャップの中点に追加
            float bestX = 0.5f, bestGap = -1.0f;
            for (size_t i = 1; i < curve.points.size(); ++i) {
                float gap = curve.points[i].x - curve.points[i - 1].x;
                if (gap > bestGap) { bestGap = gap; bestX = (curve.points[i].x + curve.points[i - 1].x) * 0.5f; }
            }
            Vector2 np = { bestX, curve.Evaluate(bestX) };
            size_t ins = curve.points.size();
            for (size_t i = 0; i < curve.points.size(); ++i) { if (np.x < curve.points[i].x) { ins = i; break; } }
            curve.points.insert(curve.points.begin() + static_cast<std::ptrdiff_t>(ins), np);
            dirty = true;
        }

        const ImVec2 size = { ImGui::GetContentRegionAvail().x, 160.0f };
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("canvas", size);
        const ImVec2 p1 = { p0.x + size.x, p0.y + size.y };
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255));
        for (int i = 0; i <= kCurveGridDiv; ++i) {
            float fx = p0.x + size.x * static_cast<float>(i) / kCurveGridDiv;
            float fy = p0.y + size.y * static_cast<float>(i) / kCurveGridDiv;
            dl->AddLine({ fx, p0.y }, { fx, p1.y }, IM_COL32(70, 70, 70, 255));
            dl->AddLine({ p0.x, fy }, { p1.x, fy }, IM_COL32(70, 70, 70, 255));
        }
        dl->AddRect(p0, p1, IM_COL32(120, 120, 120, 255));

        auto toScreen = [&](const Vector2& pt) -> ImVec2 {
            return { p0.x + pt.x * size.x, p0.y + (1.0f - pt.y) * size.y };
        };

        for (size_t i = 1; i < curve.points.size(); ++i) {
            dl->AddLine(toScreen(curve.points[i - 1]), toScreen(curve.points[i]), IM_COL32(80, 220, 80, 255), 2.0f);
        }

        // ドラッグ状態（同時に1点のみ）。dragCurve でどのカーブを掴んでいるか識別。
        static const void* dragCurve = nullptr;
        static int dragIdx = -1;
        const float grab = 8.0f;
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
            for (size_t i = 0; i < curve.points.size(); ++i) {
                ImVec2 sp = toScreen(curve.points[i]);
                float dx = sp.x - mouse.x, dy = sp.y - mouse.y;
                if (dx * dx + dy * dy <= grab * grab) { dragCurve = &curve; dragIdx = static_cast<int>(i); break; }
            }
        }
        if (dragCurve == &curve && dragIdx >= 0 && dragIdx < static_cast<int>(curve.points.size())) {
            if (ImGui::IsMouseDown(0)) {
                float nx = std::clamp((mouse.x - p0.x) / size.x, 0.0f, 1.0f);
                float ny = std::clamp(1.0f - (mouse.y - p0.y) / size.y, 0.0f, 1.0f);
                if (snap) {
                    nx = std::round(nx * kCurveGridDiv) / kCurveGridDiv;
                    ny = std::round(ny * kCurveGridDiv) / kCurveGridDiv;
                }
                const bool isFirst = (dragIdx == 0);
                const bool isLast  = (dragIdx == static_cast<int>(curve.points.size()) - 1);
                if (isFirst) nx = 0.0f;
                else if (isLast) nx = 1.0f;
                else {
                    float lo = curve.points[dragIdx - 1].x + 0.001f;
                    float hi = curve.points[dragIdx + 1].x - 0.001f;
                    nx = std::clamp(nx, lo, hi);
                }
                curve.points[dragIdx] = { nx, ny };
                dirty = true;
            } else {
                dragCurve = nullptr; dragIdx = -1;
            }
        }

        int removeIdx = -1;
        for (size_t i = 0; i < curve.points.size(); ++i) {
            ImVec2 sp = toScreen(curve.points[i]);
            float dx = sp.x - mouse.x, dy = sp.y - mouse.y;
            bool hovered = (dx * dx + dy * dy <= grab * grab) && ImGui::IsItemHovered();
            dl->AddCircleFilled(sp, hovered ? 6.0f : 4.0f, IM_COL32(80, 220, 80, 255));
            if (hovered && ImGui::IsMouseClicked(1) && i != 0 && i != curve.points.size() - 1) {
                removeIdx = static_cast<int>(i);
            }
        }
        if (removeIdx >= 0) { curve.points.erase(curve.points.begin() + removeIdx); dirty = true; }

        ImGui::TextDisabled("左ドラッグ=移動 / 右クリック=点削除 / Add Point=追加");
        ImGui::PopID();
        return dirty;
    }
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
            // 回転をDegreeで表示（内部・保存はラジアンのまま）
            Vector3 rotateDegree = RadToDeg(c.rotate);
            if (ImGui::DragFloat3("Rotate (deg)", &rotateDegree.x, 1.0f)) {
                c.rotate = DegToRad(rotateDegree);
                dirty = true;
            }

            // ----- 回転アニメーション（内部はクオータニオン。度数で表示） -----
            ImGui::SeparatorText("Rotation Animation");
            dirty |= ImGui::Checkbox("Random Rotate On Spawn", &c.randomRotateOnSpawn);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("出現時に各軸 ±範囲 のランダム角を1回だけ加える");
            if (c.randomRotateOnSpawn) {
                Vector3 randRangeDeg = RadToDeg(c.randomRotateRange);
                if (ImGui::DragFloat3("Random Range (±deg)", &randRangeDeg.x, 1.0f, 0.0f, 360.0f)) {
                    c.randomRotateRange = DegToRad(randRangeDeg);
                    dirty = true;
                }
            }
            Vector3 rotateSpeedDeg = RadToDeg(c.rotateSpeed);
            if (ImGui::DragFloat3("Rotate Speed (deg/s)", &rotateSpeedDeg.x, 1.0f)) {
                c.rotateSpeed = DegToRad(rotateSpeedDeg);
                dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("出ている間、各軸を回し続ける角速度（0で停止）");

            dirty |= ImGui::DragFloat("Start Time", &c.startTime, 0.01f, 0.0f, 60.0f);
            dirty |= ImGui::DragFloat("Lifetime",   &c.lifetime,  0.01f, 0.0f, 60.0f);
            dirty |= ImGui::DragFloat3("Start Scale", &c.startScale.x, 0.05f);
            dirty |= ImGui::DragFloat3("End Scale",   &c.endScale.x,   0.05f);
            if (ImGui::TreeNode("Scale Curve (時間→t)")) {
                dirty |= DrawCurveEditor("ScaleCurve", c.scaleCurve);
                ImGui::TreePop();
            }
            dirty |= ImGui::ColorEdit4("Start Color", &c.startColor.x);
            dirty |= ImGui::ColorEdit4("End Color",   &c.endColor.x);
            // Hue 回転（生存中のシームレス色変化）。経過秒に沿って色相を回す。
            dirty |= ImGui::Checkbox("Hue Shift", &c.hueShiftEnable);
            if (c.hueShiftEnable) {
                dirty |= ImGui::DragFloat("Hue Speed (rev/s)", &c.hueShiftSpeed, 0.01f, -5.0f, 5.0f, "%.2f");
            }
            int bb = static_cast<int>(c.billboardMode);
            if (ImGui::Combo("Billboard", &bb, BillboardNames, IM_ARRAYSIZE(BillboardNames))) {
                c.billboardMode = static_cast<BillboardMode>(bb);
                dirty = true;
            }

            // ===== 位置アニメ（StartPos→EndPos を時間で補間） =====
            ImGui::SeparatorText("Position Anim");
            dirty |= ImGui::Checkbox("Use Position Anim", &c.usePositionAnim);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON で offset の代わりに StartPos→EndPos を時間補間（Plane等を動かす用途）");
            if (c.usePositionAnim) {
                ImGui::Indent();
                dirty |= ImGui::DragFloat3("Start Pos", &c.startPos.x, 0.05f);
                dirty |= ImGui::DragFloat3("End Pos",   &c.endPos.x,   0.05f);
                if (ImGui::TreeNode("Position Curve (時間→t)")) {
                    dirty |= DrawCurveEditor("PosCurve", c.posCurve);
                    ImGui::TreePop();
                }
                ImGui::Unindent();
            }
        }

        // ===== Geometry（Ring / Cylinder / Helix / Beam / Lightning / Frame のみ。PrimitiveInstance Inspector と同等） =====
        if (c.meshType == 3 || c.meshType == 4 || c.meshType == 5 || c.meshType == 6 || c.meshType == 7 || c.meshType == 9) {
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
                } else if (c.meshType == 5) { // Helix
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
                } else if (c.meshType == 6) { // Beam
                    auto& bp = c.beamParams;
                    auto& app = bp.appearance;
                    dirty |= ImGui::DragFloat3("Start Pos", &bp.startPos.x, 0.05f);
                    dirty |= ImGui::DragFloat3("End Pos",   &bp.endPos.x,   0.05f);
                    int lenSeg = static_cast<int>(bp.lengthSegments);
                    if (ImGui::DragInt("Length Segments", &lenSeg, 1.0f, 1, 1024)) {
                        bp.lengthSegments = static_cast<uint32_t>(lenSeg);
                        dirty = true;
                    }
                    dirty |= ImGui::DragFloat("Start Width", &app.startWidth, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("End Width",   &app.endWidth,   0.01f, 0.0f, 100.0f);
                    int planeCount = static_cast<int>(app.planeCount);
                    if (ImGui::DragInt("Plane Count", &planeCount, 0.1f, 1, 8)) {
                        app.planeCount = static_cast<uint32_t>(planeCount);
                        dirty = true;
                    }
                    dirty |= ImGui::DragFloat("Fade Start Length", &app.fadeStartLength, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Fade End Length",   &app.fadeEndLength,   0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::ColorEdit4("Start Color##Beam", &app.startColor.x);
                    dirty |= ImGui::ColorEdit4("End Color##Beam",   &app.endColor.x);
                    dirty |= ImGui::Checkbox("UV Wrap By Length", &app.uvWrapByLength);
                    dirty |= ImGui::DragFloat("UV Tiles Per Unit", &app.uvTilesPerUnit, 0.01f, 0.0f, 100.0f);
                } else if (c.meshType == 9) { // Frame（額縁）
                    auto& fp = c.frameParams;
                    dirty |= ImGui::DragFloat("Outer Width",  &fp.outerWidth,  0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Outer Height", &fp.outerHeight, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Inner Width",  &fp.innerWidth,  0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Inner Height", &fp.innerHeight, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::ColorEdit4("Color##Frame", &fp.color.x);
                } else { // Lightning (meshType==7)
                    auto& lp = c.lightningParams;
                    auto& app = lp.appearance;
                    dirty |= ImGui::DragFloat3("Start Pos", &lp.startPos.x, 0.05f);
                    dirty |= ImGui::DragFloat3("End Pos",   &lp.endPos.x,   0.05f);

                    ImGui::SeparatorText("Fractal");
                    int gen = static_cast<int>(lp.generations);
                    if (ImGui::DragInt("Generations", &gen, 0.1f, 1, 10)) {
                        lp.generations = static_cast<uint32_t>(gen);
                        dirty = true;
                    }
                    dirty |= ImGui::DragFloat("Max Offset Ratio", &lp.maxOffsetRatio, 0.005f, 0.0f, 1.0f);
                    int seed = static_cast<int>(lp.randomSeed);
                    if (ImGui::DragInt("Random Seed (0=random)", &seed, 1.0f, 0, INT_MAX)) {
                        lp.randomSeed = static_cast<uint32_t>(seed < 0 ? 0 : seed);
                        dirty = true;
                    }

                    ImGui::SeparatorText("Branches");
                    dirty |= ImGui::SliderFloat("Branch Probability", &lp.branchProbability, 0.0f, 1.0f);
                    dirty |= ImGui::DragFloat("Branch Length Scale", &lp.branchLengthScale, 0.01f, 0.0f, 2.0f);
                    dirty |= ImGui::SliderAngle("Branch Max Angle", &lp.branchMaxAngle, 0.0f, 90.0f);
                    dirty |= ImGui::DragFloat("Branch Width Scale", &lp.branchWidthScale, 0.01f, 0.0f, 2.0f);
                    dirty |= ImGui::DragFloat("Branch Color Scale", &lp.branchColorScale, 0.01f, 0.0f, 2.0f);

                    ImGui::SeparatorText("Appearance");
                    dirty |= ImGui::DragFloat("Start Width##Lit", &app.startWidth, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("End Width##Lit",   &app.endWidth,   0.01f, 0.0f, 100.0f);
                    int planeCount = static_cast<int>(app.planeCount);
                    if (ImGui::DragInt("Plane Count##Lit", &planeCount, 0.1f, 1, 8)) {
                        app.planeCount = static_cast<uint32_t>(planeCount);
                        dirty = true;
                    }
                    dirty |= ImGui::DragFloat("Fade Start Length##Lit", &app.fadeStartLength, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::DragFloat("Fade End Length##Lit",   &app.fadeEndLength,   0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::ColorEdit4("Start Color##Lit", &app.startColor.x);
                    dirty |= ImGui::ColorEdit4("End Color##Lit",   &app.endColor.x);
                    dirty |= ImGui::Checkbox("UV Wrap By Length##Lit", &app.uvWrapByLength);
                    dirty |= ImGui::DragFloat("UV Tiles Per Unit##Lit", &app.uvTilesPerUnit, 0.01f, 0.0f, 100.0f);
                }
                ImGui::TextDisabled("(変更は Restart で反映)");
            }
        }

        // ===== Material（PrimitiveInstance Inspector と同等） =====
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Combo("BlendMode", &c.blendMode, BlendModeNames, IM_ARRAYSIZE(BlendModeNames));
            dirty |= ImGui::Combo("Time Group", &c.timeGroup, TimeGroupNames, IM_ARRAYSIZE(TimeGroupNames));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("アニメ進行の時間グループ。World=シーンのスローに従う / Effect=必殺技の全停止中でも動く");
            dirty |= ImGui::Checkbox("DepthWrite", &c.depthWrite);
            dirty |= ImGui::SliderFloat("Alpha Discard", &c.alphaReference, 0.0f, 1.0f, "%.3f");
            dirty |= ImGui::Checkbox("Cull Backface", &c.cullBackface);
            const char* samplerItems[] = { "Wrap U / Wrap V", "Wrap U / Clamp V", "Clamp U / Clamp V" };
            dirty |= ImGui::Combo("Sampler", &c.samplerMode, samplerItems, IM_ARRAYSIZE(samplerItems));
            dirty |= ImGui::DragFloat("View Angle Fade Power", &c.viewAngleFadePower, 0.05f, 0.0f, 16.0f,
                "%.2f (0=無効、雷/レーザーで斜め面をフェード)");
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

        // ===== Dissolve（オブジェクト単位のディゾルブ） =====
        if (ImGui::CollapsingHeader("Dissolve", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Checkbox("Use Dissolve", &c.useDissolve);

            if (c.useDissolve) {
                ImGui::Indent();

                // --- マスクテクスチャ（Sprite ドラッグ&ドロップ） ---
                ImGui::Text("Mask:");
                const std::string mkcurr = c.dissolveMaskPath.empty() ? "(none)" : c.dissolveMaskPath;
                ImGui::Button(mkcurr.c_str(), ImVec2(-FLT_MIN, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                        const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                        c.dissolveMaskPath = p->texturePath;
                        dirty = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextDisabled("drop a Sprite from SceneEditor here");
                if (ImGui::Button("Reset Mask")) {
                    c.dissolveMaskPath.clear();
                    dirty = true;
                }

                ImGui::Separator();

                // --- 出現（In）: コンポーネント出現を 0 秒として、ディゾルブで現れる ---
                dirty |= ImGui::Checkbox("Dissolve In (出現)", &c.dissolveInEnable);
                if (c.dissolveInEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::DragFloat("Start Time##DissolveIn", &c.dissolveInStartTime, 0.01f, 0.0f, 60.0f,
                        "%.2f s (出現から何秒後に現れ始めるか)");
                    dirty |= ImGui::DragFloat("Duration##DissolveIn", &c.dissolveInDuration, 0.01f, 0.0001f, 60.0f,
                        "%.2f s (現れ切るまでの秒数)");
                    ImGui::Unindent();
                }

                ImGui::Separator();

                // --- 消滅（Out）: ディゾルブで消えていく ---
                dirty |= ImGui::Checkbox("Dissolve Out (消滅)", &c.dissolveOutEnable);
                if (c.dissolveOutEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::DragFloat("Start Time##DissolveOut", &c.dissolveOutStartTime, 0.01f, 0.0f, 60.0f,
                        "%.2f s (出現から何秒後に消え始めるか)");
                    dirty |= ImGui::DragFloat("Duration##DissolveOut", &c.dissolveOutDuration, 0.01f, 0.0001f, 60.0f,
                        "%.2f s (消え切るまでの秒数)");
                    ImGui::Unindent();
                }

                ImGui::Separator();

                // --- 進行度カーブ（In/Out 共用。progress→t を再マップ） ---
                if (ImGui::TreeNode("Dissolve Curve (進行度→t)")) {
                    dirty |= DrawCurveEditor("DissolveCurve", c.dissolveCurve);
                    ImGui::TreePop();
                }

                ImGui::Separator();

                // --- アウトライン（燃えるエッジ）: In/Out 共用 ---
                dirty |= ImGui::Checkbox("Edge Outline (アウトライン)", &c.dissolveEdgeEnable);
                if (c.dissolveEdgeEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::ColorEdit4("Edge Color##Dissolve", &c.dissolveEdgeColor.x);
                    dirty |= ImGui::SliderFloat("Edge Width##Dissolve", &c.dissolveEdgeWidth, 0.0f, 0.5f, "%.3f");
                    ImGui::Unindent();
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
        if (c.gpuParticleGroupName.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Group Name が空だと描画されません。任意の名前を入力してください");
        } else {
            ImGui::TextDisabled("(未登録の名前なら下の Texture Path で自動生成)");
        }
        char texBuf[256];
        std::snprintf(texBuf, sizeof(texBuf), "%s", c.texturePath.c_str());
        if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
            c.texturePath = texBuf;
            dirty = true;
        }
        // テクスチャ D&D（SceneEditor の Sprite をドロップ）。既存グループにもライブ反映される。
        {
            const std::string txcurr = c.texturePath.empty() ? "(default circle)" : c.texturePath;
            ImGui::Button(("Tex: " + txcurr).c_str(), ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                    const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                    c.texturePath = p->texturePath;
                    dirty = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TextDisabled("drop a Sprite from SceneEditor here (live反映)");
        }
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
        dirty |= ImGui::Combo("BlendMode", &c.blendMode, BlendModeNames, IM_ARRAYSIZE(BlendModeNames));
        dirty |= ImGui::Combo("Time Group", &c.timeGroup, TimeGroupNames, IM_ARRAYSIZE(TimeGroupNames));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("粒子シミュの時間グループ。World=シーンのスローに従う / Effect=必殺技の全停止中でも動く");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("加算(Add)では黒系の粒子が映らない。黒い煙などは Normal を選ぶ");

        // ===== Rotation（3D姿勢。ビルボード None で有効） =====
        ImGui::SeparatorText("Rotation (3D)");
        if (c.billboardMode != BillboardMode::None) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Billboard を None にすると3D回転が有効（破片タンブル）");
        }
        dirty |= ImGui::Checkbox("Random Rotate On Spawn", &c.randomRotateOnSpawn);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("出現時に各軸 ±範囲のランダム初期姿勢を粒子ごとに与える");
        if (c.randomRotateOnSpawn) {
            Vector3 randRangeDeg = RadToDeg(c.randomRotateRange);
            if (ImGui::DragFloat3("Random Range (±deg)", &randRangeDeg.x, 1.0f, 0.0f, 180.0f)) {
                c.randomRotateRange = DegToRad(randRangeDeg);
                dirty = true;
            }
        }
        Vector3 rotateSpeedDeg = RadToDeg(c.rotateSpeed);
        if (ImGui::DragFloat3("Rotate Speed (deg/s)", &rotateSpeedDeg.x, 1.0f)) {
            c.rotateSpeed = DegToRad(rotateSpeedDeg);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("出ている間、各軸を回し続ける角速度（全粒子共通。0で停止）");

        // ===== Color =====
        ImGui::Separator();
        const char* colorModeNames[] = { "Random", "Fixed" };
        dirty |= ImGui::Combo("Color Mode", &c.colorMode, colorModeNames, IM_ARRAYSIZE(colorModeNames));
        if (c.colorMode == 1) {
            dirty |= ImGui::ColorEdit4("Start Color", &c.startColor.x);
            dirty |= ImGui::ColorEdit4("End Color",   &c.endColor.x);

            // ----- 多色グラデーション（中間キー） -----
            // Start/End は常に両端(0/1)。ここで足すのは「その間に挿入する中間キー」。
            ImGui::Spacing();
            ImGui::TextDisabled("中間キー（Start→End の間に挿入。1個足すと3色グラデ）");
            const size_t kMaxMidKeys = 6; // Start/End と合わせて最大8キー
            int removeIdx = -1;
            for (size_t k = 0; k < c.colorKeys.size(); ++k) {
                ImGui::PushID(static_cast<int>(k));
                ImGui::SetNextItemWidth(90.0f);
                dirty |= ImGui::DragFloat("##loc", &c.colorKeys[k].location, 0.005f, 0.0f, 1.0f, "Loc %.2f");
                ImGui::SameLine();
                dirty |= ImGui::ColorEdit4("##col", &c.colorKeys[k].color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeIdx = static_cast<int>(k);
                ImGui::PopID();
            }
            if (removeIdx >= 0) { c.colorKeys.erase(c.colorKeys.begin() + removeIdx); dirty = true; }
            if (c.colorKeys.size() < kMaxMidKeys) {
                if (ImGui::Button("+ Add Mid Key")) {
                    EffectColorKey nk;
                    nk.location = 0.5f;
                    nk.color = { (c.startColor.x + c.endColor.x) * 0.5f,
                                 (c.startColor.y + c.endColor.y) * 0.5f,
                                 (c.startColor.z + c.endColor.z) * 0.5f,
                                 (c.startColor.w + c.endColor.w) * 0.5f };
                    c.colorKeys.push_back(nk);
                    dirty = true;
                }
            } else {
                ImGui::TextDisabled("(中間キーは最大 %d 個)", static_cast<int>(kMaxMidKeys));
            }
        } else {
            ImGui::TextDisabled("(Random: 色は GPU 側でランダム生成)");
        }
        // Hue 回転（生存中のシームレス色変化）。Random/Fixed どちらにも乗る。位相を粒子ごとにばらすと虹色ドリフト。
        dirty |= ImGui::Checkbox("Hue Shift", &c.hueShiftEnable);
        if (c.hueShiftEnable) {
            dirty |= ImGui::DragFloat("Hue Speed (rev/s)", &c.hueShiftSpeed, 0.01f, -5.0f, 5.0f, "%.2f");
            dirty |= ImGui::Checkbox("Hue Random Phase (粒子ごとにばらす)", &c.hueShiftRandomPhase);
        }

        // ===== Scale Range =====
        ImGui::Separator();
        dirty |= ImGui::Checkbox("Uniform Scale (W=H)", &c.uniformScale);
        dirty |= ImGui::DragFloat2("Scale Min (W/H)", &c.scaleMin.x, 0.01f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloat2("Scale Max (W/H)", &c.scaleMax.x, 0.01f, 0.0f, 100.0f);
        if (c.uniformScale) {
            ImGui::TextDisabled("(Uniform: Min/Max の X 範囲のみ使われ、W=H に固定)");
        }
        // 寿命に沿ったサイズ倍率（発生サイズ × lerp(Start,End)）
        dirty |= ImGui::DragFloat("Start Scale", &c.startScale, 0.01f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloat("End Scale",   &c.endScale,   0.01f, 0.0f, 100.0f);
        ImGui::TextDisabled("(発生サイズ × 寿命比率で Start→End 倍率。1/1 で変化なし)");

        // ===== Emit / Lifetime =====
        ImGui::Separator();
        const char* shapeNames[] = { "Sphere (球)", "Ring (円周)" };
        dirty |= ImGui::Combo("Emit Shape", &c.emitShape, shapeNames, IM_ARRAYSIZE(shapeNames));
        if (c.emitShape == 1) {
            dirty |= ImGui::DragFloat3("Ring Normal", &c.ringNormal.x, 0.01f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("リング平面の法線（向き）。例: (0,0,1)=画面に正対 / (0,1,0)=水平");
            dirty |= ImGui::DragFloat("Ring Thickness", &c.ringThickness, 0.005f, 0.0f, 10.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("円周まわりの散らばり（太さ）");
        }
        dirty |= ImGui::DragFloat("Emit Radius", &c.emitRadius, 0.01f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("発生位置の散らばり半径（Ring では円の半径）");
        dirty |= ImGui::DragFloat("Particle Life (s)", &c.particleLifeTime, 0.01f, 0.0001f, 60.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("各粒子の寿命（Effect の totalDuration を超えない範囲でクランプ）");

        // ===== Velocity =====
        ImGui::Separator();
        const char* velModeNames[] = { "Random (全方向)", "Directional (方向固定)", "Radial (放射/外向き)", "Tangential (接線/公転)" };
        dirty |= ImGui::Combo("Velocity Mode", &c.velocityMode, velModeNames, IM_ARRAYSIZE(velModeNames));
        if (c.velocityMode == 1) {
            dirty |= ImGui::DragFloat3("Velocity Dir", &c.velocityDir.x, 0.01f);
            dirty |= ImGui::DragFloat("Velocity Speed", &c.velocitySpeed, 0.05f, 0.0f, 200.0f);
            dirty |= ImGui::DragFloat("Velocity Jitter", &c.velocityJitter, 0.02f, 0.0f, 50.0f);
        } else if (c.velocityMode == 2) {
            dirty |= ImGui::DragFloat("Velocity Speed (放射)", &c.velocitySpeed, 0.05f, 0.0f, 200.0f);
            dirty |= ImGui::DragFloat("Velocity Jitter", &c.velocityJitter, 0.02f, 0.0f, 50.0f);
            ImGui::TextDisabled("(中心から外向きに噴出。Emit Radius は小さめ推奨)");
        } else if (c.velocityMode == 3) {
            dirty |= ImGui::DragFloat("Velocity Speed (公転)", &c.velocitySpeed, 0.05f, 0.0f, 200.0f);
            dirty |= ImGui::DragFloat("Velocity Jitter", &c.velocityJitter, 0.02f, 0.0f, 50.0f);
            dirty |= ImGui::DragFloat3("Ring Normal (軸)", &c.ringNormal.x, 0.01f);
            ImGui::TextDisabled("(Ring Normal 軸まわりに公転。Emit Shape=Ring と併用で「流れるリング」)");
        } else {
            ImGui::TextDisabled("(Random: 従来の全方向ランダム初速)");
        }

        // ===== Orbit（周回）=====
        ImGui::Separator();
        dirty |= ImGui::Checkbox("Orbit (周回/外に出ない)", &c.orbitEnabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON で粒子を中心まわりに回し続ける（初速で飛ばさず一定半径）。\nSpin=帯上を流れる / Tumble=帯自体が回る。両方を粒子が受ける。");
        if (c.orbitEnabled) {
            dirty |= ImGui::DragFloat("Spin Speed (帯上を流れる)", &c.orbitSpinSpeed, 0.05f, -50.0f, 50.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ring Normal 軸まわりにリング上を流れる速度");
            dirty |= ImGui::DragFloat("Tumble Speed (帯自体の回転)", &c.orbitTumbleSpeed, 0.05f, -50.0f, 50.0f);
            dirty |= ImGui::DragFloat3("Tumble Axis", &c.orbitTumbleAxis.x, 0.01f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("帯（リング平面）自体を回す軸。Ring Normal と直交させると首を振る");
        }

        // ===== Converge（収束：spawn位置→中心。移動をカーブで制御）=====
        ImGui::Separator();
        dirty |= ImGui::Checkbox("Converge (中心へ収束)", &c.convergeEnable);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON で各粒子を spawn 位置からエミッタ中心へ寄せる（初速/Orbit より優先）。\n集まり方は下のカーブで制御（0=spawn, 1=中心）。Emit Shape を球にすると球状から収束。");
        if (c.convergeEnable) {
            dirty |= DrawCurveEditor("ConvergeCurve", c.convergeCurve);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("収束の進み具合（横=寿命比率0..1 / 縦=spawn→中心 0..1）。\n後半で急に寄せる、等の緩急を Scale/Dissolve と同じ感覚で描ける。");
        }

        // ===== Dissolve（粒子ごとの寿命ディゾルブ）=====
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Dissolve", ImGuiTreeNodeFlags_DefaultOpen)) {
            dirty |= ImGui::Checkbox("Use Dissolve##Particle", &c.useDissolve);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("各粒子が自分の寿命比率(0..1)に応じて個別に溶ける");

            if (c.useDissolve) {
                ImGui::Indent();

                // --- マスク（Sprite ドラッグ&ドロップ） ---
                ImGui::Text("Mask:");
                const std::string mkcurr = c.dissolveMaskPath.empty() ? "(none)" : c.dissolveMaskPath;
                ImGui::Button(mkcurr.c_str(), ImVec2(-FLT_MIN, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                        const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
                        c.dissolveMaskPath = p->texturePath;
                        dirty = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextDisabled("drop a Sprite from SceneEditor here");
                if (ImGui::Button("Reset Mask##Particle")) {
                    c.dissolveMaskPath.clear();
                    dirty = true;
                }

                ImGui::Separator();

                // --- 出現（In）: 寿命の最初で材質化 ---
                dirty |= ImGui::Checkbox("Dissolve In (出現)##Particle", &c.dissolveInEnable);
                if (c.dissolveInEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::SliderFloat("In End (寿命比)##Particle", &c.dissolveInEnd, 0.0f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("寿命のこの比率までに出現完了");
                    ImGui::Unindent();
                }

                ImGui::Separator();

                // --- 消滅（Out）: 寿命の最後で燃え尽き ---
                dirty |= ImGui::Checkbox("Dissolve Out (消滅)##Particle", &c.dissolveOutEnable);
                if (c.dissolveOutEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::SliderFloat("Out Start (寿命比)##Particle", &c.dissolveOutStart, 0.0f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("寿命のこの比率から消え始める");
                    ImGui::Unindent();
                }

                ImGui::Separator();

                // --- アウトライン ---
                dirty |= ImGui::Checkbox("Edge Outline (アウトライン)##Particle", &c.dissolveEdgeEnable);
                if (c.dissolveEdgeEnable) {
                    ImGui::Indent();
                    dirty |= ImGui::ColorEdit4("Edge Color##Particle", &c.dissolveEdgeColor.x);
                    dirty |= ImGui::SliderFloat("Edge Width##Particle", &c.dissolveEdgeWidth, 0.0f, 0.5f, "%.3f");
                    ImGui::Unindent();
                }

                ImGui::Unindent();
            }
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
