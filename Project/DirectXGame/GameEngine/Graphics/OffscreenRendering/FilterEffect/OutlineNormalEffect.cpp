#include "OutlineNormalEffect.h"
#include "DirectXCore.h"
#include "MathUtility.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void OutlineNormalEffect::InitializeOutline(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/OutlineNormal.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = outlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(OutlineNormalParamsCB));
	UpdateConstantBuffer();
}

void OutlineNormalEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	OutlineNormalParamsCB cb{};
	cb.projectionInverse = projectionInverse_;
	cb.depthWeight = depthWeight_;
	cb.normalWeight = normalWeight_;
	cb.depthThreshold = depthThreshold_;
	cb.normalThreshold = normalThreshold_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void OutlineNormalEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Depth Weight##OutlineNormal", &depthWeight_, 0.0f, 30.0f);
	ImGui::SliderFloat("Normal Weight##OutlineNormal", &normalWeight_, 0.0f, 30.0f);
	ImGui::SliderFloat("Depth Threshold##OutlineNormal", &depthThreshold_, 0.0f, 0.5f);
	ImGui::SliderFloat("Normal Threshold##OutlineNormal", &normalThreshold_, 0.0f, 0.5f);
#endif
}

void OutlineNormalEffect::ResetParams()
{
	depthWeight_ = 6.0f;
	normalWeight_ = 1.0f;
	depthThreshold_ = 0.0f;
	normalThreshold_ = 0.0f;
}

void OutlineNormalEffect::SetProjectionMatrix(const Matrix4x4& projection)
{
	projectionInverse_ = Inverse(projection);
}

static inline float ClampPositive(float v) { return v < 0.0f ? 0.0f : v; }
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void OutlineNormalEffect::SetDepthWeight(float v) { depthWeight_ = ClampPositive(v); }
void OutlineNormalEffect::SetNormalWeight(float v) { normalWeight_ = ClampPositive(v); }
void OutlineNormalEffect::SetDepthThreshold(float v) { depthThreshold_ = Clamp01(v); }
void OutlineNormalEffect::SetNormalThreshold(float v) { normalThreshold_ = Clamp01(v); }
