#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// <summary>
/// 入力デバイス種別
/// </summary>
enum class InputDevice : uint8_t {
	None = 0,
	Keyboard,
	Mouse,
	Gamepad,
};

/// <summary>
/// 1つの物理入力（キー/ボタン）の表現。
/// device + code の組み合わせで一意。code の解釈は device に依存する：
///   - Keyboard: DIK_* の値（BYTE 範囲）
///   - Mouse:    MouseInput::Button の値
///   - Gamepad:  XINPUT_GAMEPAD_* のビットマスク、または LT/RT 用の擬似コード
/// </summary>
struct PhysicalBinding {
	InputDevice device = InputDevice::None;
	uint32_t code = 0;

	bool IsEmpty() const { return device == InputDevice::None; }
	bool operator==(const PhysicalBinding& other) const {
		return device == other.device && code == other.code;
	}

	/// <summary>
	/// "kb.Space" / "mouse.Left" / "gp.RT" などの文字列に変換（JSON保存用）。
	/// Empty なら空文字列を返す。
	/// </summary>
	std::string ToString() const;

	/// <summary>
	/// 文字列から PhysicalBinding を復元。失敗時は Empty を返す。
	/// </summary>
	static PhysicalBinding FromString(std::string_view text);

	//====================
	// 構築ヘルパー
	//====================
	static PhysicalBinding Keyboard(uint32_t dikCode) {
		return PhysicalBinding{ InputDevice::Keyboard, dikCode };
	}
	static PhysicalBinding Mouse(uint32_t buttonIndex) {
		return PhysicalBinding{ InputDevice::Mouse, buttonIndex };
	}
	static PhysicalBinding Gamepad(uint32_t code) {
		return PhysicalBinding{ InputDevice::Gamepad, code };
	}
};

//====================
// Gamepad の擬似コード（XInputビットマスク + LT/RT 用の独自ビット）
//====================
namespace GamepadCode {
	// XInput のビットマスクをそのまま使う（A/B/X/Y/DPad/Start/Back/L3/R3/LB/RB）
	// それらと衝突しない上位ビットに LT/RT を定義する
	inline constexpr uint32_t LT = 0x10000;
	inline constexpr uint32_t RT = 0x20000;
}

//====================
// Mouse のボタンインデックス
//====================
namespace MouseCode {
	inline constexpr uint32_t Left = 0;
	inline constexpr uint32_t Right = 1;
	inline constexpr uint32_t Middle = 2;
	inline constexpr uint32_t Button4 = 3;
}
