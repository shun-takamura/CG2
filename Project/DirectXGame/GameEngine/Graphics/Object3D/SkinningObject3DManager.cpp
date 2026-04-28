#include "SkinningObject3DManager.h"

void SkinningObject3DManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateRootSignature();

    for (int st = 0; st < kCountOfShaderType; ++st) {
        for (int bm = 0; bm < kCountOfBlendMode; ++bm) {
            CreateGraphicsPipelineState(
                static_cast<ShaderType>(st),
                static_cast<BlendMode>(bm)
            );
        }
    }

    blendMode_ = kBlendModeNormal;
    currentBlendMode_ = static_cast<int>(blendMode_);
}

void SkinningObject3DManager::DrawSetting()
{
    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());  // ★これ重要

    dxCore_->GetCommandList()->SetPipelineState(
        pipelineStates_[kShaderEnvironmentMap][currentBlendMode_].Get()
    );

    if (!environmentTexturePath_.empty()) {
        dxCore_->GetCommandList()->SetGraphicsRootDescriptorTable(
            7, TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
        );
    }
}

void SkinningObject3DManager::SetBlendMode(BlendMode blendMode)
{
    if (blendMode_ == blendMode) {
        return;
    }
    blendMode_ = blendMode;
    currentBlendMode_ = static_cast<int>(blendMode);
}

SkinningObject3DManager::~SkinningObject3DManager()
{
    rootSignature_.Reset();
    for (auto& arr : pipelineStates_) {
        for (auto& pso : arr) {
            pso.Reset();
        }
    }
}

void SkinningObject3DManager::CreateRootSignature()
{
    HRESULT hr;

    // PS: SRV(t0) - テクスチャ用
    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = {};
    descriptorRangeTexture[0].BaseShaderRegister = 0;
    descriptorRangeTexture[0].NumDescriptors = 1;
    descriptorRangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS: SRV(t1) - 環境マップ用
    D3D12_DESCRIPTOR_RANGE descriptorRangeEnvironment[1] = {};
    descriptorRangeEnvironment[0].BaseShaderRegister = 1;
    descriptorRangeEnvironment[0].NumDescriptors = 1;
    descriptorRangeEnvironment[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeEnvironment[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // VS: SRV(t0) - MatrixPalette用
    D3D12_DESCRIPTOR_RANGE descriptorRangePalette[1] = {};
    descriptorRangePalette[0].BaseShaderRegister = 0;
    descriptorRangePalette[0].NumDescriptors = 1;
    descriptorRangePalette[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangePalette[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // 9個のRootParameter
    D3D12_ROOT_PARAMETER rootParameters[9] = {};

    // [0] PS: CBV(b0) Material
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // [1] VS: CBV(b0) TransformationMatrix
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // [2] PS: DescTable(t0) Texture
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeTexture;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeTexture);

    // [3] PS: CBV(b1) DirectionalLight
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    // [4] PS: CBV(b2) Camera
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].Descriptor.ShaderRegister = 2;

    // [5] PS: CBV(b3) PointLight
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[5].Descriptor.ShaderRegister = 3;

    // [6] PS: CBV(b4) SpotLight
    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[6].Descriptor.ShaderRegister = 4;

    // [7] PS: DescTable(t1) Environment Map ここをPS用に
    rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[7].DescriptorTable.pDescriptorRanges = descriptorRangeEnvironment;
    rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeEnvironment);

    // [8] VS: DescTable(t0) MatrixPalette Skinning用は最後
    rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[8].DescriptorTable.pDescriptorRanges = descriptorRangePalette;
    rootParameters[8].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangePalette);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignaturDesc{};
    rootSignaturDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignaturDesc.pParameters = rootParameters;
    rootSignaturDesc.NumParameters = _countof(rootParameters);
    rootSignaturDesc.pStaticSamplers = staticSamplers;
    rootSignaturDesc.NumStaticSamplers = _countof(staticSamplers);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootSignaturDesc,
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

void SkinningObject3DManager::CreateGraphicsPipelineState(ShaderType shaderType, BlendMode blendMode)
{
    const wchar_t* psFilePath = nullptr;
    switch (shaderType) {
    case kShaderEnvironmentMap:
        psFilePath = L"Resources/Shaders/SkinningObject3d.PS.hlsl";
        break;
    case kShaderNoEnvironmentMap:
        psFilePath = L"Resources/Shaders/SkinningObject3dNoEnv.PS.hlsl";
        break;
    default:
        assert(false);
    }

    // Skinning用VSを使用
    IDxcBlob* vs = dxCore_->CompileShader(
        L"Resources/Shaders/SkinningObject3d.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* ps = dxCore_->CompileShader(psFilePath, L"ps_6_0");

    // InputLayout: 5要素（POSITION, TEXCOORD, NORMAL, WEIGHT, INDEX）
    D3D12_INPUT_ELEMENT_DESC inputElements[5] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[0].InputSlot = 0;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[1].SemanticName = "TEXCOORD";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElements[1].InputSlot = 0;
    inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[2].SemanticName = "NORMAL";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[2].InputSlot = 0;
    inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    // Slot1: VertexInfluence
    inputElements[3].SemanticName = "WEIGHT";
    inputElements[3].SemanticIndex = 0;
    inputElements[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[3].InputSlot = 1;  // Slot 1
    inputElements[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[4].SemanticName = "INDEX";
    inputElements[4].SemanticIndex = 0;
    inputElements[4].Format = DXGI_FORMAT_R32G32B32A32_SINT;
    inputElements[4].InputSlot = 1;  // Slot 1
    inputElements[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

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

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&pipelineStates_[shaderType][blendMode])
    );
    assert(SUCCEEDED(hr));
}