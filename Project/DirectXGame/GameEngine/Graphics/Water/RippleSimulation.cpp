#include "RippleSimulation.h"

#include "DirectXCore.h"
#include "SRVManager.h"
#include "Log.h"
#include "PepperMacros.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
	enum RootParam : UINT {
		kRootParams = 0,  // b0
		kRootSource,      // t0 1つ前の状態（R=高さ / G=その前の高さ）
		kRootDestination, // u0 次の状態
		kRootCount
	};
	constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R32G32_FLOAT;
	constexpr UINT kThreadGroupSize = 8; // RippleSimulation.CS.hlsl の numthreads と合わせる
	constexpr D3D12_RESOURCE_STATES kReadState =
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

RippleSimulation::~RippleSimulation()
{
	FreeDescriptors();
}

void RippleSimulation::FreeDescriptors()
{
	if (!srvManager_) return;
	for (uint32_t i = 0; i < 2; ++i) {
		if (srvIndices_[i] != UINT32_MAX) {
			srvManager_->Free(srvIndices_[i]);
			srvIndices_[i] = UINT32_MAX;
		}
		if (uavIndices_[i] != UINT32_MAX) {
			srvManager_->Free(uavIndices_[i]);
			uavIndices_[i] = UINT32_MAX;
		}
	}
}

void RippleSimulation::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
	// 2 回目の Initialize（作り直し）でも前の番号を返してから確保する
	FreeDescriptors();
	dxCore_ = dxCore;
	srvManager_ = srvManager;

	CreateTextures();
	CreateRootSignature();
	CreatePipelineState();

	for (uint32_t i = 0; i < kMaxStepsPerFrame; ++i) {
		paramsResources_[i] = dxCore_->CreateBufferResource(sizeof(SimParamsForGPU));
		paramsResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_[i]));
	}
}

void RippleSimulation::CreateTextures()
{
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = kResolution;
	desc.Height = kResolution;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = kFormat;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;

	for (uint32_t i = 0; i < 2; ++i) {
		states_[i] = kReadState;
		HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			states_[i], nullptr, IID_PPV_ARGS(&textures_[i]));
		assert(SUCCEEDED(hr));

		srvIndices_[i] = srvManager_->Allocate();
		srvManager_->CreateSRVForTexture2D(srvIndices_[i], textures_[i].Get(), kFormat, 1);

		uavIndices_[i] = srvManager_->Allocate();
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = kFormat;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		dxCore_->GetDevice()->CreateUnorderedAccessView(textures_[i].Get(), nullptr, &uav,
			srvManager_->GetCPUDescriptorHandle(uavIndices_[i]));
	}
}

void RippleSimulation::CreateRootSignature()
{
	D3D12_DESCRIPTOR_RANGE srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	D3D12_DESCRIPTOR_RANGE uavRange = srvRange;
	uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

	D3D12_ROOT_PARAMETER params[kRootCount] = {};
	params[kRootParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[kRootParams].Descriptor.ShaderRegister = 0;
	params[kRootSource].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootSource].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[kRootSource].DescriptorTable.pDescriptorRanges = &srvRange;
	params[kRootSource].DescriptorTable.NumDescriptorRanges = 1;
	params[kRootDestination].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootDestination].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[kRootDestination].DescriptorTable.pDescriptorRanges = &uavRange;
	params[kRootDestination].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.pParameters = params;
	desc.NumParameters = kRootCount;

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

void RippleSimulation::CreatePipelineState()
{
	IDxcBlob* cs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Water/RippleSimulation.CS.hlsl", L"cs_6_0");
	assert(cs);
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
	HRESULT hr = dxCore_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
}

void RippleSimulation::Update(float deltaTime)
{
	// 止まっていた後に一気に何十刻みも回さないよう、溜める量に上限を付ける
	const float step = 1.0f / (std::max)(settings_.stepsPerSecond, 1.0f);
	accumulator_ = (std::min)(accumulator_ + (std::max)(deltaTime, 0.0f), step * kMaxStepsPerFrame);
}

void RippleSimulation::AddImpulse(const Vector3& position, float radius, float strength)
{
	if (impulseCount_ >= kMaxImpulsesPerFrame) return;
	// 範囲の外は枠を使わない（スクロール時は Dispatch で範囲が少し動くので、そこでもう一度確かめる）
	const float size = (std::max)(settings_.simSize, 0.01f);
	const Vector2 uv{ (position.x - center_.x) / size + 0.5f, (position.z - center_.y) / size + 0.5f };
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return;
	pendingImpulses_[impulseCount_++] = { { position.x, position.z }, radius, strength };
}

uint32_t RippleSimulation::BuildImpulses(Impulse* out) const
{
	const float size = (std::max)(settings_.simSize, 0.01f);
	const Vector2& center = GetCenter();
	uint32_t count = 0;
	for (uint32_t i = 0; i < impulseCount_; ++i) {
		const PendingImpulse& src = pendingImpulses_[i];
		const Vector2 uv{ (src.positionXZ.x - center.x) / size + 0.5f, (src.positionXZ.y - center.y) / size + 0.5f };
		if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) continue;
		out[count++] = { uv, src.radius / size, src.strength };
	}
	return count;
}

