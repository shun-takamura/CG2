#pragma once

#include <string>

struct MainWeaponDef;
struct ShieldDef;

/// <summary>飛行ユニットの系統（20_Loadout.md §3）。Wings＝初期の羽根（RB の技なし）</summary>
enum class FlightUnitKind : int {
	Wings = 0,
	Assault,   // 攻撃系
	Guard,     // 防御系
	Mobility,  // 機動系
	Count
};

/// <summary>
/// いま付けている装備（20_Loadout.md）。飛行ユニットの系統と系統ごとの段階、メイン武器（id。一覧は MainWeaponCatalog）、盾（id。一覧は ShieldCatalog）。
/// シーンをまたいで持つ（GameSession が 1 つだけ持つ。EtherState と同じ置き方）。**セーブはしない**（起動している間だけ）。
/// HUB の装備画面・セーブ（20 番 §6）はここを書き換えるだけで差し替わる。今は Debug の ImGui から変える。
/// </summary>
class LoadoutState {
public:
	/// <summary>初めの装備（飛行ユニット・メイン武器・盾の既定）で作る</summary>
	LoadoutState() = default;
	~LoadoutState() = default;


	static constexpr int kMaxFlightUnitLevel = 3;
	/// <summary>飛行ユニットの系統の名前（CraftingCatalog などのキー）を取得する</summary>
	/// <param name="kind">系統</param>
	/// <returns>"Wings" / "Assault" / "Guard" / "Mobility"。範囲外なら空文字列</returns>
	static const char* GetFlightUnitName(FlightUnitKind kind);

	/// <summary>付けている飛行ユニットの系統を取得する</summary>
	/// <returns>系統</returns>
	FlightUnitKind GetFlightUnit() const { return flightUnit_; }
	/// <summary>付ける飛行ユニットの系統を設定する（範囲外は無視する）</summary>
	/// <param name="kind">系統</param>
	void SetFlightUnit(FlightUnitKind kind);
	/// <summary>系統ごとの段階を取得する</summary>
	/// <param name="kind">系統</param>
	/// <returns>段階（1..kMaxFlightUnitLevel）。範囲外の系統なら 1</returns>
	int GetFlightUnitLevel(FlightUnitKind kind) const;
	/// <summary>系統ごとの段階を設定する（1..kMaxFlightUnitLevel に収める。範囲外の系統は無視する）</summary>
	/// <param name="kind">系統</param>
	/// <param name="level">段階</param>
	void SetFlightUnitLevel(FlightUnitKind kind, int level);

	/// <summary>飛行ユニットの系統と段階を ImGui で付け替える</summary>
	/// <returns>付け替えた・段階を変えたら true（呼んだ側が飛行ユニットを付け直す）</returns>
	bool OnImGui();

	//==================== メイン武器（右手、§1） ====================
	/// <summary>付けている武器の id を取得する</summary>
	/// <returns>武器の id。未設定なら MainWeaponCatalog の既定</returns>
	const std::string& GetMainWeaponId() const;
	/// <summary>付ける武器を設定する。一覧に無い id は無視する</summary>
	/// <param name="id">武器の id</param>
	void SetMainWeaponId(const std::string& id);
	/// <summary>付けている武器の定義を取得する</summary>
	/// <returns>武器の定義。一覧が空なら nullptr（＝プレハブの値のまま）</returns>
	const MainWeaponDef* GetMainWeapon() const;
	/// <summary>メイン武器を ImGui で付け替える</summary>
	/// <returns>付け替えたら true（呼んだ側が右手のモデルを作り直す）</returns>
	bool OnImGuiMainWeapon();

	//==================== 盾（左手、§2） ====================
	/// <summary>付けている盾の id を取得する</summary>
	/// <returns>盾の id。未設定なら ShieldCatalog の既定</returns>
	const std::string& GetShieldId() const;
	/// <summary>付ける盾を設定する。一覧に無い id は無視する</summary>
	/// <param name="id">盾の id</param>
	void SetShieldId(const std::string& id);
	/// <summary>付けている盾の定義を取得する</summary>
	/// <returns>盾の定義。一覧が空なら nullptr（＝盾なし）</returns>
	const ShieldDef* GetShield() const;
	/// <summary>盾を ImGui で付け替える</summary>
	/// <returns>付け替えたら true（呼んだ側が左手のモデルを作り直す）</returns>
	bool OnImGuiShield();

private:
	LoadoutState(const LoadoutState&) = delete;
	LoadoutState& operator=(const LoadoutState&) = delete;

	FlightUnitKind flightUnit_ = FlightUnitKind::Wings;
	int flightUnitLevels_[static_cast<int>(FlightUnitKind::Count)] = { 1, 1, 1, 1 };
	std::string mainWeaponId_;
	std::string shieldId_;
};
