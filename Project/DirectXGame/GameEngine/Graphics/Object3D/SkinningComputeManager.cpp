#include "SkinningComputeManager.h"

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
    descriptorRangePalette[0].BaseShaderRegister = 0;
    descriptorRangePalette[0].NumDescriptors = 1;
    descriptorRangePalette[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangePalette[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: SRV(t1) InputVertices
    D3D12_DESCRIPTOR_RANGE descriptorRangeInputVertex[1] = {};
    descriptorRangeInputVertex[0].BaseShaderRegister = 1;
    descriptorRangeInputVertex[0].NumDescriptors = 1;
    descriptorRangeInputVertex[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInputVertex[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: SRV(t2) Influence
    D3D12_DESCRIPTOR_RANGE descriptorRangeInfluence[1] = {};
    descriptorRangeInfluence[0].BaseShaderRegister = 2;
    descriptorRangeInfluence[0].NumDescriptors = 1;
    descriptorRangeInfluence[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInfluence[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // CS: UAV(u0) OutputVertices
    D3D12_DESCRIPTOR_RANGE descriptorRangeOutput[1] = {};
    descriptorRangeOutput[0].BaseShaderRegister = 0;
    descriptorRangeOutput[0].NumDescriptors = 1;
    descriptorRangeOutput[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorRangeOutput[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter（CSなのでShaderVisibilityはALL）
    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    // [0] Palette
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRangePalette;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangePalette);

    // [1] InputVertex
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeInputVertex;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInputVertex);

    // [2] Influence
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeInfluence;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInfluence);

    // [3] OutputVertex (UAV)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRangeOutput;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeOutput);

    // [4] SkinningInformation (CBV b0)
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 0;

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
    IDxcBlob* cs = dxCore_->CompileShader(
        L"Resources/Shaders/Skinning.CS.hlsl",
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
