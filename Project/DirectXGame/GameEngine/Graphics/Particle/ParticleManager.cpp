#include "ParticleManager.h"
#include "TextureManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "VertexData.h"
#include "Material.h"
#include "Log.h"
#include <cassert>
#include "imgui.h"

ParticleManager* ParticleManager::GetInstance()
{
    static ParticleManager instance;
    return &instance;
}

void ParticleManager::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    CreateRootSignature();

    // 全ブレンドモード分のPSOを作成
    for (int i = 0; i < kCountOfBlendMode; ++i) {
        CreateGraphicsPipelineState(static_cast<BlendMode>(i));
    }

    // 共通リソース作成
    CreateVertexData();
    CreateMaterialResource();

    // 乱数生成器の初期化
    std::random_device rd;
    randomEngine_.seed(rd());
}

void ParticleManager::Finalize()
{
    // 全グループのリソースを解放
    for (auto& pair : particleGroups_) {
        if (pair.second.instancingResource) {
            pair.second.instancingResource->Unmap(0, nullptr);
            pair.second.instancingResource.Reset();
        }
    }
    particleGroups_.clear();

    vertexResource_.Reset();
    materialResource_.Reset();
    rootSignature_.Reset();
    for (auto& pso : pipelineStates_) {
        pso.Reset();
    }
}

void ParticleManager::CreateParticleGroup(const std::string& name, const std::string& textureFilePath)
{
    // 登録済みの名前かチェック
    assert(particleGroups_.find(name) == particleGroups_.end());

    // 新たなパーティクルグループを作成しコンテナに登録
    ParticleGroup group;

    // マテリアルデータにテクスチャファイルパスを設定
    group.textureFilePath = textureFilePath;

    // テクスチャを読み込む
    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    // マテリアルデータにテクスチャのSRVインデックスを記録
    group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(textureFilePath);

    // インスタンシング用リソースの生成
    group.instancingResource = dxCore_->CreateBufferResource(sizeof(ParticleForGPU) * kMaxInstanceCount);

    // インスタンシング用にSRVを確保してSRVインデックスを記録
    group.instancingSrvIndex = srvManager_->Allocate();

    // SRV生成（StructuredBuffer用設定）
    srvManager_->CreateSRVForStructuredBuffer(
        group.instancingSrvIndex,
        group.instancingResource.Get(),
        kMaxInstanceCount,
        sizeof(ParticleForGPU)
    );

    // インスタンシングデータを書き込むためのポインタを取得
    group.instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&group.instancingData));

    // 初期化
    group.instanceCount = 0;
    for (uint32_t i = 0; i < kMaxInstanceCount; ++i) {
        group.instancingData[i].WVP = MakeIdentity4x4();
        group.instancingData[i].World = MakeIdentity4x4();
    }

    // コンテナに登録
    particleGroups_[name] = std::move(group);
}

void ParticleManager::RemoveParticleGroup(const std::string& name)
{
    auto it = particleGroups_.find(name);
    if (it == particleGroups_.end()) {
        return;
    }

    // リソースを解放
    if (it->second.instancingResource) {
        it->second.instancingResource->Unmap(0, nullptr);
        it->second.instancingResource.Reset();
    }

    particleGroups_.erase(it);
}

void ParticleManager::Emit(const std::string& name, const Vector3& position, uint32_t count)
{
    // 登録済みのパーティクルグループ名かチェック
    assert(particleGroups_.find(name) != particleGroups_.end());

    ParticleGroup& group = particleGroups_[name];

    // 拡散スケールを適用した速度範囲
    float velScale = emitterSettings_.velocityScale;
    std::uniform_real_distribution<float> distVel(-0.5f * velScale, 0.5f * velScale);
    std::uniform_real_distribution<float> distVelY(0.0f, 0.5f * velScale);
    std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

    for (uint32_t i = 0; i < count; ++i) {
        // 最大数チェック
        if (group.particles.size() >= kMaxInstanceCount) {
            break;
        }

        // 新たなパーティクルを作成
        Particle particle;
        particle.transform.scale = { 1.0f, 1.0f, 1.0f };
        particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
        particle.transform.translate = position;

        // ランダムな速度
        particle.velocity = {
            distVel(randomEngine_),
            distVelY(randomEngine_),
            distVel(randomEngine_)
        };

        // 色の設定
        if (emitterSettings_.useRandomColor) {
            particle.color = {
                distColor(randomEngine_),
                distColor(randomEngine_),
                distColor(randomEngine_),
                1.0f
            };
        } else {
            particle.color = emitterSettings_.baseColor;
        }

        particle.lifeTime = 2.0f;
        particle.currentTime = 0.0f;

        // パーティクルグループに登録
        group.particles.push_back(particle);
    }
}

