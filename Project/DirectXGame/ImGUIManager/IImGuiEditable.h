#pragma once
#include <string>
#include "Vector2.h"
#include "Vector3.h"
#include "Components/EntityTag.h"
#include "Components/SphereCollider.h"

// 前方宣言
class ImGuiManager;

/// <summary>
/// ImGuiで編集可能なオブジェクトのインターフェース
/// このクラスを継承すると、自動的にImGuiManagerに登録される
/// </summary>
class IImGuiEditable {
public:
    /// <summary>
    /// コンストラクタ（自動登録）
    /// </summary>
    IImGuiEditable();

    /// <summary>
    /// 自動登録をスキップしたい場合用のタグ
    /// （EffectEditor 等、独立した Hierarchy で管理する派生で使う）
    /// </summary>
    struct NoAutoRegister {};
    IImGuiEditable(NoAutoRegister);

    /// <summary>
    /// デストラクタ（自動登録解除）
    /// </summary>
    virtual ~IImGuiEditable();

    /// <summary>
    /// オブジェクト名を取得
    /// </summary>
    virtual std::string GetName() const = 0;

    /// <summary>
    /// オブジェクト名を設定（Inspectorからの編集用。デフォルトは何もしない）
    /// 派生側で実体の name_ メンバを書き換える override を提供する。
    /// </summary>
    virtual void SetName(const std::string& /*name*/) {}

    /// <summary>
    /// 型名を取得（例: "Object3D", "Sprite"）
    /// </summary>
    virtual std::string GetTypeName() const = 0;

    //====================
    // タグ
    //====================
    EntityTag GetTag() const { return tag_; }
    void SetTag(EntityTag t) { tag_ = t; }

    //====================
    // Debug時の表示ON/OFF（Hierarchyの目玉アイコン用、Releaseでは効かない）
    //====================
    bool IsVisibleInEditor() const { return visibleInEditor_; }
    void SetVisibleInEditor(bool v) { visibleInEditor_ = v; }

    //====================
    // コライダー（タグが衝突可能な場合のみ enabled を true にする運用）
    //====================
    const SphereCollider& GetCollider() const { return collider_; }
    SphereCollider& GetCollider() { return collider_; }

    /// <summary>
    /// Inspectorでの編集UIを描画
    /// </summary>
    virtual void OnImGuiInspector() = 0;

    /// <summary>
    /// 3Dギズモ操作用：Translateへのポインタを返す（nullptrならギズモ非対応）
    /// 3Dオブジェクトはオーバーライドして transform_.translate のアドレスを返す
    /// </summary>
    virtual Vector3* GetEditableTranslate() { return nullptr; }

    /// <summary>
    /// 2Dギズモ操作用：スクリーン座標(Vector2)へのポインタを返す（nullptrなら非対応）
    /// Spriteなどはオーバーライドして position_ のアドレスを返す
    /// </summary>
    virtual Vector2* GetEditable2DPosition() { return nullptr; }

    // コピー禁止
    IImGuiEditable(const IImGuiEditable&) = delete;
    IImGuiEditable& operator=(const IImGuiEditable&) = delete;

protected:
    EntityTag tag_ = EntityTag::None;
    bool visibleInEditor_ = true;
    SphereCollider collider_{};
};
