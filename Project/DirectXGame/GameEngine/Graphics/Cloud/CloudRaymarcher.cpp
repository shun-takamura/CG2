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
#include "AssetLocator.h"
#include "DirectXTex.h"
#include "d3dx12.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 形/マスクともこもこは 16 ビット無圧縮（gen_cloud_noise.py --unorm16 --recipe large_soft_smooth）。
	// BC5/BC4（8 ビット相当）だと、谷でノイズの 1 段が数 m の段差に拡大されて等高線の縞になった
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise16.dds";
	// もこもこはドームの縁で傾き 0 の版（gen_cloud_noise.py --moko --recipe large_soft_smooth）。タイトルの遠景の雲の半球版だと谷で縞が出た
	constexpr const char* kMokoPath = "Resources/Textures/Cloud/cloud_moko_soft16.dds";
	// 傾き（gen_cloud_noise.py --grad、R16G16_FLOAT）。上面の法線に使う
	constexpr const char* kShapeGradPath = "Resources/Textures/Cloud/cloud_noise_grad.dds";
	constexpr const char* kMokoGradPath = "Resources/Textures/Cloud/cloud_moko_soft_grad.dds";
	// 雲海のボリュームの 3D ノイズ（gen_cloud_noise3d.py）。TextureManager は 2D 専用なので LoadVolumeTexture で読む。
	// 中身は DDS だが拡張子は .vol（.dds だと pack_assets.py が 2D 用の配置変換と GDeflate 圧縮をかけ、CPU で読めなくなる）
	constexpr const char* kShape3DPath = "Resources/Textures/Cloud/cloud_shape3d.vol";
	constexpr const char* kDetail3DPath = "Resources/Textures/Cloud/cloud_detail3d.vol";
	constexpr uint32_t kNoiseSize = 512;                  // cloud_noise.dds の一辺（ミップ選択に使う）
	constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_R32_FLOAT;      // 雲の代表の距離 [m]
	// 時間方向の積み重ねを捨てる、1 フレームのカメラの移動量 [m]（ワープ・シーンの切り替え）
	constexpr float kHistoryResetDistance = 60.0f;
	constexpr DXGI_FORMAT kSceneFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// ルートパラメータ番号
	enum RootParam : UINT {
		kRootConstants = 0, // PS b0
		kRootTexture0,      // PS t0 深度（雲を描く）/ 雲の RT（重ねる）
		kRootNoise,         // PS t1 雲のノイズ
		kRootSkyCube,       // PS t2 空の cubemap（空気遠近）
		kRootMoko,          // PS t3 もこもこの高さ
		kRootShapeGrad,     // PS t4 形の傾き
		kRootMokoGrad,      // PS t5 もこもこの傾き
		kRootShape3D,       // PS t6 3D の形（雲海のボリューム）
		kRootDetail3D,      // PS t7 3D の細部
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

	// 無圧縮のデータなので LoadTextureLinear（LoadTexture はミップを sRGB として作り直して値を壊す）
	TextureManager::GetInstance()->LoadTextureLinear(kNoisePath);
	TextureManager::GetInstance()->LoadTextureLinear(kMokoPath);
	// 傾きは負の値を含むデータ。LoadTexture は無圧縮の DDS のミップを sRGB として作り直すので値が壊れ、陰影が平らになった
	TextureManager::GetInstance()->LoadTextureLinear(kShapeGradPath);
	TextureManager::GetInstance()->LoadTextureLinear(kMokoGradPath);
	LoadVolumeTexture(kShape3DPath, shape3D_, shape3DSrv_);
	LoadVolumeTexture(kDetail3DPath, detail3D_, detail3DSrv_);

	CreateRootSignature();
	// 1 枚目 = 乗算済みの色と被覆率、2 枚目 = 雲の代表の距離
	marchPipeline_ = CreatePipelineState(L"Resources/Shaders/Cloud/CloudRaymarch.PS.hlsl", kTargetFormat, false, kDepthFormat);
	temporalPipeline_ = CreatePipelineState(L"Resources/Shaders/Cloud/CloudTemporal.PS.hlsl", kTargetFormat, false);
	compositePipeline_ = CreatePipelineState(L"Resources/Shaders/Cloud/CloudComposite.PS.hlsl", kSceneFormat, true);
	CreateTarget();

	constantsResource_ = dxCore_->CreateBufferResource((sizeof(ConstantsForGPU) + 255) & ~255);
	constantsResource_->Map(0, nullptr, reinterpret_cast<void**>(&constantsData_));
	*constantsData_ = ConstantsForGPU{};
}

