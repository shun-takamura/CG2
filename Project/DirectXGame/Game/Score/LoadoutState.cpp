#include "LoadoutState.h"

#include <algorithm>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

LoadoutState* LoadoutState::GetInstance() {
	static LoadoutState instance;
	return &instance;
}

const char* LoadoutState::GetFlightUnitName(FlightUnitKind kind) {
	switch (kind) {
	case FlightUnitKind::Wings:    return "Wings";
	case FlightUnitKind::Assault:  return "Assault";
	case FlightUnitKind::Guard:    return "Guard";
	case FlightUnitKind::Mobility: return "Mobility";
	default:                       return "";
	}
}

void LoadoutState::SetFlightUnit(FlightUnitKind kind) {
	if (kind < FlightUnitKind::Wings || kind >= FlightUnitKind::Count) return;
	flightUnit_ = kind;
}

int LoadoutState::GetFlightUnitLevel(FlightUnitKind kind) const {
	const int i = static_cast<int>(kind);
	if (i < 0 || i >= static_cast<int>(FlightUnitKind::Count)) return 1;
	return flightUnitLevels_[i];
}

void LoadoutState::SetFlightUnitLevel(FlightUnitKind kind, int level) {
	const int i = static_cast<int>(kind);
	if (i < 0 || i >= static_cast<int>(FlightUnitKind::Count)) return;
	flightUnitLevels_[i] = std::clamp(level, 1, kMaxFlightUnitLevel);
}

bool LoadoutState::OnImGui() {
	bool changed = false;
#ifdef _DEBUG
	int kind = static_cast<int>(flightUnit_);
	const char* names[] = { "Wings (none)", "Assault", "Guard", "Mobility" };
	if (ImGui::Combo("Flight Unit##Loadout", &kind, names, IM_ARRAYSIZE(names))) {
		SetFlightUnit(static_cast<FlightUnitKind>(kind));
		changed = true;
	}
	for (int i = static_cast<int>(FlightUnitKind::Assault); i < static_cast<int>(FlightUnitKind::Count); ++i) {
		int lv = flightUnitLevels_[i];
		const std::string label = std::string("Level ") + names[i] + "##Loadout";
		if (ImGui::SliderInt(label.c_str(), &lv, 1, kMaxFlightUnitLevel)) {
			SetFlightUnitLevel(static_cast<FlightUnitKind>(i), lv);
			changed = true;
		}
	}
#endif
	return changed;
}
