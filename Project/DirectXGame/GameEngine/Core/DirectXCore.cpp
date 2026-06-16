#include "DirectXCore.h"
#include "SRVManager.h"
#include "PepperMacros.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <dxgidebug.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

void DirectXCore::Initialize(WindowsApplication* winApp) {

    assert(winApp != nullptr);
    winApp_ = winApp;

    // システムタイマーの分解能を上げる
    timeBeginPeriod(1); 

    // ウィンドウサイズ取得（初期サイズはゲーム解像度と一致）
    int32_t width = winApp_->GetClientWidth();
    int32_t height = winApp_->GetClientHeight();
    swapChainWidth_ = width;
    swapChainHeight_ = height;
    HWND hwnd = winApp_->GetHwnd();  // ここでハンドル取得

    // 1. DXGIファクトリの生成
    CreateFactory();

    // 2. デバイス生成
    CreateDevice();

    // 3. コマンド系オブジェクト生成
    CreateCommandObjects();

    // 4. スワップチェイン生成
    CreateSwapChain(hwnd, width, height);

    // 5. RTV / DSV の作成
    CreateRenderTargets();
    CreateDepthStencilView(width, height);

    // 6. フェンスとイベント生成
    CreateFenceObjects();

    // FPS固定初期化
    InitializeFixFPS();

    // dxcCompilerを初期化
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    assert(SUCCEEDED(hr));

    // 現時点でincludeはしないがincludeに対応するための設定を行う
    hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
    assert(SUCCEEDED(hr));

    // P.E.P.P.E.R. GPU 計測の初期化（タイムスタンプクエリヒープ + readback）
    PEPPER_GPU_INIT(device_.Get(), commandQueue_.Get());
}

