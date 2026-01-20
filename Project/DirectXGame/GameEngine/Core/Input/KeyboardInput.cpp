#include "KeyboardInput.h"

void KeyboardInput::Initialize(WindowsApplication* winApp, IDirectInput8* directInput) {
	assert(winApp);
	assert(directInput);

	winApp_ = winApp;
	directInput_ = directInput;

	// キーボードデバイスの生成
	HRESULT hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
	assert(SUCCEEDED(hr));

	// 以下は変更なし（SetDataFormat, SetCooperativeLevel）
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	hr = keyboard_->SetCooperativeLevel(
		winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY
	);
	assert(SUCCEEDED(hr));
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
