#include "LineRenderer.h"
#include "Camera.h"
#include "Log.h"
#include <cassert>
#include <dxcapi.h>

LineRenderer* LineRenderer::GetInstance() {
    static LineRenderer instance;
    return &instance;
}

void LineRenderer::Initialize(DirectXCore* dxCore) {
    dxCore_ = dxCore;
    CreateRootSignature();
    CreatePipelineState();
    CreateVertexResource();
    CreateViewProjectionResource();
}

void LineRenderer::Finalize() {
    pipelineState_.Reset();
    rootSignature_.Reset();
    vertexResource_.Reset();
    viewProjectionResource_.Reset();
}

void LineRenderer::AddLine(const Vector3& start, const Vector3& end, const Vector4& color) {
    if (lineCount_ >= kMaxLineCount) return;
    vertexData_[lineCount_ * 2 + 0].position = start;
    vertexData_[lineCount_ * 2 + 0].color = color;
    vertexData_[lineCount_ * 2 + 1].position = end;
    vertexData_[lineCount_ * 2 + 1].color = color;
    ++lineCount_;
}

void LineRenderer::Draw() {
    if (lineCount_ == 0) return;
    if (!camera_) { lineCount_ = 0; return; }

    // ViewProjectionMatrixを書き込む
    *viewProjectionData_ = camera_->GetViewProjectionMatrix();

    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, viewProjectionResource_->GetGPUVirtualAddress());

    commandList->DrawInstanced(lineCount_ * 2, 1, 0, 0);

    // バッファクリア
    lineCount_ = 0;
}

void LineRenderer::CreateRootSignature() {
    // [0] VS: CBV(b0) - ViewProjectionMatrix
    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCore_->GetDevice()->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void LineRenderer::CreatePipelineState() {
    IDxcBlob* vs = dxCore_->CompileShader(L"Resources/Shaders/Primitive/Line.VS.hlsl", L"vs_6_0");
    IDxcBlob* ps = dxCore_->CompileShader(L"Resources/Shaders/Primitive/Line.PS.hlsl", L"ps_6_0");

    // InputLayout: Position(float3) + Color(float4)
    D3D12_INPUT_ELEMENT_DESC inputElements[2] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElements[1].SemanticName = "COLOR";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // 深度テストはする・書き込みはしない（既存メッシュに埋もれない見せ方）
    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature_.Get();
    pipelineDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pipelineDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pipelineDesc.InputLayout = inputLayout;
    pipelineDesc.BlendState = blend;
    pipelineDesc.RasterizerState = rasterizer;
    pipelineDesc.DepthStencilState = depthStencil;
    pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pipelineDesc.SampleDesc.Count = 1;

    HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(
        &pipelineDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}

void LineRenderer::CreateVertexResource() {
    size_t sizeInBytes = sizeof(LineVertex) * kMaxVertexCount;
    vertexResource_ = dxCore_->CreateBufferResource(sizeInBytes);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(LineVertex);

    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
}

void LineRenderer::CreateViewProjectionResource() {
    viewProjectionResource_ = dxCore_->CreateBufferResource(sizeof(Matrix4x4));
    viewProjectionResource_->Map(0, nullptr, reinterpret_cast<void**>(&viewProjectionData_));
}