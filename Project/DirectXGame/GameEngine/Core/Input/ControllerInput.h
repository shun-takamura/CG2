#pragma once

#include <Windows.h>
#include <XInput.h>

#pragma comment(lib,"xinput.lib")

/// <summary>
/// Xboxコントローラー入力クラス
/// </summary>
class ControllerInput {
public:
	/// <summary>
	/// デッドゾーン設定構造体
	/// </summary>
	struct DeadZone {
		SHORT leftThumb = 7849;   // 左スティック (XInput標準値)
		SHORT rightThumb = 7849;  // 右スティック (XInput標準値)
		BYTE leftTrigger = 30;    // 左トリガー
		BYTE rightTrigger = 30;   // 右トリガー
	};

	/// <summary>
	/// スティックの正規化された入力値
	/// </summary>
	struct StickInput {
		float x = 0.0f;          // 方向ベクトル X (-1.0 ~ 1.0)
		float y = 0.0f;          // 方向ベクトル Y (-1.0 ~ 1.0)
		float magnitude = 0.0f;  // 倒し具合 (0.0 ~ 1.0)
	};

public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	/// <param name="controllerIndex">コントローラー番号 (0~3)</param>
	ControllerInput(DWORD controllerIndex = 0);

	/// <summary>
	/// デストラクタ
	/// </summary>
	~ControllerInput();

	/// <summary>
	/// 更新処理（毎フレーム呼び出し）
	/// </summary>
	void Update();

	//====================
	// ボタン入力
	//====================

	/// <summary>
	/// ボタンが押されているか
	/// </summary>
	bool IsButtonPressed(WORD button) const;

	/// <summary>
	/// ボタンがトリガー（押した瞬間）か
	/// </summary>
	bool IsButtonTriggered(WORD button) const;

	/// <summary>
	/// ボタンがリリース（離した瞬間）か
	/// </summary>
	bool IsButtonReleased(WORD button) const;

	//====================
	// スティック入力
	//====================

	/// <summary>
	/// 左スティックの入力を取得（デッドゾーン補正済み）
	/// </summary>
	const StickInput& GetLeftStick() const { return leftStick_; }

	/// <summary>
	/// 右スティックの入力を取得（デッドゾーン補正済み）
	/// </summary>
	const StickInput& GetRightStick() const { return rightStick_; }

	/// <summary>
	/// 左スティックの生の値を取得
	/// </summary>
	SHORT GetLeftStickRawX() const { return currentState_.Gamepad.sThumbLX; }
	SHORT GetLeftStickRawY() const { return currentState_.Gamepad.sThumbLY; }

	/// <summary>
	/// 右スティックの生の値を取得
	/// </summary>
	SHORT GetRightStickRawX() const { return currentState_.Gamepad.sThumbRX; }
	SHORT GetRightStickRawY() const { return currentState_.Gamepad.sThumbRY; }

	//====================
	// トリガー入力
	//====================

	/// <summary>
	/// 左トリガーの値を取得（デッドゾーン補正済み、0.0~1.0）
	/// </summary>
	float GetLeftTrigger() const { return leftTrigger_; }

	/// <summary>
	/// 右トリガーの値を取得（デッドゾーン補正済み、0.0~1.0）
	/// </summary>
	float GetRightTrigger() const { return rightTrigger_; }

	/// <summary>
	/// 左トリガーの生の値を取得（0~255）
	/// </summary>
	BYTE GetLeftTriggerRaw() const { return currentState_.Gamepad.bLeftTrigger; }

	/// <summary>
	/// 右トリガーの生の値を取得（0~255）
	/// </summary>
	BYTE GetRightTriggerRaw() const { return currentState_.Gamepad.bRightTrigger; }

	//====================
	// 振動
	//====================

	/// <summary>
	/// 振動を設定
	/// </summary>
	/// <param name="leftMotor">左モーターの強さ (0~65535)</param>
	/// <param name="rightMotor">右モーターの強さ (0~65535)</param>
	void SetVibration(WORD leftMotor, WORD rightMotor);

	/// <summary>
	/// 振動を停止
	/// </summary>
	void StopVibration();

	//====================
	// 接続状態
	//====================

	/// <summary>
	/// コントローラーが接続されているか
	/// </summary>
	bool IsConnected() const { return isConnected_; }

	//====================
	// デッドゾーン設定
	//====================

	/// <summary>
	/// デッドゾーンを設定
	/// </summary>
	void SetDeadZone(const DeadZone& deadZone) { deadZone_ = deadZone; }

	/// <summary>
	/// デッドゾーンを取得
	/// </summary>
	const DeadZone& GetDeadZone() const { return deadZone_; }

	/// <summary>
	/// 左スティックのデッドゾーンを設定
	/// </summary>
	void SetLeftThumbDeadZone(SHORT value) { deadZone_.leftThumb = value; }

	/// <summary>
	/// 右スティックのデッドゾーンを設定
	/// </summary>
	void SetRightThumbDeadZone(SHORT value) { deadZone_.rightThumb = value; }

	/// <summary>
	/// 左トリガーのデッドゾーンを設定
	/// </summary>
	void SetLeftTriggerDeadZone(BYTE value) { deadZone_.leftTrigger = value; }

	/// <summary>
	/// 右トリガーのデッドゾーンを設定
	/// </summary>
	void SetRightTriggerDeadZone(BYTE value) { deadZone_.rightTrigger = value; }

private:
	/// <summary>
	/// スティックの入力を円形デッドゾーンで補正
	/// </summary>
	StickInput ApplyCircularDeadZone(SHORT rawX, SHORT rawY, SHORT deadZone);

private:
	DWORD controllerIndex_ = 0;     // コントローラー番号
	bool isConnected_ = false;      // 接続状態

	XINPUT_STATE currentState_{};   // 現在の入力状態
	XINPUT_STATE previousState_{};  // 前フレームの入力状態

	DeadZone deadZone_{};           // デッドゾーン設定

	StickInput leftStick_{};        // 左スティック（補正済み）
	StickInput rightStick_{};       // 右スティック（補正済み）

	float leftTrigger_ = 0.0f;      // 左トリガー（補正済み、0.0~1.0）
	float rightTrigger_ = 0.0f;     // 右トリガー（補正済み、0.0~1.0）
};