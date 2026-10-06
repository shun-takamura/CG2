#include "BloomEffect.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include <cassert>
#include <algorithm>
#include <iterator>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

BloomEffect::BloomEffect() = default;
BloomEffect::~BloomEffect() = default;

void BloomEffect::InitializeBloom(
	DirectXCore* dxCore,
	SRVManager* srvManager,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
	uint32_t width,
	uint32_t height)
{
	srvManager_ = srvManager;

	CreateRootSignature(dxCore);
	CreatePipelines(dxCore, basePsoDesc);

	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	uint32_t w = width;
	uint32_t h = height;
	for (auto& level : levels_) {
		w = (std::max)(w / 2, 1u);
		h = (std::max)(h / 2, 1u);
		level.ping = std::make_unique<RenderTexture>();
		level.ping->Initialize(dxCore, srvManager, w, h, kFormat, clear);
		level.pong = std::make_unique<RenderTexture>();
		level.pong->Initialize(dxCore, srvManager, w, h, kFormat, clear);
	}
}

void BloomEffect::CreateRootSignature(DirectXCore* dxCore)
{
	// t0..t4 を1枚ずつのテーブルで持つ（縮小段の SRV は連番とは限らないため）
	D3D12_DESCRIPTOR_RANGE ranges[kLevelCount + 1] = {};
	D3D12_ROOT_PARAMETER rootParameters[kLevelCount + 2] = {};
	for (uint32_t i = 0; i < kLevelCount + 1; ++i) {
		ranges[i].BaseShaderRegister = i;
		ranges[i].NumDescriptors = 1;
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		rootParameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
		rootParameters[i].DescriptorTable.NumDescriptorRanges = 1;
	}
	// b0：パラメータはパスごとに変わるのでルート定数（パス数分の CB を持たずに済む）
	D3D12_ROOT_PARAMETER& constants = rootParameters[kLevelCount + 1];
	constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	constants.Constants.ShaderRegister = 0;
	constants.Constants.RegisterSpace = 0;
	constants.Constants.Num32BitValues = kParamCount;

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
	desc.pParameters = rootParameters;
	desc.NumParameters = _countof(rootParameters);
	desc.pStaticSamplers = &sampler;
	desc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = dxCore->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void BloomEffect::CreatePipelines(DirectXCore* dxCore, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc)
{
	auto create = [&](const wchar_t* psPath, DXGI_FORMAT rtvFormat, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
		IDxcBlob* psBlob = dxCore->LoadShaderBlob(psPath, L"ps_6_0");
		assert(psBlob);
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = rootSignature_.Get();
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
		psoDesc.RTVFormats[0] = rtvFormat;
		HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out));
		assert(SUCCEEDED(hr));
	};

	create(L"Resources/Shaders/PostEffect/Filters/BloomDownsample.PS.hlsl", kFormat, downsamplePso_);
	create(L"Resources/Shaders/PostEffect/Filters/BloomBlur.PS.hlsl", kFormat, blurPso_);
	create(L"Resources/Shaders/PostEffect/Filters/BloomComposite.PS.hlsl", basePsoDesc.RTVFormats[0], pipelineState_);
}

void BloomEffect::DrawFullscreen(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pso,
	const uint32_t (&srvs)[kLevelCount + 1], const Params& params)
{
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pso);
	for (uint32_t i = 0; i < kLevelCount + 1; ++i) {
		srvManager_->SetGraphicsRootDescriptorTable(i, srvs[i]);
	}
	commandList->SetGraphicsRoot32BitConstants(kLevelCount + 1, kParamCount, &params, 0);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void BloomEffect::Prepare(ID3D12GraphicsCommandList* commandList, uint32_t sourceSrvIndex,
	uint32_t sourceWidth, uint32_t sourceHeight)
{
	srvManager_->PreDraw();

	uint32_t srcSrv = sourceSrvIndex;
	float srcW = static_cast<float>((std::max)(sourceWidth, 1u));
	float srcH = static_cast<float>((std::max)(sourceHeight, 1u));

	// 縮小/ぼかしは t0 しか読まないが、使わない t1..t4 にも同じ SRV を入れて全テーブルを埋める
	uint32_t srvs[kLevelCount + 1];

	for (auto& level : levels_) {
		const float levelW = static_cast<float>(level.ping->GetWidth());
		const float levelH = static_cast<float>(level.ping->GetHeight());

		// 縮小（入力のテクセル間隔で 4 点を拾い平均）
		Params p{};
		p.texelSize[0] = 1.0f / srcW;
		p.texelSize[1] = 1.0f / srcH;
		level.ping->BeginRender(commandList);
		std::fill(std::begin(srvs), std::end(srvs), srcSrv);
		DrawFullscreen(commandList, downsamplePso_.Get(), srvs, p);
		level.ping->EndRender(commandList);

		// 横ぼかし ping → pong、縦ぼかし pong → ping
		p.texelSize[0] = 1.0f / levelW;
		p.texelSize[1] = 1.0f / levelH;
		const struct { RenderTexture* src; RenderTexture* dst; float dx; float dy; } passes[2] = {
			{ level.ping.get(), level.pong.get(), 1.0f, 0.0f },
			{ level.pong.get(), level.ping.get(), 0.0f, 1.0f },
		};
		for (const auto& pass : passes) {
			p.direction[0] = pass.dx;
			p.direction[1] = pass.dy;
			pass.dst->BeginRender(commandList);
			std::fill(std::begin(srvs), std::end(srvs), pass.src->GetSRVIndex());
			DrawFullscreen(commandList, blurPso_.Get(), srvs, p);
			pass.dst->EndRender(commandList);
		}

		srcSrv = level.ping->GetSRVIndex();
		srcW = levelW;
		srcH = levelH;
	}
}

void BloomEffect::Composite(ID3D12GraphicsCommandList* commandList, uint32_t sceneSrvIndex)
{
	uint32_t srvs[kLevelCount + 1];
	srvs[0] = sceneSrvIndex;
	for (uint32_t i = 0; i < kLevelCount; ++i) {
		srvs[i + 1] = levels_[i].ping->GetSRVIndex();
	}

	Params p{};
	p.levelWeights = levelWeights_;
	p.tint = tint_;
	p.intensity = (std::max)(intensity_, 0.0f);
	DrawFullscreen(commandList, pipelineState_.Get(), srvs, p);
}

void BloomEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::SliderFloat("Intensity##Bloom", &intensity_, 0.0f, 4.0f);
	ImGui::ColorEdit3("Tint##Bloom", &tint_.x);
	ImGui::SliderFloat4("Level Weights##Bloom", &levelWeights_.x, 0.0f, 2.0f);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("1/2, 1/4, 1/8, 1/16 段の重み。右ほど広く柔らかい光");
	}
#endif
}

void BloomEffect::ResetParams()
{
	intensity_ = 1.0f;
	tint_ = { 1.0f, 1.0f, 1.0f };
	levelWeights_ = { 1.0f, 0.8f, 0.6f, 0.4f };
}

void BloomEffect::Finalize()
{
	for (auto& level : levels_) {
		if (level.ping) { level.ping->Finalize(); level.ping.reset(); }
		if (level.pong) { level.pong->Finalize(); level.pong.reset(); }
	}
	downsamplePso_.Reset();
	blurPso_.Reset();
	rootSignature_.Reset();
	BaseFilterEffect::Finalize();
}
