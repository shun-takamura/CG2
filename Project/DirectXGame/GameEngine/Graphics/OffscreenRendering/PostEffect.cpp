#include "PostEffect.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include <cassert>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

// ===================================================================
// 初期化
// ===================================================================

void PostEffect::Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t width, uint32_t height)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	width_ = width;
	height_ = height;

	// ピンポンRenderTexture作成
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	renderTextureA_ = std::make_unique<RenderTexture>();
	renderTextureA_->Initialize(dxCore_, srvManager_, width, height,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor);

	renderTextureB_ = std::make_unique<RenderTexture>();
	renderTextureB_->Initialize(dxCore_, srvManager_, width, height,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor);

	// IDマスク用 RT（uint8、0 で初期化）。MaskedGrayscale 等が参照する
	float idClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	idMaskRT_ = std::make_unique<RenderTexture>();
	idMaskRT_->Initialize(dxCore_, srvManager_, width, height,
		DXGI_FORMAT_R8_UINT, idClear);

	// Distortion 用 RT。RG=0.5（オフセット0）, A=0（強度0）で初期化
	float distortionClear[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
	distortionRT_ = std::make_unique<RenderTexture>();
	distortionRT_->Initialize(dxCore_, srvManager_, width, height,
		DXGI_FORMAT_R8G8B8A8_UNORM, distortionClear);

	// シーンキャプチャ RT（ディスラプター崩壊の破片用。シーン RT と同じ sRGB）
	captureRT_ = std::make_unique<RenderTexture>();
	captureRT_->Initialize(dxCore_, srvManager_, width, height,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor);

	CreateRootSignatures();
	CreateBasePsoDesc();
	InitializeEffects();
}

// ===================================================================
// ルートシグネチャ作成
// ===================================================================

void PostEffect::CreateRootSignatures()
{
	HRESULT hr;

	// ----- copyRootSignature（テクスチャのみ） -----
	{
		D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
		descriptorRange[0].BaseShaderRegister = 0;
		descriptorRange[0].NumDescriptors = 1;
		descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[1] = {};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
		staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		staticSamplers[0].ShaderRegister = 0;
		staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		desc.pParameters = rootParameters;
		desc.NumParameters = _countof(rootParameters);
		desc.pStaticSamplers = staticSamplers;
		desc.NumStaticSamplers = _countof(staticSamplers);

		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
		hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
		hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(), IID_PPV_ARGS(&copyRootSignature_));
		assert(SUCCEEDED(hr));
	}

	// ----- effectRootSignature（テクスチャ + cbuffer） -----
	{
		D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
		descriptorRange[0].BaseShaderRegister = 0;
		descriptorRange[0].NumDescriptors = 1;
		descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[2] = {};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[1].Descriptor.ShaderRegister = 0;
		rootParameters[1].Descriptor.RegisterSpace = 0;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
		staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		staticSamplers[0].ShaderRegister = 0;
		staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		desc.pParameters = rootParameters;
		desc.NumParameters = _countof(rootParameters);
		desc.pStaticSamplers = staticSamplers;
		desc.NumStaticSamplers = _countof(staticSamplers);

		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
		hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
		hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(), IID_PPV_ARGS(&effectRootSignature_));
		assert(SUCCEEDED(hr));
	}

	// ----- outlineRootSignature（color t0 + depth t1 + cbuffer b0 + linear/point sampler） -----
	{
		// color t0
		D3D12_DESCRIPTOR_RANGE colorRange[1] = {};
		colorRange[0].BaseShaderRegister = 0;
		colorRange[0].NumDescriptors = 1;
		colorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		colorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// depth t1
		D3D12_DESCRIPTOR_RANGE depthRange[1] = {};
		depthRange[0].BaseShaderRegister = 1;
		depthRange[0].NumDescriptors = 1;
		depthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		depthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[3] = {};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = colorRange;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[1].DescriptorTable.pDescriptorRanges = depthRange;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].Descriptor.ShaderRegister = 0;
		rootParameters[2].Descriptor.RegisterSpace = 0;

		// linear(s0) と point(s1) を用意（Depthの補間防止用）
		D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
		staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		staticSamplers[0].ShaderRegister = 0;
		staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
		staticSamplers[1].ShaderRegister = 1;
		staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		desc.pParameters = rootParameters;
		desc.NumParameters = _countof(rootParameters);
		desc.pStaticSamplers = staticSamplers;
		desc.NumStaticSamplers = _countof(staticSamplers);

		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
		hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
		hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(), IID_PPV_ARGS(&outlineRootSignature_));
		assert(SUCCEEDED(hr));
	}

	// ----- distortionRootSignature（color t0 + distortion t1 + depth t2 + cbuffer b0 + linear/point sampler） -----
	{
		D3D12_DESCRIPTOR_RANGE colorRange[1] = {};
		colorRange[0].BaseShaderRegister = 0;
		colorRange[0].NumDescriptors = 1;
		colorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		colorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE distortionRange[1] = {};
		distortionRange[0].BaseShaderRegister = 1;
		distortionRange[0].NumDescriptors = 1;
		distortionRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		distortionRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE depthRange[1] = {};
		depthRange[0].BaseShaderRegister = 2;
		depthRange[0].NumDescriptors = 1;
		depthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		depthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[4] = {};
		// [0] color (t0)
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = colorRange;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		// [1] distortion (t1)
		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[1].DescriptorTable.pDescriptorRanges = distortionRange;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		// [2] depth (t2)
		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].DescriptorTable.pDescriptorRanges = depthRange;
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
		// [3] cbuffer (b0)
		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].Descriptor.ShaderRegister = 0;
		rootParameters[3].Descriptor.RegisterSpace = 0;

		// linear(s0) + point(s1)
		D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
		for (int i = 0; i < 2; ++i) {
			staticSamplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			staticSamplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			staticSamplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			staticSamplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			staticSamplers[i].MaxLOD = D3D12_FLOAT32_MAX;
			staticSamplers[i].ShaderRegister = i;
			staticSamplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
		staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		desc.pParameters = rootParameters;
		desc.NumParameters = _countof(rootParameters);
		desc.pStaticSamplers = staticSamplers;
		desc.NumStaticSamplers = _countof(staticSamplers);

		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
		hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
		hr = dxCore_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(), IID_PPV_ARGS(&distortionRootSignature_));
		assert(SUCCEEDED(hr));
	}
}

