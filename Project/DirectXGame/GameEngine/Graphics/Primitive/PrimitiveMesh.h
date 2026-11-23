#pragma once
#include "PrimitivePipeline.h"
#include "MeshData.h"
#include "PrimitiveMaterial.h"
#include "Material.h"
#include "Transform.h"
#include "Matrix4x4.h"
#include "Quaternion.h"
#include <wrl.h>
#include <d3d12.h>
#include <functional>
#include <string>
#include "Vector2.h"
#include "BillboardMode.h"

// 前方宣言
class Camera;

class PrimitiveMesh {
public:
    PrimitiveMesh() = default;
    ~PrimitiveMesh() = default;

    /// <summary>初期化（MeshDataを受け取ってGPUバッファ化。変換行列・マテリアルの CB も作る）</summary>
    /// <param name="meshData">頂点とインデックス</param>
    void Initialize(const MeshData& meshData);

    /// <summary>
    /// 同じ形を何度も作る物（弾・エフェクトの板など）用。geometryKey が同じなら頂点・インデックスのバッファを使い回し、
    /// 2 回目からは make を呼ばない（形の計算もバッファの作成もしない）。共有したメッシュは UpdateVertices できない
    /// </summary>
    /// <param name="geometryKey">形を見分けるキー</param>
    /// <param name="make">初回だけ呼ぶ形の生成関数</param>
    void InitializeShared(const std::string& geometryKey, const std::function<MeshData()>& make);
    /// <summary>InitializeShared の共有バッファを捨てる（終了時。PrimitivePipeline::Finalize から）</summary>
    static void ReleaseSharedGeometry();

    /// <summary>
    /// 頂点の中身だけ書き換える（ひも・雷など毎フレーム形が変わるもの用）。頂点数とインデックスは Initialize のまま。
    /// count が Initialize の頂点数を超えた分は捨てる。バッファは 1 本なので、同じフレームに違う形で 2 回は描けない
    /// （DirectXCore はフレームの終わりに GPU を待つので、次のフレームの Update で書き換えてよい）
    /// </summary>
    /// <param name="vertices">新しい頂点の配列</param>
    /// <param name="count">頂点数</param>
    void UpdateVertices(const MeshVertex* vertices, uint32_t count);

    /// <summary>毎フレーム更新（WVP行列計算）。旧版: dxCore のスケール済み経過時間で UV スクロールが進む</summary>
    /// <param name="camera">WVP とビルボードに使うカメラ（nullptr ならワールド行列をそのまま WVP に）</param>
    void Update(Camera* camera);
    /// <summary>毎フレーム更新。新版: 呼び出し側が渡した deltaTime で UV スクロールが進む（TimeGroup 連動用）。マテリアル CB も書く</summary>
    /// <param name="camera">WVP とビルボードに使うカメラ（nullptr ならワールド行列をそのまま WVP に）</param>
    /// <param name="deltaTime">経過時間 [秒]</param>
    void Update(Camera* camera, float deltaTime);

