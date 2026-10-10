#include "CloudRaymarcher.h"

#include "Camera.h"
#include "DirectXCore.h"
#include "Json/JsonValue.h"
#include "Log.h"
#include "MathUtility.h"
#include "RenderTexture.h"
#include "PepperMacros.h"
#include "SRVManager.h"
#include "TextureManager.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";
	constexpr const char* kMokoPath = "Resources/Textures/Cloud/cloud_moko_height.dds";
	constexpr uint32_t kNoiseSize = 512;                  // cloud_noise.dds の一辺（ミップ選択に使う）
	constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	constexpr DXGI_FORMAT kSceneFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// ルートパラメータ番号
	enum RootParam : UINT {
		kRootConstants = 0, // PS b0
		kRootTexture0,      // PS t0 深度（雲を描く）/ 雲の RT（重ねる）
		kRootNoise,         // PS t1 雲のノイズ
		kRootSkyCube,       // PS t2 空の cubemap（空気遠近）
		kRootMoko,          // PS t3 もこもこの高さ
		kRootCount
	};

	float Wrap01(float v) { return v - std::floor(v); }

	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

	float Distance(const Vector3& a, const Vector3& b) { return Length(Sub(a, b)); }

	// 折れ線が高さ y を最初に横切る点
	bool CrossHeight(const std::vector<Vector3>& points, float y, Vector3& out) {
		for (size_t i = 0; i + 1 < points.size(); ++i) {
			const Vector3& a = points[i];
			const Vector3& b = points[i + 1];
			if ((a.y - y) * (b.y - y) > 0.0f || a.y == b.y) continue;
			const float t = (y - a.y) / (b.y - a.y);
			out = { a.x + (b.x - a.x) * t, y, a.z + (b.z - a.z) * t };
			return true;
		}
		return false;
	}

	Vector2 ReadVec2(const JsonValue& j, const Vector2& fallback) {
		if (!j.IsArray() || j.Size() < 2) return fallback;
		return { static_cast<float>(j[0].AsDouble(fallback.x)), static_cast<float>(j[1].AsDouble(fallback.y)) };
	}
	Vector3 ReadVec3(const JsonValue& j, const Vector3& fallback) {
		if (!j.IsArray() || j.Size() < 3) return fallback;
		return { static_cast<float>(j[0].AsDouble(fallback.x)), static_cast<float>(j[1].AsDouble(fallback.y)),
			static_cast<float>(j[2].AsDouble(fallback.z)) };
	}
	JsonValue WriteVec2(const Vector2& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		return a;
	}
	JsonValue WriteVec3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
}

CloudRaymarcher::CloudRaymarcher() = default;
CloudRaymarcher::~CloudRaymarcher() {
	if (target_) target_->Finalize();
}

void CloudRaymarcher::Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t screenWidth, uint32_t screenHeight)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	TextureManager::GetInstance()->LoadTexture(kNoisePath);
	TextureManager::GetInstance()->LoadTexture(kMokoPath);

	CreateRootSignature();
	marchPipeline_ = CreatePipelineState(L"Resources/Shaders/Cloud/CloudRaymarch.PS.hlsl", kTargetFormat, false);
	compositePipeline_ = CreatePipelineState(L"Resources/Shaders/Cloud/CloudComposite.PS.hlsl", kSceneFormat, true);
	CreateTarget();

	constantsResource_ = dxCore_->CreateBufferResource((sizeof(ConstantsForGPU) + 255) & ~255);
	constantsResource_->Map(0, nullptr, reinterpret_cast<void**>(&constantsData_));
	*constantsData_ = ConstantsForGPU{};
}

