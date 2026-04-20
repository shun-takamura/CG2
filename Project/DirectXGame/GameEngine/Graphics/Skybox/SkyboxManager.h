#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "Log.h"
#include <cassert>
#include "Camera.h"

/// <summary>
/// Skybox描画の共通設定を管理するクラス
/// RootSignatureとPipelineStateを全Skyboxで共有する
/// </summary>
class SkyboxManager {

    Camera* defaultCamera_ = nullptr;

    DirectXCore* dxCore_ = nullptr;

    // シェーダーが使うデータの設計図
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

    // 描画の全工程の設定
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    // ルートシグネチャの作成
    void CreateRootSignature();

    // グラフィックパイプラインの生成
    void CreateGraphicsPipelineState();

public:
    void Initialize(DirectXCore* dxCore);

    void DrawSetting();

    ~SkyboxManager();

    // Releaseメソッド
    void Release() {
        rootSignature_.Reset();
        pipelineState_.Reset();
    }

    // セッター
    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }

    // ゲッター
    DirectXCore* GetDxCore() const { return dxCore_; }
    Camera* GetDefaultCamera() const { return defaultCamera_; }
};