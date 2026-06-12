#pragma once
#include <string>
#include <unordered_map>

#include "EntityTag.h"
#include "SphereCollider.h"
#include "HP.h"
#include "DamageDealer.h"
#include "BulletParams.h"
#include "MeleeParams.h"
#include "CarrierParams.h"
#include "ChargeParams.h"
#include "PrecisionParams.h"

/// <summary>
/// エンティティ（IImGuiEditable）に紐づくゲームプレイ用コンポーネント群。
/// エンジンの IImGuiEditable から切り離し、ゲーム側で Gameplay::Of(entity) を介して保持する。
/// アクセサ名は旧 IImGuiEditable と同一にして、呼び出し側の移行を受け手の差し替えだけで済ませる。
/// </summary>
struct GameplayComponents {
	//==================== タグ ====================
	EntityTag GetTag() const { return tag; }
	void SetTag(EntityTag t) { tag = t; }

	//==================== コライダー ====================
	const SphereCollider& GetCollider() const { return collider; }
	SphereCollider& GetCollider() { return collider; }

	//==================== HP ====================
	const HP& GetHP() const { return hp; }
	HP& GetHP() { return hp; }

	//==================== DamageDealer ====================
	const DamageDealer& GetDamageDealer() const { return damageDealer; }
	DamageDealer& GetDamageDealer() { return damageDealer; }

	//==================== BulletParams ====================
	const BulletParams& GetBulletParams() const { return bulletParams; }
	BulletParams& GetBulletParams() { return bulletParams; }

	//==================== MeleeParams ====================
	const MeleeParams& GetMeleeParams() const { return meleeParams; }
	MeleeParams& GetMeleeParams() { return meleeParams; }

	//==================== CarrierParams ====================
	const CarrierParams& GetCarrierParams() const { return carrierParams; }
	CarrierParams& GetCarrierParams() { return carrierParams; }

	//==================== ChargeParams ====================
	const ChargeParams& GetChargeParams() const { return chargeParams; }
	ChargeParams& GetChargeParams() { return chargeParams; }

	//==================== PrecisionParams ====================
	const PrecisionParams& GetPrecisionParams() const { return precisionParams; }
	PrecisionParams& GetPrecisionParams() { return precisionParams; }

	//==================== AttackPower ====================
	bool  HasAttackPower() const { return hasAttackPower; }
	void  SetHasAttackPower(bool v) { hasAttackPower = v; }
	int   GetAttackPower() const { return attackPower; }
	void  SetAttackPower(int v) { attackPower = v; }

	//==================== ScoreValue ====================
	int  GetScoreValue() const { return scoreValue; }
	void SetScoreValue(int v) { scoreValue = v; }

	//==================== エフェクトスロット ====================
	const std::unordered_map<std::string, std::string>& GetEffects() const { return effects; }
	std::unordered_map<std::string, std::string>& GetEffects() { return effects; }
	void SetEffect(const std::string& slot, const std::string& effectName) { effects[slot] = effectName; }
	std::string FindEffect(const std::string& slot) const {
		auto it = effects.find(slot);
		return (it != effects.end()) ? it->second : std::string();
	}

	//==================== 弾プレハブスロット ====================
	const std::unordered_map<std::string, std::string>& GetBulletPrefabs() const { return bulletPrefabs; }
	std::unordered_map<std::string, std::string>& GetBulletPrefabs() { return bulletPrefabs; }
	void SetBulletPrefab(const std::string& slot, const std::string& prefabName) { bulletPrefabs[slot] = prefabName; }
	std::string FindBulletPrefab(const std::string& slot) const {
		auto it = bulletPrefabs.find(slot);
		return (it != bulletPrefabs.end()) ? it->second : std::string();
	}

	//==================== 実データ ====================
	EntityTag tag = EntityTag::None;
	SphereCollider collider{};
	HP hp{};
	DamageDealer damageDealer{};
	BulletParams bulletParams{};
	MeleeParams meleeParams{};
	CarrierParams carrierParams{};
	ChargeParams chargeParams{};
	PrecisionParams precisionParams{};
	bool hasAttackPower = false;
	int  attackPower = 0;
	int  scoreValue = 10; // 撃破スコア（Enemy/Boss プレハブで使用、Prefab から上書き）
	std::unordered_map<std::string, std::string> effects;
	std::unordered_map<std::string, std::string> bulletPrefabs;
};
