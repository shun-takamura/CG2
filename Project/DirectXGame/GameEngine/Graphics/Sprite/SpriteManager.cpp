#include "SpriteManager.h"
#include "Log.h"
#include "ConvertString.h"
#include <cassert>

IDxcBlob* SpriteManager::CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
    //=========================
    // 1.hlslファイルを読み込む
    //=========================
    // これからシェーダをコンパイルする旨をログに出す
    Log(ConvertString(std::format(L"Begin CompileShader,path:{},profile:{}\n", filePath, profile)));

    // hlslファイルを読み込む
    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

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
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,        // 読み込んだファイル
        arguments,                  // コンパイルオプション
        _countof(arguments),        // コンパイルオプションの数
        includeHandler,             // インクルードが含まれた諸々
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

void SpriteManager::Initialize(
    DirectXCore* dxCore,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler
){
    dxCore_ = dxCore;
    dxcUtils_ = dxcUtils;
    dxcCompiler_ = dxcCompiler;
    includeHandler_ = includeHandler;

    CreateRootSignature();
    CreateAllPipelineStates();
}

void SpriteManager::CreateRootSignature()
{
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_ROOT_PARAMETER params[3] = {};

    // VS: CBV(b0)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[0].Descriptor.ShaderRegister = 0;

    // PS: CBV(b0)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 0;

    // PS: SRV(t0)
    D3D12_DESCRIPTOR_RANGE texRange{};
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.BaseShaderRegister = 0;
    texRange.NumDescriptors = 1;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.pDescriptorRanges = &texRange;
    params[2].DescriptorTable.NumDescriptorRanges = 1;

   
    rootSigDesc.pParameters = params;
    rootSigDesc.NumParameters = _countof(params);

    // ============================================
    // Sampler (PS の s0)
    // ============================================
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.ShaderRegister = 0; // s0

    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.NumStaticSamplers = 1;

    // ============================================
    // Serialize and Create
    // ============================================
    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob,
        &errorBlob
    );

    assert(SUCCEEDED(hr));

    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_)
    );

    assert(SUCCEEDED(hr));
}

void SpriteManager::CreateAllPipelineStates()
{
    for (int i = 0; i < kCountOfBlendMode; i++)
    {
        pipelineStates_[i] = CreateGraphicsPipelineState((BlendMode)i);
    }

    // デフォルトの PSO を有効化するため
    pipelineState_ = pipelineStates_[blendMode_];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> SpriteManager::CreateGraphicsPipelineState(BlendMode mode)
{
    //CreateRootSignature();

    // ===== シェーダーコンパイル =====
    IDxcBlob* vs = CompileShader(
        L"Resources/Shaders/Sprite.VS.hlsl",
        L"vs_6_0",
        dxcUtils_,
        dxcCompiler_,
        includeHandler_
    );

    IDxcBlob* ps = CompileShader(
        L"Resources/Shaders/Sprite.PS.hlsl",
        L"ps_6_0",
        dxcUtils_,
        dxcCompiler_,
        includeHandler_
    );

    D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    // Blend、Rasterizer、PSO 設定
    D3D12_RASTERIZER_DESC raster{};
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode)
    {
    case kBlendModeNone:
        blend.RenderTarget[0].BlendEnable = FALSE;
        break;

    case kBlendModeNormal:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;

    case kBlendModeAdd:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;

    case kBlendModeSubtract:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        break;

    case kBlendModeMultily:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;

    case kBlendModeScreen:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;
    }

    // アルファブレンド（共通）
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // ---- PSO 設定 ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.InputLayout = inputLayout;
    desc.BlendState = blend;
    desc.RasterizerState = raster;
    desc.DepthStencilState = D3D12_DEPTH_STENCIL_DESC{};
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
    assert(SUCCEEDED(hr));

    return pso;
}

void SpriteManager::CommonDrawSetting()
{
    auto* cmd = dxCore_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
