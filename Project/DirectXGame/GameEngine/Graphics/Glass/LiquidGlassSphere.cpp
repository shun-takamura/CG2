#include "LiquidGlassSphere.h"

#include "DirectXCore.h"
#include "TextureManager.h"
#include "LightManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Log.h"
#include "PepperMacros.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Water/RippleSimulation.h"
#include <dxcapi.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum RootParam : UINT {
		kRootTransform = 0,    // VS b0
		kRootParams,           // PS b0
		kRootDirectionalLight, // PS b1
		kRootEnvironment,      // PS t0 環境 cubemap
		kRootRippleHeight,     // PS t1 波紋の高さマップ
		kRootCount
	};

	float Clamp01(float v) { return (std::min)((std::max)(v, 0.0f), 1.0f); }
}

LiquidGlassSphere::LiquidGlassSphere() = default;
LiquidGlassSphere::~LiquidGlassSphere() = default;

void LiquidGlassSphere::Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t divisions)
{
	dxCore_ = dxCore;
	rippleSimulation_ = std::make_unique<RippleSimulation>();
	rippleSimulation_->Initialize(dxCore, srvManager);
	// 球の直径は数十 cm。512 テクセルで波が渡り切る刻みの数は範囲によらないので、刻みを細かくして速さを出す
	RippleSimulation::Settings& sim = rippleSimulation_->GetSettings();
	sim.waveSpeed = 0.4f;
	sim.damping = 0.995f;
	sim.stepsPerSecond = 240.0f;
	sim.edgeFade = 0.1f;

	CreateRootSignature();
	pipelineState_ = CreatePipelineState(false);
	bloomPipelineState_ = CreatePipelineState(true);
	CreateMesh(divisions);

	transformResource_ = dxCore_->CreateBufferResource(sizeof(TransformForGPU));
	transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
	transformData_->WVP = MakeIdentity4x4();
	transformData_->World = MakeIdentity4x4();

	paramsResource_ = dxCore_->CreateBufferResource(sizeof(ParamsForGPU));
	paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
	*paramsData_ = ParamsForGPU{};
	bloomParamsResource_ = dxCore_->CreateBufferResource(sizeof(ParamsForGPU));
	bloomParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&bloomParamsData_));
	*bloomParamsData_ = ParamsForGPU{};
}

void LiquidGlassSphere::CreateRootSignature()
{
	D3D12_DESCRIPTOR_RANGE rangeEnvironment{};
	rangeEnvironment.BaseShaderRegister = 0;
	rangeEnvironment.NumDescriptors = 1;
	rangeEnvironment.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeEnvironment.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	D3D12_DESCRIPTOR_RANGE rangeRipple = rangeEnvironment;
	rangeRipple.BaseShaderRegister = 1;

	D3D12_ROOT_PARAMETER rootParameters[kRootCount] = {};
	rootParameters[kRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[kRootTransform].Descriptor.ShaderRegister = 0;
	rootParameters[kRootParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParams].Descriptor.ShaderRegister = 0;
	rootParameters[kRootDirectionalLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootDirectionalLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootDirectionalLight].Descriptor.ShaderRegister = 1;
	rootParameters[kRootEnvironment].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootEnvironment].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootEnvironment].DescriptorTable.pDescriptorRanges = &rangeEnvironment;
	rootParameters[kRootEnvironment].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[kRootRippleHeight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootRippleHeight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootRippleHeight].DescriptorTable.pDescriptorRanges = &rangeRipple;
	rootParameters[kRootRippleHeight].DescriptorTable.NumDescriptorRanges = 1;

	// s0 = 通常（ラップ）, s1 = 波紋の高さマップ（クランプ）
	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	for (UINT i = 0; i < 2; ++i) {
		samplers[i].ShaderRegister = i;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	}
	samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

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

