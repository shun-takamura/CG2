#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <array>
#include "SRVManager.h"

// 前方宣言
class Camera;

class ParticleManager
{
public:
    // ブレンドモード
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultily,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    BlendMode blendMode_ = kBlendModeNone;

    // 初期化
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

    // 描画設定
    void DrawSetting();

    // ブレンドモードのセット
    void SetBlendMode(BlendMode blendMode);

    // カメラのセット
    void SetCamera(Camera* camera) { camera_ = camera; }

    // ゲッター
    DirectXCore* GetDxCore() const { return dxCore_; }
    SRVManager* GetSRVManager() const { return srvManager_; }
    Camera* GetCamera() const { return camera_; }
    BlendMode GetBlendMode() const { return blendMode_; }

    // デストラクタ
    ~ParticleManager();

    // リソース解放
    void Release() {
        rootSignature_.Reset();
        for (auto& pipelineState : pipelineStates_) {
            pipelineState.Reset();
        }
    }

private:
    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    Camera* camera_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

    // 全ブレンドモード分のPSOを保持
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState(BlendMode mode);
};