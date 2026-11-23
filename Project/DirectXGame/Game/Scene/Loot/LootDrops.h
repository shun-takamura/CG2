#pragma once
#include "Vector3.h"
#include "Score/LootLedger.h"

#include <random>
#include <string>
#include <vector>

class GameSession;
class IImGuiEditable;

/// <summary>
/// LootDrops がシーンに頼むこと（見た目の出し入れと、吸い寄せる先）。RailStagePart が IRailStageHost へ取り次ぐ
/// </summary>
class ILootDropsHost {
public:
	virtual ~ILootDropsHost() = default;
	/// <summary>シーンをまたいで残るプレイの状態を取得する（スコア・難易度・手に入れた物など）</summary>
	/// <returns>プレイの状態</returns>
	virtual GameSession* GetGameSession() const = 0;
	/// <summary>プレハブを置く（当たり判定のない見た目だけ）</summary>
	/// <param name="prefab">プレハブの名前</param>
	/// <param name="pos">置く位置（ワールド）</param>
	/// <returns>置いた物。置けなければ nullptr</returns>
	virtual IImGuiEditable* SpawnLootPiece(const std::string& prefab, const Vector3& pos) = 0;
	/// <summary>SpawnLootPiece で置いた物を消す</summary>
	/// <param name="entity">消す物</param>
	virtual void DestroyLootPiece(IImGuiEditable* entity) = 0;
	/// <summary>吸い寄せる先（自機の足元）を返す</summary>
	/// <param name="out">足元のワールド位置の受け取り先</param>
	/// <returns>自機がいなければ false</returns>
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
	/// <summary>シーンへの窓口を受け取る</summary>
	/// <param name="host">シーンへの窓口</param>
	void Initialize(ILootDropsHost* host);

	/// <summary>敵が倒れた所にアイテムとエーテルを落とす。表に無い敵は何も落とさない</summary>
	/// <param name="enemyPrefab">倒れた敵のプレハブの名前</param>
	/// <param name="pos">倒れた位置（ワールド）</param>
	/// <param name="source">拾ったときの出どころ（ボスの部位破壊は PartBreak。リザルトで分けて見せる）</param>
	void DropFrom(const std::string& enemyPrefab, const Vector3& pos, LootSource source = LootSource::Stage);
	/// <summary>落ちている物を弾けさせてから自機へ吸い寄せ、collectRadius まで来たら拾う</summary>
	/// <param name="dt">経過 [秒]</param>
	void Update(float dt);
	/// <summary>落ちている物を全部拾ったことにして消す（フェーズが変わるとき）</summary>
	void CollectAll();
	/// <summary>拾わずに消す（シーク）</summary>
	void Clear();

	/// <summary>落ちている物かを返す（シーンの保存から外す）</summary>
	/// <param name="entity">調べる物</param>
	/// <returns>このクラスが置いた物なら true</returns>
	bool Owns(const IImGuiEditable* entity) const;

private:
	struct Piece {
		IImGuiEditable* entity = nullptr;
		std::string itemId;   // LootLedger の id（エーテルは "Ether"）
		int amount = 1;
		LootSource source = LootSource::Stage;
		Vector3 offset{};     // 吸い寄せる先からのずれ
		Vector3 velocity{};   // 弾けた勢い
		Vector3 rotate{};
		Vector3 spin{};
		float scale = 0.5f;
		float age = 0.0f;
	};

	/// <summary>落ちる物を 1 つ置いて弾けさせる（上限を越えたら見せずに拾ったことにする）</summary>
	/// <param name="itemId">LootLedger の id（エーテルは "Ether"）</param>
	/// <param name="prefab">見た目のプレハブの名前</param>
	/// <param name="amount">数</param>
	/// <param name="scale">大きさ</param>
	/// <param name="spinSpeed">回る速さ [ラジアン/秒]（Y まわり。X はその 0.3〜0.6 倍）</param>
	/// <param name="pos">置く位置（ワールド）</param>
	/// <param name="target">吸い寄せる先（ワールド）</param>
	/// <param name="source">拾ったときの出どころ</param>
	void Spawn(const std::string& itemId, const std::string& prefab, int amount, float scale, float spinSpeed,
		const Vector3& pos, const Vector3& target, LootSource source);
	/// <summary>LootLedger へ足して見た目を消す</summary>
	/// <param name="p">拾う物</param>
	void Collect(Piece& p);
	/// <summary>一様乱数を返す</summary>
	/// <param name="a">下限</param>
	/// <param name="b">上限</param>
	/// <returns>a〜b の値</returns>
	float RandRange(float a, float b);

	ILootDropsHost* host_ = nullptr;
	std::vector<Piece> pieces_;
	std::mt19937 rng_{ 0x10075eedu };
};
