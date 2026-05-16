#include "PhysicalBinding.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Windows.h>
#include <XInput.h>

#include <array>

namespace {

	struct NameCodePair {
		std::string_view name;
		uint32_t code;
	};

	// ----- Keyboard: 文字列 <-> DIK 値 -----
	// 必要十分な範囲だけ列挙。足りなくなったらここに追加するだけで増える。
	const NameCodePair kKeyboardTable[] = {
		// 英字
		{"A", DIK_A}, {"B", DIK_B}, {"C", DIK_C}, {"D", DIK_D}, {"E", DIK_E},
		{"F", DIK_F}, {"G", DIK_G}, {"H", DIK_H}, {"I", DIK_I}, {"J", DIK_J},
		{"K", DIK_K}, {"L", DIK_L}, {"M", DIK_M}, {"N", DIK_N}, {"O", DIK_O},
		{"P", DIK_P}, {"Q", DIK_Q}, {"R", DIK_R}, {"S", DIK_S}, {"T", DIK_T},
		{"U", DIK_U}, {"V", DIK_V}, {"W", DIK_W}, {"X", DIK_X}, {"Y", DIK_Y},
		{"Z", DIK_Z},
		// 数字
		{"0", DIK_0}, {"1", DIK_1}, {"2", DIK_2}, {"3", DIK_3}, {"4", DIK_4},
		{"5", DIK_5}, {"6", DIK_6}, {"7", DIK_7}, {"8", DIK_8}, {"9", DIK_9},
		// ファンクション
		{"F1", DIK_F1}, {"F2", DIK_F2}, {"F3", DIK_F3}, {"F4", DIK_F4},
		{"F5", DIK_F5}, {"F6", DIK_F6}, {"F7", DIK_F7}, {"F8", DIK_F8},
		{"F9", DIK_F9}, {"F10", DIK_F10}, {"F11", DIK_F11}, {"F12", DIK_F12},
		// 制御系
		{"Space", DIK_SPACE},
		{"Enter", DIK_RETURN},
		{"Escape", DIK_ESCAPE},
		{"BackSpace", DIK_BACK},
		{"Tab", DIK_TAB},
		{"LShift", DIK_LSHIFT}, {"RShift", DIK_RSHIFT},
		{"LCtrl", DIK_LCONTROL}, {"RCtrl", DIK_RCONTROL},
		{"LAlt", DIK_LMENU}, {"RAlt", DIK_RMENU},
		// 矢印
		{"Up", DIK_UP}, {"Down", DIK_DOWN},
		{"Left", DIK_LEFT}, {"Right", DIK_RIGHT},
		// 記号
		{"Comma", DIK_COMMA},
		{"Period", DIK_PERIOD},
		{"Slash", DIK_SLASH},
		{"Semicolon", DIK_SEMICOLON},
		{"Apostrophe", DIK_APOSTROPHE},
		{"LBracket", DIK_LBRACKET},
		{"RBracket", DIK_RBRACKET},
		{"Minus", DIK_MINUS},
		{"Equals", DIK_EQUALS},
	};

	// ----- Mouse: 文字列 <-> MouseCode -----
	const NameCodePair kMouseTable[] = {
		{"Left", MouseCode::Left},
		{"Right", MouseCode::Right},
		{"Middle", MouseCode::Middle},
		{"Button4", MouseCode::Button4},
	};

	// ----- Gamepad: 文字列 <-> XInputビット or 独自コード -----
	const NameCodePair kGamepadTable[] = {
		{"A", XINPUT_GAMEPAD_A},
		{"B", XINPUT_GAMEPAD_B},
		{"X", XINPUT_GAMEPAD_X},
		{"Y", XINPUT_GAMEPAD_Y},
		{"LB", XINPUT_GAMEPAD_LEFT_SHOULDER},
		{"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
		{"LT", GamepadCode::LT},
		{"RT", GamepadCode::RT},
		{"Back", XINPUT_GAMEPAD_BACK},
		{"Start", XINPUT_GAMEPAD_START},
		{"L3", XINPUT_GAMEPAD_LEFT_THUMB},
		{"R3", XINPUT_GAMEPAD_RIGHT_THUMB},
		{"DPadUp", XINPUT_GAMEPAD_DPAD_UP},
		{"DPadDown", XINPUT_GAMEPAD_DPAD_DOWN},
		{"DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT},
		{"DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT},
	};

	std::string_view FindName(const NameCodePair* table, size_t count, uint32_t code) {
		for (size_t i = 0; i < count; ++i) {
			if (table[i].code == code) return table[i].name;
		}
		return {};
	}

	bool FindCode(const NameCodePair* table, size_t count, std::string_view name, uint32_t& outCode) {
		for (size_t i = 0; i < count; ++i) {
			if (table[i].name == name) {
				outCode = table[i].code;
				return true;
			}
		}
		return false;
	}

} // namespace

std::string PhysicalBinding::ToString() const {
	switch (device) {
	case InputDevice::Keyboard: {
		auto name = FindName(kKeyboardTable, std::size(kKeyboardTable), code);
		if (name.empty()) return {};
		return std::string("kb.") + std::string(name);
	}
	case InputDevice::Mouse: {
		auto name = FindName(kMouseTable, std::size(kMouseTable), code);
		if (name.empty()) return {};
		return std::string("mouse.") + std::string(name);
	}
	case InputDevice::Gamepad: {
		auto name = FindName(kGamepadTable, std::size(kGamepadTable), code);
		if (name.empty()) return {};
		return std::string("gp.") + std::string(name);
	}
	default:
		return {};
	}
}

PhysicalBinding PhysicalBinding::FromString(std::string_view text) {
	auto dot = text.find('.');
	if (dot == std::string_view::npos) return {};
	auto prefix = text.substr(0, dot);
	auto name = text.substr(dot + 1);

	uint32_t code = 0;
	if (prefix == "kb") {
		if (FindCode(kKeyboardTable, std::size(kKeyboardTable), name, code)) {
			return PhysicalBinding::Keyboard(code);
		}
	} else if (prefix == "mouse") {
		if (FindCode(kMouseTable, std::size(kMouseTable), name, code)) {
			return PhysicalBinding::Mouse(code);
		}
	} else if (prefix == "gp") {
		if (FindCode(kGamepadTable, std::size(kGamepadTable), name, code)) {
			return PhysicalBinding::Gamepad(code);
		}
	}
	return {};
}