void DirectXCore::BeginDraw() {
    // 描画準備（クリアなどはここで）
    //HRESULT hr;

    // バックバッファのインデックス取得
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // バリア（Present → RenderTarget）
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffers_[frameIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);

    // RTV/DSV 設定
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex_ * rtvDescriptorSize_;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    // ビューポート・シザー設定（Swapchain への描画用なのでウィンドウ実サイズに追従）
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(swapChainWidth_);
    viewport.Height = static_cast<float>(swapChainHeight_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    commandList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect{};
    scissorRect.right = swapChainWidth_;
    scissorRect.bottom = swapChainHeight_;
    commandList_->RSSetScissorRects(1, &scissorRect);

    // クリア処理
    //float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    //commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    //commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DirectXCore::ClearDepthBuffer()
{
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DirectXCore::EndDraw() {
    HRESULT hr;

    // バリア（RenderTarget → Present）
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffers_[frameIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &barrier);

    // P.E.P.P.E.R. GPU タイムスタンプを readback バッファへ解決（Close 直前）
    PEPPER_GPU_RESOLVE(commandList_.Get());

    // コマンドリストを閉じる
    hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    // 実行
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // 画面に表示
    swapChain_->Present(1, 0);

    // フェンス処理
    ++fenceValue_;
    commandQueue_->Signal(fence_.Get(), fenceValue_);
    if (fence_->GetCompletedValue() < fenceValue_) {
        fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    // P.E.P.P.E.R. GPU 完了後にタイムスタンプを読んで Profiler へ反映
    PEPPER_GPU_READBACK();

    // 次フレーム準備
    hr = commandAllocator_->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
    assert(SUCCEEDED(hr));

    // 60fps固定処理
    UpdateFixFPS();
}

void DirectXCore::Finalize() {
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCore::RestoreSwapchainRenderTarget(ID3D12GraphicsCommandList* commandList)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex_ * rtvDescriptorSize_;

    commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(swapChainWidth_);
    viewport.Height = static_cast<float>(swapChainHeight_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect{};
    scissorRect.right = swapChainWidth_;
    scissorRect.bottom = swapChainHeight_;
    commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCore::WaitForGpu() {
    if (!commandQueue_ || !fence_) return;
    ++fenceValue_;
    commandQueue_->Signal(fence_.Get(), fenceValue_);
    if (fence_->GetCompletedValue() < fenceValue_) {
        fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DirectXCore::Resize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    if (width == swapChainWidth_ && height == swapChainHeight_) return;
    if (!swapChain_) return;

    // 進行中の GPU 作業が backBuffers_ を参照している可能性があるので完了待ち
    WaitForGpu();

    // バックバッファを解放してから ResizeBuffers
    for (UINT i = 0; i < bufferCount_; ++i) {
        backBuffers_[i].Reset();
    }

    HRESULT hr = swapChain_->ResizeBuffers(
        bufferCount_, static_cast<UINT>(width), static_cast<UINT>(height),
        backBufferFormat_, 0);
    assert(SUCCEEDED(hr));

    // RTV を既存ヒープに作り直す
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < bufferCount_; ++i) {
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        assert(SUCCEEDED(hr));
        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc, handle);
        handle.ptr += rtvDescriptorSize_;
    }

    swapChainWidth_ = width;
    swapChainHeight_ = height;

    // 深度バッファ・オフスクリーンRT・ゲーム用ビューポートは触らない。
    // ゲームは固定 1600×900 で描き、Debug ビルドでは ImGui::Image が
    // viewportRenderTexture をウィンドウサイズに合わせて拡縮表示する。
}

//==============================
// 内部ヘルパー
//==============================

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCore::CreateBufferResource(size_t sizeInBytes)
{
    //=========================
    // VertexResourceを生成
    //=========================
    // 頂点リソース用のヒープ設定
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    // 頂点リソースの設定
    D3D12_RESOURCE_DESC vertexResourceDesc{};

    // バッファリソース。テクスチャの場合はまた別の設定をする
    vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexResourceDesc.Width = sizeInBytes;// リソースのサイズ。

    // バッファの場合はこれらを1にする決まり
    vertexResourceDesc.Height = 1;
    vertexResourceDesc.DepthOrArraySize = 1;
    vertexResourceDesc.MipLevels = 1;
    vertexResourceDesc.SampleDesc.Count = 1;

    // バッファの場合の儀式
    vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // 実際に頂点リソースを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;

    HRESULT hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexResource));

    assert(SUCCEEDED(hr));

    return vertexResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCore::CreateUavBufferResource(size_t sizeInBytes)
{
    // UAV用のResourceはGPUに近いDEFAULT heapで作成する
    D3D12_HEAP_PROPERTIES defaultHeapProperties{};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // UAVとして利用するため必須のフラグ
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    Microsoft::WRL::ComPtr<ID3D12Resource> uavResource = nullptr;

    // 初期StateはCOMMONにしておく
    HRESULT hr = device_->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&uavResource));

    assert(SUCCEEDED(hr));

    return uavResource;
}

ComPtr<ID3D12Resource> DirectXCore::CreateDefaultBuffer(
    size_t sizeInBytes,
    const void* initialData,
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES finalState)
{
    assert(sizeInBytes > 0);
    assert(initialData != nullptr);
    assert(commandList != nullptr);

    // DEFAULT heap 本体（初期 State は COPY_DEST）
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // D3D12 仕様上、バッファの InitialState は強制的に COMMON 扱いになる。
    // CopyBufferRegion 実行時に暗黙的に COPY_DEST へプロモートされる。
    ComPtr<ID3D12Resource> defaultBuffer;
    HRESULT hr = device_->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&defaultBuffer));
    assert(SUCCEEDED(hr));

    // UPLOAD 中間バッファ
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> intermediate;
    hr = device_->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&intermediate));
    assert(SUCCEEDED(hr));

    // CPU から中間バッファへコピー
    void* mapped = nullptr;
    hr = intermediate->Map(0, nullptr, &mapped);
    assert(SUCCEEDED(hr));
    std::memcpy(mapped, initialData, sizeInBytes);
    intermediate->Unmap(0, nullptr);

    // 中間 → DEFAULT へ GPU 上でコピー
    commandList->CopyBufferRegion(defaultBuffer.Get(), 0, intermediate.Get(), 0, sizeInBytes);

    // 指定された最終 State へ遷移
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = defaultBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    commandList->ResourceBarrier(1, &barrier);

    // 中間バッファは GPU が使い終わるまで生存させる必要があるのでフェンスペアで保持
    TrackIntermediateResource(intermediate);

    return defaultBuffer;
}

void DirectXCore::TrackIntermediateResource(ComPtr<ID3D12Resource> resource)
{
    intermediateResources_.emplace_back(GetNextFenceValue(), std::move(resource));
}

void DirectXCore::TickIntermediateResources()
{
    if (intermediateResources_.empty()) return;

    const uint64_t completed = GetCompletedFenceValue();
    auto newEnd = std::remove_if(
        intermediateResources_.begin(), intermediateResources_.end(),
        [completed](const auto& entry) { return entry.first <= completed; });
    intermediateResources_.erase(newEnd, intermediateResources_.end());
}

void DirectXCore::EnqueueOnFenceComplete(uint64_t fenceValue, std::function<void()> callback)
{
    pendingCallbacks_.emplace_back(fenceValue, std::move(callback));
}

