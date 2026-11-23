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
	/// <summary>入力状態と座標を 0 で初期化する（デバイスの生成は Initialize で行う）</summary>
	MouseInput();

	/// <summary>カーソルの閉じ込めを外し、マウスデバイスと（自分で作った時だけ）DirectInput を解放する</summary>
	~MouseInput();

	/// <summary>マウスのデバイスを作る（非排他・前面にある時だけ取得）。DirectInput が渡されなければ自分で作って持つ</summary>
	/// <param name="winApp">WindowsApplicationのポインタ</param>
	/// <param name="directInput">DirectInputのポインタ（既存のものを共有する場合）</param>
	void Initialize(WindowsApplication* winApp, IDirectInput8* directInput = nullptr);

	/// <summary>
	/// 前フレームの状態を控えてから入力を読み直し、カーソルの閉じ込めを更新してカーソル座標を取る（毎フレーム呼ぶ）。
	/// 読めなかったフレームは入力を 0 にする
	/// </summary>
	void Update();

	/// <summary>
	/// リプレイ再生用：記録した移動量とボタンを注入（btnMask: bit0=L,1=R,2=M,3=B4）。
	/// 絶対座標(screen/client)は記録対象外のため更新しない。
	/// </summary>
	/// <param name="dx">X の移動量</param>
	/// <param name="dy">Y の移動量</param>
	/// <param name="wheel">ホイールの回転量</param>
	/// <param name="btnMask">ボタンのビット（bit0=L, 1=R, 2=M, 3=B4）</param>
	void ApplyReplay(LONG dx, LONG dy, LONG wheel, int btnMask);

	//====================
	// ボタン入力
	//====================

	/// <summary>ボタンが押されているか</summary>
	/// <param name="button">ボタン</param>
	/// <returns>押されていれば true（範囲外のボタンは false）</returns>
	bool IsButtonPressed(Button button) const;

	/// <summary>ボタンがトリガー（押した瞬間）か</summary>
	/// <param name="button">ボタン</param>
	/// <returns>押した瞬間なら true（範囲外のボタンは false）</returns>
	bool IsButtonTriggered(Button button) const;

	/// <summary>ボタンがリリース（離した瞬間）か</summary>
	/// <param name="button">ボタン</param>
	/// <returns>離した瞬間なら true（範囲外のボタンは false）</returns>
	bool IsButtonReleased(Button button) const;

	//====================
	// 移動量
	//====================

	/// <summary>マウスの X の移動量（前フレームからの差分）</summary>
	/// <returns>移動量（DirectInput の相対値）</returns>
	LONG GetDeltaX() const { return currentState_.lX; }

	/// <summary>マウスの Y の移動量（前フレームからの差分）</summary>
	/// <returns>移動量（DirectInput の相対値）</returns>
	LONG GetDeltaY() const { return currentState_.lY; }

	/// <summary>ホイールの回転量（前フレームからの差分）</summary>
	/// <returns>回転量（1 ノッチ 120）</returns>
	LONG GetDeltaWheel() const { return currentState_.lZ; }

	//====================
	// スクリーン座標
	//====================

	/// <summary>マウスのスクリーン座標 X</summary>
	/// <returns>X [px]</returns>
	LONG GetScreenX() const { return screenPosition_.x; }

	/// <summary>マウスのスクリーン座標 Y</summary>
	/// <returns>Y [px]</returns>
	LONG GetScreenY() const { return screenPosition_.y; }

	/// <summary>マウスのクライアント座標 X（ウィンドウ内座標）</summary>
	/// <returns>X [px]</returns>
	LONG GetClientX() const { return clientPosition_.x; }

	/// <summary>マウスのクライアント座標 Y（ウィンドウ内座標）</summary>
	/// <returns>Y [px]</returns>
	LONG GetClientY() const { return clientPosition_.y; }

	/// <summary>マウスのスクリーン座標</summary>
	/// <returns>座標 [px]</returns>
	const POINT& GetScreenPosition() const { return screenPosition_; }

	/// <summary>マウスのクライアント座標（ウィンドウ内座標）</summary>
	/// <returns>座標 [px]</returns>
	const POINT& GetClientPosition() const { return clientPosition_; }

	/// <summary>
	/// マウスの位置を UI の仮想解像度（1600×900）の座標で取得する（描画解像度には関係しない）。
	/// クライアント座標から黒帯ぶんを引き、16:9 の枠の大きさから換算する（最大化・フルスクリーン対応）。
	/// </summary>
	/// <param name="x">UI 座標の X を書く先</param>
	/// <param name="y">UI 座標の Y を書く先</param>
	/// <returns>枠（ゲーム画面）の中にあれば true。外でも x/y は換算した値が入る（ウィンドウ未設定ならクライアント座標のまま false）</returns>
	bool GetUIPosition(float& x, float& y) const;

	//====================
	// カーソルの閉じ込め
	//====================

	/// <summary>
	/// カーソルをゲーム画面の枠（黒帯を除く）に閉じ込めるか。ウィンドウが手前にある間だけ効く。
	/// 既定は Debug だけオフ（ImGui のエディタで窓の外を触るため）
	/// </summary>
	/// <param name="confine">閉じ込めるなら true（false にするとすぐ外す）</param>
	void SetConfineCursor(bool confine);
	/// <summary>カーソルをゲーム画面の枠に閉じ込める設定か</summary>
	/// <returns>閉じ込めるなら true</returns>
	bool IsConfineCursor() const { return confineCursor_; }

private:
	/// <summary>手前にあればカーソルを枠へ閉じ込め、そうでなければ外す（毎フレーム。範囲が変わらなければ掛け直さない）</summary>
	void UpdateCursorClip();
	/// <summary>カーソルの閉じ込めを外す（掛けていなければ何もしない）</summary>
	void ReleaseCursorClip();

private:
	WindowsApplication* winApp_ = nullptr;

	IDirectInput8* directInput_ = nullptr;      // DirectInputインターフェース
	IDirectInputDevice8* mouse_ = nullptr;      // マウスデバイス

	bool ownsDirectInput_ = false;              // DirectInputの所有権（自分で作成したかどうか）

	DIMOUSESTATE currentState_{};               // 現在の入力状態
	DIMOUSESTATE previousState_{};              // 前フレームの入力状態

	POINT screenPosition_{};                    // スクリーン座標
	POINT clientPosition_{};                    // クライアント座標（ウィンドウ内座標）

#ifdef _DEBUG
	bool confineCursor_ = false;
#else
	bool confineCursor_ = true;
#endif
	bool cursorClipped_ = false;                // 今 ClipCursor を掛けているか
	RECT clipRect_{};                           // 掛けている範囲（同じなら掛け直さない）
};