void ParticleManager::Update()
{
    // カメラ取得
    if (!camera_) {
        return;
    }

    // デルタタイムを取得
    const float kDeltaTime = dxCore_->GetDeltaTime();

    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

    // ビルボード行列の計算
    Matrix4x4 billboardMatrix = MakeIdentity4x4();
    billboardMatrix.m[0][0] = viewMatrix.m[0][0];
    billboardMatrix.m[0][1] = viewMatrix.m[1][0];
    billboardMatrix.m[0][2] = viewMatrix.m[2][0];
    billboardMatrix.m[1][0] = viewMatrix.m[0][1];
    billboardMatrix.m[1][1] = viewMatrix.m[1][1];
    billboardMatrix.m[1][2] = viewMatrix.m[2][1];
    billboardMatrix.m[2][0] = viewMatrix.m[0][2];
    billboardMatrix.m[2][1] = viewMatrix.m[1][2];
    billboardMatrix.m[2][2] = viewMatrix.m[2][2];

    // 全てのパーティクルグループについて処理する
    for (auto& pair : particleGroups_) {
        ParticleGroup& group = pair.second;
        uint32_t instanceIndex = 0;

        // グループ内の全てのパーティクルについて処理する
        auto it = group.particles.begin();
        while (it != group.particles.end()) {
            Particle& particle = *it;

            // 経過時間を加算
            particle.currentTime += 1.0f / 60.0f;

            // 寿命に達していたらグループから外す
            if (particle.currentTime >= particle.lifeTime) {
                it = group.particles.erase(it);
                continue;
            }

            // 場の影響を計算（加速）
            if (isAccelerationFieldEnabled_) {
                // Fieldの範囲内のParticleには加速度を適用する
                if (IsCollision(accelerationField_.area, particle.transform.translate)) {
                    particle.velocity.x += accelerationField_.acceleration.x * kDeltaTime;
                    particle.velocity.y += accelerationField_.acceleration.y * kDeltaTime;
                    particle.velocity.z += accelerationField_.acceleration.z * kDeltaTime;
                }
            }

            // 移動処理（速度を座標に加算）
            particle.transform.translate.x += particle.velocity.x * kDeltaTime;
            particle.transform.translate.y += particle.velocity.y * kDeltaTime;
            particle.transform.translate.z += particle.velocity.z * kDeltaTime;

            // ワールド行列を計算
            Matrix4x4 scaleMatrix = MakeScaleMatrix(particle.transform);
            Matrix4x4 translateMatrix = MakeTranslateMatrix(particle.transform);

            // ビルボード適用
            Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, billboardMatrix), translateMatrix);

            // ワールドビュープロジェクション行列を合成
            Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);

            // インスタンシング用データ1個分の書き込み
            if (instanceIndex < kMaxInstanceCount) {
                group.instancingData[instanceIndex].WVP = wvpMatrix;
                group.instancingData[instanceIndex].World = worldMatrix;
                group.instancingData[instanceIndex].color = particle.color;
                instanceIndex++;
            }

            ++it;
        }

        // インスタンス数を記録
        group.instanceCount = instanceIndex;
    }
}

void ParticleManager::Draw()
{
    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();

    // ルートシグネチャを設定
    commandList->SetGraphicsRootSignature(rootSignature_.Get());

    // PSOを設定
    commandList->SetPipelineState(pipelineStates_[blendMode_].Get());

    // プリミティブトポロジーを設定
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // VBVを設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // マテリアルCBVをセット（RootParameter[1]）
    commandList->SetGraphicsRootConstantBufferView(1, materialResource_->GetGPUVirtualAddress());

    // 全てのパーティクルグループについて処理する
    for (auto& pair : particleGroups_) {
        ParticleGroup& group = pair.second;

        if (group.instanceCount == 0) {
            continue;
        }

        // テクスチャのSRVのDescriptorTableを設定
        srvManager_->SetGraphicsRootDescriptorTable(2, group.textureSrvIndex);

        // インスタンシングデータのSRVのDescriptorTableを設定
        srvManager_->SetGraphicsRootDescriptorTable(0, group.instancingSrvIndex);

        // DrawCall（インスタンシング描画）
        commandList->DrawInstanced(6, group.instanceCount, 0, 0);
    }
}