void CloudRaymarcher::LoadVolumeTexture(const char* path, Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint32_t& srvIndex)
{
	const std::vector<uint8_t> bytes = AssetLocator::GetInstance()->LoadAll(path);
	DirectX::ScratchImage image;
	if (bytes.empty() || FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image))) {
		Log(std::string("CloudRaymarcher: 3D テクスチャを読めない: ") + path + "\n");
		assert(false);
		return;
	}
	const DirectX::TexMetadata& meta = image.GetMetadata();
	assert(meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D);

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	desc.Width = static_cast<UINT64>(meta.width);
	desc.Height = static_cast<UINT>(meta.height);
	desc.DepthOrArraySize = static_cast<UINT16>(meta.depth);
	desc.MipLevels = static_cast<UINT16>(meta.mipLevels);
	desc.Format = meta.format;
	desc.SampleDesc.Count = 1;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	ID3D12Device* device = dxCore_->GetDevice();
	HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	resource->SetName(L"CloudRaymarcher 3D noise");

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, image.GetImages(), image.GetImageCount(), meta, subresources);
	const uint64_t uploadSize = GetRequiredIntermediateSize(resource.Get(), 0, static_cast<UINT>(subresources.size()));
	Microsoft::WRL::ComPtr<ID3D12Resource> upload = dxCore_->CreateBufferResource(uploadSize);
	ID3D12GraphicsCommandList* cmd = dxCore_->GetCommandList();
	UpdateSubresources(cmd, resource.Get(), upload.Get(), 0, 0, static_cast<UINT>(subresources.size()), subresources.data());
	dxCore_->TrackIntermediateResource(upload);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	cmd->ResourceBarrier(1, &barrier);

	srvIndex = srvManager_->Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = meta.format;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	srv.Texture3D.MipLevels = static_cast<UINT>(meta.mipLevels);
	device->CreateShaderResourceView(resource.Get(), &srv, srvManager_->GetCPUDescriptorHandle(srvIndex));
}

