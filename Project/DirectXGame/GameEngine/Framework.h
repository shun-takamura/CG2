#pragma once

#include <cstdint>
#include <math.h>
#define _USE_MATH_DEFINES
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

// ComPtr
#include <wrl.h>

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <iostream>
#include <dxcapi.h>

#include "SkyboxManager.h"
#include "SkinningComputeManager.h"
#include "ShadowMap.h"

// 前方宣言
class ConvertStringClass;
class WindowsApplication;
class DirectXCore;
class SRVManager;
class SpriteManager;
class Object3DManager;
class SkyboxManager;
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

	/// <summary>
	/// CSM シャドウマップ（全シーン共有）。ImGui 調整などから参照する。
	/// </summary>
	ShadowMap* GetShadowMap() const { return shadowMap_.get(); }

protected:
	// 終了リクエストフラグ
	bool endRequest_ = false;

	// CLI フラグ: --no-dstorage で DStorage 経路を封じる (KPI 計測比較用)
	bool noDStorage_ = false;

	// KPI 計測: Run() の冒頭で起点を打ち、最初の Update で経過時間 + VRAM/RAM/CPU をログに出す
	std::chrono::high_resolution_clock::time_point kpiStartTime_{};
	uint64_t kpiStartCpuTime100ns_ = 0;  // GetProcessTimes の Kernel+User (100ns 単位)
	bool kpiLogged_ = false;

	// 基本システム
	std::unique_ptr<ConvertStringClass> convertStringClass_;
	std::unique_ptr<WindowsApplication> winApp_;
	std::unique_ptr<DirectXCore> dxCore_;
	std::unique_ptr<SRVManager> srvManager_;

	// マネージャー
	std::unique_ptr<SpriteManager> spriteManager_;
	std::unique_ptr<Object3DManager> object3DManager_;
	std::unique_ptr<SkyboxManager> skyboxManager_;
	std::unique_ptr<SkinningComputeManager> skinningComputeManager_;

	// CSM シャドウマップ（平行光源1個。全シーン共有）
	std::unique_ptr<ShadowMap> shadowMap_;

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