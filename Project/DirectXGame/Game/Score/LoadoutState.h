#pragma once

/// <summary>飛行ユニットの系統（20_Loadout.md §3）。Wings＝初期の羽根（RB の技なし）</summary>
enum class FlightUnitKind : int {
	Wings = 0,
	Assault,   // 攻撃系
	Guard,     // 防御系
	Mobility,  // 機動系
	Count
};

/// <summary>
/// いま付けている装備（20_Loadout.md）。今は飛行ユニットの系統と、系統ごとの段階だけ。
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

private:
	LoadoutState() = default;
	~LoadoutState() = default;
	LoadoutState(const LoadoutState&) = delete;
	LoadoutState& operator=(const LoadoutState&) = delete;

	FlightUnitKind flightUnit_ = FlightUnitKind::Wings;
	int flightUnitLevels_[static_cast<int>(FlightUnitKind::Count)] = { 1, 1, 1, 1 };
};