void DirectXCore::TickPendingCallbacks()
{
    if (pendingCallbacks_.empty()) return;

    const uint64_t completed = GetCompletedFenceValue();

    // 完了した callback を抜き出して別バッファに移してから呼び出す
    // （callback 内で更に EnqueueOnFenceComplete されても安全になるよう、
    //  実行時はキュー本体に触らない）
    std::vector<std::function<void()>> toRun;
    auto newEnd = std::remove_if(
        pendingCallbacks_.begin(), pendingCallbacks_.end(),
        [&](auto& entry) {
            if (entry.first <= completed) {
                toRun.push_back(std::move(entry.second));
                return true;
            }
            return false;
        });
    pendingCallbacks_.erase(newEnd, pendingCallbacks_.end());

    for (auto& cb : toRun) {
        cb();
    }
}

IDxcBlob* DirectXCore::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
    //=========================
   // 1.hlslファイルを読み込む
   //=========================
   // これからシェーダをコンパイルする旨をログに出す
    Log(ConvertString(std::format(L"Begin CompileShader,path:{},profile:{}\n", filePath, profile)));

    // hlslファイルを読み込む
    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);

    // 読めなかったら止める
    assert(SUCCEEDED(hr));

    // 読み込んだファイルの内容を設定する
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;// utf-8の文字コードであることを通知

    //==========================
    // Compileする
    //==========================
    LPCWSTR arguments[] = {
        filePath.c_str(),        // コンパイル対象のhlslファイル名
        L"-E",L"main",           // エントリーポイントの指定。基本的にmain以外にしない
        L"-T",profile,           // ShaderProfileの設定
        L"-Zi",L"-Qembed_debug", // デバッグ用の情報を埋め込む
        L"-Od",                  // 最適化を外しておく
        L"-Zpr",                 // メモリレイアウトは"行"優先
    };

    // 実際にShaderをコンパイル
    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer,        // 読み込んだファイル
        arguments,                  // コンパイルオプション
        _countof(arguments),        // コンパイルオプションの数
        includeHandler_.Get(),       // インクルードが含まれた諸々
        IID_PPV_ARGS(&shaderResult) // コンパイル結果
    );

    // コンパイルエラーではなくdxcが起動できないなど致命的な状況で停止
    assert(SUCCEEDED(hr));

    //=============================
    // 警告、エラーが出ていないか確認
    //=============================
    // 警告、エラーが出たらログに出力し停止
    IDxcBlobUtf8* shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {

        // ログを出力
        Log(shaderError->GetStringPointer());

        // 停止
        assert(false);// ここで停止----------------------------------------------------------------------
    }

    //==============================
    // Compile結果を受け取って返す
    //==============================
    // コンパイル結果から実行用のバイナリ部分を取得
    IDxcBlob* shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));

    // 成功したログを出力
    Log(ConvertString(std::format(L"Compile Succeeded, path{},profile:{}\n", filePath, profile)));

    // 使用済みリソースの解放
    shaderSource->Release();
    shaderResult->Release();

    // 実行用のバイナリを返却
    return shaderBlob;

}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCore::CreateDescriptorHeap(
    Microsoft::WRL::ComPtr<ID3D12Device> device,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptor,
    bool shaderVicible)
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptor;
    descriptorHeapDesc.Flags = shaderVicible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));

    return descriptorHeap;
}

void DirectXCore::CreateFactory() {
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&factory_));
    assert(SUCCEEDED(hr));
}

void DirectXCore::CreateDevice() {
#ifdef _DEBUG
    // デバッグレイヤー有効化
    ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    ComPtr<IDXGIAdapter4> useAdapter = nullptr;

    // 良い順にアダプタを頼む
    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
        DXGI_ERROR_NOT_FOUND; ++i) {

        // アダプタの情報を取得
        DXGI_ADAPTER_DESC3 adapterDesc{};
        HRESULT hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));// 取得できないのは一大事

        // ソフトウェアアダプタ出なければ採用!
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

            // 採用したアダプタの情報をログに出力<-wstring
            //Log(convertStringClass->ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));

            break;
        }

        // ソフトウェアアダプタの場合は見なかったことにする
        useAdapter = nullptr;
    }
    assert(useAdapter != nullptr);

    // デバイス生成
    HRESULT hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_));
    assert(SUCCEEDED(hr));
}

void DirectXCore::CreateCommandObjects() {
    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    // コマンドキューの生成が上手くできなかったので起動できない
    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
    
    // コマンドアロケータの生成が上手くできなかったので起動できない
    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator_.Get(), nullptr,
        IID_PPV_ARGS(&commandList_));

    // コマンドリストの生成が上手くできなかったので起動できない
    assert(SUCCEEDED(hr));
}

