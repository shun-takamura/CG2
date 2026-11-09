#pragma once
#include "Vector3.h"

#include <random>
#include <string>
#include <vector>

class IImGuiEditable;

/// <summary>
/// LootDrops がシーンに頼むこと（見た目の出し入れと、吸い寄せる先）。RailStagePart が IRailStageHost へ取り次ぐ
/// </summary>
class ILootDropsHost {
public:
	virtual ~ILootDropsHost() = default;
	/// <summary>プレハブを置く（当たり判定のない見た目だけ）</summary>
	virtual IImGuiEditable* SpawnLootPiece(const std::string& prefab, const Vector3& pos) = 0;
	virtual void DestroyLootPiece(IImGuiEditable* entity) = 0;
	/// <summary>自機の足元。自機がいなければ false</summary>
	virtual bool GetLootCollectPoint(Vector3& out) const = 0;
};

/// <summary>
/// 敵を倒した所にアイテムとエーテルを落とし、自機へ吸い寄せて拾わせる（22_ItemDrop.md）。
///   1. 倒した所から弾ける（popTime 秒）
///   2. 自機へ加速しながら寄る（残りの距離が exp(-rate) で縮み、rate は時間とともに上がる）
///   3. collectRadius まで来たら LootLedger へ足して消す
/// 位置は「自機から見たずれ」で持つので、レールで自機が進んでも画面から置いていかれない。
/// 何をどれだけ落とすか・動きの数字は LootCatalog（Loot.json）。ObstacleDebris と同じく RailStagePart が持つ。
/// </summary>
class LootDrops {
public:
	void Initialize(ILootDropsHost* host);

	/// <summary>enemyPrefab の敵が pos で倒れた。表に無い敵は何も落とさない</summary>
	void DropFrom(const std::string& enemyPrefab, const Vector3& pos);
	void Update(float dt);
	/// <summary>落ちている物を全部拾ったことにして消す（フェーズが変わるとき）</summary>
	void CollectAll();
	/// <summary>拾わずに消す（シーク）</summary>
	void Clear();

	/// <summary>落ちている物か（シーンの保存から外す）</summary>
	bool Owns(const IImGuiEditable* entity) const;

private:
	struct Piece {
		IImGuiEditable* entity = nullptr;
		std::string itemId;   // LootLedger の id（エーテルは "Ether"）
		int amount = 1;
		Vector3 offset{};     // 吸い寄せる先からのずれ
		Vector3 velocity{};   // 弾けた勢い
		Vector3 rotate{};
		Vector3 spin{};
		float scale = 0.5f;
		float age = 0.0f;
	};

	void Spawn(const std::string& itemId, const std::string& prefab, int amount, float scale, float spinSpeed,
		const Vector3& pos, const Vector3& target);
	void Collect(Piece& p);
	float RandRange(float a, float b);

	ILootDropsHost* host_ = nullptr;
	std::vector<Piece> pieces_;
	std::mt19937 rng_{ 0x10075eedu };
};
