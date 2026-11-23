#pragma once
#include "Object3DManager.h"
#include "VertexData.h"
#include <fstream>
#include <sstream>
#include "Material.h"
#include <cassert>
#include "DirectXCore.h"
#include "MathUtility.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include"PointLight.h"
#include "TextureManager.h"
#include "Transform.h"
#include "ModelCore.h"
#include "ModelInstance.h"
#include "ModelManager.h"
#include "Camera.h"
#include "CameraForGPU.h"

// ImGui対応
#include "IImGuiEditable.h"

class Object3DManager;

class Object3DInstance : public IImGuiEditable {

    //==============================
    // メンバ変数
    //==============================
    std::string name_;  // オブジェクト名（ImGui用に追加）

    Object3DManager* object3DManager_ = nullptr;
    ModelInstance* modelInstance_ = nullptr;
    Camera* camera_ = nullptr;

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;

    // バッファ内データへのCPU側ポインタ
    TransformationMatrix* transformationMatrixData_ = nullptr;
    CameraForGPU* cameraData_ = nullptr;

    // バッファリソースの使い道を補足するバッファビュー
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Transform transform_;
    Transform cameraTransform_;

    // ボーン追従などで完成済みワールド行列を外部から渡すための上書き。
    // hasWorldOverride_ が true の間は transform_ ではなく worldOverride_ を使う。
    bool hasWorldOverride_ = false;
    Matrix4x4 worldOverride_{};

    // テクスチャファイルパス（テクスチャ変更機能用）
    std::string textureFilePath_;
    std::string modelFileName_;
    std::string directoryPath_;  // ロード時の dirPath（シーンJSON保存・再ロード用）

    //==============================
    // メンバ関数
    //==============================

    /// <summary>座標変換行列リソース作成（Object3DManager の VS b0。256 バイト境界、単位行列で初期化）</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateTransformationMatrixResource(DirectXCore* dxCore);

    /// <summary>カメラ CB（Object3DManager の PS b2）を作ってマップする</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateCameraResource(DirectXCore* dxCore);

public:
    //==============================
    // コンストラクタ・デストラクタ
    //==============================
    Object3DInstance() = default;
    ~Object3DInstance() override = default;

    //==============================
    // IImGuiEditable実装
    //==============================
    /// <summary>表示名を返す</summary>
    /// <returns>表示名</returns>
    std::string GetName() const override { return name_; }
    /// <summary>型名を返す</summary>
    /// <returns>"Object3D"</returns>
    std::string GetTypeName() const override { return "Object3D"; }
    /// <summary>Inspector に Transform・マテリアル・テクスチャの編集 UI を描く</summary>
    void OnImGuiInspector() override;
    /// <summary>ギズモで動かす位置を返す</summary>
    /// <returns>translate へのポインタ</returns>
    Vector3* GetEditableTranslate() override { return &transform_.translate; }
    /// <summary>ギズモ表示用の回転を返す</summary>
    /// <returns>rotate へのポインタ</returns>
    const Vector3* GetEditableRotate() const override { return &transform_.rotate; }

    /// <summary>
    /// ID Pass 用：idMaskRT に objectId_ を書き込む。ID 用ルートの [0]=VS b0 変換 / [1]=PS b0 ルート定数
    /// （ディゾルブ中はディゾルブ用 PSO と続きの定数で、消えた部分に ID を書かない）
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void DrawIdPass(class DirectXCore* dxCore);

    /// <summary>
    /// シャドウパス用：transform を b0 にバインドして深度を書き込む
    /// （PSO/RootSig/カスケード行列はパス側で設定済みの前提）
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void DrawShadowPass(class DirectXCore* dxCore);

