#include "LightManager.h"
#include "imgui.h"

//LightManager* LightManager::instance_ = nullptr;

LightManager* LightManager::GetInstance() {
    //if (instance_ == nullptr) {
        //instance_ = new LightManager();
    //}
    //return instance_;
    static LightManager instance;
    return &instance;
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

    // PointLightGroup用バッファ作成
    UINT pointLightSize = (sizeof(PointLightGroup) + 255) & ~255;
    pointLightResource_ = dxCore->CreateBufferResource(pointLightSize);
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightGroupData_));

    // PointLightGroup初期値
    pointLightGroupData_->activeCount = 1;
    for (uint32_t i = 0; i < kMaxPointLights; ++i) {
        pointLightGroupData_->lights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        pointLightGroupData_->lights[i].position = { 0.0f, 2.0f, static_cast<float>(i) * 3.0f };
        pointLightGroupData_->lights[i].intensity = 0.0f;
        pointLightGroupData_->lights[i].radius = 5.0f;
        pointLightGroupData_->lights[i].decay = 1.0f;
    }

    // SpotLightGroup用バッファ作成
    UINT spotLightSize = (sizeof(SpotLightGroup) + 255) & ~255;
    spotLightResource_ = dxCore->CreateBufferResource(spotLightSize);
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightGroupData_));

    // SpotLightGroup初期値
    spotLightGroupData_->activeCount = 1;
    for (uint32_t i = 0; i < kMaxSpotLights; ++i) {
        spotLightGroupData_->lights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        spotLightGroupData_->lights[i].position = { 2.0f + static_cast<float>(i) * 2.0f, 1.25f, 0.0f };
        spotLightGroupData_->lights[i].distance = 7.0f;
        spotLightGroupData_->lights[i].direction = Normalize({ -1.0f, -1.0f, 0.0f });
        spotLightGroupData_->lights[i].intensity = 0.0f;
        spotLightGroupData_->lights[i].decay = 2.0f;
        spotLightGroupData_->lights[i].cosAngle = std::cos(std::numbers::pi_v<float> / 3.0f);
        spotLightGroupData_->lights[i].cosFalloffStart = std::cos(std::numbers::pi_v<float> / 4.0f);
    }
}

void LightManager::Finalize() {
    directionalLightResource_.Reset();
    pointLightResource_.Reset();
    spotLightResource_.Reset();

    //delete instance_;
    //instance_ = nullptr;
}

void LightManager::BindLights(ID3D12GraphicsCommandList* commandList) {
    // rootParameter[3] = DirectionalLight (b1)
    commandList->SetGraphicsRootConstantBufferView(
        3, directionalLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[5] = PointLightGroup (b3)
    commandList->SetGraphicsRootConstantBufferView(
        5, pointLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[6] = SpotLightGroup (b4)
    commandList->SetGraphicsRootConstantBufferView(
        6, spotLightResource_->GetGPUVirtualAddress()
    );
}

// ===== PointLight設定関数 =====
void LightManager::SetPointLightCount(uint32_t count) {
    pointLightGroupData_->activeCount = (count > kMaxPointLights) ? kMaxPointLights : count;
}

void LightManager::SetPointLightColor(uint32_t index, const Vector4& color) {
    if (index < kMaxPointLights) {
        pointLightGroupData_->lights[index].color = color;
    }
}

void LightManager::SetPointLightPosition(uint32_t index, const Vector3& position) {
    if (index < kMaxPointLights) {
        pointLightGroupData_->lights[index].position = position;
    }
}

void LightManager::SetPointLightIntensity(uint32_t index, float intensity) {
    if (index < kMaxPointLights) {
        pointLightGroupData_->lights[index].intensity = intensity;
    }
}

void LightManager::SetPointLightRadius(uint32_t index, float radius) {
    if (index < kMaxPointLights) {
        pointLightGroupData_->lights[index].radius = radius;
    }
}

void LightManager::SetPointLightDecay(uint32_t index, float decay) {
    if (index < kMaxPointLights) {
        pointLightGroupData_->lights[index].decay = decay;
    }
}

// ===== SpotLight設定関数 =====
void LightManager::SetSpotLightCount(uint32_t count) {
    spotLightGroupData_->activeCount = (count > kMaxSpotLights) ? kMaxSpotLights : count;
}

void LightManager::SetSpotLightColor(uint32_t index, const Vector4& color) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].color = color;
    }
}

void LightManager::SetSpotLightPosition(uint32_t index, const Vector3& position) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].position = position;
    }
}

void LightManager::SetSpotLightDirection(uint32_t index, const Vector3& direction) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].direction = direction;
    }
}

void LightManager::SetSpotLightIntensity(uint32_t index, float intensity) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].intensity = intensity;
    }
}

void LightManager::SetSpotLightDistance(uint32_t index, float distance) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].distance = distance;
    }
}

