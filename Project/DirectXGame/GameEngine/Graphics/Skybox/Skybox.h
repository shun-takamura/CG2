#pragma once
#include "SkyboxManager.h"
#include "SkyboxVertexData.h"
#include "SkyboxTransformationMatrix.h"
#include "SkyboxMaterial.h"
#include "Material.h"
#include "Transform.h"
#include "Camera.h"
#include "DirectXCore.h"
#include "MathUtility.h"
#include "TextureManager.h"
#include <string>
#include <wrl.h>
#include <d3d12.h>

class SkyboxManager;

/// <summary>
/// Skybox（天球）を描画するクラス
/// 箱の内側にCubemapテクスチャを貼り付けて遠景を表現する
/// </summary>
class Skybox {
private:
    //==============================
    // メンバ変数
    //==============================

    SkyboxManager* skyboxManager_ = nullptr;
    Camera* camera_ = nullptr;

    // 頂点データ（CPU側）- 8頂点、36インデックス
    VertexData_Skybox vertices_[8] = {};
    uint32_t indices_[36] = {};

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // バッファビュー
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // バッファ内データへのCPU側ポインタ
    SkyboxTransformationMatrix* transformationMatrixData_ = nullptr;
    SkyboxMaterial* materialData_ = nullptr;

    // Transform（Skyboxは基本原点固定だが、一応持つ）
    Transform transform_;

    // Cubemapテクスチャのファイルパス
    std::string cubemapFilePath_;       // 現在スロット(t0)
    std::string nextCubemapFilePath_;   // 次スロット(t1)

    //==============================
    // クロスフェード状態
    //==============================
    bool  isBlending_ = false;
    float blendTime_ = 0.0f;
    float blendDuration_ = 1.0f;

    //==============================
    // 着色フェード状態
    //==============================
    bool    isColorFading_ = false;
    float   colorFadeTime_ = 0.0f;
    float   colorFadeDuration_ = 1.0f;
    Vector4 colorFadeStart_{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 colorFadeTarget_{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 currentColor_{ 1.0f, 1.0f, 1.0f, 1.0f };

    //==============================
    // メンバ関数
    //==============================

    // 頂点データを生成（-1〜1の箱）
    void CreateVertexData();

    // 頂点バッファの作成
    void CreateVertexBuffer(DirectXCore* dxCore);

    // インデックスバッファの作成
    void CreateIndexBuffer(DirectXCore* dxCore);

    // 座標変換行列リソース作成
    void CreateTransformationMatrixResource(DirectXCore* dxCore);

    // マテリアルリソース作成
    void CreateMaterialResource(DirectXCore* dxCore);

public:
    //==============================
    // コンストラクタ・デストラクタ
    //==============================
    Skybox() = default;
    ~Skybox() = default;

    //==============================
    // セッター
    //==============================
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>全体着色を即座に設定（補間なし）</summary>
    void SetColor(const Vector4& color);

    /// <summary>Cubemapを即座に差し替える（補間なし）</summary>
    void SetCubemap(const std::string& cubemapFilePath);

    //==============================
    // 演出用API
    //==============================

    /// <summary>指定Cubemapへ時間をかけてクロスフェードする</summary>
    /// <param name="cubemapFilePath">遷移先のCubemap</param>
    /// <param name="durationSec">遷移にかける秒数</param>
    void BlendTo(const std::string& cubemapFilePath, float durationSec);

    /// <summary>着色を時間をかけて目標色へ補間する（暗転などに使う）</summary>
    /// <param name="targetColor">目標の着色</param>
    /// <param name="durationSec">補間にかける秒数</param>
    void FadeColor(const Vector4& targetColor, float durationSec);

    //==============================
    // ゲッター
    //==============================
    const std::string& GetCubemapFilePath() const { return cubemapFilePath_; }
    bool IsBlending() const { return isBlending_; }

    //==============================
    // 初期化・更新・描画
    //==============================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(SkyboxManager* skyboxManager, DirectXCore* dxCore, const std::string& cubemapFilePath);

    /// <summary>
    /// 更新（毎フレームWVP行列を計算＋クロスフェード/着色補間を進める）
    /// </summary>
    /// <param name="deltaTime">経過時間（秒）。0なら補間は進まない</param>
    void Update(float deltaTime = 0.0f);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(DirectXCore* dxCore);
};