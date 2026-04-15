#include "PostEffect.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

// ===== cbuffer構造体定義（C++側） =====
// シェーダーのcbufferと完全にレイアウトを一致させること

struct SepiaParamsCB
{
	float intensity = 0.0f;       // offset: 0
	float sepiaColorR = 1.0f;    // offset: 4
	float sepiaColorG = 0.691f;  // offset: 8
	float sepiaColorB = 0.402f;  // offset: 12
};

struct VignetteParamsCB
{
	float intensity = 0.5f;  // offset: 0
	float power = 0.8f;      // offset: 4
	float scale = 16.0f;     // offset: 8
	float _padding = 0.0f;   // offset: 12
};

struct SmoothingParamsCB
{
	int kernelSize = 3;      // offset: 0
	float _padding[3];       // offset: 4-15
};

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

	CreateRootSignatures();
	CreatePipelineStates();
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
}

// ===================================================================
// パイプライン作成 & エフェクト登録
// ===================================================================

void PostEffect::CreatePipelineStates()
{
	HRESULT hr;

	// 共通PSO設定
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

	// 頂点シェーダー（Copy用）
	IDxcBlob* copyVsBlob = dxCore_->CompileShader(L"Resources/Shaders/CopyImage.VS.hlsl", L"vs_6_0");
	assert(copyVsBlob);

	// 頂点シェーダー（エフェクト用）
	IDxcBlob* effectVsBlob = dxCore_->CompileShader(L"Resources/Shaders/PostProcess.VS.hlsl", L"vs_6_0");
	assert(effectVsBlob);

	// ----- Copyパイプライン -----
	{
		IDxcBlob* psBlob = dxCore_->CompileShader(L"Resources/Shaders/CopyImage.PS.hlsl", L"ps_6_0");
		assert(psBlob);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
		psoDesc.pRootSignature = copyRootSignature_.Get();
		psoDesc.VS = { copyVsBlob->GetBufferPointer(), copyVsBlob->GetBufferSize() };
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

		hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&copyPipelineState_));
		assert(SUCCEEDED(hr));
	}

	// 以降のエフェクトはeffectVsBlobを共有
	basePsoDesc.VS = { effectVsBlob->GetBufferPointer(), effectVsBlob->GetBufferSize() };

	// ----- エフェクト登録ヘルパーのためにbasePsoDescを保存 -----
	// RegisterEffectの中でパイプラインを作るため、ローカル変数では渡せない
	// →直接ここで全エフェクトのパイプラインを作成する

	auto createEffectPipeline = [&](const wchar_t* psPath, bool needsCBuffer)
		-> Microsoft::WRL::ComPtr<ID3D12PipelineState>
		{
			IDxcBlob* psBlob = dxCore_->CompileShader(psPath, L"ps_6_0");
			assert(psBlob);

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
			psoDesc.pRootSignature = needsCBuffer ? effectRootSignature_.Get() : copyRootSignature_.Get();
			psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
			assert(SUCCEEDED(hr));
			return pso;
		};

	// ----- Grayscale（cbufferなし） -----
	{
		EffectEntry effect;
		effect.name = "Grayscale";
		effect.enabled = false;
		effect.needsCBuffer = false;
		effect.pipelineState = createEffectPipeline(L"Resources/Shaders/Grayscale.PS.hlsl", false);
		effects_.push_back(std::move(effect));
	}

	// ----- Sepia（cbufferあり） -----
	{
		EffectEntry effect;
		effect.name = "Sepia";
		effect.enabled = false;
		effect.needsCBuffer = true;
		effect.constantBufferSize = sizeof(SepiaParamsCB);
		effect.constantBuffer = CreateConstantBuffer(sizeof(SepiaParamsCB));
		effect.constantBuffer->Map(0, nullptr, &effect.constantBufferMappedPtr);
		effect.pipelineState = createEffectPipeline(L"Resources/Shaders/Sepia.PS.hlsl", true);
		// デフォルトパラメータ
		effect.params["intensity"] = 1.0f;
		effect.params["sepiaColorR"] = 1.0f;
		effect.params["sepiaColorG"] = 0.691f;
		effect.params["sepiaColorB"] = 0.402f;
		effects_.push_back(std::move(effect));
	}

	// ----- Vignette（cbufferあり） -----
	{
		EffectEntry effect;
		effect.name = "Vignette";
		effect.enabled = false;
		effect.needsCBuffer = true;
		effect.constantBufferSize = sizeof(VignetteParamsCB);
		effect.constantBuffer = CreateConstantBuffer(sizeof(VignetteParamsCB));
		effect.constantBuffer->Map(0, nullptr, &effect.constantBufferMappedPtr);
		effect.pipelineState = createEffectPipeline(L"Resources/Shaders/Vignette.PS.hlsl", true);
		// デフォルトパラメータ
		effect.params["intensity"] = 0.5f;
		effect.params["power"] = 0.8f;
		effect.params["scale"] = 16.0f;
		effects_.push_back(std::move(effect));
	}

	// ----- Smoothing（cbufferあり） -----
	{
		EffectEntry effect;
		effect.name = "Smoothing";
		effect.enabled = false;
		effect.needsCBuffer = true;
		effect.constantBufferSize = sizeof(SmoothingParamsCB);
		effect.constantBuffer = CreateConstantBuffer(sizeof(SmoothingParamsCB));
		effect.constantBuffer->Map(0, nullptr, &effect.constantBufferMappedPtr);
		effect.pipelineState = createEffectPipeline(L"Resources/Shaders/Smoothing.PS.hlsl", true);
		// デフォルトパラメータ
		effect.params["kernelSize"] = 3;
		effects_.push_back(std::move(effect));
	}
}

