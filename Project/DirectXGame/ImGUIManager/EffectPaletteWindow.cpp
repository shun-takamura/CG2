#include "EffectPaletteWindow.h"
#include "Effect/EffectComponentEditable.h"  // EffectComponentEditable::Kind
#include "EditorDropPayload.h"               // EFFECT_COMP_DROP / EffectComponentDropPayload
#include "imgui.h"

EffectPaletteWindow::EffectPaletteWindow()
    : IImGuiWindow("Effect Components") {
    SetInitialSize(ImVec2(180, 320));
}

void EffectPaletteWindow::OnDraw() {
#ifdef _DEBUG
    ImGui::TextDisabled("drag onto EffectEditor viewport");

    // kind==Primitive の D&D ソース。meshType を payload に載せる（置いた瞬間に形状確定）。
    auto primitiveSource = [](const char* label, int meshType) {
        ImGui::PushID(meshType + 700000);
        ImGui::Selectable(label, false);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            EffectComponentDropPayload payload{};
            payload.kind = static_cast<int>(EffectComponentEditable::Kind::Primitive);
            payload.meshType = meshType;
            ImGui::SetDragDropPayload(EFFECT_COMP_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        };

    // Primitive 以外（種別だけ。meshType は無視される）の D&D ソース。
    auto kindSource = [](const char* label, EffectComponentEditable::Kind kind) {
        ImGui::PushID(static_cast<int>(kind) + 710000);
        ImGui::Selectable(label, false);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            EffectComponentDropPayload payload{};
            payload.kind = static_cast<int>(kind);
            payload.meshType = 0;
            ImGui::SetDragDropPayload(EFFECT_COMP_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        };

    // meshType の順番は EffectDef.h / EffectComponentEditable の MeshTypeNames に対応
    const char* meshTypeNames[] = { "Plane", "Box", "Sphere", "Ring", "Cylinder", "Helix", "Beam", "Lightning", "Hemisphere" };
    ImGui::SeparatorText("Primitive");
    for (int i = 0; i < IM_ARRAYSIZE(meshTypeNames); ++i) {
        primitiveSource(meshTypeNames[i], i);
    }

    ImGui::SeparatorText("Other");
    kindSource("Particle", EffectComponentEditable::Kind::Particle);
    kindSource("Light",    EffectComponentEditable::Kind::Light);
    kindSource("Sound",    EffectComponentEditable::Kind::Sound);
#endif
}
