#include "LightManager.h"
#include "Object3DManager.h"
#include "imgui.h"
namespace {
    // 定数バッファの大きさを 256 バイト境界へ切り上げるためのマスク（256 - 1）
    constexpr int kConstantBufferAlignMask = 255;
    // 点光源の初期配置（高さ [m] と、番号ごとに Z 方向へずらす間隔 [m]）と影響半径 [m]
    constexpr float kDefaultPointLightHeight = 2.0f;
    constexpr float kDefaultPointLightSpacingZ = 3.0f;
    constexpr float kDefaultPointLightRadius = 5.0f;
    // スポットライトの初期配置（X の始まり [m]・番号ごとに X 方向へずらす間隔 [m]・高さ [m]）
    constexpr float kDefaultSpotLightStartX = 2.0f;
    constexpr float kDefaultSpotLightSpacingX = 2.0f;
    constexpr float kDefaultSpotLightHeight = 1.25f;
    // スポットライトの届く距離 [m] と減衰の指数
    constexpr float kDefaultSpotLightDistance = 7.0f;
    constexpr float kDefaultSpotLightDecay = 2.0f;
    // スポットライトの開き角 = π ÷ この値（60°）、減衰が始まる角 = π ÷ この値（45°）
    constexpr float kDefaultSpotAngleDivisor = 3.0f;
    constexpr float kDefaultSpotFalloffStartDivisor = 4.0f;
    // 平行光源のライティングの種類の数（None / Lambert / Half-Lambert）
    constexpr int kLightingTypeCount = 3;
    // ImGui のライトの見出しの文字バッファの大きさ [byte]
    constexpr int kLightLabelBufferSize = 32;
    // スポットライトの ImGui の ID を点光源とぶつけないためのずらし
    constexpr int kSpotLightImGuiIdOffset = 1000;
    // 半回転の角度 [度]（ラジアン ↔ 度の変換に使う）
    constexpr float kHalfTurnDegrees = 180.0f;
}

LightManager* LightManager::GetInstance() {
    static LightManager instance;
    return &instance;
}

void LightManager::Initialize(DirectXCore* dxCore) {
    dxCore_ = dxCore;

    // DirectionalLight用バッファ作成
    UINT directionalLightSize = (sizeof(DirectionalLight) + kConstantBufferAlignMask) & ~kConstantBufferAlignMask;
    directionalLightResource_ = dxCore->CreateBufferResource(directionalLightSize);
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

    // DirectionalLight初期値
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 0.0f;
    directionalLightData_->lightingType = 0;

    // PointLightGroup用バッファ作成
    UINT pointLightSize = (sizeof(PointLightGroup) + kConstantBufferAlignMask) & ~kConstantBufferAlignMask;
    pointLightResource_ = dxCore->CreateBufferResource(pointLightSize);
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightGroupData_));

    // PointLightGroup初期値
    pointLightGroupData_->activeCount = 1;
    for (uint32_t i = 0; i < kMaxPointLights; ++i) {
        pointLightGroupData_->lights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        pointLightGroupData_->lights[i].position = { 0.0f, kDefaultPointLightHeight, static_cast<float>(i) * kDefaultPointLightSpacingZ };
        pointLightGroupData_->lights[i].intensity = 0.0f;
        pointLightGroupData_->lights[i].radius = kDefaultPointLightRadius;
        pointLightGroupData_->lights[i].decay = 1.0f;
    }

    // SpotLightGroup用バッファ作成
    UINT spotLightSize = (sizeof(SpotLightGroup) + kConstantBufferAlignMask) & ~kConstantBufferAlignMask;
    spotLightResource_ = dxCore->CreateBufferResource(spotLightSize);
    spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightGroupData_));

    // SpotLightGroup初期値
    spotLightGroupData_->activeCount = 1;
    for (uint32_t i = 0; i < kMaxSpotLights; ++i) {
        spotLightGroupData_->lights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        spotLightGroupData_->lights[i].position = { kDefaultSpotLightStartX + static_cast<float>(i) * kDefaultSpotLightSpacingX, kDefaultSpotLightHeight, 0.0f };
        spotLightGroupData_->lights[i].distance = kDefaultSpotLightDistance;
        spotLightGroupData_->lights[i].direction = Normalize({ -1.0f, -1.0f, 0.0f });
        spotLightGroupData_->lights[i].intensity = 0.0f;
        spotLightGroupData_->lights[i].decay = kDefaultSpotLightDecay;
        spotLightGroupData_->lights[i].cosAngle = std::cos(std::numbers::pi_v<float> / kDefaultSpotAngleDivisor);
        spotLightGroupData_->lights[i].cosFalloffStart = std::cos(std::numbers::pi_v<float> / kDefaultSpotFalloffStartDivisor);
    }
}

