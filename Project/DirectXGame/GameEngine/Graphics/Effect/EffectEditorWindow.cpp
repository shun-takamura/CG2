#include "EffectEditorWindow.h"
#include "RenderTexture.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "LineRenderer.h"
#include "EffectManager.h"
#include "IEditorSelection.h"
#include "EditorDropPayload.h"  // EFFECT_COMP_DROP / EffectComponentDropPayload
#include "PostEffect.h"
#include "FilterEffect/DistortionEffect.h"
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

EffectEditorWindow::EffectEditorWindow(DirectXCore* dxCore, SRVManager* srvManager, IEditorSelection* selection)
    : IImGuiWindow("EffectEditor"), dxCore_(dxCore), srvManager_(srvManager), selection_(selection) {
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

    // 自前 PostEffect はここでは作らない。Initialize() は ImGuiManager::Initialize 段階で
    // 呼ばれ、その時点では TextureManager の SRVManager がまだ未設定。PostEffect 内の
    // DissolveEffect がマスクテクスチャをロードすると CanAllocate() で null 参照クラッシュする。
    // 全システムが揃った後の初回利用時に EnsurePostEffect() で遅延生成する。

    NewEffect();
}

void EffectEditorWindow::EnsurePostEffect() {
    if (postEffect_) return;
    // distortion 合成パス用の RS/PSO を用意する。RunDistortionForPreview は外部 SRV と
    // dxCore 共有深度のみ使い内部 RT は使わないので、サイズはプレビュー既定で十分。
    postEffect_ = std::make_unique<PostEffect>();
    postEffect_->Initialize(dxCore_, srvManager_, 512, 512);
}

void EffectEditorWindow::Finalize() {
    if (postEffect_) { postEffect_->Finalize(); postEffect_.reset(); }
    if (oldRenderTexture_) { oldRenderTexture_->Finalize(); oldRenderTexture_.reset(); }
    if (renderTexture_)    { renderTexture_->Finalize();    renderTexture_.reset(); }
    if (oldPreviewDistortionRT_) { oldPreviewDistortionRT_->Finalize(); oldPreviewDistortionRT_.reset(); }
    if (previewDistortionRT_)    { previewDistortionRT_->Finalize();    previewDistortionRT_.reset(); }
    if (oldPreviewOutputRT_)     { oldPreviewOutputRT_->Finalize();     oldPreviewOutputRT_.reset(); }
    if (previewOutputRT_)        { previewOutputRT_->Finalize();        previewOutputRT_.reset(); }
}

