#pragma once

#include <string_view>

/// <summary>
/// ゲーム固有の論理アクション。
/// InputActionMap には int として渡す（static_cast<int>(Action::Fire) など）。
/// Phase A はデジタル入力のみ（アナログの Move / AimLook は Phase B で追加）。
/// </summary>
enum class Action : int {
	Fire = 0,        // 通常射撃
	PrecisionAim,    // 精密射撃モード
	MeleeStrong,     // 近接攻撃 強
	MeleeWeak,       // 近接攻撃 弱
	Dodge,           // 回避
	Heal,            // ジャスト回避後HP回復
	Ultimate,        // 装備中の必殺技を発動
	FlightSkill,     // 飛行ユニットの技（RB。20_Loadout.md §4）
	LockCamButton,   // ターゲットカメラ / ロックオンカメラ切替（短押し・長押しはシーン側で判別）
	Pause,           // ポーズメニュー
	ViewToggle,      // スタート演出の一人称 ↔ 三人称（16_StageStart.md）
	Launch,          // スタート演出の発進（機体確認が済んで LAUNCH READY のときだけ受け付ける。16_StageStart.md）
	SkipIntro,       // スタート演出のスキップ（長押しで雲の上へ。HUB の降下・StagePlay の降下と機体確認の間。16_StageStart.md）
	MenuConfirm,
	MenuCancel,
	MenuUp,
	MenuDown,
	MenuLeft,
	MenuRight,

	Count
};

/// <summary>
/// アクションの名前（JSON保存/復元・UI表示に使用）
/// </summary>
inline std::string_view GetActionName(Action a) {
	switch (a) {
	case Action::Fire:           return "Fire";
	case Action::PrecisionAim:   return "PrecisionAim";
	case Action::MeleeStrong:    return "MeleeStrong";
	case Action::MeleeWeak:      return "MeleeWeak";
	case Action::Dodge:          return "Dodge";
	case Action::Heal:           return "Heal";
	case Action::Ultimate:       return "Ultimate";
	case Action::FlightSkill:    return "FlightSkill";
	case Action::LockCamButton:  return "LockCamButton";
	case Action::Pause:          return "Pause";
	case Action::ViewToggle:     return "ViewToggle";
	case Action::Launch:         return "Launch";
	case Action::SkipIntro:      return "SkipIntro";
	case Action::MenuConfirm:    return "MenuConfirm";
	case Action::MenuCancel:     return "MenuCancel";
	case Action::MenuUp:         return "MenuUp";
	case Action::MenuDown:       return "MenuDown";
	case Action::MenuLeft:       return "MenuLeft";
	case Action::MenuRight:      return "MenuRight";
	default:                     return "";
	}
}

/// <summary>
/// 名前から Action を逆引き。失敗時は Action::Count を返す。
/// </summary>
inline Action ActionFromName(std::string_view name) {
	for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
		Action a = static_cast<Action>(i);
		if (GetActionName(a) == name) return a;
	}
	return Action::Count;
}
