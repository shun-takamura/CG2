#include "EffectEditorWindow.h"
#include "RenderTexture.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "LineRenderer.h"
#include "EffectManager.h"
#include "ImGuiManager.h"
#include "SceneEditorWindow.h"
#include "Vector3.h"
#include "Vector4.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace {
    constexpr uint32_t kRtSizeStep = 32;
    constexpr uint32_t kRtMin = 128;
    constexpr uint32_t kRtMax = 2048;

    uint32_t SnapRtSize(float v) {
        if (v < (float)kRtMin) return kRtMin;
        if (v > (float)kRtMax) return kRtMax;
        uint32_t i = static_cast<uint32_t>(v);
        return (i / kRtSizeStep) * kRtSizeStep;
    }
}

EffectEditorWindow::EffectEditorWindow(DirectXCore* dxCore, SRVManager* srvManager)
    : IImGuiWindow("EffectEditor"), dxCore_(dxCore), srvManager_(srvManager) {
    SetInitialSize(ImVec2(640, 480));
}

EffectEditorWindow::~EffectEditorWindow() {
    Finalize();
}

void EffectEditorWindow::Initialize() {
    debugCamera_.Initialize();
    debugCamera_.SetAspectRatio(1.0f);
    debugCamera_.SetPivot({ 0.0f, 0.0f, 0.0f });
    debugCamera_.SetDistance(10.0f);
    debugCamera_.Orbit(0.0f, 0.5f);

    EnsureRenderTextureSize(512, 512);

    NewEffect();
}

void EffectEditorWindow::Finalize() {
    if (oldRenderTexture_) {
        oldRenderTexture_->Finalize();
        oldRenderTexture_.reset();
    }
    if (renderTexture_) {
        renderTexture_->Finalize();
        renderTexture_.reset();
    }
}

void EffectEditorWindow::EnsureRenderTextureSize(uint32_t width, uint32_t height) {
    if (width == rtWidth_ && height == rtHeight_ && renderTexture_) return;

    if (renderTexture_) {
        if (oldRenderTexture_) {
            oldRenderTexture_->Finalize();
            oldRenderTexture_.reset();
        }
        oldRenderTexture_ = std::move(renderTexture_);
    }

    renderTexture_ = std::make_unique<RenderTexture>();
    const float clear[4] = { 0.15f, 0.15f, 0.18f, 1.0f };
    renderTexture_->Initialize(dxCore_, srvManager_, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
    rtWidth_ = width;
    rtHeight_ = height;

    debugCamera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

void EffectEditorWindow::HandleMouseInput() {
    if (!isHovered_) return;

    ImGuiIO& io = ImGui::GetIO();
    const float orbitSpeed = 0.005f;
    const float panSpeed   = 0.005f;
    const float zoomSpeed  = 0.5f;

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 d = io.MouseDelta;
        debugCamera_.Orbit(d.x * orbitSpeed, d.y * orbitSpeed);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        ImVec2 d = io.MouseDelta;
        debugCamera_.Pan(-d.x * panSpeed, d.y * panSpeed);
    }
    if (io.MouseWheel != 0.0f) {
        debugCamera_.Zoom(-io.MouseWheel * zoomSpeed);
    }
}

void EffectEditorWindow::AddGridLines() {
    LineRenderer* lr = LineRenderer::GetInstance();
    if (!lr) return;

    const Vector4 colorMajor = { 0.5f, 0.5f, 0.55f, 1.0f };
    const Vector4 colorMinor = { 0.25f, 0.25f, 0.28f, 1.0f };
    const Vector4 colorAxisX = { 0.8f, 0.3f, 0.3f, 1.0f };
    const Vector4 colorAxisZ = { 0.3f, 0.5f, 0.8f, 1.0f };

    const int   halfCount = 10;
    const float step      = 1.0f;
    const float extent    = halfCount * step;

    for (int i = -halfCount; i <= halfCount; ++i) {
        float p = i * step;
        Vector4 c = (i == 0) ? colorAxisZ : ((i % 5 == 0) ? colorMajor : colorMinor);
        lr->AddLine({ p, 0.0f, -extent }, { p, 0.0f, extent }, c, LineRenderer::Pass::Preview);
        c = (i == 0) ? colorAxisX : ((i % 5 == 0) ? colorMajor : colorMinor);
        lr->AddLine({ -extent, 0.0f, p }, { extent, 0.0f, p }, c, LineRenderer::Pass::Preview);
    }
}

void EffectEditorWindow::Render() {
    if (pendingWidth_ > 0 && pendingHeight_ > 0 &&
        (pendingWidth_ != rtWidth_ || pendingHeight_ != rtHeight_)) {
        EnsureRenderTextureSize(pendingWidth_, pendingHeight_);
    }
    if (oldRenderTexture_) {
        oldRenderTexture_->Finalize();
        oldRenderTexture_.reset();
    }

    if (!renderTexture_ || !isOpen_) return;

    debugCamera_.Update();
    camera_.SetExternalMatrices(
        debugCamera_.GetViewMatrix(),
        debugCamera_.GetProjectionMatrix(),
        debugCamera_.GetTranslate());

    auto* commandList = dxCore_->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
    renderTexture_->BeginRender(commandList, &dsv);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(rtWidth_);
    vp.Height = static_cast<float>(rtHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &vp);
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(rtWidth_), static_cast<LONG>(rtHeight_) };
    commandList->RSSetScissorRects(1, &sc);

    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    LineRenderer* lr = LineRenderer::GetInstance();
    if (lr) {
        lr->SetCamera(&camera_, LineRenderer::Pass::Preview);
        AddGridLines();
        lr->Draw(LineRenderer::Pass::Preview);
    }

    EffectManager::GetInstance()->SetPreviewCamera(&camera_);
    EffectManager::GetInstance()->DrawPreview();

    renderTexture_->EndRender(commandList);
}

