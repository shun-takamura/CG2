#include "ShadowMap.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Log.h"
#include <dxcapi.h>
#include <cassert>
#include <cmath>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void ShadowMap::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
    assert(dxCore && srvManager);
    dxCore_ = dxCore;
    srvManager_ = srvManager;

    CreateResources();
    CreatePipeline();
}

void ShadowMap::CreateResources()
{
    ID3D12Device* device = dxCore_->GetDevice();

    // ---- 深度 Texture2DArray（SRV 対応のため TYPELESS）----
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kResolution;
    desc.Height = kResolution;
    desc.DepthOrArraySize = static_cast<UINT16>(kShadowCascadeCount);
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS; // DSV も SRV も作れるよう TYPELESS
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProp{};
    heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

    // 初期状態は PIXEL_SHADER_RESOURCE にしておく。こうすると、シャドウパスが
    // 走らないフレーム（カメラ未設定など）でも受光側が t3 を安全にサンプルできる。
    HRESULT hr = device->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&depthArray_));
    assert(SUCCEEDED(hr));
    currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // ---- DSV ヒープ（カスケードごとに 1 枚）----
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = kShadowCascadeCount;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));
    dsvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // スライスごとに DSV を作成
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        device->CreateDepthStencilView(depthArray_.Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += dsvDescriptorSize_;
    }

    // ---- SRV（Texture2DArray, 深度値を R32_FLOAT として読む）----
    srvIndex_ = srvManager_->Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = kShadowCascadeCount;
    device->CreateShaderResourceView(
        depthArray_.Get(), &srvDesc,
        srvManager_->GetCPUDescriptorHandle(srvIndex_));

    // ---- 受光パス用 CB（b5）----
    constantBuffer_ = dxCore_->CreateBufferResource(sizeof(ShadowConstants));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedConstants_));
    *mappedConstants_ = ShadowConstants{};
    mappedConstants_->shadowBias = bias_;
    mappedConstants_->normalOffset = normalOffset_;
    mappedConstants_->enabled = enabled_ ? 1.0f : 0.0f;
    mappedConstants_->softness = softness_;
    mappedConstants_->debug = debugShadowOnly_ ? 1.0f : 0.0f;
    mappedConstants_->maxBlur = maxBlur_;
    mappedConstants_->fadeRadius = fadeRadius_;
}

