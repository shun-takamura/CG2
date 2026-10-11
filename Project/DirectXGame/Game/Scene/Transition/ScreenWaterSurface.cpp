#include "ScreenWaterSurface.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include "Log.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &barrier);
	}
}

void ScreenWaterSurface::Initialize(DirectXCore* dxCore, SRVManager* srvManager, float screenWidth, float screenHeight) {
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;
	assert(dxCore_ && srvManager_);

	CreateCopyTexture();
	ripple_.Initialize(dxCore_, srvManager_);
	ApplyRippleSettings();
}

void ScreenWaterSurface::Finalize() {
	if (srvManager_ && sceneCopySrvIndex_ != UINT32_MAX) {
		srvManager_->Free(sceneCopySrvIndex_);
		sceneCopySrvIndex_ = UINT32_MAX;
	}
	sceneCopy_.Reset();
}

void ScreenWaterSurface::CreateCopyTexture() {
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = static_cast<UINT64>(screenWidth_);
	desc.Height = static_cast<UINT>(screenHeight_);
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = kSceneFormat;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&sceneCopy_));
	assert(SUCCEEDED(hr));
	sceneCopy_->SetName(L"ScreenWaterSurface_SceneCopy");

	sceneCopySrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVForTexture2D(sceneCopySrvIndex_, sceneCopy_.Get(), kSceneFormat, 1);
}

float ScreenWaterSurface::GetAspect() const {
	return screenWidth_ / (std::max)(screenHeight_, 1.0f);
}

Vector2 ScreenWaterSurface::ToWidthSpace(const Vector2& uv) const {
	return { uv.x, uv.y / GetAspect() };
}

void ScreenWaterSurface::ApplyRippleSettings() {
	RippleSimulation::Settings& r = ripple_.GetSettings();
	r.simSize = (std::max)(settings_.simSize, 0.1f);
	r.waveSpeed = settings_.waveSpeed;
	r.damping = settings_.damping;
	r.stepsPerSecond = settings_.stepsPerSecond;
	// 画面の中心を波の範囲の中心に置く（3D の水面として使うときはワールド XZ の中心）
	ripple_.SetCenter(useWorldCenter_ ? worldCenter_ : ToWidthSpace({ 0.5f, 0.5f }));
}

void ScreenWaterSurface::Reset() {
	ripple_.Clear();
	useWorldCenter_ = false;
}

void ScreenWaterSurface::SetRippleWorldCenter(const Vector2& centerXZ) {
	useWorldCenter_ = true;
	worldCenter_ = centerXZ;
	ApplyRippleSettings();
}

void ScreenWaterSurface::AddImpulseWorld(const Vector2& positionXZ, float radius, float strength) {
	ripple_.AddImpulse({ positionXZ.x, 0.0f, positionXZ.y }, radius, strength);
}

void ScreenWaterSurface::AddImpulse(const Vector2& uv, float radius, float strength) {
	// RippleSimulation の Z ＝ 画面の下向き
	const Vector2 p = ToWidthSpace(uv);
	ripple_.AddImpulse({ p.x, 0.0f, p.y }, radius, strength);
}

void ScreenWaterSurface::Update(float deltaTime) {
	ApplyRippleSettings();
	ripple_.Update(deltaTime);
}

