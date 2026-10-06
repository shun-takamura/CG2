#include "WaterSurface.h"

#include "WaterReflection.h"
#include "DirectXCore.h"
#include "Object3DManager.h"
#include "TextureManager.h"
#include "LightManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "WindowsApplication.h"
#include "Log.h"
#include "PepperMacros.h"
#include "Cloud/CloudLayer.h"
#include "RippleSimulation.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// ルートパラメータ番号
	enum RootParam : UINT {
		kRootTransform = 0,   // VS b0
		kRootParams,          // PS b0
		kRootDirectionalLight,// PS b1
		kRootReflection,      // PS t0 反射 RT
		kRootSkyCube,         // PS t1 空の cubemap
		kRootFloor,           // PS t4 床テクスチャ
		kRootShadowConstants, // PS b5
		kRootShadowMap,       // PS t3
		kRootFog,             // PS b6
		kRootCloud,           // PS b7 遠景の雲（Skybox と共有の CB）
		kRootCloudNoise,      // PS t5 雲のノイズ
		kRootRippleHeight,    // PS t6 波のシミュレーションの高さマップ（RippleSimulation）
		kRootFloorNormal,     // PS t7 床の法線マップ
		kRootFloorHeight,     // PS t8 床のハイトマップ（視差）
		kRootCount
	};
}

WaterSurface::WaterSurface() = default;
WaterSurface::~WaterSurface() = default;

void WaterSurface::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	const std::string& floorTexturePath)
{
	dxCore_ = dxCore;
	rippleSimulation_ = std::make_unique<RippleSimulation>();
	rippleSimulation_->Initialize(dxCore, srvManager);
	object3DManager_ = object3DManager;
	floorTexturePath_ = floorTexturePath;

	TextureManager::GetInstance()->LoadTexture(floorTexturePath_);
	TextureManager::GetInstance()->LoadTexture(CloudLayer::GetFallbackTexturePath());
	disabledCloudResource_ = CloudLayer::CreateDisabledConstantBuffer(dxCore_);

	CreateRootSignature();
	CreatePipelineState();
	CreateVertexBuffer();

	transformResource_ = dxCore_->CreateBufferResource(sizeof(TransformForGPU));
	transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
	transformData_->WVP = MakeIdentity4x4();
	transformData_->World = MakeIdentity4x4();

	paramsResource_ = dxCore_->CreateBufferResource(sizeof(Params));
	paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
	*paramsData_ = params_;
}

