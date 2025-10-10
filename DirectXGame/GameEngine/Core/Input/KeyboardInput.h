#pragma once

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

// これだとインクルードしたすべてのソースにusingの影響が出てよろしくない
// using namespace Microsoft::WRL;

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

public:

	// 全キーの入力状態を取得
	BYTE keys_[256] = {}; // 0番~255番のキーまである
	BYTE preKeys_[256] = {};

	// メンバ関数
	void Initialize(HINSTANCE hInstance,HWND hwnd);

	void Update();

	bool PuhsKey(BYTE keyNum);

	bool TriggerKey(BYTE keyNum);
};

