#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>
#include <utility>
#include <vector>
#include "ConvertStringClass.h"
#include "WindowsApplication.h"

#include "Log.h"
#include "ConvertString.h"
#include <dxcapi.h>

#include"SRVManager.h"

#pragma comment(lib, "winmm.lib")

/// <summary>
/// DirectX12 の基盤クラス。
/// Device / CommandQueue / SwapChain / RTV / DSV / Fence などの管理を担当。
/// </summary>
class DirectXCore {
public:
	/// <summary>
	/// DirectX12 の初期化を行う（ファクトリ・デバイス・コマンド・スワップチェーン・RTV/DSV・フェンス・DXC）。
	/// 深度バッファは描画解像度、スワップチェーンはウィンドウのクライアント領域の大きさで作る
	/// </summary>
	/// <param name="winApp">描画先のウィンドウ（nullptr 不可）</param>
	void Initialize(WindowsApplication* winApp);

	/// <summary>
	/// SRV(CBV/SRV/UAV) のディスクリプタサイズを取得。
	/// </summary>
	/// <returns>1 記述子あたりの大きさ [byte]</returns>
	UINT GetSrvDescriptorSize() const {
	assert(device_);
	return device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	/// <summary>
	/// 描画コマンドの積み始め。バックバッファを RenderTarget にして黒でクリアし、16:9 の枠のビューポートを張る。
	/// DSV はバインドしない
	/// </summary>
	void BeginDraw();

	/// <summary>深度バッファを 1.0 でクリアする</summary>
	void ClearDepthBuffer();

	/// <summary>
	/// 現在のバックバッファを指定色でクリアし、深度バッファもクリアして DSV をバインドする。
	/// BeginDraw() はクリアも DSV バインドも行わない（PostEffect 経由で描く構成が既定のため）ので、
	/// スワップチェーンへ直接描く場合はこれを BeginDraw() の直後に呼ぶ。
	/// </summary>
	/// <param name="clearColor">RGBA 各 0..1 の 4 要素</param>
	void ClearRenderTarget(const float clearColor[4]);

	/// <summary>
	/// バックバッファを Present に戻してコマンドを実行し、Present 後に GPU の完了を待つ。
	/// その後コマンドリストを開き直し、フレームレートの調整と経過時間の更新をする
	/// </summary>
	void EndDraw();

	/// <summary>フェンスのイベントを閉じる</summary>
	void Finalize();

	/// <summary>D3D12 デバイス</summary>
	/// <returns>デバイス</returns>
	ID3D12Device* GetDevice() const { return device_.Get(); }

	/// <summary>直接コマンドキュー</summary>
	/// <returns>コマンドキュー</returns>
	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }

