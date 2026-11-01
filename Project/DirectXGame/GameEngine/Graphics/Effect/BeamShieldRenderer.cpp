#include "BeamShieldRenderer.h"

#include "DirectXCore.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Log.h"
#include "PepperMacros.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum RootParam : UINT {
		kRootTransform = 0, // VS b0
		kRootParams,        // PS b0
		kRootCount
	};

	// 板は [-1, 1] の正方形 2 枚の三角形。VS が quadRect へ広げる
	struct QuadVertex { float x, y; };
	constexpr QuadVertex kQuad[6] = {
		{ -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f },
		{ -1.0f, -1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f },
	};
}

void BeamShieldRenderer::Initialize(DirectXCore* dxCore)
{
	dxCore_ = dxCore;
	world_ = MakeIdentity4x4();
	CreateRootSignature();
	pipelineState_ = CreatePipelineState(false);
	bloomPipelineState_ = CreatePipelineState(true);
	CreateMesh();

	transformResource_ = dxCore_->CreateBufferResource(sizeof(TransformForGPU));
	transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
	bloomTransformResource_ = dxCore_->CreateBufferResource(sizeof(TransformForGPU));
	bloomTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&bloomTransformData_));
	paramsResource_ = dxCore_->CreateBufferResource(sizeof(ParamsForGPU));
	paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
	bloomParamsResource_ = dxCore_->CreateBufferResource(sizeof(ParamsForGPU));
	bloomParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&bloomParamsData_));
}