void EffectEditorWindow::EnsureRenderTextureSize(uint32_t width, uint32_t height) {
    if (width == rtWidth_ && height == rtHeight_ && renderTexture_) return;

    // 旧 RT を保持して破棄遅延（GPU で参照中の可能性があるため次フレーム冒頭で解放）
    auto swapOut = [](std::unique_ptr<RenderTexture>& live, std::unique_ptr<RenderTexture>& old) {
        if (live) {
            if (old) { old->Finalize(); old.reset(); }
            old = std::move(live);
        }
    };
    swapOut(renderTexture_,        oldRenderTexture_);
    swapOut(previewDistortionRT_,  oldPreviewDistortionRT_);
    swapOut(previewOutputRT_,      oldPreviewOutputRT_);

    // メインのプレビュー RT
    renderTexture_ = std::make_unique<RenderTexture>();
    const float clear[4] = { 0.15f, 0.15f, 0.18f, 1.0f };
    renderTexture_->Initialize(dxCore_, srvManager_, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);

    // Distortion 用：RG=0.5（オフセット0）、A=0（強度0）でクリア。SRGB ではなく UNORM。
    previewDistortionRT_ = std::make_unique<RenderTexture>();
    const float distClear[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
    previewDistortionRT_->Initialize(dxCore_, srvManager_, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, distClear);

    // 歪み合成後の最終表示用
    previewOutputRT_ = std::make_unique<RenderTexture>();
    const float outClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    previewOutputRT_->Initialize(dxCore_, srvManager_, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, outClear);

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
    if (oldRenderTexture_) { oldRenderTexture_->Finalize(); oldRenderTexture_.reset(); }
    if (oldPreviewDistortionRT_) { oldPreviewDistortionRT_->Finalize(); oldPreviewDistortionRT_.reset(); }
    if (oldPreviewOutputRT_) { oldPreviewOutputRT_->Finalize(); oldPreviewOutputRT_.reset(); }

    if (!renderTexture_ || !isOpen_) return;

    debugCamera_.Update();
    camera_.SetExternalMatrices(
        debugCamera_.GetViewMatrix(),
        debugCamera_.GetProjectionMatrix(),
        debugCamera_.GetTranslate());

    // プレビューはエディタが自前タイムラインで進める（シーンのタイムスケール非依存）。
    // unscaled な実 delta を使い、シーンが停止していても等速で動かす。
    EffectManager* emPrev = EffectManager::GetInstance();
    emPrev->SetPreviewCamera(&camera_);
    // 編集中 def をライブ反映してから更新（Restart 不要でスライダー操作が即反映される）。
    if (previewHandle_ != 0) {
        emPrev->SyncPreview(editBuffer_, Vector3{ playPos_[0], playPos_[1], playPos_[2] });
    }
    // 専用タイムライン：一時停止中は dt=0（カメラ追従や編集反映は効く）、それ以外は実 delta×速度。
    const float realDt = dxCore_->GetDeltaTime();
    const float previewDt = (previewHandle_ != 0 && !previewPaused_) ? realDt * previewSpeed_ : 0.0f;
    emPrev->UpdatePreview(previewDt);

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

    // ===== Distortion プレビュー合成 =====
    // エディタ自前の PostEffect を使う（Game の設定には依存しない）。
    // 「Distortion」トグルが ON のときだけプレビューに歪みを適用する。
    lastFrameDistortionApplied_ = false;
    // 自前 PostEffect は初回の歪み利用時に遅延生成する（起動時は TextureManager 未準備のため）。
    if (previewDistortion_) EnsurePostEffect();
    PostEffect* postEffect = postEffect_.get();
    const bool distortionOn = previewDistortion_ && postEffect && postEffect->distortion;

    if (distortionOn && previewDistortionRT_ && previewOutputRT_) {
        // (A) 歪みマップ用 RT に effect primitives の distortion パスを描画
        previewDistortionRT_->BeginRender(commandList);
        const float dclear[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
        commandList->ClearRenderTargetView(previewDistortionRT_->GetRTVHandle(), dclear, 0, nullptr);

        // RTV は previewDistortionRT、DSV は共有（renderTexture_ 描画時の深度を引き継ぐ）
        auto distRtv = previewDistortionRT_->GetRTVHandle();
        commandList->OMSetRenderTargets(1, &distRtv, false, &dsv);
        // viewport / scissor は同サイズなので維持
        srvManager_->PreDraw();

        // プレビュー用 WVP CB はすぐ上の EffectManager::DrawPreview() で更新済み
        EffectManager::GetInstance()->DrawDistortionPassPreview();

        previewDistortionRT_->EndRender(commandList);

        // 合成シェーダーで scene depth を t2 から読むため SRV 状態へ遷移
        dxCore_->TransitionDepthState(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // (B) renderTexture_ + previewDistortionRT_ + depth → previewOutputRT_ に合成
        previewOutputRT_->BeginRender(commandList);
        srvManager_->PreDraw();
        // viewport / scissor も RT サイズに合わせ直す
        commandList->RSSetViewports(1, &vp);
        commandList->RSSetScissorRects(1, &sc);

        postEffect->RunDistortionForPreview(commandList,
            renderTexture_->GetSRVIndex(),
            previewDistortionRT_->GetSRVIndex());

        previewOutputRT_->EndRender(commandList);

        // 次フレーム DSV として使えるよう書き込み状態に戻す
        dxCore_->TransitionDepthState(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        lastFrameDistortionApplied_ = true;
    }
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
        if (selection_) selection_->SetSelected(nullptr);
        RebuildEditables();
    }

    // 複製要求を安全に処理（末尾に追加）
    if (!pendingDuplications_.empty()) {
        auto inRange = [](int i, size_t n) { return i >= 0 && i < static_cast<int>(n); };
        for (const auto& r : pendingDuplications_) {
            switch (r.kind) {
            case EffectComponentEditable::Kind::Primitive:
                if (inRange(r.index, editBuffer_.primitives.size()))
                    editBuffer_.primitives.push_back(editBuffer_.primitives[r.index]);
                break;
            case EffectComponentEditable::Kind::Particle:
                if (inRange(r.index, editBuffer_.particles.size())) {
                    EffectParticleComponent copy = editBuffer_.particles[r.index];
                    // 同じグループ名だと 1フレーム1バーストで衝突し片方しか出ないので、
                    // 複製側のグループ名をユニーク化する（別グループ＝両方バーストされる）。
                    if (!copy.gpuParticleGroupName.empty()) {
                        const std::string base = copy.gpuParticleGroupName;
                        auto used = [&](const std::string& n) {
                            for (const auto& p : editBuffer_.particles)
                                if (p.gpuParticleGroupName == n) return true;
                            return false;
                        };
                        std::string candidate = base;
                        for (int n = 2; used(candidate); ++n) {
                            candidate = base + "_" + std::to_string(n);
                        }
                        copy.gpuParticleGroupName = candidate;
                    }
                    editBuffer_.particles.push_back(copy);
                }
                break;
            case EffectComponentEditable::Kind::Light:
                if (inRange(r.index, editBuffer_.lights.size()))
                    editBuffer_.lights.push_back(editBuffer_.lights[r.index]);
                break;
            case EffectComponentEditable::Kind::Sound:
                if (inRange(r.index, editBuffer_.sounds.size()))
                    editBuffer_.sounds.push_back(editBuffer_.sounds[r.index]);
                break;
            }
        }
        pendingDuplications_.clear();
        editBufferDirty_ = true;
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

    // 既存エフェクトのコンポーネントを「現在の編集」に追加（マージ）。
    // 例：Hit_Small 編集中に Aura のコンポーネント一式を取り込む。
    {
        static std::string mergeSel;
        if ((mergeSel.empty() || std::find(defs.begin(), defs.end(), mergeSel) == defs.end()) && !defs.empty()) {
            mergeSel = defs.front();
        }
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("##mergeSrc", mergeSel.c_str())) {
            for (const auto& name : defs) {
                if (ImGui::Selectable(name.c_str(), name == mergeSel)) mergeSel = name;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Merge")) {
            if (const EffectDef* src = em->FindDef(mergeSel)) {
                for (const auto& c : src->primitives) editBuffer_.primitives.push_back(c);
                for (const auto& c : src->particles)  editBuffer_.particles.push_back(c);
                for (const auto& c : src->lights)     editBuffer_.lights.push_back(c);
                for (const auto& c : src->sounds)     editBuffer_.sounds.push_back(c);
                editBufferDirty_ = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("選択したエフェクトのコンポーネント一式を現在の編集に追加（マージ）");
    }

    ImGui::InputText("Name", editNameInput_, IM_ARRAYSIZE(editNameInput_));
    if (ImGui::Button("Save")) SaveCurrent();
    ImGui::SameLine();
    if (editBufferDirty_) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "(modified)");

    ImGui::DragFloat("Total Duration", &editBuffer_.totalDuration, 0.05f, 0.0f, 60.0f);
    if (ImGui::Checkbox("Loop", &editBuffer_.loop)) {
        editBufferDirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("ON にすると Total Duration 経過後に最初から再生し直す。\n"
                          "弾丸の trail のように寿命を外から制御したい場合は\n"
                          "EffectManager::Stop(handle) で停止する。");
    }

    ImGui::Separator();
    ImGui::DragFloat3("Play Pos", playPos_, 0.1f);
    if (ImGui::Button("Play")) {
        // プレビューが無ければ生成、あれば頭から。以後は毎フレーム SyncPreview でライブ反映されるので
        // 編集のたびに押し直す必要はない（Restart 不要）。
        if (previewHandle_ == 0 || !em->GetInstanceByHandle(previewHandle_)) {
            previewHandle_ = em->PlayPreview(editBuffer_, Vector3{ playPos_[0], playPos_[1], playPos_[2] });
        } else {
            em->RewindPreview();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        // t=0 へ巻き戻し（破棄せず同じインスタンスを再生し直す）
        em->RewindPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { em->StopAllPreview(); previewHandle_ = 0; previewPaused_ = false; }
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &previewPaused_);
    ImGui::SameLine();
    {
        bool loopPrev = em->GetPreviewLoop();
        if (ImGui::Checkbox("Loop preview", &loopPrev)) em->SetPreviewLoop(loopPrev);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ON: 寿命を終えても自動的に頭から再生し続ける（編集中の常時プレビュー）。\n"
                              "OFF: 1回再生して終了。");
        }
    }

    // 再生速度
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("Speed", &previewSpeed_, 0.01f, 0.0f, 4.0f, "%.2fx");
    if (previewSpeed_ < 0.0f) previewSpeed_ = 0.0f;

    // 歪みプレビュー（自前 PostEffect を使うので Game の設定とは独立）
    ImGui::Checkbox("Distortion", &previewDistortion_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("ON: distortion コンポーネントの歪みをプレビューに合成する。\n"
                          "エディタ自前の PostEffect を使うため、シーンの PostEffect 設定には影響されない。");
    }
    if (previewDistortion_ && postEffect_ && postEffect_->distortion) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        float strength = postEffect_->distortion->GetStrength();
        if (ImGui::DragFloat("Strength", &strength, 0.005f, 0.0f, 2.0f, "%.3f")) {
            postEffect_->distortion->SetStrength(strength);
        }
    }

    // Timeline：シーク可能なスライダー。ドラッグ中は SeekPreview で t=0 から早送り再シミュレート。
    // 非ドラッグ時はスライダー値を現在の経過時間に追従させ、再生位置を表示する。
    // シーン内の他エフェクトのタイムラインを拾わないよう、ハンドル指定で取得する。
    {
        EffectInstance* inst = (previewHandle_ != 0) ? em->GetInstanceByHandle(previewHandle_) : nullptr;
        if (!inst) previewHandle_ = 0; // 再生終了で無効化
        float elapsed = inst ? inst->GetElapsedTime() : 0.0f;
        float total   = inst ? inst->GetTotalDuration() : editBuffer_.totalDuration;
        if (total < 0.0001f) total = 0.0001f;

        float seekT = std::clamp(elapsed, 0.0f, total);
        char fmt[32];
        std::snprintf(fmt, sizeof(fmt), "%.2f / %.2f s", seekT, total);
        if (ImGui::SliderFloat("Seek", &seekT, 0.0f, total, fmt)) {
            if (inst) em->SeekPreview(std::clamp(seekT, 0.0f, total));
        }
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

    // distortion 合成済みなら出力 RT を、そうでなければ通常の RT を表示
    RenderTexture* showRT = (lastFrameDistortionApplied_ && previewOutputRT_)
        ? previewOutputRT_.get()
        : renderTexture_.get();
    uint32_t srvIndex = showRT->GetSRVIndex();
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
            AddComponent(static_cast<EffectComponentEditable::Kind>(p->kind), p->meshType);
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
    // 編集元と同じ名前なら上書き保存。そうでなければ衝突時にサフィックス。
    const bool allowOverwrite = (editBuffer_.name == selectedEffect_);
    std::string finalName = EffectManager::GetInstance()->SaveDef(editBuffer_, allowOverwrite);
    if (!finalName.empty()) {
        editBuffer_.name = finalName;
        std::snprintf(editNameInput_, sizeof(editNameInput_), "%s", finalName.c_str());
        selectedEffect_ = finalName;
        editBufferDirty_ = false;
    }
}

void EffectEditorWindow::AddComponent(EffectComponentEditable::Kind kind, int meshType) {
    switch (kind) {
    case EffectComponentEditable::Kind::Primitive: {
        EffectPrimitiveComponent c{};
        c.meshType = meshType; // 置いた瞬間に形状確定
        editBuffer_.primitives.push_back(c);
        break;
    }
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
    // 同フレームで重複してクリックされた場合の二重登録を弾く
    for (const auto& r : pendingRemovals_) {
        if (r.kind == kind && r.index == index) return;
    }
    pendingRemovals_.push_back({ kind, index });
}

void EffectEditorWindow::DuplicateComponent(EffectComponentEditable::Kind kind, int index) {
    for (const auto& r : pendingDuplications_) {
        if (r.kind == kind && r.index == index) return;
    }
    pendingDuplications_.push_back({ kind, index });
}

void EffectEditorWindow::RebuildEditables() {
    // 選択中の Editable があれば、その kind+index を覚えておいて再構築後に対応する Editable を再選択
    EffectComponentEditable::Kind selKind = EffectComponentEditable::Kind::Primitive;
    int selIndex = -1;
    if (selection_) {
        if (auto* sel = dynamic_cast<EffectComponentEditable*>(selection_->GetSelected())) {
            selKind = sel->GetKind();
            selIndex = sel->GetIndex();
        }
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
    if (selection_) {
        selection_->SetSelected(nullptr);
        if (selIndex >= 0) {
            for (auto& e : editables_) {
                if (e->GetKind() == selKind && e->GetIndex() == selIndex) {
                    selection_->SetSelected(e.get());
                    break;
                }
            }
        }
    }
}