Microsoft::WRL::ComPtr<ID3D12PipelineState> LiquidGlassSphere::CreatePipelineState(bool bloomPass)
{
	IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/Glass/LiquidGlassSphere.VS.hlsl", L"vs_6_0");
	IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/Glass/LiquidGlassSphere.PS.hlsl", L"ps_6_0");
	assert(vs && ps);

	D3D12_INPUT_ELEMENT_DESC inputElements[1] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	// 奥の面は PS が捨てる（巻き順に頼らない）
	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.DepthClipEnable = TRUE;

	D3D12_BLEND_DESC blend{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	if (bloomPass) {
		// 発光 RT：光る分を足すだけ
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
	} else {
		// 乗算済み α：奥 × (1 - a) に、ガラスの映り込みと液体の発光を足す（a が小さくても光は足される）
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	}

	// 半透明なので深度は見るだけ（後ろの物は不透明物として先に描かれている）
	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
	// 発光 RT は BloomEffect::kFormat
	desc.RTVFormats[0] = bloomPass ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
	assert(SUCCEEDED(hr));
	return pipelineState;
}

void LiquidGlassSphere::CreateMesh(uint32_t divisions)
{
	divisions = (std::max)(divisions, 8u);
	const MeshData mesh = PrimitiveGenerator::CreateSphere(1.0f, divisions);
	std::vector<Vector3> positions;
	positions.reserve(mesh.vertices.size());
	for (const MeshVertex& v : mesh.vertices) positions.push_back(v.position);

	// 頂点は真球の上なので面は内側にへこむ。輪郭の端が欠けないよう、面が真球の外に来るまで広げる
	// （真球の外のピクセルは PS が捨てる）
	meshScale_ = 1.0f / std::cos(3.14159265f / static_cast<float>(divisions)) * 1.01f;

	const UINT vbSize = static_cast<UINT>(sizeof(Vector3) * positions.size());
	vertexBuffer_ = dxCore_->CreateBufferResource(vbSize);
	Vector3* vbMapped = nullptr;
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vbMapped));
	std::copy(positions.begin(), positions.end(), vbMapped);
	vertexBuffer_->Unmap(0, nullptr);
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = vbSize;
	vertexBufferView_.StrideInBytes = sizeof(Vector3);

	indexCount_ = static_cast<uint32_t>(mesh.indices.size());
	const UINT ibSize = static_cast<UINT>(sizeof(uint32_t) * mesh.indices.size());
	indexBuffer_ = dxCore_->CreateBufferResource(ibSize);
	uint32_t* ibMapped = nullptr;
	indexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&ibMapped));
	std::copy(mesh.indices.begin(), mesh.indices.end(), ibMapped);
	indexBuffer_->Unmap(0, nullptr);
	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = ibSize;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

float LiquidGlassSphere::FillToHeight(float amount)
{
	// 半径 1 の球で、底から高さ h までの体積の割合 = h²(3 - h) / 4。単調増加なので二分法で解く
	const float target = Clamp01(amount);
	float lo = 0.0f;
	float hi = 2.0f;
	for (int i = 0; i < 24; ++i) {
		const float mid = 0.5f * (lo + hi);
		const float fraction = mid * mid * (3.0f - mid) * 0.25f;
		if (fraction < target) lo = mid; else hi = mid;
	}
	return 0.5f * (lo + hi);
}

void LiquidGlassSphere::SetFill(float amount, bool immediate)
{
	fillTarget_ = Clamp01(amount);
	if (immediate) fill_ = fillTarget_;
}

Vector3 LiquidGlassSphere::GetSurfaceNormal() const
{
	// 上向きを X 軸まわりに tilt.x、Z 軸まわりに tilt.y だけ倒す
	const float cx = std::cos(tilt_.x);
	return { -cx * std::sin(tilt_.y), cx * std::cos(tilt_.y), std::sin(tilt_.x) };
}

float LiquidGlassSphere::GetSurfaceOffset() const
{
	return (FillToHeight(fill_) - 1.0f) * radius_;
}

void LiquidGlassSphere::ResetMotion()
{
	tilt_ = {};
	tiltVelocity_ = {};
	prevAngularVelocity_ = {};
	hasPrevRotation_ = false;
	rippleSimulation_->Clear();
}

void LiquidGlassSphere::AddRipple(const Vector3& position, float radius, float strength)
{
	rippleSimulation_->AddImpulse(position, radius, strength);
}

float LiquidGlassSphere::NextRandom01()
{
	randomState_ = randomState_ * 1664525u + 1013904223u;
	return static_cast<float>(randomState_ >> 8) / static_cast<float>(1u << 24);
}

