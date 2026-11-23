#pragma once
#include <string>
#include <unordered_map>

#include "EntityTag.h"
#include "SphereCollider.h"
#include "Vector3.h"
#include "HP.h"
#include "DamageDealer.h"
#include "BulletParams.h"
#include "MeleeParams.h"
#include "CarrierParams.h"
#include "MovementParams.h"
#include "ChargeParams.h"
#include "PrecisionParams.h"
#include "WeaponParams.h"

class IImGuiEditable;

/// <summary>
/// エンティティ（IImGuiEditable）に紐づくゲームプレイ用コンポーネント群。
/// エンジンの IImGuiEditable から切り離し、ゲーム側で Gameplay::Of(entity) を介して保持する。
/// アクセサ名は旧 IImGuiEditable と同一にして、呼び出し側の移行を受け手の差し替えだけで済ませる。
/// プレハブから決まる設定（各 Params・エフェクト・弾プレハブ）は読むだけの取得と Set の組で扱い、
/// 毎フレーム書き換わる実行中の状態（コライダー・HP・DamageDealer）だけ書き換えられる参照を返す。
/// </summary>
class GameplayComponents {
public:
	//==================== タグ ====================
	/// <summary>タグを取得する</summary>
	/// <returns>タグ</returns>
	EntityTag GetTag() const { return tag_; }
	/// <summary>タグを設定する</summary>
	/// <param name="t">タグ</param>
	void SetTag(EntityTag t) { tag_ = t; }

	//==================== プレハブ由来か ====================
	/// <summary>
	/// 生成元のプレハブ名を取得する（InstantiatePrefab で生成された場合に入る）。
	/// シーン保存時にこれを見て "Prefab" エントリとして書き出し、
	/// ロードでプレハブから作り直すことで HP/弾パラメータ等を往復で失わないようにする。
	/// </summary>
	/// <returns>プレハブ名。プレハブ由来でなければ空</returns>
	const std::string& GetPrefabName() const { return prefabName_; }
	/// <summary>生成元のプレハブ名を設定する</summary>
	/// <param name="n">プレハブ名</param>
	void SetPrefabName(const std::string& n) { prefabName_ = n; }

	//==================== ストリーミングのグループ ====================
	/// <summary>シーン JSON の "streamGroup" を取得する（保存で書き戻すために持つ）</summary>
	/// <returns>グループ名。空 = シーン開始時に作った物</returns>
	const std::string& GetStreamGroup() const { return streamGroup_; }
	/// <summary>シーン JSON の "streamGroup" を設定する</summary>
	/// <param name="g">グループ名</param>
	void SetStreamGroup(const std::string& g) { streamGroup_ = g; }

	//==================== コライダー ====================
	/// <summary>コライダーを取得する（読むだけ）</summary>
	/// <returns>コライダー</returns>
	const SphereCollider& GetCollider() const { return collider_; }
	/// <summary>コライダーを取得する（書き換えられる）</summary>
	/// <returns>コライダー</returns>
	SphereCollider& GetCollider() { return collider_; }
	/// <summary>
	/// ゲームが毎フレーム決める判定の中心・軸を設定する（プレハブには保存しない）。
	/// 入っている間は translate＋offset / rotate の代わりに判定・デバッグ描画で使う（自機の前傾・回転順の違いを揃える）
	/// </summary>
	/// <param name="center">判定の中心（ワールド）</param>
	/// <param name="axes">判定の 3 軸（要素数 3 の配列）</param>
	void SetColliderPose(const Vector3& center, const Vector3 axes[3]) {
		hasColliderPose_ = true;
		colliderPoseCenter_ = center;
		for (int i = 0; i < kColliderPoseAxisCount; ++i) colliderPoseAxes_[i] = axes[i];
	}
	/// <summary>判定の姿勢の上書きをやめる（translate＋offset / rotate に戻す）</summary>
	void ClearColliderPose() { hasColliderPose_ = false; }
	/// <summary>判定の姿勢を上書きしているか</summary>
	/// <returns>上書きしていれば true</returns>
	bool HasColliderPose() const { return hasColliderPose_; }
	/// <summary>上書きした判定の中心を取得する</summary>
	/// <returns>判定の中心（ワールド）</returns>
	const Vector3& GetColliderPoseCenter() const { return colliderPoseCenter_; }
	/// <summary>上書きした判定の 3 軸を取得する</summary>
	/// <returns>要素数 3 の配列の先頭</returns>
	const Vector3* GetColliderPoseAxes() const { return colliderPoseAxes_; }

