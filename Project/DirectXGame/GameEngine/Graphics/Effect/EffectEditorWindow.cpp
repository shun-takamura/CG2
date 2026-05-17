#include "EffectEditorWindow.h"
#include "RenderTexture.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "LineRenderer.h"
#include "EffectManager.h"
#include "Vector3.h"
#include "Vector4.h"
#include "imgui.h"
#include <algorithm>

namespace {
    // RT のサイズはあまりに細かい変化で再生成しないようキリのいい値に丸める
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
    // 少し引いた位置から見下ろす（DemoScene のメインカメラに近い構図）
    debugCamera_.SetPivot({ 0.0f, 0.0f, 0.0f });
    debugCamera_.SetDistance(10.0f);
    debugCamera_.Orbit(0.0f, 0.5f); // ピッチ 0.5rad（約28°）見下ろし

    // 初期 RT
    EnsureRenderTextureSize(512, 512);
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

    // 既に RT が存在する場合：このまま破棄するとコマンドリスト確定前にリソースが死ぬので
    // 古い方を1フレーム保持する側に退避してから新規作成する。
    if (renderTexture_) {
        // さらに前のフレームから持ち越した分は念のためここで解放
        // （実際は Render() 冒頭でクリアされるが二重保険）
        if (oldRenderTexture_) {
            oldRenderTexture_->Finalize();
            oldRenderTexture_.reset();
        }
        oldRenderTexture_ = std::move(renderTexture_);
    }

    renderTexture_ = std::make_unique<RenderTexture>();
    const float clear[4] = { 0.15f, 0.15f, 0.18f, 1.0f }; // ダークグレー
    renderTexture_->Initialize(dxCore_, srvManager_, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
    rtWidth_ = width;
    rtHeight_ = height;

    // カメラのアスペクトを更新
    debugCamera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

void EffectEditorWindow::HandleMouseInput() {
    if (!isHovered_) return;

    ImGuiIO& io = ImGui::GetIO();
    const float orbitSpeed = 0.005f;
    const float panSpeed   = 0.005f;
    const float zoomSpeed  = 0.5f;

    // 左ドラッグ: Orbit
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 d = io.MouseDelta;
        debugCamera_.Orbit(d.x * orbitSpeed, d.y * orbitSpeed);
    }
    // 中ドラッグ: Pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        ImVec2 d = io.MouseDelta;
        // distance 依存でスケール
        float scale = panSpeed; // 簡易：距離に応じてスケールしてもよい
        debugCamera_.Pan(-d.x * scale, d.y * scale);
    }
    // ホイール: Zoom
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
    // 1フレーム前の OnDraw で要求されたリサイズをここで安全に処理する
    if (pendingWidth_ > 0 && pendingHeight_ > 0 &&
        (pendingWidth_ != rtWidth_ || pendingHeight_ != rtHeight_)) {
        EnsureRenderTextureSize(pendingWidth_, pendingHeight_);
    }
    // 前フレームの古い RT は GPU が使い終わっているはずなので解放
    if (oldRenderTexture_) {
        oldRenderTexture_->Finalize();
        oldRenderTexture_.reset();
    }

    if (!renderTexture_ || !isOpen_) return;

    // DebugCamera を更新して Camera に注入
    debugCamera_.Update();
    camera_.SetExternalMatrices(
        debugCamera_.GetViewMatrix(),
        debugCamera_.GetProjectionMatrix(),
        debugCamera_.GetTranslate());

    auto* commandList = dxCore_->GetCommandList();

    // プレビュー RT へ描画開始
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
    renderTexture_->BeginRender(commandList, &dsv);

    // ビューポート/シザーを RT サイズに合わせて設定
    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(rtWidth_);
    vp.Height = static_cast<float>(rtHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &vp);
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(rtWidth_), static_cast<LONG>(rtHeight_) };
    commandList->RSSetScissorRects(1, &sc);

    // 深度クリア（プレビューRT専用なので一旦クリアする）
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // グリッド線（Preview パスを使い、Scene 用ラインと干渉させない）
    LineRenderer* lr = LineRenderer::GetInstance();
    if (lr) {
        lr->SetCamera(&camera_, LineRenderer::Pass::Preview);
        AddGridLines();
        lr->Draw(LineRenderer::Pass::Preview);
    }

    // EffectManager にプレビューカメラを渡してプレビュー描画
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

    // 上部：再生制御
    EffectManager* em = EffectManager::GetInstance();
    ImGui::Text("Active: %zu  /  Defs: %zu", em->GetActiveInstanceCount(), em->ListDefNames().size());

    auto defs = em->ListDefNames();
    if (selectedEffect_.empty() && !defs.empty()) selectedEffect_ = defs.front();

    if (ImGui::BeginCombo("Effect", selectedEffect_.c_str())) {
        for (const auto& name : defs) {
            bool sel = (name == selectedEffect_);
            if (ImGui::Selectable(name.c_str(), sel)) {
                selectedEffect_ = name;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::DragFloat3("Position", playPos_, 0.1f);

    if (ImGui::Button("Play")) {
        if (!selectedEffect_.empty()) {
            em->Play(selectedEffect_, Vector3{ playPos_[0], playPos_[1], playPos_[2] });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop All")) {
        em->StopAll();
    }

    ImGui::Separator();

    // 画像領域：ImGuiウィンドウの残り領域いっぱい
    ImVec2 avail = ImGui::GetContentRegionAvail();
    uint32_t newW = SnapRtSize(avail.x);
    uint32_t newH = SnapRtSize(avail.y);
    // 即時再生成すると同フレームで recorded コマンドが死亡参照になるので
    // 「望ましいサイズ」だけ保存して、次フレームの Render() で反映する。
    if (newW > 0 && newH > 0) {
        pendingWidth_ = newW;
        pendingHeight_ = newH;
    }

    uint32_t srvIndex = renderTexture_->GetSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvManager_->GetGPUDescriptorHandle(srvIndex);

    // アスペクト維持で表示
    float texW = (float)renderTexture_->GetWidth();
    float texH = (float)renderTexture_->GetHeight();
    float texAspect = texW / texH;
    float winAspect = avail.x / avail.y;
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

    // マウス入力 → カメラ
    HandleMouseInput();

    ImGui::TextDisabled("L-Drag: Orbit  /  M-Drag: Pan  /  Wheel: Zoom");
#endif
}
