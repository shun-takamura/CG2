#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "Log.h"
#include <cassert>

// ComputeShaderでSkinningを行うためのRootSignature/PipelineStateを管理する
class SkinningComputeManager
{
private:
    DirectXCore* dxCore_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    /// <summary>ルートシグネチャを作る（[0]=t0 パレット / [1]=t1 入力頂点 / [2]=t2 影響度 / [3]=u0 出力頂点（テーブル）、[4]=b0 SkinningInformation（CBV））</summary>
    void CreateRootSignature();
    /// <summary>スキニング CS の PSO を作る</summary>
    void CreateComputePipelineState();

public:
    // ルートパラメータの番号（CreateRootSignature とスキニングの Dispatch で同じ番号を使う）
    enum RootParam : UINT {
        kRootPalette = 0,       // t0：MatrixPalette（テーブル）
        kRootInputVertex = 1,   // t1：入力頂点（テーブル）
        kRootInfluence = 2,     // t2：影響度（テーブル）
        kRootOutputVertex = 3,  // u0：出力頂点（テーブル）
        kRootSkinningInfo = 4,  // b0：SkinningInformation
        kRootCount = 5,
    };
    /// <summary>ルートシグネチャと PSO を作る</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void Initialize(DirectXCore* dxCore);
    /// <summary>ルートシグネチャと PSO を解放する</summary>
    ~SkinningComputeManager();

    /// <summary>ルートシグネチャと PSO を解放する</summary>
    void Release() {
        rootSignature_.Reset();
        pipelineState_.Reset();
    }

    /// <summary>CS実行のためのCommandList設定（コンピュートのルートシグネチャと PSO を貼る）</summary>
    void DispatchSetting();

    /// <summary>ルートシグネチャを返す</summary>
    /// <returns>ルートシグネチャ</returns>
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    /// <summary>PSO を返す</summary>
    /// <returns>PSO</returns>
    ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }

    /// <summary>DirectXCore を返す</summary>
    /// <returns>Initialize で受け取った DirectXCore</returns>
    DirectXCore* GetDxCore() const { return dxCore_; }
};
