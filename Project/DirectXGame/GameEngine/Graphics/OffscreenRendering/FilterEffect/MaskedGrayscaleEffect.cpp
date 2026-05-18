#include "MaskedGrayscaleEffect.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void MaskedGrayscaleEffect::InitializeMasked(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
	RenderTexture* idMaskRT)
{
	idMaskRT_ = idMaskRT;

	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/MaskedGrayscale.PS.hlsl",
		L"ps_6_0");
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = outlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(ParamsCB));
	UpdateConstantBuffer();
}

void MaskedGrayscaleEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;
	ParamsCB cb;
	cb.intensity = std::clamp(intensity_, 0.0f, 1.0f);
	std::memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void MaskedGrayscaleEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Intensity##MaskedGrayscale", &intensity_, 0.0f, 1.0f);
	ImGui::TextDisabled("mask=0: grayscale, else: keep color");
#endif
}

void MaskedGrayscaleEffect::ResetParams()
{
	intensity_ = 1.0f;
}

uint32_t MaskedGrayscaleEffect::GetMaskTextureSRVIndex() const
{
	return idMaskRT_ ? idMaskRT_->GetSRVIndex() : 0u;
}
