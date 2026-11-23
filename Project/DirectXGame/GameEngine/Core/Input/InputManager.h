#pragma once

// ============
// 入力デバイス
//=============
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

// ComPtr
#include <wrl.h>

// 前方宣言
class WindowsApplication;
class KeyboardInput;
class MouseInput;
class ControllerInput;
class InputActionMap;

/// <summary>
/// 入力管理クラス
/// DirectInputの初期化を一元管理し、各入力デバイスを統括する
/// </summary>
class InputManager {
public:
	// namespace省略用
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// <summary>何もしない（デバイスの生成は Initialize で行う）</summary>
	InputManager();

	/// <summary>Finalize を呼んで各デバイスを解放する</summary>
	~InputManager();

	/// <summary>DirectInput を 1 つ作り、キーボード・マウス・コントローラー（XInput の 0 番）・アクションマップを作る</summary>
	/// <param name="winApp">WindowsApplicationのポインタ</param>
	void Initialize(WindowsApplication* winApp);

	/// <summary>アクションマップの前処理をしてから、各デバイスの状態を読み直す（毎フレーム呼ぶ）</summary>
	void Update();

	/// <summary>アクションマップと各デバイスを解放し、DirectInput を手放す（2 回呼んでもよい）</summary>
	void Finalize();

	//====================
	// 各入力デバイスの取得
	//====================

	/// <summary>キーボード入力</summary>
	/// <returns>キーボード（Initialize 前・Finalize 後は nullptr）</returns>
	KeyboardInput* GetKeyboard() const { return keyboard_; }

	/// <summary>マウス入力</summary>
	/// <returns>マウス（Initialize 前・Finalize 後は nullptr）</returns>
	MouseInput* GetMouse() const { return mouse_; }

	/// <summary>コントローラー入力</summary>
	/// <returns>コントローラー（Initialize 前・Finalize 後は nullptr）</returns>
	ControllerInput* GetController() const { return controller_; }

	/// <summary>論理アクションマップ（キーコンフィグ経由のゲーム入力の問い合わせ先）</summary>
	/// <returns>アクションマップ（Initialize 前・Finalize 後は nullptr）</returns>
	InputActionMap* GetActionMap() const { return actionMap_; }

	//====================
	// DirectInputの取得（内部用）
	//====================

	/// <summary>共有している DirectInput のインターフェース</summary>
	/// <returns>DirectInput（Initialize 前は nullptr）</returns>
	IDirectInput8* GetDirectInput() const { return directInput_.Get(); }

private:
	WindowsApplication* winApp_ = nullptr;

	// DirectInput（1つだけ作成して共有）
	ComPtr<IDirectInput8> directInput_ = nullptr;

	// 各入力デバイス
	KeyboardInput* keyboard_ = nullptr;
	MouseInput* mouse_ = nullptr;
	ControllerInput* controller_ = nullptr;

	// 論理アクション層（キーコンフィグ経由のゲーム入力）
	InputActionMap* actionMap_ = nullptr;
};