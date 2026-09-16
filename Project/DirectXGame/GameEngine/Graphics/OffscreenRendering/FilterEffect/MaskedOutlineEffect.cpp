#include "MaskedOutlineEffect.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include "MathUtility.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void MaskedOutlineEffect::InitializeMaskedOutline(
	DirectXCore* dxCore,
	ID3D12RootSignature* maskedOutlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
	RenderTexture* idMaskRT)
{
	idMaskRT_ = idMaskRT;

	IDxcBlob* psBlob = dxCore->LoadShaderBlob(
		L"Resources/Shaders/PostEffect/Filters/MaskedOutlineNormal.PS.hlsl",
		L"ps_6_0");
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = maskedOutlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(MaskedOutlineParamsCB));
	UpdateConstantBuffer();
}

void MaskedOutlineEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	MaskedOutlineParamsCB cb{};
	cb.projectionInverse = projectionInverse_;
	cb.colorFire = colorFire_;
	cb.colorIce = colorIce_;
	cb.depthWeight = depthWeight_;
	cb.normalWeight = normalWeight_;
	cb.depthThreshold = depthThreshold_;
	cb.normalThreshold = normalThreshold_;
	cb.edgeStrength = edgeStrength_;
	cb.time = time_;
	cb.blinkHz = blinkHz_;
	cb.minIntensity = std::clamp(minIntensity_, 0.0f, 1.0f);
	std::memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void MaskedOutlineEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::ColorEdit4("Fire Color##MaskedOutline", &colorFire_.x);
	ImGui::ColorEdit4("Ice Color##MaskedOutline", &colorIce_.x);
	ImGui::SliderFloat("Edge Strength##MaskedOutline", &edgeStrength_, 0.0f, 8.0f);
	ImGui::SliderFloat("Depth Weight##MaskedOutline", &depthWeight_, 0.0f, 30.0f);
	ImGui::SliderFloat("Normal Weight##MaskedOutline", &normalWeight_, 0.0f, 30.0f);
	ImGui::SliderFloat("Depth Threshold##MaskedOutline", &depthThreshold_, 0.0f, 0.5f);
	ImGui::SliderFloat("Normal Threshold##MaskedOutline", &normalThreshold_, 0.0f, 0.5f);
	ImGui::SliderFloat("Blink Hz##MaskedOutline", &blinkHz_, 0.0f, 12.0f);
	ImGui::SliderFloat("Min Intensity##MaskedOutline", &minIntensity_, 0.0f, 1.0f);
	ImGui::TextDisabled("mask 1=Fire, 2=Ice");
#endif
}

void MaskedOutlineEffect::ResetParams()
{
	colorFire_ = { 1.0f, 0.0f, 0.0f, 1.0f };
	colorIce_ = { 0.0f, 0.0f, 1.0f, 1.0f };
	depthWeight_ = 6.0f;
	normalWeight_ = 1.0f;
	depthThreshold_ = 0.0f;
	normalThreshold_ = 0.0f;
	edgeStrength_ = 1.5f;
	blinkHz_ = 3.0f;
	minIntensity_ = 0.35f;
}

void MaskedOutlineEffect::SetProjectionMatrix(const Matrix4x4& projection)
{
	projectionInverse_ = Inverse(projection);
}

uint32_t MaskedOutlineEffect::GetMaskTextureSRVIndex() const
{
	return idMaskRT_ ? idMaskRT_->GetSRVIndex() : 0u;
}
