#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <array> 
#include "Log.h"
#include <cassert>
#include"Camera.h"
#include "TextureManager.h"
#include <string>  



class Object3DManager{
public:
    // シェーダー種別
    enum ShaderType {
        kShaderEnvironmentMap,     // 環境マップあり
        kShaderNoEnvironmentMap,   // 環境マップなし
        kCountOfShaderType
    };

private:

    Camera* defaultCamera_ = nullptr;
    std::string environmentTexturePath_;

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
	//Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    // PSO配列を保持
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode>, kCountOfShaderType> pipelineStates2D_;

    int currentBlendMode_ = 0;

	// ルートシグネチャの作成
	void CreateRootSignature();

    // グラフィックパイプラインの生成（引数追加）
    void CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode);

public:
  
	void Initialize(DirectXCore* dxCore);

	void DrawSetting();

    void SetBlendMode(BlendMode blendMode);

    ~Object3DManager();

    // 現在のShaderTypeに応じたPSOを取得
    ID3D12PipelineState* GetPipelineState(ShaderType shaderType) const {
        return pipelineStates2D_[shaderType][currentBlendMode_].Get();
    }

    // Releaseメソッド
    void Release() {
        rootSignature_.Reset();
        //pipelineState_.Reset();
        for (auto& pipelineStateArray : pipelineStates2D_) {
            for (auto& pipelineState : pipelineStateArray) {
                pipelineState.Reset();
            }
        }
    }

    // セッター
    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }

    // 環境マップを設定（シーン全体で使用するCubemapファイルパス）
    void SetEnvironmentTexture(const std::string& filePath) {
        environmentTexturePath_ = filePath;
    }

	// ゲッターロボ
	DirectXCore* GetDxCore() const { return dxCore_; }
    BlendMode GetBlendMode() const { return blendMode_; }
    Camera* GetDefaultCamera()const { return defaultCamera_; }

};