void BeamShieldRenderer::CreateRootSignature()
{
	D3D12_ROOT_PARAMETER rootParameters[kRootCount] = {};
	rootParameters[kRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[kRootTransform].Descriptor.ShaderRegister = 0;
	rootParameters[kRootParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParams].Descriptor.ShaderRegister = 0;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	desc.pParameters = rootParameters;
	desc.NumParameters = _countof(rootParameters);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> BeamShieldRenderer::CreatePipelineState(bool bloomPass)
{
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Effect/BeamShield.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Effect/BeamShield.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC inputElements[1] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	// 両面から見える
	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.DepthClipEnable = TRUE;

	D3D12_BLEND_DESC blend{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.SrcBlend = D3D12_BLEND_ONE;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	// 発光 RT は足すだけ。本体は乗算済み α（透ける所でも光は足される）
	rt.DestBlend = bloomPass ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
	rt.DestBlendAlpha = bloomPass ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;

	// 半透明なので深度は見るだけ
	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.InputLayout = { inputElements, _countof(inputElements) };
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	// 発光 RT は BloomEffect::kFormat
	desc.RTVFormats[0] = bloomPass ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
	assert(SUCCEEDED(hr));
	return pipelineState;
}

void BeamShieldRenderer::CreateMesh()
{
	const UINT vbSize = static_cast<UINT>(sizeof(kQuad));
	vertexBuffer_ = dxCore_->CreateBufferResource(vbSize);
	QuadVertex* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	std::copy(std::begin(kQuad), std::end(kQuad), mapped);
	vertexBuffer_->Unmap(0, nullptr);
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = vbSize;
	vertexBufferView_.StrideInBytes = sizeof(QuadVertex);
}

void BeamShieldRenderer::Update(float deltaTime)
{
	time_ += deltaTime;
	// 長く起動していてもノイズの精度が落ちないよう巻き戻す（1000 秒に 1 度、縁と筋が一瞬入れ替わるだけ）
	if (time_ > 1000.0f) time_ -= 1000.0f;
	flash_ *= std::exp(-appearance_.flashDecay * deltaTime);
}

void BeamShieldRenderer::Flash(float strength)
{
	flash_ = (std::max)(flash_, std::clamp(strength, 0.0f, 1.0f));
}

void BeamShieldRenderer::DrawImpl(const Camera& camera, bool bloomPass)
{
	const BeamShieldAppearance& a = appearance_;
	TransformForGPU& t = bloomPass ? *bloomTransformData_ : *transformData_;
	t.WVP = Multiply(world_, camera.GetViewProjectionMatrix());
	// 形の外側に、縁のゆらぎ（被弾の光りで広がる分も）と縁のにじみの余白を取る
	const float margin = a.edgeNoiseAmp * (1.0f + a.flashEdgeNoise) + a.edgeWidth + 0.05f;
	t.quadRect[0] = -a.halfWidth - margin;
	t.quadRect[1] = a.bottomY * a.halfHeight - margin;
	t.quadRect[2] = a.halfWidth + margin;
	t.quadRect[3] = a.topY * a.halfHeight + margin;

	ParamsForGPU& p = bloomPass ? *bloomParamsData_ : *paramsData_;
	p = ParamsForGPU{
		a.halfWidth, a.halfHeight, a.topY, a.shoulderY,
		a.waistX, a.waistY, a.bottomY, time_,
		a.edgeNoiseAmp, a.edgeNoiseFreq, a.edgeNoiseSpeed, a.streakCount,
		a.streakSharpness, a.streakSpeed, a.streakIntensity, a.coreRadius,
		a.coreAlpha, a.edgeWidth, a.edgeAlpha, a.midAlpha,
		a.color, a.intensity,
		a.edgeColor, flash_,
		a.flashEdgeNoise, a.bloomScale, bloomPass ? 1 : 0, a.flashIntensity,
		a.innerScale, a.innerAlpha, { 0.0f, 0.0f },
	};

	auto* cmd = dxCore_->GetCommandList();
	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(bloomPass ? bloomPipelineState_.Get() : pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
	cmd->SetGraphicsRootConstantBufferView(kRootTransform,
		(bloomPass ? bloomTransformResource_ : transformResource_)->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootParams,
		(bloomPass ? bloomParamsResource_ : paramsResource_)->GetGPUVirtualAddress());

	PEPPER_COUNT("DrawCall");
	cmd->DrawInstanced(_countof(kQuad), 1, 0, 0);
}

void BeamShieldRenderer::Draw(const Camera& camera)
{
	PEPPER_SCOPE("BeamShieldRenderer::Draw");
	DrawImpl(camera, false);
}

void BeamShieldRenderer::DrawBloomPass(const Camera& camera)
{
	DrawImpl(camera, true);
}

bool BeamShieldRenderer::EditAppearance(BeamShieldAppearance& a)
{
	bool changed = false;
#ifdef _DEBUG
	ImGui::SeparatorText("Shape");
	changed |= ImGui::DragFloat("Half Width##Beam", &a.halfWidth, 0.01f, 0.05f, 10.0f);
	changed |= ImGui::DragFloat("Half Height##Beam", &a.halfHeight, 0.01f, 0.05f, 10.0f);
	changed |= ImGui::DragFloat("Top Y##Beam", &a.topY, 0.01f, 0.0f, 3.0f);
	changed |= ImGui::DragFloat("Shoulder Y##Beam", &a.shoulderY, 0.01f, -1.0f, 2.0f);
	changed |= ImGui::DragFloat("Waist X##Beam", &a.waistX, 0.01f, 0.0f, 1.5f);
	changed |= ImGui::DragFloat("Waist Y##Beam", &a.waistY, 0.01f, -3.0f, 1.0f);
	changed |= ImGui::DragFloat("Bottom Y##Beam", &a.bottomY, 0.01f, -3.0f, 0.0f);
	ImGui::SeparatorText("Edge / Streaks");
	changed |= ImGui::DragFloat("Edge Noise Amp##Beam", &a.edgeNoiseAmp, 0.001f, 0.0f, 0.5f);
	changed |= ImGui::DragFloat("Edge Noise Freq##Beam", &a.edgeNoiseFreq, 0.5f, 1.0f, 200.0f);
	changed |= ImGui::DragFloat("Edge Noise Speed##Beam", &a.edgeNoiseSpeed, 0.05f, 0.0f, 50.0f);
	changed |= ImGui::DragFloat("Streak Count##Beam", &a.streakCount, 0.5f, 1.0f, 400.0f);
	changed |= ImGui::DragFloat("Streak Sharpness##Beam", &a.streakSharpness, 0.05f, 0.5f, 40.0f);
	changed |= ImGui::DragFloat("Streak Speed##Beam", &a.streakSpeed, 0.05f, 0.0f, 20.0f);
	changed |= ImGui::DragFloat("Streak Intensity##Beam", &a.streakIntensity, 0.01f, 0.0f, 5.0f);
	ImGui::SeparatorText("Opacity");
	changed |= ImGui::DragFloat("Core Radius##Beam", &a.coreRadius, 0.005f, 0.0f, 3.0f);
	changed |= ImGui::SliderFloat("Core Alpha##Beam", &a.coreAlpha, 0.0f, 1.0f);
	changed |= ImGui::DragFloat("Edge Width##Beam", &a.edgeWidth, 0.002f, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Edge Alpha##Beam", &a.edgeAlpha, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Mid Alpha##Beam", &a.midAlpha, 0.0f, 1.0f);
	changed |= ImGui::DragFloat("Inner Scale##Beam", &a.innerScale, 0.005f, 0.05f, 0.95f);
	changed |= ImGui::SliderFloat("Inner Alpha##Beam", &a.innerAlpha, 0.0f, 1.0f);
	ImGui::SeparatorText("Color");
	changed |= ImGui::ColorEdit3("Color##Beam", &a.color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	changed |= ImGui::ColorEdit3("Edge Color##Beam", &a.edgeColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	changed |= ImGui::DragFloat("Intensity##Beam", &a.intensity, 0.01f, 0.0f, 20.0f);
	changed |= ImGui::DragFloat("Bloom Scale##Beam", &a.bloomScale, 0.01f, 0.0f, 10.0f);
	ImGui::SeparatorText("Hit Flash");
	changed |= ImGui::DragFloat("Flash Intensity##Beam", &a.flashIntensity, 0.01f, 0.0f, 20.0f);
	changed |= ImGui::DragFloat("Flash Edge Noise##Beam", &a.flashEdgeNoise, 0.01f, 0.0f, 10.0f);
	changed |= ImGui::DragFloat("Flash Decay##Beam", &a.flashDecay, 0.05f, 0.1f, 50.0f);
#else
	(void)a;
#endif
	return changed;
}
