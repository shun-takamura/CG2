#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "Log.h"
#include <cassert>
#include "Camera.h"

/// <summary>
/// Skybox描画の共通設定を管理するクラス
/// RootSignatureとPipelineStateを全Skyboxで共有する
/// </summary>
class SkyboxManager {

    Camera* defaultCamera_ = nullptr;

    DirectXCore* dxCore_ = nullptr;

    // シェーダーが使うデータの設計図
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

    // 描画の全工程の設定
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    /// <summary>
    /// ルートシグネチャの作成。[0]=VS b0（WVP）、[1]=PS b0（マテリアル）、[2]=t0（今の Cubemap）、[3]=t1（次の Cubemap）、
    /// [4]=PS b7（雲の CB）、[5]=t5（雲のノイズ）、[6]=t9（雲のもこもこ）。サンプラは s0=linear clamp、s4=異方性 wrap（雲用）
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// グラフィックパイプラインの生成（Skybox.VS/PS、入力は POSITION のみ、表面カリング（内側から見る）、
    /// 深度は比較のみで書き込まない LESS_EQUAL、RTV は sRGB、DSV は D24S8）
    /// </summary>
    void CreateGraphicsPipelineState();

public:
    // ルートシグネチャのルートパラメータの番号（CreateRootSignature と Skybox::Draw で同じ番号を使う）
    enum RootParam : UINT {
        kRootParamTransform,     // VS b0：座標変換行列
        kRootParamMaterial,      // PS b0：マテリアル
        kRootParamCubemap,       // PS t0：今のスロットの Cubemap（テーブル）
        kRootParamNextCubemap,   // PS t1：次のスロットの Cubemap（テーブル。クロスフェード先）
        kRootParamCloud,         // PS b7：遠景の雲のパラメータ
        kRootParamCloudNoise,    // PS t5：遠景の雲のノイズ（テーブル）
        kRootParamCloudMoko,     // PS t9：遠景の雲のもこもこの高さ（テーブル）
        kRootParamCount,
    };
    /// <summary>ルートシグネチャと PSO を作る</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void Initialize(DirectXCore* dxCore);

    /// <summary>Skybox を描く前の共通設定（三角形リスト・ルートシグネチャ・PSO）をコマンドリストに積む</summary>
    void DrawSetting();

    /// <summary>ルートシグネチャと PSO を解放するデストラクタ</summary>
    ~SkyboxManager();

    /// <summary>ルートシグネチャと PSO を解放する</summary>
    void Release() {
        rootSignature_.Reset();
        pipelineState_.Reset();
    }

    // セッター
    /// <summary>Skybox が Initialize で受け取る既定のカメラを設定する</summary>
    /// <param name="camera">カメラ</param>
    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }

    // ゲッター
    /// <summary>DirectXCore を返す</summary>
    /// <returns>Initialize で受け取った DirectXCore</returns>
    DirectXCore* GetDxCore() const { return dxCore_; }
    /// <summary>既定のカメラを返す</summary>
    /// <returns>既定のカメラ（未設定なら nullptr）</returns>
    Camera* GetDefaultCamera() const { return defaultCamera_; }
};