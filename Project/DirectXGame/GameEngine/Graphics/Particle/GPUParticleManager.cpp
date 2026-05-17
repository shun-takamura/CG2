#include "GPUParticleManager.h"
#include "Camera.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include "Material.h"
#include "VertexData.h"
#include "Log.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <cassert>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GPUParticleManager::Initialize(DirectXCore* dxCore, SRVManager* srvManager, const std::string& textureFilePath)
{
    dxCore_ = dxCore;
    srvManager_ = srvManager;
    textureFilePath_ = textureFilePath;

    // テクスチャ読み込み
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);

    CreateParticleResource();
    CreateFreeListResources();
    CreateInitializePipeline();
    CreateEmitPipeline();
    CreateUpdatePipeline();
    CreateDrawPipeline();
    CreateVertexData();
    CreatePerView();
    CreatePerFrame();
    CreateEmitter();
    CreateMaterial();
}

void GPUParticleManager::Finalize()
{
    if (perViewResource_) {
        perViewResource_->Unmap(0, nullptr);
        perViewData_ = nullptr;
    }
    if (perFrameResource_) {
        perFrameResource_->Unmap(0, nullptr);
        perFrameData_ = nullptr;
    }
    if (emitterResource_) {
        emitterResource_->Unmap(0, nullptr);
        emitterData_ = nullptr;
    }
    perViewResource_.Reset();
    perFrameResource_.Reset();
    emitterResource_.Reset();
    materialResource_.Reset();
    vertexResource_.Reset();
    drawPSO_.Reset();
    drawRootSig_.Reset();
    updatePSO_.Reset();
    updateRootSig_.Reset();
    emitPSO_.Reset();
    emitRootSig_.Reset();
    initPSO_.Reset();
    initRootSig_.Reset();
    freeListResource_.Reset();
    freeListIndexResource_.Reset();
    particleResource_.Reset();
}

void GPUParticleManager::SetEmitterTranslate(const Vector3& translate)
{
    if (emitterData_) {
        emitterData_->translate = translate;
    }
}

void GPUParticleManager::OnImGui()
{
#ifdef USE_IMGUI
    ImGui::Text("Max Particles: %u", kMaxParticles);
    ImGui::Text("Elapsed Time: %.2f s", elapsedTime_);

    {
        const char* billboardItems[] = { "None", "Full", "YAxis" };
        int bIdx = static_cast<int>(billboardMode_);
        if (ImGui::Combo("Billboard", &bIdx, billboardItems, IM_ARRAYSIZE(billboardItems))) {
            billboardMode_ = static_cast<BillboardMode>(bIdx);
        }
    }
    {
        const char* timeGroupItems[] = { "World", "Player", "UI" };
        int tg = static_cast<int>(timeGroup_);
        if (ImGui::Combo("Time Group", &tg, timeGroupItems, IM_ARRAYSIZE(timeGroupItems))) {
            timeGroup_ = static_cast<TimeGroup>(tg);
        }
    }
    ImGui::Separator();

    if (!emitterData_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Emitter not initialized");
        return;
    }

    if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Translate", &emitterData_->translate.x, 0.1f);
        ImGui::DragFloat("Radius", &emitterData_->radius, 0.05f, 0.0f, 100.0f);

        int count = static_cast<int>(emitterData_->count);
        if (ImGui::DragInt("Count per Emit", &count, 1, 0, static_cast<int>(kMaxParticles))) {
            emitterData_->count = static_cast<uint32_t>(count);
        }

        ImGui::DragFloat("Frequency (s)", &emitterData_->frequency, 0.01f, 0.01f, 10.0f);
        ImGui::Text("Frequency Time: %.3f", emitterData_->frequencyTime);

        if (ImGui::Button("Emit Now")) {
            emitterData_->emit = 1;
        }
    }
#endif
}

