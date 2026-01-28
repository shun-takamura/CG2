#include "PostEffect.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include "imgui.h"
#include <cassert>

void PostEffect::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;

	CreateCopyRootSignature();
	CreateEffectRootSignature();
	CreatePipelineStates();
	CreateConstantBuffer();
}

void PostEffect::CreateCopyRootSignature()
{
	HRESULT hr;

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

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pStaticSamplers = staticSamplers;
	rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		if (errorBlob) {
			OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	hr = dxCore_->GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&copyRootSignature_)
	);
	assert(SUCCEEDED(hr));
}

void PostEffect::CreateEffectRootSignature()
{
	HRESULT hr;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[2] = {};

	// [0]: テクスチャ
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

	// [1]: 定数バッファ
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

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pStaticSamplers = staticSamplers;
	rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		if (errorBlob) {
			OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	hr = dxCore_->GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&effectRootSignature_)
	);
	assert(SUCCEEDED(hr));
}

void PostEffect::CreatePipelineStates()
{
	HRESULT hr;

	OutputDebugStringA("PostEffect::CreatePipelineStates - Start\n");

	// 共通設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc{};
	basePsoDesc.InputLayout.pInputElementDescs = nullptr;
	basePsoDesc.InputLayout.NumElements = 0;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	basePsoDesc.BlendState = blendDesc;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;
	basePsoDesc.RasterizerState = rasterizerDesc;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	basePsoDesc.DepthStencilState = depthStencilDesc;

	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	basePsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	basePsoDesc.SampleDesc.Count = 1;

	// 1. Copy（CopyImage.VS.hlslを使用）
	{
		OutputDebugStringA("Creating Copy pipeline...\n");
		IDxcBlob* vsBlob = dxCore_->CompileShader(
			L"Resources/Shaders/CopyImage.VS.hlsl",
			L"vs_6_0"
		);
		if (!vsBlob) {
			OutputDebugStringA("ERROR: CopyImage.VS.hlsl compile failed\n");
			assert(false);
		}

		IDxcBlob* psBlob = dxCore_->CompileShader(
			L"Resources/Shaders/CopyImage.PS.hlsl",
			L"ps_6_0"
		);
		if (!psBlob) {
			OutputDebugStringA("ERROR: CopyImage.PS.hlsl compile failed\n");
			assert(false);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = copyRootSignature_.Get();
		psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&copyPipelineState_));
		if (FAILED(hr)) {
			OutputDebugStringA("ERROR: Copy pipeline creation failed\n");
			assert(false);
		}
		OutputDebugStringA("Copy pipeline created successfully\n");
	}

	// 以降のエフェクトはPostProcess.VS.hlslを使用
	OutputDebugStringA("Compiling PostProcess.VS.hlsl...\n");
	IDxcBlob* vsBlob = dxCore_->CompileShader(
		L"Resources/Shaders/PostProcess.VS.hlsl",
		L"vs_6_0"
	);
	if (!vsBlob) {
		OutputDebugStringA("ERROR: PostProcess.VS.hlsl compile failed\n");
		assert(false);
	}
	basePsoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	OutputDebugStringA("PostProcess.VS.hlsl compiled successfully\n");

	// 2. Combined
	{
		OutputDebugStringA("Creating Combined pipeline...\n");
		IDxcBlob* psBlob = dxCore_->CompileShader(
			L"Resources/Shaders/PostProcess.PS.hlsl",
			L"ps_6_0"
		);
		if (!psBlob) {
			OutputDebugStringA("ERROR: PostProcess.PS.hlsl compile failed\n");
			assert(false);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = effectRootSignature_.Get();
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&combinedPipelineState_));
		if (FAILED(hr)) {
			OutputDebugStringA("ERROR: Combined pipeline creation failed\n");
			assert(false);
		}
		OutputDebugStringA("Combined pipeline created successfully\n");
	}

	// 3. Grayscale
	{
		OutputDebugStringA("Creating Grayscale pipeline...\n");
		IDxcBlob* psBlob = dxCore_->CompileShader(
			L"Resources/Shaders/Grayscale.PS.hlsl",
			L"ps_6_0"
		);
		if (!psBlob) {
			OutputDebugStringA("ERROR: Grayscale.PS.hlsl compile failed\n");
			assert(false);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = copyRootSignature_.Get();
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&grayscalePipelineState_));
		if (FAILED(hr)) {
			OutputDebugStringA("ERROR: Grayscale pipeline creation failed\n");
			assert(false);
		}
		OutputDebugStringA("Grayscale pipeline created successfully\n");
	}

	// 4. Sepia
	{
		OutputDebugStringA("Creating Sepia pipeline...\n");
		IDxcBlob* psBlob = dxCore_->CompileShader(
			L"Resources/Shaders/Sepia.PS.hlsl",
			L"ps_6_0"
		);
		if (!psBlob) {
			OutputDebugStringA("ERROR: Sepia.PS.hlsl compile failed\n");
			assert(false);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = effectRootSignature_.Get();
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&sepiaPipelineState_));
		if (FAILED(hr)) {
			OutputDebugStringA("ERROR: Sepia pipeline creation failed\n");
			assert(false);
		}
		OutputDebugStringA("Sepia pipeline created successfully\n");
	}

	// 5. Vignette
	{
		OutputDebugStringA("Creating Vignette pipeline...\n");
		IDxcBlob* psBlob = dxCore_->CompileShader(
			L"Resources/Shaders/Vignette.PS.hlsl",
			L"ps_6_0"
		);
		if (!psBlob) {
			OutputDebugStringA("ERROR: Vignette.PS.hlsl compile failed\n");
			assert(false);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = effectRootSignature_.Get();
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&vignettePipelineState_));
		if (FAILED(hr)) {
			OutputDebugStringA("ERROR: Vignette pipeline creation failed\n");
			assert(false);
		}
		OutputDebugStringA("Vignette pipeline created successfully\n");
	}

	OutputDebugStringA("PostEffect::CreatePipelineStates - Complete\n");
}

