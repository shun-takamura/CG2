#include "GlitchEffect.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GlitchEffect::InitializeMasked(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
	RenderTexture* idMaskRT)
{
	dxCore_ = dxCore;
	idMaskRT_ = idMaskRT;

	IDxcBlob* psBlob = dxCore->LoadShaderBlob(
		L"Resources/Shaders/PostEffect/Filters/Glitch.PS.hlsl",
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

void GlitchEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;
	ParamsCB cb{};
	cb.intensity = std::clamp(intensity_, 0.0f, 1.0f);
	const float interval = (std::max)(params_.jitterInterval, 0.001f);
	cb.seed = std::floor(time_ / interval);
	cb.maskMode = static_cast<int>(maskMode_);
	cb.aspect = 16.0f / 9.0f;
	if (dxCore_ && dxCore_->GetRenderHeight() > 0) {
		cb.aspect = static_cast<float>(dxCore_->GetRenderWidth()) / static_cast<float>(dxCore_->GetRenderHeight());
	}
	cb.bandSlope = params_.bandSlope;
	cb.bandCount = (std::max)(params_.bandCount, 1.0f);
	cb.bandChance = params_.bandChance;
	cb.maxShift = params_.maxShift;
	cb.rgbSplit = params_.rgbSplit;
	cb.blockSize = (std::max)(params_.blockSize, 1.0f);
	cb.blockChance = params_.blockChance;
	cb.tintStrength = params_.tintStrength;
	std::memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void GlitchEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Intensity##Glitch", &intensity_, 0.0f, 1.0f);
	int mode = static_cast<int>(maskMode_);
	const char* modes[] = { "Screen", "Inside mask", "Outside mask" };
	if (ImGui::Combo("Mask##Glitch", &mode, modes, IM_ARRAYSIZE(modes))) maskMode_ = static_cast<MaskMode>(mode);
	// 呼ぶ側が時間を進めていないとき（ImGui で試すとき）も動いて見えるように
	static bool autoTime = true;
	ImGui::Checkbox("Auto Time##Glitch", &autoTime);
	if (autoTime) time_ += ImGui::GetIO().DeltaTime;
	ImGui::SliderFloat("Band Slope##Glitch", &params_.bandSlope, -3.0f, 3.0f);
	ImGui::SliderFloat("Band Count##Glitch", &params_.bandCount, 1.0f, 80.0f);
	ImGui::SliderFloat("Band Chance##Glitch", &params_.bandChance, 0.0f, 1.0f);
	ImGui::SliderFloat("Max Shift##Glitch", &params_.maxShift, 0.0f, 0.3f);
	ImGui::SliderFloat("RGB Split##Glitch", &params_.rgbSplit, 0.0f, 0.05f);
	ImGui::SliderFloat("Block Size##Glitch", &params_.blockSize, 4.0f, 256.0f);
	ImGui::SliderFloat("Block Chance##Glitch", &params_.blockChance, 0.0f, 1.0f);
	ImGui::SliderFloat("Tint##Glitch", &params_.tintStrength, 0.0f, 1.0f);
	ImGui::SliderFloat("Jitter Interval##Glitch", &params_.jitterInterval, 0.01f, 0.3f);
#endif
}

void GlitchEffect::ResetParams()
{
	intensity_ = 0.0f;
	maskMode_ = MaskMode::Screen;
	params_ = Params{};
}

uint32_t GlitchEffect::GetMaskTextureSRVIndex() const
{
	return idMaskRT_ ? idMaskRT_->GetSRVIndex() : 0u;
}