    /// <summary>
    /// プレビュー用の WVP を別CBに書き込む（メインの Update とは独立）。
    /// 同じインスタンスを Scene RT と Effect Preview RT の両方に描画するときに使う。
    /// </summary>
    /// <param name="viewMatrix">プレビューカメラのビュー行列（ビルボード用）</param>
    /// <param name="viewProjectionMatrix">プレビューカメラのビュープロジェクション行列</param>
    /// <param name="cameraPos">プレビューカメラの位置（YAxis ビルボード用）</param>
    void UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);

    /// <summary>描画（メイン用 CB を bind。ルート [0]=VS b0 変換 / [1]=PS b0 マテリアル / [2]=PS t0 テクスチャ / [3]=PS t1 マスク）</summary>
    void Draw();

    /// <summary>ID Pass：idMaskRT に objectId を書き込む（ID 用ルートの [0]=VS b0 変換、[1]=PS b0 ルート定数）</summary>
    /// <param name="objectId">書き込むオブジェクト ID</param>
    void DrawIdPass(uint32_t objectId);

    /// <summary>
    /// Distortion Pass：DistortionRT にノーマルマップ由来の歪み情報を書き込む
    /// 通常テクスチャとは独立した UV 変換（distortionUv*）で描画される。ノーマルマップは PS t0、t1 は white1x1
    /// </summary>
    /// <param name="normalMapSrvIndex">歪みのノーマルマップの SRV 番号</param>
    void DrawDistortionPass(uint32_t normalMapSrvIndex);

    /// <summary>DrawDistortionPass のプレビュー版。VS CBV には transformPreviewResource_（プレビューカメラの WVP）を bind する</summary>
    /// <param name="normalMapSrvIndex">歪みのノーマルマップの SRV 番号</param>
    void DrawDistortionPassPreview(uint32_t normalMapSrvIndex);

    /// <summary>Bloom Pass：発光 RT に「通常描画と同じ見た目 × intensity」を加算で書き込む</summary>
    /// <param name="intensity">発光の強さ（Bloom 専用マテリアルの色に掛ける）</param>
    void DrawBloomPass(float intensity);
    /// <summary>DrawBloomPass のプレビュー版（VS CBV にプレビューカメラの WVP を bind する）</summary>
    /// <param name="intensity">発光の強さ</param>
    void DrawBloomPassPreview(float intensity);

    /// <summary>プレビュー用 CB を bind して描画</summary>
    void DrawPreview();

    /// <summary>
    /// 水面の反射 RT へ描く。鏡像の View / ViewProj（行列式が負）で WVP を作り、反射用 CB に書いて描く。
    /// 鏡像で三角形の巻きが反転するので、裏面カリングは切る。
    /// </summary>
    /// <param name="mirroredView">水面で折り返したビュー行列</param>
    /// <param name="mirroredViewProjection">水面で折り返したビュープロジェクション行列</param>
    /// <param name="mirroredCameraPos">水面で折り返したカメラ位置</param>
    void DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos);

    /// <summary>テクスチャ設定（パスを指定して読み込み、PS t0 にバインドする SRV を覚える）</summary>
    /// <param name="textureFilePath">テクスチャのパス</param>
    void SetTexture(const std::string& textureFilePath);

    /// <summary>ディゾルブ用マスクテクスチャ（空文字でリセット）。SRV は TextureManager 経由でキャッシュ</summary>
    /// <param name="textureFilePath">マスクのパス</param>
    void SetDissolveMask(const std::string& textureFilePath);
    /// <summary>ディゾルブの有効/無効と閾値。閾値を時間で 0→1 に上げると暗い部分から消える</summary>
    /// <param name="enable">有効にするなら true</param>
    /// <param name="threshold">閾値（0〜1）</param>
    void SetDissolve(bool enable, float threshold) { dissolveEnable_ = enable ? 1 : 0; dissolveThreshold_ = threshold; }
    /// <summary>ディゾルブのアウトライン（燃えるエッジ）。閾値近傍の帯を color で塗る</summary>
    /// <param name="enable">縁を出すなら true</param>
    /// <param name="color">縁の色</param>
    /// <param name="width">縁の幅（閾値の単位）</param>
    void SetDissolveEdge(bool enable, const Vector4& color, float width) {
        dissolveEdgeEnable_ = enable ? 1 : 0; dissolveEdgeColor_ = color; dissolveEdgeWidth_ = width;
    }

    // 各種設定
    /// <summary>ブレンドモードを設定する</summary>
    /// <param name="mode">ブレンドモード</param>
    void SetBlendMode(PrimitivePipeline::BlendMode mode) { blendMode_ = mode; }
    /// <summary>深度を書くか設定する</summary>
    /// <param name="enable">書くなら true</param>
    void SetDepthWrite(bool enable) { depthWrite_ = enable; }
    /// <summary>背面カリングするか設定する</summary>
    /// <param name="enable">カリングするなら true（false で両面描画）</param>
    void SetCullBackface(bool enable) { cullBackface_ = enable; }
    /// <summary>マテリアルの色を設定する（頂点色と乗算）</summary>
    /// <param name="color">RGBA</param>
    void SetColor(const Vector4& color) { color_ = color; }

    /// <summary>α値がこれ以下のピクセルはdiscardされる（0.0でdiscardなし）</summary>
    /// <param name="value">閾値</param>
    void SetAlphaReference(float value) { alphaReference_ = value; }

    /// <summary>サンプラーモードを設定する（PS が s0〜s2 から選ぶ）</summary>
    /// <param name="mode">0=WrapAll, 1=WrapU+ClampV, 2=ClampAll</param>
    void SetSamplerMode(int mode) { samplerMode_ = mode; }
    /// <summary>サンプラーモードを返す</summary>
    /// <returns>0=WrapAll, 1=WrapU+ClampV, 2=ClampAll</returns>
    int  GetSamplerMode() const { return samplerMode_; }

    /// <summary>視線角度フェード（0=無効、>0で面法線がエッジオンに近いほどα低下。雷/レーザー用）</summary>
    /// <param name="power">べき乗の指数</param>
    void SetViewAngleFadePower(float power) { viewAngleFadePower_ = power; }
    /// <summary>視線角度フェードの強さを返す</summary>
    /// <returns>べき乗の指数（0 で無効）</returns>
    float GetViewAngleFadePower() const { return viewAngleFadePower_; }

    /// <summary>ビルボードモード（None / Full / YAxis）を設定する</summary>
    /// <param name="mode">ビルボードの種類</param>
    void SetBillboardMode(BillboardMode mode) { billboardMode_ = mode; }
    /// <summary>ビルボードモードを返す</summary>
    /// <returns>ビルボードの種類</returns>
    BillboardMode GetBillboardMode() const { return billboardMode_; }

    // UV変換設定
    /// <summary>UV のスクロール速度を設定する</summary>
    /// <param name="scrollPerSec">1 秒あたりのスクロール量</param>
    void SetUVScroll(const Vector2& scrollPerSec) { uvScrollSpeed_ = scrollPerSec; }
    /// <summary>V を反転するか設定する</summary>
    /// <param name="flip">反転するなら true</param>
    void SetUVFlipV(bool flip) { uvFlipV_ = flip; }
    /// <summary>U を反転するか設定する</summary>
    /// <param name="flip">反転するなら true</param>
    void SetUVFlipU(bool flip) { uvFlipU_ = flip; }
    /// <summary>UV の拡大率を設定する</summary>
    /// <param name="scale">拡大率</param>
    void SetUVScale(const Vector2& scale) { uvScale_ = scale; }
    /// <summary>UV のオフセットを設定する（スクロールの累積に足す）</summary>
    /// <param name="offset">オフセット</param>
    void SetUVOffset(const Vector2& offset) { uvOffset_ = offset; } // 手動で設定したい場合

    // Distortion 用 UV 変換設定（通常テクスチャの UV と完全に独立）
    /// <summary>歪みテクスチャの UV スクロール速度を設定する</summary>
    /// <param name="scrollPerSec">1 秒あたりのスクロール量</param>
    void SetDistortionUVScroll(const Vector2& scrollPerSec) { distortionUVScrollSpeed_ = scrollPerSec; }
    /// <summary>歪みテクスチャの U を反転するか設定する</summary>
    /// <param name="flip">反転するなら true</param>
    void SetDistortionUVFlipU(bool flip) { distortionUVFlipU_ = flip; }
    /// <summary>歪みテクスチャの V を反転するか設定する</summary>
    /// <param name="flip">反転するなら true</param>
    void SetDistortionUVFlipV(bool flip) { distortionUVFlipV_ = flip; }
    /// <summary>歪みテクスチャの UV 拡大率を設定する</summary>
    /// <param name="scale">拡大率</param>
    void SetDistortionUVScale(const Vector2& scale) { distortionUVScale_ = scale; }
    /// <summary>歪みテクスチャの UV オフセットを設定する</summary>
    /// <param name="offset">オフセット</param>
    void SetDistortionUVOffset(const Vector2& offset) { distortionUVOffset_ = offset; }
    /// <summary>per-instance の歪み強度（0..1）。distortionMaterialData_->color.a に書かれ、シェーダーで texture.a × vertex.color.a × strength として使われる</summary>
    /// <param name="s">強さ</param>
    void SetDistortionStrength(float s) { distortionStrength_ = s; }

    /// <summary>Transformアクセス（translate/rotate/scaleを外部から書き換え可能）</summary>
    /// <returns>Transform</returns>
    Transform& GetTransform() { return transform_; }
    /// <summary>Transform を返す（読み取り専用）</summary>
    /// <returns>Transform</returns>
    const Transform& GetTransform() const { return transform_; }

    /// <summary>
    /// クオータニオン回転を使う（ジンバルロック回避）。設定すると transform_.rotate(オイラー)は無視され、
    /// この向きで回転行列が組まれる。ClearRotateQuaternion() で従来のオイラー回転に戻る。
    /// </summary>
    /// <param name="q">向き</param>
    void SetRotateQuaternion(const Quaternion& q) { rotateQuat_ = q; useQuatRotation_ = true; }
    /// <summary>クオータニオン回転をやめてオイラー回転に戻す</summary>
    void ClearRotateQuaternion() { useQuatRotation_ = false; }

    // ゲッター
    /// <summary>ブレンドモードを返す</summary>
    /// <returns>ブレンドモード</returns>
    PrimitivePipeline::BlendMode GetBlendMode() const { return blendMode_; }
    /// <summary>深度を書くか</summary>
    /// <returns>書くなら true</returns>
    bool GetDepthWrite() const { return depthWrite_; }
    /// <summary>マテリアルの色を返す</summary>
    /// <returns>RGBA</returns>
    const Vector4& GetColor() const { return color_; }

