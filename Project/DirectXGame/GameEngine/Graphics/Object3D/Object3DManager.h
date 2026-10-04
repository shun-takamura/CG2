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
#include "Vector4.h"
#include <string>

class CloudLayer;

class Object3DManager{
public:
    /// <summary>
    /// 距離フォグ（PS の b6）。全 Object3D PSO 共通で、毎フレーム1回だけバインドする。
    /// HLSL 側の FogBuffer と 1:1（float4 + float×3 + int = 32byte）。
    /// enabled=0 の間はシェーダ側で完全にスキップされるので、設定しないシーンは無影響。
    /// </summary>
    struct FogParams {
        Vector4 color{ 0.5f, 0.6f, 0.7f, 1.0f };
        float   nearDist = 50.0f;
        float   farDist = 800.0f;
        float   density = 1.0f;
        int     enabled = 0;
    };

    // シェーダー種別
    enum ShaderType {
        kShaderEnvironmentMap,     // 環境マップあり
        kShaderNoEnvironmentMap,   // 環境マップなし
        kShaderPBR,                // PBR（Cook-Torrance）
        kCountOfShaderType
    };

    // 平面リフレクション用の鏡側 CB（VS b1）のルートパラメータ番号。
    // 既存インデックスを動かさないよう末尾に足してある。通常描画のシェーダは参照しない。
    static constexpr UINT kRootReflectionCamera = 12;
    // 遠景の雲（CloudSky.hlsli の b7 / t5）。PBR の鏡面反射に雲を映すために使う
    static constexpr UINT kRootCloudParams = 13;
    static constexpr UINT kRootCloudNoise = 14;

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

    // 平面リフレクション用 PSO（ShaderType ごと、Normal ブレンドのみ）。
    // VS を反射用に差し替え、鏡像で三角形の向きが反転するのでカリングも反転する。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfShaderType> reflectionPipelineStates_;

    // グラフィックパイプラインの生成。forReflection=true なら reflectionPipelineStates_ に作る
    void CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode, bool forReflection = false);

    // ID Pass 用：Object3d.VS + WriteID.PS、出力 R8_UINT、深度テストあり書き込み無し
    Microsoft::WRL::ComPtr<ID3D12RootSignature> idRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> idPipelineState_;
    void CreateIdPassObjects();

    // シャドウ受光リソース（毎フレーム Framework から設定）。0 のうちは未バインド。
    D3D12_GPU_VIRTUAL_ADDRESS   shadowConstantsAddr_ = 0;       // b5 = ShadowConstants
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle_{};             // t3 = シャドウマップ SRV

    // 距離フォグ（b6）。Initialize で確保し、DrawSetting で毎フレームバインドする。
    Microsoft::WRL::ComPtr<ID3D12Resource> fogResource_;
    FogParams* fogData_ = nullptr;

    // 遠景の雲（b7 / t5）。未設定のシーンは enabled=0 のダミー CB とダミーテクスチャを挿す
    CloudLayer* cloudLayer_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> disabledCloudResource_;