void WaterSurface::CreateRootSignature()
{
	auto makeRange = [](UINT reg) {
		D3D12_DESCRIPTOR_RANGE r{};
		r.BaseShaderRegister = reg;
		r.NumDescriptors = 1;
		r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		return r;
	};
	D3D12_DESCRIPTOR_RANGE rangeReflection = makeRange(0);
	D3D12_DESCRIPTOR_RANGE rangeSky = makeRange(1);
	D3D12_DESCRIPTOR_RANGE rangeShadow = makeRange(3);
	D3D12_DESCRIPTOR_RANGE rangeFloor = makeRange(4);
	D3D12_DESCRIPTOR_RANGE rangeCloudNoise = makeRange(5);
	D3D12_DESCRIPTOR_RANGE rangeRippleHeight = makeRange(6);
	D3D12_DESCRIPTOR_RANGE rangeFloorNormal = makeRange(7);
	D3D12_DESCRIPTOR_RANGE rangeFloorHeight = makeRange(8);

	auto setCbv = [](D3D12_ROOT_PARAMETER& p, UINT reg, D3D12_SHADER_VISIBILITY vis) {
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		p.ShaderVisibility = vis;
		p.Descriptor.ShaderRegister = reg;
	};
	auto setTable = [](D3D12_ROOT_PARAMETER& p, D3D12_DESCRIPTOR_RANGE* range) {
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		p.DescriptorTable.pDescriptorRanges = range;
		p.DescriptorTable.NumDescriptorRanges = 1;
	};

	D3D12_ROOT_PARAMETER rootParameters[kRootCount] = {};
	setCbv(rootParameters[kRootTransform], 0, D3D12_SHADER_VISIBILITY_VERTEX);
	setCbv(rootParameters[kRootParams], 0, D3D12_SHADER_VISIBILITY_PIXEL);
	setCbv(rootParameters[kRootDirectionalLight], 1, D3D12_SHADER_VISIBILITY_PIXEL);
	setTable(rootParameters[kRootReflection], &rangeReflection);
	setTable(rootParameters[kRootSkyCube], &rangeSky);
	setTable(rootParameters[kRootFloor], &rangeFloor);
	setCbv(rootParameters[kRootShadowConstants], 5, D3D12_SHADER_VISIBILITY_PIXEL);
	setTable(rootParameters[kRootShadowMap], &rangeShadow);
	setCbv(rootParameters[kRootFog], 6, D3D12_SHADER_VISIBILITY_PIXEL);
	setCbv(rootParameters[kRootCloud], 7, D3D12_SHADER_VISIBILITY_PIXEL);
	setTable(rootParameters[kRootCloudNoise], &rangeCloudNoise);
	setTable(rootParameters[kRootRippleHeight], &rangeRippleHeight);
	setTable(rootParameters[kRootFloorNormal], &rangeFloorNormal);
	setTable(rootParameters[kRootFloorHeight], &rangeFloorHeight);

	// s0 = 通常（ラップ）, s1 = シャドウ比較, s2 = シャドウ生深度, s3 = 反射 RT（クランプ）, s4 = 雲のノイズ（ラップ・異方性）
	D3D12_STATIC_SAMPLER_DESC samplers[5] = {};
	for (UINT i = 0; i < 5; ++i) {
		samplers[i].ShaderRegister = i;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	// s1/s2 は Object3D と同じ設定（Shadow.hlsli を共用するため）
	samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	// Skybox の s4 と同じ設定（CloudSky.hlsli を共用するため）
	samplers[4].Filter = D3D12_FILTER_ANISOTROPIC;
	samplers[4].MaxAnisotropy = 8;
	samplers[4].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[4].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	desc.pParameters = rootParameters;
	desc.NumParameters = _countof(rootParameters);
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

void WaterSurface::CreatePipelineState()
{
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Water/WaterSurface.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Water/WaterSurface.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC inputElements[1] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE; // 板1枚なので両面
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blend.RenderTarget[0].BlendEnable = FALSE; // 床の透過はシェーダ内で合成するので不透明で描く

	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
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
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
}

void WaterSurface::CreateVertexBuffer()
{
	// XZ 平面の単位正方形（TriangleStrip）。大きさと位置は World 行列で決める
	const Vector3 vertices[4] = {
		{ -0.5f, 0.0f, -0.5f },
		{ -0.5f, 0.0f,  0.5f },
		{  0.5f, 0.0f, -0.5f },
		{  0.5f, 0.0f,  0.5f },
	};
	vertexBuffer_ = dxCore_->CreateBufferResource(sizeof(vertices));
	Vector3* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	for (int i = 0; i < 4; ++i) mapped[i] = vertices[i];
	vertexBuffer_->Unmap(0, nullptr);

	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(vertices);
	vertexBufferView_.StrideInBytes = sizeof(Vector3);
}

void WaterSurface::Draw(const Camera& camera, const std::string& skyCubemapPath, const WaterReflection* reflection)
{
	PEPPER_SCOPE("WaterSurface::Draw");
	auto* cmd = dxCore_->GetCommandList();
	PEPPER_GPU_SCOPE(cmd, "WaterSurface::Draw");

	const Vector3& camPos = camera.GetTranslate();
	const Vector3 center = followCamera_ ? Vector3{ camPos.x, 0.0f, camPos.z } : center_;

	Matrix4x4 world = MakeIdentity4x4();
	world.m[0][0] = size_;
	world.m[2][2] = size_;
	world.m[3][0] = center.x;
	world.m[3][1] = params_.waterHeight;
	world.m[3][2] = center.z;
	transformData_->World = world;
	transformData_->WVP = Multiply(world, camera.GetViewProjectionMatrix());

	const bool reflectionReady = reflection && reflection->IsReady();
	params_.cameraPosition = camPos;
	// 見え方は「反射を作った視点」基準。通常は描画カメラと同じで、
	// デバッグ時に別視点の反射を作れば、その視点での見え方が水面に焼き付く
	params_.reflectionViewProj = reflectionReady ? reflection->GetSourceViewProjection() : camera.GetViewProjectionMatrix();
	params_.shadingEye = reflectionReady ? reflection->GetSourceEyePosition() : camPos;
	params_.screenSize = { static_cast<float>(WindowsApplication::kClientWidth),
		static_cast<float>(WindowsApplication::kClientHeight) };
	params_.hasReflection = (reflectionReady && reflection->HasContent()) ? 1 : 0;
	params_.simCenter = rippleSimulation_->GetCenter();
	params_.simSize = rippleSimulation_->GetSettings().simSize;
	params_.simNormalScale = rippleSimulation_->GetSettings().normalScale;
	params_.simTexel = 1.0f / static_cast<float>(RippleSimulation::kResolution);
	*paramsData_ = params_;

	TextureManager* tm = TextureManager::GetInstance();
	const D3D12_GPU_DESCRIPTOR_HANDLE floorSrv = tm->GetSrvHandleGPU(floorTexturePath_);

	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);

	cmd->SetGraphicsRootConstantBufferView(kRootTransform, transformResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootParams, paramsResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootDirectionalLight,
		LightManager::GetInstance()->GetDirectionalLightGpuAddress());
	// 反射 RT が未準備なら Texture2D の床で埋める（シェーダは hasReflection=0 で読まない）
	cmd->SetGraphicsRootDescriptorTable(kRootReflection, reflectionReady ? reflection->GetSrvHandle() : floorSrv);
	cmd->SetGraphicsRootDescriptorTable(kRootSkyCube, tm->GetSrvHandleGPU(skyCubemapPath));
	cmd->SetGraphicsRootDescriptorTable(kRootFloor, floorSrv);
	cmd->SetGraphicsRootConstantBufferView(kRootShadowConstants, object3DManager_->GetShadowConstantsAddress());
	cmd->SetGraphicsRootDescriptorTable(kRootShadowMap, object3DManager_->GetShadowSrvHandle());
	cmd->SetGraphicsRootConstantBufferView(kRootFog, object3DManager_->GetFogAddress());
	// 雲なしでもルートパラメータは埋める（enabled=0 なのでシェーダはテクスチャを読まない）
	cmd->SetGraphicsRootConstantBufferView(kRootCloud,
		cloudLayer_ ? cloudLayer_->GetConstantBufferAddress() : disabledCloudResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootDescriptorTable(kRootCloudNoise,
		cloudLayer_ ? cloudLayer_->GetNoiseSrvHandle() : tm->GetSrvHandleGPU(CloudLayer::GetFallbackTexturePath()));
	cmd->SetGraphicsRootDescriptorTable(kRootRippleHeight, rippleSimulation_->GetHeightSrvHandle());
	// 法線マップ・ハイトマップが無ければ床のテクスチャで埋める（シェーダはフラグで読まない）
	cmd->SetGraphicsRootDescriptorTable(kRootFloorNormal,
		params_.floorUseNormalMap ? tm->GetSrvHandleGPU(floorNormalMapPath_) : floorSrv);
	cmd->SetGraphicsRootDescriptorTable(kRootFloorHeight,
		params_.floorUseParallax ? tm->GetSrvHandleGPU(floorHeightMapPath_) : floorSrv);

	PEPPER_COUNT("DrawCall");
	cmd->DrawInstanced(4, 1, 0, 0);
}

void WaterSurface::Update(float deltaTime)
{
	params_.time += deltaTime;
	// シミュレーションの範囲は波紋の中心まわり（タイトルでは周回中心）
	rippleSimulation_->SetCenter(params_.rippleCenter);
	rippleSimulation_->Update(deltaTime);

	// 待機中の波の倍率を smoothstep で補間
	if (ambientBlendElapsed_ < ambientBlendDuration_) {
		ambientBlendElapsed_ = (std::min)(ambientBlendElapsed_ + deltaTime, ambientBlendDuration_);
		float t = ambientBlendElapsed_ / ambientBlendDuration_;
		t = t * t * (3.0f - 2.0f * t);
		params_.ambientRingScale = ambientFrom_ + (ambientTo_ - ambientFrom_) * t;
	}

	if (bursting_) {
		burstTimer_ -= deltaTime;
		if (burstTimer_ <= 0.0f) {
			EmitRing(burstCenter_,
				burstAmplitude_ * (0.7f + 0.3f * NextRandom01()),
				burstWavelength_ * (0.8f + 0.2f * NextRandom01()));
			burstTimer_ += burstInterval_ * (0.8f + 0.4f * NextRandom01());
		}
	}
}

void WaterSurface::SetAmbientRingScale(float scale, float blendSeconds)
{
	scale = (std::max)(scale, 0.0f);
	if (blendSeconds <= 0.0f) {
		params_.ambientRingScale = scale;
		ambientTo_ = scale;
		ambientBlendElapsed_ = ambientBlendDuration_ = 0.0f;
		return;
	}
	ambientFrom_ = params_.ambientRingScale;
	ambientTo_ = scale;
	ambientBlendElapsed_ = 0.0f;
	ambientBlendDuration_ = blendSeconds;
}

void WaterSurface::EmitRing(const Vector3& center, float amplitude, float wavelength,
	float packetLength, float maxRadius)
{
	EmittedRingForGPU& ring = params_.emittedRings[nextRingSlot_];
	ring.center = { center.x, center.z };
	ring.emitTime = params_.time;
	ring.amplitude = (std::max)(amplitude, 0.0f);
	ring.wavelength = (std::max)(wavelength, 0.05f);
	ring.packetLength = (std::max)(packetLength, 0.5f);
	ring.maxRadius = (std::max)(maxRadius, 1.0f);
	nextRingSlot_ = (nextRingSlot_ + 1) % kMaxEmittedRings;
}

void WaterSurface::StartRingBurst(const Vector3& center, float interval, float amplitude, float wavelength)
{
	bursting_ = true;
	burstCenter_ = center;
	burstInterval_ = (std::max)(interval, 0.1f);
	burstAmplitude_ = amplitude;
	burstWavelength_ = wavelength;
	burstTimer_ = 0.0f; // 次の Update で即1発
}

int WaterSurface::GetActiveEmittedRingCount() const
{
	int count = 0;
	for (const EmittedRingForGPU& ring : params_.emittedRings) {
		if (ring.amplitude <= 0.0f) continue;
		// 束の後端が maxRadius を越えるまでは見えている
		const float tail = (params_.time - ring.emitTime) * params_.ringSpeed - ring.packetLength * ring.wavelength;
		if (tail < ring.maxRadius) ++count;
	}
	return count;
}

void WaterSurface::SetFloorMaps(const std::string& normalMapPath, const std::string& heightMapPath, float parallaxDepth)
{
	// どちらも色ではない値なので sRGB として読まない
	TextureManager* tm = TextureManager::GetInstance();
	floorNormalMapPath_ = normalMapPath;
	floorHeightMapPath_ = heightMapPath;
	if (!floorNormalMapPath_.empty()) tm->LoadTextureLinear(floorNormalMapPath_);
	if (!floorHeightMapPath_.empty()) tm->LoadTextureLinear(floorHeightMapPath_);
	params_.floorUseNormalMap = (!floorNormalMapPath_.empty() && tm->HasTexture(floorNormalMapPath_)) ? 1 : 0;
	params_.floorUseParallax = (!floorHeightMapPath_.empty() && tm->HasTexture(floorHeightMapPath_)) ? 1 : 0;
	params_.floorParallaxDepth = parallaxDepth;
}

void WaterSurface::AddRippleImpulse(const Vector3& position, float radius, float strength)
{
	rippleSimulation_->AddImpulse(position, radius, strength);
}

void WaterSurface::DispatchSimulation()
{
	rippleSimulation_->Dispatch();
}

float WaterSurface::NextRandom01()
{
	randomState_ = randomState_ * 1664525u + 1013904223u;
	return static_cast<float>(randomState_ >> 8) / static_cast<float>(1u << 24);
}

void WaterSurface::OnImGui()
{
#ifdef _DEBUG
	ImGui::DragFloat("Water Height", &params_.waterHeight, 0.01f, -100.0f, 100.0f);
	ImGui::DragFloat("Water Depth", &params_.depth, 0.005f, 0.0f, 10.0f);
	ImGui::DragFloat("Water Size", &size_, 1.0f, 1.0f, 5000.0f);
	ImGui::ColorEdit3("Water Color", &params_.waterColor.x);
	ImGui::DragFloat3("Absorption", &params_.absorption.x, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Fresnel F0", &params_.fresnelF0, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Reflection Intensity", &params_.reflectionIntensity, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Sky Intensity", &params_.skyIntensity, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("IOR", &params_.ior, 0.005f, 1.0f, 2.0f);
	ImGui::DragFloat("Floor Tiling", &params_.floorTiling, 0.01f, 0.01f, 10.0f);
	ImGui::DragFloat("Floor Ambient", &params_.ambient, 0.01f, 0.0f, 2.0f);
	if (params_.floorUseNormalMap) {
		ImGui::DragFloat("Floor Normal Strength", &params_.floorNormalStrength, 0.01f, 0.0f, 4.0f);
		bool flip = params_.floorNormalFlipY < 0.0f;
		if (ImGui::Checkbox("Floor Normal Flip Y", &flip)) params_.floorNormalFlipY = flip ? -1.0f : 1.0f;
	}
	if (params_.floorUseParallax) {
		ImGui::DragFloat("Floor Parallax Depth", &params_.floorParallaxDepth, 0.0005f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Floor Parallax Min Layers", &params_.floorParallaxMinLayers, 1.0f, 1.0f, 64.0f);
		ImGui::DragFloat("Floor Parallax Max Layers", &params_.floorParallaxMaxLayers, 1.0f, 1.0f, 64.0f);
	}
	ImGui::SeparatorText("Ripple (rings)");
	ImGui::DragFloat2("Ripple Center XZ", &params_.rippleCenter.x, 0.1f);
	ImGui::DragFloat("Ring Amplitude", &params_.ringAmplitude, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Ring Wavelength", &params_.ringWavelength, 0.01f, 0.05f, 20.0f);
	ImGui::DragFloat("Ring Speed", &params_.ringSpeed, 0.01f, -10.0f, 10.0f);
	ImGui::DragFloat("Ring Falloff", &params_.ringFalloff, 0.5f, 0.5f, 1000.0f);
	ImGui::DragFloat("Ring Interval", &params_.ringInterval, 0.05f, 0.2f, 30.0f);
	ImGui::SliderFloat("Ring Jitter", &params_.ringJitter, 0.0f, 1.0f);
	ImGui::SliderFloat("Ring Min Amplitude", &params_.ringMinAmplitude, 0.0f, 1.0f);
	ImGui::DragFloat("Ring Packet Length", &params_.ringPacketLength, 0.05f, 0.5f, 10.0f);
	ImGui::DragFloat("Ring Max Radius", &params_.ringMaxRadius, 0.5f, 1.0f, 500.0f);
	ImGui::SeparatorText("Ring Control (test)");
	ImGui::DragFloat("Emit Amplitude", &debugEmitAmplitude_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Emit Wavelength", &debugEmitWavelength_, 0.05f, 0.1f, 20.0f);
	ImGui::DragFloat("Burst Interval", &debugBurstInterval_, 0.05f, 0.1f, 10.0f);
	const Vector3 rippleCenter{ params_.rippleCenter.x, params_.waterHeight, params_.rippleCenter.y };
	if (ImGui::Button("Emit Ring")) {
		EmitRing(rippleCenter, debugEmitAmplitude_, debugEmitWavelength_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Burst Start")) {
		StartRingBurst(rippleCenter, debugBurstInterval_, debugEmitAmplitude_, debugEmitWavelength_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Burst Stop")) {
		StopRingBurst();
	}
	ImGui::Text("Emitted rings alive: %d / %d %s", GetActiveEmittedRingCount(), kMaxEmittedRings,
		bursting_ ? "(bursting)" : "");
	ImGui::DragFloat("Ambient Target", &debugAmbientTarget_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Ambient Blend [s]", &debugAmbientBlend_, 0.05f, 0.0f, 10.0f);
	if (ImGui::Button("Apply Ambient")) {
		SetAmbientRingScale(debugAmbientTarget_, debugAmbientBlend_);
	}
	ImGui::SameLine();
	ImGui::Text("current: %.2f", params_.ambientRingScale);
	ImGui::SeparatorText("Ripple Simulation (GPU)");
	{
		RippleSimulation::Settings& sim = rippleSimulation_->GetSettings();
		ImGui::DragFloat("Sim Size [m]", &sim.simSize, 0.5f, 2.0f, 500.0f);
		ImGui::DragFloat("Wave Speed [m/s]", &sim.waveSpeed, 0.05f, 0.05f, 20.0f);
		ImGui::SliderFloat("Damping", &sim.damping, 0.9f, 1.0f, "%.4f");
		ImGui::DragFloat("Steps / s", &sim.stepsPerSecond, 1.0f, 10.0f, 240.0f);
		ImGui::SliderFloat("Edge Fade", &sim.edgeFade, 0.001f, 0.5f);
		ImGui::DragFloat("Normal Scale", &sim.normalScale, 0.01f, 0.0f, 20.0f);
		if (ImGui::Button("Clear Ripples")) rippleSimulation_->Clear();
	}
	ImGui::SeparatorText("Undulation (noise)");
	ImGui::DragFloat("Noise Amplitude", &params_.noiseAmplitude, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Noise Scale", &params_.noiseScale, 0.01f, 0.01f, 20.0f);
	ImGui::DragFloat("Noise Speed", &params_.noiseSpeed, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Distortion", &params_.distortion, 0.001f, 0.0f, 0.3f);
	const char* views[] = { "Composite", "Floor only", "Reflection only", "Fresnel", "Normal" };
	ImGui::Combo("Debug View", &params_.debugView, views, IM_ARRAYSIZE(views));
#endif
}