	/// <summary>フレームで共有するコマンドリスト（EndDraw / FlushCommandList の後は開き直してある）</summary>
	/// <returns>コマンドリスト</returns>
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }

	/// <summary>スワップチェーンのバックバッファ数</summary>
	/// <returns>バックバッファ数</returns>
	UINT GetBackBufferCount() const { return bufferCount_; }

	/// <summary>バックバッファ（RTV）のフォーマット</summary>
	/// <returns>フォーマット</returns>
	DXGI_FORMAT GetRenderTargetFormat() const { return backBufferFormat_; }

	/// <summary>
    /// SwapchainのRTVとビューポートを再設定する
    /// ポストエフェクトのマルチパス後に呼ぶ
    /// </summary>
	/// <param name="commandList">設定するコマンドリスト</param>
	void RestoreSwapchainRenderTarget(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// WM_SIZE 経由で受け取った新クライアントサイズに合わせて Swapchain と RTV を作り直す。
	/// 内部で GPU 完了待機を行うため、フレーム描画コマンドを積んでいない安全なタイミングで呼ぶこと。
	/// 深度バッファとオフスクリーンRT（PostEffect等）は更新しない（描画解像度はウィンドウと別）。
	/// </summary>
	/// <param name="width">新しい幅 [px]</param>
	/// <param name="height">新しい高さ [px]</param>
	void Resize(int32_t width, int32_t height);

	/// <summary>
	/// 現在の Swapchain の幅（ウィンドウのクライアント領域に追従）。
	/// ゲームの UI・文字の配置には使わない（GetUIWidth を使う）
	/// </summary>
	/// <returns>幅 [px]</returns>
	int32_t GetSwapChainWidth() const { return swapChainWidth_; }
	/// <summary>現在の Swapchain の高さ（UI の配置には GetUIHeight を使う）</summary>
	/// <returns>高さ [px]</returns>
	int32_t GetSwapChainHeight() const { return swapChainHeight_; }

	/// <summary>
	/// UI の仮想解像度の幅（1600 固定）。スプライト・文字・マウスの座標はすべてこの大きさ。
	/// 描画解像度やウィンドウの大きさが変わっても変わらない
	/// </summary>
	/// <returns>幅 [px]</returns>
	static constexpr int32_t GetUIWidth() { return WindowsApplication::kUIWidth; }
	/// <summary>UI の仮想解像度の高さ（900 固定）</summary>
	/// <returns>高さ [px]</returns>
	static constexpr int32_t GetUIHeight() { return WindowsApplication::kUIHeight; }

	/// <summary>
	/// 描画解像度の幅（3D・ポストエフェクトの RT と深度バッファの大きさ。16:9）。
	/// シーン RT のビューポートと、SV_Position を使うシェーダに渡す画面の大きさはこちら。
	/// UI はこの RT に UI の仮想解像度の射影で描くので、ビューポートを全面に張れば同じ配置で拡大される
	/// </summary>
	/// <returns>幅 [px]</returns>
	int32_t GetRenderWidth() const { return renderWidth_; }
	/// <summary>描画解像度の高さ</summary>
	/// <returns>高さ [px]</returns>
	int32_t GetRenderHeight() const { return renderHeight_; }

	/// <summary>
	/// 描画解像度の変更を予約する（オプション画面などから呼ぶ）。16:9 に丸める（高さは幅から決める）。
	/// 実際の作り直しは次の ApplyPendingRenderResolution で行う
	/// </summary>
	/// <param name="width">希望の幅 [px]（320〜7680 に収め、2 の倍数に切り下げる）</param>
	/// <param name="height">希望の高さ（使わない。幅から 16:9 で決める）</param>
	void RequestRenderResolution(int32_t width, int32_t height);

	/// <summary>
	/// 予約された描画解像度があれば、GPU の完了を待って深度バッファ（と深度の SRV）を作り直し true を返す。
	/// シーン RT など深度以外の作り直しは呼び出し側（Game）が行う。フレームの頭（描画コマンドを積む前）で呼ぶこと
	/// </summary>
	/// <returns>作り直したら true（予約が無い・今と同じ大きさなら false）</returns>
	bool ApplyPendingRenderResolution();

	/// <summary>スワップチェーンの中でゲームの絵を出す 16:9 の枠（外は黒帯）</summary>
	/// <returns>枠（スワップチェーンのピクセル単位）</returns>
	WindowsApplication::OutputRect GetOutputRect() const {
		return WindowsApplication::ComputeOutputRect(swapChainWidth_, swapChainHeight_);
	}

	/// <summary>深度書き込み用の DSV</summary>
	/// <returns>DSV のハンドル</returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const {
		return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	}

	/// <summary>深度を読み取り専用（READ_ONLY_DEPTH）で張る DSV（ヒープの 2 番目）</summary>
	/// <returns>DSV のハンドル</returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetReadOnlyDsvHandle() const {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += device_->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		return handle;
	}

	/// <summary>スワップチェーン</summary>
	/// <returns>スワップチェーン</returns>
	IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
	/// <summary>DSV のヒープ</summary>
	/// <returns>ヒープ</returns>
	ID3D12DescriptorHeap* GetDsvHeap() const { return dsvHeap_.Get(); }

	/// <summary>
	/// 深度をシェーダで読むための SRV を登録する（SRVManager 初期化後に呼ぶ）。
	/// 描画解像度を変えて深度を作り直した時に同じ SRVManager で登録し直すため、渡した物を覚えておく
	/// </summary>
	/// <param name="srvManager">SRV を確保するマネージャ</param>
	void RegisterDepthSRV(class SRVManager* srvManager);

	/// <summary>深度の SRV の番号</summary>
	/// <returns>SRVManager 上の番号</returns>
	uint32_t GetDepthSRVIndex() const { return depthSrvIndex_; }

	/// <summary>深度リソースの状態を遷移させる（同じ状態なら何もしない）</summary>
	/// <param name="commandList">バリアを積むコマンドリスト</param>
	/// <param name="afterState">遷移後の状態</param>
	void TransitionDepthState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES afterState);

	/// <summary>前フレームの経過時間（タイムスケールをかける前の生の値）</summary>
	/// <returns>経過時間 [秒]</returns>
	float GetDeltaTime() const { return deltaTime_; }

	/// <summary>
	/// リプレイ再生の切り替え。ON にすると UpdateFixFPS は実時計を読まず、OverrideDeltaTime で
	/// 与えた値をそのまま使う（1 フレームをその時間だけ待つので、記録時と同じ速さで進む）
	/// </summary>
	/// <param name="enable">リプレイ再生なら true</param>
	void SetReplayMode(bool enable) { replayMode_ = enable; }
	/// <summary>経過時間を外から与える（リプレイ再生用）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void OverrideDeltaTime(float dt) { deltaTime_ = dt; }

	/// <summary>GPU の完了まで待つ（リソース解放前など、外部からも呼ぶ用）</summary>
	void WaitForGpu();

	/// <summary>
	/// 記録済みのコマンド（テクスチャ転送など）を今すぐ実行し、GPU 完了まで待ってからリストを開き直す。
	/// フレームループの外（起動時のロード計測など）で「GPU で使える状態」まで確定させたいとき用。
	/// フレームの途中で呼ぶと、それまでに積んだコマンドがその場で実行される点に注意
	/// </summary>
	void FlushCommandList();

	/// <summary>グローバルタイムスケールをかけた経過時間</summary>
	/// <returns>経過時間 [秒]</returns>
	float GetScaledDeltaTime() const { return deltaTime_ * timeScale_; }

	/// <summary>グローバルタイムスケール</summary>
	/// <returns>倍率（0 で停止、1 で等速）</returns>
	float GetTimeScale() const { return timeScale_; }
	/// <summary>グローバルタイムスケールを設定する（負の値は 0 にする）</summary>
	/// <param name="scale">倍率</param>
	void SetTimeScale(float scale) { timeScale_ = (scale < 0.0f) ? 0.0f : scale; }

	/// <summary>固定（60fps）と可変フレームレートを切り替える</summary>
	/// <param name="useFixed">60fps 固定にするなら true</param>
	void SetUseFixedFrameRate(bool useFixed) { useFixedFrameRate_ = useFixed; }
	/// <summary>60fps 固定で回しているか</summary>
	/// <returns>固定なら true</returns>
	bool IsUsingFixedFrameRate() const { return useFixedFrameRate_; }

	/// <summary>このフレーム末に Signal される予定のフェンス値（中間バッファの解放判定用）</summary>
	/// <returns>フェンス値</returns>
	uint64_t GetNextFenceValue() const { return fenceValue_ + 1; }

	/// <summary>GPU が完了したフェンス値（これ以下の値で記録された中間バッファは解放してよい）</summary>
	/// <returns>フェンス値（フェンス未作成なら 0）</returns>
	uint64_t GetCompletedFenceValue() const { return fence_ ? fence_->GetCompletedValue() : 0; }

	/// <summary>CPU から書き込める UPLOAD ヒープのバッファを作る（定数・頂点バッファ用）</summary>
	/// <param name="sizeInBytes">大きさ [byte]</param>
	/// <returns>作ったバッファ（状態は GENERIC_READ）</returns>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	/// <summary>UAV 用のバッファを作る（DEFAULT ヒープ、ALLOW_UNORDERED_ACCESS、初期状態は COMMON）</summary>
	/// <param name="sizeInBytes">大きさ [byte]</param>
	/// <returns>作ったバッファ</returns>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBufferResource(size_t sizeInBytes);

	/// <summary>
	/// DEFAULT ヒープのバッファを作り、initialData を UPLOAD 中間バッファ経由でコピーして
	/// finalState に遷移させる。中間バッファは内部で TrackIntermediateResource に積まれる
	/// </summary>
	/// <param name="sizeInBytes">大きさ [byte]</param>
	/// <param name="initialData">初期データ（sizeInBytes 分）</param>
	/// <param name="commandList">コピーとバリアを積むコマンドリスト</param>
	/// <param name="finalState">コピー後の状態</param>
	/// <returns>作ったバッファ（コマンドが実行されるまで中身は入っていない）</returns>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		size_t sizeInBytes,
		const void* initialData,
		ID3D12GraphicsCommandList* commandList,
		D3D12_RESOURCE_STATES finalState);

	/// <summary>
	/// アップロード用中間バッファをこのフレームの fenceValue とペアで保持する
	/// （次フレーム以降の TickIntermediateResources で GPU 完了後に解放される）
	/// </summary>
	/// <param name="resource">保持する中間バッファ</param>
	void TrackIntermediateResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

	/// <summary>完了済みフェンス値以下の中間バッファを解放する（毎フレーム末に呼ぶ）</summary>
	void TickIntermediateResources();

	/// <summary>
	/// 指定 fenceValue が GPU 上で完了したら callback を実行するよう登録する。
	/// DirectStorage のバッチ完了通知や、async ロードの GPUReady 状態遷移等に使う想定。
	/// メインスレッドからのみ呼ぶ前提（mutex は無い）
	/// </summary>
	/// <param name="fenceValue">待つフェンス値</param>
	/// <param name="callback">完了後に呼ぶ関数</param>
	void EnqueueOnFenceComplete(uint64_t fenceValue, std::function<void()> callback);

	/// <summary>
	/// 完了済みフェンス値以下に登録された callback を全て実行する（毎フレーム末に呼ぶ）。
	/// callback の中から EnqueueOnFenceComplete してよい
	/// </summary>
	void TickPendingCallbacks();

	/// <summary>
	/// シェーダのバイナリを得る。エンジン内のシェーダ取得はすべてこれを通す。
	///   Debug                 : .hlsl を実行時コンパイル（書き換えて即試せるようにするため）
	///   Release / Development : Resources/CompiledShaders/ の .cso を読む（無ければ .hlsl の実行時コンパイルにフォールバック）
	/// 引数は常に .hlsl のパスを渡すこと。.cso のパスは内部で導出する。
	/// 一度得たバイナリは「パス＋プロファイル」でキャッシュし、シーンをまたいで使い回す。
	/// Debug は .hlsl の更新時刻が変わっていればコンパイルし直す（#include 先だけの変更は再起動まで反映されない）。
	/// .hlsl も .cso も無い・読めない時はログとメッセージボックスを出してプロセスを終了する
	/// </summary>
	/// <param name="hlslPath">Resources/Shaders/... の .hlsl パス</param>
	/// <param name="profile">vs_6_0 / ps_6_0 / cs_6_0 など</param>
	/// <returns>シェーダのバイナリ（キャッシュが持つので呼び出し側は Release しない。.cso を DXC で包めなかった時は nullptr）</returns>
	IDxcBlob* LoadShaderBlob(const std::wstring& hlslPath, const wchar_t* profile);

	/// <summary>シェーダ取得の累計（シーン切り替えの内訳の計測用。差分で使う）</summary>
	struct ShaderLoadStats {
		uint32_t loadCount = 0;  // コンパイル or .cso の読み込みをした回数
		uint32_t cacheHits = 0;  // キャッシュで済んだ回数
		double   loadMs = 0.0;   // loadCount の合計時間
	};
	/// <summary>シェーダ取得の累計</summary>
	/// <returns>累計（コピー）</returns>
	ShaderLoadStats GetShaderLoadStats() const { return shaderStats_; }

	/// <summary>LoadShaderBlob が呼ばれるたびに知らせる関数を登録する（キャッシュに当たっても。シーンの読み込みの記録用）</summary>
	/// <param name="observer">呼ばれる関数（空の関数で外す）</param>
	void SetShaderLoadObserver(std::function<void(const std::wstring& hlslPath, const wchar_t* profile)> observer) {
		shaderLoadObserver_ = std::move(observer);
	}

	/// <summary>
	/// .hlsl をその場で DXC コンパイルする（エントリは main、最適化なし・デバッグ情報入り）。通常は LoadShaderBlob を使うこと。
	/// ファイルを読めない時はログとメッセージボックスを出してプロセスを終了し、コンパイルエラーは Debug で assert する
	/// </summary>
	/// <param name="filePath">.hlsl のパス</param>
	/// <param name="profile">vs_6_0 / ps_6_0 / cs_6_0 など</param>
	/// <returns>コンパイル結果（所有権は呼び出し側）</returns>
	IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile);

	/// <summary>記述子ヒープを作る（失敗は assert）</summary>
	/// <param name="device">作るデバイス</param>
	/// <param name="heapType">ヒープの種類</param>
	/// <param name="numDescriptor">記述子の数</param>
	/// <param name="shaderVicible">シェーダから見えるヒープにするなら true</param>
	/// <returns>作ったヒープ</returns>
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		Microsoft::WRL::ComPtr<ID3D12Device> device,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescriptor,
		bool shaderVicible);

