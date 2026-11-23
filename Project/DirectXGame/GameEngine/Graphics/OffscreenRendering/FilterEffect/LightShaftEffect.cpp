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
	// ResetParams で戻す既定値
	constexpr float kDefaultLightU = 0.5f;           // 光源の画面上の位置 U（真ん中）
	constexpr float kDefaultLightV = 0.5f;           // 光源の画面上の位置 V（真ん中）
	constexpr float kDefaultColorG = 0.92f;          // 光の色の G（R は 1。少し暖色）
	constexpr float kDefaultColorB = 0.78f;          // 光の色の B
	constexpr float kDefaultDensity = 0.9f;          // 光の筋を伸ばす長さの割合
	constexpr float kDefaultDecay = 0.97f;           // 1 サンプルごとの減衰
	constexpr float kDefaultWeight = 0.04f;          // 1 サンプルの重み
	constexpr uint32_t kDefaultSampleCount = 64;     // サンプル数
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
	lightUV_ = { kDefaultLightU, kDefaultLightV };
	color_ = { 1.0f, kDefaultColorG, kDefaultColorB };
	intensity_ = 1.0f;
	density_ = kDefaultDensity;
	decay_ = kDefaultDecay;
	weight_ = kDefaultWeight;
	sampleCount_ = kDefaultSampleCount;
	targetId_ = 1;
}

uint32_t LightShaftEffect::GetMaskTextureSRVIndex() const
{
	return idMaskRT_ ? idMaskRT_->GetSRVIndex() : 0u;
}
