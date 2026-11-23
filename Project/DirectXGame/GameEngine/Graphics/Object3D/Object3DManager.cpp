#include "Object3DManager.h"
#include "BloomEffect.h"
#include "Cloud/CloudLayer.h"
#include "Material.h"

namespace {
    // 通常描画の SRV のレジスタ（PS t0〜t7）
    constexpr UINT kRegTextureSrv = 0;        // t0：ベースカラー
    constexpr UINT kRegEnvironmentSrv = 1;    // t1：環境マップ
    constexpr UINT kRegNormalMapSrv = 2;      // t2：法線マップ
    constexpr UINT kRegShadowMapSrv = 3;      // t3：シャドウマップ
    constexpr UINT kRegHeightMapSrv = 4;      // t4：視差のハイトマップ
    constexpr UINT kRegCloudNoiseSrv = 5;     // t5：遠景の雲のノイズ
    constexpr UINT kRegTerrainColorSrv = 6;   // t6：地形の層の色
    constexpr UINT kRegTerrainNormalSrv = 7;  // t7：地形の層の法線
    // 通常描画の CBV のレジスタ（PS b0〜b7 / VS b0・b1）
    constexpr UINT kRegMaterialCb = 0;            // PS b0
    constexpr UINT kRegTransformCb = 0;           // VS b0
    constexpr UINT kRegDirectionalLightCb = 1;    // PS b1
    constexpr UINT kRegCameraCb = 2;              // PS b2
    constexpr UINT kRegPointLightCb = 3;          // PS b3
    constexpr UINT kRegSpotLightCb = 4;           // PS b4
    constexpr UINT kRegShadowConstantsCb = 5;     // PS b5
    constexpr UINT kRegFogCb = 6;                 // PS b6
    constexpr UINT kRegReflectionCameraCb = 1;    // VS b1
    constexpr UINT kRegCloudParamsCb = 7;         // PS b7
    // 通常描画の静的サンプラーの数とレジスタ（s0 / s1 / s2 / s4 / s5）
    constexpr int kStaticSamplerCount = 5;
    constexpr UINT kRegLinearSampler = 0;         // s0：通常テクスチャ
    constexpr UINT kRegShadowCompareSampler = 1;  // s1：シャドウ比較
    constexpr UINT kRegShadowDepthSampler = 2;    // s2：シャドウ生深度読み
    constexpr UINT kRegCloudNoiseSampler = 4;     // s4：雲のノイズ
    constexpr UINT kRegTerrainSampler = 5;        // s5：地形の層
    // 雲のノイズ・地形の層のサンプラーの異方性の最大値
    constexpr UINT kCloudNoiseMaxAnisotropy = 8;
    constexpr UINT kTerrainMaxAnisotropy = 16;
}

void Object3DManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateRootSignature();

    // 全ShaderType × 全BlendModeのPSOを作成
    for (int st = 0; st < kCountOfShaderType; ++st) {
        for (int bm = 0; bm < kCountOfBlendMode; ++bm) {
            CreateGraphicsPipelineState(
                static_cast<ShaderType>(st),
                static_cast<BlendMode>(bm)
            );
        }
        CreateGraphicsPipelineState(static_cast<ShaderType>(st), kBlendModeNormal, true);
    }

    blendMode_ = kBlendModeNormal;
    currentBlendMode_ = static_cast<int>(blendMode_);

    // 距離フォグ用の定数バッファ（b6）。既定は enabled=0 なので設定しないシーンは無影響。
    fogResource_ = dxCore_->CreateBufferResource(sizeof(FogParams));
    fogResource_->Map(0, nullptr, reinterpret_cast<void**>(&fogData_));
    *fogData_ = FogParams{};

    // 雲（b7 / t5）を使わないシーン用のダミー。PBR の PS が無条件に参照するので未バインドにしない
    disabledCloudResource_ = CloudLayer::CreateDisabledConstantBuffer(dxCore_);
    TextureManager::GetInstance()->LoadTexture(CloudLayer::GetFallbackTexturePath());

    // ID Pass 用 PSO / RootSignature を1回だけ作成
    CreateIdPassObjects();
    CreateBloomPassObjects();
}

