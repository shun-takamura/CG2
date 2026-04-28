#pragma once
#include "DirectXCore.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>

class Camera;

class LineRenderer {
public:
    static LineRenderer* GetInstance();

    void Initialize(DirectXCore* dxCore);
    void Finalize();

    // 描画前に呼ぶ
    void SetCamera(Camera* camera) { camera_ = camera; }

    // 線を1本追加する（毎フレーム呼ぶ→Draw→自動クリア）
    void AddLine(const Vector3& start, const Vector3& end, const Vector4& color);

    // たまった線をまとめて描画してクリア
    void Draw();

private:
    LineRenderer() = default;
    ~LineRenderer() = default;
    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;

    void CreateRootSignature();
    void CreatePipelineState();
    void CreateVertexResource();
    void CreateViewProjectionResource();

    static const uint32_t kMaxLineCount = 4096;
    static const uint32_t kMaxVertexCount = kMaxLineCount * 2;

    struct LineVertex {
        Vector3 position;
        Vector4 color;
    };

    DirectXCore* dxCore_ = nullptr;
    Camera* camera_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionResource_;

    LineVertex* vertexData_ = nullptr;
    Matrix4x4* viewProjectionData_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    uint32_t lineCount_ = 0;
};