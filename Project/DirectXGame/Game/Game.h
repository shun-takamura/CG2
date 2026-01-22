#pragma once

#include <cstdint>
#include <math.h>
#define _USE_MATH_DEFINES
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

// ComPtr
#include <wrl.h>

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <iostream>
#include <dxcapi.h>

// 前方宣言
class ConvertStringClass;
class DebugCamera;
class WindowsApplication;
class DirectXCore;
class SRVManager;
class SpriteManager;
class SpriteInstance;
class Object3DManager;
class Object3DInstance;
class Camera;
class InputManager;

// ゲーム
class Game {
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	Game();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~Game();

	/// <summary>
	/// ゲームの初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 毎フレーム更新
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	void Draw();

	/// <summary>
	/// ゲームの終了
	/// </summary>
	void Finalize();

	/// <summary>
	/// 終了リクエストがあるか
	/// </summary>
	bool IsEndRequest() const { return isEndRequest_; }

private:
	// 終了リクエストフラグ
	bool isEndRequest_ = false;

	// 基本システム
	ConvertStringClass* convertStringClass_ = nullptr;
	DebugCamera* debugCamera_ = nullptr;
	WindowsApplication* winApp_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// dxcコンパイラ関連
	IDxcUtils* dxcUtils_ = nullptr;
	IDxcCompiler3* dxcCompiler_ = nullptr;
	IDxcIncludeHandler* includeHandler_ = nullptr;

	// スプライト関連
	SpriteManager* spriteManager_ = nullptr;
	SpriteInstance* sprite_ = nullptr;
	std::vector<SpriteInstance*> sprites_;

	// 3Dオブジェクト関連
	Object3DManager* object3DManager_ = nullptr;
	std::vector<Object3DInstance*> object3DInstances_;

	// カメラ
	Camera* camera_ = nullptr;

	// 入力
	InputManager* input_ = nullptr;

	// ビューポートとシザー矩形
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;
};