void Object3DManager::DrawSetting()
{  
    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());

    // デフォルトは環境マップありのPSOをセット（個別オブジェクトで上書きされる）
    dxCore_->GetCommandList()->SetPipelineState(
        pipelineStates2D_[kShaderEnvironmentMap][currentBlendMode_].Get()
    );

    // 環境マップをバインド
    if (!environmentTexturePath_.empty()) {
        dxCore_->GetCommandList()->SetGraphicsRootDescriptorTable(
            kRootEnvironmentMap, TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
        );
        // 法線マップ(t2)のフォールバックも貼っておく（個別オブジェクトで上書きされる。
        // PBR を使わないオブジェクトでも未バインドにならないようにするため）
        dxCore_->GetCommandList()->SetGraphicsRootDescriptorTable(
            kRootNormalMap, TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
        );
    }

    // シャドウ受光リソースをバインド（b5=ShadowConstants / t3=シャドウマップ）。
    // Framework が ShadowMap を配線済みなので影未使用シーンでも必ずバインドされる
    // （未バインドだと PS の b5/t3 参照で GPU ベース検証 #935 が落ちる）。
    BindShadow(dxCore_->GetCommandList());

    // 距離フォグ（b6 = rootParameter[11]）。全オブジェクト共通なのでここで1回だけ。
    BindFog(dxCore_->GetCommandList());

    // 遠景の雲（b7 / t5）
    BindCloud(dxCore_->GetCommandList());

    // 視差のハイトマップ（t4）のダミー。実物は ModelInstance が submesh ごとに貼る
    BindHeightMapFallback(dxCore_->GetCommandList());
}

void Object3DManager::BindHeightMapFallback(ID3D12GraphicsCommandList* commandList) const
{
    // 雲のダミーと同じ 2D テクスチャを流用する（PS は useParallax=0 のとき読まない）
    const D3D12_GPU_DESCRIPTOR_HANDLE fallback =
        TextureManager::GetInstance()->GetSrvHandleGPU(CloudLayer::GetFallbackTexturePath());
    commandList->SetGraphicsRootDescriptorTable(kRootHeightMap, fallback);
    // 地形の層（t6 / t7）も未バインドにしない。読むのは地形の PS だけで、地形の submesh は実物を貼り直す
    commandList->SetGraphicsRootDescriptorTable(kRootTerrainLayerColor, fallback);
    commandList->SetGraphicsRootDescriptorTable(kRootTerrainLayerNormal, fallback);
}