void ScreenWaterSurface::BeginDraw(ID3D12GraphicsCommandList* cmd, RenderTexture* sceneTarget, bool captureScene) {
	if (captureScene) {
		Barrier(cmd, sceneTarget->GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		Barrier(cmd, sceneCopy_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		cmd->CopyResource(sceneCopy_.Get(), sceneTarget->GetResource());
		Barrier(cmd, sceneCopy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		Barrier(cmd, sceneTarget->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	srvManager_->PreDraw();
	// 波を進める（コンピュート。グラフィックスのルートシグネチャ・PSO は呼んだ側が貼る）
	ripple_.Dispatch();

	const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = sceneTarget->GetRTVHandle();
	cmd->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(sceneTarget->GetWidth()),
		static_cast<float>(sceneTarget->GetHeight()), 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(sceneTarget->GetWidth()), static_cast<LONG>(sceneTarget->GetHeight()) };
	cmd->RSSetViewports(1, &viewport);
	cmd->RSSetScissorRects(1, &scissor);
}

void ScreenWaterSurface::EndDraw(ID3D12GraphicsCommandList* cmd, RenderTexture* sceneTarget) {
	const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = sceneTarget->GetRTVHandle();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
	cmd->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> ScreenWaterSurface::CreateRootSignature(UINT extraTextureCount) const {
	// テクスチャはヒープ上で並んでいないので1枚ずつ別のテーブルにする
	constexpr UINT kMaxTextures = 8;
	const UINT textureCount = 2 + extraTextureCount;
	assert(textureCount <= kMaxTextures);
	D3D12_DESCRIPTOR_RANGE ranges[kMaxTextures] = {};
	D3D12_ROOT_PARAMETER params[1 + kMaxTextures] = {};
	params[kRootParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootParams].Descriptor.ShaderRegister = 0;
	for (UINT i = 0; i < textureCount; ++i) {
		ranges[i].BaseShaderRegister = i;
		ranges[i].NumDescriptors = 1;
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		D3D12_ROOT_PARAMETER& p = params[kRootSceneCopy + i];
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		p.DescriptorTable.pDescriptorRanges = &ranges[i];
		p.DescriptorTable.NumDescriptorRanges = 1;
	}

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	for (UINT i = 0; i < 2; ++i) {
		samplers[i].ShaderRegister = i;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
	}
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.pParameters = params;
	desc.NumParameters = 1 + textureCount;
	desc.pStaticSamplers = samplers;
	desc.NumStaticSamplers = _countof(samplers);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));
	return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ScreenWaterSurface::CreatePipelineState(
	ID3D12RootSignature* rootSignature, const wchar_t* pixelShaderPath, bool alphaBlend) const {
	// 頂点バッファなしの全画面三角形（ポストエフェクトと共用）
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/PostEffect/Common/PostProcess.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(pixelShaderPath, L"ps_6_0");
	assert(vs && ps);

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_BLEND_DESC blend{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if (alphaBlend) {
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}

	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature;
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = kSceneFormat;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
	assert(SUCCEEDED(hr));
	return pipelineState;
}

void ScreenWaterSurface::BindTextures(ID3D12GraphicsCommandList* cmd) const {
	srvManager_->SetGraphicsRootDescriptorTable(kRootSceneCopy, sceneCopySrvIndex_);
	cmd->SetGraphicsRootDescriptorTable(kRootRipple, ripple_.GetHeightSrvHandle());
}

ScreenWaterSurface::ConstantsForGPU ScreenWaterSurface::GetConstants() const {
	static_assert(sizeof(ConstantsForGPU) == 32, "ScreenWater.hlsli の並びと大きさを合わせる");
	ConstantsForGPU c{};
	c.rippleCenter = ripple_.GetCenter();
	c.rippleSize = ripple_.GetSettings().simSize;
	c.refraction = settings_.refraction;
	const Vector2& l = settings_.lightDirection;
	const float len = std::sqrt(l.x * l.x + l.y * l.y);
	c.lightDirection = (len > 1e-4f) ? Vector2{ l.x / len, l.y / len } : Vector2{ 0.0f, -1.0f };
	c.highlight = settings_.highlight;
	c.aspect = GetAspect();
	return c;
}

void ScreenWaterSurface::OnImGui() {
#ifdef _DEBUG
	Settings& s = settings_;
	ImGui::DragFloat("Sim Size [x screen W]", &s.simSize, 0.05f, 0.5f, 6.0f);
	ImGui::DragFloat("Wave Speed [W/s]", &s.waveSpeed, 0.01f, 0.0f, 3.0f);
	ImGui::SliderFloat("Damping", &s.damping, 0.9f, 1.0f, "%.4f");
	ImGui::DragFloat("Steps / s", &s.stepsPerSecond, 1.0f, 30.0f, 240.0f);
	ImGui::DragFloat("Refraction", &s.refraction, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Highlight", &s.highlight, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat2("Light Direction", &s.lightDirection.x, 0.01f, -1.0f, 1.0f);
#endif
}