// ===================================================================
// 共通PSO設定の作成
// ===================================================================

void PostEffect::CreateBasePsoDesc()
{
	basePsoDesc_ = {};
	basePsoDesc_.InputLayout.pInputElementDescs = nullptr;
	basePsoDesc_.InputLayout.NumElements = 0;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	basePsoDesc_.BlendState = blendDesc;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;
	basePsoDesc_.RasterizerState = rasterizerDesc;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	basePsoDesc_.DepthStencilState = depthStencilDesc;

	basePsoDesc_.SampleMask = UINT_MAX;
	basePsoDesc_.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc_.NumRenderTargets = 1;
	basePsoDesc_.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	basePsoDesc_.DSVFormat = DXGI_FORMAT_UNKNOWN;
	basePsoDesc_.SampleDesc.Count = 1;

	// 頂点シェーダー（エフェクト共通）
	IDxcBlob* vsBlob = dxCore_->CompileShader(L"Resources/Shaders/PostEffect/Common/PostProcess.VS.hlsl", L"vs_6_0");
	assert(vsBlob);
	basePsoDesc_.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };

	// コピー用パイプライン
	{
		IDxcBlob* copyVsBlob = dxCore_->CompileShader(L"Resources/Shaders/PostEffect/Common/CopyImage.VS.hlsl", L"vs_6_0");
		assert(copyVsBlob);
		IDxcBlob* copyPsBlob = dxCore_->CompileShader(L"Resources/Shaders/PostEffect/Common/CopyImage.PS.hlsl", L"ps_6_0");
		assert(copyPsBlob);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC copyPsoDesc = basePsoDesc_;
		copyPsoDesc.pRootSignature = copyRootSignature_.Get();
		copyPsoDesc.VS = { copyVsBlob->GetBufferPointer(), copyVsBlob->GetBufferSize() };
		copyPsoDesc.PS = { copyPsBlob->GetBufferPointer(), copyPsBlob->GetBufferSize() };

		HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&copyPsoDesc, IID_PPV_ARGS(&copyPipelineState_));
		assert(SUCCEEDED(hr));
	}
}

// ===================================================================
// エフェクト初期化
// ===================================================================

