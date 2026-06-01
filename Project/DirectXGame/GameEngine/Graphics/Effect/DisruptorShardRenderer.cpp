#include "DisruptorShardRenderer.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "MathUtility.h"
#include <cassert>
#include <cstring>
#include <algorithm>

namespace {
	struct ShardVertex {
		float pos[3];
		float uv[2];
	};
}

void DisruptorShardRenderer::Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t maxInstances) {
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	maxInstances_ = (std::max)(maxInstances, 1u);

	CreateRootSignature();
	CreatePipelineState();
	CreateCubeMesh();
	CreateInstanceBuffer();
}

void DisruptorShardRenderer::Finalize() {
	if (cbBuffer_ && cbMapped_) {
		cbBuffer_->Unmap(0, nullptr);
		cbMapped_ = nullptr;
	}
	cbBuffer_.Reset();
	vertexBuffer_.Reset();
	indexBuffer_.Reset();
	pipelineState_.Reset();
	rootSignature_.Reset();
}

void DisruptorShardRenderer::CreateRootSignature() {
	// [0] CBV b0（VS:WVP/UV, PS:alpha）, [1] SRV t0（capture, PS）, s0 linear clamp
	D3D12_DESCRIPTOR_RANGE srvRange[1] = {};
	srvRange[0].BaseShaderRegister = 0;
	srvRange[0].NumDescriptors = 1;
	srvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParams[2] = {};
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParams[0].Descriptor.ShaderRegister = 0;

	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[1].DescriptorTable.pDescriptorRanges = srvRange;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	desc.pParameters = rootParams;
	desc.NumParameters = _countof(rootParams);
	desc.pStaticSamplers = &sampler;
	desc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr)) {
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		assert(false);
	}
	hr = dxCore_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(),
		sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void DisruptorShardRenderer::CreatePipelineState() {
	IDxcBlob* vs = dxCore_->CompileShader(L"Resources/Shaders/Disruptor/DisruptorShard.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->CompileShader(L"Resources/Shaders/Disruptor/DisruptorShard.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC elems[2] = {};
	elems[0].SemanticName = "POSITION";
	elems[0].SemanticIndex = 0;
	elems[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	elems[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	elems[1].SemanticName = "TEXCOORD";
	elems[1].SemanticIndex = 0;
	elems[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	elems[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayout{};
	inputLayout.pInputElementDescs = elems;
	inputLayout.NumElements = _countof(elems);

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE; // 破片は両面
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.DepthClipEnable = TRUE;

	// アルファブレンド（Normal）
	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

	// 深度テストなし（崩壊のオーバーレイ演出として確実に手前に出す。書き込みもしない）
	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.InputLayout = inputLayout;
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // シーン RT と同じ
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
}

void DisruptorShardRenderer::CreateCubeMesh() {
	// 24 頂点（面ごとに 4 頂点、面ローカル UV 0..1）。pos は [-0.5,0.5]。
	const float h = 0.5f;
	const ShardVertex verts[24] = {
		// +X
		{{ h,-h,-h},{0,0}}, {{ h,-h, h},{1,0}}, {{ h, h, h},{1,1}}, {{ h, h,-h},{0,1}},
		// -X
		{{-h,-h, h},{0,0}}, {{-h,-h,-h},{1,0}}, {{-h, h,-h},{1,1}}, {{-h, h, h},{0,1}},
		// +Y
		{{-h, h,-h},{0,0}}, {{ h, h,-h},{1,0}}, {{ h, h, h},{1,1}}, {{-h, h, h},{0,1}},
		// -Y
		{{-h,-h, h},{0,0}}, {{ h,-h, h},{1,0}}, {{ h,-h,-h},{1,1}}, {{-h,-h,-h},{0,1}},
		// +Z
		{{ h,-h, h},{0,0}}, {{-h,-h, h},{1,0}}, {{-h, h, h},{1,1}}, {{ h, h, h},{0,1}},
		// -Z
		{{-h,-h,-h},{0,0}}, {{ h,-h,-h},{1,0}}, {{ h, h,-h},{1,1}}, {{-h, h,-h},{0,1}},
	};
	uint32_t indices[36];
	for (uint32_t f = 0; f < 6; ++f) {
		const uint32_t b = f * 4;
		const uint32_t o = f * 6;
		indices[o + 0] = b + 0; indices[o + 1] = b + 1; indices[o + 2] = b + 2;
		indices[o + 3] = b + 0; indices[o + 4] = b + 2; indices[o + 5] = b + 3;
	}
	indexCount_ = 36;

	const UINT vbSize = sizeof(verts);
	vertexBuffer_ = dxCore_->CreateBufferResource(vbSize);
	void* vp = nullptr;
	vertexBuffer_->Map(0, nullptr, &vp);
	std::memcpy(vp, verts, vbSize);
	vertexBuffer_->Unmap(0, nullptr);
	vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vbView_.SizeInBytes = vbSize;
	vbView_.StrideInBytes = sizeof(ShardVertex);

	const UINT ibSize = sizeof(indices);
	indexBuffer_ = dxCore_->CreateBufferResource(ibSize);
	void* ip = nullptr;
	indexBuffer_->Map(0, nullptr, &ip);
	std::memcpy(ip, indices, ibSize);
	indexBuffer_->Unmap(0, nullptr);
	ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	ibView_.SizeInBytes = ibSize;
	ibView_.Format = DXGI_FORMAT_R32_UINT;
}

void DisruptorShardRenderer::CreateInstanceBuffer() {
	cbStride_ = (static_cast<uint32_t>(sizeof(ShardCB)) + 255u) & ~255u;
	cbBuffer_ = dxCore_->CreateBufferResource(static_cast<size_t>(cbStride_) * maxInstances_);
	cbBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cbMapped_));
}

void DisruptorShardRenderer::Draw(const Matrix4x4& viewProjection, uint32_t captureSrvIndex,
	const std::vector<Instance>& instances) {
	if (instances.empty() || !pipelineState_ || !cbMapped_) return;

	ID3D12GraphicsCommandList* cmd = dxCore_->GetCommandList();
	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vbView_);
	cmd->IASetIndexBuffer(&ibView_);
	srvManager_->SetGraphicsRootDescriptorTable(1, captureSrvIndex);

	const size_t n = (std::min)(instances.size(), static_cast<size_t>(maxInstances_));
	for (size_t i = 0; i < n; ++i) {
		const Instance& inst = instances[i];
		ShardCB cb{};
		cb.wvp = Multiply(inst.world, viewProjection);
		cb.uvMin[0] = inst.uvMin.x;  cb.uvMin[1] = inst.uvMin.y;
		cb.uvSize[0] = inst.uvSize.x; cb.uvSize[1] = inst.uvSize.y;
		cb.alpha = inst.alpha;
		cb.distortAmount = inst.distortAmount;
		cb.distortFreq = inst.distortFreq;
		std::memcpy(cbMapped_ + i * cbStride_, &cb, sizeof(cb));
		const D3D12_GPU_VIRTUAL_ADDRESS addr = cbBuffer_->GetGPUVirtualAddress() + i * cbStride_;
		cmd->SetGraphicsRootConstantBufferView(0, addr);
		cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
	}
}
