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
    // ルートパラメータの番号（Initialize と BeginPass で同じ番号を使う）
    enum RootParam : UINT {
        kRootTransform = 0,        // VS b0：TransformationMatrix
        kRootCascadeViewProj = 1,  // VS b1：カスケードの ViewProj（ルート定数）
        kRootCount = 2,
    };
    // カスケードの ViewProj のルート定数の数（float4x4）
    static constexpr UINT kCascadeViewProjConstantCount = 16;
    // 受光パスの b5 に渡す CSM 情報。シェーダー側 ShadowCB と構造を一致させること。
    struct ShadowConstants {
        Matrix4x4 cascadeViewProj[kShadowCascadeCount]; // カスケードごとのライト ViewProj
        Vector4   cascadeSplitsView;                    // 各カスケード遠端のビュー空間Z（x,y,z使用）
        float     shadowBias;                           // 深度比較バイアス
        float     normalOffset;                         // 法線オフセットバイアス（ワールド単位）
        float     enabled;                              // 1=影あり, 0=影なし（受光側で常に1.0を返す）
        float     softness;                             // 深度差→ボケ幅の係数（接地で硬く、離れるほど柔らかく）
        float     debug;                                // 1=影係数をグレースケール出力（デバッグ）
        float     maxBlur;                              // ボケ半径の上限（UV）。washout防止
        float     fadeRadius;                           // penumbra がこの値で影が消える（距離フェード）。0で無効
        float     pad_;
    };

    /// <summary>深度の Texture2DArray（DSV ×カスケード数・SRV）・受光用 CB（b5）・シャドウ専用のルートシグネチャと PSO を作る</summary>
    /// <param name="dxCore">デバイスの取得元（nullptr は assert）</param>
    /// <param name="srvManager">シャドウマップの SRV を確保する先（nullptr は assert）</param>
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

    /// <summary>
    /// カメラ視錐台と平行光源の向きから各カスケードのライト行列を再計算し、
    /// 受光パス用 CB（b5）へ書き込む。毎フレーム呼ぶ。
    /// </summary>
    /// <param name="camera">分割の基準にする描画カメラ（ニア・ファー）</param>
    /// <param name="lightDirection">光の進む向き（正規化しなくてよい）</param>
    void UpdateCascades(const Camera& camera, const Vector3& lightDirection);

    // ===== シャドウパス用 =====

    /// <summary>シャドウパス開始：DEPTH_WRITE へ遷移し、PSO/RootSig/ビューポートを設定する</summary>
    /// <param name="commandList">積む先のコマンドリスト</param>
    void BeginPass(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// 描画するカスケードを選ぶ：該当スライスを DSV にバインド・クリアし、
    /// ライト ViewProj をルート定数（[1]=VS b1）へ積む。各オブジェクトはこの後 transform を b0（ルート [0]）に積んで描画する。
    /// </summary>
    /// <param name="commandList">積む先のコマンドリスト</param>
    /// <param name="cascadeIndex">カスケードの番号（0〜kShadowCascadeCount-1）</param>
    void BindCascade(ID3D12GraphicsCommandList* commandList, uint32_t cascadeIndex);

    /// <summary>シャドウパス終了：PIXEL_SHADER_RESOURCE へ遷移する</summary>
    /// <param name="commandList">積む先のコマンドリスト</param>
    void EndPass(ID3D12GraphicsCommandList* commandList);

    // ===== 受光パス用ゲッター =====

    /// <summary>t3 にバインドするシャドウマップ SRV（Texture2DArray）の GPU ハンドルを返す</summary>
    /// <returns>SRV のハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const;

    /// <summary>b5 にバインドする ShadowConstants の GPU アドレスを返す</summary>
    /// <returns>CB のアドレス</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetConstantsGpuAddress() const {
        return constantBuffer_->GetGPUVirtualAddress();
    }

    /// <summary>影が有効か（無効時はシャドウパスを省略し、受光側は常に「照らされる」を返す）</summary>
    /// <returns>有効なら true</returns>
    bool IsEnabled() const { return enabled_; }
    /// <summary>影の有効/無効を切り替える</summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// <summary>Lightパネルに差し込むシャドウ調整 UI</summary>
    void OnImGui();

    // ===== 調整用 =====
    /// <summary>受光時の深度比較バイアスを設定する</summary>
    /// <param name="bias">バイアス（深度の単位）</param>
    void  SetBias(float bias) { bias_ = bias; }
    /// <summary>受光時の深度比較バイアスを返す</summary>
    /// <returns>バイアス</returns>
    float GetBias() const { return bias_; }
    /// <summary>法線オフセットバイアスを設定する</summary>
    /// <param name="offset">受光点を法線方向にずらす量 [m]</param>
    void  SetNormalOffset(float offset) { normalOffset_ = offset; }
    /// <summary>法線オフセットバイアスを返す</summary>
    /// <returns>ずらす量 [m]</returns>
    float GetNormalOffset() const { return normalOffset_; }
    /// <summary>practical split のブレンド係数を設定する</summary>
    /// <param name="lambda">0=均等, 1=対数</param>
    void  SetCascadeLambda(float lambda) { cascadeLambda_ = lambda; }
    /// <summary>practical split のブレンド係数を返す</summary>
    /// <returns>0=均等, 1=対数</returns>
    float GetCascadeLambda() const { return cascadeLambda_; }

private:
    /// <summary>深度 Texture2DArray（R32_TYPELESS）・カスケードごとの DSV（D32_FLOAT）・SRV（R32_FLOAT）・受光用 CB を作る</summary>
    void CreateResources();
    /// <summary>シャドウ描画の VS だけの PSO とルートシグネチャ（[0]=VS b0 TransformationMatrix / [1]=VS b1 ViewProj のルート定数 16 個）を作る</summary>
    void CreatePipeline();

    /// <summary>depthArray_ の状態を afterState へ遷移（同一なら何もしない）</summary>
    /// <param name="commandList">積む先のコマンドリスト</param>
    /// <param name="afterState">遷移後の状態</param>
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

    // 距離で変化するボケの強さ（深度差→ボケ幅の係数）。大きいほど離れた影が柔らかい。
    float softness_ = 0.15f;

    // ボケ半径の上限（UV）。距離で柔らかくなる影もこの値で頭打ち。Vogel化で大きめでも崩れにくい。
    float maxBlur_ = 0.04f;

    // 距離フェード：penumbra がこの値に達すると影が完全に消える。0でフェード無効。
    float fadeRadius_ = 0.1f;

    // デバッグ：影係数をそのままグレースケール表示する
    bool debugShadowOnly_ = false;
};
