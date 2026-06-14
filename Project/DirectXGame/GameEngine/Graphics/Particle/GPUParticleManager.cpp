#include "GPUParticleManager.h"
#include "Camera.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include "Material.h"
#include "VertexData.h"
#include "Log.h"
#include "EngineTime.h"
#include <cassert>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GPUParticleManager::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    // 共通パイプラインとリソース
    CreateInitializePipeline();
    CreateEmitPipeline();
    CreateUpdatePipeline();
    CreateDrawPipeline();
    CreateVertexData();
    CreateMaterial();
}

void GPUParticleManager::Finalize()
{
    for (auto& g : previewGroupPendingDelete_) {
        ReleaseGroupResources(g);
    }
    previewGroupPendingDelete_.clear();

    for (auto& pair : groups_) {
        ReleaseGroupResources(pair.second);
    }
    groups_.clear();

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
}

//==========================================================
// グループ管理
//==========================================================

bool GPUParticleManager::IsPreviewName(const std::string& name)
{
    const std::string prefix = kPreviewPrefix;
    return name.size() >= prefix.size() &&
           name.compare(0, prefix.size(), prefix) == 0;
}

void GPUParticleManager::CreateGroup(const std::string& name, const std::string& texturePath)
{
    if (groups_.find(name) != groups_.end()) return;

    GPUParticleGroup g{};
    g.isPreview = IsPreviewName(name);
    CreateGroupResources(g, texturePath);
    groups_.emplace(name, std::move(g));
}

void GPUParticleManager::RemoveGroup(const std::string& name)
{
    auto it = groups_.find(name);
    if (it == groups_.end()) return;
    ReleaseGroupResources(it->second);
    groups_.erase(it);
}

bool GPUParticleManager::HasGroup(const std::string& name) const
{
    return groups_.find(name) != groups_.end();
}

//==========================================================
// 発射API
//==========================================================

void GPUParticleManager::BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius)
{
    // 色指定なしは Random モードで委譲
    BurstEmit(name, position, count, radius, 0, Vector4{ 1, 1, 1, 1 }, Vector4{ 1, 1, 1, 0 });
}

void GPUParticleManager::BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                                    uint32_t colorMode, const Vector4& startColor, const Vector4& endColor)
{
    // スケール範囲なしの版は現状のCB既定値（0.1〜0.5, uniform=true）に委譲
    BurstEmit(name, position, count, radius, colorMode, startColor, endColor,
              Vector2{ 0.1f, 0.1f }, Vector2{ 0.5f, 0.5f }, true);
}

void GPUParticleManager::BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                                    uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                                    const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale)
{
    BurstEmit(name, position, count, radius, colorMode, startColor, endColor,
              scaleMin, scaleMax, uniformScale, 1.0f);
}

void GPUParticleManager::BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                                    uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                                    const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale,
                                    float particleLifeTime,
                                    float lifeScaleStart, float lifeScaleEnd,
                                    const Vector3& rotRandomRange, const Vector3& rotateSpeed)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.emitterData) return;

    auto& e = *it->second.emitterData;
    e.translate = position;
    e.radius = radius;
    e.count = count;
    e.colorMode = colorMode;
    e.startColor = startColor;
    e.endColor = endColor;
    e.scaleMin = scaleMin;
    e.scaleMax = scaleMax;
    e.uniformScale = uniformScale ? 1u : 0u;
    e.particleLifeTime = (particleLifeTime > 0.0001f) ? particleLifeTime : 0.0001f;
    e.lifeScaleStart = lifeScaleStart;
    e.lifeScaleEnd = lifeScaleEnd;
    e.rotRandomRange = { rotRandomRange.x, rotRandomRange.y, rotRandomRange.z, 0.0f };
    e.rotateSpeed = { rotateSpeed.x, rotateSpeed.y, rotateSpeed.z, 0.0f };
    // emit フラグは Update() で CB に転写（CPU/GPU レース回避）
    it->second.pendingBurst = true;
}

