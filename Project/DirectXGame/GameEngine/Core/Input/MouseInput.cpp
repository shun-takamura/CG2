#include "MouseInput.h"
#include "WindowsApplication.h"
#include <cassert>

MouseInput::MouseInput() {
	ZeroMemory(&currentState_, sizeof(currentState_));
	ZeroMemory(&previousState_, sizeof(previousState_));
	ZeroMemory(&screenPosition_, sizeof(screenPosition_));
	ZeroMemory(&clientPosition_, sizeof(clientPosition_));
}

MouseInput::~MouseInput() {
	// マウスデバイスの解放
	if (mouse_) {
		mouse_->Unacquire();
		mouse_->Release();
		mouse_ = nullptr;
	}

	// DirectInputの解放（自分で作成した場合のみ）
	if (ownsDirectInput_ && directInput_) {
		directInput_->Release();
		directInput_ = nullptr;
	}
}

void MouseInput::Initialize(WindowsApplication* winApp, IDirectInput8* directInput) {
	assert(winApp);
	winApp_ = winApp;

	HRESULT hr;

	// DirectInputが渡されなかった場合は自分で作成
	if (directInput == nullptr) {
		hr = DirectInput8Create(
			winApp->GetInstanceHandle(),
			DIRECTINPUT_VERSION,
			IID_IDirectInput8,
			(void**)&directInput_,
			nullptr
		);
		assert(SUCCEEDED(hr));
		ownsDirectInput_ = true;
	} else {
		// 渡されたDirectInputを使用
		directInput_ = directInput;
		ownsDirectInput_ = false;
	}

	// マウスデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
	assert(SUCCEEDED(hr));

	// マウスの入力データ形式のセット
	hr = mouse_->SetDataFormat(&c_dfDIMouse);
	assert(SUCCEEDED(hr));

	// マウスの排他制御レベルのセット
	// DISCL_NONEXCLUSIVE : デバイスをこのアプリだけで占有しない
	// DISCL_FOREGROUND : フォアグラウンドウィンドウの場合のみ取得
	hr = mouse_->SetCooperativeLevel(
		winApp->GetHwnd(),
		DISCL_NONEXCLUSIVE | DISCL_FOREGROUND
	);
	assert(SUCCEEDED(hr));
}

void MouseInput::Update() {
	// 前フレームの状態を保存
	previousState_ = currentState_;

	// マウスデバイスからの入力を取得
	HRESULT hr = mouse_->Acquire();
	if (SUCCEEDED(hr)) {
		hr = mouse_->GetDeviceState(sizeof(currentState_), &currentState_);
		if (FAILED(hr)) {
			// 取得失敗時は状態をクリア
			ZeroMemory(&currentState_, sizeof(currentState_));
		}
	} else {
		// Acquire失敗時は状態をクリア
		ZeroMemory(&currentState_, sizeof(currentState_));
	}

	// スクリーン座標を取得
	GetCursorPos(&screenPosition_);

	// クライアント座標に変換
	clientPosition_ = screenPosition_;
	ScreenToClient(winApp_->GetHwnd(), &clientPosition_);
}

bool MouseInput::IsButtonPressed(Button button) const {
	int index = static_cast<int>(button);
	if (index < 0 || index >= 4) return false;
	return (currentState_.rgbButtons[index] & 0x80) != 0;
}

bool MouseInput::IsButtonTriggered(Button button) const {
	int index = static_cast<int>(button);
	if (index < 0 || index >= 4) return false;
	// 現在押されていて、前フレームは押されていなかった
	return ((currentState_.rgbButtons[index] & 0x80) != 0) &&
		((previousState_.rgbButtons[index] & 0x80) == 0);
}

bool MouseInput::IsButtonReleased(Button button) const {
	int index = static_cast<int>(button);
	if (index < 0 || index >= 4) return false;
	// 現在押されていなくて、前フレームは押されていた
	return ((currentState_.rgbButtons[index] & 0x80) == 0) &&
		((previousState_.rgbButtons[index] & 0x80) != 0);
}