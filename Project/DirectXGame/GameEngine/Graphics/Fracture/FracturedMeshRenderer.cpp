#include "FracturedMeshRenderer.h"
#include "Camera.h"
#include "DirectXCore.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "Object3DManager.h"
#include "PepperMacros.h"
#include "SRVManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// 定数バッファを 256 バイト境界へ切り上げるためのマスク
	constexpr uint32_t kConstantBufferAlignMask = 255u;
	// 異方性フィルタの最大値（斜めから見た岩肌をぼかさない）
	constexpr UINT kMaxAnisotropy = 8;

	// ルートパラメータの番号
	enum RootParam : UINT {
		kRootFrame = 0,       // b0 フレーム（VS/PS）
		kRootDraw,            // b3 ルート定数（VS）
		kRootLight,           // b1 平行光源（PS）
		kRootFog,             // b6 フォグ（PS）
		kRootInstances,       // t0 岩・破片ごとの行列（VS）
		kRootExteriorBase,    // t1
		kRootExteriorNormal,  // t2
		kRootInteriorBase,    // t3
		kRootInteriorNormal,  // t4
		kRootCount,
	};
	// ルート定数の 32 ビット値の数（DrawConstants）
	constexpr UINT kDrawConstantCount = 3;
	// これ以上見えていればディゾルブしていないとみなす（シェーダの判定と同じ値）
	constexpr float kFullyVisible = 0.999f;

	// 描き分けの組（この順にインスタンスを詰める）
	enum DrawGroup : uint32_t {
		kGroupIntactSolid = 0,  // 割る前・普通（背面カリングあり）
		kGroupIntactDissolve,   // 割る前・ディゾルブ中（カリングなし）
		kGroupBrokenSolid,      // 割れた・普通
		kGroupBrokenDissolve,   // 割れた・ディゾルブ中
		kGroupCount,
	};

	Matrix4x4 MakeTranslation(const Vector3& t) {
		Matrix4x4 m = MakeIdentity4x4();
		m.m[3][0] = t.x;
		m.m[3][1] = t.y;
		m.m[3][2] = t.z;
		return m;
	}
}

void FracturedMeshRenderer::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	uint32_t maxRocks, const TextureSet& textures) {
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	object3DManager_ = object3DManager;
	maxRocks_ = (std::max)(maxRocks, 1u);
	textures_ = textures;

	CreateRootSignature();
	CreatePipelineState();

	const size_t cbSize = (sizeof(FrameCB) + kConstantBufferAlignMask) & ~static_cast<size_t>(kConstantBufferAlignMask);
	frameBuffer_ = dxCore_->CreateBufferResource(cbSize);
	frameBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&frameMapped_));

	// フォグを借りられないとき用。全部 0 ＝ enabled = 0
	disabledFogBuffer_ = dxCore_->CreateBufferResource(kConstantBufferAlignMask + 1);
	void* fogMapped = nullptr;
	disabledFogBuffer_->Map(0, nullptr, &fogMapped);
	std::memset(fogMapped, 0, kConstantBufferAlignMask + 1);
	disabledFogBuffer_->Unmap(0, nullptr);

	TextureManager* tm = TextureManager::GetInstance();
	tm->LoadTexture(textures_.exteriorBase);
	tm->LoadTextureLinear(textures_.exteriorNormal);
	tm->LoadTexture(textures_.interiorBase);
	tm->LoadTextureLinear(textures_.interiorNormal);
}

void FracturedMeshRenderer::Finalize() {
	if (frameBuffer_ && frameMapped_) {
		frameBuffer_->Unmap(0, nullptr);
		frameMapped_ = nullptr;
	}
	if (instanceBuffer_ && instanceMapped_) {
		instanceBuffer_->Unmap(0, nullptr);
		instanceMapped_ = nullptr;
	}
	if (srvManager_ && instanceSrvIndex_ != UINT32_MAX) {
		srvManager_->Free(instanceSrvIndex_);
		instanceSrvIndex_ = UINT32_MAX;
	}
	// 終了時は GPU の完了を待ってから呼ばれる前提なので、預かり物もここで返す
	for (RetiredResource& r : retired_) {
		if (srvManager_ && r.srvIndex != UINT32_MAX) srvManager_->Free(r.srvIndex);
	}
	retired_.clear();
	instanceCapacity_ = 0;
	instanceBuffer_.Reset();
	vertexBuffer_.Reset();
	indexBuffer_.Reset();
	frameBuffer_.Reset();
	disabledFogBuffer_.Reset();
	pipelineState_.Reset();
	pipelineStateNoCull_.Reset();
	rootSignature_.Reset();
	fragments_.clear();
	intactIndexCount_ = 0;
	fragmentIndexCount_ = 0;
}

