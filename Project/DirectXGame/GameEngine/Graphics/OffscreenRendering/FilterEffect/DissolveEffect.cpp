#include "DissolveEffect.h"
#include "DirectXCore.h"
#include "TextureManager.h"
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void DissolveEffect::InitializeMasked(
	DirectXCore* dxCore,
	ID3D12RootSignature* maskedRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/Dissolve.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = maskedRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(DissolveParamsCB));

	// デフォルトのマスクをロード（SRGB変換させず線形値として扱う）
	auto* tm = TextureManager::GetInstance();
	if (!tm->HasTexture(maskTexturePath_)) {
		tm->LoadTextureLinear(maskTexturePath_);
	}
	maskSrvIndex_ = tm->GetSrvIndex(maskTexturePath_);

	UpdateConstantBuffer();
}

void DissolveEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	DissolveParamsCB cb{};
	cb.threshold = threshold_;
	cb.edgeWidth = edgeWidth_;
	cb.edgeColor[0] = edgeColor_[0];
	cb.edgeColor[1] = edgeColor_[1];
	cb.edgeColor[2] = edgeColor_[2];
	cb.edgeColor[3] = edgeIntensity_;
	cb.fillColor[0] = fillColor_[0];
	cb.fillColor[1] = fillColor_[1];
	cb.fillColor[2] = fillColor_[2];
	cb.fillColor[3] = fillIntensity_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void DissolveEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Threshold##Dissolve", &threshold_, 0.0f, 1.0f);
	ImGui::SliderFloat("Edge Width##Dissolve", &edgeWidth_, 0.0f, 0.2f, "%.3f");
	ImGui::ColorEdit3("Edge Color##Dissolve", edgeColor_);
	ImGui::SliderFloat("Edge Intensity##Dissolve", &edgeIntensity_, 0.0f, 5.0f);

	ImGui::Separator();
	ImGui::ColorEdit3("Fill Color##Dissolve", fillColor_);
	ImGui::SliderFloat("Fill Intensity##Dissolve", &fillIntensity_, 0.0f, 5.0f);
	if (ImGui::Button("Copy Edge Color##Dissolve")) {
		fillColor_[0] = edgeColor_[0];
		fillColor_[1] = edgeColor_[1];
		fillColor_[2] = edgeColor_[2];
		fillIntensity_ = edgeIntensity_;
	}

	ImGui::Separator();
	ImGui::TextDisabled("Mask: %s", maskTexturePath_.c_str());
	if (ImGui::Button("noise0##Dissolve")) {
		SetMaskTexture("Resources/Textures/MaskTexture/noise0.png");
	}
	ImGui::SameLine();
	if (ImGui::Button("noise1##Dissolve")) {
		SetMaskTexture("Resources/Textures/MaskTexture/noise1.png");
	}
#endif
}

void DissolveEffect::ResetParams()
{
	threshold_ = 0.0f;
	edgeWidth_ = 0.03f;
	edgeColor_[0] = 1.0f;
	edgeColor_[1] = 0.4f;
	edgeColor_[2] = 0.3f;
	edgeIntensity_ = 1.0f;
	fillColor_[0] = 0.0f;
	fillColor_[1] = 0.0f;
	fillColor_[2] = 0.0f;
	fillIntensity_ = 1.0f;
}

void DissolveEffect::SetThreshold(float v)
{
	threshold_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void DissolveEffect::SetEdgeWidth(float v)
{
	edgeWidth_ = v < 0.0f ? 0.0f : v;
}

void DissolveEffect::SetEdgeColor(float r, float g, float b)
{
	edgeColor_[0] = r;
	edgeColor_[1] = g;
	edgeColor_[2] = b;
}

void DissolveEffect::SetEdgeIntensity(float v)
{
	edgeIntensity_ = v < 0.0f ? 0.0f : v;
}

void DissolveEffect::SetFillColor(float r, float g, float b)
{
	fillColor_[0] = r;
	fillColor_[1] = g;
	fillColor_[2] = b;
}

void DissolveEffect::SetFillIntensity(float v)
{
	fillIntensity_ = v < 0.0f ? 0.0f : v;
}

void DissolveEffect::SetMaskTexture(const std::string& texturePath)
{
	auto* tm = TextureManager::GetInstance();
	if (!tm->HasTexture(texturePath)) {
		tm->LoadTextureLinear(texturePath);
	}
	maskTexturePath_ = texturePath;
	maskSrvIndex_ = tm->GetSrvIndex(texturePath);
}
