#include "BeamBladeRenderer.h"

#include "DirectXCore.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Log.h"
#include "PepperMacros.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <vector>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum RootParam : UINT {
		kRootTransform = 0, // VS b0
		kRootParams,        // PS b0
		kRootCount
	};

	// 形の係数だけを持つ頂点。w＝幅の向き(-1..1)、t＝厚みの向き(-1..1)、station＝0 根元 / 1 肩 / 2 先
	struct BladeVertex { float w, t, station; };

	// 菱形の断面（幅の縁 → 上の稜線 → 反対の縁 → 下の稜線）
	constexpr float kSection[4][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f }, { 0.0f, -1.0f } };
}

void BeamBladeRenderer::Initialize(DirectXCore* dxCore)
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

void BeamBladeRenderer::CreateRootSignature()
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

Microsoft::WRL::ComPtr<ID3D12PipelineState> BeamBladeRenderer::CreatePipelineState(bool bloomPass)
{
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Effect/BeamBlade.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Effect/BeamBlade.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC inputElements[1] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	// 透けた奥の面も見せる（結晶のような重なり）
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
	// 発光 RT は足すだけ。本体は乗算済み α
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

void BeamBladeRenderer::CreateMesh()
{
	std::vector<BladeVertex> v;
	auto push = [&v](int k, float station) {
		v.push_back({ kSection[k & 3][0], kSection[k & 3][1], station });
	};
	// 根元→肩、肩→先 の側面（面は頂点を分けて持つ。法線は PS が ddx/ddy で出す）
	for (int seg = 0; seg < 2; ++seg) {
		const float sa = static_cast<float>(seg);
		const float sb = static_cast<float>(seg + 1);
		for (int k = 0; k < 4; ++k) {
			push(k, sa); push(k, sb); push(k + 1, sb);
			push(k, sa); push(k + 1, sb); push(k + 1, sa);
		}
	}
	// 根元のふた
	push(0, 0.0f); push(1, 0.0f); push(2, 0.0f);
	push(0, 0.0f); push(2, 0.0f); push(3, 0.0f);

	vertexCount_ = static_cast<UINT>(v.size());
	const UINT vbSize = static_cast<UINT>(sizeof(BladeVertex) * v.size());
	vertexBuffer_ = dxCore_->CreateBufferResource(vbSize);
	BladeVertex* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	std::copy(v.begin(), v.end(), mapped);
	vertexBuffer_->Unmap(0, nullptr);
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = vbSize;
	vertexBufferView_.StrideInBytes = sizeof(BladeVertex);
}

void BeamBladeRenderer::Update(float deltaTime)
{
	const float time = active_ ? appearance_.extendTime : appearance_.retractTime;
	const float step = time > 0.0f ? deltaTime / time : 1.0f;
	extend_ = std::clamp(extend_ + (active_ ? step : -step), 0.0f, 1.0f);
}

void BeamBladeRenderer::SnapExtend(float extend)
{
	extend_ = std::clamp(extend, 0.0f, 1.0f);
}

void BeamBladeRenderer::DrawImpl(const Camera& camera, bool bloomPass)
{
	if (extend_ <= 0.0f) return;
	const BeamBladeAppearance& a = appearance_;
	const float eased = extend_ * extend_ * (3.0f - 2.0f * extend_);

	TransformForGPU& t = bloomPass ? *bloomTransformData_ : *transformData_;
	t.WVP = Multiply(world_, camera.GetViewProjectionMatrix());
	t.World = world_;
	t.length = a.length;
	t.halfWidth = a.halfWidth;
	t.halfThickness = a.halfThickness;
	t.baseWidth = a.baseWidth;
	t.tipLength = std::clamp(a.tipLength, 0.0f, 1.0f);
	t.tipOffset = std::clamp(a.tipOffset, -1.0f, 1.0f);
	t.extend = eased;
	t.ridgeOffset = std::clamp(a.ridgeOffset, -1.0f, 1.0f);
	t.backAnchor = std::clamp(a.backAnchor, 0.0f, 1.0f);
	t.padding0[0] = t.padding0[1] = t.padding0[2] = 0.0f;

	const Matrix4x4& cw = camera.GetWorldMatrix();
	ParamsForGPU& p = bloomPass ? *bloomParamsData_ : *paramsData_;
	p = ParamsForGPU{
		a.coreColor, a.intensity,
		a.edgeColor, a.bodyAlpha,
		{ cw.m[3][0], cw.m[3][1], cw.m[3][2] }, a.edgeAlpha,
		a.edgeSharpness, a.fresnelPower, a.fresnelStrength, a.bloomScale,
		bloomPass ? 1 : 0, { 0.0f, 0.0f, 0.0f },
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
	cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

void BeamBladeRenderer::Draw(const Camera& camera)
{
	PEPPER_SCOPE("BeamBladeRenderer::Draw");
	DrawImpl(camera, false);
}

void BeamBladeRenderer::DrawBloomPass(const Camera& camera)
{
	DrawImpl(camera, true);
}

bool BeamBladeRenderer::EditAppearance(BeamBladeAppearance& a)
{
	bool changed = false;
#ifdef _DEBUG
	ImGui::SeparatorText("Shape");
	changed |= ImGui::DragFloat("Length##Blade", &a.length, 0.01f, 0.05f, 10.0f);
	changed |= ImGui::DragFloat("Half Width##Blade", &a.halfWidth, 0.002f, 0.005f, 2.0f);
	changed |= ImGui::DragFloat("Half Thickness##Blade", &a.halfThickness, 0.001f, 0.001f, 1.0f);
	changed |= ImGui::SliderFloat("Base Width##Blade", &a.baseWidth, 0.0f, 2.0f);
	changed |= ImGui::SliderFloat("Tip Length##Blade", &a.tipLength, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Tip Offset##Blade", &a.tipOffset, -1.0f, 1.0f);
	changed |= ImGui::SliderFloat("Ridge Offset##Blade", &a.ridgeOffset, -1.0f, 1.0f);
	changed |= ImGui::SliderFloat("Back Anchor##Blade", &a.backAnchor, 0.0f, 1.0f);
	if (ImGui::Button("Single Edge (down)##Blade")) {
		a.ridgeOffset = 0.75f;
		a.backAnchor = 1.0f;
		a.tipOffset = 1.0f;
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Double Edge##Blade")) {
		a.ridgeOffset = 0.0f;
		a.backAnchor = 0.0f;
		a.tipOffset = 0.0f;
		changed = true;
	}
	ImGui::SeparatorText("Color");
	changed |= ImGui::ColorEdit3("Core Color##Blade", &a.coreColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	changed |= ImGui::ColorEdit3("Edge Color##Blade", &a.edgeColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	changed |= ImGui::DragFloat("Intensity##Blade", &a.intensity, 0.01f, 0.0f, 20.0f);
	changed |= ImGui::SliderFloat("Body Alpha##Blade", &a.bodyAlpha, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Edge Alpha##Blade", &a.edgeAlpha, 0.0f, 1.0f);
	changed |= ImGui::DragFloat("Edge Sharpness##Blade", &a.edgeSharpness, 0.05f, 0.5f, 40.0f);
	changed |= ImGui::DragFloat("Fresnel Power##Blade", &a.fresnelPower, 0.05f, 0.5f, 10.0f);
	changed |= ImGui::DragFloat("Fresnel Strength##Blade", &a.fresnelStrength, 0.01f, 0.0f, 5.0f);
	changed |= ImGui::DragFloat("Bloom Scale##Blade", &a.bloomScale, 0.01f, 0.0f, 10.0f);
	ImGui::SeparatorText("Extend");
	changed |= ImGui::DragFloat("Extend Time##Blade", &a.extendTime, 0.005f, 0.0f, 2.0f);
	changed |= ImGui::DragFloat("Retract Time##Blade", &a.retractTime, 0.005f, 0.0f, 2.0f);
#else
	(void)a;
#endif
	return changed;
}