void LiquidGlassSphere::UpdateSlosh(float deltaTime)
{
	// 球の角速度（X・Z 軸まわり）。最初のフレームは差が取れないので 0
	Vector2 angularVelocity{};
	if (hasPrevRotation_ && deltaTime > 0.0f) {
		angularVelocity = { (containerRotation_.x - prevContainerRotation_.x) / deltaTime,
			(containerRotation_.z - prevContainerRotation_.z) / deltaTime };
	}
	prevContainerRotation_ = containerRotation_;
	hasPrevRotation_ = true;
	if (deltaTime <= 0.0f) return;

	// 液面は回る壁に引きずられて傾き（目標＝角速度 × drive）、ばね＋減衰で水平へ戻る
	const float maxTilt = (std::max)(motion_.sloshMaxTilt, 0.0f);
	const Vector2 target{ std::clamp(angularVelocity.x * motion_.sloshDrive, -maxTilt, maxTilt),
		std::clamp(angularVelocity.y * motion_.sloshDrive, -maxTilt, maxTilt) };
	const float k = motion_.sloshStiffness;
	const float c = motion_.sloshDamping;
	// 1 フレームが長いと発散するので刻む
	const int steps = (std::max)(1, static_cast<int>(std::ceil(deltaTime / (1.0f / 240.0f))));
	const float h = deltaTime / static_cast<float>(steps);
	for (int i = 0; i < steps; ++i) {
		tiltVelocity_.x += (k * (target.x - tilt_.x) - c * tiltVelocity_.x) * h;
		tiltVelocity_.y += (k * (target.y - tilt_.y) - c * tiltVelocity_.y) * h;
		tilt_.x = std::clamp(tilt_.x + tiltVelocity_.x * h, -maxTilt, maxTilt);
		tilt_.y = std::clamp(tilt_.y + tiltVelocity_.y * h, -maxTilt, maxTilt);
	}

	// 回り方が変わった瞬間に、液面の縁（傾いて持ち上がった側）へ波源を入れる
	sloshRippleCooldown_ = (std::max)(sloshRippleCooldown_ - deltaTime, 0.0f);
	const Vector2 change{ angularVelocity.x - prevAngularVelocity_.x, angularVelocity.y - prevAngularVelocity_.y };
	prevAngularVelocity_ = angularVelocity;
	const float changeLength = std::sqrt(change.x * change.x + change.y * change.y);
	if (changeLength > 0.05f && sloshRippleCooldown_ <= 0.0f && motion_.sloshRippleStrength > 0.0f) {
		const float offset = GetSurfaceOffset();
		const float diskRadius = std::sqrt((std::max)(radius_ * radius_ - offset * offset, 0.0f));
		// 壁に引きずられて持ち上がる側：Z 軸まわりに + 回ると +X 側（+X の壁が上がる）、X 軸まわりに + 回ると -Z 側
		Vector2 dir{ change.y, -change.x };
		const float dirLength = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		dir = { dir.x / dirLength, dir.y / dirLength };
		const Vector3 position{ center_.x + dir.x * diskRadius * 0.8f, center_.y + offset, center_.z + dir.y * diskRadius * 0.8f };
		AddRipple(position, motion_.sloshRippleRadius, motion_.sloshRippleStrength * (std::min)(changeLength, 5.0f));
		sloshRippleCooldown_ = 0.05f;
	}
}

void LiquidGlassSphere::UpdateAmbientRipples(float deltaTime)
{
	if (motion_.ambientRippleInterval <= 0.0f || motion_.ambientRippleStrength <= 0.0f) return;
	ambientRippleTimer_ -= deltaTime;
	if (ambientRippleTimer_ > 0.0f) return;
	ambientRippleTimer_ = motion_.ambientRippleInterval * (0.5f + NextRandom01());

	// 液面の円の中のランダムな点（spread 0 なら中央に同心円が広がる）
	const float offset = GetSurfaceOffset();
	const float diskRadius = std::sqrt((std::max)(radius_ * radius_ - offset * offset, 0.0f));
	const float r = diskRadius * 0.85f * std::clamp(motion_.ambientRippleSpread, 0.0f, 1.0f) * std::sqrt(NextRandom01());
	const float a = NextRandom01() * 6.2831853f;
	AddRipple({ center_.x + r * std::cos(a), center_.y + offset, center_.z + r * std::sin(a) },
		motion_.ambientRippleRadius, motion_.ambientRippleStrength * (0.6f + 0.4f * NextRandom01()));
}

void LiquidGlassSphere::Update(float deltaTime)
{
	time_ += deltaTime;
	const float follow = 1.0f - std::exp(-(std::max)(motion_.fillFollow, 0.0f) * (std::max)(deltaTime, 0.0f));
	fill_ += (fillTarget_ - fill_) * follow;

	UpdateSlosh(deltaTime);
	UpdateAmbientRipples(deltaTime);

	// 波のシミュレーションの範囲は球の中心まわり。球の直径が範囲の rippleCoverage になるように
	RippleSimulation::Settings& sim = rippleSimulation_->GetSettings();
	sim.simSize = 2.0f * radius_ / std::clamp(motion_.rippleCoverage, 0.05f, 1.0f);
	rippleSimulation_->SetCenter({ center_.x, center_.z });
	rippleSimulation_->Update(deltaTime);
}

