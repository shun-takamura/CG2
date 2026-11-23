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
	/// <summary>読むスロットを決めて入力状態を 0 で初期化する</summary>
	/// <param name="controllerIndex">コントローラー番号 (0~3)</param>
	ControllerInput(DWORD controllerIndex = 0);

	/// <summary>振動を止める</summary>
	~ControllerInput();

	/// <summary>
	/// 前フレームの状態を控えてから入力を読み直し、スティック・トリガーを補正する（毎フレーム呼ぶ）。
	/// まだ操作されているスロットを掴んでいない・今のスロットが切れた時は 0〜3 から操作されているスロットを探して切り替える
	/// </summary>
	void Update();

	/// <summary>
	/// リプレイ再生用：記録した生値を注入して通常 Update と同じ補正処理を通す。
	/// </summary>
	/// <param name="connected">接続していたか</param>
	/// <param name="lx">左スティック X の生値</param>
	/// <param name="ly">左スティック Y の生値</param>
	/// <param name="rx">右スティック X の生値</param>
	/// <param name="ry">右スティック Y の生値</param>
	/// <param name="lt">左トリガーの生値（0〜255）</param>
	/// <param name="rt">右トリガーの生値（0〜255）</param>
	/// <param name="buttons">ボタンのビット（XINPUT_GAMEPAD_* の和）</param>
	void ApplyReplay(bool connected, SHORT lx, SHORT ly, SHORT rx, SHORT ry,
		BYTE lt, BYTE rt, WORD buttons);

	//====================
	// ボタン入力
	//====================

	/// <summary>ボタンが押されているか</summary>
	/// <param name="button">XINPUT_GAMEPAD_* のビット（複数ならどれか 1 つ）</param>
	/// <returns>押されていれば true</returns>
	bool IsButtonPressed(WORD button) const;

	/// <summary>ボタンがトリガー（押した瞬間）か</summary>
	/// <param name="button">XINPUT_GAMEPAD_* のビット</param>
	/// <returns>押した瞬間なら true</returns>
	bool IsButtonTriggered(WORD button) const;

	/// <summary>ボタンがリリース（離した瞬間）か</summary>
	/// <param name="button">XINPUT_GAMEPAD_* のビット</param>
	/// <returns>離した瞬間なら true</returns>
	bool IsButtonReleased(WORD button) const;

	//====================
	// スティック入力
	//====================

	/// <summary>左スティックの入力（円形デッドゾーン補正済み）</summary>
	/// <returns>方向と倒し具合（未接続なら 0）</returns>
	const StickInput& GetLeftStick() const { return leftStick_; }

	/// <summary>右スティックの入力（円形デッドゾーン補正済み）</summary>
	/// <returns>方向と倒し具合（未接続なら 0）</returns>
	const StickInput& GetRightStick() const { return rightStick_; }

	/// <summary>左スティック X の生の値</summary>
	/// <returns>生値（-32768〜32767）</returns>
	SHORT GetLeftStickRawX() const { return currentState_.Gamepad.sThumbLX; }
	/// <summary>左スティック Y の生の値</summary>
	/// <returns>生値（-32768〜32767）</returns>
	SHORT GetLeftStickRawY() const { return currentState_.Gamepad.sThumbLY; }

	/// <summary>右スティック X の生の値</summary>
	/// <returns>生値（-32768〜32767）</returns>
	SHORT GetRightStickRawX() const { return currentState_.Gamepad.sThumbRX; }
	/// <summary>右スティック Y の生の値</summary>
	/// <returns>生値（-32768〜32767）</returns>
	SHORT GetRightStickRawY() const { return currentState_.Gamepad.sThumbRY; }

	//====================
	// トリガー入力
	//====================

	/// <summary>左トリガーの値（デッドゾーン補正済み）</summary>
	/// <returns>0.0〜1.0（未接続なら 0）</returns>
	float GetLeftTrigger() const { return leftTrigger_; }

	/// <summary>右トリガーの値（デッドゾーン補正済み）</summary>
	/// <returns>0.0〜1.0（未接続なら 0）</returns>
	float GetRightTrigger() const { return rightTrigger_; }

	/// <summary>左トリガーの生の値</summary>
	/// <returns>0〜255</returns>
	BYTE GetLeftTriggerRaw() const { return currentState_.Gamepad.bLeftTrigger; }

	/// <summary>右トリガーの生の値</summary>
	/// <returns>0〜255</returns>
	BYTE GetRightTriggerRaw() const { return currentState_.Gamepad.bRightTrigger; }

	/// <summary>ボタンの生ビット（リプレイ記録用）</summary>
	/// <returns>XINPUT_GAMEPAD_* のビット和</returns>
	WORD GetButtonsRaw() const { return currentState_.Gamepad.wButtons; }

	//====================
	// 振動
	//====================

	/// <summary>振動を設定する（今読んでいるスロットへ）</summary>
	/// <param name="leftMotor">左モーターの強さ (0~65535)</param>
	/// <param name="rightMotor">右モーターの強さ (0~65535)</param>
	void SetVibration(WORD leftMotor, WORD rightMotor);

	/// <summary>振動を止める</summary>
	void StopVibration();

	//====================
	// 接続状態
	//====================

	/// <summary>コントローラーが接続されているか</summary>
	/// <returns>接続されていれば true</returns>
	bool IsConnected() const { return isConnected_; }

	//====================
	// デッドゾーン設定
	//====================

	/// <summary>デッドゾーンをまとめて設定する</summary>
	/// <param name="deadZone">デッドゾーン</param>
	void SetDeadZone(const DeadZone& deadZone) { deadZone_ = deadZone; }

	/// <summary>デッドゾーンの設定</summary>
	/// <returns>デッドゾーン</returns>
	const DeadZone& GetDeadZone() const { return deadZone_; }

	/// <summary>左スティックのデッドゾーンを設定する</summary>
	/// <param name="value">生値の半径（0〜32767）</param>
	void SetLeftThumbDeadZone(SHORT value) { deadZone_.leftThumb = value; }

	/// <summary>右スティックのデッドゾーンを設定する</summary>
	/// <param name="value">生値の半径（0〜32767）</param>
	void SetRightThumbDeadZone(SHORT value) { deadZone_.rightThumb = value; }

	/// <summary>左トリガーのデッドゾーンを設定する</summary>
	/// <param name="value">生値（0〜255）</param>
	void SetLeftTriggerDeadZone(BYTE value) { deadZone_.leftTrigger = value; }

	/// <summary>右トリガーのデッドゾーンを設定する</summary>
	/// <param name="value">生値（0〜255）</param>
	void SetRightTriggerDeadZone(BYTE value) { deadZone_.rightTrigger = value; }

