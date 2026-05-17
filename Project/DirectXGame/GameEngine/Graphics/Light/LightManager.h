#pragma once
#include "DirectXCore.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "MathUtility.h"
#include <array>
#include <numbers>
#include <cmath>
#include <wrl.h>

// Acquire 失敗時に返す無効スロット値
static constexpr uint32_t kInvalidLightSlot = UINT32_MAX;

class LightManager {
private:
    // シングルトン
    //static LightManager* instance_;

    DirectXCore* dxCore_ = nullptr;

    // DirectionalLight
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // PointLightGroup (複数対応)
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightGroup* pointLightGroupData_ = nullptr;

    // SpotLightGroup (複数対応)
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLightGroup* spotLightGroupData_ = nullptr;

    // 動的予約フラグ（Acquire/Release で管理）
    std::array<bool, kMaxPointLights> pointReserved_{};
    std::array<bool, kMaxSpotLights>  spotReserved_{};

    // コンストラクタ（private）
    LightManager() = default;
    ~LightManager() = default;

public:
    // コピー禁止
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

    // シングルトンインスタンス取得
    static LightManager* GetInstance();

    // 初期化・終了
    void Initialize(DirectXCore* dxCore);
    void Finalize();

    // 描画前にライトをバインド（全オブジェクト共通）
    void BindLights(ID3D12GraphicsCommandList* commandList);

    // ===== DirectionalLight設定 =====
    void SetDirectionalLightColor(const Vector4& color) { directionalLightData_->color = color; }
    void SetDirectionalLightDirection(const Vector3& direction) { directionalLightData_->direction = direction; }
    void SetDirectionalLightIntensity(float intensity) { directionalLightData_->intensity = intensity; }
    void SetDirectionalLightType(int type) { directionalLightData_->lightingType = type; }

    // ===== PointLight設定 (インデックス指定) =====
    void SetPointLightCount(uint32_t count);
    uint32_t GetPointLightCount() const { return pointLightGroupData_->activeCount; }
    void SetPointLightColor(uint32_t index, const Vector4& color);
    void SetPointLightPosition(uint32_t index, const Vector3& position);
    void SetPointLightIntensity(uint32_t index, float intensity);
    void SetPointLightRadius(uint32_t index, float radius);
    void SetPointLightDecay(uint32_t index, float decay);

    // ===== 動的予約API（Effect Editor 用） =====
    // 空きスロットを確保し、そのインデックスを返す。空き無しなら kInvalidLightSlot。
    // 確保した時点で intensity=0 にリセットされる。
    uint32_t AcquirePointLight();
    uint32_t AcquireSpotLight();
    // スロットを解放（intensity=0 にして予約フラグを下ろす）。
    void ReleasePointLight(uint32_t slot);
    void ReleaseSpotLight(uint32_t slot);

    // ===== SpotLight設定 (インデックス指定) =====
    void SetSpotLightCount(uint32_t count);
    uint32_t GetSpotLightCount() const { return spotLightGroupData_->activeCount; }
    void SetSpotLightColor(uint32_t index, const Vector4& color);
    void SetSpotLightPosition(uint32_t index, const Vector3& position);
    void SetSpotLightDirection(uint32_t index, const Vector3& direction);
    void SetSpotLightIntensity(uint32_t index, float intensity);
    void SetSpotLightDistance(uint32_t index, float distance);
    void SetSpotLightDecay(uint32_t index, float decay);
    void SetSpotLightCosAngle(uint32_t index, float cosAngle);
    void SetSpotLightCosFalloffStart(uint32_t index, float cosFalloffStart);

    // ===== ゲッター =====
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    PointLightGroup* GetPointLightGroupData() { return pointLightGroupData_; }
    SpotLightGroup* GetSpotLightGroupData() { return spotLightGroupData_; }
    PointLight* GetPointLight(uint32_t index);
    SpotLight* GetSpotLight(uint32_t index);

    // ImGui用
    void OnImGui();
};