#include "LoadoutState.h"

#include "MainWeaponCatalog.h"
#include "ShieldCatalog.h"
#include "Effect/BeamShieldRenderer.h"
#include "Effect/BeamBladeRenderer.h"

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

const std::string& LoadoutState::GetMainWeaponId() const {
	return mainWeaponId_.empty() ? MainWeaponCatalog::GetInstance()->GetDefaultId() : mainWeaponId_;
}

void LoadoutState::SetMainWeaponId(const std::string& id) {
	if (MainWeaponCatalog::GetInstance()->IndexOf(id) < 0) return;
	mainWeaponId_ = id;
}

const MainWeaponDef* LoadoutState::GetMainWeapon() const {
	return MainWeaponCatalog::GetInstance()->Find(GetMainWeaponId());
}

bool LoadoutState::OnImGuiMainWeapon() {
	bool changed = false;
#ifdef _DEBUG
	MainWeaponCatalog* catalog = MainWeaponCatalog::GetInstance();
	const int current = catalog->IndexOf(GetMainWeaponId());
	const MainWeaponDef* cur = catalog->GetDef(current);
	if (ImGui::BeginCombo("Main Weapon##Loadout", cur ? cur->id.c_str() : "(none)")) {
		for (int i = 0; i < catalog->GetCount(); ++i) {
			const MainWeaponDef* d = catalog->GetDef(i);
			if (ImGui::Selectable(d->id.c_str(), i == current) && i != current) {
				SetMainWeaponId(d->id);
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Reload MainWeapon.json##Loadout")) {
		catalog->Load();
		changed = true;
	}
	if (const MainWeaponDef* d = GetMainWeapon()) {
		ImGui::Text("Attack +%d  FireRate %.2fs  Charge %.1f / %.1fs", d->attackBonus, d->fireRate,
			d->chargeStage1Time, d->chargeStage2Time);
		ImGui::Text("Normal: speed x%.2f size x%.2f homing x%.2f", d->normal.speedScale, d->normal.sizeScale, d->normal.homingScale);
		ImGui::Text("Charge: speed x%.2f size x%.2f homing x%.2f", d->charge.speedScale, d->charge.sizeScale, d->charge.homingScale);
	}
	if (MainWeaponDef* d = catalog->GetDefMutable(catalog->IndexOf(GetMainWeaponId()))) {
		// 右手の握り（モデルの置き方を合わせる用。Save で MainWeapon.json へ）
		ImGui::DragFloat3("Grip Translate##MainWeapon", &d->gripTranslate.x, 0.01f);
		ImGui::DragFloat3("Grip Rotate##MainWeapon", &d->gripRotate.x, 0.01f);
		ImGui::DragFloat3("Grip Scale##MainWeapon", &d->gripScale.x, 0.01f, 0.0f, 10.0f);
		// ビームの刃（位置・向き・大きさは武器のモデルのローカル。銃口が +Z、刃は +Z へ伸び、幅は Y）
		ImGui::SeparatorText("Blade");
		ImGui::Checkbox("Has Blade##MainWeapon", &d->blade);
		ImGui::DragFloat3("Blade Offset##MainWeapon", &d->bladeOffset.x, 0.005f);
		ImGui::DragFloat3("Blade Rotate##MainWeapon", &d->bladeRotate.x, 0.01f);
		ImGui::DragFloat3("Blade Scale##MainWeapon", &d->bladeScale.x, 0.01f, 0.01f, 20.0f);
		ImGui::DragFloat("Blade Length##MainWeapon", &d->bladeLook.length, 0.01f, 0.05f, 10.0f);
		ImGui::DragFloat("Blade Half Width##MainWeapon", &d->bladeLook.halfWidth, 0.002f, 0.005f, 2.0f);
		if (ImGui::TreeNode("Blade Look (shape / color)##MainWeapon")) {
			BeamBladeRenderer::EditAppearance(d->bladeLook);
			ImGui::TreePop();
		}
		if (ImGui::Button("Save MainWeapon.json##Loadout")) catalog->Save();
	}
#endif
	return changed;
}

const std::string& LoadoutState::GetShieldId() const {
	return shieldId_.empty() ? ShieldCatalog::GetInstance()->GetDefaultId() : shieldId_;
}

void LoadoutState::SetShieldId(const std::string& id) {
	if (ShieldCatalog::GetInstance()->IndexOf(id) < 0) return;
	shieldId_ = id;
}

const ShieldDef* LoadoutState::GetShield() const {
	return ShieldCatalog::GetInstance()->Find(GetShieldId());
}

bool LoadoutState::OnImGuiShield() {
	bool changed = false;
#ifdef _DEBUG
	ShieldCatalog* catalog = ShieldCatalog::GetInstance();
	const int current = catalog->IndexOf(GetShieldId());
	const ShieldDef* cur = catalog->GetDef(current);
	if (ImGui::BeginCombo("Shield##Loadout", cur ? cur->id.c_str() : "(none)")) {
		for (int i = 0; i < catalog->GetCount(); ++i) {
			const ShieldDef* d = catalog->GetDef(i);
			if (ImGui::Selectable(d->id.c_str(), i == current) && i != current) {
				SetShieldId(d->id);
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Reload Shield.json##Loadout")) {
		catalog->Load();
		changed = true;
	}
	if (ShieldDef* d = catalog->GetDefMutable(catalog->IndexOf(GetShieldId()))) {
		ImGui::Text("MaxHP +%d  DamageCut %.0f%%  Move x%.2f", d->maxHpAdd, d->damageCut * 100.0f, d->moveSpeedScale);
		// 左手の握り（仮モデルの置き方を合わせる用。Save で Shield.json へ）
		ImGui::DragFloat3("Grip Translate##Shield", &d->gripTranslate.x, 0.01f);
		ImGui::DragFloat3("Grip Rotate##Shield", &d->gripRotate.x, 0.01f);
		ImGui::DragFloat3("Grip Scale##Shield", &d->gripScale.x, 0.01f, 0.0f, 10.0f);
		if (d->beam && ImGui::TreeNode("Beam##Shield")) {
			ImGui::DragFloat3("Beam Offset##Shield", &d->beamOffset.x, 0.005f);
			BeamShieldRenderer::EditAppearance(d->beamLook);
			ImGui::TreePop();
		}
		if (ImGui::Button("Save Shield.json##Loadout")) catalog->Save();
	}
#endif
	return changed;
}
