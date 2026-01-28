#include "RenderTexture.h"
#include "DirectXCore.h"
#include "SRVManager.h"

void RenderTexture::Initialize(
	DirectXCore* dxCore,
	SRVManager* srvManager,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	const float clearColor[4]
)
{
	// 引数を保存
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	width_ = width;
	height_ = height;
	format_ = format;

	// クリアカラーが指定されていれば設定
	if (clearColor != nullptr) {
		clearColor_[0] = clearColor[0];
		clearColor_[1] = clearColor[1];
		clearColor_[2] = clearColor[2];
		clearColor_[3] = clearColor[3];
	}

	// 各リソースを作成
	CreateResource();
	CreateRTV();
	CreateSRV();
}

void RenderTexture::CreateResource()
{
	// ===== リソースの設定 =====
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = width_;
	resourceDesc.Height = height_;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;  // ミップマップは使わない
	resourceDesc.Format = format_;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	// ★重要：ALLOW_RENDER_TARGETフラグを指定
	// これがないとRTVとして使えない
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// ===== ヒープの設定 =====
	D3D12_HEAP_PROPERTIES heapProperties{};
	// VRAM上に配置（GPUからの読み書きが高速）
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// ===== クリアバリューの設定 =====
	// DX12では、リソース作成時にクリア値を指定しておくと
	// ClearRenderTargetViewで同じ値を使った時に最適化される
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format_;
	clearValue.Color[0] = clearColor_[0];
	clearValue.Color[1] = clearColor_[1];
	clearValue.Color[2] = clearColor_[2];
	clearValue.Color[3] = clearColor_[3];

	// ===== リソース作成 =====
	// 初期ステートはRENDER_TARGET（すぐに描画先として使える状態）
	HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,  // 初期ステート
		&clearValue,                          // クリア最適値
		IID_PPV_ARGS(&resource_)
	);
	assert(SUCCEEDED(hr));

	// 現在のステートを記録
	currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

void RenderTexture::CreateRTV()
{
	// ===== RTV用ディスクリプタヒープを作成 =====
	// シンプルな設計として、RenderTextureごとに1つのヒープを持つ
	// 複数のRenderTextureを使う場合は、RTVManagerを作ってまとめて管理するのが良い
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // RTVはシェーダーから見えない

	HRESULT hr = dxCore_->GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(&rtvHeap_)
	);
	assert(SUCCEEDED(hr));

	// ===== RTVを作成 =====
	rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = format_;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	dxCore_->GetDevice()->CreateRenderTargetView(
		resource_.Get(),
		&rtvDesc,
		rtvHandle_
	);
}

void RenderTexture::CreateSRV()
{
	// SRVManagerからインデックスを確保
	assert(srvManager_->CanAllocate());
	srvIndex_ = srvManager_->Allocate();

	// SRVを作成（既存のSRVManagerの機能を使用）
	srvManager_->CreateSRVForTexture2D(
		srvIndex_,
		resource_.Get(),
		format_,
		1  // MipLevels
	);
}

void RenderTexture::BeginRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle)
{
	// ===== リソースバリア =====
	// 現在のステートがRENDER_TARGETでなければ遷移
	if (currentState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource_.Get();
		barrier.Transition.StateBefore = currentState_;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);

		currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	// ===== レンダーターゲットをクリア =====
	// ClearValueで指定した色と同じ色を使うと最適化される
	commandList->ClearRenderTargetView(rtvHandle_, clearColor_, 0, nullptr);

	// ===== レンダーターゲットを設定 =====
	commandList->OMSetRenderTargets(1, &rtvHandle_, FALSE, dsvHandle);

	// ===== ビューポートとシザー矩形を設定 =====
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = static_cast<float>(width_);
	viewport.Height = static_cast<float>(height_);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = static_cast<LONG>(width_);
	scissorRect.bottom = static_cast<LONG>(height_);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void RenderTexture::EndRender(ID3D12GraphicsCommandList* commandList)
{
	// ===== リソースバリア =====
	// RENDER_TARGET → PIXEL_SHADER_RESOURCE
	// これにより、シェーダーからテクスチャとして読めるようになる
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void RenderTexture::Finalize()
{
	// ComPtrが自動で解放するので、明示的な処理は不要
	// 必要に応じてリソースの参照をクリア
	resource_.Reset();
	rtvHeap_.Reset();
}
