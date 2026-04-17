#include "GrayscaleEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GrayscaleEffect::Initialize(
	DirectXCore* dxCore,
	ID3D12RootSignature* copyRootSignature,
	ID3D12RootSignature* effectRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	// シェーダーコンパイル
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/Grayscale.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	// パイプライン作成（cbufferありなのでeffectRootSignatureを使う）
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = effectRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	// 定数バッファ作成
	CreateConstantBuffer(dxCore, sizeof(GrayscaleParamsCB));

	// 初期値を書き込み
	UpdateConstantBuffer();
}

void GrayscaleEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	GrayscaleParamsCB cb;
	cb.intensity = intensity_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void GrayscaleEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Intensity##Grayscale", &intensity_, 0.0f, 1.0f);
#endif
}

void GrayscaleEffect::ResetParams()
{
	intensity_ = 1.0f;
}

void GrayscaleEffect::SetIntensity(float intensity)
{
	intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}