void LightManager::SetSpotLightDecay(uint32_t index, float decay) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].decay = decay;
    }
}

void LightManager::SetSpotLightCosAngle(uint32_t index, float cosAngle) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].cosAngle = cosAngle;
    }
}

void LightManager::SetSpotLightCosFalloffStart(uint32_t index, float cosFalloffStart) {
    if (index < kMaxSpotLights) {
        spotLightGroupData_->lights[index].cosFalloffStart = cosFalloffStart;
    }
}

// ===== ゲッター =====
PointLight* LightManager::GetPointLight(uint32_t index) {
    if (index < kMaxPointLights) {
        return &pointLightGroupData_->lights[index];
    }
    return nullptr;
}

SpotLight* LightManager::GetSpotLight(uint32_t index) {
    if (index < kMaxSpotLights) {
        return &spotLightGroupData_->lights[index];
    }
    return nullptr;
}

void LightManager::OnImGui() {
#ifdef _DEBUG

    if (ImGui::Begin("Light Settings")) {

        // ===== DirectionalLight =====
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("DL Color", &directionalLightData_->color.x);
            ImGui::DragFloat3("DL Direction", &directionalLightData_->direction.x, 0.01f);
            ImGui::DragFloat("DL Intensity", &directionalLightData_->intensity, 0.01f, 0.0f, 10.0f);

            const char* lightingTypes[] = { "None", "Lambert", "Half-Lambert" };
            ImGui::Combo("DL Type", &directionalLightData_->lightingType, lightingTypes, 3);
        }

        // ===== PointLight (複数対応) =====
        if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            // ライト数の調整
            int pointLightCount = static_cast<int>(pointLightGroupData_->activeCount);
            if (ImGui::SliderInt("Point Light Count", &pointLightCount, 0, kMaxPointLights)) {
                pointLightGroupData_->activeCount = static_cast<uint32_t>(pointLightCount);
            }

            // 各ライトの設定
            for (uint32_t i = 0; i < pointLightGroupData_->activeCount; ++i) {
                ImGui::PushID(static_cast<int>(i));

                char label[32];
                sprintf_s(label, "Point Light [%d]", i);

                if (ImGui::TreeNode(label)) {
                    PointLight& pl = pointLightGroupData_->lights[i];
                    ImGui::ColorEdit4("Color", &pl.color.x);
                    ImGui::DragFloat3("Position", &pl.position.x, 0.1f);
                    ImGui::DragFloat("Intensity", &pl.intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("Radius", &pl.radius, 0.1f, 0.1f, 50.0f);
                    ImGui::DragFloat("Decay", &pl.decay, 0.01f, 0.1f, 10.0f);
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }

        // ===== SpotLight (複数対応) =====
        if (ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            // ライト数の調整
            int spotLightCount = static_cast<int>(spotLightGroupData_->activeCount);
            if (ImGui::SliderInt("Spot Light Count", &spotLightCount, 0, kMaxSpotLights)) {
                spotLightGroupData_->activeCount = static_cast<uint32_t>(spotLightCount);
            }

            // 各ライトの設定
            for (uint32_t i = 0; i < spotLightGroupData_->activeCount; ++i) {
                ImGui::PushID(1000 + static_cast<int>(i)); // PointLightとIDが被らないようにオフセット

                char label[32];
                sprintf_s(label, "Spot Light [%d]", i);

                if (ImGui::TreeNode(label)) {
                    SpotLight& sl = spotLightGroupData_->lights[i];
                    ImGui::ColorEdit4("Color", &sl.color.x);
                    ImGui::DragFloat3("Position", &sl.position.x, 0.1f);
                    ImGui::DragFloat3("Direction", &sl.direction.x, 0.01f);
                    ImGui::DragFloat("Intensity", &sl.intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("Distance", &sl.distance, 0.1f, 0.1f, 50.0f);
                    ImGui::DragFloat("Decay", &sl.decay, 0.01f, 0.1f, 10.0f);

                    // 角度をdegreeで表示・編集
                    float angleDeg = std::acos(sl.cosAngle) * 180.0f / std::numbers::pi_v<float>;
                    if (ImGui::DragFloat("Angle", &angleDeg, 0.5f, 1.0f, 90.0f)) {
                        sl.cosAngle = std::cos(angleDeg * std::numbers::pi_v<float> / 180.0f);
                    }

                    float falloffStartDeg = std::acos(sl.cosFalloffStart) * 180.0f / std::numbers::pi_v<float>;
                    if (ImGui::DragFloat("Falloff Start", &falloffStartDeg, 0.5f, 0.0f, angleDeg)) {
                        sl.cosFalloffStart = std::cos(falloffStartDeg * std::numbers::pi_v<float> / 180.0f);
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
    }
    ImGui::End();

#endif // DEBUG
}