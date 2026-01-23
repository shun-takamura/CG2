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
class WindowsApplication;
class DirectXCore;
class SRVManager;
class SpriteManager;
class Object3DManager;
class InputManager;
class AbstractSceneFactory;

/// <summary>
/// フレームワーク（汎用部分）
/// </summary>
class Framework {
public:
	/// <summary>
	/// 仮想デストラクタ
	/// </summary>
	virtual ~Framework() = default;

	/// <summary>
	/// 初期化
	/// </summary>
	virtual void Initialize();

	/// <summary>
	/// 終了
	/// </summary>
	virtual void Finalize();

	/// <summary>
	/// 毎フレーム更新
	/// </summary>
	virtual void Update();

	/// <summary>
	/// 描画
	/// </summary>
	virtual void Draw() = 0;

	/// <summary>
	/// 終了チェック
	/// </summary>
	virtual bool IsEndRequest() { return endRequest_; }

	/// <summary>
	/// 実行
	/// </summary>
	void Run();

protected:
	// 終了リクエストフラグ
	bool endRequest_ = false;

	// 基本システム
	ConvertStringClass* convertStringClass_ = nullptr;
	WindowsApplication* winApp_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// dxcコンパイラ関連
	IDxcUtils* dxcUtils_ = nullptr;
	IDxcCompiler3* dxcCompiler_ = nullptr;
	IDxcIncludeHandler* includeHandler_ = nullptr;

	// マネージャー類
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;

	// 入力
	InputManager* input_ = nullptr;

	// シーンファクトリー
	AbstractSceneFactory* sceneFactory_ = nullptr;

	// ビューポートとシザー矩形
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};
};