private:
	/// <summary>
	/// currentState_ からスティック/トリガーの補正値を計算する（Update / ApplyReplay 共通）。
	/// </summary>
	void ProcessCurrentState();

	/// <summary>
	/// そのスロットの状態が「実際に操作されている」か判定する。
	/// ボタン押下・トリガー・デッドゾーン超えのスティックのどれかがあれば true。
	/// スロット0に居座るドリフトした幽霊パッド（固定値・btns=0）を弾くために使う。
	/// </summary>
	/// <param name="state">調べる状態</param>
	/// <returns>操作されていれば true</returns>
	static bool HasActivity(const XINPUT_STATE& state);

	/// <summary>スティックの入力を円形デッドゾーンで補正し、デッドゾーンの外を 0〜1 に引き伸ばす</summary>
	/// <param name="rawX">X の生値</param>
	/// <param name="rawY">Y の生値</param>
	/// <param name="deadZone">デッドゾーンの半径（生値）</param>
	/// <returns>方向（単位ベクトル）と倒し具合（デッドゾーン内なら 0）</returns>
	StickInput ApplyCircularDeadZone(SHORT rawX, SHORT rawY, SHORT deadZone);

private:
	DWORD controllerIndex_ = 0;     // コントローラー番号
	bool isConnected_ = false;      // 接続状態
	bool activeSlotLocked_ = false; // 実際に操作されているスロットを掴んだか（掴んだら切断まで固定）

	XINPUT_STATE currentState_{};   // 現在の入力状態
	XINPUT_STATE previousState_{};  // 前フレームの入力状態

	DeadZone deadZone_{};           // デッドゾーン設定

	StickInput leftStick_{};        // 左スティック（補正済み）
	StickInput rightStick_{};       // 右スティック（補正済み）

	float leftTrigger_ = 0.0f;      // 左トリガー（補正済み、0.0~1.0）
	float rightTrigger_ = 0.0f;     // 右トリガー（補正済み、0.0~1.0）
};