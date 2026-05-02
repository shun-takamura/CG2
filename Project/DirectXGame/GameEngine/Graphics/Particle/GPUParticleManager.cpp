#include "GPUParticleManager.h"
#include "Camera.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include "Material.h"
#include "VertexData.h"
#include "Log.h"
#include <cassert>

void GPUParticleManager::Initialize(DirectXCore* dxCore, SRVManager* srvManager, const std::string& textureFilePath)
{
    dxCore_ = dxCore;
    srvManager_ = srvManager;
    textureFilePath_ = textureFilePath;

    // テクスチャ読み込み
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);

    CreateParticleResource();
    CreateInitializePipeline();
    CreateDrawPipeline();
    CreateVertexData();
    CreatePerView();
    CreateMaterial();
}

void GPUParticleManager::Finalize()
{
    if (perViewResource_) {
        perViewResource_->Unmap(0, nullptr);
        perViewData_ = nullptr;
    }
    perViewResource_.Reset();
    materialResource_.Reset();
    vertexResource_.Reset();
    drawPSO_.Reset();
    drawRootSig_.Reset();
    initPSO_.Reset();
    initRootSig_.Reset();
    particleResource_.Reset();
}

void GPUParticleManager::Update(const Camera* camera)
{
    if (!camera || !perViewData_) {
        return;
    }

    // ViewProjection
    perViewData_->viewProjection = camera->GetViewProjectionMatrix();

    // billboardMatrix（ViewMatrixの回転部だけを抜き出してinverse=転置）
    const Matrix4x4& view = camera->GetViewMatrix();
    Matrix4x4 billboard = MakeIdentity4x4();
    billboard.m[0][0] = view.m[0][0];
    billboard.m[0][1] = view.m[1][0];
    billboard.m[0][2] = view.m[2][0];
    billboard.m[1][0] = view.m[0][1];
    billboard.m[1][1] = view.m[1][1];
    billboard.m[1][2] = view.m[2][1];
    billboard.m[2][0] = view.m[0][2];
    billboard.m[2][1] = view.m[1][2];
    billboard.m[2][2] = view.m[2][2];
    perViewData_->billboardMatrix = billboard;
}

void GPUParticleManager::Draw()
{
    auto commandList = dxCore_->GetCommandList();

    // 初回のみCSによる初期化を実行
    if (!initializedOnGPU_) {
        DispatchInitializeCS();
        initializedOnGPU_ = true;
    }

    // 描画
    commandList->SetGraphicsRootSignature(drawRootSig_.Get());
    commandList->SetPipelineState(drawPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // [0] VS: Particle StructuredBuffer (t0)
    srvManager_->SetGraphicsRootDescriptorTable(0, srvIndex_);
    // [1] VS: PerView CBV (b0)
    commandList->SetGraphicsRootConstantBufferView(1, perViewResource_->GetGPUVirtualAddress());
    // [2] PS: Material CBV (b0)
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    // [3] PS: Texture (t0)
    srvManager_->SetGraphicsRootDescriptorTable(3, textureSrvIndex_);

    commandList->DrawInstanced(6, kMaxParticles, 0, 0);
}

void GPUParticleManager::DispatchInitializeCS()
{
    auto commandList = dxCore_->GetCommandList();

    // Resource state: COMMON -> UNORDERED_ACCESS
    D3D12_RESOURCE_BARRIER toUav{};
    toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toUav.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toUav.Transition.pResource = particleResource_.Get();
    toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toUav);

    // CSのRootSignature/PSOを設定してDispatch
    commandList->SetComputeRootSignature(initRootSig_.Get());
    commandList->SetPipelineState(initPSO_.Get());

    // UAVをセット
    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(uavIndex_));

    // 1024 / numthreads(1024) = 1
    commandList->Dispatch(1, 1, 1);

    // Resource state: UNORDERED_ACCESS -> NON_PIXEL_SHADER_RESOURCE (VSで読むため)
    D3D12_RESOURCE_BARRIER toSrv{};
    toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toSrv.Transition.pResource = particleResource_.Get();
    toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toSrv);
}

//==========================================================
// 内部ヘルパー
//==========================================================

void GPUParticleManager::CreateParticleResource()
{
    // DEFAULT heapに1024個分のParticleResourceを作成
    particleResource_ = dxCore_->CreateUavBufferResource(sizeof(ParticleCS) * kMaxParticles);

    // UAVとSRVを別スロットに作成
    uavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(uavIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));

    srvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVForStructuredBuffer(srvIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
}