void CloudRaymarcher::CreateRootSignature()
{
	D3D12_DESCRIPTOR_RANGE ranges[4] = {};
	for (UINT i = 0; i < 4; ++i) {
		ranges[i].BaseShaderRegister = i;
		ranges[i].NumDescriptors = 1;
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_PARAMETER params[kRootCount] = {};
	params[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootConstants].Descriptor.ShaderRegister = 0;
	for (UINT i = 0; i < 4; ++i) {
		D3D12_ROOT_PARAMETER& p = params[kRootTexture0 + i];
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		p.DescriptorTable.pDescriptorRanges = &ranges[i];
		p.DescriptorTable.NumDescriptorRanges = 1;
	}

	// s0 = ラップ・線形（ノイズ）, s1 = クランプ・点（深度）, s2 = クランプ・線形（cubemap / 雲の RT の拡大）
	D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
	for (UINT i = 0; i < 3; ++i) {
		samplers[i].ShaderRegister = i;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.pParameters = params;
	desc.NumParameters = _countof(params);
	desc.pStaticSamplers = samplers;
	desc.NumStaticSamplers = _countof(samplers);

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

Microsoft::WRL::ComPtr<ID3D12PipelineState> CloudRaymarcher::CreatePipelineState(
	const wchar_t* psPath, DXGI_FORMAT rtvFormat, bool premultipliedBlend)
{
	// 頂点バッファなしの全画面三角形（ポストエフェクトと共用）
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/PostEffect/Common/PostProcess.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(psPath, L"ps_6_0");
	assert(vs && ps);

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_BLEND_DESC blend{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if (premultipliedBlend) {
		// 雲の RT は乗算済み：シーン × (1 − a) ＋ 雲。シーンの α はそのまま残す
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}

	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depth;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = rtvFormat;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
	assert(SUCCEEDED(hr));
	return pipelineState;
}

void CloudRaymarcher::CreateTarget()
{
	if (target_) target_->Finalize();
	targetScale_ = std::clamp(params_.resolutionScale, 0.125f, 1.0f);
	const uint32_t width = (std::max)(1u, static_cast<uint32_t>(screenWidth_ * targetScale_));
	const uint32_t height = (std::max)(1u, static_cast<uint32_t>(screenHeight_ * targetScale_));
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	target_ = std::make_unique<RenderTexture>();
	target_->Initialize(dxCore_, srvManager_, width, height, kTargetFormat, clear);
}

void CloudRaymarcher::SetShaftPath(const std::vector<Vector3>& points)
{
	shaftPoints_.clear();
	if (points.size() >= 2) {
		// 16 点に収まるよう等間隔に間引く（端は必ず残す）
		const size_t n = (std::min)(points.size(), static_cast<size_t>(kShaftPointMax));
		for (size_t i = 0; i < n; ++i) {
			const size_t src = (n == 1) ? 0 : i * (points.size() - 1) / (n - 1);
			shaftPoints_.push_back(points[src]);
		}
	}
	hasEntry_ = CrossHeight(shaftPoints_, params_.top, shaftEntry_);
	hasExit_ = CrossHeight(shaftPoints_, params_.bottom, shaftExit_);
}

void CloudRaymarcher::SetSunDirection(const Vector3& towardSun)
{
	sunDirection_ = Normalize(towardSun);
}

void CloudRaymarcher::Update(float deltaTime)
{
	time_ += deltaTime;
	const auto scroll = [deltaTime](Vector2& offset, const Vector2& wind, float scale) {
		offset.x = Wrap01(offset.x - wind.x / scale * deltaTime);
		offset.y = Wrap01(offset.y - wind.y / scale * deltaTime);
	};
	scroll(shapeOffset_, params_.shapeWind, (std::max)(params_.shapeScale, 1.0f));
	// もこもこは大きい形と同じ風で流す（こぶだけが雲の上を滑って見えないように）
	scroll(mokoOffset_, params_.shapeWind, (std::max)(params_.mokoScale, 1.0f));
	scroll(maskOffset_, params_.maskWind, (std::max)(params_.maskScale, 1.0f));
	// 3D ノイズは時間×風で直接ずらす（周期が無いので折り返せない）。長時間で精度が落ちないよう 1 時間で戻す
	if (time_ > 3600.0f) time_ -= 3600.0f;
}

float CloudRaymarcher::ComputeVolumeWeight(const Vector3& eye) const
{
	const auto weightAt = [&](const Vector3& center, float inner, float outer) {
		outer = (std::max)(outer, inner + 1.0f);
		const float t = std::clamp((Distance(eye, center) - inner) / (outer - inner), 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	};
	float w = 0.0f;
	if (hasEntry_) w = (std::max)(w, weightAt(shaftEntry_, params_.volumeZoneInner, params_.volumeZoneOuter));
	if (hasExit_) w = (std::max)(w, weightAt(shaftExit_, params_.exitZoneInner, params_.exitZoneOuter));

	// 縦穴の中（層の高さの中で、中心線から半径以内）にいる間は volumeShaftWeight。壁の手前がちぎれたもやになり、筒の見通しは残る
	if (shaftPoints_.size() >= 2 && eye.y <= params_.top && eye.y >= params_.bottom) {
		float best = 1e9f;
		for (size_t i = 0; i + 1 < shaftPoints_.size(); ++i) {
			const Vector3 a = shaftPoints_[i];
			const Vector3 ab = Sub(shaftPoints_[i + 1], a);
			const float len2 = (std::max)(Dot(ab, ab), 1e-3f);
			const float t = std::clamp(Dot(Sub(eye, a), ab) / len2, 0.0f, 1.0f);
			best = (std::min)(best, Distance(eye, { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t }));
		}
		const float x = std::clamp((best - params_.shaftRadius) / (std::max)(params_.volumeShaftBlend, 1.0f), 0.0f, 1.0f);
		w = (std::max)(w, (1.0f - x * x * (3.0f - 2.0f * x)) * params_.volumeShaftWeight);
	}
	return w;
}

void CloudRaymarcher::WriteConstants(const Camera& camera)
{
	const Params& p = params_;
	const float shapeScale = (std::max)(p.shapeScale, 1.0f);
	const Vector3& eye = camera.GetTranslate();
	lastVolumeWeight_ = ComputeVolumeWeight(eye);

	ConstantsForGPU c{};
	c.invViewProj = Inverse(camera.GetViewProjectionMatrix());
	c.eye = eye;
	c.enabled = p.enabled ? 1 : 0;
	c.sunDirection = sunDirection_;
	c.opacity = std::clamp(p.opacity, 0.0f, 1.0f);
	c.top = p.top;
	c.bottom = (std::min)(p.bottom, p.top - 1.0f);
	c.seaDepth = (std::max)(p.seaDepth, 1.0f);
	c.bottomFade = (std::max)(p.bottomFade, 1.0f);
	c.noiseFloor = std::clamp(p.noiseFloor, 0.0f, 0.99f);
	c.jitter = std::clamp(p.jitter, 0.0f, 1.0f);
	c.wrap = std::clamp(p.wrap, 0.0f, 1.0f);
	c.valley = std::clamp(p.valley, 0.0f, 1.0f);
	c.valleyPow = (std::max)(p.valleyPow, 0.01f);
	c.traceSlope = (std::max)(p.traceSlope, 0.01f);
	c.traceEpsilon = (std::max)(p.traceEpsilon, 0.01f);
	c.shaftWallBlend = (std::max)(p.shaftWallBlend, 0.1f);
	c.shaftDarkness = std::clamp(p.shaftDarkness, 0.0f, 1.0f);
	c.undersideBright = std::clamp(p.undersideBright, 0.0f, 1.0f);
	c.capTop = (std::max)(p.capTop, 0.0f);
	c.floorDepth = (std::max)(p.floorDepth, 1.0f);
	c.capBottom = (std::max)(p.capBottom, 0.0f);
	c.erosionSharp = (std::max)(p.erosionSharp, 0.01f);
	c.lightExtinction = (std::max)(p.lightExtinction, 0.0f);
	c.shapeOffset = shapeOffset_;
	c.mokoOffset = mokoOffset_;
	c.maskOffset = maskOffset_;
	c.shapeTiling = 1.0f / shapeScale;
	c.mokoTiling = 1.0f / (std::max)(p.mokoScale, 1.0f);
	c.maskTiling = 1.0f / (std::max)(p.maskScale, 1.0f);
	c.mokoWeight = std::clamp(p.mokoWeight, 0.0f, 1.0f);
	c.normalEps = (std::max)(p.normalEps, 0.1f);
	c.maskStrength = p.maskStrength;
	c.coverage = p.coverage;
	c.sharpness = p.sharpness;
	// RT の 1 ピクセルが見込む角度 × ノイズの解像度。距離とタイリングを掛けると「1 ピクセルに入るテクセル数」になる
	const float pixelAngle = 2.0f * std::tan(camera.GetFovY() * 0.5f) / (std::max)(1.0f, static_cast<float>(target_->GetHeight()));
	c.lodScale = pixelAngle * static_cast<float>(kNoiseSize);
	c.time = time_;
	c.steps = std::clamp(p.steps, 4, 128);
	c.firstStep = (std::max)(p.firstStep, 0.1f);
	c.maxDistance = (std::max)(p.maxDistance, 10.0f);
	c.lightStep = p.lightStep;
	c.density = p.density;
	c.fadeStart = p.fadeStart;
	c.fadeEnd = (std::max)(p.fadeEnd, p.fadeStart + 1.0f);
	c.hazeStart = p.hazeStart;
	c.hazeEnd = (std::max)(p.hazeEnd, p.hazeStart + 1.0f);
	c.hazeMax = p.hazeMax;
	c.rimIntensity = p.rimIntensity;
	c.litColor = p.litColor;
	c.rimG = std::clamp(p.rimG, 0.0f, 0.95f);
	c.shadowColor = p.shadowColor;
	c.ambient = std::clamp(p.ambient, 0.0f, 1.0f);
	c.rimColor = p.rimColor;
	c.sunGlow = p.sunGlow;

	// 縦穴：折れ線の外接箱（半径＋揺れ＋ぼかしだけ広げる）の外では距離計算を省く
	c.shaftCount = static_cast<int>(shaftPoints_.size());
	c.shaftRadius = p.shaftRadius;
	c.shaftWallDepth = (std::max)(p.shaftWallDepth, 1.0f);
	c.capDepth = (std::max)(p.capDepth, 1.0f);
	c.shaftNoiseTiling = 1.0f / (std::max)(p.shaftNoiseScale, 0.01f);
	if (!shaftPoints_.empty()) {
		const float pad = p.shaftRadius + p.shaftWallDepth * 1.5f;
		Vector3 mn = shaftPoints_[0];
		Vector3 mx = shaftPoints_[0];
		for (size_t i = 0; i < shaftPoints_.size(); ++i) {
			const Vector3& s = shaftPoints_[i];
			mn = { (std::min)(mn.x, s.x), (std::min)(mn.y, s.y), (std::min)(mn.z, s.z) };
			mx = { (std::max)(mx.x, s.x), (std::max)(mx.y, s.y), (std::max)(mx.z, s.z) };
			c.shaftPoints[i][0] = s.x;
			c.shaftPoints[i][1] = s.y;
			c.shaftPoints[i][2] = s.z;
		}
		c.shaftMin = { mn.x - pad, mn.y - pad, mn.z - pad };
		c.shaftMax = { mx.x + pad, mx.y + pad, mx.z + pad };
	}

	c.volumeWeight = lastVolumeWeight_;
	c.volumeSteps = std::clamp(p.volumeSteps, 4, 128);
	c.volumeRange = (std::max)(p.volumeRange, 1.0f);
	c.extinction = (std::max)(p.extinction, 0.0f);
	c.erosion = p.erosion;
	c.erosionScale = 1.0f / (std::max)(p.erosionScale, 0.1f);
	c.powder = p.powder;
	c.ambientStrength = p.ambientStrength;
	c.volumeLightStep = (std::max)(p.volumeLightStep, 0.1f);
	c.erosionWind = { p.erosionWind.x * c.erosionScale, p.erosionWind.y * c.erosionScale, p.erosionWind.z * c.erosionScale };
	c.invTargetSize = { 1.0f / static_cast<float>(target_->GetWidth()), 1.0f / static_cast<float>(target_->GetHeight()) };
	*constantsData_ = c;
}

void CloudRaymarcher::Draw(const Camera& camera, RenderTexture* sceneTarget, const std::string& skyCubemapPath)
{
	if (!params_.enabled || params_.opacity <= 0.0f || !sceneTarget) return;
	if (std::clamp(params_.resolutionScale, 0.125f, 1.0f) != targetScale_) CreateTarget();

	PEPPER_SCOPE("CloudRaymarcher::Draw");
	auto* cmd = dxCore_->GetCommandList();
	WriteConstants(camera);

	// 不透明物の深度を読む（この間は DSV を貼れない）
	dxCore_->TransitionDepthState(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	srvManager_->PreDraw();

	{
		PEPPER_GPU_SCOPE(cmd, "CloudRaymarcher::March");
		target_->BeginRender(cmd);
		cmd->SetGraphicsRootSignature(rootSignature_.Get());
		cmd->SetPipelineState(marchPipeline_.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->SetGraphicsRootConstantBufferView(kRootConstants, constantsResource_->GetGPUVirtualAddress());
		srvManager_->SetGraphicsRootDescriptorTable(kRootTexture0, dxCore_->GetDepthSRVIndex());
		cmd->SetGraphicsRootDescriptorTable(kRootNoise, TextureManager::GetInstance()->GetSrvHandleGPU(kNoisePath));
		cmd->SetGraphicsRootDescriptorTable(kRootSkyCube, TextureManager::GetInstance()->GetSrvHandleGPU(skyCubemapPath));
		cmd->SetGraphicsRootDescriptorTable(kRootMoko, TextureManager::GetInstance()->GetSrvHandleGPU(kMokoPath));
		cmd->DrawInstanced(3, 1, 0, 0);
		target_->EndRender(cmd);
	}

	{
		PEPPER_GPU_SCOPE(cmd, "CloudRaymarcher::Composite");
		const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = sceneTarget->GetRTVHandle();
		cmd->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(sceneTarget->GetWidth()),
			static_cast<float>(sceneTarget->GetHeight()), 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(sceneTarget->GetWidth()), static_cast<LONG>(sceneTarget->GetHeight()) };
		cmd->RSSetViewports(1, &viewport);
		cmd->RSSetScissorRects(1, &scissor);
		cmd->SetGraphicsRootSignature(rootSignature_.Get());
		cmd->SetPipelineState(compositePipeline_.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		srvManager_->SetGraphicsRootDescriptorTable(kRootTexture0, target_->GetSRVIndex());
		cmd->DrawInstanced(3, 1, 0, 0);
	}

	// 後に続く半透明の描画のために、深度を書き込みに戻してシーン RT と DSV を貼り直す
	dxCore_->TransitionDepthState(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = sceneTarget->GetRTVHandle();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
	cmd->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
}

void CloudRaymarcher::LoadFromJson(const JsonValue& j)
{
	if (!j.IsObject()) return;
	Params& p = params_;
	const auto f = [&j](const char* key, float& v) { v = static_cast<float>(j[key].AsDouble(v)); };
	const auto i = [&j](const char* key, int& v) { v = static_cast<int>(j[key].AsInt(v)); };
	p.enabled = j["enabled"].AsBool(p.enabled);
	f("opacity", p.opacity);
	f("top", p.top); f("bottom", p.bottom); f("seaDepth", p.seaDepth); f("bottomFade", p.bottomFade);
	f("noiseFloor", p.noiseFloor);
	f("shapeScale", p.shapeScale); p.shapeWind = ReadVec2(j["shapeWind"], p.shapeWind);
	f("mokoScale", p.mokoScale); f("mokoWeight", p.mokoWeight);
	f("maskScale", p.maskScale); p.maskWind = ReadVec2(j["maskWind"], p.maskWind);
	f("maskStrength", p.maskStrength);
	f("coverage", p.coverage); f("sharpness", p.sharpness);
	i("steps", p.steps); f("firstStep", p.firstStep); f("maxDistance", p.maxDistance); f("jitter", p.jitter);
	f("traceSlope", p.traceSlope); f("traceEpsilon", p.traceEpsilon);
	f("normalEps", p.normalEps); f("wrap", p.wrap);
	f("lightStep", p.lightStep); f("density", p.density);
	f("valley", p.valley); f("valleyPow", p.valleyPow); f("ambient", p.ambient);
	p.litColor = ReadVec3(j["litColor"], p.litColor);
	p.shadowColor = ReadVec3(j["shadowColor"], p.shadowColor);
	p.rimColor = ReadVec3(j["rimColor"], p.rimColor);
	f("rimIntensity", p.rimIntensity); f("rimG", p.rimG); f("sunGlow", p.sunGlow);
	f("hazeStart", p.hazeStart); f("hazeEnd", p.hazeEnd); f("hazeMax", p.hazeMax);
	f("fadeStart", p.fadeStart); f("fadeEnd", p.fadeEnd);
	f("shaftRadius", p.shaftRadius); f("shaftWallDepth", p.shaftWallDepth);
	f("capDepth", p.capDepth); f("shaftNoiseScale", p.shaftNoiseScale);
	f("shaftWallBlend", p.shaftWallBlend); f("shaftDarkness", p.shaftDarkness); f("undersideBright", p.undersideBright);
	f("capTop", p.capTop); f("capBottom", p.capBottom); f("floorDepth", p.floorDepth);
	f("volumeZoneInner", p.volumeZoneInner); f("volumeZoneOuter", p.volumeZoneOuter); f("volumeShaftBlend", p.volumeShaftBlend); f("volumeShaftWeight", p.volumeShaftWeight);
	f("exitZoneInner", p.exitZoneInner); f("exitZoneOuter", p.exitZoneOuter);
	i("volumeSteps", p.volumeSteps); f("volumeRange", p.volumeRange);
	f("extinction", p.extinction); f("erosion", p.erosion); f("erosionScale", p.erosionScale);
	f("erosionSharp", p.erosionSharp); f("lightExtinction", p.lightExtinction);
	p.erosionWind = ReadVec3(j["erosionWind"], p.erosionWind);
	f("powder", p.powder); f("ambientStrength", p.ambientStrength); f("volumeLightStep", p.volumeLightStep);
	f("resolutionScale", p.resolutionScale);
	// 上面・下面が変わったら入口・出口も取り直す
	SetShaftPath(std::vector<Vector3>(shaftPoints_));
}

void CloudRaymarcher::SaveToJson(JsonValue& j) const
{
	const Params& p = params_;
	j = JsonValue::MakeObject();
	const auto f = [&j](const char* key, float v) { j[key] = JsonValue(static_cast<double>(v)); };
	const auto i = [&j](const char* key, int v) { j[key] = JsonValue(v); };
	j["enabled"] = JsonValue(p.enabled);
	f("opacity", p.opacity);
	f("top", p.top); f("bottom", p.bottom); f("seaDepth", p.seaDepth); f("bottomFade", p.bottomFade);
	f("noiseFloor", p.noiseFloor);
	f("shapeScale", p.shapeScale); j["shapeWind"] = WriteVec2(p.shapeWind);
	f("mokoScale", p.mokoScale); f("mokoWeight", p.mokoWeight);
	f("maskScale", p.maskScale); j["maskWind"] = WriteVec2(p.maskWind);
	f("maskStrength", p.maskStrength);
	f("coverage", p.coverage); f("sharpness", p.sharpness);
	i("steps", p.steps); f("firstStep", p.firstStep); f("maxDistance", p.maxDistance); f("jitter", p.jitter);
	f("traceSlope", p.traceSlope); f("traceEpsilon", p.traceEpsilon);
	f("normalEps", p.normalEps); f("wrap", p.wrap);
	f("lightStep", p.lightStep); f("density", p.density);
	f("valley", p.valley); f("valleyPow", p.valleyPow); f("ambient", p.ambient);
	j["litColor"] = WriteVec3(p.litColor);
	j["shadowColor"] = WriteVec3(p.shadowColor);
	j["rimColor"] = WriteVec3(p.rimColor);
	f("rimIntensity", p.rimIntensity); f("rimG", p.rimG); f("sunGlow", p.sunGlow);
	f("hazeStart", p.hazeStart); f("hazeEnd", p.hazeEnd); f("hazeMax", p.hazeMax);
	f("fadeStart", p.fadeStart); f("fadeEnd", p.fadeEnd);
	f("shaftRadius", p.shaftRadius); f("shaftWallDepth", p.shaftWallDepth);
	f("capDepth", p.capDepth); f("shaftNoiseScale", p.shaftNoiseScale);
	f("shaftWallBlend", p.shaftWallBlend); f("shaftDarkness", p.shaftDarkness); f("undersideBright", p.undersideBright);
	f("capTop", p.capTop); f("capBottom", p.capBottom); f("floorDepth", p.floorDepth);
	f("volumeZoneInner", p.volumeZoneInner); f("volumeZoneOuter", p.volumeZoneOuter); f("volumeShaftBlend", p.volumeShaftBlend); f("volumeShaftWeight", p.volumeShaftWeight);
	f("exitZoneInner", p.exitZoneInner); f("exitZoneOuter", p.exitZoneOuter);
	i("volumeSteps", p.volumeSteps); f("volumeRange", p.volumeRange);
	f("extinction", p.extinction); f("erosion", p.erosion); f("erosionScale", p.erosionScale);
	f("erosionSharp", p.erosionSharp); f("lightExtinction", p.lightExtinction);
	j["erosionWind"] = WriteVec3(p.erosionWind);
	f("powder", p.powder); f("ambientStrength", p.ambientStrength); f("volumeLightStep", p.volumeLightStep);
	f("resolutionScale", p.resolutionScale);
}

void CloudRaymarcher::OnImGui()
{
#ifdef _DEBUG
	Params& p = params_;
	ImGui::Checkbox("Enabled", &p.enabled);
	ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f);
	ImGui::Text("B' weight: %.2f  (entry %s / exit %s)", lastVolumeWeight_, hasEntry_ ? "ok" : "-", hasExit_ ? "ok" : "-");
	ImGui::SeparatorText("Layer");
	bool layerChanged = false;
	layerChanged |= ImGui::DragFloat("Top [m]", &p.top, 1.0f, -1000.0f, 5000.0f);
	layerChanged |= ImGui::DragFloat("Bottom [m]", &p.bottom, 1.0f, -1000.0f, 5000.0f);
	if (layerChanged) SetShaftPath(std::vector<Vector3>(shaftPoints_));
	ImGui::DragFloat("Sea Depth [m]", &p.seaDepth, 1.0f, 1.0f, 2000.0f);
	ImGui::DragFloat("Bottom Fade [m]", &p.bottomFade, 1.0f, 1.0f, 500.0f);
	ImGui::SliderFloat("Noise Floor", &p.noiseFloor, 0.0f, 0.95f);
	ImGui::SeparatorText("Shape");
	ImGui::DragFloat("Shape Scale [m]", &p.shapeScale, 10.0f, 10.0f, 50000.0f);
	ImGui::DragFloat2("Shape Wind [m/s]", &p.shapeWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::DragFloat("Moko Scale [m]", &p.mokoScale, 5.0f, 10.0f, 50000.0f);
	ImGui::SliderFloat("Moko Weight", &p.mokoWeight, 0.0f, 1.0f);
	ImGui::DragFloat("Mask Scale [m]", &p.maskScale, 10.0f, 10.0f, 100000.0f);
	ImGui::DragFloat2("Mask Wind [m/s]", &p.maskWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::SliderFloat("Mask Strength", &p.maskStrength, 0.0f, 1.0f);
	ImGui::SliderFloat("Coverage", &p.coverage, 0.0f, 1.0f);
	ImGui::DragFloat("Sharpness", &p.sharpness, 0.05f, 0.1f, 50.0f);
	ImGui::SeparatorText("March (Xenoblade)");
	ImGui::SliderInt("Steps", &p.steps, 4, 128);
	ImGui::DragFloat("Trace Slope", &p.traceSlope, 0.01f, 0.01f, 4.0f);
	ImGui::DragFloat("Trace Epsilon [m]", &p.traceEpsilon, 0.05f, 0.01f, 20.0f);
	ImGui::DragFloat("First Step [m]", &p.firstStep, 0.1f, 0.1f, 200.0f);
	ImGui::DragFloat("Max Distance [m]", &p.maxDistance, 100.0f, 100.0f, 50000.0f);
	ImGui::SliderFloat("Jitter (needs TAA)", &p.jitter, 0.0f, 1.0f);
	ImGui::SliderFloat("Resolution Scale", &p.resolutionScale, 0.125f, 1.0f);
	ImGui::SeparatorText("Light / Distance");
	ImGui::DragFloat("Normal Eps [m]", &p.normalEps, 0.5f, 0.1f, 200.0f);
	ImGui::SliderFloat("Wrap", &p.wrap, 0.0f, 1.0f);
	ImGui::DragFloat("Light Step [m]", &p.lightStep, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Density", &p.density, 0.01f, 0.0f, 10.0f);
	ImGui::SliderFloat("Valley", &p.valley, 0.0f, 1.0f);
	ImGui::DragFloat("Valley Pow", &p.valleyPow, 0.01f, 0.05f, 4.0f);
	ImGui::SliderFloat("Ambient", &p.ambient, 0.0f, 1.0f);
	ImGui::ColorEdit3("Lit Color", &p.litColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::ColorEdit3("Shadow Color", &p.shadowColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::ColorEdit3("Rim Color", &p.rimColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::DragFloat("Rim Intensity", &p.rimIntensity, 0.005f, 0.0f, 2.0f);
	ImGui::SliderFloat("Rim G", &p.rimG, 0.0f, 0.95f);
	ImGui::DragFloat("Sun Glow", &p.sunGlow, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Haze Start [m]", &p.hazeStart, 50.0f, 0.0f, 100000.0f);
	ImGui::DragFloat("Haze End [m]", &p.hazeEnd, 50.0f, 0.0f, 100000.0f);
	ImGui::SliderFloat("Haze Max", &p.hazeMax, 0.0f, 1.0f);
	ImGui::DragFloat("Fade Start [m]", &p.fadeStart, 50.0f, 0.0f, 100000.0f);
	ImGui::DragFloat("Fade End [m]", &p.fadeEnd, 50.0f, 0.0f, 100000.0f);
	ImGui::SeparatorText("Shaft (descent hole)");
	ImGui::Text("points %d", static_cast<int>(shaftPoints_.size()));
	ImGui::DragFloat("Shaft Radius [m]", &p.shaftRadius, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Wall Depth [m]", &p.shaftWallDepth, 1.0f, 1.0f, 1000.0f);
	ImGui::DragFloat("Cap Depth [m]", &p.capDepth, 1.0f, 1.0f, 1000.0f);
	ImGui::SliderFloat("Hole Noise Scale", &p.shaftNoiseScale, 0.05f, 1.0f);
	ImGui::DragFloat("Wall Shade Blend [m]", &p.shaftWallBlend, 0.5f, 0.1f, 500.0f);
	ImGui::SliderFloat("Shaft Darkness", &p.shaftDarkness, 0.0f, 1.0f);
	ImGui::SliderFloat("Underside Bright", &p.undersideBright, 0.0f, 1.0f);
	ImGui::DragFloat("Cap Top [m]", &p.capTop, 1.0f, 0.0f, 500.0f);
	ImGui::DragFloat("Cap Bottom [m]", &p.capBottom, 1.0f, 0.0f, 500.0f);
	ImGui::DragFloat("Floor Depth [m]", &p.floorDepth, 1.0f, 1.0f, 500.0f);
	ImGui::SeparatorText("Volume (B', entry / exit)");
	ImGui::DragFloat("Zone Inner [m]", &p.volumeZoneInner, 5.0f, 0.0f, 5000.0f);
	ImGui::DragFloat("Zone Outer [m]", &p.volumeZoneOuter, 5.0f, 0.0f, 5000.0f);
	ImGui::SliderInt("Volume Steps", &p.volumeSteps, 4, 128);
	ImGui::DragFloat("Volume Range [m]", &p.volumeRange, 5.0f, 1.0f, 5000.0f);
	ImGui::DragFloat("Volume Shaft Blend [m]", &p.volumeShaftBlend, 1.0f, 1.0f, 500.0f);
	ImGui::SliderFloat("Volume Shaft Weight", &p.volumeShaftWeight, 0.0f, 1.0f);
	ImGui::DragFloat("Exit Zone Inner [m]", &p.exitZoneInner, 1.0f, 0.0f, 2000.0f);
	ImGui::DragFloat("Exit Zone Outer [m]", &p.exitZoneOuter, 1.0f, 0.0f, 2000.0f);
	ImGui::DragFloat("Extinction [1/m]", &p.extinction, 0.001f, 0.0f, 1.0f, "%.4f");
	ImGui::SliderFloat("Erosion (gap)", &p.erosion, 0.0f, 1.0f);
	ImGui::DragFloat("Erosion Sharp", &p.erosionSharp, 0.05f, 0.01f, 20.0f);
	ImGui::DragFloat("Light Extinction x", &p.lightExtinction, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Erosion Scale [m]", &p.erosionScale, 0.5f, 0.1f, 1000.0f);
	ImGui::DragFloat3("Erosion Wind [m/s]", &p.erosionWind.x, 0.1f, -100.0f, 100.0f);
	ImGui::DragFloat("Powder", &p.powder, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Ambient Strength", &p.ambientStrength, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Volume Light Step [m]", &p.volumeLightStep, 0.5f, 0.1f, 500.0f);
#endif
}
