#pragma once

#include <string>
#include <vector>
#include "Vector3.h"
#include "Effect/BeamShieldAppearance.h"

/// <summary>装備画面に出す性能の 1 行（例：label "HP 上限" / value "+40"）。UTF-8</summary>
struct ShieldStatLine {
	std::string label;
	std::string value;
};

/// <summary>盾 1 つぶんの定義（Shield.json の shields[] の 1 要素）</summary>
struct ShieldDef {
	std::string id;           // セーブ・LoadoutState が持つ名前（変えない）
	std::string displayName;  // 装備画面に出す名前
	std::string description;  // 装備画面に出す説明文
	int   maxHpAdd = 0;         // プレハブの HP 上限に足す（飛行ユニットの補正を掛ける前）
	float damageCut = 0.0f;     // 通常の被弾の軽減（0.2＝20% 減らす）
	float moveSpeedScale = 1.0f; // 移動速度の倍率（0.9＝−10%）
	// 左手のモデル。modelFile が空なら描かない
	std::string modelDir;
	std::string modelFile;
	std::string bone = "mixamorig:LeftHand";
	Vector3 gripTranslate{ 0.0f, 0.0f, 0.0f };
	Vector3 gripRotate{ 0.0f, 0.0f, 0.0f };
	Vector3 gripScale{ 1.0f, 1.0f, 1.0f };
	// ビームシールド（モデルは発生装置だけで、本体は BeamShieldRenderer が描く）。beam が false なら出さない
	bool beam = false;
	Vector3 beamOffset{ 0.0f, 0.0f, 0.0f };   // 発生装置の位置（盾のモデルのローカル。板は XY 面、+Z が正面）
	BeamShieldAppearance beamLook;

	/// <summary>軽減を掛けた被ダメージを求める（0 より大きいダメージは 1 未満にしない）</summary>
	/// <param name="damage">元のダメージ</param>
	/// <returns>軽減後のダメージ。0 以下・軽減なしなら元のまま</returns>
	int ReduceDamage(int damage) const;
};

/// <summary>
/// 盾の一覧（20_Loadout.md §2）。Resources/Json/Tuning/Shield.json を読む。
/// 装備画面は JSON を直接読まず、ここの GetCount / GetDef / GetStatLines を呼ぶ。付けている盾は LoadoutState が id で持つ。
/// シングルトンにする理由：JSON を一度だけ読む、読むだけのデータの表のため。
/// </summary>
class ShieldCatalog {
public:
	/// <summary>唯一のインスタンスを取得する</summary>
	/// <returns>インスタンス</returns>
	static ShieldCatalog* GetInstance();

	/// <summary>盾の数を取得する</summary>
	/// <returns>盾の数</returns>
	int GetCount() const { return static_cast<int>(shields_.size()); }
	/// <summary>番号で盾の定義を取得する</summary>
	/// <param name="index">番号</param>
	/// <returns>盾の定義。範囲外なら nullptr</returns>
	const ShieldDef* GetDef(int index) const;
	/// <summary>id で盾の定義を引く</summary>
	/// <param name="id">盾の id</param>
	/// <returns>盾の定義。見つからなければ nullptr</returns>
	const ShieldDef* Find(const std::string& id) const;
	/// <summary>id から盾の番号を引く</summary>
	/// <param name="id">盾の id</param>
	/// <returns>番号。見つからなければ -1</returns>
	int IndexOf(const std::string& id) const;
	/// <summary>最初に付けている盾の id を取得する</summary>
	/// <returns>JSON の default。無ければ先頭の盾の id</returns>
	const std::string& GetDefaultId() const { return defaultId_; }

	/// <summary>装備画面の右下に出す性能（HP 上限・被ダメージ軽減・移動速度の 3 行。差が分かるよう 0 の行も出す）</summary>
	/// <param name="def">盾の定義</param>
	/// <returns>性能の 3 行</returns>
	static std::vector<ShieldStatLine> GetStatLines(const ShieldDef& def);

	/// <summary>Resources/Json/Tuning/Shield.json を読む</summary>
	/// <returns>読めたら true</returns>
	bool Load();
	/// <summary>Debug の ImGui で握り（grip）を直したとき用。全体を書き戻す</summary>
	/// <returns>書けたら true</returns>
	bool Save() const;
	/// <summary>番号で盾の定義を書き換えられる形で取得する（Debug の ImGui から握りを直す用）</summary>
	/// <param name="index">番号</param>
	/// <returns>盾の定義。範囲外なら nullptr</returns>
	ShieldDef* GetDefMutable(int index);

private:
	/// <summary>一覧を読み込む</summary>
	ShieldCatalog();
	~ShieldCatalog() = default;
	ShieldCatalog(const ShieldCatalog&) = delete;
	ShieldCatalog& operator=(const ShieldCatalog&) = delete;

	std::vector<ShieldDef> shields_;
	std::string defaultId_;
};
