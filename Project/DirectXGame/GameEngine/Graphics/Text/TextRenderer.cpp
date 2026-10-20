#include "TextRenderer.h"
#include "FontAtlas.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "WindowsApplication.h"
#include "Utf8.h"
#include "Log.h"
#include "PepperMacros.h"
#include "BloomEffect.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <d3dcompiler.h>

TextRenderer* TextRenderer::GetInstance()
{
	static TextRenderer instance;
	return &instance;
}

void TextRenderer::Initialize(DirectXCore* dxCore, SRVManager* srvManager,
	const std::string& ttfPath, int pixelHeight, int atlasSize)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;

	atlas_ = std::make_unique<FontAtlas>();
	if (!atlas_->Initialize(dxCore_, srvManager_, ttfPath, pixelHeight, atlasSize)) {
		Log("[TextRenderer] FontAtlas initialization failed\n");
		atlas_.reset();
		return;
	}

	CreateRootSignature();
	CreatePipelineState();
	CreateInstanceBuffer();

	// ASCII+仮名を CPU 側でラスタライズしておく（テクスチャ転送は Flush 時）
	atlas_->PreloadDefaultCharset(nullptr);

	layers_[kLayerScreen].reserve(kMaxInstances);
	layers_[kLayerGlow].reserve(1024);
	layers_[kLayerOffscreen].reserve(256);
	initialized_ = true;
}

void TextRenderer::Finalize()
{
	if (instanceResource_) {
		instanceResource_->Unmap(0, nullptr);
		instanceData_ = nullptr;
	}
	if (atlas_) { atlas_->Finalize(); atlas_.reset(); }
	instanceResource_.Reset();
	pipelineState_.Reset();
	glowPipelineState_.Reset();
	rootSignature_.Reset();
	for (auto& layer : layers_) layer.clear();
	initialized_ = false;
}

void TextRenderer::CreateRootSignature()
{
	// DescriptorRange: SRV(t0) for VS (StructuredBuffer)
	D3D12_DESCRIPTOR_RANGE rangeInst[1] = {};
	rangeInst[0].BaseShaderRegister = 0;
	rangeInst[0].NumDescriptors = 1;
	rangeInst[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeInst[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// DescriptorRange: SRV(t0) for PS (atlas)
	D3D12_DESCRIPTOR_RANGE rangeAtlas[1] = {};
	rangeAtlas[0].BaseShaderRegister = 0;
	rangeAtlas[0].NumDescriptors = 1;
	rangeAtlas[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeAtlas[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER params[3] = {};
	// [0] VS DescriptorTable - instances (t0)
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[0].DescriptorTable.pDescriptorRanges = rangeInst;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	// [1] VS ルート定数(b0) - 描く先の大きさ（ピクセル）と、層の区画の先頭のインスタンス番号
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[1].Constants.ShaderRegister = 0;
	params[1].Constants.RegisterSpace = 0;
	params[1].Constants.Num32BitValues = 4;
	// [2] PS DescriptorTable - atlas (t0)
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[2].DescriptorTable.pDescriptorRanges = rangeAtlas;
	params[2].DescriptorTable.NumDescriptorRanges = 1;

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
	desc.NumParameters = _countof(params);
	desc.pParameters = params;
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &sampler;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr)) {
		if (errBlob) Log(reinterpret_cast<const char*>(errBlob->GetBufferPointer()));
		assert(false);
	}
	hr = dxCore_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(),
		sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void TextRenderer::CreatePipelineState()
{
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Text/Text.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Text/Text.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	// 入力レイアウトは不要（SV_VertexID で頂点を生成）
	D3D12_INPUT_LAYOUT_DESC inputLayout{};
	inputLayout.pInputElementDescs = nullptr;
	inputLayout.NumElements = 0;

	D3D12_RASTERIZER_DESC rs{};
	rs.CullMode = D3D12_CULL_MODE_NONE;
	rs.FillMode = D3D12_FILL_MODE_SOLID;

	// アルファブレンド（通常）
	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	psoDesc.InputLayout = inputLayout;
	psoDesc.BlendState = blend;
	psoDesc.RasterizerState = rs;
	psoDesc.DepthStencilState = depth;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	// 発光の層：ブルームの発光 RT（float）へ加算（色 × α を足す。重なるほど強く光る）
	psoDesc.RTVFormats[0] = BloomEffect::kFormat;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&glowPipelineState_));
	assert(SUCCEEDED(hr));
}

void TextRenderer::CreateInstanceBuffer()
{
	// 層ごとに kMaxInstances ずつの区画（同じフレームに複数の層を描いても上書きし合わない）
	const uint32_t total = kMaxInstances * kLayerCount;
	instanceResource_ = dxCore_->CreateBufferResource(sizeof(GlyphInstance) * total);
	instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));

	instanceSrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVForStructuredBuffer(instanceSrvIndex_, instanceResource_.Get(),
		total, sizeof(GlyphInstance));
}

