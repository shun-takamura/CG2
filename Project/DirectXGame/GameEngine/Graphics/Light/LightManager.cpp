#include "LightManager.h"
#include "imgui.h"

LightManager* LightManager::instance_ = nullptr;

LightManager* LightManager::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new LightManager();
    }
    return instance_;
}

void LightManager::Initialize(DirectXCore* dxCore) {
    dxCore_ = dxCore;

    // DirectionalLight用バッファ作成
    UINT directionalLightSize = (sizeof(DirectionalLight) + 255) & ~255;
    directionalLightResource_ = dxCore->CreateBufferResource(directionalLightSize);
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

    // DirectionalLight初期値
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 0.0f;
    directionalLightData_->lightingType = 0;

    // PointLight用バッファ作成
    UINT pointLightSize = (sizeof(PointLight) + 255) & ~255;
    pointLightResource_ = dxCore->CreateBufferResource(pointLightSize);
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData_));

    // PointLight初期値
    pointLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    pointLightData_->position = { 0.0f, 2.0f, 0.0f };
    pointLightData_->intensity = 1.0f;
    pointLightData_->radius = 5.0f;
    pointLightData_->decay = 1.0f;

    // SpotLight用バッファ作成 ← 追加
    UINT spotLightSize = (sizeof(SpotLight) + 255) & ~255;
    spotLightResource_ = dxCore->CreateBufferResource(spotLightSize);
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightData_));

    // SpotLight初期値（スライド3の値）
    spotLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    spotLightData_->position = { 2.0f, 1.25f, 0.0f };
    spotLightData_->distance = 7.0f;
    spotLightData_->direction = Normalize({ -1.0f, -1.0f, 0.0f });
    spotLightData_->intensity = 4.0f;
    spotLightData_->decay = 2.0f;
    spotLightData_->cosAngle = std::cos(std::numbers::pi_v<float> / 3.0f);
    spotLightData_->cosFalloffStart = std::cos(std::numbers::pi_v<float> / 4.0f); // 45度

}

void LightManager::Finalize() {
    directionalLightResource_.Reset();
    pointLightResource_.Reset();
    spotLightResource_.Reset();

    delete instance_;
    instance_ = nullptr;
}

void LightManager::BindLights(ID3D12GraphicsCommandList* commandList) {
    // rootParameter[3] = DirectionalLight (b1)
    commandList->SetGraphicsRootConstantBufferView(
        3, directionalLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[5] = PointLight (b3)
    commandList->SetGraphicsRootConstantBufferView(
        5, pointLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[6] = SpotLight (b4)
    commandList->SetGraphicsRootConstantBufferView(
        6, spotLightResource_->GetGPUVirtualAddress()
    );
}

void LightManager::OnImGui() {
    if (ImGui::Begin("Light Settings")) {
        
        // DirectionalLight
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("DL Color", &directionalLightData_->color.x);
            ImGui::DragFloat3("DL Direction", &directionalLightData_->direction.x, 0.01f);
            ImGui::DragFloat("DL Intensity", &directionalLightData_->intensity, 0.01f, 0.0f, 10.0f);
            
            // ライティングタイプ選択
            const char* lightingTypes[] = { "None", "Lambert", "Half-Lambert" };
            ImGui::Combo("DL Type", &directionalLightData_->lightingType, lightingTypes, 3);
        }

        // PointLight
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("PL Color", &pointLightData_->color.x);
            ImGui::DragFloat3("PL Position", &pointLightData_->position.x, 0.1f);
            ImGui::DragFloat("PL Intensity", &pointLightData_->intensity, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("PL Radius", &pointLightData_->radius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("PL Decay", &pointLightData_->decay, 0.01f, 0.0f, 10.0f);
        }

        // SpotLight
        if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("SL Color", &spotLightData_->color.x);
            ImGui::DragFloat3("SL Position", &spotLightData_->position.x, 0.1f);
            ImGui::DragFloat3("SL Direction", &spotLightData_->direction.x, 0.01f);
            ImGui::DragFloat("SL Intensity", &spotLightData_->intensity, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("SL Distance", &spotLightData_->distance, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("SL Decay", &spotLightData_->decay, 0.01f, 0.1f, 10.0f);

            // 角度をdegreeで表示・編集
            float angleDeg = std::acos(spotLightData_->cosAngle) * 180.0f / std::numbers::pi_v<float>;
            if (ImGui::DragFloat("SL Angle", &angleDeg, 0.5f, 1.0f, 90.0f)) {
                spotLightData_->cosAngle = std::cos(angleDeg * std::numbers::pi_v<float> / 180.0f);
            }

            float falloffStartDeg = std::acos(spotLightData_->cosFalloffStart) * 180.0f / std::numbers::pi_v<float>;
            if (ImGui::DragFloat("SL Falloff Start", &falloffStartDeg, 0.5f, 0.0f, angleDeg)) {
                spotLightData_->cosFalloffStart = std::cos(falloffStartDeg * std::numbers::pi_v<float> / 180.0f);
            }
        }
    }
    ImGui::End();
}
