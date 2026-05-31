#include "DisruptorRevealEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void DisruptorRevealEffect::Initialize(
	DirectXCore* dxCore,
	ID3D12RootSignature* /*copyRootSignature*/,
	ID3D12RootSignature* effectRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/DisruptorReveal.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = effectRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(ParamsCB));
	UpdateConstantBuffer();
}

void DisruptorRevealEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	ParamsCB cb;
	cb.lineP0[0] = lineP0_[0]; cb.lineP0[1] = lineP0_[1];
	cb.lineP1[0] = lineP1_[0]; cb.lineP1[1] = lineP1_[1];
	cb.revealT = std::clamp(revealT_, 0.0f, 1.0f);
	cb.edgeSoftness = (std::max)(edgeSoftness_, 0.0f);
	cb.intensity = std::clamp(intensity_, 0.0f, 1.0f);
	cb.aspect = aspect_;
	std::memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void DisruptorRevealEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Reveal T##DisruptorReveal", &revealT_, 0.0f, 1.0f);
	ImGui::SliderFloat("Edge Softness##DisruptorReveal", &edgeSoftness_, 0.0f, 0.3f);
	ImGui::SliderFloat("Intensity##DisruptorReveal", &intensity_, 0.0f, 1.0f);
	ImGui::DragFloat2("Line P0 (uv)##DisruptorReveal", lineP0_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat2("Line P1 (uv)##DisruptorReveal", lineP1_, 0.01f, 0.0f, 1.0f);
	ImGui::TextDisabled("revealT=0: 全画面反転 / 1: 全画面通常");
#endif
}

void DisruptorRevealEffect::ResetParams()
{
	lineP0_[0] = 0.5f; lineP0_[1] = 0.5f;
	lineP1_[0] = 1.0f; lineP1_[1] = 0.5f;
	revealT_ = 0.0f;
	edgeSoftness_ = 0.03f;
	intensity_ = 1.0f;
}
