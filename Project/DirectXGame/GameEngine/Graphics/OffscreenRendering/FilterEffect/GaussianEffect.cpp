#include "GaussianEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#include<cmath>
#define _USE_MATH_DEFINES

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void GaussianEffect::Initialize(
	DirectXCore* dxCore,
	ID3D12RootSignature* copyRootSignature,
	ID3D12RootSignature* effectRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	// シェーダーコンパイル
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/GaussianFilter.PS.hlsl",
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
	CreateConstantBuffer(dxCore, sizeof(GaussianParamsCB));

	// 初期値を書き込み
	UpdateConstantBuffer();
}

void GaussianEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	GaussianParamsCB cb;
	cb.kernelSize = kernelSize_;
	cb.sigma = sigma_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void GaussianEffect::ShowImGui()
{
#ifdef USE_IMGUI
	if (ImGui::SliderInt("Kernel Size##Gaussian", &kernelSize_, 1, 15)) {
		// 奇数に補正
		if (kernelSize_ % 2 == 0) kernelSize_ += 1;
	}
	ImGui::SliderFloat("Sigma##Gaussian", &sigma_, 0.1f, 10.0f);
#endif
}

void GaussianEffect::ResetParams()
{
	kernelSize_ = 3;
	sigma_ = 1.0f;
}

void GaussianEffect::SetKernelSize(int size)
{
	if (size % 2 == 0) size += 1;
	if (size < 1) size = 1;
	kernelSize_ = size;
}

void GaussianEffect::SetSigma(float sigma)
{
    sigma_ = (std::max)(0.1f, sigma);
}