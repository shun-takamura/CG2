#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <array> 
#include "Log.h"
#include <cassert>

class Object3DManager{

    // ブレンドモード
    enum BlendMode {
        // ブレンド無し
        kBlendModeNone,

        // 通常αブレンド。デフォルト。Src*SrcA+Dest*(1-SrcA)
        kBlendModeNormal,

        // 加算 Src*SrcA+Dest*1
        kBlendModeAdd,

        // 減算 Dest*1-Src*SrcA
        kBlendModeSubtract,

        // 乗算 Src*0+Dest*Src
        kBlendModeMultily,

        // スクリーン Src*(1-Dest)+Dest*1
        kBlendModeScreen,

        // 利用してはいけない
        kCountOfBlendMode
    };

    BlendMode blendMode_ = kBlendModeNormal;

	DirectXCore* dxCore_;

	// シェーダーが使うデータの設計図
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

	// 描画の全工程の設定
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    // 全ブレンドモード分の PSO を保持する
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

    int currentBlendMode_ = 0;

	// ルートシグネチャの作成
	void CreateRootSignature();

	// グラフィックパイプラインの生成
	void CreateGraphicsPipelineState(BlendMode mode);

public:
	void Initialize(DirectXCore* dxCore);

	void DrawSetting();

    void SetBlendMode(BlendMode blendMode);

    ~Object3DManager();

    // Releaseメソッド
    void Release() {
        rootSignature_.Reset();
        pipelineState_.Reset();
        for (auto& pipelineState : pipelineStates_) {
            pipelineState.Reset();
        }
    }

	// ゲッターロボ
	DirectXCore* GetDxCore() const { return dxCore_; }
    BlendMode GetBlendMode() const { return blendMode_; }

};

