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

	/// <summary>割り当てが空か</summary>
	/// <returns>デバイスが None なら true</returns>
	bool IsEmpty() const { return device == InputDevice::None; }
	/// <summary>デバイスとコードが同じか</summary>
	/// <param name="other">比べる相手</param>
	/// <returns>同じなら true</returns>
	bool operator==(const PhysicalBinding& other) const {
		return device == other.device && code == other.code;
	}

	/// <summary>
	/// "kb.Space" / "mouse.Left" / "gp.RT" などの文字列に変換する（JSON保存用）
	/// </summary>
	/// <returns>文字列（Empty・名前の表に無いコードなら空文字列）</returns>
	std::string ToString() const;

	/// <summary>
	/// "kb.Space" / "mouse.Left" / "gp.RT" の形の文字列から PhysicalBinding を復元する
	/// </summary>
	/// <param name="text">"接頭辞.名前" の文字列</param>
	/// <returns>復元した割り当て（"." が無い・接頭辞や名前が表に無いと Empty）</returns>
	static PhysicalBinding FromString(std::string_view text);

	//====================
	// 構築ヘルパー
	//====================
	/// <summary>キーボードの割り当てを作る</summary>
	/// <param name="dikCode">DIK_* の値</param>
	/// <returns>割り当て</returns>
	static PhysicalBinding Keyboard(uint32_t dikCode) {
		return PhysicalBinding{ InputDevice::Keyboard, dikCode };
	}
	/// <summary>マウスの割り当てを作る</summary>
	/// <param name="buttonIndex">MouseCode の値</param>
	/// <returns>割り当て</returns>
	static PhysicalBinding Mouse(uint32_t buttonIndex) {
		return PhysicalBinding{ InputDevice::Mouse, buttonIndex };
	}
	/// <summary>ゲームパッドの割り当てを作る</summary>
	/// <param name="code">XINPUT_GAMEPAD_* のビット、または GamepadCode::LT / RT</param>
	/// <returns>割り当て</returns>
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