	//==================== HP ====================
	/// <summary>HP を取得する（読むだけ）</summary>
	/// <returns>HP</returns>
	const HP& GetHP() const { return hp_; }
	/// <summary>HP を取得する（書き換えられる）</summary>
	/// <returns>HP</returns>
	HP& GetHP() { return hp_; }

	//==================== ダメージの持ち主 ====================
	/// <summary>
	/// ダメージの持ち主を取得する。ボスの腕・脚のように、受けたダメージを別のエンティティ（本体）へ流す部位に本体が入る。
	/// 「1 発が同じ相手に当たるのは 1 回」の相手はこの持ち主で数える（Gameplay::DamageRootOf）
	/// </summary>
	/// <returns>持ち主。nullptr = 自分が持ち主</returns>
	IImGuiEditable* GetDamageOwner() const { return damageOwner_; }
	/// <summary>ダメージの持ち主を設定する</summary>
	/// <param name="owner">持ち主の本体（nullptr = 自分が持ち主）</param>
	void SetDamageOwner(IImGuiEditable* owner) { damageOwner_ = owner; }

	//==================== DamageDealer ====================
	/// <summary>与ダメージの設定を取得する（読むだけ）</summary>
	/// <returns>DamageDealer</returns>
	const DamageDealer& GetDamageDealer() const { return damageDealer_; }
	/// <summary>与ダメージの設定を取得する（書き換えられる）</summary>
	/// <returns>DamageDealer</returns>
	DamageDealer& GetDamageDealer() { return damageDealer_; }

	//==================== BulletParams ====================
	/// <summary>弾のパラメータを取得する</summary>
	/// <returns>BulletParams</returns>
	const BulletParams& GetBulletParams() const { return bulletParams_; }
	/// <summary>弾のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetBulletParams(const BulletParams& value) { bulletParams_ = value; }

	//==================== MeleeParams ====================
	/// <summary>近接攻撃のパラメータを取得する</summary>
	/// <returns>MeleeParams</returns>
	const MeleeParams& GetMeleeParams() const { return meleeParams_; }
	/// <summary>近接攻撃のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetMeleeParams(const MeleeParams& value) { meleeParams_ = value; }

	//==================== CarrierParams ====================
	/// <summary>運び屋のパラメータを取得する</summary>
	/// <returns>CarrierParams</returns>
	const CarrierParams& GetCarrierParams() const { return carrierParams_; }
	/// <summary>運び屋のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetCarrierParams(const CarrierParams& value) { carrierParams_ = value; }

	//==================== MovementParams ====================
	/// <summary>移動のパラメータを取得する</summary>
	/// <returns>MovementParams</returns>
	const MovementParams& GetMovementParams() const { return movementParams_; }
	/// <summary>移動のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetMovementParams(const MovementParams& value) { movementParams_ = value; }

	//==================== ChargeParams ====================
	/// <summary>チャージのパラメータを取得する</summary>
	/// <returns>ChargeParams</returns>
	const ChargeParams& GetChargeParams() const { return chargeParams_; }
	/// <summary>チャージのパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetChargeParams(const ChargeParams& value) { chargeParams_ = value; }

	//==================== PrecisionParams ====================
	/// <summary>精密射撃のパラメータを取得する</summary>
	/// <returns>PrecisionParams</returns>
	const PrecisionParams& GetPrecisionParams() const { return precisionParams_; }
	/// <summary>精密射撃のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetPrecisionParams(const PrecisionParams& value) { precisionParams_ = value; }

	//==================== WeaponParams ====================
	/// <summary>武器のパラメータを取得する</summary>
	/// <returns>WeaponParams</returns>
	const WeaponParams& GetWeaponParams() const { return weaponParams_; }
	/// <summary>武器のパラメータを設定する</summary>
	/// <param name="value">設定する値</param>
	void SetWeaponParams(const WeaponParams& value) { weaponParams_ = value; }

	//==================== AttackPower ====================
	/// <summary>基礎攻撃力を持つか</summary>
	/// <returns>持つなら true</returns>
	bool  HasAttackPower() const { return hasAttackPower_; }
	/// <summary>基礎攻撃力を持つかを設定する</summary>
	/// <param name="v">持つなら true</param>
	void  SetHasAttackPower(bool v) { hasAttackPower_ = v; }
	/// <summary>基礎攻撃力を取得する</summary>
	/// <returns>基礎攻撃力</returns>
	int   GetAttackPower() const { return attackPower_; }
	/// <summary>基礎攻撃力を設定する</summary>
	/// <param name="v">基礎攻撃力</param>
	void  SetAttackPower(int v) { attackPower_ = v; }

