#include "InputManager.h"
#include "WindowsApplication.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"
#include "InputAction.h"
#include <cassert>

InputManager::InputManager() {
}

InputManager::~InputManager() {
	Finalize();
}

void InputManager::Initialize(WindowsApplication* winApp) {
	assert(winApp);
	winApp_ = winApp;

	//==============================
	// DirectInputの初期化（1回だけ）
	//==============================
	HRESULT hr = DirectInput8Create(
		winApp->GetInstanceHandle(),
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		(void**)&directInput_,
		nullptr
	);
	assert(SUCCEEDED(hr));

	//==============================
	// 各入力デバイスの初期化
	//==============================

	// キーボード
	keyboard_ = new KeyboardInput();
	keyboard_->Initialize(winApp, directInput_.Get());

	// マウス
	mouse_ = new MouseInput();
	mouse_->Initialize(winApp, directInput_.Get());

	// コントローラー（XInputなのでDirectInput不要）
	controller_ = new ControllerInput(0);

	// 論理アクション層
	actionMap_ = new InputActionMap();
	actionMap_->SetDevices(keyboard_, mouse_, controller_);
}

void InputManager::Update() {
	// アクション層の前処理（LT/RT の前フレ状態キャプチャ。デバイス Update より前に呼ぶ）
	if (actionMap_) {
		actionMap_->BeginFrame();
	}

	// 各デバイスの更新
	if (keyboard_) {
		keyboard_->Update();
	}
	if (mouse_) {
		mouse_->Update();
	}
	if (controller_) {
		controller_->Update();
	}
}

void InputManager::Finalize() {
	if (actionMap_) {
		delete actionMap_;
		actionMap_ = nullptr;
	}

	// 各デバイスの解放
	if (controller_) {
		delete controller_;
		controller_ = nullptr;
	}
	if (mouse_) {
		delete mouse_;
		mouse_ = nullptr;
	}
	if (keyboard_) {
		delete keyboard_;
		keyboard_ = nullptr;
	}

	// DirectInputの解放（ComPtrなので自動解放されるが明示的にリセット）
	directInput_.Reset();
}