void EffectEditorWindow::OnDraw() {
#ifdef _DEBUG
    if (!renderTexture_ || !srvManager_) {
        ImGui::Text("RenderTexture not initialized");
        return;
    }

    // 前フレームの Inspector で立った削除要求をここで安全に処理
    if (!pendingRemovals_.empty()) {
        // index 大きい順に消す（小さい index から消すと後続のIDがずれる）
        std::sort(pendingRemovals_.begin(), pendingRemovals_.end(),
            [](const PendingRemoval& a, const PendingRemoval& b) { return a.index > b.index; });
        for (const auto& r : pendingRemovals_) {
            auto inRange = [](int i, size_t n) { return i >= 0 && i < static_cast<int>(n); };
            switch (r.kind) {
            case EffectComponentEditable::Kind::Primitive:
                if (inRange(r.index, editBuffer_.primitives.size()))
                    editBuffer_.primitives.erase(editBuffer_.primitives.begin() + r.index);
                break;
            case EffectComponentEditable::Kind::Particle:
                if (inRange(r.index, editBuffer_.particles.size()))
                    editBuffer_.particles.erase(editBuffer_.particles.begin() + r.index);
                break;
            case EffectComponentEditable::Kind::Light:
                if (inRange(r.index, editBuffer_.lights.size()))
                    editBuffer_.lights.erase(editBuffer_.lights.begin() + r.index);
                break;
            case EffectComponentEditable::Kind::Sound:
                if (inRange(r.index, editBuffer_.sounds.size()))
                    editBuffer_.sounds.erase(editBuffer_.sounds.begin() + r.index);
                break;
            }
        }
        pendingRemovals_.clear();
        editBufferDirty_ = true;
        ImGuiManager::Instance().SetSelected(nullptr);
        RebuildEditables();
    }

    EffectManager* em = EffectManager::GetInstance();

    // ============ 上部：定義選択 + Save + Play ============
    auto defs = em->ListDefNames();
    if (selectedEffect_.empty() && !defs.empty()) {
        selectedEffect_ = defs.front();
        LoadIntoBuffer(selectedEffect_);
    }

    if (ImGui::BeginCombo("Effect", selectedEffect_.c_str())) {
        for (const auto& name : defs) {
            bool sel = (name == selectedEffect_);
            if (ImGui::Selectable(name.c_str(), sel)) {
                selectedEffect_ = name;
                LoadIntoBuffer(name);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New")) {
        NewEffect();
    }

    ImGui::InputText("Name", editNameInput_, IM_ARRAYSIZE(editNameInput_));
    if (ImGui::Button("Save")) SaveCurrent();
    ImGui::SameLine();
    if (editBufferDirty_) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "(modified)");

    ImGui::DragFloat("Total Duration", &editBuffer_.totalDuration, 0.05f, 0.0f, 60.0f);

    ImGui::Separator();
    ImGui::DragFloat3("Play Pos", playPos_, 0.1f);
    if (ImGui::Button("Play")) {
        // editBuffer の現在の状態をそのまま再生（未保存でも反映）
        em->PlayWithDef(editBuffer_, Vector3{ playPos_[0], playPos_[1], playPos_[2] });
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        em->StopAll();
        em->PlayWithDef(editBuffer_, Vector3{ playPos_[0], playPos_[1], playPos_[2] });
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) em->StopAll();

    // Timeline：最初の再生中インスタンスの経過時間 / 総寿命をプログレスバーで表示
    {
        EffectInstance* inst = em->GetFirstActiveInstance();
        float elapsed = inst ? inst->GetElapsedTime() : 0.0f;
        float total   = inst ? inst->GetTotalDuration() : editBuffer_.totalDuration;
        if (total < 0.0001f) total = 0.0001f;
        float ratio = std::clamp(elapsed / total, 0.0f, 1.0f);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%.2f / %.2f s", elapsed, total);
        ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 0), overlay);
    }

    ImGui::Separator();

    // ============ Viewport（残り領域いっぱい） ============
    ImVec2 avail = ImGui::GetContentRegionAvail();
    uint32_t newW = SnapRtSize(avail.x);
    uint32_t newH = SnapRtSize(avail.y);
    if (newW > 0 && newH > 0) {
        pendingWidth_ = newW;
        pendingHeight_ = newH;
    }

    uint32_t srvIndex = renderTexture_->GetSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvManager_->GetGPUDescriptorHandle(srvIndex);

    float texW = (float)renderTexture_->GetWidth();
    float texH = (float)renderTexture_->GetHeight();
    float texAspect = texW / texH;
    float winAspect = (avail.y > 0) ? avail.x / avail.y : 1.0f;
    ImVec2 imgSize;
    if (winAspect > texAspect) {
        imgSize.y = avail.y;
        imgSize.x = avail.y * texAspect;
    } else {
        imgSize.x = avail.x;
        imgSize.y = avail.x / texAspect;
    }

    imageScreenPos_ = ImGui::GetCursorScreenPos();
    imageScreenSize_ = imgSize;

    ImGui::Image((ImTextureID)gpuHandle.ptr, imgSize, ImVec2(0,0), ImVec2(1,1));
    isHovered_ = ImGui::IsItemHovered();

    // ===== Drop: Effect Component =====
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EFFECT_COMP_DROP_PAYLOAD_TYPE)) {
            const EffectComponentDropPayload* p = static_cast<const EffectComponentDropPayload*>(payload->Data);
            AddComponent(static_cast<EffectComponentEditable::Kind>(p->kind));
        }
        ImGui::EndDragDropTarget();
    }

    HandleMouseInput();

    ImGui::TextDisabled("L-Drag: Orbit  /  M-Drag: Pan  /  Wheel: Zoom");
