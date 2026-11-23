#pragma once
#include "DirectXCore.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>

class Camera;

/// <summary>
/// 線分をまとめて積み、1 回の描画で描く。
/// シングルトンにする理由：線の頂点バッファと PSO を 1 組だけ持ち、デバッグ表示などどこからでも線を積めるようにするため。
/// </summary>
class LineRenderer {
public:
    // 描画対象パス。Scene と EffectEditor で頂点バッファ・CB を分けるため。
    enum class Pass {
        Main,
        Preview,
        Count,
    };

    /// <summary>シングルトンを返す</summary>
    /// <returns>唯一のインスタンス</returns>
    static LineRenderer* GetInstance();

    /// <summary>ルートシグネチャ・PSO と、パスごとの頂点バッファ・ViewProjection CB を作る</summary>
    /// <param name="dxCore">デバイスとコマンドリストの取得元</param>
    void Initialize(DirectXCore* dxCore);
    /// <summary>PSO・ルートシグネチャ・パスごとのリソースを解放する</summary>
    void Finalize();

    /// <summary>パスのカメラを設定する（描画前に呼ぶ）</summary>
    /// <param name="camera">ViewProjection を取るカメラ</param>
    /// <param name="pass">対象のパス</param>
    void SetCamera(Camera* camera, Pass pass = Pass::Main) { passes_[static_cast<int>(pass)].camera = camera; }

    /// <summary>線を1本追加する（毎フレーム呼ぶ→Draw→自動クリア）。上限を超えた分は捨てて 1 回だけ警告する</summary>
    /// <param name="start">始点のワールド座標</param>
    /// <param name="end">終点のワールド座標</param>
    /// <param name="color">線の色</param>
    /// <param name="pass">積むパス</param>
    void AddLine(const Vector3& start, const Vector3& end, const Vector4& color, Pass pass = Pass::Main);

    /// <summary>たまった線をまとめて描画してクリア（カメラ未設定なら描かずにクリア）</summary>
    /// <param name="pass">描くパス</param>
    void Draw(Pass pass = Pass::Main);

private:
    LineRenderer() = default;
    ~LineRenderer() = default;
    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;

    /// <summary>ルートシグネチャを作る（[0]=VS b0 の ViewProjection、ルート CBV）</summary>
    void CreateRootSignature();
    /// <summary>線リストの PSO を作る（α ブレンド・深度テストなし・sRGB の RT）</summary>
    void CreatePipelineState();

    // 1 パス・1 フレームあたりの上限。スプライン（1 区間 24 分割）を数十本出すと 4096 では足りず、
    // 後から積んだデバッグ線（ボロノイのセル境界など）が黙って捨てられていたため引き上げた。
    static const uint32_t kMaxLineCount = 16384;
    static const uint32_t kMaxVertexCount = kMaxLineCount * 2;

    struct LineVertex {
        Vector3 position;
        Vector4 color;
    };

    // パスごとに独立した頂点バッファ + ViewProjection CB + カメラ + 蓄積数
    struct PassState {
        bool overflowWarned = false; // 上限超過の警告は 1 回だけ出す
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionResource;
        LineVertex* vertexData = nullptr;
        Matrix4x4*  viewProjectionData = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        Camera*  camera = nullptr;
        uint32_t lineCount = 0;
    };

    /// <summary>1 パス分の頂点バッファ（kMaxVertexCount 個）と ViewProjection CB を作ってマップする</summary>
    /// <param name="s">作る先のパス</param>
    void CreatePassResources(PassState& s);

    DirectXCore* dxCore_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    PassState passes_[static_cast<int>(Pass::Count)];
};