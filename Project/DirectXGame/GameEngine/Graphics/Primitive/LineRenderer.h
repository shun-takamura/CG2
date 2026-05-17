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
    // 描画対象パス。Scene と EffectEditor で頂点バッファ・CB を分けるため。
    enum class Pass {
        Main,
        Preview,
        Count,
    };

    static LineRenderer* GetInstance();

    void Initialize(DirectXCore* dxCore);
    void Finalize();

    // 描画前に呼ぶ
    void SetCamera(Camera* camera, Pass pass = Pass::Main) { passes_[static_cast<int>(pass)].camera = camera; }

    // 線を1本追加する（毎フレーム呼ぶ→Draw→自動クリア）
    void AddLine(const Vector3& start, const Vector3& end, const Vector4& color, Pass pass = Pass::Main);

    // たまった線をまとめて描画してクリア
    void Draw(Pass pass = Pass::Main);

private:
    LineRenderer() = default;
    ~LineRenderer() = default;
    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;

    void CreateRootSignature();
    void CreatePipelineState();

    static const uint32_t kMaxLineCount = 4096;
    static const uint32_t kMaxVertexCount = kMaxLineCount * 2;

    struct LineVertex {
        Vector3 position;
        Vector4 color;
    };

    // パスごとに独立した頂点バッファ + ViewProjection CB + カメラ + 蓄積数
    struct PassState {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionResource;
        LineVertex* vertexData = nullptr;
        Matrix4x4*  viewProjectionData = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        Camera*  camera = nullptr;
        uint32_t lineCount = 0;
    };

    void CreatePassResources(PassState& s);

    DirectXCore* dxCore_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    PassState passes_[static_cast<int>(Pass::Count)];
};