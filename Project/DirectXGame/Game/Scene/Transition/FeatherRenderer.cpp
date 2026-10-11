#include "FeatherRenderer.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include "Transform.h"
#include "Vector2.h"
#include "Log.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const wchar_t* kVertexShaderPath = L"Resources/Shaders/Transition/Feather.VS.hlsl";
	constexpr const wchar_t* kPixelShaderPath = L"Resources/Shaders/Transition/Feather.PS.hlsl";
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";

	// 板の分割数（横 × 縦）。反りは縦に強くかかるので縦を細かく
	constexpr uint32_t kSegmentsAcross = 6;
	constexpr uint32_t kSegmentsAlong = 16;

	std::string FeatherTexturePath(uint32_t index) {
		return "Resources/Textures/Effect/Feathers_0" + std::to_string(index) + ".dds";
	}

	// ルートパラメータ番号
	enum RootParam : UINT {
		kRootCamera = 0,     // b0（VS / PS）
		kRootInstances,      // t0（VS / PS）
		kRootFeather0,       // t1〜t5（PS）
		kRootNoise = kRootFeather0 + FeatherRenderer::kTextureCount, // t6（PS）
		kRootCount
	};
}

void FeatherRenderer::Initialize(DirectXCore* dxCore, SRVManager* srvManager) {
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	assert(dxCore_ && srvManager_);

	for (uint32_t i = 0; i < kTextureCount; ++i) {
		TextureManager::GetInstance()->LoadTexture(FeatherTexturePath(i));
	}
	TextureManager::GetInstance()->LoadTexture(kNoisePath);

	CreateMesh();
	CreateRootSignature();
	CreatePipelineState();

	instanceBuffer_ = dxCore_->CreateBufferResource(sizeof(InstanceForGPU) * kMaxFeathers);
	instanceBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
	instanceSrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVForStructuredBuffer(instanceSrvIndex_, instanceBuffer_.Get(), kMaxFeathers, sizeof(InstanceForGPU));

	cameraResource_ = dxCore_->CreateBufferResource((sizeof(CameraForGPU) + 255) & ~255);
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
	*cameraData_ = CameraForGPU{};

	// 既定のカメラ：原点から +Z を見る（羽根は z = 数 m 先に置く）
	camera_.SetTranslate({ 0.0f, 0.0f, 0.0f });
	camera_.SetRotate({ 0.0f, 0.0f, 0.0f });
	camera_.SetNearClip(0.05f);
	camera_.SetFarClip(100.0f);
	camera_.Update();

	order_.reserve(kMaxFeathers);
	depth_.reserve(kMaxFeathers);
}

void FeatherRenderer::Finalize() {
	if (srvManager_ && instanceSrvIndex_ != UINT32_MAX) {
		srvManager_->Free(instanceSrvIndex_);
		instanceSrvIndex_ = UINT32_MAX;
	}
	instanceBuffer_.Reset();
	instanceData_ = nullptr;
	cameraResource_.Reset();
	cameraData_ = nullptr;
	vertexBuffer_.Reset();
	indexBuffer_.Reset();
}

void FeatherRenderer::CreateMesh() {
	// 頂点は UV だけ。形は頂点シェーダが UV から作る
	std::vector<Vector2> vertices;
	for (uint32_t y = 0; y <= kSegmentsAlong; ++y) {
		for (uint32_t x = 0; x <= kSegmentsAcross; ++x) {
			vertices.push_back({ static_cast<float>(x) / kSegmentsAcross, static_cast<float>(y) / kSegmentsAlong });
		}
	}
	std::vector<uint16_t> indices;
	const uint32_t rowStride = kSegmentsAcross + 1;
	for (uint32_t y = 0; y < kSegmentsAlong; ++y) {
		for (uint32_t x = 0; x < kSegmentsAcross; ++x) {
			const uint16_t i0 = static_cast<uint16_t>(y * rowStride + x);
			const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
			const uint16_t i2 = static_cast<uint16_t>(i0 + rowStride);
			const uint16_t i3 = static_cast<uint16_t>(i2 + 1);
			indices.insert(indices.end(), { i0, i1, i2, i1, i3, i2 });
		}
	}
	indexCount_ = static_cast<uint32_t>(indices.size());

	const UINT vertexBytes = static_cast<UINT>(sizeof(Vector2) * vertices.size());
	vertexBuffer_ = dxCore_->CreateBufferResource(vertexBytes);
	void* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, &mapped);
	std::copy(vertices.begin(), vertices.end(), static_cast<Vector2*>(mapped));
	vertexBuffer_->Unmap(0, nullptr);
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = vertexBytes;
	vertexBufferView_.StrideInBytes = sizeof(Vector2);

	const UINT indexBytes = static_cast<UINT>(sizeof(uint16_t) * indices.size());
	indexBuffer_ = dxCore_->CreateBufferResource(indexBytes);
	indexBuffer_->Map(0, nullptr, &mapped);
	std::copy(indices.begin(), indices.end(), static_cast<uint16_t*>(mapped));
	indexBuffer_->Unmap(0, nullptr);
	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = indexBytes;
	indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
}

