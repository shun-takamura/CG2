#pragma once

#include <string>
#include <vector>
#include "Vector3.h"
#include "Effect/BeamBladeAppearance.h"

/// <summary>メイン武器の弾 1 種類ぶんの倍率（基準は弾プレハブの BulletParams・コライダー）</summary>
struct MainWeaponShot {
	float speedScale = 1.0f;
	float sizeScale = 1.0f;   // 当たり判定と見た目の両方
	float homingScale = 1.0f; // 軽ホーミング・強ホーミングの両方
};

/// <summary>メイン武器 1 本ぶんの定義（MainWeapon.json の weapons[] の 1 要素）</summary>
struct MainWeaponDef {
	std::string id;           // セーブ・LoadoutState が持つ名前（変えない）
	std::string displayName;  // 装備画面に出す名前
	std::string description;  // 装備画面に出す説明文
	int   attackBonus = 0;    // プレハブの攻撃力に足す（射撃・近接で分けない）
	float fireRate = 0.3f;    // 通常弾の連射間隔 [sec]
	float chargeStage1Time = 3.0f;
	float chargeStage2Time = 6.0f;
	MainWeaponShot normal;
	MainWeaponShot charge;    // チャージ 1・2 段目
	// 右手のモデル。modelFile が空ならプレハブの weapon のまま
	std::string modelDir;
	std::string modelFile;
	Vector3 gripTranslate{ 0.0f, 0.0f, 0.0f };
	Vector3 gripRotate{ 0.0f, 0.0f, 0.0f };
	Vector3 gripScale{ 1.0f, 1.0f, 1.0f };
	// 近接のときだけ出すビームの刃（BeamBladeRenderer）。位置・向きは武器のモデルのローカル（銃口が +Z）
	bool blade = false;
	Vector3 bladeOffset{ 0.0f, 0.0f, 0.0f };
	Vector3 bladeRotate{ 0.0f, 0.0f, 0.0f };
	Vector3 bladeScale{ 1.0f, 1.0f, 1.0f };  // 刃全体の大きさ（形の値に掛ける）
	BeamBladeAppearance bladeLook;
	int price = 0;            // 購入を入れるときのための予約（今は使わない）

	/// <summary>チャージ段階（playerChargeLevel_：-1/0＝通常、1 以上＝チャージ）に合う弾の倍率</summary>
	const MainWeaponShot& ShotFor(float chargeLevel) const { return chargeLevel >= 1.0f ? charge : normal; }
};

/// <summary>
/// メイン武器の一覧（20_Loadout.md §1）。Resources/Json/Tuning/MainWeapon.json を読む。
/// 装備画面は JSON を直接読まず、ここの GetCount / GetDef を呼ぶ。付けている武器は LoadoutState が id で持つ。
/// </summary>
class MainWeaponCatalog {
public:
	static MainWeaponCatalog* GetInstance();

	int GetCount() const { return static_cast<int>(weapons_.size()); }
	/// <summary>範囲外なら nullptr</summary>
	const MainWeaponDef* GetDef(int index) const;
	/// <summary>Debug の調整用（握り・刃を ImGui で触る）</summary>
	MainWeaponDef* GetDefMutable(int index);
	const MainWeaponDef* Find(const std::string& id) const;
	/// <summary>見つからなければ -1</summary>
	int IndexOf(const std::string& id) const;
	/// <summary>最初に付けている武器（JSON の default。無ければ先頭）</summary>
	const std::string& GetDefaultId() const { return defaultId_; }

	bool Load();
	/// <summary>今の値を MainWeapon.json へ書く（Debug の握り・刃の調整用）</summary>
	bool Save() const;

private:
	MainWeaponCatalog();
	~MainWeaponCatalog() = default;
	MainWeaponCatalog(const MainWeaponCatalog&) = delete;
	MainWeaponCatalog& operator=(const MainWeaponCatalog&) = delete;

	std::vector<MainWeaponDef> weapons_;
	std::string defaultId_;
};