void FracturedMeshRenderer::CreateRootSignature() {
	D3D12_DESCRIPTOR_RANGE ranges[5] = {};
	for (UINT i = 0; i < _countof(ranges); ++i) {
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i;  // t0〜t4
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_PARAMETER params[kRootCount] = {};
	params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootFrame].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[kRootFrame].Descriptor.ShaderRegister = 0;

	params[kRootDraw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[kRootDraw].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[kRootDraw].Constants.ShaderRegister = 3;
	params[kRootDraw].Constants.Num32BitValues = kDrawConstantCount;

	params[kRootLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootLight].Descriptor.ShaderRegister = 1;

	params[kRootFog].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootFog].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootFog].Descriptor.ShaderRegister = 6;

	for (UINT i = 0; i < 5; ++i) {
		D3D12_ROOT_PARAMETER& p = params[kRootInstances + i];
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		p.ShaderVisibility = (i == 0) ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
		p.DescriptorTable.pDescriptorRanges = &ranges[i];
		p.DescriptorTable.NumDescriptorRanges = 1;
	}

	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_ANISOTROPIC;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MaxAnisotropy = kMaxAnisotropy;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	desc.pParameters = params;
	desc.NumParameters = _countof(params);
	desc.pStaticSamplers = &sampler;
	desc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr)) {
		if (errBlob) OutputDebugStringA(static_cast<char*>(errBlob->GetBufferPointer()));
		assert(false);
		return;
	}
	hr = dxCore_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void FracturedMeshRenderer::CreatePipelineState() {
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Fracture/FracturedMesh.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Fracture/FracturedMesh.PS.hlsl", L"ps_6_0");
	assert(vs && ps);
	if (!vs || !ps) return;

	D3D12_INPUT_ELEMENT_DESC elems[4] = {};
	elems[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[2] = { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[3] = { "FRAGMENT", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	static_assert(sizeof(VoronoiFracture::Vertex) == 32, "頂点の並びがシェーダの入力と合わない");

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_BACK;  // Object3D と同じ（断面も元のメッシュと同じ巻き方で作ってある）
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.DepthClipEnable = TRUE;

	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.InputLayout = { elems, _countof(elems) };
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // シーン RT と同じ
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	// ディゾルブ中の岩用：穴から裏面（岩の内側）が見えるので両面描く。裏面の塗り方は PS が SV_IsFrontFace で切り替える
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStateNoCull_));
	assert(SUCCEEDED(hr));
}

void FracturedMeshRenderer::Retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint32_t srvIndex) {
	if (!resource && srvIndex == UINT32_MAX) return;
	retired_.push_back({ std::move(resource), srvIndex, dxCore_->GetNextFenceValue() });
	resource.Reset();
}

void FracturedMeshRenderer::ReleaseRetired() {
	if (retired_.empty()) return;
	const uint64_t completed = dxCore_->GetCompletedFenceValue();
	auto done = [&](const RetiredResource& r) { return r.fenceValue <= completed; };
	for (const RetiredResource& r : retired_) {
		if (done(r) && r.srvIndex != UINT32_MAX) srvManager_->Free(r.srvIndex);
	}
	retired_.erase(std::remove_if(retired_.begin(), retired_.end(), done), retired_.end());
}

void FracturedMeshRenderer::EnsureInstanceCapacity(uint32_t count) {
	count = (std::max)(count, 1u);
	if (count <= instanceCapacity_ && instanceBuffer_) return;

	if (instanceBuffer_ && instanceMapped_) {
		instanceBuffer_->Unmap(0, nullptr);
		instanceMapped_ = nullptr;
	}
	// 前のフレームが読んでいるかもしれないので、バッファも SRV も新しく取り、古い方は預ける
	Retire(instanceBuffer_, instanceSrvIndex_);
	instanceBuffer_ = dxCore_->CreateBufferResource(static_cast<size_t>(count) * sizeof(InstanceGPU));
	instanceBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&instanceMapped_));
	instanceCapacity_ = count;

	instanceSrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVForStructuredBuffer(instanceSrvIndex_, instanceBuffer_.Get(), count,
		static_cast<UINT>(sizeof(InstanceGPU)));
}