void LiquidGlassSphere::DispatchSimulation()
{
	rippleSimulation_->Dispatch();
}

void LiquidGlassSphere::WriteParams(ParamsForGPU& dst, const Camera& camera, bool bloomPass) const
{
	const Appearance& a = appearance_;
	dst.center = center_;
	dst.radius = radius_;
	dst.cameraPosition = camera.GetTranslate();
	dst.surfaceOffset = GetSurfaceOffset();
	dst.surfaceNormal = GetSurfaceNormal();
	dst.time = time_;
	dst.glassTint = a.glassTint;
	dst.fresnelF0 = a.fresnelF0;
	dst.rimColor = a.rimColor;
	dst.rimPower = a.rimPower;
	dst.reflectionIntensity = a.reflectionIntensity;
	dst.specularIntensity = a.specularIntensity;
	dst.specularPower = a.specularPower;
	dst.glassAlpha = a.glassAlpha;
	dst.backRimIntensity = a.backRimIntensity;
	dst.lensIor = a.lensIor;
	dst.bloomPass = bloomPass ? 1 : 0;
	dst.rimBloom = a.rimBloom;
	dst.liquidColor = a.liquidColor;
	dst.liquidGlow = a.liquidGlow;
	dst.liquidDensity = a.liquidDensity;
	dst.liquidAbsorption = a.liquidAbsorption;
	dst.coreWhite = a.coreWhite;
	dst.corePower = a.corePower;
	dst.surfaceColor = a.surfaceColor;
	dst.surfaceIntensity = a.surfaceIntensity;
	dst.meniscusWidth = a.meniscusWidth;
	dst.meniscusIntensity = a.meniscusIntensity;
	dst.bloomScale = a.bloomScale;
	dst.rippleNormalScale = a.rippleNormalScale;
	dst.rippleUvScale = 1.0f / (std::max)(rippleSimulation_->GetSettings().simSize, 1e-3f);
	dst.rippleTexel = 1.0f / static_cast<float>(RippleSimulation::kResolution);
	dst.rippleEnabled = 1;
}

