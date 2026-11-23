#pragma once

#include <string>
#include <vector>
#include "Vector4.h"

/// <summary>アイテム 1 種の定義（Loot.json の items[] の 1 要素。エーテルも同じ形で ether に持つ）</summary>
struct LootItemDef {
	std::string id;           // 集計・必要素材の表が持つ名前（変えない）
	std::string displayName;  // 画面に出す名前
	std::string prefab;       // 落ちている間に見せるプレハブ（当たり判定なし）
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };  // 拾ったときの表示・リザルトの印の色
	float scale = 0.5f;       // 落ちている間の大きさ
	float spinSpeed = 3.0f;   // 回る速さ [rad/s]
	std::string description;  // リザルトの説明欄（"\n" で改行。23_StageClear.md §2）
	int rarity = 1;           // レア度（1〜5。リザルトの説明欄）
	int sellPrice = 0;        // リザルトで売ったときの 1 個あたりのエーテル（0 なら売れない）
};

/// <summary>ドロップの 1 行：chance の確率で count 個落とす</summary>
struct LootRoll {
	std::string itemId;
	float chance = 1.0f;
	int count = 1;
};

/// <summary>敵 1 種（プレハブ名）が落とす物</summary>
struct LootTable {
	std::string prefab;
	int ether = 0;               // 落とすエーテル（難易度の報酬倍率を掛ける前）
	std::vector<LootRoll> rolls;
};

/// <summary>落ちた物の動き（LootDrops が読む）</summary>
struct LootMotionParams {
	float popSpeedMin = 4.0f;    // 倒した所から弾ける速さ [m/s]
	float popSpeedMax = 8.0f;
	float popDamping = 6.0f;     // 弾けた勢いの減り方 [1/s]
	float popTime = 0.3f;        // 弾けてから吸い寄せ始めるまで [s]
	float homeRate = 2.0f;       // 吸い寄せの強さ（残りの距離が毎秒 exp(-rate) になる）[1/s]
	float homeAccel = 10.0f;     // 吸い寄せの強さの伸び [1/s^2]
	float homeMinSpeed = 6.0f;   // 吸い寄せの最低の速さ [m/s]（近くで止まりかけないように）
	float collectRadius = 0.8f;  // これより近づいたら獲得 [m]
	float maxLifetime = 3.0f;    // これを過ぎたら届いていなくても獲得 [s]
	float aimHeight = 1.0f;      // 自機の足元から吸い寄せる先までの高さ [m]
	float appearTime = 0.12f;    // 出たときに大きくなる時間 [s]
	int   maxPieces = 48;        // 同時に落ちている数の上限（越えた分はすぐ獲得）
};

/// <summary>
/// アイテムとドロップの表（22_ItemDrop.md）。Resources/Json/Tuning/Loot.json を読む。
/// 落とす（LootDrops）・集計（LootLedger）・表示（LootPickupLog・リザルト・装備画面）は JSON を直接読まずここを呼ぶ。
/// シングルトンにする理由：JSON を一度だけ読む、読むだけのデータの表のため。
/// </summary>
class LootCatalog {
public:
	/// <summary>唯一のインスタンスを取得する</summary>
	/// <returns>インスタンス</returns>
	static LootCatalog* GetInstance();

	/// <summary>アイテムの種類の数を取得する</summary>
	/// <returns>種類の数</returns>
	int GetItemCount() const { return static_cast<int>(items_.size()); }
	/// <summary>番号でアイテムの定義を取得する</summary>
	/// <param name="index">番号</param>
	/// <returns>アイテムの定義。範囲外なら nullptr</returns>
	const LootItemDef* GetItem(int index) const;
	/// <summary>id でアイテムの定義を引く</summary>
	/// <param name="id">アイテムの id</param>
	/// <returns>アイテムの定義。見つからなければ nullptr</returns>
	const LootItemDef* FindItem(const std::string& id) const;
	/// <summary>id からアイテムの番号を引く</summary>
	/// <param name="id">アイテムの id</param>
	/// <returns>番号。見つからなければ -1</returns>
	int IndexOf(const std::string& id) const;
	/// <summary>エーテルの定義（落ちている間の見た目と表示名）を取得する</summary>
	/// <returns>エーテルの定義</returns>
	const LootItemDef& GetEther() const { return ether_; }

	/// <summary>敵のプレハブ名でドロップの表を引く</summary>
	/// <param name="prefab">敵のプレハブ名</param>
	/// <returns>ドロップの表。表に無ければ nullptr（落とさない）</returns>
	const LootTable* FindTable(const std::string& prefab) const;
	/// <summary>ボスの報酬（撃破でそのまま集計へ入れる）を取得する</summary>
	/// <returns>ボスの報酬の表</returns>
	const LootTable& GetBossTable() const { return boss_; }
	/// <summary>落ちた物の動きの調整値を取得する</summary>
	/// <returns>動きの調整値</returns>
	const LootMotionParams& GetMotion() const { return motion_; }

	/// <summary>Resources/Json/Tuning/Loot.json を読む</summary>
	/// <returns>読めたら true。ファイルが無い・パースに失敗したら false</returns>
	bool Load();

private:
	/// <summary>エーテルの既定の見た目を入れてから表を読み込む</summary>
	LootCatalog();
	~LootCatalog() = default;
	LootCatalog(const LootCatalog&) = delete;
	LootCatalog& operator=(const LootCatalog&) = delete;

	std::vector<LootItemDef> items_;
	LootItemDef ether_;
	std::vector<LootTable> tables_;
	LootTable boss_;
	LootMotionParams motion_;
};
