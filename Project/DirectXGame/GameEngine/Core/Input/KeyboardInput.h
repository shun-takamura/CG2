#pragma once
#include "WindowsApplication.h"
#include <cassert>

// ============
// 入力デバイス
//=============
#define DIRECTINPUT_VERSION 0x0800 // Directinputのバージョン指定
#include <dinput.h> // 必ずバージョン指定後にインクルード
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

// ComPtr
#include <wrl.h>

class KeyboardInput
{
public:

	// クラスは基本独立させるべきなのでエイリアステンプレートを使う
	// namespace省略
	template<class T>using ComPtr = Microsoft::WRL::ComPtr<T>;

private:
	// メンバ変数
    ComPtr<IDirectInput8> directInput_ = nullptr;
	ComPtr<IDirectInputDevice8> keyboard_ = nullptr;
	WindowsApplication* winApp_ = nullptr;

public:

	// 全キーの入力状態を取得
	BYTE keys_[256] = {}; // 0番~255番のキーまである
	BYTE preKeys_[256] = {};

	/// <summary>キーボードのデバイスを作る（前面にある時だけ・非排他・Windows キー無効）</summary>
	/// <param name="winApp">入力を受けるウィンドウ</param>
	/// <param name="directInput">共有の DirectInput</param>
	void Initialize(WindowsApplication* winApp, IDirectInput8* directInput);

	/// <summary>前フレームの状態を控えてから、全キーの押下状態を読み直す</summary>
	void Update();

	/// <summary>リプレイ再生用：前フレームの状態を控えてから、記録した押下状態を流し込む（ハードは読まない）</summary>
	/// <param name="keys">DIK ごとの押下状態（0x80=押下）</param>
	void ApplyReplay(const BYTE keys[256]);

	/// <summary>キーを押しているか</summary>
	/// <param name="keyNum">DIK_* のキー番号</param>
	/// <returns>押していれば true</returns>
	bool PushKey(BYTE keyNum);

	/// <summary>キーを押した瞬間か（前フレームは離していて今フレーム押した）</summary>
	/// <param name="keyNum">DIK_* のキー番号</param>
	/// <returns>押した瞬間なら true</returns>
	bool TriggerKey(BYTE keyNum);
};