private:
    // GPUリソースの作成
    /// <summary>頂点バッファを作ってコピーする（空なら 1 頂点分だけ確保し描画側でスキップ）</summary>
    /// <param name="meshData">頂点の取り出し元</param>
    void CreateVertexResource(const MeshData& meshData);
    /// <summary>インデックスバッファ（R32_UINT）を作ってコピーする（空なら 1 個分だけ確保）</summary>
    /// <param name="meshData">インデックスの取り出し元</param>
    void CreateIndexResource(const MeshData& meshData);
    /// <summary>メイン用の変換行列 CB（VS b0）を作ってマップする</summary>
    void CreateTransformResource();
    /// <summary>マテリアル CB（PS b0）を作ってマップし、t1 の既定マスク white1x1 を読む</summary>
    void CreateMaterialResource();
    // プレビュー・反射・歪みの CB は使う物が少ないので、初めて使う時に作る（弾 1 発ごとのバッファ作成を減らす）
    /// <summary>プレビュー用の変換行列 CB が無ければ作る</summary>
    void EnsurePreviewTransform();
    /// <summary>水面の反射用の変換行列 CB が無ければ作る</summary>
    void EnsureReflectionTransform();
    /// <summary>歪みパス用のマテリアル CB が無ければ作り、今の UV と強さを書く</summary>
    void EnsureDistortionMaterial();
    /// <summary>歪みパス用のマテリアル CB に UV 変換と強さ（color.a）を書く</summary>
    void WriteDistortionMaterial();

    /// <summary>Bloom 専用マテリアルに「色 × 強さ」を書き、Bloom PSO で 1 回描く（初回に Bloom 用 CB を作る）</summary>
    /// <param name="transformResource">VS b0 に渡す変換行列 CB（メイン用かプレビュー用）</param>
    /// <param name="intensity">発光の強さ</param>
    void DrawBloomPassImpl(ID3D12Resource* transformResource, float intensity);

    /// <summary>指定カメラに対するワールド行列を構築（billboardMode に応じて回転に補正がかかる）</summary>
    /// <param name="camera">カメラ（nullptr ならビルボードしない）</param>
    /// <returns>ワールド行列</returns>
    Matrix4x4 BuildWorldMatrix(Camera* camera) const;

    /// <summary>ビュー行列とカメラ位置から billboard 補正込みのワールド行列を構築</summary>
    /// <param name="viewMatrix">ビュー行列（Full ビルボード用）</param>
    /// <param name="cameraPos">カメラ位置（YAxis ビルボード用）</param>
    /// <returns>ワールド行列</returns>
    Matrix4x4 BuildWorldMatrixFromMatrices(const Matrix4x4& viewMatrix, const Vector3& cameraPos) const;

    // GPU送信用の変換行列構造体
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t indexCount_ = 0;
    // 頂点・インデックスが InitializeShared の共有バッファ（書き換え禁止）
    bool sharedGeometry_ = false;

    // 変換行列バッファ（メイン用）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformationMatrix* transformData_ = nullptr;

    // 変換行列バッファ（プレビュー用）。同じワールド座標を別カメラで描画するための WVP 専用 CB。
    // ワールド行列は同じものを格納（プレビュー描画でも World 用途に使えるよう）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformPreviewResource_;
    TransformationMatrix* transformPreviewData_ = nullptr;
    // 水面の反射用（DrawReflection）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformReflectionResource_;
    TransformationMatrix* transformReflectionData_ = nullptr;

    // マテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    PrimitiveMaterial* materialData_ = nullptr;

    // Distortion 用マテリアル CB（distortion パスでのみ bind される）
    // 通常テクスチャと独立した uvTransform を持つ
    Microsoft::WRL::ComPtr<ID3D12Resource> distortionMaterialResource_;
    PrimitiveMaterial* distortionMaterialData_ = nullptr;

    // Bloom 用マテリアル CB（通常マテリアルの color に強度を掛けた写し）。初回の Bloom 描画時に作る。
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomMaterialResource_;
    PrimitiveMaterial* bloomMaterialData_ = nullptr;

    // Transform
    Transform transform_{
        { 1.0f, 1.0f, 1.0f }, // scale
        { 0.0f, 0.0f, 0.0f }, // rotate
        { 0.0f, 0.0f, 0.0f }  // translate
    };

    // クオータニオン回転（useQuatRotation_ が true のとき transform_.rotate の代わりに使う）
    Quaternion rotateQuat_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool       useQuatRotation_ = false;

    // 色
    Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };

    // UV変換関連
    Vector2 uvScrollSpeed_ = { 0.0f, 0.0f };    // 1秒あたりのスクロール量
    Vector2 uvScrollAccumulated_ = { 0.0f, 0.0f }; // スクロール累積値
    Vector2 uvOffset_ = { 0.0f, 0.0f };         // 手動設定分
    Vector2 uvScale_ = { 1.0f, 1.0f };
    bool uvFlipU_ = false;
    bool uvFlipV_ = false;
    float alphaReference_ = 0.0f; // デフォルト：discardしない
    int   samplerMode_ = 0;       // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll
    float viewAngleFadePower_ = 0.0f; // デフォルト無効

    // Distortion 用 UV 変換関連（通常テクスチャと別の蓄積・状態を持つ）
    Vector2 distortionUVScrollSpeed_     = { 0.0f, 0.0f };
    Vector2 distortionUVScrollAccumulated_ = { 0.0f, 0.0f };
    Vector2 distortionUVOffset_          = { 0.0f, 0.0f };
    Vector2 distortionUVScale_           = { 1.0f, 1.0f };
    bool    distortionUVFlipU_ = false;
    bool    distortionUVFlipV_ = false;
    float   distortionStrength_ = 1.0f; // per-instance 歪み強度（distortionMaterialData_->color.a へ書く）

    // 描画設定
    PrimitivePipeline::BlendMode blendMode_ = PrimitivePipeline::kBlendModeAdd;
    bool depthWrite_ = false;
    bool cullBackface_ = false; // true で背面カリング、false で両面描画

    // ビルボードモード
    BillboardMode billboardMode_ = BillboardMode::None;

    // テクスチャ（SRVインデックスで管理）
    uint32_t textureSrvIndex_ = 0;
    bool hasTexture_ = false;

    // ディゾルブ
    uint32_t dissolveMaskSrvIndex_ = 0;
    bool     hasDissolveMask_ = false;
    int      dissolveEnable_ = 0;
    float    dissolveThreshold_ = 0.0f;
    int      dissolveEdgeEnable_ = 0;
    float    dissolveEdgeWidth_ = 0.05f;
    Vector4  dissolveEdgeColor_ = { 1.0f, 0.4f, 0.1f, 1.0f };
    // t1 に常時バインドする既定マスク（white1x1）。マスク未設定時のフォールバック。
    uint32_t whiteSrvIndex_ = 0;
};