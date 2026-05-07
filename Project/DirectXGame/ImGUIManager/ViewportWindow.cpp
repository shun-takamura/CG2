#include "ViewportWindow.h"
#include "RenderTexture.h"
#include "SRVManager.h"
#include "imgui.h"

void ViewportWindow::OnDraw() {
    if (!renderTexture_ || !srvManager_) {
        ImGui::Text("RenderTexture not set");
        return;
    }

    // ウィンドウサイズを取得（ImGuiウィンドウ内の利用可能領域）
    ImVec2 windowSize = ImGui::GetContentRegionAvail();

    // RenderTextureのアスペクト比を維持
    float texW = (float)renderTexture_->GetWidth();
    float texH = (float)renderTexture_->GetHeight();
    float texAspect = texW / texH;
    float winAspect = windowSize.x / windowSize.y;

    ImVec2 imageSize;
    if (winAspect > texAspect) {
        // ウィンドウが横長 → 縦に合わせる
        imageSize.y = windowSize.y;
        imageSize.x = windowSize.y * texAspect;
    } else {
        // ウィンドウが縦長 → 横に合わせる
        imageSize.x = windowSize.x;
        imageSize.y = windowSize.x / texAspect;
    }

    // RenderTextureのSRVインデックスからGPUハンドルを取得
    uint32_t srvIndex = renderTexture_->GetSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvManager_->GetGPUDescriptorHandle(srvIndex);

    // 表示開始位置を保存（Gizmo描画で使用）
    imageScreenPos_ = ImGui::GetCursorScreenPos();
    imageScreenSize_ = imageSize;

    // ImGui::Imageで表示（ウィンドウサイズに合わせる）
    ImGui::Image(
        (ImTextureID)gpuHandle.ptr,
        imageSize,
        ImVec2(0, 0),
        ImVec2(1, 1)
    );

    // ホバー状態を更新
    isHovered_ = ImGui::IsItemHovered();
}