private:
	/// <summary>キャッシュを通さずにシェーダのバイナリを得る（Debug は .hlsl 優先、それ以外は .cso 優先）</summary>
	/// <param name="hlslPath">.hlsl のパス</param>
	/// <param name="profile">vs_6_0 / ps_6_0 / cs_6_0 など</param>
	/// <returns>シェーダのバイナリ（所有権は呼び出し側。.cso を DXC で包めなかった時は nullptr）</returns>
	IDxcBlob* LoadShaderBlobUncached(const std::wstring& hlslPath, const wchar_t* profile);

	/// <summary>DXGI ファクトリを作る</summary>
	void CreateFactory();
	/// <summary>
	/// 高性能なハードウェアアダプタでデバイスを作る。Debug はデバッグレイヤーを有効にし、
	/// --gpu-validation の時だけ GPU ベース検証も有効にする
	/// </summary>
	void CreateDevice();
	/// <summary>コマンドキュー・アロケータ・リストを作る</summary>
	void CreateCommandObjects();
	/// <summary>ダブルバッファのスワップチェーンを作る</summary>
	/// <param name="hwnd">描画先のウィンドウ</param>
	/// <param name="width">幅 [px]</param>
	/// <param name="height">高さ [px]</param>
	void CreateSwapChain(HWND hwnd, int32_t width, int32_t height);
	/// <summary>バックバッファの RTV を作る</summary>
	void CreateRenderTargets();
	/// <summary>深度バッファと DSV（書き込み用・読み取り専用の 2 つ）を作る。作り直しの時はヒープを使い回す</summary>
	/// <param name="width">幅 [px]</param>
	/// <param name="height">高さ [px]</param>
	void CreateDepthStencilView(int32_t width, int32_t height);
	/// <summary>フェンスと完了待ちのイベントを作る</summary>
	void CreateFenceObjects();

	/// <summary>フレームレート調整の基準時刻を今にする</summary>
	void InitializeFixFPS();
	/// <summary>
	/// フレームレートを調整して経過時間を更新する。固定なら 1/60 秒までスリープ＋待ち、
	/// 可変なら実時間（上限 0.1 秒）。リプレイ中は与えられた経過時間だけ待つ
	/// </summary>
	void UpdateFixFPS();

	/// <summary>スワップチェーンへの描画のビューポート・シザーを 16:9 の枠に合わせる</summary>
	/// <param name="commandList">設定するコマンドリスト</param>
	void SetOutputViewport(ID3D12GraphicsCommandList* commandList);

	// 前回フレームの時刻
	std::chrono::steady_clock::time_point reference_;