void GPUParticleManager::SetContinuousEmit(const std::string& name, bool enabled, float frequency, uint32_t countPerEmit, float radius)
{
    auto it = groups_.find(name);
    if (it == groups_.end()) return;
    it->second.continuousEnabled = enabled;
    if (it->second.emitterData) {
        it->second.emitterData->frequency = frequency;
        it->second.emitterData->count = countPerEmit;
        it->second.emitterData->radius = radius;
    }
}

void GPUParticleManager::SetEmitterTranslate(const std::string& name, const Vector3& translate)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.emitterData) return;
    it->second.emitterData->translate = translate;
}

void GPUParticleManager::SetEmitterVelocity(const std::string& name, const Vector3& baseVelocity, float jitter, int mode)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.emitterData) return;
    auto& e = *it->second.emitterData;
    e.baseVelocity = baseVelocity;
    e.velocityJitter = jitter;
    e.velocityMode = static_cast<float>(mode);
}

void GPUParticleManager::SetEmitterShape(const std::string& name, int mode, const Vector3& ringNormal, float ringThickness)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.emitterData) return;
    auto& e = *it->second.emitterData;
    e.shapeMode = static_cast<float>(mode);
    e.ringNormal = ringNormal;
    e.ringThickness = ringThickness;
}

void GPUParticleManager::SetGroupOrbit(const std::string& name, bool enabled, const Vector3& center,
                                       const Vector3& spinAxis, float spinSpeed,
                                       const Vector3& tumbleAxis, float tumbleSpeed)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.orbitData) return;
    auto& o = *it->second.orbitData;
    o.enabled = enabled ? 1.0f : 0.0f;
    o.center = center;
    o.spinAxis = spinAxis;
    o.spinSpeed = spinSpeed;
    o.tumbleAxis = tumbleAxis;
    o.tumbleSpeed = tumbleSpeed;
}

void GPUParticleManager::SetEmitterGradient(const std::string& name, const std::vector<std::pair<float, Vector4>>& keys)
{
    auto it = groups_.find(name);
    if (it == groups_.end() || !it->second.gradientData) return;
    auto& g = *it->second.gradientData;
    const uint32_t n = (keys.size() < kMaxGradientKeys) ? static_cast<uint32_t>(keys.size()) : kMaxGradientKeys;
    // 2個未満は無効化（粒子の start/end 2色補間に戻す）
    g.keyCount = (n >= 2) ? n : 0;
    for (uint32_t i = 0; i < n; ++i) {
        g.keyLoc[i]   = { keys[i].first, 0.0f, 0.0f, 0.0f };
        g.keyColor[i] = keys[i].second;
    }
}

//==========================================================
// ビルボード / TimeGroup
//==========================================================

void GPUParticleManager::SetGroupBillboardMode(const std::string& name, BillboardMode mode)
{
    auto it = groups_.find(name);
    if (it == groups_.end()) return;
    it->second.billboardMode = mode;
}

void GPUParticleManager::SetGroupTimeGroup(const std::string& name, TimeGroup group)
{
    auto it = groups_.find(name);
    if (it == groups_.end()) return;
    it->second.timeGroup = group;
}

//==========================================================
// 毎フレーム
//==========================================================