void PostEffect::InitializeEffects()
{
	// エフェクトを作成して登録するラムダ
	auto registerEffect = [&](std::unique_ptr<BaseFilterEffect> effect) -> BaseFilterEffect*
		{
			effect->Initialize(dxCore_, copyRootSignature_.Get(), effectRootSignature_.Get(), basePsoDesc_);
			BaseFilterEffect* ptr = effect.get();
			effectOrder_.push_back(ptr);
			effectOwners_.push_back(std::move(effect));
			return ptr;
		};

	// ----- 各エフェクトを登録 -----
	grayscale = static_cast<GrayscaleEffect*>(registerEffect(std::make_unique<GrayscaleEffect>()));
	gaussian = static_cast<GaussianEffect*>(registerEffect(std::make_unique<GaussianEffect>()));
	sepia = static_cast<SepiaEffect*>(registerEffect(std::make_unique<SepiaEffect>()));
	vignette = static_cast<VignetteEffect*>(registerEffect(std::make_unique<VignetteEffect>()));
	smoothing = static_cast<SmoothingEffect*>(registerEffect(std::make_unique<SmoothingEffect>()));
	radialBlur = static_cast<RadialBlurEffect*>(registerEffect(std::make_unique<RadialBlurEffect>()));
	colorInvert = static_cast<ColorInvertEffect*>(registerEffect(std::make_unique<ColorInvertEffect>()));
	disruptorReveal = static_cast<DisruptorRevealEffect*>(registerEffect(std::make_unique<DisruptorRevealEffect>()));
	precisionBlur = static_cast<PrecisionBlurEffect*>(registerEffect(std::make_unique<PrecisionBlurEffect>()));

	// ----- Outline系エフェクトの登録（outline用RootSignatureで初期化） -----
	{
		auto depthEffect = std::make_unique<OutlineDepthEffect>();
		depthEffect->InitializeOutline(dxCore_, outlineRootSignature_.Get(), basePsoDesc_);
		outlineDepth = depthEffect.get();
		effectOrder_.push_back(outlineDepth);
		effectOwners_.push_back(std::move(depthEffect));
	}
	{
		auto normalEffect = std::make_unique<OutlineNormalEffect>();
		normalEffect->InitializeOutline(dxCore_, outlineRootSignature_.Get(), basePsoDesc_);
		outlineNormal = normalEffect.get();
		effectOrder_.push_back(outlineNormal);
		effectOwners_.push_back(std::move(normalEffect));
	}

	// ----- Dissolve（マスクテクスチャを使う、outline用RootSignatureを共用） -----
	{
		auto dissolveEffect = std::make_unique<DissolveEffect>();
		dissolveEffect->InitializeMasked(dxCore_, outlineRootSignature_.Get(), basePsoDesc_);
		dissolve = dissolveEffect.get();
		effectOrder_.push_back(dissolve);
		effectOwners_.push_back(std::move(dissolveEffect));
	}

	// ----- MaskedGrayscale（idMaskRT_ を t1 に参照）-----
	{
		auto mg = std::make_unique<MaskedGrayscaleEffect>();
		mg->InitializeMasked(dxCore_, outlineRootSignature_.Get(), basePsoDesc_, idMaskRT_.get());
		maskedGrayscale = mg.get();
		effectOrder_.push_back(maskedGrayscale);
		effectOwners_.push_back(std::move(mg));
	}

	// ----- Distortion（distortionRT_ を t1 に、scene depth を t2 に参照）-----
	// 専用 RS を使う（color t0 + distortion t1 + depth t2 + cbuffer b0）
	// per-instance の歪み強度を使う設計のため、グローバルストレングスは 1 で固定。
	// 有効/無効は毎フレーム Game.cpp / EffectEditor から EffectManager の状態で自動切替する。
	{
		auto de = std::make_unique<DistortionEffect>();
		de->InitializeMasked(dxCore_, distortionRootSignature_.Get(), basePsoDesc_, distortionRT_.get());
		de->SetStrength(1.0f);
		de->SetEnabled(false); // 既定は OFF、毎フレーム自動切替
		distortion = de.get();
		effectOrder_.push_back(distortion);
		effectOwners_.push_back(std::move(de));
	}
}

// ===================================================================
// ID Pass の開始/終了
// ===================================================================

void PostEffect::BeginIdPass(ID3D12GraphicsCommandList* commandList)
{
	if (!idMaskRT_) return;
	// 0 でクリアして開始
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	idMaskRT_->BeginRender(commandList);
	// BeginRender 内で ClearRenderTargetView は init 時の clear color で行われるはず
	// 念のため明示クリア（カラー指定でゼロにする）
	auto rtv = idMaskRT_->GetRTVHandle();
	commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
}

