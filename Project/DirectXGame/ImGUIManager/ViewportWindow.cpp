#include "ViewportWindow.h"
#include "RenderTexture.h"
#include "SRVManager.h"
#include "imgui.h"

#include "SceneEditorWindow.h"  // ModelDropPayload / SpriteDropPayload / AnimatedDropPayload
#include "SceneManager.h"
#include "BaseScene.h"
#include "WindowsApplication.h"  // kClientWidth / kClientHeight

void ViewportWindow::OnDraw() {
    if (!renderTexture_ || !srvManager_) {
        ImGui::Text("RenderTexture not set");
        return;
    }

    // --- 再生/停止コントロール（現在シーンの sceneTimeScale を 0/1 に切り替え） ---
    {
        BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
        const bool isPaused = scene && scene->GetSceneTimeScale() == 0.0f;
        if (ImGui::Button(isPaused ? "Play" : "Pause")) {
            if (scene) scene->SetSceneTimeScale(isPaused ? 1.0f : 0.0f);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(isPaused ? "[ Paused ]" : "[ Playing ]");
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

    // SceneEditorWindow からのドラッグを受け取り、現在のシーンに動的エンティティを追加
    if (ImGui::BeginDragDropTarget()) {
        BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();

        // 静的モデル（.obj）
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(MODEL_DROP_PAYLOAD_TYPE)) {
            const ModelDropPayload* p = static_cast<const ModelDropPayload*>(payload->Data);
            if (scene) scene->AddDynamicObject(p->dirPath, p->filename);
        }
        // スプライト（.png）— ドロップ位置をクライアント座標に変換
        else if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
            const SpriteDropPayload* p = static_cast<const SpriteDropPayload*>(payload->Data);
            if (scene && imageScreenSize_.x > 0 && imageScreenSize_.y > 0) {
                ImVec2 mouse = ImGui::GetMousePos();
                float relX = (mouse.x - imageScreenPos_.x) / imageScreenSize_.x;
                float relY = (mouse.y - imageScreenPos_.y) / imageScreenSize_.y;
                float clientX = relX * static_cast<float>(WindowsApplication::kClientWidth);
                float clientY = relY * static_cast<float>(WindowsApplication::kClientHeight);
                scene->AddDynamicSprite(p->texturePath, clientX, clientY);
            }
        }
        // アニメーションモデル（.gltf / .glb / .fbx）
        else if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(ANIMATED_DROP_PAYLOAD_TYPE)) {
            const AnimatedDropPayload* p = static_cast<const AnimatedDropPayload*>(payload->Data);
            if (scene) scene->AddDynamicAnimated(p->dirPath, p->filename);
        }

        ImGui::EndDragDropTarget();
    }
}