void ParticleManager::OnImGui()
{
#ifdef _DEBUG

    if (ImGui::Begin("Particle Settings")) {
        // エミッター設定
        ImGui::Text("Emitter Settings");
        ImGui::SliderFloat("Emit Rate (per sec)", &emitterSettings_.emitRate, 0.0f, 100.0f);
        ImGui::SliderFloat("Velocity Scale", &emitterSettings_.velocityScale, 0.0f, 5.0f);
        ImGui::Checkbox("Use Random Color", &emitterSettings_.useRandomColor);

        if (!emitterSettings_.useRandomColor) {
            ImGui::ColorEdit4("Base Color", &emitterSettings_.baseColor.x);
        }

        ImGui::Separator();

        // 加速度フィールド設定
        ImGui::Text("Acceleration Field");
        ImGui::Checkbox("Enable Field", &isAccelerationFieldEnabled_);

        if (isAccelerationFieldEnabled_) {
            ImGui::DragFloat3("Acceleration", &accelerationField_.acceleration.x, 0.1f);
            ImGui::DragFloat3("Area Min", &accelerationField_.area.min.x, 0.1f);
            ImGui::DragFloat3("Area Max", &accelerationField_.area.max.x, 0.1f);
        }

        ImGui::Separator();

        // ブレンドモード
        const char* blendModes[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
        int currentMode = static_cast<int>(blendMode_);
        if (ImGui::Combo("Blend Mode", &currentMode, blendModes, IM_ARRAYSIZE(blendModes))) {
            blendMode_ = static_cast<BlendMode>(currentMode);
        }

        // パーティクル情報
        ImGui::Separator();
        ImGui::Text("Particle Info");
        uint32_t totalCount = 0;
        for (const auto& pair : particleGroups_) {
            totalCount += static_cast<uint32_t>(pair.second.particles.size());
        }
        ImGui::Text("Total Particles: %u", totalCount);
    }
    ImGui::End();

#endif // DEBUG
}

bool ParticleManager::IsCollision(const AABB& aabb, const Vector3& point)
{
    if (point.x >= aabb.min.x && point.x <= aabb.max.x &&
        point.y >= aabb.min.y && point.y <= aabb.max.y &&
        point.z >= aabb.min.z && point.z <= aabb.max.z) {
        return true;
    }
    return false;
}

void ParticleManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange: SRV(t0) - インスタンシング用StructuredBuffer（VS）
    D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing[1] = {};
    descriptorRangeInstancing[0].BaseShaderRegister = 0;
    descriptorRangeInstancing[0].NumDescriptors = 1;
    descriptorRangeInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // DescriptorRange: SRV(t0) - テクスチャ用（PS）
    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = {};
    descriptorRangeTexture[0].BaseShaderRegister = 0;
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

    // InputLayout
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
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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

void ParticleManager::CreateVertexData()
{
    // 板ポリ（6頂点）
    vertexResource_ = dxCore_->CreateBufferResource(sizeof(VertexData) * 6);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 左上
    vertexData[0].position = { -0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 0.0f };
    vertexData[0].normal = { 0.0f, 0.0f, 1.0f };

    // 右上
    vertexData[1].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[1].texcoord = { 1.0f, 0.0f };
    vertexData[1].normal = { 0.0f, 0.0f, 1.0f };

    // 左下
    vertexData[2].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[2].texcoord = { 0.0f, 1.0f };
    vertexData[2].normal = { 0.0f, 0.0f, 1.0f };

    // 左下
    vertexData[3].position = { -0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[3].texcoord = { 0.0f, 1.0f };
    vertexData[3].normal = { 0.0f, 0.0f, 1.0f };

    // 右上
    vertexData[4].position = { 0.5f, 0.5f, 0.0f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[4].normal = { 0.0f, 0.0f, 1.0f };

    // 右下
    vertexData[5].position = { 0.5f, -0.5f, 0.0f, 1.0f };
    vertexData[5].texcoord = { 1.0f, 1.0f };
    vertexData[5].normal = { 0.0f, 0.0f, 1.0f };

    vertexResource_->Unmap(0, nullptr);

    // VertexBufferView作成
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void ParticleManager::CreateMaterialResource()
{
    // マテリアル用バッファ作成
    materialResource_ = dxCore_->CreateBufferResource(sizeof(Material));

    Material* materialData = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = false;
    materialData->uvTransform = MakeIdentity4x4();

    materialResource_->Unmap(0, nullptr);
}