#include "KeyboardInput.h"

void KeyboardInput::Initialize(WindowsApplication* winApp) {

	// 借りてきたWinAppのインスタンスを記録
	winApp_ = winApp;

	//==============================
	// DirectInputの初期化
	//==============================
	// ここはデバイスを増やしても1つでいい
	HRESULT hr = DirectInput8Create(
		winApp->GetInstanceHandle(), DIRECTINPUT_VERSION, IID_IDirectInput8,
		(void**)&directInput_, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);// GUID_JoystickやGUID_Mouseとかでほかのデバイスも使える
	assert(SUCCEEDED(hr));

	// キーボードの入力データ形式のセット
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard); // 標準形式。入力デバイスによっては複数用意されていたりする
	assert(SUCCEEDED(hr));

	// キーボードの排他制御レベルのセット
	hr = keyboard_->SetCooperativeLevel(
		// DISCL_FOREGROUND : 画面が手前にある場合のみ入力を受け付ける
		// DISCL_NONEXCLUSIVE : デバイスをこのアプリだけで占有しない
		// DISCL_NOWINKEY : Windowsキーを無効化
		winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY
	);

}

void KeyboardInput::Update() {

	//=========================
	// 前フレームのキー状態を保存
	//=========================
	memcpy(preKeys_, keys_, sizeof(keys_));

	//=========================
	// キーボードの入力処理
	//=========================
	// キーボード情報の取得開始
	keyboard_->Acquire();

	// 例えばENTERキーを押しているときkeys[DIK_RETURN]に0x80が代入される。押してないときは0x00
	keyboard_->GetDeviceState(sizeof(keys_), keys_);

}

bool KeyboardInput::PuhsKey(BYTE keyNum)
{
	// 指定キーを押していればtrueを返す
	if (keys_[keyNum]) {
		return true;
	}

	return false;
}

bool KeyboardInput::TriggerKey(BYTE keyNum)
{
	// 指定キーを押したフレームのみtrueを返す
	if (keys_[keyNum]&&!preKeys_[keyNum]) {
		return true;
	}

	return false;
}
