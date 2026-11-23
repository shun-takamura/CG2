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
struct Material;

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
        kShaderTerrain,            // 地形（スプラット＋3 層。.mat v5 の shadingModel = ShadingModel::kTerrain）
        kCountOfShaderType
    };

    // 平面リフレクション用の鏡側 CB（VS b1）のルートパラメータ番号。
    // 既存インデックスを動かさないよう末尾に足してある。通常描画のシェーダは参照しない。
    static constexpr UINT kRootReflectionCamera = 12;
    // 遠景の雲（CloudSky.hlsli の b7 / t5）。PBR の鏡面反射に雲を映すために使う
    static constexpr UINT kRootCloudParams = 13;
    static constexpr UINT kRootCloudNoise = 14;
    // 視差オクルージョン（POM）のハイトマップ（PS t4）。ModelInstance が submesh ごとに貼る
    static constexpr UINT kRootHeightMap = 15;
    // 地形の層の配列（PS t6 = 色 / t7 = 法線。Texture2DArray）。ModelInstance が地形の submesh だけに貼る
    static constexpr UINT kRootTerrainLayerColor = 16;
    static constexpr UINT kRootTerrainLayerNormal = 17;
    // 通常描画のルートシグネチャのルートパラメータの番号（CreateRootSignature と各描画で同じ番号を使う）
    static constexpr UINT kRootMaterial = 0;          // PS b0：マテリアル
    static constexpr UINT kRootTransform = 1;         // VS b0：TransformationMatrix
    static constexpr UINT kRootTexture = 2;           // PS t0：ベースカラー（テーブル）
    static constexpr UINT kRootDirectionalLight = 3;  // PS b1：平行光源
    static constexpr UINT kRootCamera = 4;            // PS b2：カメラ
    static constexpr UINT kRootPointLight = 5;        // PS b3：点光源
    static constexpr UINT kRootSpotLight = 6;         // PS b4：スポットライト
    static constexpr UINT kRootEnvironmentMap = 7;    // PS t1：環境マップ（テーブル）
    static constexpr UINT kRootShadowConstants = 8;   // PS b5：ShadowConstants
    static constexpr UINT kRootShadowMap = 9;         // PS t3：シャドウマップ（テーブル）
    static constexpr UINT kRootNormalMap = 10;        // PS t2：法線マップ（テーブル）
    static constexpr UINT kRootFog = 11;              // PS b6：距離フォグ
    static constexpr UINT kRootParamCount = 18;       // 通常描画のルートパラメータの数
    // ID Pass のルートシグネチャのルートパラメータの番号
    static constexpr UINT kIdRootTransform = 0;       // VS b0：TransformationMatrix
    static constexpr UINT kIdRootConstants = 1;       // PS b0：ルート定数（id ＋ディゾルブの値）
    static constexpr UINT kIdRootParamCount = 2;
    // 発光パスのルートシグネチャのルートパラメータの番号
    static constexpr UINT kBloomRootTransform = 0;    // VS b0：TransformationMatrix
    static constexpr UINT kBloomRootMaterial = 1;     // PS b0：Material
    static constexpr UINT kBloomRootConstants = 2;    // PS b1：倍率のルート定数
    static constexpr UINT kBloomRootBaseColor = 3;    // PS t0：ベースカラー（テーブル）
    static constexpr UINT kBloomRootParamCount = 4;

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

    // PSO配列を保持
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode>, kCountOfShaderType> pipelineStates2D_;

    int currentBlendMode_ = 0;

	/// <summary>
	/// ルートシグネチャの作成。[0]=PS b0 マテリアル / [1]=VS b0 変換 / [2]=PS t0 テクスチャ / [3]=PS b1 平行光源 / [4]=PS b2 カメラ /
	/// [5]=PS b3 点光源 / [6]=PS b4 スポットライト / [7]=PS t1 環境マップ / [8]=PS b5 シャドウ定数 / [9]=PS t3 シャドウマップ /
	/// [10]=PS t2 法線マップ / [11]=PS b6 フォグ / [12]=VS b1 鏡像 ViewProj / [13]=PS b7 雲 / [14]=PS t5 雲のノイズ /
	/// [15]=PS t4 ハイトマップ / [16]=PS t6 地形の層の色 / [17]=PS t7 地形の層の法線。
	/// サンプラー s0=通常 / s1=シャドウ比較 / s2=シャドウ生深度 / s4=雲のノイズ / s5=地形の層
	/// </summary>
	void CreateRootSignature();

    // 平面リフレクション用 PSO（ShaderType ごと、Normal ブレンドのみ）。
    // VS を反射用に差し替え、鏡像で三角形の向きが反転するのでカリングも反転する。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfShaderType> reflectionPipelineStates_;

    /// <summary>グラフィックパイプラインの生成。forReflection=true なら反射用 VS・カリング反転で reflectionPipelineStates_ に作る</summary>
    /// <param name="shaderType">シェーダーの種類</param>
    /// <param name="blendMode">ブレンドモード（反射用は Normal のみ）</param>
    /// <param name="forReflection">平面リフレクション用なら true</param>
    void CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode, bool forReflection = false);

    // ID Pass 用：Object3d.VS + WriteID.PS、出力 R8_UINT、深度テストあり書き込み無し
    Microsoft::WRL::ComPtr<ID3D12RootSignature> idRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> idPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> idDissolvePipelineState_;
    /// <summary>ID Pass のルートシグネチャ（[0]=VS b0 変換 / [1]=PS b0 ルート定数 kIdPassConstantCount 個）と通常・ディゾルブ用の PSO を作る</summary>
    void CreateIdPassObjects();

    // 発光パス用（ブルームの発光 RT に光る分だけ書く）：Object3d.VS + WriteEmissive.PS、
    // 出力 float（BloomEffect::kFormat）、加算、深度テストあり書き込み無し
    Microsoft::WRL::ComPtr<ID3D12RootSignature> bloomRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPipelineState_;
    /// <summary>発光パスのルートシグネチャ（[0]=VS b0 / [1]=PS b0 / [2]=PS b1 ルート定数 / [3]=PS t0）と PSO を作る</summary>
    void CreateBloomPassObjects();

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
  
	/// <summary>ルートシグネチャ・全 ShaderType×全 BlendMode と反射用の PSO・フォグ CB・雲のダミー・ID/発光パスを作る</summary>
	/// <param name="dxCore">デバイスとコマンドリストの取得元</param>
	void Initialize(DirectXCore* dxCore);

	/// <summary>描画前の共通設定。ルートシグネチャ・既定 PSO・環境マップ（法線マップの仮も）・シャドウ・フォグ・雲・ハイトマップの仮をバインドする</summary>
	void DrawSetting();

    /// <summary>GetPipelineState が返す PSO のブレンドモードを切り替える</summary>
    /// <param name="blendMode">ブレンドモード</param>
    void SetBlendMode(BlendMode blendMode);

    /// <summary>ルートシグネチャと PSO を解放する</summary>
    ~Object3DManager();

    /// <summary>現在のブレンドモードと ShaderType に応じたPSOを取得</summary>
    /// <param name="shaderType">シェーダーの種類</param>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetPipelineState(ShaderType shaderType) const {
        return pipelineStates2D_[shaderType][currentBlendMode_].Get();
    }

    /// <summary>平面リフレクション用のPSOを取得</summary>
    /// <param name="shaderType">シェーダーの種類</param>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetReflectionPipelineState(ShaderType shaderType) const {
        return reflectionPipelineStates_[shaderType].Get();
    }

    /// <summary>マテリアルの shadingModel と環境マップの有無から、使うシェーダーの種類を選ぶ</summary>
    /// <param name="material">マテリアル（nullptr なら環境マップなし）</param>
    /// <param name="allowTerrain">地形のシェーダーを使ってよいか（地形の層を持たないスキニングのモデルは false）</param>
    /// <returns>シェーダーの種類</returns>
    static ShaderType SelectShaderType(const Material* material, bool allowTerrain);

    /// <summary>ルートシグネチャと通常・反射用の PSO を解放する</summary>
    void Release() {
        rootSignature_.Reset();
        for (auto& pipelineStateArray : pipelineStates2D_) {
            for (auto& pipelineState : pipelineStateArray) {
                pipelineState.Reset();
            }
        }
        for (auto& pipelineState : reflectionPipelineStates_) {
            pipelineState.Reset();
        }
    }

    /// <summary>ID Pass の PSO を返す</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetIdPipelineState() const { return idPipelineState_.Get(); }
    /// <summary>ディゾルブ中の物用（WriteIDDissolve.PS）の ID Pass の PSO を返す。ルート定数 [1] に id ＋ディゾルブの値を kIdPassConstantCount 個渡す</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetIdDissolvePipelineState() const { return idDissolvePipelineState_.Get(); }
    static constexpr UINT kIdPassConstantCount = 8;
    /// <summary>ID Pass のルートシグネチャを返す</summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetIdRootSignature() const { return idRootSignature_.Get(); }

    /// <summary>
    /// 発光パスの PSO を返す。ルート [0] VS b0 = TransformationMatrix / [1] PS b0 = Material /
    /// [2] PS b1 = ルート定数 kBloomPassConstantCount 個（倍率） / [3] PS t0 = ベースカラー
    /// </summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetBloomPipelineState() const { return bloomPipelineState_.Get(); }
    /// <summary>発光パスのルートシグネチャを返す</summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetBloomRootSignature() const { return bloomRootSignature_.Get(); }
    static constexpr UINT kBloomPassConstantCount = 4;

    // セッター
    /// <summary>新しく作る Object3D に渡す既定のカメラを設定する</summary>
    /// <param name="camera">カメラ</param>
    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }

    /// <summary>環境マップを設定（シーン全体で使用するCubemapファイルパス。PS t1）</summary>
    /// <param name="filePath">cubemap のパス</param>
    void SetEnvironmentTexture(const std::string& filePath) {
        environmentTexturePath_ = filePath;
    }

    /// <summary>
    /// 距離フォグを設定する。シーンをまたいで残るので、使い終わったシーンは
    /// enabled=0 の FogParams を渡して必ず戻すこと。
    /// </summary>
    /// <param name="params">フォグの設定</param>
    void SetFogParams(const FogParams& params) {
        if (fogData_) *fogData_ = params;
    }
    /// <summary>距離フォグを切る</summary>
    void DisableFog() {
        if (fogData_) fogData_->enabled = 0;
    }

    /// <summary>
    /// フォグ CB（b6 = rootParameter[11]）をバインドする。
    /// **ルートシグネチャを貼り直した直後は必ず呼ぶこと**。グラフィックスルートシグネチャを
    /// セットし直すと全ルート引数が無効化されるため（AnimatedObject3DInstance が
    /// スキニング CS のあとで貼り直している）、呼ばないと b6 が未バインドのまま描画される。
    /// </summary>
    /// <param name="commandList">バインド先のコマンドリスト</param>
    void BindFog(ID3D12GraphicsCommandList* commandList) const {
        if (fogResource_) {
            commandList->SetGraphicsRootConstantBufferView(kRootFog, fogResource_->GetGPUVirtualAddress());
        }
    }

    /// <summary>
    /// シャドウ受光リソースを設定（Framework の初期化で ShadowMap を1回配線する。
    /// ShadowMap の CB / SRV は寿命中アドレス不変で、影未使用シーンでも enabled=0 で安全）。
    /// DrawSetting / BindShadow で b5(ShadowConstants)/t3(シャドウマップ) をバインドする。
    /// </summary>
    /// <param name="constantsAddr">ShadowConstants の CB のアドレス</param>
    /// <param name="srvHandle">シャドウマップの SRV</param>
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
    /// <param name="commandList">バインド先のコマンドリスト</param>
    void BindShadow(ID3D12GraphicsCommandList* commandList) const {
        if (shadowConstantsAddr_ != 0) {
            commandList->SetGraphicsRootConstantBufferView(kRootShadowConstants, shadowConstantsAddr_);
            commandList->SetGraphicsRootDescriptorTable(kRootShadowMap, shadowSrvHandle_);
        }
    }

    /// <summary>
    /// PBR の鏡面反射に映す雲。シーンをまたいで残るので、雲を持つシーンは終了時に必ず nullptr に戻すこと
    /// （CloudLayer の寿命はシーン側）。映る強さはマテリアルの cloudReflection（既定 0）で決まる。
    /// </summary>
    /// <param name="cloudLayer">雲（nullptr で外す）</param>
    void SetCloudLayer(CloudLayer* cloudLayer) { cloudLayer_ = cloudLayer; }

    /// <summary>
    /// 雲の b7(rootParameter[13]) / t5(rootParameter[14]) をバインドする。
    /// **ルートシグネチャを貼り直した直後は BindFog / BindShadow と一緒に必ず呼ぶこと**。
    /// </summary>
    /// <param name="commandList">バインド先のコマンドリスト</param>
    void BindCloud(ID3D12GraphicsCommandList* commandList) const;

    /// <summary>
    /// ハイトマップ t4(rootParameter[15]) にダミーを貼る。視差を使わない描画でも未バインドにしないため。
    /// ModelInstance / AnimatedModelInstance は submesh ごとに貼り直す
    /// </summary>
    /// <param name="commandList">バインド先のコマンドリスト</param>
    void BindHeightMapFallback(ID3D12GraphicsCommandList* commandList) const;

    /// <summary>別ルートシグネチャ（水面など）から同じシャドウを参照するための取得口</summary>
    /// <returns>ShadowConstants の CB のアドレス（未設定なら 0）</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetShadowConstantsAddress() const { return shadowConstantsAddr_; }
    /// <summary>シャドウマップの SRV を返す</summary>
    /// <returns>SRV のハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSrvHandle() const { return shadowSrvHandle_; }
    /// <summary>別ルートシグネチャから同じフォグを参照するための取得口</summary>
    /// <returns>フォグ CB のアドレス（未作成なら 0）</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetFogAddress() const {
        return fogResource_ ? fogResource_->GetGPUVirtualAddress() : 0;
    }

	// ゲッターロボ
	/// <summary>DirectXCore を返す</summary>
	/// <returns>Initialize で受け取った DirectXCore</returns>
	DirectXCore* GetDxCore() const { return dxCore_; }
    /// <summary>現在のブレンドモードを返す</summary>
    /// <returns>ブレンドモード</returns>
    BlendMode GetBlendMode() const { return blendMode_; }
    /// <summary>既定のカメラを返す</summary>
    /// <returns>カメラ（未設定なら nullptr）</returns>
    Camera* GetDefaultCamera()const { return defaultCamera_; }
    /// <summary>通常描画のルートシグネチャを返す（割り当ては CreateRootSignature を参照）</summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    /// <summary>環境マップのパスを返す</summary>
    /// <returns>cubemap のパス（未設定なら空）</returns>
    const std::string& GetEnvironmentTexturePath() const { return environmentTexturePath_; }
};