// ===================================================================
// 定数バッファ作成ヘルパー
// ===================================================================

Microsoft::WRL::ComPtr<ID3D12Resource> PostEffect::CreateConstantBuffer(uint32_t dataSize)
{
	uint32_t bufferSize = (dataSize + 0xFF) & ~0xFF;

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

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
	HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer));
	assert(SUCCEEDED(hr));

	return buffer;
}

// ===================================================================
// 定数バッファ更新
// ===================================================================

void PostEffect::UpdateEffectConstantBuffer(EffectEntry& effect)
{
	if (!effect.needsCBuffer || !effect.constantBufferMappedPtr) return;

	if (effect.name == "Sepia") {
		SepiaParamsCB cb;
		cb.intensity = std::get<float>(effect.params["intensity"]);
		cb.sepiaColorR = std::get<float>(effect.params["sepiaColorR"]);
		cb.sepiaColorG = std::get<float>(effect.params["sepiaColorG"]);
		cb.sepiaColorB = std::get<float>(effect.params["sepiaColorB"]);
		memcpy(effect.constantBufferMappedPtr, &cb, sizeof(cb));
	}
	else if (effect.name == "Vignette") {
		VignetteParamsCB cb;
		cb.intensity = std::get<float>(effect.params["intensity"]);
		cb.power = std::get<float>(effect.params["power"]);
		cb.scale = std::get<float>(effect.params["scale"]);
		memcpy(effect.constantBufferMappedPtr, &cb, sizeof(cb));
	}
	else if (effect.name == "Smoothing") {
		SmoothingParamsCB cb;
		cb.kernelSize = std::get<int>(effect.params["kernelSize"]);
		// 奇数に補正
		if (cb.kernelSize % 2 == 0) cb.kernelSize += 1;
		if (cb.kernelSize < 1) cb.kernelSize = 1;
		memcpy(effect.constantBufferMappedPtr, &cb, sizeof(cb));
	}
}

// ===================================================================
// シーン描画開始/終了
// ===================================================================

void PostEffect::BeginSceneRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle)
{
	// シーンはRenderTexture Aに描画する
	renderTextureA_->BeginRender(commandList, dsvHandle);
}

void PostEffect::EndSceneRender(ID3D12GraphicsCommandList* commandList)
{
	renderTextureA_->EndRender(commandList);
}

// ===================================================================
// マルチパス描画
// ===================================================================
void PostEffect::Draw(ID3D12GraphicsCommandList* commandList)
{
	std::vector<EffectEntry*> activeEffects;
	for (auto& effect : effects_) {
		if (effect.enabled) {
			activeEffects.push_back(&effect);
		}
	}

	if (activeEffects.empty()) {
		DrawCopy(commandList, renderTextureA_.get());
		return;
	}

	if (activeEffects.size() == 1) {
		DrawEffect(commandList, *activeEffects[0], renderTextureA_.get());
		return;
	}

	RenderTexture* currentInput = renderTextureA_.get();
	RenderTexture* currentOutput = renderTextureB_.get();

	for (size_t i = 0; i < activeEffects.size(); ++i) {
		bool isLast = (i == activeEffects.size() - 1);

		if (isLast) {
			// 最後のパス: SwapchainのRTVに戻す
			dxCore_->RestoreSwapchainRenderTarget(commandList);
			srvManager_->PreDraw();

			DrawEffect(commandList, *activeEffects[i], currentInput);
		} else {
			currentOutput->BeginRender(commandList);
			srvManager_->PreDraw();
			DrawEffect(commandList, *activeEffects[i], currentInput);
			currentOutput->EndRender(commandList);

			RenderTexture* temp = currentInput;
			currentInput = currentOutput;
			currentOutput = temp;
		}
	}
}