#endif
}

//==========================================================
// Editor logic
//==========================================================

void EffectEditorWindow::LoadIntoBuffer(const std::string& defName) {
    const EffectDef* src = EffectManager::GetInstance()->FindDef(defName);
    if (!src) {
        NewEffect();
        return;
    }
    editBuffer_ = *src;
    editBufferDirty_ = false;
    std::snprintf(editNameInput_, sizeof(editNameInput_), "%s", editBuffer_.name.c_str());
    RebuildEditables();
}

void EffectEditorWindow::NewEffect() {
    editBuffer_ = EffectDef{};
    editBuffer_.name = "Untitled";
    editBuffer_.totalDuration = 1.0f;
    editBufferDirty_ = true;
    std::snprintf(editNameInput_, sizeof(editNameInput_), "%s", editBuffer_.name.c_str());
    RebuildEditables();
}

void EffectEditorWindow::SaveCurrent() {
    editBuffer_.name = editNameInput_;
    std::string finalName = EffectManager::GetInstance()->SaveDef(editBuffer_);
    if (!finalName.empty()) {
        editBuffer_.name = finalName;
        std::snprintf(editNameInput_, sizeof(editNameInput_), "%s", finalName.c_str());
        selectedEffect_ = finalName;
        editBufferDirty_ = false;
    }
}

