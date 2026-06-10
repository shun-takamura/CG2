#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"

class DirectXCore;
class SRVManager;
class Camera;

// カスケード数。CB の ABI（ShadowCB）とシェーダーのループ段数に必ず一致させること。
static constexpr uint32_t kShadowCascadeCount = 3;

/// <summary>
/// 平行光源1個のカスケードシャドウマップ（CSM）。
/// 深度 Texture2DArray（スライス=カスケード）へライト視点で描画し、
/// 受光パスで t3 として読む。座標系は LH。
/// 描画順は「シャドウパス → 通常パス」。
/// </summary>
class ShadowMap {
public:
    // 受光パスの b5 に渡す CSM 情報。シェーダー側 ShadowCB と構造を一致させること。
    struct ShadowConstants {
        Matrix4x4 cascadeViewProj[kShadowCascadeCount]; // カスケードごとのライト ViewProj
        Vector4   cascadeSplitsView;                    // 各カスケード遠端のビュー空間Z（x,y,z使用）
        float     shadowBias;                           // 深度比較バイアス
        float     normalOffset;                         // 法線オフセットバイアス（ワールド単位）
        float     enabled;                              // 1=影あり, 0=影なし（受光側で常に1.0を返す）
        float     lightSize;                            // PCSS ソフトネス（仮想太陽の角サイズ。0で硬い影）
    };

    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

    /// <summary>
    /// カメラ視錐台と平行光源の向きから各カスケードのライト行列を再計算し、
    /// 受光パス用 CB（b5）へ書き込む。毎フレーム呼ぶ。
    /// </summary>
    void UpdateCascades(const Camera& camera, const Vector3& lightDirection);

    // ===== シャドウパス用 =====

    // シャドウパス開始：DEPTH_WRITE へ遷移し、PSO/RootSig/ビューポートを設定する。
    void BeginPass(ID3D12GraphicsCommandList* commandList);

    // 描画するカスケードを選ぶ：該当スライスを DSV にバインド・クリアし、
    // ライト ViewProj をルート定数（b1）へ積む。各オブジェクトはこの後 transform を b0 に積んで描画する。
    void BindCascade(ID3D12GraphicsCommandList* commandList, uint32_t cascadeIndex);

    // シャドウパス終了：PIXEL_SHADER_RESOURCE へ遷移する。
    void EndPass(ID3D12GraphicsCommandList* commandList);

    // ===== 受光パス用ゲッター =====

    // t3 にバインドするシャドウマップ SRV（Texture2DArray）の GPU ハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const;

    // b5 にバインドする ShadowConstants の GPU アドレス
    D3D12_GPU_VIRTUAL_ADDRESS GetConstantsGpuAddress() const {
        return constantBuffer_->GetGPUVirtualAddress();
    }

    // 影の有効/無効（無効時はシャドウパスを省略し、受光側は常に「照らされる」を返す）
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    // Lightパネルに差し込むシャドウ調整 UI
    void OnImGui();

    // ===== 調整用 =====
    void  SetBias(float bias) { bias_ = bias; }
    float GetBias() const { return bias_; }
    void  SetNormalOffset(float offset) { normalOffset_ = offset; }
    float GetNormalOffset() const { return normalOffset_; }
    void  SetCascadeLambda(float lambda) { cascadeLambda_ = lambda; }
    float GetCascadeLambda() const { return cascadeLambda_; }

private:
    void CreateResources();
    void CreatePipeline();

    // depthArray_ の状態を afterState へ遷移（同一なら何もしない）
    void TransitionState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES afterState);

    DirectXCore* dxCore_ = nullptr;
    SRVManager*  srvManager_ = nullptr;

    static constexpr uint32_t kResolution = 2048;

    // 深度 Texture2DArray（スライス=カスケード）
    Microsoft::WRL::ComPtr<ID3D12Resource>       depthArray_;
    D3D12_RESOURCE_STATES                          currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // カスケード分の DSV を並べた専用ヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    uint32_t dsvDescriptorSize_ = 0;

    // SRVManager 上に確保したシャドウマップ SRV スロット
    uint32_t srvIndex_ = 0;

    // 受光パス用 CB（b5）
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    ShadowConstants* mappedConstants_ = nullptr;

    // シャドウ描画専用 PSO / RootSignature（VS のみ）
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // BindCascade でルート定数(b1)へ積む各カスケードの ViewProj（CPU 保持）
    Matrix4x4 cascadeViewProj_[kShadowCascadeCount]{};

    // 深度バイアス（受光時の比較バイアス。ラスタライザ側の斜面バイアスとは別物）
    float bias_ = 0.0015f;

    // 法線オフセットバイアス（受光点を法線方向にずらす量。ワールド単位）。
    // 真上ライト×平坦地面など斜面バイアスが効かないケースのアクネ対策。
    float normalOffset_ = 0.05f;

    // practical split のブレンド係数（0=均等, 1=対数）
    float cascadeLambda_ = 0.5f;

    // 影の有効/無効
    bool enabled_ = true;

    // PCSS ソフトネス（UV単位の探索半径＝仮想太陽の角サイズ）。0で硬い影、大きいほど柔らかい。
    float lightSize_ = 0.02f;
};
