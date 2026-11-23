#include "SkyboxManager.h"
namespace {
    // シェーダーのレジスタ番号
    constexpr UINT kCubemapRegister = 0;          // t0：今のスロットの Cubemap
    constexpr UINT kNextCubemapRegister = 1;      // t1：次のスロットの Cubemap
    constexpr UINT kCloudNoiseRegister = 5;       // t5：遠景の雲のノイズ
    constexpr UINT kCloudMokoRegister = 9;        // t9：遠景の雲のもこもこの高さ
    constexpr UINT kTransformRegister = 0;        // VS b0：座標変換行列
    constexpr UINT kMaterialRegister = 0;         // PS b0：マテリアル
    constexpr UINT kCloudParamsRegister = 7;      // PS b7：遠景の雲のパラメータ（水面と共有）
    constexpr UINT kCubemapSamplerRegister = 0;   // s0：Cubemap 用の linear clamp
    constexpr UINT kCloudSamplerRegister = 4;     // s4：雲のノイズ用の異方性 wrap
    // 静的サンプラーの数（s0 と s4）
    constexpr int kStaticSamplerCount = 2;
    // 雲のノイズのサンプラーの異方性の最大値
    constexpr UINT kCloudSamplerMaxAnisotropy = 8;
}

void SkyboxManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateRootSignature();
    CreateGraphicsPipelineState();
}

void SkyboxManager::DrawSetting()
{
    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());
    dxCore_->GetCommandList()->SetPipelineState(pipelineState_.Get());
}

SkyboxManager::~SkyboxManager()
{
    rootSignature_.Reset();
    pipelineState_.Reset();
}

void SkyboxManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange
    // PS: SRV(t0) - 現在スロットのTextureCube用
    D3D12_DESCRIPTOR_RANGE descriptorRange0[1] = {};
    descriptorRange0[0].BaseShaderRegister = kCubemapRegister;                      // t0
    descriptorRange0[0].NumDescriptors = 1;
    descriptorRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t1) - 次スロットのTextureCube用（クロスフェード用）
    D3D12_DESCRIPTOR_RANGE descriptorRange1[1] = {};
    descriptorRange1[0].BaseShaderRegister = kNextCubemapRegister;                      // t1
    descriptorRange1[0].NumDescriptors = 1;
    descriptorRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t5) - 遠景の雲のノイズ（CloudLayer。雲を使わないシーンはダミー）
    D3D12_DESCRIPTOR_RANGE descriptorRangeCloud[1] = {};
    descriptorRangeCloud[0].BaseShaderRegister = kCloudNoiseRegister;                  // t5
    descriptorRangeCloud[0].NumDescriptors = 1;
    descriptorRangeCloud[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeCloud[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t9) - 遠景の雲のもこもこの高さ（CloudLayer。雲を使わないシーンはダミー）
    D3D12_DESCRIPTOR_RANGE descriptorRangeCloudMoko[1] = {};
    descriptorRangeCloudMoko[0].BaseShaderRegister = kCloudMokoRegister;              // t9
    descriptorRangeCloudMoko[0].NumDescriptors = 1;
    descriptorRangeCloudMoko[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeCloudMoko[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[kRootParamCount] = {};

    // VS: CBV(b0) - TransformationMatrix用
    rootParameters[kRootParamTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootParamTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[kRootParamTransform].Descriptor.ShaderRegister = kTransformRegister;

    // PS: CBV(b0) - Material用
    rootParameters[kRootParamMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootParamMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamMaterial].Descriptor.ShaderRegister = kMaterialRegister;

    // PS: DescriptorTable(t0) - 現在スロットのTextureCube用
    rootParameters[kRootParamCubemap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootParamCubemap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamCubemap].DescriptorTable.pDescriptorRanges = descriptorRange0;
    rootParameters[kRootParamCubemap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange0);

    // PS: DescriptorTable(t1) - 次スロットのTextureCube用
    rootParameters[kRootParamNextCubemap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootParamNextCubemap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamNextCubemap].DescriptorTable.pDescriptorRanges = descriptorRange1;
    rootParameters[kRootParamNextCubemap].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange1);

    // PS: CBV(b7) - 遠景の雲のパラメータ（CloudSky.hlsli。水面と同じ CB を共有する）
    rootParameters[kRootParamCloud].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootParamCloud].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamCloud].Descriptor.ShaderRegister = kCloudParamsRegister;

    // PS: DescriptorTable(t5) - 遠景の雲のノイズ
    rootParameters[kRootParamCloudNoise].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootParamCloudNoise].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamCloudNoise].DescriptorTable.pDescriptorRanges = descriptorRangeCloud;
    rootParameters[kRootParamCloudNoise].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeCloud);

    // PS: DescriptorTable(t9) - 遠景の雲のもこもこ
    rootParameters[kRootParamCloudMoko].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootParamCloudMoko].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootParamCloudMoko].DescriptorTable.pDescriptorRanges = descriptorRangeCloudMoko;
    rootParameters[kRootParamCloudMoko].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeCloudMoko);

    // ============================================
    // Sampler (PS の s0)
    // ============================================
    D3D12_STATIC_SAMPLER_DESC staticSamplers[kStaticSamplerCount] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;      // Cubemap用にCLAMP推奨
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = kCubemapSamplerRegister;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s4: 雲のノイズ。タイルするので WRAP、水平線で UV が引き伸ばされるので異方性
    staticSamplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
    staticSamplers[1].MaxAnisotropy = kCloudSamplerMaxAnisotropy;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = kCloudSamplerRegister;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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

void SkyboxManager::CreateGraphicsPipelineState()
{
    // ===== シェーダーコンパイル =====
    IDxcBlob* vs = dxCore_->LoadShaderBlob(
        L"Resources/Shaders/Skybox/Skybox.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* ps = dxCore_->LoadShaderBlob(
        L"Resources/Shaders/Skybox/Skybox.PS.hlsl",
        L"ps_6_0"
    );

    // 入力レイアウト設定（positionのみ）
    D3D12_INPUT_ELEMENT_DESC inputElements[1] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    //=================================
    // Rasterizer、Blend、PSO 設定
    //=================================

    // Rasterizer - 内側から見るのでFRONTをカリング
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_FRONT;  // ← 内側から見るためFRONTカリング
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    // BlendStateの設定（ブレンド無効）
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = FALSE;

    // DepthStencilStateの設定（Skybox特有の設定）
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;                                    // 比較はする
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;          // 書き込まない
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;          // =も許可

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

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}