#pragma once
#include "SkyboxManager.h"
#include "SkyboxVertexData.h"
#include "SkyboxTransformationMatrix.h"
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
    Material* materialData_ = nullptr;

    // Transform（Skyboxは基本原点固定だが、一応持つ）
    Transform transform_;

    // Cubemapテクスチャのファイルパス
    std::string cubemapFilePath_;

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
    void SetColor(const Vector4& color) { if (materialData_) materialData_->color = color; }

    //==============================
    // ゲッター
    //==============================
    const std::string& GetCubemapFilePath() const { return cubemapFilePath_; }

    //==============================
    // 初期化・更新・描画
    //==============================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(SkyboxManager* skyboxManager, DirectXCore* dxCore, const std::string& cubemapFilePath);

    /// <summary>
    /// 更新（毎フレームWVP行列を計算）
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(DirectXCore* dxCore);
};