void TextRenderer::SetTransform(const Vector2& center, const Vector2& scale)
{
	hasTransform_ = true;
	transformCenter_ = center;
	transformScale_ = scale;
}

void TextRenderer::ResetTransform()
{
	hasTransform_ = false;
	transformScale_ = { 1.0f, 1.0f };
}

void TextRenderer::PushText(std::vector<GlyphInstance>& out, std::string_view utf8, const Vector2& pos, float scale,
	const Vector4& color, float outlineThickness, const Vector4& outlineColor)
{
	if (!initialized_ || !atlas_) return;
	if (out.size() >= kMaxInstances) return;

	const float baseline = atlas_->GetAscent() * scale;
	float penX = pos.x;
	const float penY = pos.y + baseline;

	size_t i = 0;
	while (i < utf8.size()) {
		uint32_t cp = Utf8::DecodeNext(utf8, i);
		if (cp == 0) break;

		const FontAtlas::GlyphInfo& g = atlas_->GetOrBake(cp);
		if (!g.valid) continue;

		if (g.width > 0 && g.height > 0) {
			if (out.size() >= kMaxInstances) break;
			GlyphInstance inst{};
			inst.screenPosX = penX + g.xoff * scale;
			inst.screenPosY = penY + g.yoff * scale;
			inst.sizeX = g.width * scale;
			inst.sizeY = g.height * scale;
			if (hasTransform_) {
				inst.screenPosX = transformCenter_.x + (inst.screenPosX - transformCenter_.x) * transformScale_.x;
				inst.screenPosY = transformCenter_.y + (inst.screenPosY - transformCenter_.y) * transformScale_.y;
				inst.sizeX *= transformScale_.x;
				inst.sizeY *= transformScale_.y;
			}
			inst.u0 = g.u0; inst.v0 = g.v0; inst.u1 = g.u1; inst.v1 = g.v1;
			inst.r = color.x; inst.g = color.y; inst.b = color.z; inst.a = color.w;
			inst.outR = outlineColor.x; inst.outG = outlineColor.y;
			inst.outB = outlineColor.z; inst.outA = outlineColor.w;
			inst.outlineWidth = outlineThickness;
			out.push_back(inst);
		}
		penX += g.advance * scale;
	}
}

void TextRenderer::PushRect(std::vector<GlyphInstance>& out, const Vector2& pos, const Vector2& size, const Vector4& color)
{
	if (!initialized_ || out.size() >= kMaxInstances) return;
	GlyphInstance inst{};
	inst.screenPosX = pos.x;
	inst.screenPosY = pos.y;
	inst.sizeX = size.x;
	inst.sizeY = size.y;
	if (hasTransform_) {
		inst.screenPosX = transformCenter_.x + (inst.screenPosX - transformCenter_.x) * transformScale_.x;
		inst.screenPosY = transformCenter_.y + (inst.screenPosY - transformCenter_.y) * transformScale_.y;
		inst.sizeX *= transformScale_.x;
		inst.sizeY *= transformScale_.y;
	}
	inst.r = color.x; inst.g = color.y; inst.b = color.z; inst.a = color.w;
	inst.outlineWidth = -1.0f;   // 塗りつぶし（Text.PS がアトラスを読まない）
	out.push_back(inst);
}

void TextRenderer::DrawText(std::string_view utf8, const Vector2& pos, float scale, const Vector4& color,
	float outlineThickness, const Vector4& outlineColor)
{
	PushText(CurrentLayer(), utf8, pos, scale, color, outlineThickness, outlineColor);
}