void FeatherRenderer::CreateRootSignature() {
	constexpr UINT kTableCount = 1 + kTextureCount + 1;   // インスタンス・羽根 5 枚・ノイズ
	D3D12_DESCRIPTOR_RANGE ranges[kTableCount] = {};
	for (UINT i = 0; i < kTableCount; ++i) {
		ranges[i].BaseShaderRegister = i;   // t0〜t6
		ranges[i].NumDescriptors = 1;
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_PARAMETER params[kRootCount] = {};
	params[kRootCamera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootCamera].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[kRootCamera].Descriptor.ShaderRegister = 0;
	for (UINT i = 0; i < kTableCount; ++i) {
		D3D12_ROOT_PARAMETER& p = params[kRootInstances + i];
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		// インスタンスは VS（置き方）と PS（色・ディゾルブ）の両方で読む
		p.ShaderVisibility = (i == 0) ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_PIXEL;
		p.DescriptorTable.pDescriptorRanges = &ranges[i];
		p.DescriptorTable.NumDescriptorRanges = 1;
	}

	// s0 = クランプ・線形（羽根）, s1 = ラップ・線形（ノイズ）
	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	for (UINT i = 0; i < 2; ++i) {
		samplers[i].ShaderRegister = i;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
	}
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.pParameters = params;
	desc.NumParameters = kRootCount;
	desc.pStaticSamplers = samplers;
	desc.NumStaticSamplers = _countof(samplers);
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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

void FeatherRenderer::CreatePipelineState() {
	IDxcBlob* vs = dxCore_->LoadShaderBlob(kVertexShaderPath, L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(kPixelShaderPath, L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC inputElement{};
	inputElement.SemanticName = "TEXCOORD";
	inputElement.SemanticIndex = 0;
	inputElement.Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElement.AlignedByteOffset = 0;
	inputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

	// 羽根は薄い板なので両面を描く
	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_BLEND_DESC blend{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

	// 深度は使わず、CPU で奥から順に並べて描く（半透明の毛羽立ちを正しく重ねるため）
	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.InputLayout = { &inputElement, 1 };
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // シーン RT
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
}

void FeatherRenderer::Draw(ID3D12GraphicsCommandList* cmd, const std::vector<Feather>& feathers, float time) {
	static_assert(sizeof(InstanceForGPU) == 96, "Feather.hlsli の FeatherInstance と大きさを合わせる");
	static_assert(sizeof(CameraForGPU) == 128, "Feather.hlsli の FeatherCamera と大きさを合わせる");

	const uint32_t count = static_cast<uint32_t>((std::min)(feathers.size(), static_cast<size_t>(kMaxFeathers)));
	if (count == 0) return;

	// 奥から順に並べる（カメラの前向きに沿った距離が大きい方から）
	const Vector3 eye = camera_.GetTranslate();
	const Vector3 forward = camera_.GetForward();
	order_.resize(count);
	depth_.resize(count);
	std::iota(order_.begin(), order_.end(), 0u);
	for (uint32_t i = 0; i < count; ++i) {
		const Vector3& p = feathers[i].position;
		depth_[i] = (p.x - eye.x) * forward.x + (p.y - eye.y) * forward.y + (p.z - eye.z) * forward.z;
	}
	std::sort(order_.begin(), order_.end(), [this](uint32_t a, uint32_t b) { return depth_[a] > depth_[b]; });

	for (uint32_t i = 0; i < count; ++i) {
		const Feather& f = feathers[order_[i]];
		InstanceForGPU& g = instanceData_[i];
		g.world = MakeAffineMatrix(Transform{ { f.size, f.size, f.size }, f.rotation, f.position });
		g.bend = f.bend;
		g.cup = f.cup;
		g.alpha = std::clamp(f.alpha, 0.0f, 1.0f);
		g.dissolve = std::clamp(f.dissolve, 0.0f, 1.0f);
		g.texture = static_cast<float>((std::min)(f.texture, kTextureCount - 1));
		g.brightness = f.brightness;
		g.noiseOffset = f.noiseOffset;
		g.padding = 0.0f;
	}

	CameraForGPU c{};
	c.viewProjection = camera_.GetViewProjectionMatrix();
	c.cameraPosition = eye;
	c.time = time;
	c.lightDirection = Normalize(settings_.lightDirection);
	c.ambient = std::clamp(settings_.ambient, 0.0f, 1.0f);
	c.edgeColor = settings_.edgeColor;
	c.edgeWidth = (std::max)(settings_.edgeWidth, 0.001f);
	c.translucency = settings_.translucency;
	c.noiseScale = settings_.noiseScale;
	*cameraData_ = c;

	srvManager_->PreDraw();
	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
	cmd->IASetIndexBuffer(&indexBufferView_);
	cmd->SetGraphicsRootConstantBufferView(kRootCamera, cameraResource_->GetGPUVirtualAddress());
	srvManager_->SetGraphicsRootDescriptorTable(kRootInstances, instanceSrvIndex_);
	auto* textures = TextureManager::GetInstance();
	for (uint32_t i = 0; i < kTextureCount; ++i) {
		cmd->SetGraphicsRootDescriptorTable(kRootFeather0 + i, textures->GetSrvHandleGPU(FeatherTexturePath(i)));
	}
	cmd->SetGraphicsRootDescriptorTable(kRootNoise, textures->GetSrvHandleGPU(kNoisePath));
	cmd->DrawIndexedInstanced(indexCount_, count, 0, 0, 0);
}

void FeatherRenderer::OnImGui() {
#ifdef _DEBUG
	Settings& s = settings_;
	ImGui::DragFloat3("Light Direction", &s.lightDirection.x, 0.01f, -1.0f, 1.0f);
	ImGui::SliderFloat("Ambient", &s.ambient, 0.0f, 1.0f);
	ImGui::DragFloat("Translucency", &s.translucency, 0.01f, 0.0f, 2.0f);
	ImGui::ColorEdit3("Dissolve Edge Color", &s.edgeColor.x);
	ImGui::DragFloat("Dissolve Edge Width", &s.edgeWidth, 0.005f, 0.001f, 0.5f);
	ImGui::DragFloat("Dissolve Noise Scale", &s.noiseScale, 0.05f, 0.1f, 10.0f);
#endif
}