void GPUParticleManager::Update(const Camera* camera, float deltaTime)
{
    // 共通：カメラからview/billboard行列を作る
    if (camera) {
        viewProjectionMatrix_ = camera->GetViewProjectionMatrix();
        const Matrix4x4& view = camera->GetViewMatrix();
        Matrix4x4 b = MakeIdentity4x4();
        b.m[0][0] = view.m[0][0]; b.m[0][1] = view.m[1][0]; b.m[0][2] = view.m[2][0];
        b.m[1][0] = view.m[0][1]; b.m[1][1] = view.m[1][1]; b.m[1][2] = view.m[2][1];
        b.m[2][0] = view.m[0][2]; b.m[2][1] = view.m[1][2]; b.m[2][2] = view.m[2][2];
        fullBillboardMatrix_ = b;
        cameraPosition_ = camera->GetTranslate();
    }

    // 各グループのCBを更新（プレビュー用は UpdatePreviewSim で別途進めるのでスキップ）
    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        if (g.isPreview) continue;

        // TimeGroup連動dt。供給元（シーン）が無ければ deltaTime にフォールバック
        const float dt = EngineTime::ScaledDeltaTime(g.timeGroup, deltaTime);
        UpdateGroupSim(g, dt);

        // PerView（メイン用）
        if (g.perViewData) {
            g.perViewData->viewProjection = viewProjectionMatrix_;
            g.perViewData->billboardMatrix = fullBillboardMatrix_;
            g.perViewData->cameraPosition = cameraPosition_;
            g.perViewData->billboardMode = static_cast<uint32_t>(g.billboardMode);
        }
    }
}

void GPUParticleManager::UpdateGroupSim(GPUParticleGroup& g, float dt)
{
    g.elapsedTime += dt;

    // PerFrame
    if (g.perFrameData) {
        g.perFrameData->time = g.elapsedTime;
        g.perFrameData->deltaTime = dt;
    }

    // Emitter の emit フラグをこのフレームの最終値で確定させる
    //   - pendingBurst が立っていれば 1（その後フラグを下ろす）
    //   - 連続発射なら frequency に従って 0/1
    //   - それ以外は 0
    if (g.emitterData) {
        if (g.pendingBurst) {
            g.emitterData->emit = 1;
            g.pendingBurst = false;
        } else if (g.continuousEnabled) {
            g.emitterData->frequencyTime += dt;
            if (g.emitterData->frequency <= g.emitterData->frequencyTime) {
                g.emitterData->frequencyTime -= g.emitterData->frequency;
                g.emitterData->emit = 1;
            } else {
                g.emitterData->emit = 0;
            }
        } else {
            g.emitterData->emit = 0;
        }
    }
}

void GPUParticleManager::UpdatePreviewSim(float deltaTime)
{
    // 前フレームに遅延予約したプレビューグループをここ（描画コマンドを積む前）で実解放。
    for (auto& g : previewGroupPendingDelete_) {
        ReleaseGroupResources(g);
    }
    previewGroupPendingDelete_.clear();

    // プレビュー用グループだけを unscaled な実 delta で進める（シーンのタイムスケール非依存）。
    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        if (!g.isPreview) continue;
        UpdateGroupSim(g, deltaTime);
    }
}

void GPUParticleManager::RecyclePreviewGroups(const std::vector<std::string>& keepNames)
{
    for (auto it = groups_.begin(); it != groups_.end();) {
        GPUParticleGroup& g = it->second;
        if (!g.isPreview) { ++it; continue; }

        const bool keep = std::find(keepNames.begin(), keepNames.end(), it->first) != keepNames.end();
        if (keep) { ++it; continue; }

        // 今プレビュー中のエフェクトが参照していないプレビューグループは遅延解放へ。
        previewGroupPendingDelete_.push_back(std::move(g));
        it = groups_.erase(it);
    }
}

void GPUParticleManager::UpdatePreviewView(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos)
{
    // プレビューカメラから billboard / cameraPosition / VP を計算してプレビュー用CBに書き込む
    Matrix4x4 bb = MakeIdentity4x4();
    bb.m[0][0] = viewMatrix.m[0][0]; bb.m[0][1] = viewMatrix.m[1][0]; bb.m[0][2] = viewMatrix.m[2][0];
    bb.m[1][0] = viewMatrix.m[0][1]; bb.m[1][1] = viewMatrix.m[1][1]; bb.m[1][2] = viewMatrix.m[2][1];
    bb.m[2][0] = viewMatrix.m[0][2]; bb.m[2][1] = viewMatrix.m[1][2]; bb.m[2][2] = viewMatrix.m[2][2];

    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        if (!g.perViewPreviewData) continue;
        g.perViewPreviewData->viewProjection = viewProjectionMatrix;
        g.perViewPreviewData->billboardMatrix = bb;
        g.perViewPreviewData->cameraPosition = cameraPos;
        g.perViewPreviewData->billboardMode = static_cast<uint32_t>(g.billboardMode);
    }
}

