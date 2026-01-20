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

/// <summary>
/// 入力管理クラス
/// DirectInputの初期化を一元管理し、各入力デバイスを統括する
/// </summary>
class InputManager {
public:
	// namespace省略用
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	InputManager();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~InputManager();

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="winApp">WindowsApplicationのポインタ</param>
	void Initialize(WindowsApplication* winApp);

	/// <summary>
	/// 更新処理（毎フレーム呼び出し）
	/// </summary>
	void Update();

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

	//====================
	// 各入力デバイスの取得
	//====================

	/// <summary>
	/// キーボード入力を取得
	/// </summary>
	KeyboardInput* GetKeyboard() const { return keyboard_; }

	/// <summary>
	/// マウス入力を取得
	/// </summary>
	MouseInput* GetMouse() const { return mouse_; }

	/// <summary>
	/// コントローラー入力を取得
	/// </summary>
	ControllerInput* GetController() const { return controller_; }

	//====================
	// DirectInputの取得（内部用）
	//====================

	/// <summary>
	/// DirectInputインターフェースを取得
	/// </summary>
	IDirectInput8* GetDirectInput() const { return directInput_.Get(); }

private:
	WindowsApplication* winApp_ = nullptr;

	// DirectInput（1つだけ作成して共有）
	ComPtr<IDirectInput8> directInput_ = nullptr;

	// 各入力デバイス
	KeyboardInput* keyboard_ = nullptr;
	MouseInput* mouse_ = nullptr;
	ControllerInput* controller_ = nullptr;
};