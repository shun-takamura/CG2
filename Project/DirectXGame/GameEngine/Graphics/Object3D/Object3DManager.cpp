#include "Object3DManager.h"

void Object3DManager::Initialize(DirectXCore* dxCore)
{
    dxCore_ = dxCore;
    CreateRootSignature();

    // 全ブレンドモードのPSOを作成
    for (int i = 0; i < kCountOfBlendMode; ++i) {
        CreateGraphicsPipelineState(static_cast<BlendMode>(i));
    }

    // デフォルトはNormalブレンド
    blendMode_ = kBlendModeNormal;
    currentBlendMode_ = static_cast<int>(blendMode_);
    pipelineState_ = pipelineStates_[currentBlendMode_];
}

void Object3DManager::DrawSetting()
{
   
    dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dxCore_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());
    dxCore_->GetCommandList()->SetPipelineState(pipelineState_.Get());
}

void Object3DManager::SetBlendMode(BlendMode blendMode)
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

Object3DManager::~Object3DManager()
{
    // RootSignature / PSO の解放(ComPtr なので自動)
    rootSignature_.Reset();
    pipelineState_.Reset();
    for (auto& p : pipelineStates_) {
        p.Reset();
    }
}

void Object3DManager::CreateRootSignature()
{
    HRESULT hr;

    // DescriptorRange 
    // PS: SRV(t0)
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;                      // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                          // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

    D3D12_ROOT_PARAMETER rootParameters[6] = {};

    // PS: CBV(b0) - マテリアル用
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0

    // VS: CBV(b0) - トランスフォーム用
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
    rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0

    // PS: DescriptorTable(t0) - テクスチャ用
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使用
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

    // PS: CBV(b1) DirectionalLight用
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[3].Descriptor.ShaderRegister = 1;                     // レジスタ番号1を使う

    // rootParameters[4] にカメラ用のCBVを追加
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].Descriptor.ShaderRegister = 2;  // b2

    // PS: CBV(b3) PointLight用
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[5].Descriptor.ShaderRegister = 3;  // b3

    // ============================================
    // Sampler (PS の s0)
    // ============================================
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;         // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 0~1の範囲をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // Mipmapをあるだけ使用
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使用
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用

    D3D12_ROOT_SIGNATURE_DESC rootSignaturDesc{};
    rootSignaturDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignaturDesc.pParameters = rootParameters;
    rootSignaturDesc.NumParameters = _countof(rootParameters);
    rootSignaturDesc.pStaticSamplers = staticSamplers;
    rootSignaturDesc.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリにする
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSignaturDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    // バイナリをもとに生成
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void Object3DManager::CreateGraphicsPipelineState(BlendMode mode)
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

    // 入力レイアウト設定
    D3D12_INPUT_ELEMENT_DESC inputElements[3] = {};
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

    // Rasterizer - 3Dオブジェクトなのでバックフェースカリング
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;  // 裏面をカリング
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

    // BlendStateの設定
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    switch (blendMode_)
    {
    case kBlendModeNone:
        blend.RenderTarget[0].BlendEnable = FALSE;
        break;

    case kBlendModeNormal:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;

    case kBlendModeAdd:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kBlendModeSubtract:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kBlendModeMultily:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        break;

    case kBlendModeScreen:
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;

    case kCountOfBlendMode:
    default:
        break;
    }

    // DepthStencilStateの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込む
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
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
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStates_[blendMode_]));
    assert(SUCCEEDED(hr));
}
