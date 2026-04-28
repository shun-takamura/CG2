#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <array>
#include "Log.h"
#include <cassert>
#include "Camera.h"
#include "TextureManager.h"
#include <string>

class SkinningObject3DManager
{
public:
    // シェーダー種別
    enum ShaderType {
        kShaderEnvironmentMap,
        kShaderNoEnvironmentMap,
        kCountOfShaderType
    };

    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultily,
        kBlendModeScreen,
        kCountOfBlendMode
    };

private:
    Camera* defaultCamera_ = nullptr;
    std::string environmentTexturePath_;

    BlendMode blendMode_ = kBlendModeNormal;
    int currentBlendMode_ = 0;

    DirectXCore* dxCore_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

    // [ShaderType][BlendMode]
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode>, kCountOfShaderType> pipelineStates_;

    void CreateRootSignature();
    void CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode);

public:
    void Initialize(DirectXCore* dxCore);
    void DrawSetting();
    void SetBlendMode(BlendMode blendMode);
    ~SkinningObject3DManager();

    ID3D12PipelineState* GetPipelineState(ShaderType shaderType) const {
        return pipelineStates_[shaderType][currentBlendMode_].Get();
    }

    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    void Release() {
        rootSignature_.Reset();
        for (auto& arr : pipelineStates_) {
            for (auto& pso : arr) {
                pso.Reset();
            }
        }
    }

    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }
    void SetEnvironmentTexture(const std::string& filePath) {
        environmentTexturePath_ = filePath;
    }

    DirectXCore* GetDxCore() const { return dxCore_; }
    BlendMode GetBlendMode() const { return blendMode_; }
    Camera* GetDefaultCamera() const { return defaultCamera_; }
    // publicに追加
    const std::string& GetEnvironmentTexturePath() const { return environmentTexturePath_; }
};