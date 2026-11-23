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
	/// <param name="keyboard">キーボード</param>
	/// <param name="mouse">マウス</param>
	/// <param name="gamepad">コントローラー</param>
	void SetDevices(KeyboardInput* keyboard, MouseInput* mouse, ControllerInput* gamepad);

	/// <summary>
	/// 扱うアクション数を確保する（今のバインドはすべて消える）
	/// </summary>
	/// <param name="actionCount">アクション数</param>
	void Resize(size_t actionCount);

	/// <summary>
	/// フレーム開始時の前処理。
	/// 各物理デバイスの Update を呼ぶ直前に1回呼ぶ。
	/// LT/RT のような「自前で前フレ状態を持つ」入力の prev を更新する。
	/// </summary>
	void BeginFrame();

	/// <summary>扱うアクション数</summary>
	/// <returns>アクション数</returns>
	size_t Size() const { return actions_.size(); }

	/// <summary>
	/// 指定スロットにバインドを設定する（Empty を渡せばクリア。範囲外の ID・スロットは無視）
	/// </summary>
	/// <param name="actionId">アクション ID</param>
	/// <param name="slot">スロット（0=プライマリ, 1=セカンダリ）</param>
	/// <param name="binding">割り当てる物理入力</param>
	void Bind(int actionId, size_t slot, const PhysicalBinding& binding);

	/// <summary>指定スロットのバインドをクリアする（範囲外の ID・スロットは無視）</summary>
	/// <param name="actionId">アクション ID</param>
	/// <param name="slot">スロット（0=プライマリ, 1=セカンダリ）</param>
	void Unbind(int actionId, size_t slot);

	/// <summary>
	/// 指定アクションのバインドを取得する
	/// </summary>
	/// <param name="actionId">アクション ID</param>
	/// <returns>バインド（範囲外の ID なら空のスロット）</returns>
	const Slots& GetSlots(int actionId) const;

	//====================
	// 入力問い合わせ（いずれかのスロットが反応していれば true）
	//====================

	/// <summary>押下中か</summary>
	/// <param name="actionId">アクション ID</param>
	/// <returns>どれかのスロットが押下中なら true（範囲外の ID は false）</returns>
	bool IsPressed(int actionId) const;

	/// <summary>この frame で押された瞬間か（前フレ未押下 → 今フレ押下）</summary>
	/// <param name="actionId">アクション ID</param>
	/// <returns>どれかのスロットが押された瞬間なら true（範囲外の ID は false）</returns>
	bool IsTriggered(int actionId) const;

	/// <summary>この frame で離された瞬間か</summary>
	/// <param name="actionId">アクション ID</param>
	/// <returns>どれかのスロットが離された瞬間なら true（範囲外の ID は false）</returns>
	bool IsReleased(int actionId) const;

	/// <summary>
	/// アクション登録に関係なく、何らかの物理入力（キー/マウスボタン/コントローラーボタン/トリガー）が
	/// この frame で押された瞬間か。タイトルの「Press Any Button」用。
	/// </summary>
	/// <returns>何か押された瞬間なら true</returns>
	bool AnyInputTriggered() const;

	//====================
	// 物理デバイスへの直接アクセス（リバインドUI用：次に押されたキーをキャプチャするなど）
	//====================
	/// <summary>紐付けたキーボード</summary>
	/// <returns>キーボード（未設定なら nullptr）</returns>
	KeyboardInput* GetKeyboard() const { return keyboard_; }
	/// <summary>紐付けたマウス</summary>
	/// <returns>マウス（未設定なら nullptr）</returns>
	MouseInput* GetMouse() const { return mouse_; }
	/// <summary>紐付けたコントローラー</summary>
	/// <returns>コントローラー（未設定なら nullptr）</returns>
	ControllerInput* GetGamepad() const { return gamepad_; }

private:
	/// <summary>1 つの物理入力が押下中か（LT/RT はトリガー値 0.5 以上で押下）</summary>
	/// <param name="b">物理入力</param>
	/// <returns>押下中なら true（Empty・デバイス未設定なら false）</returns>
	bool IsBindingPressed(const PhysicalBinding& b) const;
	/// <summary>1 つの物理入力が押された瞬間か（LT/RT は BeginFrame で控えた前フレの状態と比べる）</summary>
	/// <param name="b">物理入力</param>
	/// <returns>押された瞬間なら true（Empty・デバイス未設定なら false）</returns>
	bool IsBindingTriggered(const PhysicalBinding& b) const;
	/// <summary>1 つの物理入力が離された瞬間か（LT/RT は BeginFrame で控えた前フレの状態と比べる）</summary>
	/// <param name="b">物理入力</param>
	/// <returns>離された瞬間なら true（Empty・デバイス未設定なら false）</returns>
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
