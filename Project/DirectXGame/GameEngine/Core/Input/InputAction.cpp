#include "InputAction.h"

#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"

const InputActionMap::Slots InputActionMap::kEmptySlots{};

namespace {
	// LT/RT を「押下」とみなす閾値
	constexpr float kTriggerThreshold = 0.5f;
}

void InputActionMap::SetDevices(KeyboardInput* keyboard, MouseInput* mouse, ControllerInput* gamepad) {
	keyboard_ = keyboard;
	mouse_ = mouse;
	gamepad_ = gamepad;
}

void InputActionMap::Resize(size_t actionCount) {
	actions_.assign(actionCount, Slots{});
}

void InputActionMap::BeginFrame() {
	// 物理デバイス Update の直前に呼ばれる。
	// このタイミングで「今フレーム」の値はまだ前フレームの値を保持しているため、
	// LT/RT の押下状態を prev として記憶しておく。
	if (gamepad_) {
		prevLTPressed_ = gamepad_->GetLeftTrigger() >= kTriggerThreshold;
		prevRTPressed_ = gamepad_->GetRightTrigger() >= kTriggerThreshold;
	} else {
		prevLTPressed_ = false;
		prevRTPressed_ = false;
	}
}

void InputActionMap::Bind(int actionId, size_t slot, const PhysicalBinding& binding) {
	if (actionId < 0 || static_cast<size_t>(actionId) >= actions_.size()) return;
	if (slot >= kSlotCount) return;
	actions_[actionId].bindings[slot] = binding;
}

void InputActionMap::Unbind(int actionId, size_t slot) {
	Bind(actionId, slot, PhysicalBinding{});
}

const InputActionMap::Slots& InputActionMap::GetSlots(int actionId) const {
	if (actionId < 0 || static_cast<size_t>(actionId) >= actions_.size()) return kEmptySlots;
	return actions_[actionId];
}

bool InputActionMap::IsPressed(int actionId) const {
	if (actionId < 0 || static_cast<size_t>(actionId) >= actions_.size()) return false;
	const auto& slots = actions_[actionId];
	for (const auto& b : slots.bindings) {
		if (IsBindingPressed(b)) return true;
	}
	return false;
}

bool InputActionMap::IsTriggered(int actionId) const {
	if (actionId < 0 || static_cast<size_t>(actionId) >= actions_.size()) return false;
	const auto& slots = actions_[actionId];
	for (const auto& b : slots.bindings) {
		if (IsBindingTriggered(b)) return true;
	}
	return false;
}

bool InputActionMap::IsReleased(int actionId) const {
	if (actionId < 0 || static_cast<size_t>(actionId) >= actions_.size()) return false;
	const auto& slots = actions_[actionId];
	for (const auto& b : slots.bindings) {
		if (IsBindingReleased(b)) return true;
	}
	return false;
}

bool InputActionMap::AnyInputTriggered() const {
	// キーボード: 全256キーをスキャン
	if (keyboard_) {
		for (int i = 0; i < 256; ++i) {
			if ((keyboard_->keys_[i] & 0x80) && !(keyboard_->preKeys_[i] & 0x80)) {
				return true;
			}
		}
	}
	// マウス: 4ボタン
	if (mouse_) {
		using B = MouseInput::Button;
		if (mouse_->IsButtonTriggered(B::Left)) return true;
		if (mouse_->IsButtonTriggered(B::Right)) return true;
		if (mouse_->IsButtonTriggered(B::Middle)) return true;
		if (mouse_->IsButtonTriggered(B::Button4)) return true;
	}
	// コントローラー: 各ボタン + LT/RT
	if (gamepad_) {
		const WORD buttons[] = {
			XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
			XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
			XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
			XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
			XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
			XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
		};
		for (WORD btn : buttons) {
			if (gamepad_->IsButtonTriggered(btn)) return true;
		}
		bool ltCur = gamepad_->GetLeftTrigger() >= kTriggerThreshold;
		bool rtCur = gamepad_->GetRightTrigger() >= kTriggerThreshold;
		if (ltCur && !prevLTPressed_) return true;
		if (rtCur && !prevRTPressed_) return true;
	}
	return false;
}

bool InputActionMap::IsBindingPressed(const PhysicalBinding& b) const {
	if (b.IsEmpty()) return false;
	switch (b.device) {
	case InputDevice::Keyboard:
		return keyboard_ && keyboard_->PuhsKey(static_cast<BYTE>(b.code));
	case InputDevice::Mouse: {
		if (!mouse_) return false;
		return mouse_->IsButtonPressed(static_cast<MouseInput::Button>(b.code));
	}
	case InputDevice::Gamepad: {
		if (!gamepad_) return false;
		if (b.code == GamepadCode::LT) return gamepad_->GetLeftTrigger() >= kTriggerThreshold;
		if (b.code == GamepadCode::RT) return gamepad_->GetRightTrigger() >= kTriggerThreshold;
		return gamepad_->IsButtonPressed(static_cast<WORD>(b.code));
	}
	default:
		return false;
	}
}

bool InputActionMap::IsBindingTriggered(const PhysicalBinding& b) const {
	if (b.IsEmpty()) return false;
	switch (b.device) {
	case InputDevice::Keyboard:
		return keyboard_ && keyboard_->TriggerKey(static_cast<BYTE>(b.code));
	case InputDevice::Mouse: {
		if (!mouse_) return false;
		return mouse_->IsButtonTriggered(static_cast<MouseInput::Button>(b.code));
	}
	case InputDevice::Gamepad: {
		if (!gamepad_) return false;
		if (b.code == GamepadCode::LT) {
			bool cur = gamepad_->GetLeftTrigger() >= kTriggerThreshold;
			return cur && !prevLTPressed_;
		}
		if (b.code == GamepadCode::RT) {
			bool cur = gamepad_->GetRightTrigger() >= kTriggerThreshold;
			return cur && !prevRTPressed_;
		}
		return gamepad_->IsButtonTriggered(static_cast<WORD>(b.code));
	}
	default:
		return false;
	}
}

bool InputActionMap::IsBindingReleased(const PhysicalBinding& b) const {
	if (b.IsEmpty()) return false;
	switch (b.device) {
	case InputDevice::Keyboard:
		// KeyboardInput には Released 用APIが無い。前フレキー状態を直接見る
		if (!keyboard_) return false;
		return (keyboard_->preKeys_[b.code] & 0x80) && !(keyboard_->keys_[b.code] & 0x80);
	case InputDevice::Mouse: {
		if (!mouse_) return false;
		return mouse_->IsButtonReleased(static_cast<MouseInput::Button>(b.code));
	}
	case InputDevice::Gamepad: {
		if (!gamepad_) return false;
		if (b.code == GamepadCode::LT) {
			bool cur = gamepad_->GetLeftTrigger() >= kTriggerThreshold;
			return !cur && prevLTPressed_;
		}
		if (b.code == GamepadCode::RT) {
			bool cur = gamepad_->GetRightTrigger() >= kTriggerThreshold;
			return !cur && prevRTPressed_;
		}
		return gamepad_->IsButtonReleased(static_cast<WORD>(b.code));
	}
	default:
		return false;
	}
}