void PostEffect::EndIdPass(ID3D12GraphicsCommandList* commandList)
{
	if (!idMaskRT_) return;
	idMaskRT_->EndRender(commandList);
}

// ===================================================================
// Distortion Pass の開始/終了
// ===================================================================

void PostEffect::BeginDistortionPass(ID3D12GraphicsCommandList* commandList)
{
	if (!distortionRT_) return;
	// (0.5, 0.5, 0.5, 0) でクリア（オフセット0・強度0）
	const float clear[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
	distortionRT_->BeginRender(commandList);
	auto rtv = distortionRT_->GetRTVHandle();
	commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
}

void PostEffect::EndDistortionPass(ID3D12GraphicsCommandList* commandList)
{
	if (!distortionRT_) return;
	distortionRT_->EndRender(commandList);
}

void PostEffect::RunDistortionForPreview(ID3D12GraphicsCommandList* commandList,
	uint32_t colorSrvIndex, uint32_t distortionSrvIndex)
{
	if (!distortion) return;

	// strength を最新化
	distortion->UpdateConstantBuffer();

	// distortion 専用 RS：t0=color / t1=distortion / t2=depth / b0=cbuffer
	commandList->SetGraphicsRootSignature(distortionRootSignature_.Get());
	commandList->SetPipelineState(distortion->GetPipelineState());
	srvManager_->SetGraphicsRootDescriptorTable(0, colorSrvIndex);
	srvManager_->SetGraphicsRootDescriptorTable(1, distortionSrvIndex);
	srvManager_->SetGraphicsRootDescriptorTable(2, dxCore_->GetDepthSRVIndex());
	commandList->SetGraphicsRootConstantBufferView(3, distortion->GetConstantBufferGPUAddress());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

// ===================================================================
// シーンキャプチャ（ディスラプター崩壊の破片用）
// ===================================================================

uint32_t PostEffect::GetCaptureSRVIndex() const
{
	return captureRT_ ? captureRT_->GetSRVIndex() : 0u;
}

void PostEffect::CaptureSceneForDisruptor(ID3D12GraphicsCommandList* commandList)
{
	if (!captureRT_ || !renderTextureA_) return;

	// renderTextureA_ は現在 RENDER_TARGET。コピー元にするため SRV に遷移する。
	ID3D12Resource* sceneRes = renderTextureA_->GetResource();
	D3D12_RESOURCE_BARRIER toSrv{};
	toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toSrv.Transition.pResource = sceneRes;
	toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &toSrv);

	// captureRT_ を RT にして全画面コピー（既存の copy パイプライン流用）
	captureRT_->BeginRender(commandList);
	srvManager_->PreDraw();
	DrawCopy(commandList, renderTextureA_.get());
	captureRT_->EndRender(commandList); // capture -> PIXEL_SHADER_RESOURCE

	// renderTextureA_ を RENDER_TARGET に戻す
	D3D12_RESOURCE_BARRIER toRt = toSrv;
	toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	toRt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &toRt);

	// シーン RTV + 深度 + ビューポート/シザーを復帰（以降のシーン描画が継続できるように）
	auto rtv = renderTextureA_->GetRTVHandle();
	auto dsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtv, false, &dsv);
	D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
	D3D12_RECT sc{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
	commandList->RSSetViewports(1, &vp);
	commandList->RSSetScissorRects(1, &sc);
}

// ===================================================================
// シーン描画開始/終了
// ===================================================================

void PostEffect::BeginSceneRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle)
{
	renderTextureA_->BeginRender(commandList, dsvHandle);
}

void PostEffect::EndSceneRender(ID3D12GraphicsCommandList* commandList)
{
	renderTextureA_->EndRender(commandList);
}

