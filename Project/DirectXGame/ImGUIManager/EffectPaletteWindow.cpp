#include "EffectPaletteWindow.h"
#include "Effect/EffectComponentEditable.h"  // EffectComponentEditable::Kind
#include "EditorDropPayload.h"               // EFFECT_COMP_DROP / EffectComponentDropPayload
#include "Effect/EffectDef.h"                // EffectRibbonMode
#include "imgui.h"

namespace {
    // ウィンドウの初めの大きさ [px]
    constexpr int kInitialWidth = 180;
    constexpr int kInitialHeight = 320;
    // ImGui の ID がほかの項目とぶつからないように足す値（Primitive の D&D 元 / 種別の D&D 元）
    constexpr int kPrimitiveSourceIdOffset = 700000;
    constexpr int kKindSourceIdOffset = 710000;
    constexpr int kRibbonSourceIdOffset = 720000;
}

EffectPaletteWindow::EffectPaletteWindow()
    : IImGuiWindow("Effect Components") {
    SetInitialSize(ImVec2(kInitialWidth, kInitialHeight));
}

void EffectPaletteWindow::OnDraw() {
#ifdef _DEBUG
    ImGui::TextDisabled("drag onto EffectEditor viewport");

    // kind==Primitive の D&D ソース。meshType を payload に載せる（置いた瞬間に形状確定）。
    auto primitiveSource = [](const char* label, int meshType) {
        ImGui::PushID(meshType + kPrimitiveSourceIdOffset);
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
        ImGui::PushID(static_cast<int>(kind) + kKindSourceIdOffset);
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
    const char* meshTypeNames[] = { "Plane", "Box", "Sphere", "Ring", "Cylinder", "Helix", "Beam", "Lightning", "Hemisphere", "Frame" };
    ImGui::SeparatorText("Primitive");
    for (int i = 0; i < IM_ARRAYSIZE(meshTypeNames); ++i) {
        primitiveSource(meshTypeNames[i], i);
    }

    ImGui::SeparatorText("Other");
    kindSource("Particle", EffectComponentEditable::Kind::Particle);
    kindSource("Light",    EffectComponentEditable::Kind::Light);
    kindSource("Sound",    EffectComponentEditable::Kind::Sound);

    // Ribbon は動き方（Trail / Beam）を meshType に載せる
    ImGui::SeparatorText("Ribbon");
    auto ribbonSource = [](const char* label, int mode) {
        ImGui::PushID(mode + kRibbonSourceIdOffset);
        ImGui::Selectable(label, false);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            EffectComponentDropPayload payload{};
            payload.kind = static_cast<int>(EffectComponentEditable::Kind::Ribbon);
            payload.meshType = mode;
            ImGui::SetDragDropPayload(EFFECT_COMP_DROP_PAYLOAD_TYPE, &payload, sizeof(payload));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        };
    ribbonSource("Trail (弾の尾)", EffectRibbonMode::Trail);
    ribbonSource("Beam (ビーム)",  EffectRibbonMode::Beam);
#endif
}