void RippleSimulation::SetScrollWithCenter(bool enabled)
{
	if (scrollWithCenter_ == enabled) return;
	scrollWithCenter_ = enabled;
	gridCenter_ = center_;
	clearRequested_ = true;
}

void RippleSimulation::Transition(uint32_t index, D3D12_RESOURCE_STATES after)
{
	if (states_[index] == after) return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = textures_[index].Get();
	barrier.Transition.StateBefore = states_[index];
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	dxCore_->GetCommandList()->ResourceBarrier(1, &barrier);
	states_[index] = after;
}

void RippleSimulation::Dispatch()
{
	const float step = 1.0f / (std::max)(settings_.stepsPerSecond, 1.0f);
	uint32_t steps = static_cast<uint32_t>(accumulator_ / step);
	steps = (std::min)(steps, kMaxStepsPerFrame);
	// 消す指示や波源があれば、刻みが溜まっていなくても1回は回して反映する
	if (steps == 0 && (clearRequested_ || impulseCount_ > 0)) steps = 1;
	if (steps == 0) return;
	accumulator_ = (std::max)(accumulator_ - step * static_cast<float>(steps), 0.0f);

	PEPPER_SCOPE("RippleSimulation::Dispatch");
	auto* cmd = dxCore_->GetCommandList();
	PEPPER_GPU_SCOPE(cmd, "RippleSimulation::Dispatch");

	const float dx = settings_.simSize / static_cast<float>(kResolution);

	// スクロール：中心の動いた分を整数テクセルにして最初の刻みでずらす（端数は次のフレームへ持ち越す）。
	// 刻みを回さないフレームはずらさず、GetCenter も前のまま（PS が読む中身と範囲が食い違わない）
	int32_t shiftX = 0;
	int32_t shiftZ = 0;
	if (scrollWithCenter_) {
		if (clearRequested_) {
			gridCenter_ = center_;
		} else {
			const float texelX = std::round((center_.x - gridCenter_.x) / (std::max)(dx, 1e-4f));
			const float texelZ = std::round((center_.y - gridCenter_.y) / (std::max)(dx, 1e-4f));
			const float limit = static_cast<float>(kResolution);
			if (std::abs(texelX) >= limit || std::abs(texelZ) >= limit) {
				// 範囲より大きく飛んだ（シーク等）。残る波が無いので消してそこから始める
				clearRequested_ = true;
				gridCenter_ = center_;
			} else {
				shiftX = static_cast<int32_t>(texelX);
				shiftZ = static_cast<int32_t>(texelZ);
				gridCenter_.x += texelX * dx;
				gridCenter_.y += texelZ * dx;
			}
		}
	}

	// k = (c·Δt/Δx)²。0.5 以上だと発散するので手前で止める
	const float courant = settings_.waveSpeed * step / (std::max)(dx, 1e-4f);
	const float waveFactor = (std::min)(courant * courant, 0.45f);

	cmd->SetComputeRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	const UINT groups = (kResolution + kThreadGroupSize - 1) / kThreadGroupSize;

	for (uint32_t s = 0; s < steps; ++s) {
		SimParamsForGPU& p = *paramsData_[s];
		p.waveFactor = waveFactor;
		p.damping = std::clamp(settings_.damping, 0.0f, 1.0f);
		p.edgeFade = std::clamp(settings_.edgeFade, 0.001f, 0.5f);
		p.resolution = kResolution;
		p.clear = (s == 0 && clearRequested_) ? 1u : 0u;
		// 波源とずらしは最初の刻みにだけ入れる
		p.impulseCount = (s == 0) ? BuildImpulses(p.impulses) : 0u;
		p.shift[0] = (s == 0) ? shiftX : 0;
		p.shift[1] = (s == 0) ? shiftZ : 0;

		const uint32_t src = current_;
		const uint32_t dst = 1u - current_;
		Transition(src, kReadState);
		Transition(dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmd->SetComputeRootConstantBufferView(kRootParams, paramsResources_[s]->GetGPUVirtualAddress());
		cmd->SetComputeRootDescriptorTable(kRootSource, srvManager_->GetGPUDescriptorHandle(srvIndices_[src]));
		cmd->SetComputeRootDescriptorTable(kRootDestination, srvManager_->GetGPUDescriptorHandle(uavIndices_[dst]));
		cmd->Dispatch(groups, groups, 1);
		current_ = dst;
	}

	// PS から読めるようにしておく
	Transition(current_, kReadState);
	impulseCount_ = 0;
	clearRequested_ = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE RippleSimulation::GetHeightSrvHandle() const
{
	return srvManager_->GetGPUDescriptorHandle(srvIndices_[current_]);
}
