#pragma once
#include "DirectXCore.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include"MathUtility.h"
#include <numbers>
#include <cmath>
#include <wrl.h>

class LightManager {
private:
    // シングルトン
    static LightManager* instance_;

    DirectXCore* dxCore_ = nullptr;

    // DirectionalLight
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // PointLight
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLight* pointLightData_ = nullptr;

    // SpotLight
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLight* spotLightData_ = nullptr;


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

    // DirectionalLight設定
    void SetDirectionalLightColor(const Vector4& color) { directionalLightData_->color = color; }
    void SetDirectionalLightDirection(const Vector3& direction) { directionalLightData_->direction = direction; }
    void SetDirectionalLightIntensity(float intensity) { directionalLightData_->intensity = intensity; }
    void SetDirectionalLightType(int type) { directionalLightData_->lightingType = type; }

    // PointLight設定
    void SetPointLightColor(const Vector4& color) { pointLightData_->color = color; }
    void SetPointLightPosition(const Vector3& position) { pointLightData_->position = position; }
    void SetPointLightIntensity(float intensity) { pointLightData_->intensity = intensity; }

    // SpotLight設定
    void SetSpotLightColor(const Vector4& color) { spotLightData_->color = color; }
    void SetSpotLightPosition(const Vector3& position) { spotLightData_->position = position; }
    void SetSpotLightDirection(const Vector3& direction) { spotLightData_->direction = direction; }
    void SetSpotLightIntensity(float intensity) { spotLightData_->intensity = intensity; }
    void SetSpotLightDistance(float distance) { spotLightData_->distance = distance; }
    void SetSpotLightDecay(float decay) { spotLightData_->decay = decay; }
    void SetSpotLightCosAngle(float cosAngle) { spotLightData_->cosAngle = cosAngle; }
    void SetSpotLightCosFalloffStart(float cosFalloffStart) { spotLightData_->cosFalloffStart = cosFalloffStart; }

    // ゲッター
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    PointLight* GetPointLightData() { return pointLightData_; }
    SpotLight* GetSpotLightData() { return spotLightData_; }

    // ImGui用
    void OnImGui();
};
