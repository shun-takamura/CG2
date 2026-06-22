#include "CurveEditorWidget.h"
#include "EffectDef.h"
#include "Vector2.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace {
    // グリッド吸着の分割数（1/kCurveGridDiv 刻みに吸着）
    constexpr int kCurveGridDiv = 10;
}

namespace EffectUI {

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

} // namespace EffectUI