void DirectXCore::CreateSwapChain(HWND hwnd, int32_t width, int32_t height) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = backBufferFormat_;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = bufferCount_;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = factory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        hwnd,
        &desc,
        nullptr,
        nullptr,
        &swapChain1
    );
    assert(SUCCEEDED(hr));

    // IDXGISwapChain1 → IDXGISwapChain4 へ昇格してメンバ変数に格納
    hr = swapChain1.As(&swapChain_);
    assert(SUCCEEDED(hr));
}

void DirectXCore::CreateRenderTargets() {
    HRESULT hr;

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // RTVヒープ作成
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    assert(SUCCEEDED(hr));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < 2; ++i) {
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        assert(SUCCEEDED(hr));

        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc, handle);
        handle.ptr += rtvDescriptorSize_;
    }
}

void DirectXCore::CreateDepthStencilView(int32_t width, int32_t height) {
    // DSVヒープ作成
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HRESULT hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));

    // 深度バッファ（SRV対応のためTYPELESSで作成）
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS; // DSVもSRVも作れるようにTYPELESS
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProp{};
    heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = device_->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue, IID_PPV_ARGS(&depthStencilBuffer_));
    assert(SUCCEEDED(hr));


    // DSVのサイズを取得
    UINT dsvDescriptorSize = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // DSV Desc設定
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    // 通常用DSV（index 0）
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateDepthStencilView(depthStencilBuffer_.Get(), &dsvDesc, dsvHandle);

    // READ_ONLY_DEPTH用DSV（index 1）
    dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    dsvHandle.ptr += dsvDescriptorSize;
    device_->CreateDepthStencilView(depthStencilBuffer_.Get(), &dsvDesc, dsvHandle);

   /* device_->CreateDepthStencilView(depthStencilBuffer_.Get(), nullptr,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());*/

    // 初期状態
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCore::RegisterDepthSRV(SRVManager* srvManager)
{
    assert(srvManager);
    depthSrvIndex_ = srvManager->Allocate();

    // depthをR24_UNORM_X8_TYPELESSとしてSRV化（深度値のみ読み出す）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device_->CreateShaderResourceView(
        depthStencilBuffer_.Get(),
        &srvDesc,
        srvManager->GetCPUDescriptorHandle(depthSrvIndex_)
    );
}

void DirectXCore::TransitionDepthState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES afterState)
{
    if (depthState_ == afterState) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = depthStencilBuffer_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = depthState_;
    barrier.Transition.StateAfter = afterState;
    commandList->ResourceBarrier(1, &barrier);

    depthState_ = afterState;
}

void DirectXCore::CreateFenceObjects() {
    HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent_ != nullptr);

}

void DirectXCore::InitializeFixFPS(){
    reference_ = std::chrono::steady_clock::now();
    lastFrameTime_ = reference_;
}

void DirectXCore::UpdateFixFPS(){
    // リプレイ再生中は実時計で deltaTime_ を上書きしない（OverrideDeltaTime で供給済み）。
    // ただし記録時と同じ体感速度になるよう、1フレームを deltaTime_ 秒に間延びさせる
    // （これをしないとフレーム制限が外れて早送りになる）。
    if (replayMode_) {
        const std::chrono::microseconds target(
            static_cast<long long>(deltaTime_ * 1000000.0f));
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::microseconds elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);
        if (elapsed < target) {
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<long long>((target - elapsed).count() * 0.9)));
            do {
                now = std::chrono::steady_clock::now();
                elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);
            } while (elapsed < target);
        }
        reference_ = now;
        return;
    }

    // 現在時刻を取得
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    // 前フレームからの経過時間を計算
    std::chrono::microseconds elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - lastFrameTime_);

    if (useFixedFrameRate_) {
        // 固定フレームレート（60FPS）
        const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 60.0f));
        const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));

        // 1/60秒経っていない場合は待機
        std::chrono::microseconds check =
            std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

        if (check < kMinCheckTime) {
            std::chrono::microseconds waitTime = kMinCheckTime - check;
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<long long>(waitTime.count() * 0.9)));
        }

        // 時間が経つまでループ
        do {
            now = std::chrono::steady_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);
        } while (elapsed < kMinTime);

        reference_ = now;
        deltaTime_ = 1.0f / 60.0f; // 固定
    } else {
        // 可変フレームレート
        deltaTime_ = elapsed.count() / 1000000.0f;

        // デルタタイムの上限を設定（処理落ち時の暴走防止）
        const float kMaxDeltaTime = 0.1f; // 最大0.1秒（10FPS相当）
        if (deltaTime_ > kMaxDeltaTime) {
            deltaTime_ = kMaxDeltaTime;
        }
    }

    lastFrameTime_ = now;
}