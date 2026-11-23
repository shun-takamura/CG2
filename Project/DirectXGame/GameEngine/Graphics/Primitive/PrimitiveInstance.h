#pragma once
#include "PrimitiveMesh.h"
#include "PrimitiveGenerator.h"  // RingParams / CylinderParams
#include "PrimitivePrefabParams.h"
#include "IImGuiEditable.h"
#include "TimeGroup.h"
#include "Vector2.h"
#include <string>

// 前方宣言
class Camera;

/// <summary>
/// シーン内に配置できるプリミティブ（Plane/Box/Sphere/Ring/Cylinder）。
/// Object3DInstance と同じく Hierarchy/Inspector に表示される動的エンティティ。
/// </summary>
class PrimitiveInstance : public IImGuiEditable {
public:
    /// <summary>
    /// プリミティブの種類
    /// </summary>
    enum class PrimitiveType {
        Plane,
        Box,
        Sphere,
        Ring,
        Cylinder,
        Helix,
        Hemisphere, // 末尾に追加（既存の保存値 0〜5 を保つため kCount の手前）
        kCount,
    };

    PrimitiveInstance() = default;
    ~PrimitiveInstance() override = default;

    /// <summary>
    /// 初期化（指定タイプのメッシュを生成し、デフォルトテクスチャを適用）
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="name">表示名（空ならタイプ名）</param>
    void Initialize(PrimitiveType type, const std::string& name = "");

    /// <summary>
    /// 毎フレーム更新（WVP・UV計算）
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    /// <summary>
    /// ID Pass：idMaskRT に objectId_ を書き込む
    /// </summary>
    void DrawIdPass();

    //==============================
    // IImGuiEditable 実装
    //==============================
    /// <summary>表示名を返す</summary>
    /// <returns>表示名</returns>
    std::string GetName() const override { return name_; }
    /// <summary>型名を返す</summary>
    /// <returns>"Primitive"</returns>
    std::string GetTypeName() const override { return "Primitive"; }
    /// <summary>Inspector に形状・テクスチャ・UV・材質・TimeGroup の編集 UI を描く（形状の再生成は次の Update で行う）</summary>
    void OnImGuiInspector() override;
    /// <summary>ギズモで動かす位置を返す</summary>
    /// <returns>メッシュの translate へのポインタ</returns>
    Vector3* GetEditableTranslate() override { return &mesh_.GetTransform().translate; }
    /// <summary>ギズモ表示用の回転を返す</summary>
    /// <returns>メッシュの rotate へのポインタ</returns>
    const Vector3* GetEditableRotate() const override { return &mesh_.GetTransform().rotate; }

    //==============================
    // セッター
    //==============================
    /// <summary>表示名を設定する</summary>
    /// <param name="name">表示名</param>
    void SetName(const std::string& name) override { name_ = name; }
    /// <summary>Update の WVP・ビルボードに使うカメラを設定する</summary>
    /// <param name="camera">カメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }
    /// <summary>テクスチャを差し替える（パスも保存用に覚える）</summary>
    /// <param name="filePath">テクスチャのパス</param>
    void SetTexture(const std::string& filePath);
    /// <summary>拡大率を設定する</summary>
    /// <param name="scale">軸ごとの拡大率</param>
    void SetScale(const Vector3& scale) { mesh_.GetTransform().scale = scale; }
    /// <summary>向きを設定する</summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetRotate(const Vector3& rotate) { mesh_.GetTransform().rotate = rotate; }
    /// <summary>位置を設定する</summary>
    /// <param name="translate">ワールド座標</param>
    void SetTranslate(const Vector3& translate) { mesh_.GetTransform().translate = translate; }

    //==============================
    // ゲッター
    //==============================
    /// <summary>プリミティブの種類を返す</summary>
    /// <returns>種類</returns>
    PrimitiveType GetPrimitiveType() const { return primitiveType_; }
    /// <summary>テクスチャのパスを返す</summary>
    /// <returns>今のテクスチャのパス</returns>
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    /// <summary>描画メッシュを返す（読み取り専用）</summary>
    /// <returns>メッシュ</returns>
    const PrimitiveMesh& GetMesh() const { return mesh_; }
    /// <summary>描画メッシュを返す</summary>
    /// <returns>メッシュ</returns>
    PrimitiveMesh& GetMesh() { return mesh_; }