// ===================================================================
// マルチパス描画
// ===================================================================

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* outputTarget)
{
	// 最終出力先がViewport用RTか、Swapchainかで分岐するためのヘルパ
	const bool useOutputTarget = (outputTarget != nullptr);

	// 最終出力RTVをセットする処理（Swapchain or 指定された RenderTexture）
	auto bindFinalOutputRTV = [&]() {
		if (useOutputTarget) {
			outputTarget->BeginRender(commandList);
		} else {
			dxCore_->RestoreSwapchainRenderTarget(commandList);
		}
		srvManager_->PreDraw();
	};

	// ONになっているエフェクトを集める
	std::vector<BaseFilterEffect*> activeEffects;
	bool needsDepth = false;
	for (auto* effect : effectOrder_) {
		if (effect->IsEnabled()) {
			activeEffects.push_back(effect);
			if (effect->NeedsDepth()) {
				needsDepth = true;
			}
		}
	}

	// depth参照系がONなら、depthをShaderResource状態に遷移する
	if (needsDepth) {
		dxCore_->TransitionDepthState(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// 全エフェクトOFF → コピーだけ
	if (activeEffects.empty()) {
		// outputTarget が指定されている場合は、ここで RTV を切り替えてから描画する
		// （Swapchain出力時は呼び出し側で BeginDraw 済みなので RTV はセット済み）
		if (useOutputTarget) {
			bindFinalOutputRTV();
		}
		DrawCopy(commandList, renderTextureA_.get());
	}
	// 1つだけ → 直接Swapchainに描画
	else if (activeEffects.size() == 1) {
		if (useOutputTarget) {
			bindFinalOutputRTV();
		}
		DrawEffect(commandList, activeEffects[0], renderTextureA_.get());
	}
	else {
		// 複数エフェクトのピンポン描画
		RenderTexture* currentInput = renderTextureA_.get();
		RenderTexture* currentOutput = renderTextureB_.get();

		for (size_t i = 0; i < activeEffects.size(); ++i) {
			bool isLast = (i == activeEffects.size() - 1);

			if (isLast) {
				// 最後のパス: 最終出力RTV（Swapchain or outputTarget）にセットして描画
				bindFinalOutputRTV();
				DrawEffect(commandList, activeEffects[i], currentInput);
			}
			else {
				// 中間パス: currentOutputに描画
				currentOutput->BeginRender(commandList);
				srvManager_->PreDraw();
				DrawEffect(commandList, activeEffects[i], currentInput);
				currentOutput->EndRender(commandList);

				// 入力と出力を入れ替え
				RenderTexture* temp = currentInput;
				currentInput = currentOutput;
				currentOutput = temp;
			}
		}
	}

	// outputTarget を使った場合は、PIXEL_SHADER_RESOURCE 状態に戻して
	// ImGui::Image でSRVとして読めるようにする
	if (useOutputTarget) {
		outputTarget->EndRender(commandList);
	}

	// depthを次フレームのために書き込み可能状態に戻す
	if (needsDepth) {
		dxCore_->TransitionDepthState(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}
}

// ===================================================================
// 1エフェクト分の描画
// ===================================================================

void PostEffect::DrawEffect(ID3D12GraphicsCommandList* commandList, BaseFilterEffect* effect, RenderTexture* input)
{
	if (effect->NeedsCBuffer()) {
		effect->UpdateConstantBuffer();
	}

	if (effect == distortion) {
		// Distortion: color t0, distortion t1, depth t2, cbuffer b0（専用 RS）
		commandList->SetGraphicsRootSignature(distortionRootSignature_.Get());
		commandList->SetPipelineState(effect->GetPipelineState());
		srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());
		srvManager_->SetGraphicsRootDescriptorTable(1, effect->GetMaskTextureSRVIndex());
		srvManager_->SetGraphicsRootDescriptorTable(2, dxCore_->GetDepthSRVIndex());
		commandList->SetGraphicsRootConstantBufferView(3, effect->GetConstantBufferGPUAddress());
	}
	else if (effect->NeedsDepth() || effect->NeedsMaskTexture()) {
		// Outline系/Dissolve系: color t0, aux t1, cbuffer b0
		uint32_t auxSrvIndex = effect->NeedsDepth()
			? dxCore_->GetDepthSRVIndex()
			: effect->GetMaskTextureSRVIndex();

		commandList->SetGraphicsRootSignature(outlineRootSignature_.Get());
		commandList->SetPipelineState(effect->GetPipelineState());
		srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());
		srvManager_->SetGraphicsRootDescriptorTable(1, auxSrvIndex);
		commandList->SetGraphicsRootConstantBufferView(2, effect->GetConstantBufferGPUAddress());
	}
	else {
		ID3D12RootSignature* rootSig = effect->NeedsCBuffer() ? effectRootSignature_.Get() : copyRootSignature_.Get();
		commandList->SetGraphicsRootSignature(rootSig);
		commandList->SetPipelineState(effect->GetPipelineState());
		srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());

		if (effect->NeedsCBuffer()) {
			commandList->SetGraphicsRootConstantBufferView(1, effect->GetConstantBufferGPUAddress());
		}
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

// ===================================================================
// コピー描画
// ===================================================================

void PostEffect::DrawCopy(ID3D12GraphicsCommandList* commandList, RenderTexture* input)
{
	commandList->SetGraphicsRootSignature(copyRootSignature_.Get());
	commandList->SetPipelineState(copyPipelineState_.Get());
	srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

// ===================================================================
// エフェクト順序設定
// ===================================================================

void PostEffect::SetEffectOrder(const std::vector<std::string>& order)
{
	std::vector<BaseFilterEffect*> reordered;
	reordered.reserve(effectOrder_.size());

	// 指定された順番のエフェクトを先に追加
	for (const auto& name : order) {
		for (auto* effect : effectOrder_) {
			if (effect->GetName() == name) {
				reordered.push_back(effect);
				break;
			}
		}
	}

	// orderに含まれなかったエフェクトを末尾に追加
	for (auto* effect : effectOrder_) {
		bool found = false;
		for (auto* e : reordered) {
			if (e == effect) { found = true; break; }
		}
		if (!found) {
			reordered.push_back(effect);
		}
	}

	effectOrder_ = std::move(reordered);
}

// ===================================================================
// リセット
// ===================================================================

void PostEffect::ResetEffects()
{
	for (auto* effect : effectOrder_) {
		effect->SetEnabled(false);
		effect->ResetParams();
	}
}

// ===================================================================
// 射影行列を全Outline系エフェクトに伝播
// ===================================================================

void PostEffect::SetProjectionMatrix(const Matrix4x4& projection)
{
	for (auto& effect : effectOwners_) {
		if (effect->NeedsDepth()) {
			effect->SetProjectionMatrix(projection);
		}
	}
}

// ===================================================================
// ダメージ演出
// ===================================================================

void PostEffect::ApplyDamageEffect(float damageRatio)
{
	damageRatio = std::clamp(damageRatio, 0.0f, 1.0f);

	// グレースケール
	if (damageRatio >= 0.9f) {
		grayscale->SetEnabled(true);
	}
	else {
		grayscale->SetEnabled(false);
	}
}

// ===================================================================
// ImGui
// ===================================================================

void PostEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::Text("Drag to reorder, check to enable");
	ImGui::Separator();

	int moveFrom = -1;
	int moveTo = -1;

	for (int i = 0; i < static_cast<int>(effectOrder_.size()); ++i) {
		auto* effect = effectOrder_[i];

		// Distortion はエフェクト連動の自動制御（per-instance strength）にしたので、
		// ユーザー操作の対象外。ImGui リストからは除外する。
		if (effect == distortion) continue;

		ImGui::PushID(i);

		// ON/OFFチェックボックス
		bool enabled = effect->IsEnabled();
		if (ImGui::Checkbox(effect->GetName().c_str(), &enabled)) {
			effect->SetEnabled(enabled);
		}

		// ドラッグで順番入れ替え
		if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
			int next = i + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
			if (next >= 0 && next < static_cast<int>(effectOrder_.size())) {
				moveFrom = i;
				moveTo = next;
				ImGui::ResetMouseDragDelta();
			}
		}

		// パラメータ表示（ONの時だけ）
		if (effect->IsEnabled()) {
			ImGui::Indent();
			effect->ShowImGui();
			ImGui::Unindent();
		}

		ImGui::PopID();
	}

	// 順番入れ替え実行
	if (moveFrom >= 0 && moveTo >= 0) {
		std::swap(effectOrder_[moveFrom], effectOrder_[moveTo]);
	}

	ImGui::Separator();
	if (ImGui::Button("Reset All")) {
		ResetEffects();
	}
#endif
}

// ===================================================================
// 終了処理
// ===================================================================

void PostEffect::Finalize()
{
	for (auto& effect : effectOwners_) {
		effect->Finalize();
	}
	effectOrder_.clear();
	effectOwners_.clear();

	copyRootSignature_.Reset();
	effectRootSignature_.Reset();
	outlineRootSignature_.Reset();
	distortionRootSignature_.Reset();
	copyPipelineState_.Reset();

	if (renderTextureA_) renderTextureA_->Finalize();
	if (renderTextureB_) renderTextureB_->Finalize();
	if (idMaskRT_)       idMaskRT_->Finalize();
	if (distortionRT_)   distortionRT_->Finalize();
	if (captureRT_)      captureRT_->Finalize();
}
