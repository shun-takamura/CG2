#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "Vector2.h"
#include "Vector3.h"
#include "Components/EntityTag.h"
#include "Components/SphereCollider.h"
#include "Components/HP.h"
#include "Components/DamageDealer.h"
#include "Components/BulletParams.h"
#include "Components/CarrierParams.h"
#include "Components/ChargeParams.h"

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

    //====================
    // HP（被ダメージ側のコンポーネント。enabled=false でHPシステム非対象）
    //====================
    const HP& GetHP() const { return hp_; }
    HP& GetHP() { return hp_; }

    //====================
    // DamageDealer（与ダメージ側のコンポーネント。攻撃エンティティに付与）
    //====================
    const DamageDealer& GetDamageDealer() const { return damageDealer_; }
    DamageDealer& GetDamageDealer() { return damageDealer_; }

    //====================
    // BulletParams（弾プレハブ用：速度・寿命）
    //====================
    const BulletParams& GetBulletParams() const { return bulletParams_; }
    BulletParams& GetBulletParams() { return bulletParams_; }

    //====================
    // CarrierParams（運び屋プレハブ用：子敵パラメータ）
    //====================
    const CarrierParams& GetCarrierParams() const { return carrierParams_; }
    CarrierParams& GetCarrierParams() { return carrierParams_; }

    //====================
    // ChargeParams（プレイヤープレハブ用：チャージにかかる時間）
    //====================
    const ChargeParams& GetChargeParams() const { return chargeParams_; }
    ChargeParams& GetChargeParams() { return chargeParams_; }

    //====================
    // AttackPower（プレイヤー用の基礎攻撃力。各攻撃の倍率と乗算してダメージを決める）
    // enabled=false なら未設定。
    //====================
    bool  HasAttackPower() const { return hasAttackPower_; }
    void  SetHasAttackPower(bool v) { hasAttackPower_ = v; }
    int   GetAttackPower() const { return attackPower_; }
    void  SetAttackPower(int v) { attackPower_ = v; }

    //====================
    // ScoreValue（敵プレハブ用：撃破時の獲得スコア。Enemy/Boss タグで意味を持つ）
    //====================
    int  GetScoreValue() const { return scoreValue_; }
    void SetScoreValue(int v) { scoreValue_ = v; }

    //====================
    // エフェクトスロット（スロット名 → EffectManager 登録名）
    // 例: charge1, charge2（プレイヤー）/ hit, death（敵）
    // Inspector の DnD で編集。空文字列はそのスロットに未割当を表す。
    //====================
    const std::unordered_map<std::string, std::string>& GetEffects() const { return effects_; }
    std::unordered_map<std::string, std::string>& GetEffects() { return effects_; }
    void SetEffect(const std::string& slot, const std::string& effectName) { effects_[slot] = effectName; }
    std::string FindEffect(const std::string& slot) const {
        auto it = effects_.find(slot);
        return (it != effects_.end()) ? it->second : std::string();
    }

    //====================
    // ObjectID（自動採番、0 は予約＝未割当 / マスク対象外）。
    // ジャスト回避演出など、特定インスタンスをハイライトするためのキーに使う。
    //====================
    uint8_t GetObjectId() const { return objectId_; }

    /// <summary>
    /// Inspectorでの編集UIを描画
    /// </summary>
    virtual void OnImGuiInspector() = 0;

    /// <summary>
    /// 向きをオイラー角（ラジアン）で設定する。対応していない派生はデフォルトで何もしない。
    /// </summary>
    virtual void SetRotate(const Vector3& /*rotate*/) {}

    /// <summary>
    /// 3Dギズモ操作用：Translateへのポインタを返す（nullptrならギズモ非対応）
    /// 3Dオブジェクトはオーバーライドして transform_.translate のアドレスを返す
    /// </summary>
    virtual Vector3* GetEditableTranslate() { return nullptr; }

    /// <summary>
    /// OBB/Capsule コライダーの向き計算用にオーナーの rotate を返す（オイラー角・ラジアン）。
    /// nullptr の場合は単位回転扱い。3Dオブジェクトは override 推奨。
    /// </summary>
    virtual const Vector3* GetEditableRotate() const { return nullptr; }

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
    HP hp_{};
    DamageDealer damageDealer_{};
    BulletParams bulletParams_{};
    CarrierParams carrierParams_{};
    ChargeParams chargeParams_{};
    bool hasAttackPower_ = false;
    int  attackPower_ = 0;
    int  scoreValue_ = 10; // 撃破スコア（Enemy/Boss プレハブで使用、Prefab から上書き）
    std::unordered_map<std::string, std::string> effects_;

    // 0 は予約。コンストラクタで採番される。256 を超えたら 1..255 をラップ（実用上問題ない範囲）
    uint8_t objectId_ = 0;
};