void LiquidGlassSphere::DrawImpl(const Camera& camera, const std::string& environmentCubemapPath, bool bloomPass)
{
	auto* cmd = dxCore_->GetCommandList();

	const float s = radius_ * meshScale_;
	Matrix4x4 world = MakeIdentity4x4();
	world.m[0][0] = s;
	world.m[1][1] = s;
	world.m[2][2] = s;
	world.m[3][0] = center_.x;
	world.m[3][1] = center_.y;
	world.m[3][2] = center_.z;
	transformData_->World = world;
	transformData_->WVP = Multiply(world, camera.GetViewProjectionMatrix());

	ID3D12Resource* params = bloomPass ? bloomParamsResource_.Get() : paramsResource_.Get();
	WriteParams(bloomPass ? *bloomParamsData_ : *paramsData_, camera, bloomPass);

	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(bloomPass ? bloomPipelineState_.Get() : pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
	cmd->IASetIndexBuffer(&indexBufferView_);

	cmd->SetGraphicsRootConstantBufferView(kRootTransform, transformResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootParams, params->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(kRootDirectionalLight,
		LightManager::GetInstance()->GetDirectionalLightGpuAddress());
	cmd->SetGraphicsRootDescriptorTable(kRootEnvironment,
		TextureManager::GetInstance()->GetSrvHandleGPU(environmentCubemapPath));
	cmd->SetGraphicsRootDescriptorTable(kRootRippleHeight, rippleSimulation_->GetHeightSrvHandle());

	PEPPER_COUNT("DrawCall");
	cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void LiquidGlassSphere::Draw(const Camera& camera, const std::string& environmentCubemapPath)
{
	PEPPER_SCOPE("LiquidGlassSphere::Draw");
	PEPPER_GPU_SCOPE(dxCore_->GetCommandList(), "LiquidGlassSphere::Draw");
	DrawImpl(camera, environmentCubemapPath, false);
}

void LiquidGlassSphere::DrawBloomPass(const Camera& camera, const std::string& environmentCubemapPath)
{
	DrawImpl(camera, environmentCubemapPath, true);
}

void LiquidGlassSphere::OnImGui()
{
#ifdef _DEBUG
	Appearance& a = appearance_;
	Motion& m = motion_;
	ImGui::Text("Fill %.3f -> %.3f  tilt (%.3f, %.3f) rad", fill_, fillTarget_, tilt_.x, tilt_.y);
	if (ImGui::TreeNode("Glass")) {
		ImGui::ColorEdit3("Tint", &a.glassTint.x, ImGuiColorEditFlags_Float);
		ImGui::DragFloat("Fresnel F0", &a.fresnelF0, 0.002f, 0.0f, 1.0f);
		ImGui::ColorEdit3("Rim Color", &a.rimColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		ImGui::DragFloat("Rim Power", &a.rimPower, 0.05f, 0.5f, 16.0f);
		ImGui::DragFloat("Reflection", &a.reflectionIntensity, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("Specular", &a.specularIntensity, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Specular Power", &a.specularPower, 1.0f, 1.0f, 2048.0f);
		ImGui::DragFloat("Glass Alpha", &a.glassAlpha, 0.002f, 0.0f, 1.0f);
		ImGui::DragFloat("Back Rim", &a.backRimIntensity, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Lens IOR", &a.lensIor, 0.005f, 1.0f, 2.0f);
		ImGui::DragFloat("Rim Bloom", &a.rimBloom, 0.01f, 0.0f, 4.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Liquid")) {
		ImGui::ColorEdit3("Color", &a.liquidColor.x, ImGuiColorEditFlags_Float);
		ImGui::DragFloat("Glow", &a.liquidGlow, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Density [1/m]", &a.liquidDensity, 0.05f, 0.0f, 50.0f);
		ImGui::DragFloat("Absorption [1/m]", &a.liquidAbsorption, 0.05f, 0.0f, 50.0f);
		ImGui::DragFloat("Core White", &a.coreWhite, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Core Power", &a.corePower, 0.05f, 0.1f, 16.0f);
		ImGui::ColorEdit3("Surface Color", &a.surfaceColor.x, ImGuiColorEditFlags_Float);
		ImGui::DragFloat("Surface Intensity", &a.surfaceIntensity, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("Meniscus Width [m]", &a.meniscusWidth, 0.0005f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Meniscus Intensity", &a.meniscusIntensity, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Bloom Scale", &a.bloomScale, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Ripple Normal Scale", &a.rippleNormalScale, 0.01f, 0.0f, 20.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Motion")) {
		ImGui::DragFloat("Fill Follow [1/s]", &m.fillFollow, 0.05f, 0.0f, 30.0f);
		ImGui::DragFloat("Slosh Stiffness", &m.sloshStiffness, 0.5f, 0.0f, 500.0f);
		ImGui::DragFloat("Slosh Damping", &m.sloshDamping, 0.05f, 0.0f, 50.0f);
		ImGui::DragFloat("Slosh Drive", &m.sloshDrive, 0.005f, 0.0f, 2.0f);
		ImGui::DragFloat("Slosh Max Tilt [rad]", &m.sloshMaxTilt, 0.01f, 0.0f, 1.5f);
		ImGui::DragFloat("Ripple Coverage", &m.rippleCoverage, 0.01f, 0.05f, 1.0f);
		ImGui::DragFloat("Slosh Ripple Strength [m]", &m.sloshRippleStrength, 0.0001f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Slosh Ripple Radius [m]", &m.sloshRippleRadius, 0.001f, 0.001f, 0.5f, "%.3f");
		ImGui::DragFloat("Ambient Ripple Interval [s]", &m.ambientRippleInterval, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Ambient Ripple Strength [m]", &m.ambientRippleStrength, 0.0001f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Ambient Ripple Radius [m]", &m.ambientRippleRadius, 0.001f, 0.001f, 0.5f, "%.3f");
		ImGui::SliderFloat("Ambient Ripple Spread", &m.ambientRippleSpread, 0.0f, 1.0f);
		RippleSimulation::Settings& sim = rippleSimulation_->GetSettings();
		ImGui::DragFloat("Wave Speed [m/s]", &sim.waveSpeed, 0.01f, 0.01f, 5.0f);
		ImGui::SliderFloat("Wave Damping", &sim.damping, 0.9f, 1.0f, "%.4f");
		ImGui::DragFloat("Steps / s", &sim.stepsPerSecond, 1.0f, 10.0f, 240.0f);
		ImGui::DragFloat("Test Ripple Strength [m]", &debugRippleStrength_, 0.0005f, 0.0f, 0.1f, "%.4f");
		if (ImGui::Button("Drop at Center")) {
			AddRipple({ center_.x, center_.y + GetSurfaceOffset(), center_.z }, m.sloshRippleRadius, debugRippleStrength_);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Motion")) ResetMotion();
		ImGui::TreePop();
	}
#endif
}
