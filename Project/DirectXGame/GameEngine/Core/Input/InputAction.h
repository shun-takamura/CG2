#pragma once

#include <array>
#include <vector>

#include "PhysicalBinding.h"

class KeyboardInput;
class MouseInput;
class ControllerInput;

/// <summary>
/// 論理アクション層。
/// 「Action ID（整数）」と「物理バインド最大2つ」のマップを持ち、
/// 物理デバイスを参照して Pressed/Triggered/Released を返す。
/// ゲーム固有のアクション enum は Game 側で定義し、int にキャストして渡す。
/// </summary>
class InputActionMap {
public:
	/// <summary>
	/// 1アクションあたりのバインドスロット数（プライマリ + セカンダリ）。
	/// </summary>
	static constexpr size_t kSlotCount = 2;

	struct Slots {
		std::array<PhysicalBinding, kSlotCount> bindings{};
	};

public:
	/// <summary>
	/// 物理入力デバイスを紐付ける。Update より前に必ず呼ぶ。
	/// </summary>
	void SetDevices(KeyboardInput* keyboard, MouseInput* mouse, ControllerInput* gamepad);

	/// <summary>
	/// 扱うアクション数を確保。
	/// </summary>
	void Resize(size_t actionCount);

	/// <summary>
	/// フレーム開始時の前処理。
	/// 各物理デバイスの Update を呼ぶ直前に1回呼ぶ。
	/// LT/RT のような「自前で前フレ状態を持つ」入力の prev を更新する。
	/// </summary>
	void BeginFrame();

	/// <summary>
	/// アクション数を取得
	/// </summary>
	size_t Size() const { return actions_.size(); }

	/// <summary>
	/// 指定スロットにバインドを設定（Empty を渡せばクリア）
	/// </summary>
	void Bind(int actionId, size_t slot, const PhysicalBinding& binding);

	/// <summary>
	/// すべてのバインドをクリア
	/// </summary>
	void Unbind(int actionId, size_t slot);

	/// <summary>
	/// 指定アクションのバインドを取得
	/// </summary>
	const Slots& GetSlots(int actionId) const;

	//====================
	// 入力問い合わせ（いずれかのスロットが反応していれば true）
	//====================

	/// <summary>
	/// 押下中
	/// </summary>
	bool IsPressed(int actionId) const;

	/// <summary>
	/// この frame で押された瞬間（前フレ未押下 → 今フレ押下）
	/// </summary>
	bool IsTriggered(int actionId) const;

	/// <summary>
	/// この frame で離された瞬間
	/// </summary>
	bool IsReleased(int actionId) const;

	/// <summary>
	/// アクション登録に関係なく、何らかの物理入力（キー/マウスボタン/コントローラーボタン/トリガー）が
	/// この frame で押された瞬間か。タイトルの「Press Any Button」用。
	/// </summary>
	bool AnyInputTriggered() const;

	//====================
	// 物理デバイスへの直接アクセス（リバインドUI用：次に押されたキーをキャプチャするなど）
	//====================
	KeyboardInput* GetKeyboard() const { return keyboard_; }
	MouseInput* GetMouse() const { return mouse_; }
	ControllerInput* GetGamepad() const { return gamepad_; }

private:
	bool IsBindingPressed(const PhysicalBinding& b) const;
	bool IsBindingTriggered(const PhysicalBinding& b) const;
	bool IsBindingReleased(const PhysicalBinding& b) const;

private:
	std::vector<Slots> actions_;
	KeyboardInput* keyboard_ = nullptr;
	MouseInput* mouse_ = nullptr;
	ControllerInput* gamepad_ = nullptr;

	// 空スロット返却用
	static const Slots kEmptySlots;

	// LT/RT のような自前管理が必要なアナログ入力の前フレーム押下状態
	bool prevLTPressed_ = false;
	bool prevRTPressed_ = false;
};
