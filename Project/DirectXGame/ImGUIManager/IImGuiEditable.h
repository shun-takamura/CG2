#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "Vector2.h"
#include "Vector3.h"

/// <summary>
/// ImGuiで編集可能なオブジェクトのインターフェース（エンジン/エディタの関心のみ）。
/// 生成/破棄時にライフサイクルフックが呼ばれ、ゲーム側が CollisionManager 登録や
/// GameplayComponents の確保/破棄、ImGuiManager 登録を差し込む（依存性の逆転）。
/// 戦闘コンポーネント（HP / Bullet / Melee 等）や EntityTag はこのIFには含めず、
/// ゲーム側の Gameplay::Of(entity) で保持する。
/// </summary>
class IImGuiEditable {
public:
    /// <summary>ObjectID を採番し、生成フックを発火する</summary>
    IImGuiEditable();

    /// <summary>
    /// 自動登録（フック発火）をスキップしたい場合用のタグ。
    /// （EffectEditor 等、独立した Hierarchy で管理する派生で使う）
    /// </summary>
    struct NoAutoRegister {};
    /// <summary>ObjectID だけ採番し、生成フックは発火しない（Hierarchy / Collision に自動登録しない）</summary>
    IImGuiEditable(NoAutoRegister);

    /// <summary>破棄フックを発火する</summary>
    virtual ~IImGuiEditable();

    //====================
    // ライフサイクルフック（依存性の逆転）
    // 生成/破棄時に呼ばれる。ゲーム側が CollisionManager 登録・GameplayComponents 確保/破棄・
    // ImGuiManager 登録などを差し込む。エンジン基底はゲーム/エディタの型を一切知らない。
    //====================
    using LifecycleHook = std::function<void(IImGuiEditable*)>;
    /// <summary>生成／破棄のフックを登録する（以後に作る・消すインスタンスから効く）</summary>
    /// <param name="onConstructed">生成時に呼ぶ関数（空なら呼ばない）</param>
    /// <param name="onDestroyed">破棄時に呼ぶ関数（空なら呼ばない）</param>
    static void SetHooks(LifecycleHook onConstructed, LifecycleHook onDestroyed);

    /// <summary>オブジェクト名を取得</summary>
    /// <returns>名前</returns>
    virtual std::string GetName() const = 0;

    /// <summary>オブジェクト名を設定（Inspectorからの編集用。デフォルトは何もしない）</summary>
    /// <param name="name">新しい名前</param>
    virtual void SetName([[maybe_unused]] const std::string& name) {}

    /// <summary>型名を取得（例: "Object3D", "Sprite"）</summary>
    /// <returns>型名</returns>
    virtual std::string GetTypeName() const = 0;

    //====================
    // Debug時の表示ON/OFF（Hierarchyの目玉アイコン用、Releaseでは効かない）
    //====================
    /// <summary>エディタで表示する設定か（Debug 時だけ効く）</summary>
    /// <returns>表示するなら true</returns>
    bool IsVisibleInEditor() const { return visibleInEditor_; }
    /// <summary>エディタで表示するかを設定する（Debug 時だけ効く）</summary>
    /// <param name="v">表示するなら true</param>
    void SetVisibleInEditor(bool v) { visibleInEditor_ = v; }

    //====================
    // ObjectID（自動採番、0 は予約＝未割当 / マスク対象外）。
    // ジャスト回避演出など、特定インスタンスをハイライトするためのキーに使う。
    //====================
    /// <summary>ObjectID（ハイライトのマスクに書く値）</summary>
    /// <returns>ID（0 は未割当 / マスク対象外）</returns>
    uint8_t GetObjectId() const { return objectId_; }

    /// <summary>
    /// ObjectID を明示的に上書きする（0 は「マスク対象外」の予約値）。
    /// 自動採番されたインスタンス固有 ID ではなく、状態異常アウトライン等で
    /// 「炎=1 / 氷=2」のような意味値を毎フレーム流し込む用途に使う。
    /// </summary>
    /// <param name="id">ID（0 はマスク対象外）</param>
    void SetObjectId(uint8_t id) { objectId_ = id; }

    /// <summary>Inspectorでの編集UIを描画</summary>
    virtual void OnImGuiInspector() = 0;

    /// <summary>向きをオイラー角（ラジアン）で設定する。対応していない派生はデフォルトで何もしない。</summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    virtual void SetRotate([[maybe_unused]] const Vector3& rotate) {}

    /// <summary>大きさを設定する。対応していない派生はデフォルトで何もしない（SetRotate と対）。</summary>
    /// <param name="scale">大きさ</param>
    virtual void SetScale([[maybe_unused]] const Vector3& scale) {}

    /// <summary>3Dギズモ操作用：Translateへのポインタを返す（nullptrならギズモ非対応）</summary>
    /// <returns>translate へのポインタ（nullptr ならギズモ非対応）</returns>
    virtual Vector3* GetEditableTranslate() { return nullptr; }

    /// <summary>OBB/Capsule の向き計算用にオーナーの rotate を返す（nullptr は単位回転扱い）。</summary>
    /// <returns>rotate へのポインタ（nullptr は単位回転扱い）</returns>
    virtual const Vector3* GetEditableRotate() const { return nullptr; }

    /// <summary>2Dギズモ操作用：スクリーン座標(Vector2)へのポインタを返す（nullptrなら非対応）</summary>
    /// <returns>2D 位置へのポインタ（nullptr なら非対応）</returns>
    virtual Vector2* GetEditable2DPosition() { return nullptr; }

    // コピー禁止
    IImGuiEditable(const IImGuiEditable&) = delete;
    IImGuiEditable& operator=(const IImGuiEditable&) = delete;

protected:
    bool visibleInEditor_ = true;

    // 0 は予約。コンストラクタで採番される。256 を超えたら 1..255 をラップ（実用上問題ない範囲）
    uint8_t objectId_ = 0;

private:
    // 生成/破棄フック（ゲーム側が SetHooks で登録）
    static LifecycleHook s_onConstructed;
    static LifecycleHook s_onDestroyed;
};