	//==================== ScoreValue ====================
	/// <summary>撃破スコアを取得する</summary>
	/// <returns>撃破スコア</returns>
	int  GetScoreValue() const { return scoreValue_; }
	/// <summary>撃破スコアを設定する</summary>
	/// <param name="v">撃破スコア</param>
	void SetScoreValue(int v) { scoreValue_ = v; }

	//==================== エフェクトスロット ====================
	/// <summary>エフェクトスロットの表を取得する</summary>
	/// <returns>スロット名 → エフェクト名</returns>
	const std::unordered_map<std::string, std::string>& GetEffects() const { return effects_; }
	/// <summary>エフェクトスロットの表をまとめて置き換える</summary>
	/// <param name="value">スロット名 → エフェクト名</param>
	void SetEffects(const std::unordered_map<std::string, std::string>& value) { effects_ = value; }
	/// <summary>エフェクトスロットを 1 つ設定する</summary>
	/// <param name="slot">スロット名（charge1 / hit / death など）</param>
	/// <param name="effectName">EffectManager の登録名</param>
	void SetEffect(const std::string& slot, const std::string& effectName) { effects_[slot] = effectName; }
	/// <summary>エフェクトスロットに割り当てたエフェクト名を引く</summary>
	/// <param name="slot">スロット名</param>
	/// <returns>エフェクト名。無ければ空文字列</returns>
	std::string FindEffect(const std::string& slot) const {
		auto it = effects_.find(slot);
		return (it != effects_.end()) ? it->second : std::string();
	}

	//==================== 弾プレハブスロット ====================
	/// <summary>弾プレハブスロットの表を取得する</summary>
	/// <returns>スロット名 → 弾プレハブ名</returns>
	const std::unordered_map<std::string, std::string>& GetBulletPrefabs() const { return bulletPrefabs_; }
	/// <summary>弾プレハブスロットの表をまとめて置き換える</summary>
	/// <param name="value">スロット名 → 弾プレハブ名</param>
	void SetBulletPrefabs(const std::unordered_map<std::string, std::string>& value) { bulletPrefabs_ = value; }
	/// <summary>弾プレハブスロットを 1 つ設定する</summary>
	/// <param name="slot">スロット名（normal / charge1 / charge2）</param>
	/// <param name="bulletPrefabName">弾プレハブ名</param>
	void SetBulletPrefab(const std::string& slot, const std::string& bulletPrefabName) { bulletPrefabs_[slot] = bulletPrefabName; }
	/// <summary>弾プレハブスロットに割り当てた弾プレハブ名を引く</summary>
	/// <param name="slot">スロット名</param>
	/// <returns>弾プレハブ名。無ければ空文字列</returns>
	std::string FindBulletPrefab(const std::string& slot) const {
		auto it = bulletPrefabs_.find(slot);
		return (it != bulletPrefabs_.end()) ? it->second : std::string();
	}

private:
	//==================== 実データ ====================
	// 判定の姿勢の軸の数（X/Y/Z）
	static constexpr int kColliderPoseAxisCount = 3;
	EntityTag tag_ = EntityTag::None;
	std::string prefabName_;   // 空 = プレハブ由来でない（手置きの Object3D/Animated/Primitive）
	std::string streamGroup_;  // 空 = 区間ごとのストリーミングの対象でない
	SphereCollider collider_{};
	bool    hasColliderPose_ = false;
	Vector3 colliderPoseCenter_{ 0.0f, 0.0f, 0.0f };
	Vector3 colliderPoseAxes_[kColliderPoseAxisCount]{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
	HP hp_{};
	IImGuiEditable* damageOwner_ = nullptr; // nullptr = 自分が持ち主
	DamageDealer damageDealer_{};
	BulletParams bulletParams_{};
	MeleeParams meleeParams_{};
	CarrierParams carrierParams_{};
	MovementParams movementParams_{};
	ChargeParams chargeParams_{};
	PrecisionParams precisionParams_{};
	WeaponParams weaponParams_{};
	bool hasAttackPower_ = false;
	int  attackPower_ = 0;
	int  scoreValue_ = 10; // 撃破スコア（Enemy/Boss プレハブで使用、Prefab から上書き）
	std::unordered_map<std::string, std::string> effects_;
	std::unordered_map<std::string, std::string> bulletPrefabs_;
};