// ===================================================================
// 1エフェクト分の描画
// ===================================================================

void PostEffect::DrawEffect(ID3D12GraphicsCommandList* commandList, EffectEntry& effect, RenderTexture* input)
{
	ID3D12RootSignature* rootSig = effect.needsCBuffer ? effectRootSignature_.Get() : copyRootSignature_.Get();

	if (effect.needsCBuffer) {
		UpdateEffectConstantBuffer(effect);
	}

	commandList->SetGraphicsRootSignature(rootSig);
	commandList->SetPipelineState(effect.pipelineState.Get());
	srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());

	if (effect.needsCBuffer) {
		commandList->SetGraphicsRootConstantBufferView(1, effect.constantBuffer->GetGPUVirtualAddress());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

// ===================================================================
// コピー描画（エフェクトなし）
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
// ゲームコード用API
// ===================================================================

void PostEffect::SetEffectEnabled(const std::string& name, bool enabled)
{
	EffectEntry* effect = FindEffect(name);
	if (effect) {
		effect->enabled = enabled;
	}

	else {
		OutputDebugStringA(("PostEffect::SetEffectEnabled - Unknown effect: " + name + "\n").c_str());
		assert(false && "Unknown effect name");
	}
}

void PostEffect::SetEffectParam(const std::string& name, const std::string& param, float value)
{
	EffectEntry* effect = FindEffect(name);
	if (!effect) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Unknown effect: " + name + "\n").c_str());
		assert(false && "Unknown effect name");
		return;
	}

	auto it = effect->params.find(param);
	if (it == effect->params.end()) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Unknown param: " + param + " on " + name + "\n").c_str());
		assert(false && "Unknown param name");
		return;
	}

	if (!std::holds_alternative<float>(it->second)) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Type mismatch: " + param + " on " + name + " is not float\n").c_str());
		assert(false && "Param type mismatch: expected float");
		return;
	}

	it->second = value;
}

void PostEffect::SetEffectParam(const std::string& name, const std::string& param, int value)
{
	EffectEntry* effect = FindEffect(name);
	if (!effect) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Unknown effect: " + name + "\n").c_str());
		assert(false && "Unknown effect name");
		return;
	}

	auto it = effect->params.find(param);
	if (it == effect->params.end()) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Unknown param: " + param + " on " + name + "\n").c_str());
		assert(false && "Unknown param name");
		return;
	}

	if (!std::holds_alternative<int>(it->second)) {
		OutputDebugStringA(("PostEffect::SetEffectParam - Type mismatch: " + param + " on " + name + " is not int\n").c_str());
		assert(false && "Param type mismatch: expected int");
		return;
	}

	it->second = value;
}

void PostEffect::SetEffectOrder(const std::vector<std::string>& order)
{
	std::vector<EffectEntry> reordered;
	reordered.reserve(effects_.size());

	// 指定された順番のエフェクトを先に追加
	for (const auto& name : order) {
		for (auto& effect : effects_) {
			if (effect.name == name) {
				reordered.push_back(std::move(effect));
				break;
			}
		}
	}

	// orderに含まれなかったエフェクトを末尾に追加
	for (auto& effect : effects_) {
		if (effect.pipelineState) { // moveされていないもの
			reordered.push_back(std::move(effect));
		}
	}

	effects_ = std::move(reordered);
}

void PostEffect::ResetEffects()
{
	for (auto& effect : effects_) {
		effect.enabled = false;

		if (effect.name == "Sepia") {
			effect.params["intensity"] = 1.0f;
			effect.params["sepiaColorR"] = 1.0f;
			effect.params["sepiaColorG"] = 0.691f;
			effect.params["sepiaColorB"] = 0.402f;
		}
		else if (effect.name == "Vignette") {
			effect.params["intensity"] = 0.5f;
			effect.params["power"] = 0.8f;
			effect.params["scale"] = 16.0f;
		}
		else if (effect.name == "Smoothing") {
			effect.params["kernelSize"] = 3;
		}
	}
}

bool PostEffect::IsEffectEnabled(const std::string& name) const
{
	const EffectEntry* effect = FindEffect(name);
	return effect ? effect->enabled : false;
}

// ===================================================================
// ダメージ演出ヘルパー
// ===================================================================