void GPUParticleManager::DrawPreview()
{
    if (groups_.empty()) return;

    // プレビュー用グループを独立してシミュレート＋描画する（シーンの Draw からは完全に分離）。
    // シーンが停止していても、プレビューは UpdatePreviewSim の unscaled delta で進んだ状態が描かれる。
    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        if (!g.isPreview) continue;
        SimulateAndDrawGroup(g, g.perViewPreviewResource.Get());
    }
}

void GPUParticleManager::Draw()
{
    if (groups_.empty()) return;

    // シーン用グループのみ（プレビュー用は DrawPreview 側で独立処理）。
    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        if (g.isPreview) continue;
        SimulateAndDrawGroup(g, g.perViewResource.Get());
    }
}

void GPUParticleManager::SimulateAndDrawGroup(GPUParticleGroup& g, ID3D12Resource* perViewCB)
{
    auto commandList = dxCore_->GetCommandList();

    // 1グループを単独のサイクルで処理する
    //   未初期化:  COMMON -> UAV + Init CS + UAV barrier
    //   初期化済:  NPS    -> UAV
    //   共通:      Emit CS -> UAV barrier -> Update CS -> UAV -> NPS -> Draw
    if (!g.initializedOnGPU) {
        // COMMON -> UAV へ遷移
        D3D12_RESOURCE_BARRIER toUav[3] = {};
        toUav[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[0].Transition.pResource = g.particleResource.Get();
        toUav[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toUav[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[1].Transition.pResource = g.freeListIndexResource.Get();
        toUav[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toUav[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav[2].Transition.pResource = g.freeListResource.Get();
        toUav[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toUav[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(3, toUav);

        DispatchInitializeCS(g);

        // Init後のUAVバリアでEmit前の書き込みを保証
        D3D12_RESOURCE_BARRIER initUavBarrier[3] = {};
        initUavBarrier[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[0].UAV.pResource = g.particleResource.Get();
        initUavBarrier[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[1].UAV.pResource = g.freeListIndexResource.Get();
        initUavBarrier[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        initUavBarrier[2].UAV.pResource = g.freeListResource.Get();
        commandList->ResourceBarrier(3, initUavBarrier);

        g.initializedOnGPU = true;
        // ここでは Particle は UAV 状態（NPS には行っていない）
    } else {
        // 既に init 済み：前フレーム描画後の NPS から UAV に戻す
        TransitionParticle(g, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Emit （emit フラグは Update で確定済み。Draw中に CB を書き換えると GPU 実行前にレースするので触らない）
    DispatchEmitCS(g);

    {
        D3D12_RESOURCE_BARRIER barriers[3] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[0].UAV.pResource = g.particleResource.Get();
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[1].UAV.pResource = g.freeListIndexResource.Get();
        barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[2].UAV.pResource = g.freeListResource.Get();
        commandList->ResourceBarrier(3, barriers);
    }

    // Update
    DispatchUpdateCS(g);

    // 描画用 NPS へ
    TransitionParticle(g, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 描画
    commandList->SetGraphicsRootSignature(drawRootSig_.Get());
    commandList->SetPipelineState(drawPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    srvManager_->SetGraphicsRootDescriptorTable(0, g.particleSrvIndex);
    commandList->SetGraphicsRootConstantBufferView(1, perViewCB->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    srvManager_->SetGraphicsRootDescriptorTable(3, g.textureSrvIndex);

    commandList->DrawInstanced(6, kMaxParticles, 0, 0);
}

void GPUParticleManager::OnImGui()
{
#ifdef USE_IMGUI
    ImGui::Text("Groups: %zu", groups_.size());
    ImGui::Separator();

    const char* billboardItems[] = { "None", "Full", "YAxis" };
    const char* timeGroupItems[] = { "World", "Player", "UI" };

    for (auto& pair : groups_) {
        GPUParticleGroup& g = pair.second;
        ImGui::PushID(pair.first.c_str());
        if (ImGui::CollapsingHeader(pair.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Texture: %s", g.textureFilePath.c_str());
            ImGui::Text("Elapsed: %.2f s", g.elapsedTime);

            int bIdx = static_cast<int>(g.billboardMode);
            if (ImGui::Combo("Billboard", &bIdx, billboardItems, IM_ARRAYSIZE(billboardItems))) {
                g.billboardMode = static_cast<BillboardMode>(bIdx);
            }
            int tg = static_cast<int>(g.timeGroup);
            if (ImGui::Combo("Time Group", &tg, timeGroupItems, IM_ARRAYSIZE(timeGroupItems))) {
                g.timeGroup = static_cast<TimeGroup>(tg);
            }

            ImGui::Checkbox("Continuous Emit", &g.continuousEnabled);
            if (g.emitterData) {
                ImGui::DragFloat3("Translate", &g.emitterData->translate.x, 0.1f);
                ImGui::DragFloat("Radius", &g.emitterData->radius, 0.05f, 0.0f, 100.0f);
                int count = static_cast<int>(g.emitterData->count);
                if (ImGui::DragInt("Count per Emit", &count, 1, 0, static_cast<int>(kMaxParticles))) {
                    g.emitterData->count = static_cast<uint32_t>(count);
                }
                ImGui::DragFloat("Frequency (s)", &g.emitterData->frequency, 0.01f, 0.01f, 10.0f);
                if (ImGui::Button("Burst Now")) {
                    g.pendingBurst = true;
                }
            }
        }
        ImGui::PopID();
    }
#endif
}

//==========================================================
// CS Dispatch
//==========================================================

void GPUParticleManager::DispatchInitializeCS(GPUParticleGroup& g)
{
    auto commandList = dxCore_->GetCommandList();
    commandList->SetComputeRootSignature(initRootSig_.Get());
    commandList->SetPipelineState(initPSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(g.particleUavIndex));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(g.freeListIndexUavIndex));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(g.freeListUavIndex));

    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::DispatchEmitCS(GPUParticleGroup& g)
{
    auto commandList = dxCore_->GetCommandList();
    commandList->SetComputeRootSignature(emitRootSig_.Get());
    commandList->SetPipelineState(emitPSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(g.particleUavIndex));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(g.freeListIndexUavIndex));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(g.freeListUavIndex));
    commandList->SetComputeRootConstantBufferView(3, g.emitterResource->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(4, g.perFrameResource->GetGPUVirtualAddress());

    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::DispatchUpdateCS(GPUParticleGroup& g)
{
    auto commandList = dxCore_->GetCommandList();
    commandList->SetComputeRootSignature(updateRootSig_.Get());
    commandList->SetPipelineState(updatePSO_.Get());

    commandList->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptorHandle(g.particleUavIndex));
    commandList->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(g.freeListIndexUavIndex));
    commandList->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(g.freeListUavIndex));
    commandList->SetComputeRootConstantBufferView(3, g.perFrameResource->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(4, g.gradientResource->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(5, g.orbitResource->GetGPUVirtualAddress());

    commandList->Dispatch(1, 1, 1);
}

void GPUParticleManager::TransitionParticle(GPUParticleGroup& g, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g.particleResource.Get();
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dxCore_->GetCommandList()->ResourceBarrier(1, &barrier);
}

//==========================================================
// グループ別リソース確保・解放
//==========================================================

void GPUParticleManager::CreateGroupResources(GPUParticleGroup& g, const std::string& texturePath)
{
    // テクスチャ
    g.textureFilePath = texturePath;
    TextureManager::GetInstance()->LoadTexture(texturePath);
    g.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texturePath);

    // Particle DEFAULT heap
    g.particleResource = dxCore_->CreateUavBufferResource(sizeof(ParticleCS) * kMaxParticles);
    g.particleUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(g.particleUavIndex, g.particleResource.Get(), kMaxParticles, sizeof(ParticleCS));
    g.particleSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVForStructuredBuffer(g.particleSrvIndex, g.particleResource.Get(), kMaxParticles, sizeof(ParticleCS));

    // FreeListIndex
    g.freeListIndexResource = dxCore_->CreateUavBufferResource(sizeof(int32_t));
    g.freeListIndexUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(g.freeListIndexUavIndex, g.freeListIndexResource.Get(), 1, sizeof(int32_t));

    // FreeList
    g.freeListResource = dxCore_->CreateUavBufferResource(sizeof(uint32_t) * kMaxParticles);
    g.freeListUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(g.freeListUavIndex, g.freeListResource.Get(), kMaxParticles, sizeof(uint32_t));

    // Emitter CB
    g.emitterResource = dxCore_->CreateBufferResource(sizeof(EmitterSphere));
    g.emitterResource->Map(0, nullptr, reinterpret_cast<void**>(&g.emitterData));
    g.emitterData->translate = { 0.0f, 0.0f, 0.0f };
    g.emitterData->radius = 1.0f;
    g.emitterData->count = 10;
    g.emitterData->frequency = 0.5f;
    g.emitterData->frequencyTime = 0.0f;
    g.emitterData->emit = 0;
    g.emitterData->colorMode = 0;
    g.emitterData->baseVelocity = { 0.0f, 0.0f, 0.0f };
    g.emitterData->startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    g.emitterData->endColor   = { 1.0f, 1.0f, 1.0f, 0.0f };
    g.emitterData->scaleMin = { 0.1f, 0.1f };
    g.emitterData->scaleMax = { 0.5f, 0.5f };
    g.emitterData->uniformScale = 1;
    g.emitterData->particleLifeTime = 1.0f;
    g.emitterData->velocityMode = 0.0f;
    g.emitterData->velocityJitter = 0.0f;
    g.emitterData->shapeMode = 0.0f;
    g.emitterData->ringNormal = { 0.0f, 0.0f, 1.0f };
    g.emitterData->ringThickness = 0.0f;
    g.emitterData->lifeScaleStart = 1.0f;
    g.emitterData->lifeScaleEnd = 1.0f;
    g.emitterData->rotRandomRange = { 0.0f, 0.0f, 0.0f, 0.0f };
    g.emitterData->rotateSpeed = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Gradient CB（Update CS b1）。既定 keyCount=0（無効＝粒子の start/end 2色補間）
    // CBV は 256 バイト境界なのでサイズを 256 の倍数に丸める
    g.gradientResource = dxCore_->CreateBufferResource((sizeof(ParticleGradient) + 255) & ~static_cast<size_t>(255));
    g.gradientResource->Map(0, nullptr, reinterpret_cast<void**>(&g.gradientData));
    *g.gradientData = ParticleGradient{};

    // Orbit CB（Update CS b2）。既定 enabled=0（従来の velocity 直線移動）
    g.orbitResource = dxCore_->CreateBufferResource((sizeof(ParticleOrbit) + 255) & ~static_cast<size_t>(255));
    g.orbitResource->Map(0, nullptr, reinterpret_cast<void**>(&g.orbitData));
    *g.orbitData = ParticleOrbit{};

    // PerFrame CB
    g.perFrameResource = dxCore_->CreateBufferResource(sizeof(PerFrame));
    g.perFrameResource->Map(0, nullptr, reinterpret_cast<void**>(&g.perFrameData));
    g.perFrameData->time = 0.0f;
    g.perFrameData->deltaTime = 0.0f;
    g.perFrameData->pad[0] = 0.0f;
    g.perFrameData->pad[1] = 0.0f;

    // PerView CB（メイン用）
    g.perViewResource = dxCore_->CreateBufferResource(sizeof(PerView));
    g.perViewResource->Map(0, nullptr, reinterpret_cast<void**>(&g.perViewData));
    g.perViewData->viewProjection = MakeIdentity4x4();
    g.perViewData->billboardMatrix = MakeIdentity4x4();
    g.perViewData->cameraPosition = { 0.0f, 0.0f, 0.0f };
    g.perViewData->billboardMode = static_cast<uint32_t>(BillboardMode::Full);

    // PerView CB（プレビュー用）
    g.perViewPreviewResource = dxCore_->CreateBufferResource(sizeof(PerView));
    g.perViewPreviewResource->Map(0, nullptr, reinterpret_cast<void**>(&g.perViewPreviewData));
    g.perViewPreviewData->viewProjection = MakeIdentity4x4();
    g.perViewPreviewData->billboardMatrix = MakeIdentity4x4();
    g.perViewPreviewData->cameraPosition = { 0.0f, 0.0f, 0.0f };
    g.perViewPreviewData->billboardMode = static_cast<uint32_t>(BillboardMode::Full);
}

void GPUParticleManager::ReleaseGroupResources(GPUParticleGroup& g)
{
    if (g.emitterResource && g.emitterData) {
        g.emitterResource->Unmap(0, nullptr);
        g.emitterData = nullptr;
    }
    if (g.gradientResource && g.gradientData) {
        g.gradientResource->Unmap(0, nullptr);
        g.gradientData = nullptr;
    }
    if (g.orbitResource && g.orbitData) {
        g.orbitResource->Unmap(0, nullptr);
        g.orbitData = nullptr;
    }
    if (g.perFrameResource && g.perFrameData) {
        g.perFrameResource->Unmap(0, nullptr);
        g.perFrameData = nullptr;
    }
    if (g.perViewResource && g.perViewData) {
        g.perViewResource->Unmap(0, nullptr);
        g.perViewData = nullptr;
    }
    if (g.perViewPreviewResource && g.perViewPreviewData) {
        g.perViewPreviewResource->Unmap(0, nullptr);
        g.perViewPreviewData = nullptr;
    }
    g.emitterResource.Reset();
    g.gradientResource.Reset();
    g.orbitResource.Reset();
    g.perFrameResource.Reset();
    g.perViewResource.Reset();
    g.perViewPreviewResource.Reset();
    g.freeListResource.Reset();
    g.freeListIndexResource.Reset();
    g.particleResource.Reset();

    // このクラスが Allocate した SRV/UAV スロットを SRVManager へ返却する（再利用される）。
    // textureSrvIndex は TextureManager 所有なのでここでは返却しない。
    // ※ GPU 使用中の解放を避けるため、呼び出し側で遅延（次フレーム）解放すること。
    if (srvManager_) {
        srvManager_->Free(g.particleUavIndex);
        srvManager_->Free(g.particleSrvIndex);
        srvManager_->Free(g.freeListIndexUavIndex);
        srvManager_->Free(g.freeListUavIndex);
    }
    g.particleUavIndex = 0;
    g.particleSrvIndex = 0;
    g.freeListIndexUavIndex = 0;
    g.freeListUavIndex = 0;
}

//==========================================================
// 共通パイプライン・リソース生成
//==========================================================

void GPUParticleManager::CreateInitializePipeline()
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
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].Descriptor.ShaderRegister = 0;
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

    D3D12_ROOT_PARAMETER rootParameters[6] = {};
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
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].Descriptor.ShaderRegister = 0; // PerFrame b0
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 1; // Gradient b1
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[5].Descriptor.ShaderRegister = 2; // Orbit b2

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