void GPUParticleManager::CreateInitializePipeline()
{
    HRESULT hr;

    // RootSignature: u0 のDescriptorTable
    D3D12_DESCRIPTOR_RANGE rangeUav[1] = {};
    rangeUav[0].BaseShaderRegister = 0;
    rangeUav[0].NumDescriptors = 1;
    rangeUav[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeUav[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeUav;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCore_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&initRootSig_));
    assert(SUCCEEDED(hr));

    // CSコンパイル
    IDxcBlob* cs = dxCore_->CompileShader(
        L"Resources/Shaders/InitializeParticle.CS.hlsl",
        L"cs_6_0"
    );

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    psoDesc.pRootSignature = initRootSig_.Get();
    hr = dxCore_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&initPSO_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::CreateDrawPipeline()
{
    HRESULT hr;

    // VS用 t0 (Particle SRV)
    D3D12_DESCRIPTOR_RANGE rangeParticleSrv[1] = {};
    rangeParticleSrv[0].BaseShaderRegister = 0;
    rangeParticleSrv[0].NumDescriptors = 1;
    rangeParticleSrv[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeParticleSrv[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // PS用 t0 (Texture)
    D3D12_DESCRIPTOR_RANGE rangeTexture[1] = {};
    rangeTexture[0].BaseShaderRegister = 0;
    rangeTexture[0].NumDescriptors = 1;
    rangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    // [0] VS: DescTable t0 (Particle StructuredBuffer)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeParticleSrv;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    // [1] VS: CBV b0 (PerView)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // [2] PS: CBV b0 (Material)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].Descriptor.ShaderRegister = 0;

    // [3] PS: DescTable t0 (Texture)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].DescriptorTable.pDescriptorRanges = rangeTexture;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;

    // [4] PS: CBV b1 (DirectionalLight - 既存Particle.PS.hlslの宣言に合わせるだけで未使用)
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].Descriptor.ShaderRegister = 1;

    // Static Sampler
    D3D12_STATIC_SAMPLER_DESC sampler[1] = {};
    sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[0].ShaderRegister = 0;
    sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);
    desc.pStaticSamplers = sampler;
    desc.NumStaticSamplers = _countof(sampler);

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCore_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&drawRootSig_));
    assert(SUCCEEDED(hr));

    // VS/PSコンパイル
    IDxcBlob* vs = dxCore_->CompileShader(L"Resources/Shaders/GPUParticle.VS.hlsl", L"vs_6_0");
    IDxcBlob* ps = dxCore_->CompileShader(L"Resources/Shaders/Particle.PS.hlsl", L"ps_6_0");

    // InputLayout（板ポリ用）
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

    // BlendState（加算）
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // DepthStencil（深度書き込みなし）
    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = true;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = drawRootSig_.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = inputLayout;
    psoDesc.BlendState = blend;
    psoDesc.RasterizerState = rasterizer;
    psoDesc.DepthStencilState = depth;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&drawPSO_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::CreateVertexData()
{
    // 板ポリ6頂点
    vertexResource_ = dxCore_->CreateBufferResource(sizeof(VertexData) * 6);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    vertexData[0].position = { -0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 0.0f };
    vertexData[0].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[1].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[1].texcoord = { 1.0f, 0.0f };
    vertexData[1].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[2].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[2].texcoord = { 0.0f, 1.0f };
    vertexData[2].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[3].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[3].texcoord = { 0.0f, 1.0f };
    vertexData[3].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[4].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[4].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[5].position = { 0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[5].texcoord = { 1.0f, 1.0f };
    vertexData[5].normal = { 0.0f, 0.0f, 1.0f };

    vertexResource_->Unmap(0, nullptr);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void GPUParticleManager::CreatePerView()
{
    perViewResource_ = dxCore_->CreateBufferResource(sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&perViewData_));
    perViewData_->viewProjection = MakeIdentity4x4();
    perViewData_->billboardMatrix = MakeIdentity4x4();
}

void GPUParticleManager::CreateMaterial()
{
    materialResource_ = dxCore_->CreateBufferResource(sizeof(Material));

    Material* materialData = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = false;
    materialData->uvTransform = MakeIdentity4x4();
    materialData->shininess = 0.0f;
    materialData->environmentCoefficient = 0.0f;
    materialData->useEnvironmentMap = false;
    materialResource_->Unmap(0, nullptr);
}