void CloudRaymarcher::CreateRootSignature()
{
	constexpr UINT kTextureCount = kRootCount - kRootTexture0;
	D3D12_DESCRIPTOR_RANGE ranges[kTextureCount] = {};
	for (UINT i = 0; i < kTextureCount; ++i) {
		ranges[i].BaseShaderRegister = i;
		ranges[i].NumDescriptors = 1;
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_PARAMETER params[kRootCount] = {};
	params[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootConstants].Descriptor.ShaderRegister = 0;
	for (UINT i = 0; i < kTextureCount; ++i) {
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
	// s0 は異方性 16（雲海の上面を SampleGrad で読む時に、浅い角度の奥行き方向だけをぼかす）。SampleLevel でも使える
	samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
	samplers[0].MaxAnisotropy = 16;
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
	const wchar_t* psPath, DXGI_FORMAT rtvFormat, bool premultipliedBlend, DXGI_FORMAT rtvFormat2)
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
	blend.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
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
	desc.NumRenderTargets = (rtvFormat2 == DXGI_FORMAT_UNKNOWN) ? 1 : 2;
	desc.RTVFormats[0] = rtvFormat;
	desc.RTVFormats[1] = rtvFormat2;
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
	if (depthTarget_) depthTarget_->Finalize();
	depthTarget_ = std::make_unique<RenderTexture>();
	depthTarget_->Initialize(dxCore_, srvManager_, width, height, kDepthFormat, clear);
	for (auto& h : history_) {
		if (h) h->Finalize();
		h = std::make_unique<RenderTexture>();
		h->Initialize(dxCore_, srvManager_, width, height, kTargetFormat, clear);
	}
	historyValid_ = false;
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
	// 足す風（SetExtraWind）は大きい形・もこもこ・マスクを同じだけ流す＝雲の場全体が形を保ったまま動く
	const Vector2 shapeWind = { params_.shapeWind.x + extraWind_.x, params_.shapeWind.y + extraWind_.y };
	const Vector2 maskWind = { params_.maskWind.x + extraWind_.x, params_.maskWind.y + extraWind_.y };
	scroll(shapeOffset_, shapeWind, (std::max)(params_.shapeScale, 1.0f));
	// もこもこは大きい形と同じ風で流す（こぶだけが雲の上を滑って見えないように）
	scroll(mokoOffset_, shapeWind, (std::max)(params_.mokoScale, 1.0f));
	scroll(maskOffset_, maskWind, (std::max)(params_.maskScale, 1.0f));
	// 3D ノイズは m 単位でずらす（周期がそろわないので UV で折り返さない。1e5m で戻す＝6m/s で 4 時間以上）
	volOffset_.x -= shapeWind.x * deltaTime;
	volOffset_.z -= shapeWind.y * deltaTime;
	volOffset_.y -= 0.4f * deltaTime;
	for (float* v : { &volOffset_.x, &volOffset_.y, &volOffset_.z }) {
		if (std::fabs(*v) > 1.0e5f) *v = 0.0f;
	}
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
	c.mokoLodBias = (std::max)(p.mokoLodBias, 0.0f);
	c.grazingMin = std::clamp(p.grazingMin, 0.001f, 1.0f);
	c.grazingPower = std::clamp(p.grazingPower, 0.5f, 4.0f);
	c.detailFadeStart = (std::max)(p.detailFadeStart, 0.0f);
	c.detailFadeEnd = (std::max)(p.detailFadeEnd, c.detailFadeStart + 1.0f);
	c.detailFadeLod = (std::max)(p.detailFadeLod, 0.0f);
	c.anisoEnabled = p.anisoEnabled ? 1 : 0;
	c.slopeClampNoise = std::clamp(p.slopeClampNoise, 0.01f, 1.0f);
	c.shapeGradLodBias = (std::max)(p.shapeGradLodBias, 0.0f);
	c.shapeLodBias = (std::max)(p.shapeLodBias, 0.0f);
	c.mokoInHeight = std::clamp(p.mokoInHeight, 0.0f, 1.0f);
	c.debugView = debugView_;
	c.prevViewProj = prevViewProj_;
	c.frameIndex = static_cast<int>(frameIndex_);
	c.historyValid = historyValid_ ? 1 : 0;
	c.temporalBlend = std::clamp(p.temporalBlend, 0.01f, 1.0f);
	c.volumeSea = p.volumeSea ? 1 : 0;
	c.volShapeTiling = 1.0f / (std::max)(p.volShapeScale, 1.0f);
	c.volDetailTiling = 1.0f / (std::max)(p.volDetailScale, 1.0f);
	c.volFluff = (std::max)(p.volFluff, 1.0f);
	c.volErosion = (std::max)(p.volErosion, 0.0f);
	c.volExtinction = (std::max)(p.volExtinction, 0.0f);
	c.volSteps = std::clamp(p.volSteps, 4, 256);
	c.volStepMin = (std::max)(p.volStepMin, 0.1f);
	c.volStepGrowth = (std::max)(p.volStepGrowth, 0.0f);
	c.volLightSigma = (std::max)(p.volLightSigma, 0.0f);
	c.volSunScale = (std::max)(p.volSunScale, 0.0f);
	c.volAmbientScale = (std::max)(p.volAmbientScale, 0.0f);
	c.volPhaseG = std::clamp(p.volPhaseG, 0.0f, 0.95f);
	c.volPowder = std::clamp(p.volPowder, 0.0f, 1.0f);
	c.volSkyAmbient = p.volSkyAmbient;
	c.volMacroShadow = (std::max)(p.volMacroShadow, 0.0f);
	c.volWind = volOffset_;
	c.volDensityGain = (std::max)(p.volDensityGain, 0.1f);
	c.volMultiScatter = std::clamp(p.volMultiScatter, 0.0f, 1.0f);
	c.volVerticalStretch = (std::max)(p.volVerticalStretch, 0.1f);
	c.occlusionSoft = (std::max)(p.occlusionSoft, 0.1f);

	c.hvEnabled = p.hvEnabled ? 1 : 0;
	// ハイトマップの UV のずれ（shapeOffset_ = −風/大きさ×時間）と同じだけ、3D ノイズも流す [m]
	c.hvWind = { p.shapeWind.x * time_, p.shapeWind.y * time_ };
	c.hvShapeTiling = 1.0f / (std::max)(p.hvShapeScale, 1.0f);
	c.hvDetailTiling = 1.0f / (std::max)(p.hvDetailScale, 1.0f);
	{
		const float dw = std::clamp(p.hvDetailWeight, 0.0f, 1.0f);
		c.hvDetailWeight = dw;
		c.hvDetailContrast = 1.0f / std::sqrt((1.0f - dw) * (1.0f - dw) + dw * dw);
	}
	c.hvRelief = (std::max)(p.hvRelief, 0.0f);
	c.hvFloorLow = (std::min)(p.coverage - p.hvFloorSpread, 0.99f);
	c.hvBumpPower = (std::max)(p.hvBumpPower, 0.01f);
	c.hvFluff = (std::max)(p.hvFluff, 0.1f);
	c.hvMarchDepth = (std::max)(p.hvMarchDepth, 0.1f);
	c.hvMaxMarch = (std::max)(p.hvMaxMarch, 1.0f);
	c.hvSteps = std::clamp(p.hvSteps, 1, 128);
	c.hvSegments = std::clamp(p.hvSegments, 1, 4);
	c.hvExtinction = (std::max)(p.hvExtinction, 0.0f);
	c.hvErosion = p.hvErosion;
	c.hvErosionTiling = 1.0f / (std::max)(p.hvErosionScale, 0.1f);
	c.hvWarp = (std::max)(p.hvWarp, 0.0f);
	c.hvWarpTiling = 1.0f / (std::max)(p.hvWarpScale, 0.1f);
	c.hvWarpRamp = (std::max)(p.hvWarpRamp, 0.1f);
	c.hvLightSteps[0] = p.hvLightSteps.x;
	c.hvLightSteps[1] = p.hvLightSteps.y;
	c.hvLightSteps[2] = p.hvLightSteps.z;
	c.hvLightSteps[3] = p.hvLightSteps.w;
	c.hvLightSigma = (std::max)(p.hvLightSigma, 0.0f);
	c.hvPowderMix = std::clamp(p.hvPowderMix, 0.0f, 1.0f);
	c.hvAoBias = (std::max)(p.hvAoBias, 0.0f);
	c.hvAoRange = (std::max)(p.hvAoRange, 0.1f);
	c.hvAoDepth = (std::max)(p.hvAoDepth, 0.1f);
	c.hvLitColor = p.hvLitColor;
	c.hvShadowColor = p.hvShadowColor;
	c.hvSkyAmbient = p.hvSkyAmbient;
	c.hvSunScale = (std::max)(p.hvSunScale, 0.0f);
	c.hvAmbientScale = (std::max)(p.hvAmbientScale, 0.0f);
	c.normalEps = (std::max)(p.normalEps, 0.1f);
	c.maskStrength = p.maskStrength;
	c.coverage = p.coverage;
	c.sharpness = p.sharpness;
	// RT の 1 ピクセルが見込む角度 × ノイズの解像度。距離とタイリングを掛けると「1 ピクセルに入るテクセル数」になる
	const float pixelAngle = 2.0f * std::tan(camera.GetFovY() * 0.5f) / (std::max)(1.0f, static_cast<float>(target_->GetHeight()));
	c.lodScale = pixelAngle * static_cast<float>(kNoiseSize);
	c.pixelAngle = pixelAngle;
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
	// カメラが飛んだら（ワープ・シーンの切り替え）積み重ねを捨てる
	if (Distance(camera.GetTranslate(), prevEye_) > kHistoryResetDistance) historyValid_ = false;
	if (!params_.temporal) historyValid_ = false;
	WriteConstants(camera);

	// 不透明物の深度を読む（この間は DSV を貼れない）
	dxCore_->TransitionDepthState(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	srvManager_->PreDraw();

	{
		PEPPER_GPU_SCOPE(cmd, "CloudRaymarcher::March");
		depthTarget_->BeginRender(cmd);
		target_->BeginRender(cmd);
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { target_->GetRTVHandle(), depthTarget_->GetRTVHandle() };
		cmd->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
		cmd->SetGraphicsRootSignature(rootSignature_.Get());
		cmd->SetPipelineState(marchPipeline_.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->SetGraphicsRootConstantBufferView(kRootConstants, constantsResource_->GetGPUVirtualAddress());
		srvManager_->SetGraphicsRootDescriptorTable(kRootTexture0, dxCore_->GetDepthSRVIndex());
		cmd->SetGraphicsRootDescriptorTable(kRootNoise, TextureManager::GetInstance()->GetSrvHandleGPU(kNoisePath));
		cmd->SetGraphicsRootDescriptorTable(kRootSkyCube, TextureManager::GetInstance()->GetSrvHandleGPU(skyCubemapPath));
		cmd->SetGraphicsRootDescriptorTable(kRootMoko, TextureManager::GetInstance()->GetSrvHandleGPU(kMokoPath));
		cmd->SetGraphicsRootDescriptorTable(kRootShapeGrad, TextureManager::GetInstance()->GetSrvHandleGPU(kShapeGradPath));
		cmd->SetGraphicsRootDescriptorTable(kRootMokoGrad, TextureManager::GetInstance()->GetSrvHandleGPU(kMokoGradPath));
		srvManager_->SetGraphicsRootDescriptorTable(kRootShape3D, shape3DSrv_);
		srvManager_->SetGraphicsRootDescriptorTable(kRootDetail3D, detail3DSrv_);
		cmd->DrawInstanced(3, 1, 0, 0);
		target_->EndRender(cmd);
		depthTarget_->EndRender(cmd);
	}

	// 時間方向の積み重ね：前のフレームの結果を雲の距離で今の画面へ写し、今のフレームと混ぜる
	RenderTexture* result = target_.get();
	if (params_.temporal) {
		PEPPER_GPU_SCOPE(cmd, "CloudRaymarcher::Temporal");
		RenderTexture* prev = history_[historyIndex_].get();
		RenderTexture* next = history_[historyIndex_ ^ 1].get();
		if (!historyValid_) {
			// 作ったばかりの RT は描画先の状態なので、一度クリアして読める状態にしておく（中身は historyValid = 0 で使わない）
			prev->BeginRender(cmd);
			prev->EndRender(cmd);
		}
		next->BeginRender(cmd);
		cmd->SetGraphicsRootSignature(rootSignature_.Get());
		cmd->SetPipelineState(temporalPipeline_.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->SetGraphicsRootConstantBufferView(kRootConstants, constantsResource_->GetGPUVirtualAddress());
		srvManager_->SetGraphicsRootDescriptorTable(kRootTexture0, target_->GetSRVIndex());
		srvManager_->SetGraphicsRootDescriptorTable(kRootNoise, depthTarget_->GetSRVIndex());
		srvManager_->SetGraphicsRootDescriptorTable(kRootSkyCube, prev->GetSRVIndex());
		cmd->DrawInstanced(3, 1, 0, 0);
		next->EndRender(cmd);
		historyIndex_ ^= 1;
		historyValid_ = true;
		result = next;
	}
	prevViewProj_ = camera.GetViewProjectionMatrix();
	prevEye_ = camera.GetTranslate();
	++frameIndex_;

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
		srvManager_->SetGraphicsRootDescriptorTable(kRootTexture0, result->GetSRVIndex());
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
	f("mokoLodBias", p.mokoLodBias); f("grazingMin", p.grazingMin); f("grazingPower", p.grazingPower);
	f("detailFadeStart", p.detailFadeStart); f("detailFadeEnd", p.detailFadeEnd); f("detailFadeLod", p.detailFadeLod);
	p.anisoEnabled = j["anisoEnabled"].AsBool(p.anisoEnabled); f("occlusionSoft", p.occlusionSoft); f("slopeClampNoise", p.slopeClampNoise); f("shapeGradLodBias", p.shapeGradLodBias);
	f("shapeLodBias", p.shapeLodBias); f("mokoInHeight", p.mokoInHeight);
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
	const JsonValue& vs = j["volumeSea"];
	if (vs.IsObject()) {
		const auto g = [&vs](const char* key, float& v) { v = static_cast<float>(vs[key].AsDouble(v)); };
		p.volumeSea = vs["enabled"].AsBool(p.volumeSea);
		p.temporal = vs["temporal"].AsBool(p.temporal);
		g("temporalBlend", p.temporalBlend);
		g("shapeScale", p.volShapeScale); g("detailScale", p.volDetailScale); g("fluff", p.volFluff);
		g("erosion", p.volErosion); g("extinction", p.volExtinction);
		p.volSteps = static_cast<int>(vs["steps"].AsInt(p.volSteps));
		g("stepMin", p.volStepMin); g("stepGrowth", p.volStepGrowth);
		g("lightSigma", p.volLightSigma); g("sunScale", p.volSunScale); g("ambientScale", p.volAmbientScale);
		g("phaseG", p.volPhaseG); g("powder", p.volPowder); g("macroShadow", p.volMacroShadow);
		g("densityGain", p.volDensityGain); g("multiScatter", p.volMultiScatter); g("verticalStretch", p.volVerticalStretch);
		p.volSkyAmbient = ReadVec3(vs["skyAmbient"], p.volSkyAmbient);
	}
	const JsonValue& hv = j["heightVolume"];
	if (hv.IsObject()) {
		const auto g = [&hv](const char* key, float& v) { v = static_cast<float>(hv[key].AsDouble(v)); };
		const auto gi = [&hv](const char* key, int& v) { v = static_cast<int>(hv[key].AsInt(v)); };
		p.hvEnabled = hv["enabled"].AsBool(p.hvEnabled);
		g("shapeScale", p.hvShapeScale); g("detailScale", p.hvDetailScale); g("detailWeight", p.hvDetailWeight);
		g("relief", p.hvRelief); g("floorSpread", p.hvFloorSpread); g("bumpPower", p.hvBumpPower);
		g("fluff", p.hvFluff); g("marchDepth", p.hvMarchDepth); g("maxMarch", p.hvMaxMarch);
		gi("steps", p.hvSteps); gi("segments", p.hvSegments);
		g("extinction", p.hvExtinction); g("erosion", p.hvErosion); g("erosionScale", p.hvErosionScale);
		g("warp", p.hvWarp); g("warpScale", p.hvWarpScale); g("warpRamp", p.hvWarpRamp);
		const JsonValue& ls = hv["lightSteps"];
		if (ls.IsArray() && ls.Size() >= 4) {
			p.hvLightSteps = { static_cast<float>(ls[0].AsDouble(p.hvLightSteps.x)), static_cast<float>(ls[1].AsDouble(p.hvLightSteps.y)),
				static_cast<float>(ls[2].AsDouble(p.hvLightSteps.z)), static_cast<float>(ls[3].AsDouble(p.hvLightSteps.w)) };
		}
		g("lightSigma", p.hvLightSigma); g("powderMix", p.hvPowderMix);
		g("aoBias", p.hvAoBias); g("aoRange", p.hvAoRange); g("aoDepth", p.hvAoDepth);
		p.hvLitColor = ReadVec3(hv["litColor"], p.hvLitColor);
		p.hvShadowColor = ReadVec3(hv["shadowColor"], p.hvShadowColor);
		p.hvSkyAmbient = ReadVec3(hv["skyAmbient"], p.hvSkyAmbient);
		g("sunScale", p.hvSunScale); g("ambientScale", p.hvAmbientScale);
	}
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
	f("mokoLodBias", p.mokoLodBias); f("grazingMin", p.grazingMin); f("grazingPower", p.grazingPower);
	f("detailFadeStart", p.detailFadeStart); f("detailFadeEnd", p.detailFadeEnd); f("detailFadeLod", p.detailFadeLod);
	j["anisoEnabled"] = JsonValue(p.anisoEnabled); f("occlusionSoft", p.occlusionSoft); f("slopeClampNoise", p.slopeClampNoise); f("shapeGradLodBias", p.shapeGradLodBias);
	f("shapeLodBias", p.shapeLodBias); f("mokoInHeight", p.mokoInHeight);
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

	JsonValue vs = JsonValue::MakeObject();
	const auto g = [&vs](const char* key, float v) { vs[key] = JsonValue(static_cast<double>(v)); };
	vs["enabled"] = JsonValue(p.volumeSea);
	vs["temporal"] = JsonValue(p.temporal);
	g("temporalBlend", p.temporalBlend);
	g("shapeScale", p.volShapeScale); g("detailScale", p.volDetailScale); g("fluff", p.volFluff);
	g("erosion", p.volErosion); g("extinction", p.volExtinction);
	vs["steps"] = JsonValue(p.volSteps);
	g("stepMin", p.volStepMin); g("stepGrowth", p.volStepGrowth);
	g("lightSigma", p.volLightSigma); g("sunScale", p.volSunScale); g("ambientScale", p.volAmbientScale);
	g("phaseG", p.volPhaseG); g("powder", p.volPowder); g("macroShadow", p.volMacroShadow);
	g("densityGain", p.volDensityGain); g("multiScatter", p.volMultiScatter); g("verticalStretch", p.volVerticalStretch);
	vs["skyAmbient"] = WriteVec3(p.volSkyAmbient);
	j["volumeSea"] = std::move(vs);

	JsonValue hv = JsonValue::MakeObject();
	const auto h = [&hv](const char* key, float v) { hv[key] = JsonValue(static_cast<double>(v)); };
	const auto hi = [&hv](const char* key, int v) { hv[key] = JsonValue(v); };
	hv["enabled"] = JsonValue(p.hvEnabled);
	h("shapeScale", p.hvShapeScale); h("detailScale", p.hvDetailScale); h("detailWeight", p.hvDetailWeight);
	h("relief", p.hvRelief); h("floorSpread", p.hvFloorSpread); h("bumpPower", p.hvBumpPower);
	h("fluff", p.hvFluff); h("marchDepth", p.hvMarchDepth); h("maxMarch", p.hvMaxMarch);
	hi("steps", p.hvSteps); hi("segments", p.hvSegments);
	h("extinction", p.hvExtinction); h("erosion", p.hvErosion); h("erosionScale", p.hvErosionScale);
	h("warp", p.hvWarp); h("warpScale", p.hvWarpScale); h("warpRamp", p.hvWarpRamp);
	JsonValue ls = JsonValue::MakeArray();
	ls.Push(JsonValue(static_cast<double>(p.hvLightSteps.x)));
	ls.Push(JsonValue(static_cast<double>(p.hvLightSteps.y)));
	ls.Push(JsonValue(static_cast<double>(p.hvLightSteps.z)));
	ls.Push(JsonValue(static_cast<double>(p.hvLightSteps.w)));
	hv["lightSteps"] = std::move(ls);
	h("lightSigma", p.hvLightSigma); h("powderMix", p.hvPowderMix);
	h("aoBias", p.hvAoBias); h("aoRange", p.hvAoRange); h("aoDepth", p.hvAoDepth);
	hv["litColor"] = WriteVec3(p.hvLitColor);
	hv["shadowColor"] = WriteVec3(p.hvShadowColor);
	hv["skyAmbient"] = WriteVec3(p.hvSkyAmbient);
	h("sunScale", p.hvSunScale); h("ambientScale", p.hvAmbientScale);
	j["heightVolume"] = std::move(hv);
}

void CloudRaymarcher::OnImGuiDebugView()
{
#ifdef _DEBUG
	static const char* kItems[] = {
		"0: 通常", "1: 法線", "2: 法線の当たり", "3: 太陽側の遮り", "4: 谷の暗さ",
		"5: 当たった距離 (25m 縞)", "6: 当たった高さ (5m 縞)", "7: 陰影の合計",
		"8: [ボリューム] 太陽側の透過率", "9: [ボリューム] 空の光だけ", "10: [ボリューム] 代表の距離 (40m 縞)" };
	ImGui::Combo("Debug View", &debugView_, kItems, IM_ARRAYSIZE(kItems));
	ImGui::TextDisabled("雲海（上から見下ろした時）だけ。1〜7 は従来の面（Volume Sea オフ）、8〜10 は雲海のボリューム用");
#endif
}

void CloudRaymarcher::OnImGui()
{
#ifdef _DEBUG
	Params& p = params_;
	ImGui::Checkbox("Enabled", &p.enabled);
	ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f);
	if (ImGui::TreeNodeEx("Volume Sea (sea of clouds from above)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Volume Sea", &p.volumeSea);
		ImGui::SameLine();
		ImGui::Checkbox("Temporal", &p.temporal);
		ImGui::SliderFloat("Temporal Blend", &p.temporalBlend, 0.02f, 1.0f);
		ImGui::DragFloat("3D Shape Scale [m]", &p.volShapeScale, 5.0f, 10.0f, 20000.0f);
		ImGui::DragFloat("3D Detail Scale [m]", &p.volDetailScale, 1.0f, 1.0f, 5000.0f);
		ImGui::DragFloat("Fluff [m]", &p.volFluff, 0.5f, 1.0f, 500.0f);
		ImGui::SliderFloat("Erosion", &p.volErosion, 0.0f, 1.0f);
		ImGui::DragFloat("Extinction [1/m]", &p.volExtinction, 0.001f, 0.0f, 2.0f, "%.4f");
		ImGui::DragFloat("Density Gain", &p.volDensityGain, 0.05f, 0.1f, 10.0f);
		ImGui::DragFloat("Vertical Stretch", &p.volVerticalStretch, 0.01f, 0.1f, 5.0f);
		ImGui::SliderInt("Vol Steps", &p.volSteps, 4, 256);
		ImGui::DragFloat("Step Min [m]", &p.volStepMin, 0.1f, 0.1f, 100.0f);
		ImGui::DragFloat("Step Growth", &p.volStepGrowth, 0.0005f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Light Sigma", &p.volLightSigma, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Sun Scale", &p.volSunScale, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Ambient Scale", &p.volAmbientScale, 0.01f, 0.0f, 10.0f);
		ImGui::SliderFloat("Phase G", &p.volPhaseG, 0.0f, 0.95f);
		ImGui::SliderFloat("Powder##vol", &p.volPowder, 0.0f, 1.0f);
		ImGui::DragFloat("Macro Shadow", &p.volMacroShadow, 0.01f, 0.0f, 10.0f);
		ImGui::SliderFloat("Multi Scatter", &p.volMultiScatter, 0.0f, 1.0f);
		ImGui::ColorEdit3("Sky Ambient", &p.volSkyAmbient.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Height Volume (sea of clouds from above)")) {
		ImGui::Checkbox("HV Enabled", &p.hvEnabled);
		ImGui::SeparatorText("Shape");
		ImGui::DragFloat("HV Shape Scale [m]", &p.hvShapeScale, 10.0f, 10.0f, 50000.0f);
		ImGui::DragFloat("HV Detail Scale [m]", &p.hvDetailScale, 5.0f, 10.0f, 50000.0f);
		ImGui::SliderFloat("HV Detail Weight", &p.hvDetailWeight, 0.0f, 1.0f);
		ImGui::DragFloat("HV Relief [m]", &p.hvRelief, 1.0f, 0.0f, 500.0f);
		ImGui::DragFloat("HV Floor Spread", &p.hvFloorSpread, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("HV Bump Power", &p.hvBumpPower, 0.01f, 0.1f, 4.0f);
		ImGui::DragFloat("HV Warp [m]", &p.hvWarp, 0.5f, 0.0f, 200.0f);
		ImGui::DragFloat("HV Warp Scale [m]", &p.hvWarpScale, 1.0f, 1.0f, 1000.0f);
		ImGui::DragFloat("HV Warp Ramp [m]", &p.hvWarpRamp, 0.5f, 0.1f, 200.0f);
		ImGui::SeparatorText("Fluff layer");
		ImGui::DragFloat("HV Fluff [m]", &p.hvFluff, 0.5f, 0.1f, 200.0f);
		ImGui::DragFloat("HV March Depth", &p.hvMarchDepth, 0.05f, 0.1f, 10.0f);
		ImGui::DragFloat("HV Max March [m]", &p.hvMaxMarch, 5.0f, 1.0f, 5000.0f);
		ImGui::SliderInt("HV Steps", &p.hvSteps, 1, 96);
		ImGui::SliderInt("HV Segments", &p.hvSegments, 1, 4);
		ImGui::DragFloat("HV Extinction", &p.hvExtinction, 0.005f, 0.0f, 2.0f);
		ImGui::DragFloat("HV Erosion", &p.hvErosion, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("HV Erosion Scale [m]", &p.hvErosionScale, 1.0f, 0.1f, 1000.0f);
		ImGui::SeparatorText("Light");
		ImGui::DragFloat4("HV Light Steps [m]", &p.hvLightSteps.x, 0.5f, 0.0f, 1000.0f);
		ImGui::DragFloat("HV Light Sigma", &p.hvLightSigma, 0.01f, 0.0f, 10.0f);
		ImGui::SliderFloat("HV Powder Mix", &p.hvPowderMix, 0.0f, 1.0f);
		ImGui::DragFloat("HV Sun Scale", &p.hvSunScale, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("HV Ambient Scale", &p.hvAmbientScale, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("HV AO Bias", &p.hvAoBias, 0.1f, 0.0f, 8.0f);
		ImGui::DragFloat("HV AO Range [m]", &p.hvAoRange, 1.0f, 0.1f, 1000.0f);
		ImGui::DragFloat("HV AO Depth [m]", &p.hvAoDepth, 1.0f, 0.1f, 1000.0f);
		ImGui::ColorEdit3("HV Lit Color", &p.hvLitColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("HV Shadow Color", &p.hvShadowColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("HV Sky Ambient", &p.hvSkyAmbient.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::TreePop();
	}
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
	ImGui::SliderFloat("Moko Lod Bias", &p.mokoLodBias, 0.0f, 4.0f);
	ImGui::SliderFloat("Grazing Min", &p.grazingMin, 0.01f, 1.0f);
	ImGui::SliderFloat("Grazing Power", &p.grazingPower, 0.5f, 3.0f);
	ImGui::DragFloat("Detail Fade Start [m]", &p.detailFadeStart, 10.0f, 0.0f, 20000.0f);
	ImGui::DragFloat("Detail Fade End [m]", &p.detailFadeEnd, 10.0f, 0.0f, 20000.0f);
	ImGui::SliderFloat("Detail Fade Lod", &p.detailFadeLod, 0.0f, 4.0f);
	ImGui::Checkbox("Aniso Sampling", &p.anisoEnabled);
	ImGui::DragFloat("Occlusion Soft [m]", &p.occlusionSoft, 0.1f, 0.1f, 100.0f);
	ImGui::SliderFloat("Slope Clamp Noise", &p.slopeClampNoise, 0.3f, 1.0f);
	ImGui::SliderFloat("Shape Lod Bias", &p.shapeLodBias, 0.0f, 5.0f);
	ImGui::SliderFloat("Moko In Height", &p.mokoInHeight, 0.0f, 1.0f);
	ImGui::SliderFloat("Shape Grad Lod Bias", &p.shapeGradLodBias, 0.0f, 4.0f);
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
