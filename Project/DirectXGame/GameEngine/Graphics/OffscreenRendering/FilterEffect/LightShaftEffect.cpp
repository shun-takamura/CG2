#include "LightShaftEffect.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// シェーダのループ上限（LightShaft.PS.hlsl の kMaxSamples と合わせる）
	constexpr uint32_t kMaxSamples = 128;
}

void LightShaftEffect::InitializeMasked(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
	RenderTexture* idMaskRT)
{
	idMaskRT_ = idMaskRT;

	IDxcBlob* psBlob = dxCore->LoadShaderBlob(
		L"Resources/Shaders/PostEffect/Filters/LightShaft.PS.hlsl",
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

void LightShaftEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;
	ParamsCB cb{};
	cb.lightUV = lightUV_;
	cb.density = (std::max)(density_, 0.0f);
	cb.decay = std::clamp(decay_, 0.0f, 1.0f);
	cb.color = color_;
	cb.weight = (std::max)(weight_, 0.0f);
	cb.intensity = (std::max)(intensity_, 0.0f);
	cb.sampleCount = std::clamp(sampleCount_, 1u, kMaxSamples);
	cb.targetId = targetId_;
	std::memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void LightShaftEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::DragFloat2("Light UV##LightShaft", &lightUV_.x, 0.005f);
	ImGui::ColorEdit3("Color##LightShaft", &color_.x);
	ImGui::SliderFloat("Intensity##LightShaft", &intensity_, 0.0f, 4.0f);
	ImGui::SliderFloat("Density##LightShaft", &density_, 0.0f, 1.5f);
	ImGui::SliderFloat("Decay##LightShaft", &decay_, 0.8f, 1.0f, "%.4f");
	ImGui::SliderFloat("Weight##LightShaft", &weight_, 0.0f, 0.2f, "%.4f");
	int samples = static_cast<int>(sampleCount_);
	if (ImGui::SliderInt("Samples##LightShaft", &samples, 8, static_cast<int>(kMaxSamples))) {
		sampleCount_ = static_cast<uint32_t>(samples);
	}
	int target = static_cast<int>(targetId_);
	if (ImGui::SliderInt("Target ID##LightShaft", &target, 1, 255)) {
		targetId_ = static_cast<uint32_t>(target);
	}
#endif
}

void LightShaftEffect::ResetParams()
{
	lightUV_ = { 0.5f, 0.5f };
	color_ = { 1.0f, 0.92f, 0.78f };
	intensity_ = 1.0f;
	density_ = 0.9f;
	decay_ = 0.97f;
	weight_ = 0.04f;
	sampleCount_ = 64;
	targetId_ = 1;
}

uint32_t LightShaftEffect::GetMaskTextureSRVIndex() const
{
	return idMaskRT_ ? idMaskRT_->GetSRVIndex() : 0u;
}