void PostEffect::CreateConstantBuffer()
{
	uint32_t bufferSize = (sizeof(PostProcessParams) + 0xFF) & ~0xFF;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = bufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constantBuffer_)
	);
	assert(SUCCEEDED(hr));

	hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantBufferMappedPtr_));
	assert(SUCCEEDED(hr));
}

void PostEffect::UpdateConstantBuffer()
{
	// デバッグ: 送信する値をログ出力
	char debugMsg[256];
	sprintf_s(debugMsg, "UpdateConstantBuffer:\n");
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  grayscaleIntensity: %f\n", params_.grayscaleIntensity);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  sepiaIntensity: %f\n", params_.sepiaIntensity);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  sepiaColor: [%f, %f, %f]\n",
		params_.sepiaColor[0], params_.sepiaColor[1], params_.sepiaColor[2]);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  _padding1: %f\n", params_._padding1);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  vignetteIntensity: %f\n", params_.vignetteIntensity);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  vignettePower: %f\n", params_.vignettePower);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  vignetteScale: %f\n", params_.vignetteScale);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  _padding2: %f\n", params_._padding2);
	OutputDebugStringA(debugMsg);

	sprintf_s(debugMsg, "  sizeof(PostProcessParams): %zu\n", sizeof(PostProcessParams));
	OutputDebugStringA(debugMsg);

	memcpy(constantBufferMappedPtr_, &params_, sizeof(PostProcessParams));
}

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* renderTexture)
{
	ID3D12RootSignature* rootSignature = nullptr;
	ID3D12PipelineState* pipelineState = nullptr;
	bool needsConstantBuffer = false;

	switch (currentEffectType_)
	{
	case 0: // Copy
		rootSignature = copyRootSignature_.Get();
		pipelineState = copyPipelineState_.Get();
		break;
	case 1: // Combined
		rootSignature = effectRootSignature_.Get();
		pipelineState = combinedPipelineState_.Get();
		needsConstantBuffer = true;
		break;
	case 2: // Grayscale
		rootSignature = copyRootSignature_.Get();
		pipelineState = grayscalePipelineState_.Get();
		break;
	case 3: // Sepia
		rootSignature = effectRootSignature_.Get();
		pipelineState = sepiaPipelineState_.Get();
		needsConstantBuffer = true;
		break;
	case 4: // Vignette
		rootSignature = effectRootSignature_.Get();
		pipelineState = vignettePipelineState_.Get();
		needsConstantBuffer = true;
		break;
	default: // フォールバック: Copy
		rootSignature = copyRootSignature_.Get();
		pipelineState = copyPipelineState_.Get();
		break;
	}

	// エラーチェック
	if (!rootSignature || !pipelineState) {
		OutputDebugStringA("PostEffect::Draw - RootSignature or PipelineState is NULL\n");
		return;
	}

	if (needsConstantBuffer) {
		UpdateConstantBuffer();
	}

	commandList->SetGraphicsRootSignature(rootSignature);
	commandList->SetPipelineState(pipelineState);
	srvManager_->SetGraphicsRootDescriptorTable(0, renderTexture->GetSRVIndex());

	if (needsConstantBuffer) {
		commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void PostEffect::ShowImGui()
{
	ImGui::Begin("Post Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	const char* effectTypes[] = { "Copy", "Combined", "Grayscale", "Sepia", "Vignette" };
	ImGui::Combo("Effect Type", &currentEffectType_, effectTypes, 5);

	ImGui::Separator();

	ImGui::Text("Grayscale");
	ImGui::SliderFloat("Intensity##GS", &params_.grayscaleIntensity, 0.0f, 1.0f);

	ImGui::Separator();

	ImGui::Text("Sepia");
	ImGui::SliderFloat("Intensity##Sepia", &params_.sepiaIntensity, 0.0f, 1.0f);
	if (ImGui::ColorEdit3("Color", params_.sepiaColor)) {
		// 値は自動的に更新される
	}
	if (ImGui::Button("Reset Sepia Color")) {
		params_.sepiaColor[0] = 1.0f;
		params_.sepiaColor[1] = 0.691f;
		params_.sepiaColor[2] = 0.402f;
	}

	ImGui::Separator();

	ImGui::Separator();

	if (ImGui::Button("Reset All")) {
		params_ = PostProcessParams();
	}

	ImGui::End();
}

void PostEffect::Finalize()
{
	if (constantBuffer_ && constantBufferMappedPtr_) {
		constantBuffer_->Unmap(0, nullptr);
		constantBufferMappedPtr_ = nullptr;
	}

	copyRootSignature_.Reset();
	effectRootSignature_.Reset();
	copyPipelineState_.Reset();
	combinedPipelineState_.Reset();
	grayscalePipelineState_.Reset();
	sepiaPipelineState_.Reset();
	vignettePipelineState_.Reset();
	constantBuffer_.Reset();
}