    // ----- プリファブ保存 / 復元に必要な内部状態の公開 -----
    /// <summary>時間グループを返す</summary>
    /// <returns>UV スクロール等の進み方</returns>
    TimeGroup GetTimeGroup() const { return timeGroup_; }
    /// <summary>時間グループを設定する</summary>
    /// <param name="g">UV スクロール等の進み方</param>
    void SetTimeGroup(TimeGroup g) { timeGroup_ = g; }
    /// <summary>UV 自動スクロールが有効か</summary>
    /// <returns>有効なら true</returns>
    bool   GetAutoScroll() const { return autoScrollEnabled_; }
    /// <summary>UV スクロール速度を返す</summary>
    /// <returns>1 秒あたりのスクロール量</returns>
    Vector2 GetScrollSpeed() const { return scrollSpeed_; }
    /// <summary>手動の UV オフセットを返す</summary>
    /// <returns>オフセット</returns>
    Vector2 GetManualUVOffset() const { return manualUVOffset_; }
    /// <summary>UV の拡大率を返す</summary>
    /// <returns>拡大率</returns>
    Vector2 GetUVScale() const { return uvScale_; }
    /// <summary>U を反転しているか</summary>
    /// <returns>反転していれば true</returns>
    bool   GetFlipU() const { return flipU_; }
    /// <summary>V を反転しているか</summary>
    /// <returns>反転していれば true</returns>
    bool   GetFlipV() const { return flipV_; }
    /// <summary>discard の α 閾値を返す</summary>
    /// <returns>閾値（0 で捨てない）</returns>
    float  GetAlphaReference() const { return alphaReference_; }
    /// <summary>背面カリングするか</summary>
    /// <returns>カリングするなら true</returns>
    bool   GetCullBackface() const { return cullBackface_; }
    /// <summary>サンプラーの種類を返す</summary>
    /// <returns>0=WrapAll, 1=WrapU+ClampV, 2=ClampAll</returns>
    int    GetSamplerMode() const { return samplerMode_; }
    /// <summary>Ring の形状パラメータを返す</summary>
    /// <returns>パラメータ</returns>
    const PrimitiveGenerator::RingParams&     GetRingParams() const { return ringParams_; }
    /// <summary>Cylinder の形状パラメータを返す</summary>
    /// <returns>パラメータ</returns>
    const PrimitiveGenerator::CylinderParams& GetCylinderParams() const { return cylinderParams_; }
    /// <summary>Helix の形状パラメータを返す</summary>
    /// <returns>パラメータ</returns>
    const PrimitiveGenerator::HelixParams&    GetHelixParams() const { return helixParams_; }

    /// <summary>
    /// プリファブから読み込んだパラメータを適用する。
    /// Initialize 直後に呼ぶ想定（必要に応じてメッシュを再生成する）。
    /// </summary>
    /// <param name="p">プリファブから読み込んだパラメータ</param>
    void ApplyPrefabParams(const PrimitivePrefabParams& p);

    /// <summary>
    /// プリミティブタイプ名（"Plane", "Box" 等）
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <returns>タイプ名。範囲外は "Unknown"</returns>
    static const char* PrimitiveTypeToString(PrimitiveType type);

    /// <summary>
    /// 保存用の数字（JSON・ドラッグ＆ドロップのデータに入っている int）をプリミティブの種類に直す
    /// </summary>
    /// <param name="value">保存用の数字</param>
    /// <param name="outType">直した種類（範囲外の時は書き換えない）</param>
    /// <returns>0 以上 kCount 未満なら true</returns>
    static bool PrimitiveTypeFromInt(int value, PrimitiveType& outType);

    /// <summary>
    /// デフォルトテクスチャパス（白1x1）
    /// </summary>
    /// <returns>"Resources/Textures/white1x1.dds"</returns>
    static const char* GetDefaultTexturePath() { return "Resources/Textures/white1x1.dds"; }

private:
    PrimitiveMesh mesh_;
    PrimitiveType primitiveType_ = PrimitiveType::Plane;
    Camera* camera_ = nullptr;
    std::string name_;
    std::string textureFilePath_;

    // UV 編集用に Inspector 側で保持する状態
    // （PrimitiveMesh 側のメンバには getter が無いため、ここで二重管理）
    bool   autoScrollEnabled_ = false;
    Vector2 scrollSpeed_      = { 0.0f, 0.0f }; // 1秒あたりのスクロール量
    Vector2 manualUVOffset_   = { 0.0f, 0.0f };
    Vector2 uvScale_          = { 1.0f, 1.0f };
    bool   flipU_             = false;
    bool   flipV_             = false;

    // Ring / Cylinder / Helix のジオメトリパラメータ（Inspector で編集 → 再生成）
    PrimitiveGenerator::RingParams     ringParams_;
    PrimitiveGenerator::CylinderParams cylinderParams_;
    PrimitiveGenerator::HelixParams    helixParams_;

    // discard 閾値 / 背面カリング / サンプラーモード（Inspector で編集）
    float alphaReference_ = 0.0f;
    bool  cullBackface_   = false;
    int   samplerMode_    = 0; // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll

    // このプリミティブの時間グループ（UV スクロール等の進行速度を切り替える）
    TimeGroup timeGroup_ = TimeGroup::World;

    // Inspector のスライダー編集中にメッシュを即時再生成すると、
    // 描画中コマンドリストが参照中のリソースが解放されて D3D12 ERROR #921 になる。
    // ImGui からは「次フレーム再生成」フラグだけ立てて、Update 冒頭で実行する。
    bool regenPending_ = false;

    /// <summary>形状ごとのパラメータからメッシュを再生成（Ring/Cylinder/Helix のみ。他の形状は何もしない）</summary>
    void RegenerateGeometry();
};
