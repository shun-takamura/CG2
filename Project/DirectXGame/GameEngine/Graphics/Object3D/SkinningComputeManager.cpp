#include "SkinningComputeManager.h"

namespace {
    // スキニング CS のレジスタ（t0 パレット / t1 入力頂点 / t2 影響度 / u0 出力頂点 / b0 SkinningInformation）
    constexpr UINT kRegPaletteSrv = 0;
    constexpr UINT kRegInputVertexSrv = 1;
    constexpr UINT kRegInfluenceSrv = 2;
    constexpr UINT kRegOutputVertexUav = 0;
    constexpr UINT kRegSkinningInfoCb = 0;
}

void SkinningComputeManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateRootSignature();
    CreateComputePipelineState();
}

SkinningComputeManager::~SkinningComputeManager()
{
    rootSignature_.Reset();
    pipelineState_.Reset();
}

void SkinningComputeManager::DispatchSetting()
{
    dxCore_->GetCommandList()->SetComputeRootSignature(rootSignature_.Get());
    dxCore_->GetCommandList()->SetPipelineState(pipelineState_.Get());
}

void SkinningComputeManager::CreateRootSignature()
{
    HRESULT hr;

    // CS: SRV(t0) MatrixPalette
    D3D12_DESCRIPTOR_RANGE descriptorRangePalette[1] = {};
    descriptorRangePalette[0].BaseShaderRegister = kRegPaletteSrv;
    descriptorRangePalette[0].NumDescriptors = 1;
    descriptorRangePalette[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangePalette[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: SRV(t1) InputVertices
    D3D12_DESCRIPTOR_RANGE descriptorRangeInputVertex[1] = {};
    descriptorRangeInputVertex[0].BaseShaderRegister = kRegInputVertexSrv;
    descriptorRangeInputVertex[0].NumDescriptors = 1;
    descriptorRangeInputVertex[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInputVertex[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: SRV(t2) Influence
    D3D12_DESCRIPTOR_RANGE descriptorRangeInfluence[1] = {};
    descriptorRangeInfluence[0].BaseShaderRegister = kRegInfluenceSrv;
    descriptorRangeInfluence[0].NumDescriptors = 1;
    descriptorRangeInfluence[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInfluence[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: UAV(u0) OutputVertices
    D3D12_DESCRIPTOR_RANGE descriptorRangeOutput[1] = {};
    descriptorRangeOutput[0].BaseShaderRegister = kRegOutputVertexUav;
    descriptorRangeOutput[0].NumDescriptors = 1;
    descriptorRangeOutput[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorRangeOutput[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter（CSなのでShaderVisibilityはALL）
    D3D12_ROOT_PARAMETER rootParameters[kRootCount] = {};

    // [0] Palette
    rootParameters[kRootPalette].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootPalette].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[kRootPalette].DescriptorTable.pDescriptorRanges = descriptorRangePalette;
    rootParameters[kRootPalette].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangePalette);

    // [1] InputVertex
    rootParameters[kRootInputVertex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootInputVertex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[kRootInputVertex].DescriptorTable.pDescriptorRanges = descriptorRangeInputVertex;
    rootParameters[kRootInputVertex].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInputVertex);

    // [2] Influence
    rootParameters[kRootInfluence].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootInfluence].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[kRootInfluence].DescriptorTable.pDescriptorRanges = descriptorRangeInfluence;
    rootParameters[kRootInfluence].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInfluence);

    // [3] OutputVertex (UAV)
    rootParameters[kRootOutputVertex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootOutputVertex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[kRootOutputVertex].DescriptorTable.pDescriptorRanges = descriptorRangeOutput;
    rootParameters[kRootOutputVertex].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeOutput);

    // [4] SkinningInformation (CBV b0)
    rootParameters[kRootSkinningInfo].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootSkinningInfo].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[kRootSkinningInfo].Descriptor.ShaderRegister = kRegSkinningInfoCb;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    // ComputeなのでInputLayoutフラグは不要
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);

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

void SkinningComputeManager::CreateComputePipelineState()
{
    // CSコンパイル
    IDxcBlob* cs = dxCore_->LoadShaderBlob(
        L"Resources/Shaders/Skinning/Skinning.CS.hlsl",
        L"cs_6_0"
    );

    // ComputePipelineStateはGraphicsよりだいぶ簡素
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    desc.pRootSignature = rootSignature_.Get();

    HRESULT hr = dxCore_->GetDevice()->CreateComputePipelineState(
        &desc,
        IID_PPV_ARGS(&pipelineState_)
    );
    assert(SUCCEEDED(hr));
}
