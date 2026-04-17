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
	IDxcBlob* vsBlob = dxCore_->CompileShader(L"Resources/Shaders/PostProcess.VS.hlsl", L"vs_6_0");
	assert(vsBlob);
	basePsoDesc_.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };

	// コピー用パイプライン
	{
		IDxcBlob* copyVsBlob = dxCore_->CompileShader(L"Resources/Shaders/CopyImage.VS.hlsl", L"vs_6_0");
		assert(copyVsBlob);
		IDxcBlob* copyPsBlob = dxCore_->CompileShader(L"Resources/Shaders/CopyImage.PS.hlsl", L"ps_6_0");
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

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList)
{
	// ONになっているエフェクトを集める
	std::vector<BaseFilterEffect*> activeEffects;
	for (auto* effect : effectOrder_) {
		if (effect->IsEnabled()) {
			activeEffects.push_back(effect);
		}
	}

	// 全エフェクトOFF → コピーだけ
	if (activeEffects.empty()) {
		DrawCopy(commandList, renderTextureA_.get());
		return;
	}

	// 1つだけ → 直接Swapchainに描画
	if (activeEffects.size() == 1) {
		DrawEffect(commandList, activeEffects[0], renderTextureA_.get());
		return;
	}

	// 複数エフェクトのピンポン描画
	RenderTexture* currentInput = renderTextureA_.get();
	RenderTexture* currentOutput = renderTextureB_.get();

	for (size_t i = 0; i < activeEffects.size(); ++i) {
		bool isLast = (i == activeEffects.size() - 1);

		if (isLast) {
			// 最後のパス: SwapchainのRTVに戻して描画
			dxCore_->RestoreSwapchainRenderTarget(commandList);
			srvManager_->PreDraw();
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

// ===================================================================
// 1エフェクト分の描画
// ===================================================================

void PostEffect::DrawEffect(ID3D12GraphicsCommandList* commandList, BaseFilterEffect* effect, RenderTexture* input)
{
	ID3D12RootSignature* rootSig = effect->NeedsCBuffer() ? effectRootSignature_.Get() : copyRootSignature_.Get();

	if (effect->NeedsCBuffer()) {
		effect->UpdateConstantBuffer();
	}

	commandList->SetGraphicsRootSignature(rootSig);
	commandList->SetPipelineState(effect->GetPipelineState());
	srvManager_->SetGraphicsRootDescriptorTable(0, input->GetSRVIndex());

	if (effect->NeedsCBuffer()) {
		commandList->SetGraphicsRootConstantBufferView(1, effect->GetConstantBufferGPUAddress());
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
	ImGui::Begin("Post Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Drag to reorder, check to enable");
	ImGui::Separator();

	int moveFrom = -1;
	int moveTo = -1;

	for (int i = 0; i < static_cast<int>(effectOrder_.size()); ++i) {
		auto* effect = effectOrder_[i];

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

	ImGui::End();
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
	copyPipelineState_.Reset();

	if (renderTextureA_) renderTextureA_->Finalize();
	if (renderTextureB_) renderTextureB_->Finalize();
}
