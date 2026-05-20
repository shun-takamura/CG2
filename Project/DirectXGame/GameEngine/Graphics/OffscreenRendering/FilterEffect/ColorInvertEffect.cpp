#include "ColorInvertEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void ColorInvertEffect::Initialize(
	DirectXCore* dxCore,
	ID3D12RootSignature* copyRootSignature,
	ID3D12RootSignature* effectRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/ColorInvert.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = effectRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(ColorInvertParamsCB));
	UpdateConstantBuffer();
}

void ColorInvertEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	ColorInvertParamsCB cb;
	cb.intensity = intensity_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void ColorInvertEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Intensity##ColorInvert", &intensity_, 0.0f, 1.0f);
#endif
}

void ColorInvertEffect::ResetParams()
{
	intensity_ = 1.0f;
}

void ColorInvertEffect::SetIntensity(float intensity)
{
	intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}
