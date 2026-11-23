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
class CloudLayer;

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

    // 立方体の頂点数とインデックス数（6 面 × 2 枚 × 3 頂点）
    static constexpr int kVertexCount = 8;
    static constexpr int kIndexCount = 36;
    // 頂点データ（CPU側）- 8頂点、36インデックス
    VertexData_Skybox vertices_[kVertexCount] = {};
    uint32_t indices_[kIndexCount] = {};

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    // 雲を使わないときに b7 へ挿す enabled=0 の CB
    Microsoft::WRL::ComPtr<ID3D12Resource> disabledCloudResource_;

    // 遠景の雲（null なら雲なし）。所有はシーン側
    const CloudLayer* cloudLayer_ = nullptr;

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

    /// <summary>頂点データを生成（-1〜1の箱の 8 頂点と、内側から見る向きの 36 インデックス）</summary>
    void CreateVertexData();

    /// <summary>頂点バッファを作り、8 頂点を書き込んでビューを設定する</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void CreateVertexBuffer(DirectXCore* dxCore);

    /// <summary>インデックスバッファ（R32_UINT）を作り、36 インデックスを書き込んでビューを設定する</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void CreateIndexBuffer(DirectXCore* dxCore);

    /// <summary>座標変換行列（WVP）の定数バッファを作ってマップし、単位行列を入れる</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void CreateTransformationMatrixResource(DirectXCore* dxCore);

    /// <summary>マテリアル（着色・ブレンド係数）の定数バッファを作ってマップし、白・ブレンドなしを入れる</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
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
    /// <summary>追従と WVP の計算に使うカメラを設定する</summary>
    /// <param name="camera">カメラ（nullptr ならワールド行列のまま描く）</param>
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>遠景の雲を空に重ねる（null で外す）。水面にも同じ CloudLayer を渡すと映り込みと一致する</summary>
    /// <param name="cloudLayer">雲のレイヤー（所有はシーン側）。nullptr で雲なし</param>
    void SetCloudLayer(const CloudLayer* cloudLayer) { cloudLayer_ = cloudLayer; }

    /// <summary>全体着色を即座に設定（補間なし。進行中の着色フェードは打ち切る）</summary>
    /// <param name="color">テクスチャに乗算する色（RGBA）</param>
    void SetColor(const Vector4& color);

    /// <summary>Cubemapを即座に差し替える（補間なし。進行中のクロスフェードは打ち切る）</summary>
    /// <param name="cubemapFilePath">Cubemap テクスチャのパス（未ロードならロードする）</param>
    void SetCubemap(const std::string& cubemapFilePath);

    //==============================
    // 演出用API
    //==============================

    /// <summary>指定Cubemapへ時間をかけてクロスフェードする（遷移先を t1 のスロットに置き、完了で t0 に確定する）</summary>
    /// <param name="cubemapFilePath">遷移先のCubemap</param>
    /// <param name="durationSec">遷移にかける秒数（0 以下なら即座に差し替え）</param>
    void BlendTo(const std::string& cubemapFilePath, float durationSec);

    /// <summary>着色を時間をかけて目標色へ補間する（暗転などに使う）</summary>
    /// <param name="targetColor">目標の着色</param>
    /// <param name="durationSec">補間にかける秒数（0 以下なら即座に設定）</param>
    void FadeColor(const Vector4& targetColor, float durationSec);

    //==============================
    // ゲッター
    //==============================
    /// <summary>今のスロット（t0）の Cubemap のパスを返す</summary>
    /// <returns>Cubemap テクスチャのパス</returns>
    const std::string& GetCubemapFilePath() const { return cubemapFilePath_; }
    /// <summary>クロスフェード中かどうかを返す</summary>
    /// <returns>クロスフェード中なら true</returns>
    bool IsBlending() const { return isBlending_; }

    //==============================
    // 初期化・更新・描画
    //==============================

    /// <summary>
    /// Cubemap と雲の代わりのテクスチャをロードし、箱の頂点・インデックス・WVP・マテリアルの各バッファと
    /// 雲なし用の定数バッファを作る。カメラは SkyboxManager の既定カメラを使う
    /// </summary>
    /// <param name="skyboxManager">共通のルートシグネチャ・PSO を持つ SkyboxManager</param>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    /// <param name="cubemapFilePath">最初に表示する Cubemap テクスチャのパス</param>
    void Initialize(SkyboxManager* skyboxManager, DirectXCore* dxCore, const std::string& cubemapFilePath);

    /// <summary>
    /// 更新（毎フレームWVP行列を計算＋クロスフェード/着色補間を進める）
    /// </summary>
    /// <param name="deltaTime">経過時間（秒）。0なら補間は進まない</param>
    void Update(float deltaTime = 0.0f);

    /// <summary>
    /// 箱を描く。SkyboxManager のルートシグネチャで [0]=WVP(VS b0)、[1]=マテリアル(PS b0)、
    /// [2]=今の Cubemap(t0)、[3]=次の Cubemap(t1)、[4]=雲の CB(b7)、[5]=雲のノイズ(t5)、[6]=雲のもこもこ(t9) をバインドする。
    /// 雲なしでも [4]〜[6] は無効の CB と代わりのテクスチャで埋める。先に SkyboxManager::DrawSetting を呼んでおくこと
    /// </summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void Draw(DirectXCore* dxCore);
};