void TextRenderer::DrawRect(const Vector2& pos, const Vector2& size, const Vector4& color)
{
	PushRect(CurrentLayer(), pos, size, color);
}

void TextRenderer::DrawTextGlow(std::string_view utf8, const Vector2& pos, float scale, const Vector4& color)
{
	PushText(layers_[kLayerGlow], utf8, pos, scale, color, 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f });
}

void TextRenderer::DrawRectGlow(const Vector2& pos, const Vector2& size, const Vector4& color)
{
	PushRect(layers_[kLayerGlow], pos, size, color);
}

float TextRenderer::MeasureWidth(std::string_view utf8, float scale)
{
	if (!initialized_ || !atlas_) return 0.0f;
	float w = 0.0f;
	size_t i = 0;
	while (i < utf8.size()) {
		uint32_t cp = Utf8::DecodeNext(utf8, i);
		if (cp == 0) break;
		const FontAtlas::GlyphInfo& g = atlas_->GetOrBake(cp);
		if (!g.valid) continue;
		w += g.advance * scale;
	}
	return w;
}

void TextRenderer::DrawLayer(Layer layer, ID3D12PipelineState* pso, float targetWidth, float targetHeight)
{
	std::vector<GlyphInstance>& instances = layers_[layer];
	if (!initialized_ || !atlas_) {
		instances.clear();
		return;
	}

	ID3D12GraphicsCommandList* cmd = dxCore_->GetCommandList();

	// アトラスへの bake をテクスチャに反映 → PSR に戻す
	atlas_->FlushPendingUploads(cmd);
	atlas_->EnsureShaderResourceState(cmd);

	if (instances.empty()) return;

	// CPU 側のインスタンスを、この層の区画の「このフレームで書いた続き」へ反映
	// （先頭へ書くと、同じフレームの前の Flush の分を GPU が描く前に上書きしてしまう）
	uint32_t& written = layerWritten_[layer];
	const uint32_t room = kMaxInstances - (std::min)(written, kMaxInstances);
	const uint32_t n = (std::min)(static_cast<uint32_t>(instances.size()), room);
	if (n == 0) {
		instances.clear();
		return;
	}
	const uint32_t base = kMaxInstances * static_cast<uint32_t>(layer) + written;
	std::memcpy(instanceData_ + base, instances.data(), sizeof(GlyphInstance) * n);
	written += n;

	struct RootConstants { float width, height; uint32_t instanceBase, pad; } constants{
		targetWidth, targetHeight, base, 0 };

	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pso);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmd->IASetVertexBuffers(0, 0, nullptr); // SV_VertexID で頂点生成

	srvManager_->SetGraphicsRootDescriptorTable(0, instanceSrvIndex_);
	cmd->SetGraphicsRoot32BitConstants(1, 4, &constants, 0);
	srvManager_->SetGraphicsRootDescriptorTable(2, atlas_->GetSrvIndex());

	PEPPER_COUNT("DrawCall");
	cmd->DrawInstanced(4, n, 0, 0);

	instances.clear();
}

void TextRenderer::BeginFrame()
{
	// 前のフレームの GPU の描画は Present の後の完了待ちで終わっているので、先頭から書き直してよい
	layerWritten_.fill(0);
}

void TextRenderer::Flush()
{
	if (!dxCore_) return;
	// 文字は内部解像度のシーン RT に描くので、ウィンドウの大きさではなく内部解像度で射影する
	DrawLayer(kLayerScreen, pipelineState_.Get(),
		static_cast<float>(dxCore_->GetRenderWidth()), static_cast<float>(dxCore_->GetRenderHeight()));
}

void TextRenderer::FlushGlow()
{
	if (!dxCore_) return;
	// 発光 RT は画面と同じ大きさ
	DrawLayer(kLayerGlow, glowPipelineState_.Get(),
		static_cast<float>(dxCore_->GetRenderWidth()), static_cast<float>(dxCore_->GetRenderHeight()));
}

void TextRenderer::BeginOffscreen(float width, float height)
{
	offscreenActive_ = true;
	offscreenWidth_ = width;
	offscreenHeight_ = height;
}

void TextRenderer::FlushOffscreen()
{
	offscreenActive_ = false;
	if (!dxCore_) return;
	DrawLayer(kLayerOffscreen, pipelineState_.Get(), offscreenWidth_, offscreenHeight_);
}
