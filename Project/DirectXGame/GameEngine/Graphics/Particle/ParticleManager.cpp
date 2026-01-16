#include "ParticleManager.h"
#include "Log.h"
#include <cassert>

void ParticleManager::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    CreateRootSignature();

    // 全ブレンドモード分のPSOを作成
    for (int i = 0; i < kCountOfBlendMode; ++i) {
        CreateGraphicsPipelineState(static_cast<BlendMode>(i));
    }
}

void ParticleManager::DrawSetting()
{
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());
    dxCore_->GetCommandList()->SetPipelineState(pipelineStates_[blendMode_].Get());
    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ParticleManager::SetBlendMode(BlendMode blendMode)
{
    blendMode_ = blendMode;
}

ParticleManager::~ParticleManager()
{
    rootSignature_.Reset();
    for (auto& pso : pipelineStates_) {
        pso.Reset();
    }
}

void ParticleManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange: SRV(t0) - インスタンシング用StructuredBuffer（VS）
    D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing[1] = {};
    descriptorRangeInstancing[0].BaseShaderRegister = 0;  // t0
    descriptorRangeInstancing[0].NumDescriptors = 1;
    descriptorRangeInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // DescriptorRange: SRV(t0) - テクスチャ用（PS）
    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = {};
    descriptorRangeTexture[0].BaseShaderRegister = 0;  // t0
    descriptorRangeTexture[0].NumDescriptors = 1;
    descriptorRangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};

    // [0] VS: DescriptorTable - インスタンシングデータ(t0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRangeInstancing;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    // [1] PS: CBV(b0) - マテリアル
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // [2] PS: DescriptorTable - テクスチャ(t0)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeTexture;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    // [3] PS: CBV(b1) - ライト
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    // Sampler
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズ
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    // 生成
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void ParticleManager::CreateGraphicsPipelineState(BlendMode blendMode)
{
    // シェーダーコンパイル
    IDxcBlob* vs = dxCore_->CompileShader(
        L"Resources/Shaders/Particle.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* ps = dxCore_->CompileShader(
        L"Resources/Shaders/Particle.PS.hlsl",
        L"ps_6_0"
    );

    // InputLayout（頂点データ）
    D3D12_INPUT_ELEMENT_DESC inputElements[3] = {};
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

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    // Rasterizer
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    // BlendState
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    switch (blendMode)
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

    default:
        break;
    }

    // DepthStencil（パーティクルは深度書き込みしない）
    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = true;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;  // 書き込まない
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = depthStencil;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(&pipelineStates_[blendMode]));
    assert(SUCCEEDED(hr));
}