void LightManager::Finalize() {
    directionalLightResource_.Reset();
    pointLightResource_.Reset();
    spotLightResource_.Reset();
}

void LightManager::BindLights(ID3D12GraphicsCommandList* commandList) {
    // rootParameter[3] = DirectionalLight (b1)
    commandList->SetGraphicsRootConstantBufferView(
        Object3DManager::kRootDirectionalLight, directionalLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[5] = PointLightGroup (b3)
    commandList->SetGraphicsRootConstantBufferView(
        Object3DManager::kRootPointLight, pointLightResource_->GetGPUVirtualAddress()
    );

    // rootParameter[6] = SpotLightGroup (b4)
    commandList->SetGraphicsRootConstantBufferView(
        Object3DManager::kRootSpotLight, spotLightResource_->GetGPUVirtualAddress()
    );
}

// ===== 動的予約API =====

uint32_t LightManager::AcquirePointLight() {
    for (uint32_t i = 0; i < kMaxPointLights; ++i) {
        if (!pointReserved_[i]) {
            pointReserved_[i] = true;
            // 初期化：強度0で確保する（Effect 側で改めて設定する想定）
            pointLightGroupData_->lights[i].intensity = 0.0f;
            // activeCount を確保したスロットまで広げる（縮みはしない）
            if (i + 1 > pointLightGroupData_->activeCount) {
                pointLightGroupData_->activeCount = i + 1;
            }
            return i;
        }
    }
    return kInvalidLightSlot;
}

uint32_t LightManager::AcquireSpotLight() {
    for (uint32_t i = 0; i < kMaxSpotLights; ++i) {
        if (!spotReserved_[i]) {
            spotReserved_[i] = true;
            spotLightGroupData_->lights[i].intensity = 0.0f;
            if (i + 1 > spotLightGroupData_->activeCount) {
                spotLightGroupData_->activeCount = i + 1;
            }
            return i;
        }
    }
    return kInvalidLightSlot;
}

void LightManager::ReleasePointLight(uint32_t slot) {
    if (slot >= kMaxPointLights) return;
    pointReserved_[slot] = false;
    pointLightGroupData_->lights[slot].intensity = 0.0f;
    // activeCount は縮めない（シェーダ側は intensity=0 のスロットを処理しても無害）
}

void LightManager::ReleaseSpotLight(uint32_t slot) {
    if (slot >= kMaxSpotLights) return;
    spotReserved_[slot] = false;
    spotLightGroupData_->lights[slot].intensity = 0.0f;
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
#ifdef USE_IMGUI

    {
        // ===== DirectionalLight =====
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("DL Color", &directionalLightData_->color.x);
            ImGui::DragFloat3("DL Direction", &directionalLightData_->direction.x, 0.01f);
            ImGui::DragFloat("DL Intensity", &directionalLightData_->intensity, 0.01f, 0.0f, 10.0f);

            const char* lightingTypes[] = { "None", "Lambert", "Half-Lambert" };
            ImGui::Combo("DL Type", &directionalLightData_->lightingType, lightingTypes, kLightingTypeCount);
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

                char label[kLightLabelBufferSize];
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
                ImGui::PushID(kSpotLightImGuiIdOffset + static_cast<int>(i)); // PointLightとIDが被らないようにオフセット

                char label[kLightLabelBufferSize];
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
                    float angleDeg = std::acos(sl.cosAngle) * kHalfTurnDegrees / std::numbers::pi_v<float>;
                    if (ImGui::DragFloat("Angle", &angleDeg, 0.5f, 1.0f, 90.0f)) {
                        sl.cosAngle = std::cos(angleDeg * std::numbers::pi_v<float> / kHalfTurnDegrees);
                    }

                    float falloffStartDeg = std::acos(sl.cosFalloffStart) * kHalfTurnDegrees / std::numbers::pi_v<float>;
                    if (ImGui::DragFloat("Falloff Start", &falloffStartDeg, 0.5f, 0.0f, angleDeg)) {
                        sl.cosFalloffStart = std::cos(falloffStartDeg * std::numbers::pi_v<float> / kHalfTurnDegrees);
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
    }

#endif // USE_IMGUI
}