void FracturedMeshRenderer::SetMesh(const VoronoiFracture::Result& result) {
	fragments_.clear();
	intactIndexCount_ = 0;
	fragmentIndexCount_ = 0;
	ReleaseRetired();
	Retire(vertexBuffer_);
	Retire(indexBuffer_);
	if (!result.IsValid()) return;

	const size_t vbSize = result.vertices.size() * sizeof(VoronoiFracture::Vertex);
	vertexBuffer_ = dxCore_->CreateBufferResource(vbSize);
	void* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, &mapped);
	std::memcpy(mapped, result.vertices.data(), vbSize);
	vertexBuffer_->Unmap(0, nullptr);
	vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vbView_.SizeInBytes = static_cast<UINT>(vbSize);
	vbView_.StrideInBytes = sizeof(VoronoiFracture::Vertex);

	const size_t ibSize = result.indices.size() * sizeof(uint32_t);
	indexBuffer_ = dxCore_->CreateBufferResource(ibSize);
	indexBuffer_->Map(0, nullptr, &mapped);
	std::memcpy(mapped, result.indices.data(), ibSize);
	indexBuffer_->Unmap(0, nullptr);
	ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	ibView_.SizeInBytes = static_cast<UINT>(ibSize);
	ibView_.Format = DXGI_FORMAT_R32_UINT;

	intactFirstIndex_ = result.intactFirstIndex;
	intactIndexCount_ = result.intactIndexCount;
	fragments_ = result.fragments;
	// 破片は連結してあるので、最初の破片の先頭から最後の破片の末尾までを 1 回で描く
	fragmentFirstIndex_ = fragments_.front().firstIndex;
	fragmentIndexCount_ = fragments_.back().firstIndex + fragments_.back().indexCount - fragmentFirstIndex_;

	// 全部の岩が割れても入るだけ先に取っておく（描画中に作り直さない）
	EnsureInstanceCapacity(maxRocks_ * (1u + static_cast<uint32_t>(fragments_.size())));
}

Matrix4x4 FracturedMeshRenderer::MakeFragmentMatrix(const VoronoiFracture::Fragment& fragment, const FragmentPose& pose,
	const Matrix4x4& rockWorld) {
	const Vector3& c = fragment.center;
	const Matrix4x4 toCenter = MakeTranslation({ -c.x, -c.y, -c.z });
	const Matrix4x4 back = MakeTranslation({ c.x + pose.offset.x, c.y + pose.offset.y, c.z + pose.offset.z });
	return Multiply(Multiply(Multiply(toCenter, MakeRotateMatrix(pose.rotation)), back), rockWorld);
}