private:
	Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[2];
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer_;
	// Depth SRVのインデックス（SRVManagerに登録）
	uint32_t depthSrvIndex_ = 0;
	// Depthリソースの現在の状態
	D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;

	// LoadShaderBlob のキャッシュ（キー＝.hlsl のパス | プロファイル）
	struct ShaderCacheEntry {
		Microsoft::WRL::ComPtr<IDxcBlob> blob;
		std::filesystem::file_time_type hlslTime{}; // Debug の作り直し判定用
	};
	std::unordered_map<std::wstring, ShaderCacheEntry> shaderCache_;
	std::vector<Microsoft::WRL::ComPtr<IDxcBlob>> retiredShaderBlobs_; // Debug で作り直した古い blob（終了まで持つ）
	std::mutex shaderCacheMutex_;
	ShaderLoadStats shaderStats_;
	std::function<void(const std::wstring& hlslPath, const wchar_t* profile)> shaderLoadObserver_;

	// デルタタイム
	std::chrono::steady_clock::time_point lastFrameTime_;
	float deltaTime_ = 1.0f / 60.0f;
	bool useFixedFrameRate_ = false; // true: 60fps固定, false: 可変(VSyncの上限=モニタリフレッシュまで)
	bool replayMode_ = false;       // true: dt は外部供給（UpdateFixFPS は実時計を読まない）

	// グローバルタイムスケール（0で停止、1で等速、0.5でスロー、2で倍速）
	float timeScale_ = 1.0f;

	HANDLE fenceEvent_ = nullptr;
	UINT frameIndex_ = 0;
	UINT rtvDescriptorSize_ = 0;
	uint64_t fenceValue_ = 0;

	// テクスチャ/バッファアップロード用の中間 UPLOAD バッファ。
	// 各エントリは「この値以下の fenceValue が完了したら解放してよい」というペア。
	std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>> intermediateResources_;

	// fenceValue 完了時に呼ぶ汎用コールバック群。EnqueueOnFenceComplete で積み、
	// TickPendingCallbacks で完了済みのものを呼び出す。
	std::vector<std::pair<uint64_t, std::function<void()>>> pendingCallbacks_;
	WindowsApplication* winApp_ = nullptr;
	// スワップチェイン設定の記録用
	UINT bufferCount_ = 2;
	DXGI_FORMAT backBufferFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;

	// 現在の Swapchain サイズ。Resize() で書き換わる。
	int32_t swapChainWidth_ = WindowsApplication::kClientWidth;
	int32_t swapChainHeight_ = WindowsApplication::kClientHeight;

	// 描画解像度。既定は UI の仮想解像度と同じ（ゲームが起動時に RequestRenderResolution で変える）
	int32_t renderWidth_ = WindowsApplication::kUIWidth;
	int32_t renderHeight_ = WindowsApplication::kUIHeight;
	bool pendingRenderResolution_ = false;
	int32_t pendingRenderWidth_ = 0;
	int32_t pendingRenderHeight_ = 0;
	// 深度の SRV を作り直すときに使う（RegisterDepthSRV で覚える）
	SRVManager* depthSrvManager_ = nullptr;

};
