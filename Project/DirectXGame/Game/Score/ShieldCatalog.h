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

	/// <summary>軽減を掛けた被ダメージ（0 より大きいダメージは 1 未満にしない）</summary>
	int ReduceDamage(int damage) const;
};

/// <summary>
/// 盾の一覧（20_Loadout.md §2）。Resources/Json/Tuning/Shield.json を読む。
/// 装備画面は JSON を直接読まず、ここの GetCount / GetDef / GetStatLines を呼ぶ。付けている盾は LoadoutState が id で持つ。
/// </summary>
class ShieldCatalog {
public:
	static ShieldCatalog* GetInstance();

	int GetCount() const { return static_cast<int>(shields_.size()); }
	/// <summary>範囲外なら nullptr</summary>
	const ShieldDef* GetDef(int index) const;
	const ShieldDef* Find(const std::string& id) const;
	/// <summary>見つからなければ -1</summary>
	int IndexOf(const std::string& id) const;
	/// <summary>最初に付けている盾（JSON の default。無ければ先頭）</summary>
	const std::string& GetDefaultId() const { return defaultId_; }

	/// <summary>装備画面の右下に出す性能（HP 上限・被ダメージ軽減・移動速度の 3 行。差が分かるよう 0 の行も出す）</summary>
	static std::vector<ShieldStatLine> GetStatLines(const ShieldDef& def);

	bool Load();
	/// <summary>Debug の ImGui で握り（grip）を直したとき用。全体を書き戻す</summary>
	bool Save() const;
	/// <summary>Debug の ImGui から握りを直す用</summary>
	ShieldDef* GetDefMutable(int index);

private:
	ShieldCatalog();
	~ShieldCatalog() = default;
	ShieldCatalog(const ShieldCatalog&) = delete;
	ShieldCatalog& operator=(const ShieldCatalog&) = delete;

	std::vector<ShieldDef> shields_;
	std::string defaultId_;
};
