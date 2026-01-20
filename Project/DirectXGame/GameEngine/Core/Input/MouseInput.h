#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Windows.h>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

// 前方宣言
class WindowsApplication;

/// <summary>
/// マウス入力クラス（DirectInput使用）
/// </summary>
class MouseInput {
public:
	/// <summary>
	/// マウスボタン定数
	/// </summary>
	enum class Button {
		Left = 0,    // 左ボタン
		Right = 1,   // 右ボタン
		Middle = 2,  // 中央ボタン（ホイールクリック）
		Button4 = 3  // 拡張ボタン4
	};

public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	MouseInput();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~MouseInput();

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="winApp">WindowsApplicationのポインタ</param>
	/// <param name="directInput">DirectInputのポインタ（既存のものを共有する場合）</param>
	void Initialize(WindowsApplication* winApp, IDirectInput8* directInput = nullptr);

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
	bool IsButtonPressed(Button button) const;

	/// <summary>
	/// ボタンがトリガー（押した瞬間）か
	/// </summary>
	bool IsButtonTriggered(Button button) const;

	/// <summary>
	/// ボタンがリリース（離した瞬間）か
	/// </summary>
	bool IsButtonReleased(Button button) const;

	//====================
	// 移動量
	//====================

	/// <summary>
	/// マウスの移動量X（前フレームからの差分）
	/// </summary>
	LONG GetDeltaX() const { return currentState_.lX; }

	/// <summary>
	/// マウスの移動量Y（前フレームからの差分）
	/// </summary>
	LONG GetDeltaY() const { return currentState_.lY; }

	/// <summary>
	/// ホイールの回転量（前フレームからの差分）
	/// </summary>
	LONG GetDeltaWheel() const { return currentState_.lZ; }

	//====================
	// スクリーン座標
	//====================

	/// <summary>
	/// マウスのスクリーン座標Xを取得
	/// </summary>
	LONG GetScreenX() const { return screenPosition_.x; }

	/// <summary>
	/// マウスのスクリーン座標Yを取得
	/// </summary>
	LONG GetScreenY() const { return screenPosition_.y; }

	/// <summary>
	/// マウスのクライアント座標Xを取得（ウィンドウ内座標）
	/// </summary>
	LONG GetClientX() const { return clientPosition_.x; }

	/// <summary>
	/// マウスのクライアント座標Yを取得（ウィンドウ内座標）
	/// </summary>
	LONG GetClientY() const { return clientPosition_.y; }

	/// <summary>
	/// マウスのスクリーン座標を取得
	/// </summary>
	const POINT& GetScreenPosition() const { return screenPosition_; }

	/// <summary>
	/// マウスのクライアント座標を取得
	/// </summary>
	const POINT& GetClientPosition() const { return clientPosition_; }

private:
	WindowsApplication* winApp_ = nullptr;

	IDirectInput8* directInput_ = nullptr;      // DirectInputインターフェース
	IDirectInputDevice8* mouse_ = nullptr;      // マウスデバイス

	bool ownsDirectInput_ = false;              // DirectInputの所有権（自分で作成したかどうか）

	DIMOUSESTATE currentState_{};               // 現在の入力状態
	DIMOUSESTATE previousState_{};              // 前フレームの入力状態

	POINT screenPosition_{};                    // スクリーン座標
	POINT clientPosition_{};                    // クライアント座標（ウィンドウ内座標）
};