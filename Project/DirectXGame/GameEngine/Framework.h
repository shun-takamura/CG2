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
class ISceneRunner;

/// <summary>
/// フレームワーク（汎用部分）
/// </summary>
class Framework {
public:
	/// <summary>
	/// コンストラクタ / 仮想デストラクタ。
	///
	/// どちらも実装は .cpp 側に置く。メンバの unique_ptr が前方宣言のみの型
	/// （AbstractSceneFactory 等）を指しているため、ヘッダで = default にすると
	/// Framework を「構築する」または「破棄する」翻訳単位すべてで完全型が要求される。
	/// コンストラクタ側も対象なのは、メンバ構築が例外を投げたときの巻き戻しコードが
	/// unique_ptr の破棄を必要とするため。
	/// </summary>
	Framework();
	virtual ~Framework();

	/// <summary>
	/// ウィンドウのタイトルバーに出す文字列。
	/// アプリ側で override して自分のゲーム名を返す。
	/// </summary>
	/// <returns>タイトル文字列（既定は L"ArcanaEngine"）</returns>
	virtual const wchar_t* GetWindowTitle() const { return L"ArcanaEngine"; }

	/// <summary>
	/// exe に埋め込んだ ICON リソースの ID。0 なら Windows の既定アイコン。
	/// アプリが .rc でアイコンを持つ場合に override する（Documents/13_AppIcon.md）。
	/// </summary>
	/// <returns>ICON リソースの ID（既定は 0）</returns>
	virtual int GetWindowIconResourceId() const { return 0; }

	/// <summary>
	/// シーン駆動の委譲先を返す（依存性の逆転）。
	/// 既定は nullptr。ゲーム側（Game）が自身の SceneManager を返すよう override する。
	/// Framework はこの IF だけを通してシーンを Initialize/Update/Finalize する。
	/// </summary>
	/// <returns>シーン駆動の委譲先（既定は nullptr＝シーンを回さない）</returns>
	virtual ISceneRunner* GetSceneRunner() { return nullptr; }

	/// <summary>
	/// エンジン基盤（ウィンドウ・D3D12・アセットの読み込み経路・各マネージャ・入力）を作り、シーンランナーを初期化する。
	/// コマンドライン引数（--seed / --replay / --use-pack / --no-dstorage / --exit-after 等）もここで読む。
	/// 派生で override する時は先に基底を呼ぶ
	/// </summary>
	virtual void Initialize();

	/// <summary>
	/// シーンランナーと各マネージャを生成と逆順に解放し、ログを閉じる。
	/// 初期化が途中で失敗した時も呼ばれるので、未生成のメンバは触らない
	/// </summary>
	virtual void Finalize();

	/// <summary>
	/// 1 フレーム分の更新（ウィンドウメッセージ・入力・シーンランナー・サウンド・リプレイ記録）。初回だけ起動の KPI をログに出す。
	/// ウィンドウが閉じられた・--exit-after の時間が経った・リプレイが終わった時は終了リクエストを立てる
	/// </summary>
	virtual void Update();

	/// <summary>1 フレーム分の描画。派生（Game）が実装する</summary>
	virtual void Draw() = 0;

	/// <summary>終了リクエストが立っているか</summary>
	/// <returns>終了するなら true</returns>
	virtual bool IsEndRequest() { return endRequest_; }

	/// <summary>
	/// Initialize → (Update → Draw) のループ → Finalize を実行する。終了リクエストが立つまで戻らない。
	/// Initialize の時点で終了リクエストが立っていればループに入らず終わる
	/// </summary>
	void Run();

	/// <summary>
	/// CSM シャドウマップ（全シーン共有）。ImGui 調整などから参照する。
	/// </summary>
	/// <returns>シャドウマップ（Initialize 前は nullptr）</returns>
	ShadowMap* GetShadowMap() const { return shadowMap_.get(); }

protected:
	//====================
	// 派生（Game）が使うエンジンの部品。持ち主は Framework で、派生は借りて使うだけ
	//====================
	/// <summary>ウィンドウ（フルスクリーン切り替えなど）</summary>
	/// <returns>WindowsApplication</returns>
	WindowsApplication* GetWinApp() const { return winApp_.get(); }
	/// <summary>GPU リソースの作成・コマンドリスト・経過時間</summary>
	/// <returns>DirectXCore</returns>
	DirectXCore* GetDxCore() const { return dxCore_.get(); }
	/// <summary>SRV（テクスチャの記述子）の確保</summary>
	/// <returns>SRVManager</returns>
	SRVManager* GetSRVManager() const { return srvManager_.get(); }
	/// <summary>3D モデルの描画（影の設定・既定のカメラ）</summary>
	/// <returns>Object3DManager</returns>
	Object3DManager* GetObject3DManager() const { return object3DManager_.get(); }
	/// <summary>キーボード・マウス・パッドと入力アクション</summary>
	/// <returns>InputManager</returns>
	InputManager* GetInput() const { return input_.get(); }

	/// <summary>シーンを作る工場を渡す（持ち主は Framework になる）</summary>
	/// <param name="factory">ゲームのシーンを作る工場</param>
	void SetSceneFactory(std::unique_ptr<AbstractSceneFactory> factory);
	/// <summary>渡したシーンの工場</summary>
	/// <returns>工場（まだ渡していなければ nullptr）</returns>
	AbstractSceneFactory* GetSceneFactory() const { return sceneFactory_.get(); }

private:
	// 終了リクエストフラグ
	bool endRequest_ = false;

	// CLI フラグ: --no-dstorage で DStorage 経路を封じる (KPI 計測比較用)
	bool noDStorage_ = false;
	// CLI フラグ: --bench-textures で起動時にテクスチャロードを計測（Logs/load_bench.csv へ追記）
	bool benchTextures_ = false;
	// CLI フラグ: --exit-after-bench で計測後すぐ終了（tools/Python/run_load_bench.py 用）
	bool exitAfterBench_ = false;
	// CLI フラグ: --exit-after <秒> で、最初のフレームから指定秒（実時間）経ったら終了（描画計測の時間を揃える用。0 以下で無効）
	double exitAfterSeconds_ = 0.0;
	std::chrono::steady_clock::time_point exitAfterStart_{};
	bool exitAfterStarted_ = false;

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