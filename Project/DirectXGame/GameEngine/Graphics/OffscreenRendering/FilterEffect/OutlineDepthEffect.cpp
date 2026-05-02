#include "OutlineDepthEffect.h"
#include "DirectXCore.h"
#include "MathUtility.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void OutlineDepthEffect::InitializeOutline(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
)
{
	IDxcBlob* psBlob = dxCore->CompileShader(
		L"Resources/Shaders/PostEffect/Filters/OutlineDepth.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = outlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(OutlineDepthParamsCB));
	UpdateConstantBuffer();
}

void OutlineDepthEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	OutlineDepthParamsCB cb{};
	cb.projectionInverse = projectionInverse_;
	cb.edgeStrength = edgeStrength_;
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void OutlineDepthEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Edge Strength##OutlineDepth", &edgeStrength_, 0.0f, 30.0f);
#endif
}

void OutlineDepthEffect::ResetParams()
{
	edgeStrength_ = 6.0f;
}

void OutlineDepthEffect::SetProjectionMatrix(const Matrix4x4& projection)
{
	projectionInverse_ = Inverse(projection);
}

void OutlineDepthEffect::SetEdgeStrength(float v)
{
	edgeStrength_ = (v < 0.0f) ? 0.0f : v;
}
