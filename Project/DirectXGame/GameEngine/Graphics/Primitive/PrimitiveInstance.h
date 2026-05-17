#pragma once
#include "PrimitiveMesh.h"
#include "PrimitiveGenerator.h"  // RingParams / CylinderParams
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
        kCount,
    };

    PrimitiveInstance() = default;
    ~PrimitiveInstance() override = default;

    /// <summary>
    /// 初期化（指定タイプのメッシュを生成し、デフォルトテクスチャを適用）
    /// </summary>
    void Initialize(PrimitiveType type, const std::string& name = "");

    /// <summary>
    /// 毎フレーム更新（WVP・UV計算）
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    //==============================
    // IImGuiEditable 実装
    //==============================
    std::string GetName() const override { return name_; }
    std::string GetTypeName() const override { return "Primitive"; }
    void OnImGuiInspector() override;
    Vector3* GetEditableTranslate() override { return &mesh_.GetTransform().translate; }

    //==============================
    // セッター
    //==============================
    void SetName(const std::string& name) override { name_ = name; }
    void SetCamera(Camera* camera) { camera_ = camera; }
    void SetTexture(const std::string& filePath);
    void SetScale(const Vector3& scale) { mesh_.GetTransform().scale = scale; }
    void SetRotate(const Vector3& rotate) { mesh_.GetTransform().rotate = rotate; }
    void SetTranslate(const Vector3& translate) { mesh_.GetTransform().translate = translate; }

    //==============================
    // ゲッター
    //==============================
    PrimitiveType GetPrimitiveType() const { return primitiveType_; }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    const PrimitiveMesh& GetMesh() const { return mesh_; }
    PrimitiveMesh& GetMesh() { return mesh_; }

    /// <summary>
    /// プリミティブタイプ名（"Plane", "Box" 等）
    /// </summary>
    static const char* PrimitiveTypeToString(PrimitiveType type);

    /// <summary>
    /// デフォルトテクスチャパス（白1x1）
    /// </summary>
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

    // Ring / Cylinder のジオメトリパラメータ（Inspector で編集 → 再生成）
    PrimitiveGenerator::RingParams     ringParams_;
    PrimitiveGenerator::CylinderParams cylinderParams_;

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

    // 形状ごとのパラメータからメッシュを再生成（Ring/Cylinder のみ）
    void RegenerateGeometry();
};
