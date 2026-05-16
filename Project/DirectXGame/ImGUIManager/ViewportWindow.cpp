#include "ViewportWindow.h"
#include "RenderTexture.h"
#include "SRVManager.h"
#include "imgui.h"

#include "SceneEditorWindow.h"  // ModelDropPayload / SpriteDropPayload / AnimatedDropPayload
#include "SceneManager.h"
#include "BaseScene.h"
#include "WindowsApplication.h"  // kClientWidth / kClientHeight
#include "Camera.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "MathUtility.h"

#include <algorithm>
#include <cmath>

namespace {
    // ドロップ配置時のグリッドスナップ刻み（0.5）
    constexpr float kPlacementSnapStep = 0.5f;

    inline float SnapTo(float v, float step) {
        if (step <= 0.0f) return v;
        return std::round(v / step) * step;
    }

    /// <summary>
    /// マウス相対座標 (0..1) からカメラを通って Y=0 平面に当たる点を算出。
    /// 失敗時（カメラがほぼ水平で平面に当たらない等）は false を返す。
    /// </summary>
    bool ScreenToY0Plane(const Camera* camera, float relX, float relY, Vector3& outHit) {
        if (!camera) return false;

        // NDC: x,y ∈ [-1, 1], 上方向は +Y。relY は上が0なので反転
        float ndcX = relX * 2.0f - 1.0f;
        float ndcY = 1.0f - relY * 2.0f;

        // ViewProjection の逆行列
        Matrix4x4 invVP = Inverse(camera->GetViewProjectionMatrix());

        // ニア (z=0) とファー (z=1) の点をワールドに復元
        Vector3 nearPoint = TransformCoordinate({ ndcX, ndcY, 0.0f }, invVP);
        Vector3 farPoint  = TransformCoordinate({ ndcX, ndcY, 1.0f }, invVP);

        // レイの方向
        Vector3 dir{
            farPoint.x - nearPoint.x,
            farPoint.y - nearPoint.y,
            farPoint.z - nearPoint.z };

        // Y=0 平面と交差。dir.y が極小ならカメラがほぼ水平で交差しない
        if (std::fabs(dir.y) < 1e-5f) return false;

        float t = -nearPoint.y / dir.y;
        if (t < 0.0f) return false; // 後方なら無効

        outHit.x = nearPoint.x + dir.x * t;
        outHit.y = 0.0f;
        outHit.z = nearPoint.z + dir.z * t;

        // 0.5 刻みにスナップ
        outHit.x = SnapTo(outHit.x, kPlacementSnapStep);
        outHit.z = SnapTo(outHit.z, kPlacementSnapStep);
        return true;
    }
}

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
        imageSize.y = windowSize.y;
        imageSize.x = windowSize.y * texAspect;
    } else {
        imageSize.x = windowSize.x;
        imageSize.y = windowSize.x / texAspect;
    }

    uint32_t srvIndex = renderTexture_->GetSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvManager_->GetGPUDescriptorHandle(srvIndex);

    imageScreenPos_ = ImGui::GetCursorScreenPos();
    imageScreenSize_ = imageSize;

    ImGui::Image(
        (ImTextureID)gpuHandle.ptr,
        imageSize,
        ImVec2(0, 0),
        ImVec2(1, 1)
    );

    isHovered_ = ImGui::IsItemHovered();

    // ----- SceneEditor からのドロップ受付 -----
    if (ImGui::BeginDragDropTarget()) {
        BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();

        // マウス位置 → ビューポート画像相対座標 (0..1) → ワールド座標 (Y=0平面)
        Vector3 worldPos{ 0.0f, 0.0f, 0.0f };
        bool hasWorldPos = false;
        if (scene && imageScreenSize_.x > 0 && imageScreenSize_.y > 0) {
            ImVec2 mouse = ImGui::GetMousePos();
            float relX = (mouse.x - imageScreenPos_.x) / imageScreenSize_.x;
            float relY = (mouse.y - imageScreenPos_.y) / imageScreenSize_.y;
            relX = std::clamp(relX, 0.0f, 1.0f);
            relY = std::clamp(relY, 0.0f, 1.0f);
            hasWorldPos = ScreenToY0Plane(scene->GetCamera(), relX, relY, worldPos);
        }

        // 静的モデル（.obj） → Y=0 にスナップ配置
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(MODEL_DROP_PAYLOAD_TYPE)) {
            const ModelDropPayload* p = static_cast<const ModelDropPayload*>(payload->Data);
            if (scene) scene->AddDynamicObject(p->dirPath, p->filename,
                hasWorldPos ? worldPos : Vector3{});
        }
        // スプライト（.png） — 2D なのでスクリーン座標（クライアント座標）で配置
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
        // アニメーションモデル → Y=0 にスナップ配置
        else if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(ANIMATED_DROP_PAYLOAD_TYPE)) {
            const AnimatedDropPayload* p = static_cast<const AnimatedDropPayload*>(payload->Data);
            if (scene) scene->AddDynamicAnimated(p->dirPath, p->filename,
                hasWorldPos ? worldPos : Vector3{});
        }
        // プリミティブ → Y=0 にスナップ配置
        else if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(PRIMITIVE_DROP_PAYLOAD_TYPE)) {
            const PrimitiveDropPayload* p = static_cast<const PrimitiveDropPayload*>(payload->Data);
            if (scene) scene->AddDynamicPrimitive(p->primitiveType,
                hasWorldPos ? worldPos : Vector3{});
        }

        ImGui::EndDragDropTarget();
    }
}