void Object3DManager::BindCloud(ID3D12GraphicsCommandList* commandList) const
{
    commandList->SetGraphicsRootConstantBufferView(kRootCloudParams,
        cloudLayer_ ? cloudLayer_->GetConstantBufferAddress() : disabledCloudResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(kRootCloudNoise,
        cloudLayer_ ? cloudLayer_->GetNoiseSrvHandle()
                    : TextureManager::GetInstance()->GetSrvHandleGPU(CloudLayer::GetFallbackTexturePath()));
}

void Object3DManager::SetBlendMode(BlendMode blendMode)
{
    // すでにそのモードなら何もしない
    if (blendMode_ == blendMode) {
        return;
    }

    blendMode_ = blendMode;
    currentBlendMode_ = static_cast<int>(blendMode);

}

Object3DManager::ShaderType Object3DManager::SelectShaderType(const Material* material, bool allowTerrain)
{
    if (!material) {
        return kShaderNoEnvironmentMap;
    }
    // 地形を使えないモデルで地形が指定されていたら、PBR にはせず環境マップの有無で選ぶ（今までと同じ）
    if (allowTerrain && material->shadingModel == ShadingModel::kTerrain) {
        return kShaderTerrain;
    }
    if (material->shadingModel == ShadingModel::kPBR) {
        return kShaderPBR;
    }
    if (material->useEnvironmentMap) {
        return kShaderEnvironmentMap;
    }
    return kShaderNoEnvironmentMap;
}

Object3DManager::~Object3DManager()
{
    // RootSignature / PSO の解放(ComPtr なので自動)
    rootSignature_.Reset();
    for (auto& pipelineStateArray : pipelineStates2D_) {
        for (auto& pipelineState : pipelineStateArray) {
            pipelineState.Reset();
        }
    }
}

void Object3DManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange 
    // PS: SRV(t0)
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = kRegTextureSrv;                      // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                          // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

    // PS: SRV(t1)
    D3D12_DESCRIPTOR_RANGE descriptorRangeEnvironment[1] = {};
    descriptorRangeEnvironment[0].BaseShaderRegister = kRegEnvironmentSrv;                      // 1を使用
    descriptorRangeEnvironment[0].NumDescriptors = 1;                          // 数は1つ
    descriptorRangeEnvironment[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
    descriptorRangeEnvironment[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

    // PS: SRV(t3) - シャドウマップ（CSM, Texture2DArray）
    D3D12_DESCRIPTOR_RANGE descriptorRangeShadow[1] = {};
    descriptorRangeShadow[0].BaseShaderRegister = kRegShadowMapSrv;                      // t3
    descriptorRangeShadow[0].NumDescriptors = 1;
    descriptorRangeShadow[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeShadow[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t2) - 法線マップ
    D3D12_DESCRIPTOR_RANGE descriptorRangeNormalMap[1] = {};
    descriptorRangeNormalMap[0].BaseShaderRegister = kRegNormalMapSrv;                      // t2
    descriptorRangeNormalMap[0].NumDescriptors = 1;
    descriptorRangeNormalMap[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeNormalMap[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t5) - 遠景の雲のノイズ（CloudSky.hlsli）
    D3D12_DESCRIPTOR_RANGE descriptorRangeCloud[1] = {};
    descriptorRangeCloud[0].BaseShaderRegister = kRegCloudNoiseSrv;                      // t5
    descriptorRangeCloud[0].NumDescriptors = 1;
    descriptorRangeCloud[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeCloud[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t4) - 視差オクルージョンのハイトマップ
    D3D12_DESCRIPTOR_RANGE descriptorRangeHeight[1] = {};
    descriptorRangeHeight[0].BaseShaderRegister = kRegHeightMapSrv;                      // t4
    descriptorRangeHeight[0].NumDescriptors = 1;
    descriptorRangeHeight[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeHeight[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t6 / t7) - 地形の層の色・法線の配列
    D3D12_DESCRIPTOR_RANGE descriptorRangeTerrainColor[1] = {};
    descriptorRangeTerrainColor[0].BaseShaderRegister = kRegTerrainColorSrv;                // t6
    descriptorRangeTerrainColor[0].NumDescriptors = 1;
    descriptorRangeTerrainColor[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTerrainColor[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE descriptorRangeTerrainNormal[1] = {};
    descriptorRangeTerrainNormal[0].BaseShaderRegister = kRegTerrainNormalSrv;               // t7
    descriptorRangeTerrainNormal[0].NumDescriptors = 1;
    descriptorRangeTerrainNormal[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTerrainNormal[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[kRootParamCount] = {};

    // PS: CBV(b0) - マテリアル用
    rootParameters[kRootMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[kRootMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[kRootMaterial].Descriptor.ShaderRegister = kRegMaterialCb;                     // レジスタ番号0

    // VS: CBV(b0) - トランスフォーム用
    rootParameters[kRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[kRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
    rootParameters[kRootTransform].Descriptor.ShaderRegister = kRegTransformCb;                     // レジスタ番号0

    // PS: DescriptorTable(t0) - テクスチャ用
    rootParameters[kRootTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使用
    rootParameters[kRootTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[kRootTexture].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
    rootParameters[kRootTexture].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

    // PS: CBV(b1) DirectionalLight用
    rootParameters[kRootDirectionalLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[kRootDirectionalLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[kRootDirectionalLight].Descriptor.ShaderRegister = kRegDirectionalLightCb;                     // レジスタ番号1を使う

    // rootParameters[4] にカメラ用のCBVを追加
    rootParameters[kRootCamera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootCamera].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootCamera].Descriptor.ShaderRegister = kRegCameraCb;  // b2

    // PS: CBV(b3) PointLight用
    rootParameters[kRootPointLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootPointLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootPointLight].Descriptor.ShaderRegister = kRegPointLightCb;  // b3

    // PS: CBV(b4) SpotLight用
    rootParameters[kRootSpotLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootSpotLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootSpotLight].Descriptor.ShaderRegister = kRegSpotLightCb;  // b4

    // ============================================
    // PS: DescriptorTable(t1) - Environment Map用
    // ============================================
    rootParameters[kRootEnvironmentMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootEnvironmentMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootEnvironmentMap].DescriptorTable.pDescriptorRanges = descriptorRangeEnvironment;
    rootParameters[kRootEnvironmentMap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeEnvironment);

    // ============================================
    // PS: CBV(b5) - シャドウ情報（ShadowConstants）
    // ============================================
    rootParameters[kRootShadowConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootShadowConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootShadowConstants].Descriptor.ShaderRegister = kRegShadowConstantsCb;  // b5

    // ============================================
    // PS: DescriptorTable(t3) - シャドウマップ（CSM）
    // ============================================
    rootParameters[kRootShadowMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootShadowMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootShadowMap].DescriptorTable.pDescriptorRanges = descriptorRangeShadow;
    rootParameters[kRootShadowMap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeShadow);

    // ============================================
    // PS: DescriptorTable(t2) - 法線マップ
    // ============================================
    rootParameters[kRootNormalMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootNormalMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootNormalMap].DescriptorTable.pDescriptorRanges = descriptorRangeNormalMap;
    rootParameters[kRootNormalMap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeNormalMap);

    // rootParameters[11] = 距離フォグ（b6）。既存インデックスを動かさないよう末尾に足す。
    rootParameters[kRootFog].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootFog].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootFog].Descriptor.ShaderRegister = kRegFogCb;  // b6

    // rootParameters[12] = 平面リフレクションの鏡側 CB（VS b1 = 鏡像 ViewProj ＋ クリップ平面）。
    // 反射用 VS だけが参照する。映る物側に CB を増やさず、鏡1枚につき CB 1個で済ませるため。
    rootParameters[kRootReflectionCamera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootReflectionCamera].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[kRootReflectionCamera].Descriptor.ShaderRegister = kRegReflectionCameraCb;  // b1

    // rootParameters[13] / [14] = 遠景の雲（CloudSky.hlsli の b7 / t5）。PBR の鏡面反射に雲を映す
    rootParameters[kRootCloudParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootCloudParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootCloudParams].Descriptor.ShaderRegister = kRegCloudParamsCb;  // b7
    rootParameters[kRootCloudNoise].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootCloudNoise].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootCloudNoise].DescriptorTable.pDescriptorRanges = descriptorRangeCloud;
    rootParameters[kRootCloudNoise].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeCloud);

    // rootParameters[15] = 視差オクルージョンのハイトマップ（t4）
    rootParameters[kRootHeightMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootHeightMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootHeightMap].DescriptorTable.pDescriptorRanges = descriptorRangeHeight;
    rootParameters[kRootHeightMap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeHeight);

    // rootParameters[16] / [17] = 地形の層の配列（t6 / t7）
    rootParameters[kRootTerrainLayerColor].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootTerrainLayerColor].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootTerrainLayerColor].DescriptorTable.pDescriptorRanges = descriptorRangeTerrainColor;
    rootParameters[kRootTerrainLayerColor].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeTerrainColor);
    rootParameters[kRootTerrainLayerNormal].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootTerrainLayerNormal].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootTerrainLayerNormal].DescriptorTable.pDescriptorRanges = descriptorRangeTerrainNormal;
    rootParameters[kRootTerrainLayerNormal].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeTerrainNormal);

    // ============================================
    // Sampler (PS の s0 = 通常テクスチャ, s1 = シャドウ比較, s2 = シャドウ生深度読み, s4 = 雲のノイズ, s5 = 地形の層)
    // ============================================
    D3D12_STATIC_SAMPLER_DESC staticSamplers[kStaticSamplerCount] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;         // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 0~1の範囲をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // Mipmapをあるだけ使用
    staticSamplers[0].ShaderRegister = kRegLinearSampler;                               // レジスタ番号0を使用
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用

    // シャドウ比較サンプラー（PCF）。深度比較で「手前にあるか」を返す
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // current<=stored で「照らされている」
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = kRegShadowCompareSampler;                               // s1
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // シャドウ生深度読みサンプラー（ブロッカー探索用）。ポイント＋クランプ
    staticSamplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[2].ShaderRegister = kRegShadowDepthSampler;                               // s2
    staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 雲のノイズ。Skybox / WaterSurface の s4 と同じ設定（CloudSky.hlsli を共用するため）
    staticSamplers[3].Filter = D3D12_FILTER_ANISOTROPIC;
    staticSamplers[3].MaxAnisotropy = kCloudNoiseMaxAnisotropy;
    staticSamplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[3].ShaderRegister = kRegCloudNoiseSampler;                               // s4
    staticSamplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 地形の層（Terrain.PS）。低空から地面を浅い角度で見るので異方性にする（線形だと遠くの地面がボケる）
    staticSamplers[4].Filter = D3D12_FILTER_ANISOTROPIC;
    staticSamplers[4].MaxAnisotropy = kTerrainMaxAnisotropy;
    staticSamplers[4].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[4].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[4].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[4].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[4].ShaderRegister = kRegTerrainSampler;                               // s5
    staticSamplers[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignaturDesc{};
    rootSignaturDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignaturDesc.pParameters = rootParameters;
    rootSignaturDesc.NumParameters = _countof(rootParameters);
    rootSignaturDesc.pStaticSamplers = staticSamplers;
    rootSignaturDesc.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリにする
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSignaturDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    // バイナリをもとに生成
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void Object3DManager::CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode, bool forReflection)
{
    // ShaderTypeに応じてPSファイルを切り替え
    const wchar_t* psFilePath = nullptr;
    switch (shaderType) {
    case kShaderEnvironmentMap:
        psFilePath = L"Resources/Shaders/Object3D/Object3d.PS.hlsl";
        break;
    case kShaderNoEnvironmentMap:
        psFilePath = L"Resources/Shaders/Object3D/Object3dNoEnv.PS.hlsl";
        break;
    case kShaderPBR:
        psFilePath = L"Resources/Shaders/Object3D/Object3dPBR.PS.hlsl";
        break;
    case kShaderTerrain:
        psFilePath = L"Resources/Shaders/Object3D/Terrain.PS.hlsl";
        break;
    default:
        assert(false);
    }

    // ===== シェーダーコンパイル =====
    IDxcBlob* vs = dxCore_->LoadShaderBlob(
        forReflection ? L"Resources/Shaders/Object3D/Object3dReflection.VS.hlsl"
                      : L"Resources/Shaders/Object3D/Object3d.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* ps = dxCore_->LoadShaderBlob(
        psFilePath,
        L"ps_6_0"
    );

    // 入力レイアウト設定
    D3D12_INPUT_ELEMENT_DESC inputElements[4] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[1].SemanticName = "TEXCOORD";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[2].SemanticName = "NORMAL";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[3].SemanticName = "TANGENT";
    inputElements[3].SemanticIndex = 0;
    inputElements[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    //=================================
    // Rasterizer、Blend、PSO 設定
    //=================================

    // Rasterizer - 3Dオブジェクトなのでバックフェースカリング
    D3D12_RASTERIZER_DESC rasterizer{};
    // 反射は鏡像行列（行列式が負）で三角形の巻きが反転するので、表面側を落とす
    rasterizer.CullMode = forReflection ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

    // BlendStateの設定
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // 透過部分(src.a=0)で destAlpha を保持し、ImGui Viewport 表示時に下のImGui背景が透けないようにする
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    switch (blendMode_)
    {
    case kBlendModeNone:
        blend.RenderTarget[0].BlendEnable = FALSE;
        break;

    case kBlendModeNormal:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;

    case kBlendModeAdd:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kBlendModeSubtract:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kBlendModeMultily:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        break;

    case kBlendModeScreen:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kCountOfBlendMode:
    default:
        break;
    }

    // DepthStencilStateの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込む
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // ---- PSO 設定 ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = depthStencilDesc;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    auto& target = forReflection ? reflectionPipelineStates_[shaderType]
                                 : pipelineStates2D_[shaderType][blendMode];
    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&target)
    ); assert(SUCCEEDED(hr));
}

void Object3DManager::CreateIdPassObjects()
{
    HRESULT hr;

    // ----- Root Signature -----
    // [0] VS CBV(b0) = TransformationMatrix
    // [1] PS RootConstant(b0) = uint id（＋ディゾルブ版は続けて 7 個。WriteIDDissolve.PS / Object3DInstance::DrawIdPass）
    D3D12_ROOT_PARAMETER rootParams[kIdRootParamCount] = {};
    rootParams[kIdRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kIdRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[kIdRootTransform].Descriptor.ShaderRegister = 0;

    rootParams[kIdRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kIdRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kIdRootConstants].Constants.ShaderRegister = 0;
    rootParams[kIdRootConstants].Constants.RegisterSpace = 0;
    rootParams[kIdRootConstants].Constants.Num32BitValues = kIdPassConstantCount;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr) && errBlob) Log(static_cast<char*>(errBlob->GetBufferPointer()));
    assert(SUCCEEDED(hr));
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&idRootSignature_));
    assert(SUCCEEDED(hr));

    // ----- PSO -----
    IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Object3D/Object3d.VS.hlsl", L"vs_6_0");
    IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Object3D/WriteID.PS.hlsl", L"ps_6_0");
    assert(vs && ps);

    // Object3d.VS が TANGENT0 を要求するため、ID パスの入力レイアウトにも含める
    D3D12_INPUT_ELEMENT_DESC elems[4] = {};
    elems[0].SemanticName = "POSITION";
    elems[0].SemanticIndex = 0;
    elems[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    elems[1].SemanticName = "TEXCOORD";
    elems[1].SemanticIndex = 0;
    elems[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    elems[2].SemanticName = "NORMAL";
    elems[2].SemanticIndex = 0;
    elems[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    elems[3].SemanticName = "TANGENT";
    elems[3].SemanticIndex = 0;
    elems[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = elems;
    inputLayout.NumElements = _countof(elems);

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = FALSE;

    // 深度テストあり、書き込みなし（メイン描画で既に書かれた depth に対して LessEqual）
    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = idRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = dsDesc;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8_UINT;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&idPipelineState_));
    assert(SUCCEEDED(hr));

    // ディゾルブ中の物用：消えている部分に ID を書かない（他は同じ設定）
    IDxcBlob* psDissolve = dxCore_->LoadShaderBlob(L"Resources/Shaders/Object3D/WriteIDDissolve.PS.hlsl", L"ps_6_0");
    assert(psDissolve);
    desc.PS = { psDissolve->GetBufferPointer(), psDissolve->GetBufferSize() };
    hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&idDissolvePipelineState_));
    assert(SUCCEEDED(hr));
}

void Object3DManager::CreateBloomPassObjects()
{
    HRESULT hr;

    // ----- Root Signature -----
    // [0] VS CBV(b0) = TransformationMatrix
    // [1] PS CBV(b0) = Material（発光とディゾルブを読む）
    // [2] PS RootConstant(b1) = 倍率
    // [3] PS DescriptorTable(t0) = ベースカラー（emissive.w = 1 のとき発光に掛ける）
    D3D12_DESCRIPTOR_RANGE textureRange[1] = {};
    textureRange[0].BaseShaderRegister = 0;
    textureRange[0].NumDescriptors = 1;
    textureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[kBloomRootParamCount] = {};
    rootParams[kBloomRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kBloomRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[kBloomRootTransform].Descriptor.ShaderRegister = 0;

    rootParams[kBloomRootMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kBloomRootMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kBloomRootMaterial].Descriptor.ShaderRegister = 0;

    rootParams[kBloomRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kBloomRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kBloomRootConstants].Constants.ShaderRegister = 1;
    rootParams[kBloomRootConstants].Constants.RegisterSpace = 0;
    rootParams[kBloomRootConstants].Constants.Num32BitValues = kBloomPassConstantCount;

    rootParams[kBloomRootBaseColor].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kBloomRootBaseColor].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kBloomRootBaseColor].DescriptorTable.pDescriptorRanges = textureRange;
    rootParams[kBloomRootBaseColor].DescriptorTable.NumDescriptorRanges = _countof(textureRange);

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr) && errBlob) Log(static_cast<char*>(errBlob->GetBufferPointer()));
    assert(SUCCEEDED(hr));
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&bloomRootSignature_));
    assert(SUCCEEDED(hr));

    // ----- PSO -----
    IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Object3D/Object3d.VS.hlsl", L"vs_6_0");
    IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Object3D/WriteEmissive.PS.hlsl", L"ps_6_0");
    assert(vs && ps);

    D3D12_INPUT_ELEMENT_DESC elems[4] = {};
    elems[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    elems[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    elems[2] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    elems[3] = { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = elems;
    inputLayout.NumElements = _countof(elems);

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    // 加算（エフェクトの発光と重なったら足し合わせる）
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // メイン描画で書いた深度に LessEqual（手前の物に隠れた所は光らない）
    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = bloomRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = dsDesc;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = BloomEffect::kFormat;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&bloomPipelineState_));
    assert(SUCCEEDED(hr));
}