public:
  
	void Initialize(DirectXCore* dxCore);

	void DrawSetting();

    void SetBlendMode(BlendMode blendMode);

    ~Object3DManager();

    // 現在のShaderTypeに応じたPSOを取得
    ID3D12PipelineState* GetPipelineState(ShaderType shaderType) const {
        return pipelineStates2D_[shaderType][currentBlendMode_].Get();
    }

    // 平面リフレクション用のPSOを取得
    ID3D12PipelineState* GetReflectionPipelineState(ShaderType shaderType) const {
        return reflectionPipelineStates_[shaderType].Get();
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
        for (auto& pipelineState : reflectionPipelineStates_) {
            pipelineState.Reset();
        }
    }

    // ID Pass の PSO / RootSignature
    ID3D12PipelineState* GetIdPipelineState() const { return idPipelineState_.Get(); }
    ID3D12RootSignature* GetIdRootSignature() const { return idRootSignature_.Get(); }

    // セッター
    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }

    // 環境マップを設定（シーン全体で使用するCubemapファイルパス）
    void SetEnvironmentTexture(const std::string& filePath) {
        environmentTexturePath_ = filePath;
    }

    /// <summary>
    /// 距離フォグを設定する。シーンをまたいで残るので、使い終わったシーンは
    /// enabled=0 の FogParams を渡して必ず戻すこと。
    /// </summary>
    void SetFogParams(const FogParams& params) {
        if (fogData_) *fogData_ = params;
    }
    void DisableFog() {
        if (fogData_) fogData_->enabled = 0;
    }

    /// <summary>
    /// フォグ CB（b6 = rootParameter[11]）をバインドする。
    /// **ルートシグネチャを貼り直した直後は必ず呼ぶこと**。グラフィックスルートシグネチャを
    /// セットし直すと全ルート引数が無効化されるため（AnimatedObject3DInstance が
    /// スキニング CS のあとで貼り直している）、呼ばないと b6 が未バインドのまま描画される。
    /// </summary>
    void BindFog(ID3D12GraphicsCommandList* commandList) const {
        if (fogResource_) {
            commandList->SetGraphicsRootConstantBufferView(11, fogResource_->GetGPUVirtualAddress());
        }
    }

    // シャドウ受光リソースを設定（Framework の初期化で ShadowMap を1回配線する。
    // ShadowMap の CB / SRV は寿命中アドレス不変で、影未使用シーンでも enabled=0 で安全）。
    // DrawSetting / BindShadow で b5(ShadowConstants)/t3(シャドウマップ) をバインドする。
    void SetShadowBindings(D3D12_GPU_VIRTUAL_ADDRESS constantsAddr, D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
        shadowConstantsAddr_ = constantsAddr;
        shadowSrvHandle_ = srvHandle;
    }

    /// <summary>
    /// シャドウ受光の b5(ShadowConstants = rootParameter[8]) と t3(シャドウマップ = rootParameter[9])
    /// をバインドする。**ルートシグネチャを貼り直した直後は BindFog と一緒に必ず呼ぶこと**。
    /// PS は Shadow.hlsli 経由で b5/t3 を無条件参照するため、未バインドだと
    /// GPU ベース検証 #935(ROOT_ARGUMENT_UNINITIALIZED) で落ちる。
    /// </summary>
    void BindShadow(ID3D12GraphicsCommandList* commandList) const {
        if (shadowConstantsAddr_ != 0) {
            commandList->SetGraphicsRootConstantBufferView(8, shadowConstantsAddr_);
            commandList->SetGraphicsRootDescriptorTable(9, shadowSrvHandle_);
        }
    }

    /// <summary>
    /// PBR の鏡面反射に映す雲。シーンをまたいで残るので、雲を持つシーンは終了時に必ず nullptr に戻すこと
    /// （CloudLayer の寿命はシーン側）。映る強さはマテリアルの cloudReflection（既定 0）で決まる。
    /// </summary>
    void SetCloudLayer(CloudLayer* cloudLayer) { cloudLayer_ = cloudLayer; }

    /// <summary>
    /// 雲の b7(rootParameter[13]) / t5(rootParameter[14]) をバインドする。
    /// **ルートシグネチャを貼り直した直後は BindFog / BindShadow と一緒に必ず呼ぶこと**。
    /// </summary>
    void BindCloud(ID3D12GraphicsCommandList* commandList) const;

    // 別ルートシグネチャ（水面など）から同じシャドウ／フォグを参照するための取得口
    D3D12_GPU_VIRTUAL_ADDRESS GetShadowConstantsAddress() const { return shadowConstantsAddr_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSrvHandle() const { return shadowSrvHandle_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetFogAddress() const {
        return fogResource_ ? fogResource_->GetGPUVirtualAddress() : 0;
    }

	// ゲッターロボ
	DirectXCore* GetDxCore() const { return dxCore_; }
    BlendMode GetBlendMode() const { return blendMode_; }
    Camera* GetDefaultCamera()const { return defaultCamera_; }
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    const std::string& GetEnvironmentTexturePath() const { return environmentTexturePath_; }
};