void EffectEditorWindow::AddComponent(EffectComponentEditable::Kind kind) {
    switch (kind) {
    case EffectComponentEditable::Kind::Primitive: editBuffer_.primitives.push_back({}); break;
    case EffectComponentEditable::Kind::Particle:  editBuffer_.particles.push_back({});  break;
    case EffectComponentEditable::Kind::Light:     editBuffer_.lights.push_back({});     break;
    case EffectComponentEditable::Kind::Sound:     editBuffer_.sounds.push_back({});     break;
    }
    editBufferDirty_ = true;
    RebuildEditables();
}

void EffectEditorWindow::RemoveComponent(EffectComponentEditable::Kind kind, int index) {
    // 即時に erase + RebuildEditables を呼ぶと、現在 OnImGuiInspector 中の
    // EffectComponentEditable 自身が削除されて use-after-free になる。
    // 次の OnDraw 冒頭で安全に処理するため pending キューへ。
    pendingRemovals_.push_back({ kind, index });
}

void EffectEditorWindow::RebuildEditables() {
    // 選択中の Editable があれば、その kind+index を覚えておいて再構築後に対応する Editable を再選択
    ImGuiManager& mgr = ImGuiManager::Instance();
    EffectComponentEditable::Kind selKind = EffectComponentEditable::Kind::Primitive;
    int selIndex = -1;
    if (auto* sel = dynamic_cast<EffectComponentEditable*>(mgr.GetSelected())) {
        selKind = sel->GetKind();
        selIndex = sel->GetIndex();
    }

    editables_.clear();
    for (int i = 0; i < static_cast<int>(editBuffer_.primitives.size()); ++i) {
        editables_.push_back(std::make_unique<EffectComponentEditable>(this, EffectComponentEditable::Kind::Primitive, i));
    }
    for (int i = 0; i < static_cast<int>(editBuffer_.particles.size()); ++i) {
        editables_.push_back(std::make_unique<EffectComponentEditable>(this, EffectComponentEditable::Kind::Particle, i));
    }
    for (int i = 0; i < static_cast<int>(editBuffer_.lights.size()); ++i) {
        editables_.push_back(std::make_unique<EffectComponentEditable>(this, EffectComponentEditable::Kind::Light, i));
    }
    for (int i = 0; i < static_cast<int>(editBuffer_.sounds.size()); ++i) {
        editables_.push_back(std::make_unique<EffectComponentEditable>(this, EffectComponentEditable::Kind::Sound, i));
    }

    // 選択復元
    mgr.SetSelected(nullptr);
    if (selIndex >= 0) {
        for (auto& e : editables_) {
            if (e->GetKind() == selKind && e->GetIndex() == selIndex) {
                mgr.SetSelected(e.get());
                break;
            }
        }
    }
}