    /// <summary>
    /// 発光パス用：ブルームの発光 RT に「光る分」（マテリアルの発光＋ディゾルブの発光帯）を書く。
    /// 発光パスのルートの [0]=VS b0 変換 / [2]=PS b1 倍率のルート定数。RTV / DSV は呼び出し側（Game の発光パス）で設定済みの前提
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="bloomScale">メイン描画の発光に掛ける倍率</param>
    /// <param name="includeAll">発光していない submesh も描くなら true（六角形のディゾルブの発光帯を光らせるとき）</param>
    void DrawBloomPass(class DirectXCore* dxCore, float bloomScale, bool includeAll = false);

    /// <summary>
    /// 平面リフレクション用：鏡側が持つ CB（VS b1 = 鏡像 ViewProj / PS b2 = 鏡像カメラ位置）で描く。
    /// 自身の transform CB は World だけ使うので、反射のために CB を増やさない。
    /// （RootSig/ライト/シャドウは Object3DManager::DrawSetting 済みの前提）
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="reflectionViewProjAddress">鏡像の ViewProj とクリップ平面の CB（ルート [12]=VS b1）</param>
    /// <param name="reflectionCameraAddress">鏡像のカメラの CB（ルート [4]=PS b2）</param>
    void DrawReflection(class DirectXCore* dxCore,
        D3D12_GPU_VIRTUAL_ADDRESS reflectionViewProjAddress,
        D3D12_GPU_VIRTUAL_ADDRESS reflectionCameraAddress);

    //==============================
    // セッター
    //==============================
    /// <summary>表示名を設定する</summary>
    /// <param name="name">表示名</param>
    void SetName(const std::string& name) override { name_ = name; }
    /// <summary>描くモデルを差し替える</summary>
    /// <param name="modelInstance">モデル</param>
    void SetModel(ModelInstance* modelInstance) { modelInstance_ = modelInstance; }
    /// <summary>ModelManager から読み込み済みのモデルを探して差し替える</summary>
    /// <param name="filePath">モデルのファイル名（無ければ nullptr になる）</param>
    void SetModel(const std::string& filePath);
    /// <summary>WVP とカメラ CB に使うカメラを設定する</summary>
    /// <param name="camera">カメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }
    /// <summary>拡大率を設定する</summary>
    /// <param name="scale">軸ごとの拡大率</param>
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    /// <summary>向きを設定する</summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    /// <summary>位置を設定する</summary>
    /// <param name="translate">ワールド座標</param>
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    /// <summary>テクスチャのパスを覚える（モデルへの反映は未実装）</summary>
    /// <param name="filePath">テクスチャのパス</param>
    void SetTexture(const std::string& filePath);

    /// <summary>ワールド行列を直接上書きする（ボーン追従用）。Update がこれを優先採用する</summary>
    /// <param name="m">ワールド行列</param>
    void SetWorldMatrixOverride(const Matrix4x4& m) { worldOverride_ = m; hasWorldOverride_ = true; }
    /// <summary>ワールド行列の上書きをやめて transform_ に戻す</summary>
    void ClearWorldMatrixOverride() { hasWorldOverride_ = false; }

    //==============================
    // Material関連セッター（環境マッピング関連）
    //==============================

    /// <summary>
    /// 環境マップの使用ON/OFF
    /// true: 環境マップあり（Object3d.PS.hlsl を使用）
    /// false: 環境マップなし（Object3dNoEnv.PS.hlsl を使用、計算自体をスキップ）
    /// </summary>
    /// <param name="use">使うなら true</param>
    void SetUseEnvironmentMap(bool use);

    /// <summary>
    /// 環境マップの反映度合い (0.0〜1.0)
    /// 1.0で「磨き上げられた金属」レベルの映り込みすぎ
    /// 0.3程度が現実的な金属光沢
    /// 0.0で映り込みなし（ただし計算自体は行われる、完全OFFは SetUseEnvironmentMap(false)）
    /// </summary>
    /// <param name="coef">反映度合い（0.0〜1.0）</param>
    void SetEnvironmentCoefficient(float coef);

    /// <summary>
    /// マテリアルカラー（GPU側 Material::color）を直接書き換える。
    /// SetModel 前は no-op。alpha < 1.0f で半透明になる。
    /// </summary>
    /// <param name="color">RGBA</param>
    void SetMaterialColor(const Vector4& color);

