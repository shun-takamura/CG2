#include "SpriteManager.h"
#include "Log.h"
#include "ConvertString.h"
#include <cassert>
#include "d3dx12.h"

void SpriteManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateDescriptorHeap();
    CreateRootSignature();
    CreateGraphicsPipelineState(kBlendModeNone);
}

void SpriteManager::DrawSetting()
{
    ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap_.Get() };
    dxCore_->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);

    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());
    dxCore_->GetCommandList()->SetPipelineState(pipelineState_.Get());
}

void SpriteManager::SetBlendMode(BlendMode blendMode)
{
    // すでにそのモードなら何もしない
    if (blendMode_ == blendMode) {
        return;
    }

    blendMode_ = blendMode;
    currentBlendMode_ = static_cast<int>(blendMode);

    // PSO を差し替える
    pipelineState_ = pipelineStates_[currentBlendMode_];
}

void SpriteManager::ClearIntermediateResources()
{
    // リスト内のComPtrがスコープを抜けることで自動的に解放される
    intermediateResources_.clear();
}

void SpriteManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange 
    // PS: SRV(t0)
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;                      // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                          // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

    D3D12_ROOT_PARAMETER rootParameters[4] = {};

    // VS: CBV(b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う。PSのb0のb
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0。PSのb0の0

    // PS: CBV(b0)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う。VSのb0のb
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
    rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0。VSのb0の0

    // DescriptorTable
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使用
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[3].Descriptor.ShaderRegister = 1;                     // レジスタ番号1を使う

    // ============================================
    // Sampler (PS の s0)
    // ============================================
    // Sumplerの設定。rootSignatureは532行付近にあると思う(上にコード追加してたら知らん)
    // 基本の設定なので暫くはこれで問題ない
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;         // バイリニアフィルタ？
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 0~1の範囲をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // Mipmapをあるだけ使用
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使用
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用

    D3D12_ROOT_SIGNATURE_DESC rootSignaturDesc{};
    rootSignaturDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignaturDesc.pParameters = rootParameters;  // ルートパラメータ配列へのポイント
    rootSignaturDesc.NumParameters = _countof(rootParameters);   // 配列の長さ
    rootSignaturDesc.pStaticSamplers = staticSamplers;
    rootSignaturDesc.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリにする
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob>  errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSignaturDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferSize()));
        assert(false);
    }

    // バイナリをもとに生成
    //Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    // 作った RootSignature をクラスに保存する
    //rootSignature_ = rootSignature;

}

void SpriteManager::CreateGraphicsPipelineState(BlendMode blendMode)
{

    // ===== シェーダーコンパイル =====
    IDxcBlob* vs = dxCore_->CompileShader(
        L"Resources/Shaders/Object3d.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* ps = dxCore_->CompileShader(
        L"Resources/Shaders/Object3d.PS.hlsl",
        L"ps_6_0"
    );

    D3D12_INPUT_ELEMENT_DESC inputElements[3] ={};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[1].SemanticName = "TEXCOORD";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElements[2].SemanticName = "NORMAL";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    //=================================
    // Rasterizer、Blend、PSO 設定
    //=================================

    // Rasterizer
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;  // 裏面(時計回り)を表示しない
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

    // BlendStateの設定
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;   // α値の設定だから基本使わない
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD; // α値の設定だから基本使わない
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; // α値の設定だから基本使わない

    switch (blendMode_)
    {
    case kBlendModeNone:
        break;

    case kBlendModeNormal:

        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; // 基本ここをいじる
        break;

    case kBlendModeAdd:
        
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる
        break;

    case kBlendModeSubtract:

        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT; // 基本ここをいじる
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる

        break;
    case kBlendModeMultily:

        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;           // 基本ここをいじる
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;     // 基本ここをいじる

        break;
    case kBlendModeScreen:

        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR; // 基本ここをいじる
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる

        break;

    case kCountOfBlendMode:
        break;

    default:
        break;
    }

    // DepthStencilStateの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込む
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される。変更したい場合は3_1の22ページを参照
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // ---- PSO 設定 ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = depthStencilDesc;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;//DXGI_FORMAT_D32_FLOAT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

}

void SpriteManager::CreateDescriptorHeap()
{
    HRESULT hr;

    // ディスクリプタヒープの設定
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // CBV/SRV/UAVを扱う
    // ※ SpriteManagerが扱うテクスチャの最大数を設定してください
    srvHeapDesc.NumDescriptors = 128; // 例として128個分のディスクリプタを確保
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // シェーダーから参照可能にする

    // ヒープの作成
    hr = dxCore_->GetDevice()->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&srvHeap_)); // srvHeap_ (ComPtr<ID3D12DescriptorHeap>) に代入

    assert(SUCCEEDED(hr));
}

DirectX::ScratchImage SpriteManager::LoadTexture(const std::string& filePath)
{
    // テクスチャファイルを読み込んでプログラムで扱えるようにする
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filePath);
    HRESULT hr = DirectX::LoadFromWICFile(
        filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));

    // ミップマップの作成
    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(), image.GetImageCount(), image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB, 0, mipImages);
    assert(SUCCEEDED(hr));

    // ミップマップ付きのデータを返す
    return mipImages;
}

D3D12_GPU_DESCRIPTOR_HANDLE SpriteManager::LoadTextureToGPU(const std::string& filePath)
{
    // --- 1) 画像読み込み ---
    DirectX::ScratchImage mipImages = LoadTexture(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

    // --- 2) GPUリソース作成 ---
    Microsoft::WRL::ComPtr<ID3D12Resource> texture = CreateTextureResource(dxCore_->GetDevice(), metadata);

    // --- 3) データ転送 ---
    UploadTextureData(texture, mipImages, dxCore_->GetDevice(), dxCore_->GetCommandList());

    // --- 4) SRV 作成 ---
    UINT descriptorSize =
        dxCore_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += descriptorSize * nextSrvIndex_;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += descriptorSize * nextSrvIndex_;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = (UINT)metadata.mipLevels;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    dxCore_->GetDevice()->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    // --- 5) TextureInfo を登録 ---
    textures_.push_back({ texture, gpuHandle });

    nextSrvIndex_++;

    return gpuHandle;
}

Microsoft::WRL::ComPtr<ID3D12Resource> SpriteManager::CreateTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, const DirectX::TexMetadata& metadata)
{
    // metadataを基にResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);                             // Textureの横幅
    resourceDesc.Height = UINT(metadata.height);                           // Textureの縦幅
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);                   // mipmapの数
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);            // 奥行or配列Textureの配列数
    resourceDesc.Format = metadata.format;                                 // TextureのFormat
    resourceDesc.SampleDesc.Count = 1;                                     // サンプリングカウント。1固定
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    //resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // 利用するHeapの設定。非常に特殊な運用--------------------------------------------
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;                       // Heap設定をVRAM上に作成

    // Resourceを生成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,                   // Heapの設定
        D3D12_HEAP_FLAG_NONE,              // Heapの特殊な設定。無し
        &resourceDesc,                     // Resourceの設定
        D3D12_RESOURCE_STATE_COPY_DEST,    // データ転送される設定
        nullptr,                           // Clear最適値。使わない
        IID_PPV_ARGS(&resource));          // 作成するResourceポインタへのポインタ
    assert(SUCCEEDED(hr));

    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> SpriteManager::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, Microsoft::WRL::ComPtr<ID3D12Device> device, ID3D12GraphicsCommandList* commandList)
{
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCore_->CreateBufferResource(intermediateSize);
    UpdateSubresources(commandList, texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

    intermediateResources_.push_back(intermediateResource);

    // Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}
