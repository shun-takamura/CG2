#include "CameraRotKey.h"

#include <cstdio>

#ifdef USE_IMGUI
#include "imgui.h"
#include "MathUtility.h"          // RadToDeg / DegToRad
#include "CurveEditorWidget.h"    // EffectUI::DrawCurveEditor
#endif

std::string CameraRotKey::GetName() const {
    if (!name_.empty()) return name_;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "RotKey t=%.2f", t);
    return buf;
}

void CameraRotKey::OnImGuiInspector() {
#ifdef USE_IMGUI
    ImGui::DragFloat("t (progress)", &t, 0.005f, 0.0f, 1.0f, "%.3f");

    // 回転は度で編集（内部はラジアン保存）。pitch/yaw/roll の順。
    Vector3 deg = RadToDeg(rotate);
    if (ImGui::DragFloat3("Rotation (pitch/yaw/roll)", &deg.x, 0.5f)) {
        rotate = DegToRad(deg);
    }

    ImGui::SeparatorText("Ease to next（このキー→次キーの緩急）");
    EffectUI::DrawCurveEditor("EaseToNext", easeToNext);
#endif
}
