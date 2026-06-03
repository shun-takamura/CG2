#include "DisruptorShardRenderer.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "MathUtility.h"
#include <cassert>
#include <cstring>
#include <algorithm>

void DisruptorShardRenderer::Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t maxCells) {
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	maxCells_ = (std::max)(maxCells, 1u);

	CreateRootSignature();
	CreatePipelineState();
	CreateConstantBuffer();
}

void DisruptorShardRenderer::Finalize() {
	if (cbBuffer_ && cbMapped_) {
		cbBuffer_->Unmap(0, nullptr);
		cbMapped_ = nullptr;
	}
	if (vertexBuffer_ && vbMapped_) {
		vertexBuffer_->Unmap(0, nullptr);
		vbMapped_ = nullptr;
	}
	cbBuffer_.Reset();
	vertexBuffer_.Reset();
	pipelineState_.Reset();
	rootSignature_.Reset();
}

void DisruptorShardRenderer::CreateRootSignature() {
	// [0] CBV b0（VS:WVP / PS:alpha,satBoost）, [1] SRV t0（capture, PS）, s0 linear clamp
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

	// 深度テストなし（PostEffect 後のオーバーレイ＝DSVなしで描く。書き込みもしない）
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
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN; // PostEffect 後に DSVなしで描くため（DepthEnable=FALSE と整合）
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
}

void DisruptorShardRenderer::CreateConstantBuffer() {
	cbStride_ = (static_cast<uint32_t>(sizeof(ShardCB)) + 255u) & ~255u;
	cbBuffer_ = dxCore_->CreateBufferResource(static_cast<size_t>(cbStride_) * maxCells_);
	cbBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cbMapped_));
}

void DisruptorShardRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
	if (vertexCount <= vbCapacityVerts_ && vertexBuffer_) return;

	// 既存をアンマップして作り直す（SetCells は崩壊開始に1回＝フレーム途中の描画中ではない）
	if (vertexBuffer_ && vbMapped_) {
		vertexBuffer_->Unmap(0, nullptr);
		vbMapped_ = nullptr;
	}
	// 余裕を持って確保（churn 抑制）
	uint32_t cap = (std::max)(vertexCount, vbCapacityVerts_ + vbCapacityVerts_ / 2u);
	cap = (std::max)(cap, 3u);
	vertexBuffer_ = dxCore_->CreateBufferResource(static_cast<size_t>(cap) * sizeof(Vertex));
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vbMapped_));
	vbCapacityVerts_ = cap;
}

void DisruptorShardRenderer::SetCells(const std::vector<CellMesh>& cells) {
	cellRanges_.clear();
	cellRanges_.reserve(cells.size());

	uint32_t total = 0;
	for (const auto& c : cells) total += static_cast<uint32_t>(c.verts.size());
	if (total == 0) {
		vbView_ = {};
		return;
	}

	EnsureVertexCapacity(total);
	if (!vbMapped_) return;

	uint32_t cursor = 0;
	for (const auto& c : cells) {
		const uint32_t n = static_cast<uint32_t>(c.verts.size());
		CellRange range{ cursor, n };
		cellRanges_.push_back(range);
		if (n > 0) {
			std::memcpy(vbMapped_ + static_cast<size_t>(cursor) * sizeof(Vertex),
				c.verts.data(), static_cast<size_t>(n) * sizeof(Vertex));
		}
		cursor += n;
	}

	vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vbView_.SizeInBytes = total * sizeof(Vertex);
	vbView_.StrideInBytes = sizeof(Vertex);
}

void DisruptorShardRenderer::Draw(const Matrix4x4& viewProjection, uint32_t captureSrvIndex,
	const std::vector<DrawItem>& items) {
	if (items.empty() || !pipelineState_ || !cbMapped_ || cellRanges_.empty() || vbView_.SizeInBytes == 0) return;

	ID3D12GraphicsCommandList* cmd = dxCore_->GetCommandList();
	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vbView_);
	srvManager_->SetGraphicsRootDescriptorTable(1, captureSrvIndex);

	const size_t n = (std::min)(items.size(), static_cast<size_t>(maxCells_));
	for (size_t i = 0; i < n; ++i) {
		const DrawItem& it = items[i];
		if (it.cellIndex >= cellRanges_.size()) continue;
		const CellRange& range = cellRanges_[it.cellIndex];
		if (range.count == 0) continue;

		ShardCB cb{};
		cb.wvp = Multiply(it.world, viewProjection);
		cb.alpha = it.alpha;
		cb.satBoost = it.satBoost;
		std::memcpy(cbMapped_ + i * cbStride_, &cb, sizeof(cb));
		const D3D12_GPU_VIRTUAL_ADDRESS addr = cbBuffer_->GetGPUVirtualAddress() + i * cbStride_;
		cmd->SetGraphicsRootConstantBufferView(0, addr);
		cmd->DrawInstanced(range.count, 1, range.start, 0);
	}
}