    /// <summary>
    /// 現在のマテリアルカラーを取得（SetModel 前は白を返す）。
    /// </summary>
    /// <returns>マテリアルの色。モデルやマテリアルが無ければ白</returns>
    Vector4 GetMaterialColor() const;

    //==============================
    // PBR マテリアルセッター
    //==============================
    /// <summary>PBR の金属度を書く</summary>
    /// <param name="metallic">金属度（0〜1）</param>
    void SetMetallic(float metallic);              // 金属度 0..1
    /// <summary>PBR の粗さを書く</summary>
    /// <param name="roughness">粗さ（0〜1）</param>
    void SetRoughness(float roughness);            // 粗さ 0..1
    /// <summary>シェーディングの種類を書く（Draw の PSO 選択に使う）</summary>
    /// <param name="shadingModel">0=BlinnPhong, 1=PBR（kShaderPBR）, 2=地形（kShaderTerrain）</param>
    void SetShadingModel(int shadingModel);        // 0=BlinnPhong, 1=PBR（kShaderPBR を使用）

    //==============================
    // ゲッター
    //==============================
    /// <summary>拡大率を返す</summary>
    /// <returns>拡大率</returns>
    const Vector3& GetScale() const { return transform_.scale; }
    /// <summary>向きを返す</summary>
    /// <returns>オイラー角 [ラジアン]</returns>
    const Vector3& GetRotate() const { return transform_.rotate; }
    /// <summary>位置を返す</summary>
    /// <returns>ワールド座標</returns>
    const Vector3& GetTranslate() const { return transform_.translate; }
    /// <summary>テクスチャのパスを返す</summary>
    /// <returns>パス</returns>
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    /// <summary>モデルのファイル名を返す（シーン JSON 保存用）</summary>
    /// <returns>ファイル名</returns>
    const std::string& GetModelFileName() const { return modelFileName_; }
    /// <summary>モデルのディレクトリを返す（シーン JSON 保存用）</summary>
    /// <returns>ディレクトリ</returns>
    const std::string& GetDirectoryPath() const { return directoryPath_; }
    /// <summary>モデルを返す（部位別マテリアル（submesh ごとの Material）を個別に調整するため）</summary>
    /// <returns>モデル（無ければ nullptr）</returns>
    ModelInstance* GetModelInstance() const { return modelInstance_; }

    /// <summary>
    /// ワールド空間の包む球（モデルの球をワールド行列で動かし、半径に一番大きいスケールを掛ける）。
    /// モデルが無い・球が分からない時は false（呼び出し側は常に見える扱いにする）。
    /// </summary>
    /// <param name="center">ワールドの中心の書き込み先</param>
    /// <param name="radius">ワールドの半径の書き込み先</param>
    /// <returns>求められたら true</returns>
    bool GetWorldBoundingSphere(Vector3& center, float& radius) const;

    //==============================
    // 初期化・更新・描画
    //==============================
    /// <summary>ModelManager でモデルを読み込んで取得し、変換行列とカメラの CB を作る</summary>
    /// <param name="object3DManager">既定カメラ・PSO の取得元</param>
    /// <param name="dxCore">デバイスの取得元</param>
    /// <param name="directorPath">モデルのあるディレクトリ</param>
    /// <param name="filename">モデルのファイル名</param>
    /// <param name="name">表示名（空ならファイル名）</param>
    void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore,
        const std::string& directorPath, const std::string& filename,
        const std::string& name = "");

    /// <summary>ワールド行列（rootNode の localMatrix 込み）・WVP・逆転置とカメラ位置を CB に書く</summary>
    void Update();

    /// <summary>マテリアルに合う PSO を選び、[1]=VS b0 変換 / [4]=PS b2 カメラ を貼ってモデルを描く（ルートシグネチャは DrawSetting 済み）</summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void Draw(DirectXCore* dxCore);
};