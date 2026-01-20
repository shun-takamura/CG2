#include "ControllerInput.h"
#include <cmath>

ControllerInput::ControllerInput(DWORD controllerIndex)
	: controllerIndex_(controllerIndex)
{
	// 状態を初期化
	ZeroMemory(&currentState_, sizeof(currentState_));
	ZeroMemory(&previousState_, sizeof(previousState_));
}

ControllerInput::~ControllerInput() {
	// 振動を停止
	StopVibration();
}

void ControllerInput::Update() {
	// 前フレームの状態を保存
	previousState_ = currentState_;

	// 現在の状態を初期化
	ZeroMemory(&currentState_, sizeof(currentState_));

	// コントローラーの状態を取得
	DWORD result = XInputGetState(controllerIndex_, &currentState_);
	isConnected_ = (result == ERROR_SUCCESS);

	if (isConnected_) {
		// 左スティックの補正
		leftStick_ = ApplyCircularDeadZone(
			currentState_.Gamepad.sThumbLX,
			currentState_.Gamepad.sThumbLY,
			deadZone_.leftThumb
		);

		// 右スティックの補正
		rightStick_ = ApplyCircularDeadZone(
			currentState_.Gamepad.sThumbRX,
			currentState_.Gamepad.sThumbRY,
			deadZone_.rightThumb
		);

		// 左トリガーの補正
		BYTE rawLeftTrigger = currentState_.Gamepad.bLeftTrigger;
		if (rawLeftTrigger < deadZone_.leftTrigger) {
			leftTrigger_ = 0.0f;
		} else {
			leftTrigger_ = static_cast<float>(rawLeftTrigger - deadZone_.leftTrigger) /
				static_cast<float>(255 - deadZone_.leftTrigger);
		}

		// 右トリガーの補正
		BYTE rawRightTrigger = currentState_.Gamepad.bRightTrigger;
		if (rawRightTrigger < deadZone_.rightTrigger) {
			rightTrigger_ = 0.0f;
		} else {
			rightTrigger_ = static_cast<float>(rawRightTrigger - deadZone_.rightTrigger) /
				static_cast<float>(255 - deadZone_.rightTrigger);
		}
	} else {
		// 接続されていない場合は全ての値をリセット
		leftStick_ = {};
		rightStick_ = {};
		leftTrigger_ = 0.0f;
		rightTrigger_ = 0.0f;
	}
}

bool ControllerInput::IsButtonPressed(WORD button) const {
	return (currentState_.Gamepad.wButtons & button) != 0;
}

bool ControllerInput::IsButtonTriggered(WORD button) const {
	// 現在押されていて、前フレームは押されていなかった
	return ((currentState_.Gamepad.wButtons & button) != 0) &&
		((previousState_.Gamepad.wButtons & button) == 0);
}

bool ControllerInput::IsButtonReleased(WORD button) const {
	// 現在押されていなくて、前フレームは押されていた
	return ((currentState_.Gamepad.wButtons & button) == 0) &&
		((previousState_.Gamepad.wButtons & button) != 0);
}

void ControllerInput::SetVibration(WORD leftMotor, WORD rightMotor) {
	XINPUT_VIBRATION vibration{};
	vibration.wLeftMotorSpeed = leftMotor;
	vibration.wRightMotorSpeed = rightMotor;
	XInputSetState(controllerIndex_, &vibration);
}

void ControllerInput::StopVibration() {
	SetVibration(0, 0);
}

ControllerInput::StickInput ControllerInput::ApplyCircularDeadZone(SHORT rawX, SHORT rawY, SHORT deadZone) {
	StickInput result{};

	// スティックの倒れ具合（ベクトルの長さ）を計算
	float magnitude = std::sqrt(static_cast<float>(rawX * rawX + rawY * rawY));

	// デッドゾーン外にある場合
	if (magnitude > static_cast<float>(deadZone)) {
		// 最大値でクリップ（±32767）
		if (magnitude > 32767.0f) {
			magnitude = 32767.0f;
		}

		// デッドゾーン分を差し引く
		magnitude -= static_cast<float>(deadZone);

		// 0.0～1.0 に正規化
		result.magnitude = magnitude / (32767.0f - static_cast<float>(deadZone));

		// 方向ベクトルを計算
		float rawMagnitude = std::sqrt(static_cast<float>(rawX * rawX + rawY * rawY));
		if (rawMagnitude > 0.0f) {
			result.x = static_cast<float>(rawX) / rawMagnitude;
			result.y = static_cast<float>(rawY) / rawMagnitude;
		}
	}
	// デッドゾーン内の場合は初期値（0）のまま

	return result;
}