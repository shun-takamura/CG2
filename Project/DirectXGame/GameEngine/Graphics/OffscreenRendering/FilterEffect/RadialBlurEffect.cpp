#include "RadialBlurEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void RadialBlurEffect::InitializeDepth(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->LoadShaderBlob(
		L"Resources/Shaders/PostEffect/Filters/RadialBlur.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = outlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(RadialBlurParamsCB));
	UpdateConstantBuffer();
}

void RadialBlurEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	RadialBlurParamsCB cb{};
	cb.center[0] = centerArray_[0];
	cb.center[1] = centerArray_[1];
	cb.blurWidth = blurWidth_;
	cb.numSamples = numSamples_;
	cb.depthThreshold = depthThreshold_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void RadialBlurEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat2("Center##RadialBlur", centerArray_, 0.0f, 1.0f);
	ImGui::SliderFloat("Blur Width##RadialBlur", &blurWidth_, 0.0f, 0.1f, "%.4f");
	ImGui::SliderInt("Num Samples##RadialBlur", &numSamples_, 1, 32);
	ImGui::SliderFloat("Depth Threshold##RadialBlur", &depthThreshold_, 0.0f, 1.0f, "%.5f");
#endif
}

void RadialBlurEffect::ResetParams()
{
	centerArray_[0] = 0.5f;
	centerArray_[1] = 0.5f;
	blurWidth_ = 0.01f;
	numSamples_ = 10;
	depthThreshold_ = 0.0f;
}

float RadialBlurEffect::DepthAtDistance(float distance, float nearClip, float farClip)
{
	// MakePerspectiveFovMatrix（z' = f/(f-n)·z − n·f/(f-n)、w = z）の深度
	if (distance <= nearClip || farClip <= nearClip) return 0.0f;
	return farClip / (farClip - nearClip) * (1.0f - nearClip / distance);
}

void RadialBlurEffect::SetCenter(float x, float y)
{
	centerArray_[0] = x;
	centerArray_[1] = y;
}

void RadialBlurEffect::SetBlurWidth(float width)
{
	blurWidth_ = width < 0.0f ? 0.0f : width;
}

void RadialBlurEffect::SetNumSamples(int samples)
{
	if (samples < 1) samples = 1;
	if (samples > 32) samples = 32;
	numSamples_ = samples;
}