void GPUParticleManager::Update(const Camera* camera, float deltaTime)
{
    // シーン取得できればTimeGroup連動dtで上書き
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    const float dt = scene ? scene->GetScaledDeltaTime(timeGroup_) : deltaTime;
    elapsedTime_ += dt;

    // PerFrame更新
    if (perFrameData_) {
        perFrameData_->time = elapsedTime_;
        perFrameData_->deltaTime = dt;
    }

    // Emitterの更新（CPU側）
    if (emitterData_) {
        emitterData_->frequencyTime += dt;
        if (emitterData_->frequency <= emitterData_->frequencyTime) {
            emitterData_->frequencyTime -= emitterData_->frequency;
            emitterData_->emit = 1; // 射出許可
        }
        else {
            emitterData_->emit = 0;
        }
    }

    // PerView更新
    if (camera && perViewData_) {
        perViewData_->viewProjection = camera->GetViewProjectionMatrix();

        // billboardMatrix（Full 用、共通）
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

        // YAxis 用にカメラ位置とモードを送る
        perViewData_->cameraPosition = camera->GetTranslate();
        perViewData_->billboardMode = static_cast<uint32_t>(billboardMode_);
    }
}

void GPUParticleManager::Draw()
{
    auto commandList = dxCore_->GetCommandList();

    // 初回はInit CSも実行する
    if (!initializedOnGPU_) {
        // COMMON -> UAV へ遷移（FreeListIndex / FreeListは以降ずっとUAV）
        D3D12_RESOURCE_BARRIER toUav[3] = {};
        toUav[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[0].Transition.pResource = particleResource_.Get();
        toUav[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toUav[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[1].Transition.pResource = freeListIndexResource_.Get();
        toUav[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toUav[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[2].Transition.pResource = freeListResource_.Get();
        toUav[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(3, toUav);

        DispatchInitializeCS();

        // Init CSの書き込みを後段のEmit CS前に確実にするUAV barrier（3つ全部）
        D3D12_RESOURCE_BARRIER initUavBarrier[3] = {};
        initUavBarrier[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[0].UAV.pResource = particleResource_.Get();
        initUavBarrier[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[1].UAV.pResource = freeListIndexResource_.Get();
        initUavBarrier[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[2].UAV.pResource = freeListResource_.Get();
        commandList->ResourceBarrier(3, initUavBarrier);

        initializedOnGPU_ = true;
    }
    else {
        // 描画用のNPS_RESOURCEからUAVへ戻す（ParticleのみNPSと往復する）
        TransitionParticle(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // ====== EmitParticle ======
    DispatchEmitCS();

    // EmitとUpdateの間：3つともUpdateで触るのでUAV Barrierを張る
    {
        D3D12_RESOURCE_BARRIER barriers[3] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[0].UAV.pResource = particleResource_.Get();
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[1].UAV.pResource = freeListIndexResource_.Get();
        barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[2].UAV.pResource = freeListResource_.Get();
        commandList->ResourceBarrier(3, barriers);
    }

    // ====== UpdateParticle ======
    DispatchUpdateCS();

    // ====== Particleを描画用stateへ ======
    TransitionParticle(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 描画
    commandList->SetGraphicsRootSignature(drawRootSig_.Get());
    commandList->SetPipelineState(drawPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // [0] VS: Particle StructuredBuffer (t0)
    srvManager_->SetGraphicsRootDescriptorTable(0, particleSrvIndex_);
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
    commandList->SetComputeRootSignature(initRootSig_.Get());
    commandList->SetPipelineState(initPSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(particleUavIndex_));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(freeListIndexUavIndex_));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(freeListUavIndex_));

    // 1024 / numthreads(1024) = 1
    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::DispatchEmitCS()
{
    auto commandList = dxCore_->GetCommandList();
    commandList->SetComputeRootSignature(emitRootSig_.Get());
    commandList->SetPipelineState(emitPSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(particleUavIndex_));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(freeListIndexUavIndex_));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(freeListUavIndex_));
    commandList->SetComputeRootConstantBufferView(3, emitterResource_->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(4, perFrameResource_->GetGPUVirtualAddress());

    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::DispatchUpdateCS()
{
    auto commandList = dxCore_->GetCommandList();
    commandList->SetComputeRootSignature(updateRootSig_.Get());
    commandList->SetPipelineState(updatePSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(particleUavIndex_));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(freeListIndexUavIndex_));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(freeListUavIndex_));
    commandList->SetComputeRootConstantBufferView(3, perFrameResource_->GetGPUVirtualAddress());

    // 1024 / numthreads(1024) = 1
    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::UavBarrierParticle()
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = particleResource_.Get();
    dxCore_->GetCommandList()->ResourceBarrier(1, &barrier);
}

void GPUParticleManager::TransitionParticle(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleResource_.Get();
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dxCore_->GetCommandList()->ResourceBarrier(1, &barrier);
}

//==========================================================
// 内部ヘルパー
//==========================================================

void GPUParticleManager::CreateParticleResource()
{
    particleResource_ = dxCore_->CreateUavBufferResource(sizeof(ParticleCS) * kMaxParticles);

    particleUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(particleUavIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));

    particleSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVForStructuredBuffer(particleSrvIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
}

void GPUParticleManager::CreateFreeListResources()
{
    // FreeListIndex: int32_t × 1
    freeListIndexResource_ = dxCore_->CreateUavBufferResource(sizeof(int32_t));
    freeListIndexUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(freeListIndexUavIndex_, freeListIndexResource_.Get(), 1, sizeof(int32_t));

    // FreeList: uint32_t × kMaxParticles
    freeListResource_ = dxCore_->CreateUavBufferResource(sizeof(uint32_t) * kMaxParticles);
    freeListUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(freeListUavIndex_, freeListResource_.Get(), kMaxParticles, sizeof(uint32_t));
}

void GPUParticleManager::CreateInitializePipeline()
{
    HRESULT hr;

    // u0 (Particle UAV)
    D3D12_DESCRIPTOR_RANGE rangeParticle[1] = {};
    rangeParticle[0].BaseShaderRegister = 0;
    rangeParticle[0].NumDescriptors = 1;
    rangeParticle[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeParticle[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // u1 (FreeListIndex UAV)
    D3D12_DESCRIPTOR_RANGE rangeListIndex[1] = {};
    rangeListIndex[0].BaseShaderRegister = 1;
    rangeListIndex[0].NumDescriptors = 1;
    rangeListIndex[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeListIndex[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // u2 (FreeList UAV)
    D3D12_DESCRIPTOR_RANGE rangeList[1] = {};
    rangeList[0].BaseShaderRegister = 2;
    rangeList[0].NumDescriptors = 1;
    rangeList[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeList[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeParticle;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = rangeListIndex;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = rangeList;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

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

    IDxcBlob* cs = dxCore_->CompileShader(
        L"Resources/Shaders/Particle/InitializeParticle.CS.hlsl",
        L"cs_6_0"
    );

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    psoDesc.pRootSignature = initRootSig_.Get();
    hr = dxCore_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&initPSO_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::CreateEmitPipeline()
{
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE rangeParticle[1] = {};
    rangeParticle[0].BaseShaderRegister = 0;
    rangeParticle[0].NumDescriptors = 1;
    rangeParticle[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeParticle[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeListIndex[1] = {};
    rangeListIndex[0].BaseShaderRegister = 1;
    rangeListIndex[0].NumDescriptors = 1;
    rangeListIndex[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeListIndex[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeList[1] = {};
    rangeList[0].BaseShaderRegister = 2;
    rangeList[0].NumDescriptors = 1;
    rangeList[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeList[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    // [0] u0 Particle
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeParticle;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    // [1] u1 FreeListIndex
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = rangeListIndex;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    // [2] u2 FreeList
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = rangeList;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    // [3] b0 EmitterSphere
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].Descriptor.ShaderRegister = 0;
    // [4] b1 PerFrame
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 1;

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
        IID_PPV_ARGS(&emitRootSig_));
    assert(SUCCEEDED(hr));

    IDxcBlob* cs = dxCore_->CompileShader(
        L"Resources/Shaders/Particle/EmitParticle.CS.hlsl",
        L"cs_6_0"
    );

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    psoDesc.pRootSignature = emitRootSig_.Get();
    hr = dxCore_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&emitPSO_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::CreateUpdatePipeline()
{
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE rangeParticle[1] = {};
    rangeParticle[0].BaseShaderRegister = 0;
    rangeParticle[0].NumDescriptors = 1;
    rangeParticle[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeParticle[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeListIndex[1] = {};
    rangeListIndex[0].BaseShaderRegister = 1;
    rangeListIndex[0].NumDescriptors = 1;
    rangeListIndex[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeListIndex[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeList[1] = {};
    rangeList[0].BaseShaderRegister = 2;
    rangeList[0].NumDescriptors = 1;
    rangeList[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeList[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    // [0] u0 Particle
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeParticle;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    // [1] u1 FreeListIndex
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = rangeListIndex;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    // [2] u2 FreeList
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = rangeList;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    // [3] b0 PerFrame
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].Descriptor.ShaderRegister = 0;

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
        IID_PPV_ARGS(&updateRootSig_));
    assert(SUCCEEDED(hr));

    IDxcBlob* cs = dxCore_->CompileShader(
        L"Resources/Shaders/Particle/UpdateParticle.CS.hlsl",
        L"cs_6_0"
    );

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    psoDesc.pRootSignature = updateRootSig_.Get();
    hr = dxCore_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&updatePSO_));
    assert(SUCCEEDED(hr));
}

void GPUParticleManager::CreateDrawPipeline()
{
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE rangeParticleSrv[1] = {};
    rangeParticleSrv[0].BaseShaderRegister = 0;
    rangeParticleSrv[0].NumDescriptors = 1;
    rangeParticleSrv[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeParticleSrv[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeTexture[1] = {};
    rangeTexture[0].BaseShaderRegister = 0;
    rangeTexture[0].NumDescriptors = 1;
    rangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeParticleSrv;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].Descriptor.ShaderRegister = 0;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].DescriptorTable.pDescriptorRanges = rangeTexture;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;

    // 既存Particle.PS.hlslの宣言に合わせるだけで未使用
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].Descriptor.ShaderRegister = 1;

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

    IDxcBlob* vs = dxCore_->CompileShader(L"Resources/Shaders/Particle/GPUParticle.VS.hlsl", L"vs_6_0");
    IDxcBlob* ps = dxCore_->CompileShader(L"Resources/Shaders/Particle/Particle.PS.hlsl", L"ps_6_0");

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

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // 透過部分(src.a=0)で destAlpha を保持し、ImGui Viewport 表示時に下のImGui背景が透けないようにする
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = true;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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

void GPUParticleManager::CreatePerFrame()
{
    perFrameResource_ = dxCore_->CreateBufferResource(sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&perFrameData_));
    perFrameData_->time = 0.0f;
    perFrameData_->deltaTime = 0.0f;
    perFrameData_->pad[0] = 0.0f;
    perFrameData_->pad[1] = 0.0f;
}

void GPUParticleManager::CreateEmitter()
{
    emitterResource_ = dxCore_->CreateBufferResource(sizeof(EmitterSphere));
    emitterResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitterData_));

    // テキストの初期値
    emitterData_->translate = { 0.0f, 0.0f, 0.0f };
    emitterData_->radius = 1.0f;
    emitterData_->count = 10;
    emitterData_->frequency = 0.5f;
    emitterData_->frequencyTime = 0.0f;
    emitterData_->emit = 0;
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
