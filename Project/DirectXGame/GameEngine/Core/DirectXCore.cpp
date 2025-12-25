#include "DirectXCore.h"
#include <cassert>
#include <dxgidebug.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

const uint32_t DirectXCore::kMaxTextureCount = 512;

void DirectXCore::Initialize(WindowsApplication* winApp) {

    assert(winApp != nullptr);
    winApp_ = winApp;

    // システムタイマーの分解能を上げる
    timeBeginPeriod(1);

    // ウィンドウサイズ取得
    int32_t width = WindowsApplication::kClientWidth;
    int32_t height = WindowsApplication::kClientHeight;
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

    srvDescriptorHeap_ = CreateDescriptorHeap(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxTextureCount, true);

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

    commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

    // クリア処理
    float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
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
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HRESULT hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));

    // 深度バッファ
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//DXGI_FORMAT_D32_FLOAT
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

    device_->CreateDepthStencilView(depthStencilBuffer_.Get(), nullptr,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());

}

void DirectXCore::CreateFenceObjects() {
    HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent_ != nullptr);

}

void DirectXCore::InitializeFixFPS(){
    reference_ = std::chrono::steady_clock::now();
}

void DirectXCore::UpdateFixFPS(){
    using namespace std::chrono;

    // 現在時刻
    steady_clock::time_point now = steady_clock::now();

    // 前回からの経過時間（マイクロ秒）
    microseconds elapsed =
        duration_cast<microseconds>(now - reference_);

    // 1/60秒
    const microseconds kMinTime(uint64_t(1000000.0f / 60.0f));

    // 1/60秒よりちょい短め（半端Hz対策）
    const microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

    // ここに “kMinCheckTime を使うバージョン” を入れる
    if (elapsed < kMinCheckTime) {
        // 1マイクロ秒スリープを繰り返す
        while ((steady_clock::now() - reference_) < kMinTime) {
            std::this_thread::sleep_for(microseconds(1));
        }
    }

    // 時刻更新
    reference_ = steady_clock::now();
}