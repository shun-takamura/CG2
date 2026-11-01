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
/// シーンをまたいで持つシングルトン（EtherState と同じ置き方）。**セーブはしない**（起動している間だけ）。
/// HUB の装備画面・セーブ（20 番 §6）はここを書き換えるだけで差し替わる。今は Debug の ImGui から変える。
/// </summary>
class LoadoutState {
public:
	static LoadoutState* GetInstance();

	static constexpr int kMaxFlightUnitLevel = 3;
	static const char* GetFlightUnitName(FlightUnitKind kind);

	FlightUnitKind GetFlightUnit() const { return flightUnit_; }
	void SetFlightUnit(FlightUnitKind kind);
	/// <summary>系統ごとの段階（1..kMaxFlightUnitLevel）</summary>
	int GetFlightUnitLevel(FlightUnitKind kind) const;
	void SetFlightUnitLevel(FlightUnitKind kind, int level);

	/// <summary>ImGui で付け替えた・段階を変えたら true（呼んだ側が飛行ユニットを付け直す）</summary>
	bool OnImGui();

	//==================== メイン武器（右手、§1） ====================
	/// <summary>付けている武器の id。未設定なら MainWeaponCatalog の既定</summary>
	const std::string& GetMainWeaponId() const;
	/// <summary>一覧に無い id は無視する</summary>
	void SetMainWeaponId(const std::string& id);
	/// <summary>付けている武器の定義（一覧が空なら nullptr＝プレハブの値のまま）</summary>
	const MainWeaponDef* GetMainWeapon() const;
	/// <summary>ImGui で付け替えたら true（呼んだ側が右手のモデルを作り直す）</summary>
	bool OnImGuiMainWeapon();

	//==================== 盾（左手、§2） ====================
	/// <summary>付けている盾の id。未設定なら ShieldCatalog の既定</summary>
	const std::string& GetShieldId() const;
	/// <summary>一覧に無い id は無視する</summary>
	void SetShieldId(const std::string& id);
	/// <summary>付けている盾の定義（一覧が空なら nullptr＝盾なし）</summary>
	const ShieldDef* GetShield() const;
	/// <summary>ImGui で付け替えたら true（呼んだ側が左手のモデルを作り直す）</summary>
	bool OnImGuiShield();

private:
	LoadoutState() = default;
	~LoadoutState() = default;
	LoadoutState(const LoadoutState&) = delete;
	LoadoutState& operator=(const LoadoutState&) = delete;

	FlightUnitKind flightUnit_ = FlightUnitKind::Wings;
	int flightUnitLevels_[static_cast<int>(FlightUnitKind::Count)] = { 1, 1, 1, 1 };
	std::string mainWeaponId_;
	std::string shieldId_;
};
