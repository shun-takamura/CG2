#include "SmoothingEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#include<cmath>
#define _USE_MATH_DEFINES

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void SmoothingEffect::Initialize(
	DirectXCore* dxCore,
	ID3D12RootSignature* copyRootSignature,
	ID3D12RootSignature* effectRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
	// シェーダーコンパイル
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/Smoothing.PS.hlsl",
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

	// 定数バッファ作成（基底クラスのヘルパーを使う）
	CreateConstantBuffer(dxCore, sizeof(SmootingParamsCB));

	// 初期値を書き込み
	UpdateConstantBuffer();
}

void SmoothingEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	SmootingParamsCB cb;
	cb.kernelSize = kernelSize_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void SmoothingEffect::ShowImGui()
{
#ifdef USE_IMGUI
	if (ImGui::SliderInt("Kernel Size##Smoothing", &kernelSize_, 1, 15)) {
		// 奇数に補正
		if (kernelSize_ % 2 == 0) kernelSize_ += 1;
	}
#endif
}

void SmoothingEffect::ResetParams()
{
	kernelSize_ = 3;
}

void SmoothingEffect::SetKernelSize(int size)
{
	if (size % 2 == 0) size += 1;
	if (size < 1) size = 1;
	kernelSize_ = size;
}