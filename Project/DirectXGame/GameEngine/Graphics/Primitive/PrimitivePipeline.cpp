#include "PrimitivePipeline.h"
#include "Log.h"
#include <cassert>

PrimitivePipeline* PrimitivePipeline::GetInstance() {
    static PrimitivePipeline instance;
    return &instance;
}

void PrimitivePipeline::Initialize(DirectXCore* dxCore, SRVManager* srvManager) {
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    CreateRootSignature();

    // BlendMode × DepthWrite × CullBackface の3軸でPSOを作成
    for (int i = 0; i < kCountOfBlendMode; ++i) {
        BlendMode bm = static_cast<BlendMode>(i);
        CreateGraphicsPipelineState(bm, false, false);
        CreateGraphicsPipelineState(bm, false, true);
        CreateGraphicsPipelineState(bm, true,  false);
        CreateGraphicsPipelineState(bm, true,  true);
    }
}

void PrimitivePipeline::Finalize() {
    for (auto& dim2 : pipelineStates_) {
        for (auto& dim1 : dim2) {
            for (auto& pso : dim1) {
                pso.Reset();
            }
        }
    }
}

void PrimitivePipeline::PreDraw(BlendMode blendMode, bool depthWrite, bool cullBackface) {
    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();

    int depthIndex = depthWrite   ? 1 : 0;
    int cullIndex  = cullBackface ? 1 : 0;

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineStates_[blendMode][depthIndex][cullIndex].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void PrimitivePipeline::CreateRootSignature() {
    HRESULT hr;

    // DescriptorRange: SRV(t0) - テクスチャ用（PS）
    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = {};
    descriptorRangeTexture[0].BaseShaderRegister = 0;
    descriptorRangeTexture[0].NumDescriptors = 1;
    descriptorRangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // [0] VS: CBV(b0) - TransformationMatrix
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // [1] PS: CBV(b0) - Material
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // [2] PS: DescriptorTable - Texture(t0)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeTexture;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    // Sampler 3種類（material.samplerMode で PS 側が選択）
    //   s0: WRAP U  / WRAP V  （タイリング用）
    //   s1: WRAP U  / CLAMP V （Ring/Cylinder 用。中心側に白いリング型のアーティファクトが出るのを防ぐ）
    //   s2: CLAMP U / CLAMP V （非繰り返し）
    D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
    for (int i = 0; i < 3; ++i) {
        staticSamplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSamplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        staticSamplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        staticSamplers[i].ShaderRegister = i;
        staticSamplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void PrimitivePipeline::CreateGraphicsPipelineState(BlendMode blendMode, bool depthWrite, bool cullBackface) {
    // シェーダーコンパイル
    IDxcBlob* vs = dxCore_->CompileShader(
        L"Resources/Shaders/Primitive/Primitive.VS.hlsl",
        L"vs_6_0"
    );
    IDxcBlob* ps = dxCore_->CompileShader(
        L"Resources/Shaders/Primitive/Primitive.PS.hlsl",
        L"ps_6_0"
    );

    // InputLayout: Position(float3) + Texcoord(float2) + Normal(float3) + Color(float4)
    D3D12_INPUT_ELEMENT_DESC inputElements[4] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[1].SemanticName = "TEXCOORD";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[2].SemanticName = "NORMAL";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[3].SemanticName = "COLOR";
    inputElements[3].SemanticIndex = 0;
    inputElements[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    // Rasterizer（cullBackface でカリング切替）
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = cullBackface ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    // BlendState
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // 透過部分(src.a=0)で destAlpha を保持し、ImGui Viewport 表示時に下のImGui背景が透けないようにする
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    switch (blendMode) {
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
    case kBlendModeMultiply:
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

    // DepthStencil（depthWrite引数で書き込み有無を切り替え）
    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = true;
    if (depthWrite) {
        depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    } else{
        depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    }
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

    int depthIndex = depthWrite   ? 1 : 0;
    int cullIndex  = cullBackface ? 1 : 0;
    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(&pipelineStates_[blendMode][depthIndex][cullIndex]));
    assert(SUCCEEDED(hr));
}