void ShadowMap::CreatePipeline()
{
    ID3D12Device* device = dxCore_->GetDevice();
    HRESULT hr;

    // ---- RootSignature ----
    // [0] VS CBV(b0) = TransformationMatrix（各オブジェクトの World を使う）
    // [1] VS 32bit定数(b1) = カスケードの ViewProj（float4x4 = 16 個）
    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[0].Descriptor.ShaderRegister = 0;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].Constants.Num32BitValues = 16; // float4x4

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr) && errBlob) Log(static_cast<char*>(errBlob->GetBufferPointer()));
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    // ---- PSO（VS のみ・深度書き込み専用）----
    IDxcBlob* vs = dxCore_->CompileShader(L"Resources/Shaders/Shadow/ShadowDepth.VS.hlsl", L"vs_6_0");
    assert(vs);

    // Object3D と同じ入力レイアウト（POSITION / TEXCOORD / NORMAL）
    D3D12_INPUT_ELEMENT_DESC elems[3] = {};
    elems[0].SemanticName = "POSITION";
    elems[0].SemanticIndex = 0;
    elems[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    elems[1].SemanticName = "TEXCOORD";
    elems[1].SemanticIndex = 0;
    elems[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    elems[2].SemanticName = "NORMAL";
    elems[2].SemanticIndex = 0;
    elems[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = elems;
    inputLayout.NumElements = _countof(elems);

    // ラスタライザ：裏面カリング（back-face culling）。オブジェクト底面の自己シャドウを
    // 正しく出すためライトを向いた面を書く。平坦面のアクネは受光側の法線オフセットで抑える。
    // 斜面バイアスは傾いた面のアクネ補助。
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.DepthBias = 5000;
    rasterizer.DepthBiasClamp = 0.0f;
    rasterizer.SlopeScaledDepthBias = 2.0f;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = FALSE;

    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    // PS なし（深度のみ）
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = dsDesc;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 0; // カラー出力なし
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}

void ShadowMap::TransitionState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES afterState)
{
    if (currentState_ == afterState) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthArray_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = currentState_;
    barrier.Transition.StateAfter = afterState;
    commandList->ResourceBarrier(1, &barrier);

    currentState_ = afterState;
}

void ShadowMap::BeginPass(ID3D12GraphicsCommandList* commandList)
{
    TransitionState(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    commandList->SetPipelineState(pipelineState_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 全カスケード共通（同解像度）のビューポート・シザー
    D3D12_VIEWPORT viewport{ 0.0f, 0.0f,
        static_cast<float>(kResolution), static_cast<float>(kResolution), 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(kResolution), static_cast<LONG>(kResolution) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
}

void ShadowMap::BindCascade(ID3D12GraphicsCommandList* commandList, uint32_t cascadeIndex)
{
    assert(cascadeIndex < kShadowCascadeCount);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    dsvHandle.ptr += static_cast<SIZE_T>(cascadeIndex) * dsvDescriptorSize_;

    // カラーなし・深度のみをバインドしてクリア
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // このカスケードのライト ViewProj をルート定数(b1)へ
    commandList->SetGraphicsRoot32BitConstants(1, 16, &cascadeViewProj_[cascadeIndex], 0);
}

void ShadowMap::EndPass(ID3D12GraphicsCommandList* commandList)
{
    TransitionState(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMap::GetSrvGpuHandle() const
{
    return srvManager_->GetGPUDescriptorHandle(srvIndex_);
}

void ShadowMap::UpdateCascades(const Camera& camera, const Vector3& lightDirection)
{
    const float nearClip = camera.GetNearClip();
    const float farClip = camera.GetFarClip();

    // ---- practical split（PSSM）で分割境界（ビュー空間Z）を求める ----
    float splits[kShadowCascadeCount];
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        float p = static_cast<float>(i + 1) / static_cast<float>(kShadowCascadeCount);
        float logSplit = nearClip * std::pow(farClip / nearClip, p);
        float uniSplit = nearClip + (farClip - nearClip) * p;
        splits[i] = cascadeLambda_ * logSplit + (1.0f - cascadeLambda_) * uniSplit;
    }

    // ---- カメラ視錐台の 8 隅をワールド空間で得る（NDC を逆 ViewProj）----
    Matrix4x4 invViewProj = Inverse(camera.GetViewProjectionMatrix());
    auto ndcToWorld = [&](float x, float y, float z) -> Vector3 {
        // 行ベクトル規約：world = ndc * invViewProj、その後 w 除算
        float wx = x * invViewProj.m[0][0] + y * invViewProj.m[1][0] + z * invViewProj.m[2][0] + invViewProj.m[3][0];
        float wy = x * invViewProj.m[0][1] + y * invViewProj.m[1][1] + z * invViewProj.m[2][1] + invViewProj.m[3][1];
        float wz = x * invViewProj.m[0][2] + y * invViewProj.m[1][2] + z * invViewProj.m[2][2] + invViewProj.m[3][2];
        float ww = x * invViewProj.m[0][3] + y * invViewProj.m[1][3] + z * invViewProj.m[2][3] + invViewProj.m[3][3];
        return { wx / ww, wy / ww, wz / ww };
    };

    // 近平面(z=0)・遠平面(z=1)の各 4 隅（LH の深度 [0,1]）
    Vector3 nearCorners[4] = {
        ndcToWorld(-1.0f,  1.0f, 0.0f), ndcToWorld(1.0f,  1.0f, 0.0f),
        ndcToWorld(-1.0f, -1.0f, 0.0f), ndcToWorld(1.0f, -1.0f, 0.0f),
    };
    Vector3 farCorners[4] = {
        ndcToWorld(-1.0f,  1.0f, 1.0f), ndcToWorld(1.0f,  1.0f, 1.0f),
        ndcToWorld(-1.0f, -1.0f, 1.0f), ndcToWorld(1.0f, -1.0f, 1.0f),
    };

    Vector3 lightDirN = Normalize(lightDirection);
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    if (std::fabs(Dot(lightDirN, up)) > 0.99f) up = { 0.0f, 0.0f, 1.0f };

    float prevSplit = nearClip;
    for (uint32_t c = 0; c < kShadowCascadeCount; ++c) {
        const float splitNear = prevSplit;
        const float splitFar = splits[c];

        // 視錐台の側辺は直線なので、ビュー深度比で near 隅↔far 隅を補間してスライスの 8 隅を得る
        const float tNear = (splitNear - nearClip) / (farClip - nearClip);
        const float tFar = (splitFar - nearClip) / (farClip - nearClip);

        Vector3 sliceCorners[8];
        for (int i = 0; i < 4; ++i) {
            sliceCorners[i] = Lerp(nearCorners[i], farCorners[i], tNear);
            sliceCorners[i + 4] = Lerp(nearCorners[i], farCorners[i], tFar);
        }

        // スライスのバウンディング球（中心＝重心、半径＝最遠隅まで）でサイズを固定し
        // カメラ回転によるシャドウのちらつきを抑える
        Vector3 center{ 0.0f, 0.0f, 0.0f };
        for (const Vector3& v : sliceCorners) {
            center.x += v.x; center.y += v.y; center.z += v.z;
        }
        center.x /= 8.0f; center.y /= 8.0f; center.z /= 8.0f;

        float radius = 0.0f;
        for (const Vector3& v : sliceCorners) {
            Vector3 d{ v.x - center.x, v.y - center.y, v.z - center.z };
            float len = Length(d);
            if (len > radius) radius = len;
        }

        // ライト視点：球の中心を見下ろす。near 側に radius 分引いて配置
        Vector3 eye{ center.x - lightDirN.x * radius,
                     center.y - lightDirN.y * radius,
                     center.z - lightDirN.z * radius };
        Matrix4x4 lightView = MakeLookAtMatrix(eye, center, up);

        // ライト空間 [-radius, radius] の正射影。near は球の外側のキャスターも拾えるよう手前へ広げる
        const float backMargin = radius; // 球とライトの間にあるキャスターを拾う余白
        Matrix4x4 lightProj = MakeOrthographicMatrix(
            -radius, radius, radius, -radius, -backMargin, radius * 2.0f);

        Matrix4x4 viewProj = Multiply(lightView, lightProj);

        cascadeViewProj_[c] = viewProj;                       // シャドウパス(b1)用
        mappedConstants_->cascadeViewProj[c] = viewProj;      // 受光パス(b5)用

        prevSplit = splitFar;
    }

    // カスケード選択に使う遠端のビュー空間Z（受光 PS は SV_Position.w と比較）
    mappedConstants_->cascadeSplitsView = {
        splits[0],
        kShadowCascadeCount > 1 ? splits[1] : splits[0],
        kShadowCascadeCount > 2 ? splits[2] : splits[kShadowCascadeCount - 1],
        0.0f
    };
    mappedConstants_->shadowBias = bias_;
    mappedConstants_->normalOffset = normalOffset_;
    mappedConstants_->enabled = enabled_ ? 1.0f : 0.0f;
    mappedConstants_->softness = softness_;
    mappedConstants_->debug = debugShadowOnly_ ? 1.0f : 0.0f;
    mappedConstants_->maxBlur = maxBlur_;
    mappedConstants_->fadeRadius = fadeRadius_;
}

void ShadowMap::OnImGui()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("Shadow (CSM)")) {
        ImGui::Checkbox("Enable Shadow", &enabled_);
        ImGui::DragFloat("Depth Bias", &bias_, 0.0001f, 0.0f, 0.05f, "%.4f");
        ImGui::DragFloat("Normal Offset", &normalOffset_, 0.005f, 0.0f, 2.0f, "%.3f");
        // 距離で変化するボケ（PCSS）：Softness=変化の強さ、Max Blur=ボケ上限(washout防止)
        ImGui::SliderFloat("Softness", &softness_, 0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("Max Blur", &maxBlur_, 0.0f, 0.1f, "%.4f");
        // 距離フェード：小さいほど近くで影が消える。0でフェードOFF（消えない）
        ImGui::SliderFloat("Fade Dist", &fadeRadius_, 0.0f, 2.0f, "%.3f");
        ImGui::SliderFloat("Cascade Lambda", &cascadeLambda_, 0.0f, 1.0f);
        // デバッグ：影係数をそのままグレースケール表示（ボケ具合の確認用）
        ImGui::Checkbox("Debug: Shadow Only", &debugShadowOnly_);
        ImGui::TextDisabled("cascades=%u  res=%u  PCSS 7x7", kShadowCascadeCount, kResolution);
    }
#endif
}
