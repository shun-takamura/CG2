#pragma once

#include <cstdint>
#include <math.h>
#define _USE_MATH_DEFINES
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory> 

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
	std::unique_ptr<ConvertStringClass> convertStringClass_;
	std::unique_ptr<WindowsApplication> winApp_;
	std::unique_ptr<DirectXCore> dxCore_;
	std::unique_ptr<SRVManager> srvManager_;

	// マネージャー
	std::unique_ptr<SpriteManager> spriteManager_;
	std::unique_ptr<Object3DManager> object3DManager_;

	// 入力
	std::unique_ptr<InputManager> input_;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_;

	// dxcコンパイラ
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;

	// ビューポートとシザー矩形
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};
};