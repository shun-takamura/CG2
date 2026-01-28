#include "PostEffect.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "RenderTexture.h"
#include <cassert>

void PostEffect::Initialize(DirectXCore* dxCore, SRVManager* srvManager)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;

	// ルートシグネチャとパイプラインを作成
	CreateRootSignature();
	CreatePipelineState();
}

void PostEffect::CreateRootSignature()
{
	HRESULT hr;

	// ===== ディスクリプタレンジの設定 =====
	// t0: テクスチャ用
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;  // t0から始まる
	descriptorRange[0].NumDescriptors = 1;       // 1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // SRV
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ===== ルートパラメータの設定 =====
	D3D12_ROOT_PARAMETER rootParameters[1] = {};

	// [0]: テクスチャ（ディスクリプタテーブル）
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // ピクセルシェーダーで使用
	rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

	// ===== スタティックサンプラーの設定 =====
	// s0: テクスチャサンプラー
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;  // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;  // s0
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// ===== ルートシグネチャの設定 =====
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pStaticSamplers = staticSamplers;
	rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

	// ===== シリアライズしてルートシグネチャを作成 =====
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);

	// エラーがあれば出力
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
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));
}

void PostEffect::CreatePipelineState()
{
	HRESULT hr;

	// ===== シェーダーをコンパイル =====
	// DirectXCoreのCompileShader関数を使用
	IDxcBlob* vsBlob = dxCore_->CompileShader(
		L"Resources/Shaders/CopyImage.VS.hlsl",
		L"vs_6_0"
	);
	IDxcBlob* psBlob = dxCore_->CompileShader(
		L"Resources/Shaders/CopyImage.PS.hlsl",
		L"ps_6_0"
	);
	assert(vsBlob != nullptr);
	assert(psBlob != nullptr);

	// ===== パイプラインステートの設定 =====
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

	// ルートシグネチャ
	psoDesc.pRootSignature = rootSignature_.Get();

	// シェーダー
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	// ===== InputLayout =====
	// 頂点データを入力しないので空にする
	// SV_VertexIDを使って頂点位置を計算するため
	psoDesc.InputLayout.pInputElementDescs = nullptr;
	psoDesc.InputLayout.NumElements = 0;

	// ===== ブレンドステート =====
	// 単純なコピーなのでブレンドなし
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState = blendDesc;

	// ===== ラスタライザステート =====
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;  // カリングなし（全画面なので）
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;
	psoDesc.RasterizerState = rasterizerDesc;

	// ===== 深度ステンシルステート =====
	// 全画面描画では深度テスト不要
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = FALSE;  // 深度テスト無効
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState = depthStencilDesc;

	// ===== その他の設定 =====
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // Swapchainと同じフォーマット
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;  // 深度バッファなし
	psoDesc.SampleDesc.Count = 1;

	// ===== パイプラインステートを作成 =====
	hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc,
		IID_PPV_ARGS(&pipelineState_)
	);
	assert(SUCCEEDED(hr));
}

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* renderTexture)
{
	// ===== パイプラインを設定 =====
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());

	// ===== テクスチャを設定 =====
	// RenderTextureのSRVをルートパラメータ[0]にバインド
	srvManager_->SetGraphicsRootDescriptorTable(0, renderTexture->GetSRVIndex());

	// ===== プリミティブトポロジを設定 =====
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== 描画 =====
	// 3頂点を描画（頂点バッファなしで、SV_VertexIDを使用）
	// VertexShaderで頂点位置を計算するので、ここでは頂点数だけ指定
	commandList->DrawInstanced(3, 1, 0, 0);
}

void PostEffect::Finalize()
{
	rootSignature_.Reset();
	pipelineState_.Reset();
}