void FracturedMeshRenderer::Draw(const Camera& camera, const std::vector<RockDraw>& rocks) {
	ReleaseRetired();
	if (!pipelineState_ || !frameMapped_ || !instanceMapped_ || intactIndexCount_ == 0 || rocks.empty()) return;
	const D3D12_GPU_VIRTUAL_ADDRESS lightAddress = LightManager::GetInstance()->GetDirectionalLightGpuAddress();
	if (lightAddress == 0) return;
	PEPPER_SCOPE("FracturedMeshRenderer::Draw");

	const uint32_t fragmentCount = static_cast<uint32_t>(fragments_.size());
	const size_t rockCount = (std::min)(rocks.size(), static_cast<size_t>(maxRocks_));

	const FragmentPose restPose{};
	auto poseOf = [&](const RockDraw& rock, uint32_t f) -> const FragmentPose& {
		return (rock.fragments && f < rock.fragments->size()) ? (*rock.fragments)[f] : restPose;
	};
	// ディゾルブ中（穴が開いて裏面が見える）の岩か
	auto isDissolving = [&](const RockDraw& rock) {
		if (rock.visibility < kFullyVisible) return true;
		if (!rock.broken) return false;
		for (uint32_t f = 0; f < fragmentCount; ++f) {
			if (poseOf(rock, f).visibility < kFullyVisible) return true;
		}
		return false;
	};

	// [割る前・普通][割る前・ディゾルブ中][割れた・普通][割れた・ディゾルブ中] の順に詰める。
	// ディゾルブ中だけ背面カリングを切って描く（普通の岩は今までどおり裏面を描かない）
	instanceScratch_.clear();
	uint32_t groupStart[kGroupCount] = {};
	uint32_t groupRocks[kGroupCount] = {};
	for (uint32_t g = 0; g < kGroupCount; ++g) {
		const bool broken = (g >= kGroupBrokenSolid);
		const bool dissolving = (g == kGroupIntactDissolve || g == kGroupBrokenDissolve);
		groupStart[g] = static_cast<uint32_t>(instanceScratch_.size());
		for (size_t i = 0; i < rockCount; ++i) {
			const RockDraw& rock = rocks[i];
			if (rock.broken != broken || rock.visibility <= 0.0f || isDissolving(rock) != dissolving) continue;
			if (!broken) {
				instanceScratch_.push_back({ rock.world, { rock.visibility, rock.textureScale, 0.0f, 0.0f } });
			} else {
				for (uint32_t f = 0; f < fragmentCount; ++f) {
					const FragmentPose& pose = poseOf(rock, f);
					instanceScratch_.push_back({ MakeFragmentMatrix(fragments_[f], pose, rock.world),
						{ pose.visibility * rock.visibility, rock.textureScale, 0.0f, 0.0f } });
				}
			}
			++groupRocks[g];
		}
	}
	if (instanceScratch_.empty() || instanceScratch_.size() > instanceCapacity_) return;
	std::memcpy(instanceMapped_, instanceScratch_.data(), instanceScratch_.size() * sizeof(InstanceGPU));

	FrameCB& cb = *frameMapped_;
	cb.viewProjection = camera.GetViewProjectionMatrix();
	cb.cameraPosition = camera.GetTranslate();
	cb.textureTiling = appearance_.textureTiling;
	cb.exteriorTint = appearance_.exteriorTint;
	cb.interiorTint = appearance_.interiorTint;
	cb.dissolveEdgeColor = appearance_.dissolveEdgeColor;
	cb.normalStrength = appearance_.normalStrength;
	cb.ambient = appearance_.ambient;
	cb.dissolveEdgeWidth = appearance_.dissolveEdgeWidth;
	cb.dissolveNoiseScale = appearance_.dissolveNoiseScale;
	cb.interiorEmissive = appearance_.interiorEmissive;
	cb.backfaceShade = appearance_.backfaceShade;

	ID3D12GraphicsCommandList* cmd = dxCore_->GetCommandList();
	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vbView_);
	cmd->IASetIndexBuffer(&ibView_);

	const D3D12_GPU_VIRTUAL_ADDRESS fogAddress = object3DManager_ ? object3DManager_->GetFogAddress() : 0;
	cmd->SetGraphicsRootConstantBufferView(kRootFrame, frameBuffer_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootLight, lightAddress);
	cmd->SetGraphicsRootConstantBufferView(kRootFog,
		fogAddress != 0 ? fogAddress : disabledFogBuffer_->GetGPUVirtualAddress());
	srvManager_->SetGraphicsRootDescriptorTable(kRootInstances, instanceSrvIndex_);

	TextureManager* tm = TextureManager::GetInstance();
	cmd->SetGraphicsRootDescriptorTable(kRootExteriorBase, tm->GetSrvHandleGPU(textures_.exteriorBase));
	cmd->SetGraphicsRootDescriptorTable(kRootExteriorNormal, tm->GetSrvHandleGPU(textures_.exteriorNormal));
	cmd->SetGraphicsRootDescriptorTable(kRootInteriorBase, tm->GetSrvHandleGPU(textures_.interiorBase));
	cmd->SetGraphicsRootDescriptorTable(kRootInteriorNormal, tm->GetSrvHandleGPU(textures_.interiorNormal));

	ID3D12PipelineState* currentPso = nullptr;
	for (uint32_t g = 0; g < kGroupCount; ++g) {
		if (groupRocks[g] == 0) continue;
		const bool broken = (g >= kGroupBrokenSolid);
		if (broken && fragmentCount == 0) continue;
		const bool dissolving = (g == kGroupIntactDissolve || g == kGroupBrokenDissolve);
		ID3D12PipelineState* pso = dissolving ? pipelineStateNoCull_.Get() : pipelineState_.Get();
		if (!pso) continue;
		if (pso != currentPso) {
			cmd->SetPipelineState(pso);
			currentPso = pso;
		}
		const DrawConstants dc{ groupStart[g], broken ? 1u : 0u, fragmentCount };
		cmd->SetGraphicsRoot32BitConstants(kRootDraw, kDrawConstantCount, &dc, 0);
		PEPPER_COUNT("DrawCall");
		if (broken) {
			cmd->DrawIndexedInstanced(fragmentIndexCount_, groupRocks[g], fragmentFirstIndex_, 0, 0);
		} else {
			cmd->DrawIndexedInstanced(intactIndexCount_, groupRocks[g], intactFirstIndex_, 0, 0);
		}
	}
}

#ifdef USE_IMGUI
void FracturedMeshRenderer::OnImGui() {
	Appearance& a = appearance_;
	ImGui::SliderFloat("模様の繰り返し", &a.textureTiling, 0.05f, 4.0f);
	ImGui::SliderFloat("凹凸の強さ", &a.normalStrength, 0.0f, 3.0f);
	ImGui::SliderFloat("環境光", &a.ambient, 0.0f, 1.0f);
	ImGui::ColorEdit3("外側の色", &a.exteriorTint.x);
	ImGui::ColorEdit3("断面の色", &a.interiorTint.x);
	ImGui::SliderFloat("断面の発光", &a.interiorEmissive, 0.0f, 2.0f);
	ImGui::SliderFloat("ディゾルブの穴から見える内側の明るさ", &a.backfaceShade, 0.0f, 1.0f);
	ImGui::SliderFloat("ディゾルブの縁の幅", &a.dissolveEdgeWidth, 0.0f, 0.5f);
	ImGui::SliderFloat("ディゾルブの模様の細かさ", &a.dissolveNoiseScale, 0.1f, 20.0f);
	ImGui::ColorEdit3("ディゾルブの縁の色", &a.dissolveEdgeColor.x);
}
#endif
