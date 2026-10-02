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
#include <dxcapi.h>
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
		kRootCount
	};
}

WaterSurface::WaterSurface() = default;
WaterSurface::~WaterSurface() = default;

void WaterSurface::Initialize(DirectXCore* dxCore, Object3DManager* object3DManager, const std::string& floorTexturePath)
{
	dxCore_ = dxCore;
	object3DManager_ = object3DManager;
	floorTexturePath_ = floorTexturePath;

	TextureManager::GetInstance()->LoadTexture(floorTexturePath_);

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

	// s0 = 通常（ラップ）, s1 = シャドウ比較, s2 = シャドウ生深度, s3 = 反射 RT（クランプ）
	D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
	for (UINT i = 0; i < 4; ++i) {
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

	PEPPER_COUNT("DrawCall");
	cmd->DrawInstanced(4, 1, 0, 0);
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
	ImGui::SeparatorText("Undulation (noise)");
	ImGui::DragFloat("Noise Amplitude", &params_.noiseAmplitude, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Noise Scale", &params_.noiseScale, 0.01f, 0.01f, 20.0f);
	ImGui::DragFloat("Noise Speed", &params_.noiseSpeed, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Distortion", &params_.distortion, 0.001f, 0.0f, 0.3f);
	const char* views[] = { "Composite", "Floor only", "Reflection only", "Fresnel", "Normal" };
	ImGui::Combo("Debug View", &params_.debugView, views, IM_ARRAYSIZE(views));
#endif
}
