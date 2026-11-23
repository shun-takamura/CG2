#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <array>

class SRVManager;

/// <summary>
/// プリミティブ（板・箱・球など）を描くための共通のルートシグネチャと PSO を作って持つ。
/// シングルトンにする理由：プリミティブ共通のルートシグネチャと PSO を 1 組だけ作って使い回すため。
/// </summary>
class PrimitivePipeline {
public:
    // ブレンドモード（ParticleManagerと同じ体系）
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultiply,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    // 共通ルートシグネチャのルートパラメータの番号（CreateRootSignature と PrimitiveMesh の描画で同じ番号を使う）
    enum RootParam : UINT {
        kRootTransform = 0,     // VS b0：TransformationMatrix
        kRootMaterial = 1,      // PS b0：Material
        kRootTexture = 2,       // PS t0：テクスチャ（テーブル）
        kRootDissolveMask = 3,  // PS t1：ディゾルブマスク（テーブル）
        kRootCount = 4,
    };
    // ID Pass のルートシグネチャのルートパラメータの番号
    enum IdRootParam : UINT {
        kIdRootTransform = 0,  // VS b0：TransformationMatrix
        kIdRootObjectId = 1,   // PS b0：objectId のルート定数
        kIdRootCount = 2,
    };

    /// <summary>シングルトンを返す</summary>
    /// <returns>唯一のインスタンス</returns>
    static PrimitivePipeline* GetInstance();

    /// <summary>共通ルートシグネチャ、BlendMode×深度書き込み×背面カリングの全 PSO、ID・歪み・Bloom パスの PSO を作る</summary>
    /// <param name="dxCore">デバイスとコマンドリストの取得元</param>
    /// <param name="srvManager">PrimitiveMesh がテクスチャをバインドするのに使う</param>
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
    /// <summary>PrimitiveMesh の共有ジオメトリとすべての PSO・ルートシグネチャを解放する</summary>
    void Finalize();

    /// <summary>DirectXCore を返す</summary>
    /// <returns>Initialize で受け取った DirectXCore</returns>
    DirectXCore* GetDxCore() const { return dxCore_; }
    /// <summary>SRVManager を返す</summary>
    /// <returns>Initialize で受け取った SRVManager</returns>
    SRVManager* GetSRVManager() const { return srvManager_; }

    /// <summary>描画前の共通設定（RootSig / PSO / Topology をセット）</summary>
    /// <param name="blendMode">ブレンドモード</param>
    /// <param name="depthWrite">深度を書くなら true</param>
    /// <param name="cullBackface">背面カリングするなら true</param>
    void PreDraw(BlendMode blendMode, bool depthWrite = false, bool cullBackface = false);

    /// <summary>
    /// 共通ルートシグネチャを返す。[0]=VS b0 TransformationMatrix / [1]=PS b0 Material（ルート CBV）、
    /// [2]=PS t0 テクスチャ / [3]=PS t1 ディゾルブマスク（テーブル、常に何かをバインドする）、s0〜s2=サンプラー 3 種
    /// </summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    /// <summary>ID Pass のルートシグネチャを返す（[0]=VS b0 TransformationMatrix、[1]=PS b0 objectId のルート定数）</summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetIdRootSignature() const { return idRootSignature_.Get(); }
    /// <summary>ID Pass の PSO を返す（R8_UINT の RT へ書く）</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetIdPipelineState() const { return idPipelineState_.Get(); }

    /// <summary>Distortion Pass の PSO を返す（メインの rootSignature_ を流用 / PSO のみ専用、R8G8B8A8_UNORM の RT）</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetDistortionPipelineState() const { return distortionPipelineState_.Get(); }

    /// <summary>Bloom Pass の PSO を返す（メインの rootSignature_ / Primitive シェーダを流用、R16G16B16A16_FLOAT の発光 RT へ加算）</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetBloomPipelineState() const { return bloomPipelineState_.Get(); }

private:
    PrimitivePipeline() = default;
    ~PrimitivePipeline() = default;
    PrimitivePipeline(const PrimitivePipeline&) = delete;
    PrimitivePipeline& operator=(const PrimitivePipeline&) = delete;

    /// <summary>共通ルートシグネチャを作る（割り当ては GetRootSignature を参照）</summary>
    void CreateRootSignature();
    /// <summary>通常描画の PSO を 1 本作って pipelineStates_ に入れる（sRGB の RT）</summary>
    /// <param name="mode">ブレンドモード</param>
    /// <param name="depthWrite">深度を書くなら true</param>
    /// <param name="cullBackface">背面カリングするなら true</param>
    void CreateGraphicsPipelineState(BlendMode mode, bool depthWrite, bool cullBackface);
    /// <summary>ID Pass のルートシグネチャと PSO を作る</summary>
    void CreateIdPassObjects();
    /// <summary>Distortion Pass の PSO を作る（DistortionMesh.PS、深度は読むだけ）</summary>
    void CreateDistortionPassObjects();
    /// <summary>Bloom Pass の PSO を作る（Primitive シェーダで発光 RT へ加算）</summary>
    void CreateBloomPassObjects();

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    // pipelineStates_[BlendMode][DepthWrite(0/1)][CullBackface(0/1)]
    std::array<std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 2>, 2>, kCountOfBlendMode> pipelineStates_;

    // ID Pass 用
    Microsoft::WRL::ComPtr<ID3D12RootSignature> idRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> idPipelineState_;

    // Distortion Pass 用（DistortionRT に歪みマップを書き込む）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distortionPipelineState_;

    // Bloom Pass 用（発光 RT に加算で書き込む）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPipelineState_;
};