void PostEffect::ApplyDamageEffect(float damageRatio)
{
	damageRatio = std::clamp(damageRatio, 0.0f, 1.0f);

	// ヴィネット: HPが減るほど視野が狭くなる
	SetEffectEnabled("Vignette", damageRatio > 0.0f);
	SetEffectParam("Vignette", "intensity", damageRatio * 1.2f);
	SetEffectParam("Vignette", "scale", 16.0f - damageRatio * 10.0f);

	// セピア / グレースケール
	if (damageRatio >= 0.9f) {
		SetEffectEnabled("Grayscale", true);
		SetEffectEnabled("Sepia", false);
	}
	else if (damageRatio > 0.0f) {
		SetEffectEnabled("Grayscale", false);
		SetEffectEnabled("Sepia", true);
		SetEffectParam("Sepia", "intensity", (damageRatio / 0.9f) * 0.8f);
	}
	else {
		SetEffectEnabled("Grayscale", false);
		SetEffectEnabled("Sepia", false);
	}
}

// ===================================================================
// ImGui
// ===================================================================

void PostEffect::ShowImGui()
{
#ifdef USE_IMGUI
	ImGui::Begin("Post Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Drag to reorder, check to enable");
	ImGui::Separator();

	int moveFrom = -1;
	int moveTo = -1;

	for (int i = 0; i < static_cast<int>(effects_.size()); ++i) {
		auto& effect = effects_[i];

		ImGui::PushID(i);

		// ON/OFFチェックボックス
		ImGui::Checkbox(effect.name.c_str(), &effect.enabled);

		// ドラッグで順番入れ替え
		if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
			int next = i + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
			if (next >= 0 && next < static_cast<int>(effects_.size())) {
				moveFrom = i;
				moveTo = next;
				ImGui::ResetMouseDragDelta();
			}
		}

		// パラメータ表示（ONの時だけ）
		if (effect.enabled && !effect.params.empty()) {
			ImGui::Indent();

			if (effect.name == "Sepia") {
				float intensity = std::get<float>(effect.params["intensity"]);
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
					effect.params["intensity"] = intensity;
				}
				float color[3] = {
					std::get<float>(effect.params["sepiaColorR"]),
					std::get<float>(effect.params["sepiaColorG"]),
					std::get<float>(effect.params["sepiaColorB"])
				};
				if (ImGui::ColorEdit3("Color", color)) {
					effect.params["sepiaColorR"] = color[0];
					effect.params["sepiaColorG"] = color[1];
					effect.params["sepiaColorB"] = color[2];
				}
			}
			else if (effect.name == "Vignette") {
				float intensity = std::get<float>(effect.params["intensity"]);
				float power = std::get<float>(effect.params["power"]);
				float scale = std::get<float>(effect.params["scale"]);
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 2.0f)) effect.params["intensity"] = intensity;
				if (ImGui::SliderFloat("Power", &power, 0.1f, 3.0f)) effect.params["power"] = power;
				if (ImGui::SliderFloat("Scale", &scale, 1.0f, 50.0f)) effect.params["scale"] = scale;
			}
			else if (effect.name == "Smoothing") {
				int kernelSize = std::get<int>(effect.params["kernelSize"]);
				if (ImGui::SliderInt("Kernel Size", &kernelSize, 1, 15)) {
					if (kernelSize % 2 == 0) kernelSize += 1;
					effect.params["kernelSize"] = kernelSize;
				}
			}

			ImGui::Unindent();
		}

		ImGui::PopID();
	}

	// 順番入れ替え実行
	if (moveFrom >= 0 && moveTo >= 0) {
		std::swap(effects_[moveFrom], effects_[moveTo]);
	}

	ImGui::Separator();
	if (ImGui::Button("Reset All")) {
		ResetEffects();
	}

	ImGui::End();
#endif
}

// ===================================================================
// エフェクト検索
// ===================================================================

EffectEntry* PostEffect::FindEffect(const std::string& name)
{
	for (auto& effect : effects_) {
		if (effect.name == name) return &effect;
	}
	return nullptr;
}

const EffectEntry* PostEffect::FindEffect(const std::string& name) const
{
	for (const auto& effect : effects_) {
		if (effect.name == name) return &effect;
	}
	return nullptr;
}

// ===================================================================
// 終了処理
// ===================================================================

void PostEffect::Finalize()
{
	for (auto& effect : effects_) {
		if (effect.constantBuffer && effect.constantBufferMappedPtr) {
			effect.constantBuffer->Unmap(0, nullptr);
			effect.constantBufferMappedPtr = nullptr;
		}
		effect.pipelineState.Reset();
		effect.constantBuffer.Reset();
	}
	effects_.clear();

	copyRootSignature_.Reset();
	effectRootSignature_.Reset();
	copyPipelineState_.Reset();

	if (renderTextureA_) renderTextureA_->Finalize();
	if (renderTextureB_) renderTextureB_->Finalize();
}
