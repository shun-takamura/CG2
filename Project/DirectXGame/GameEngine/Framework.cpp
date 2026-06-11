#include "Framework.h"

#include "DirectXTex.h"
#include "d3dx12.h"
#include <format>
#include <chrono>
#include <psapi.h>
#include <filesystem>
#pragma comment(lib, "psapi.lib")

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

//============================
// 自作ヘッダーのインクルード
//============================
#include "ConvertStringClass.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "AssetLocator.h"
#include "DStorageManager.h"
#include "SpriteManager.h"
#include "Object3DManager.h"
#include "Log.h"
#include "SessionLogger.h"
#include "RandomGenerator.h"
#include "ReplaySystem.h"
#include "TextureManager.h"
#include <random>
#include "ModelManager.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "InputManager.h"
#include "ImGuiManager.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "CameraCapture.h"
#include "PrimitivePipeline.h"
#include "LineRenderer.h"
#include "SkinningComputeManager.h"

void Framework::Run() {
	// KPI: 計測起点 (Run の入り口 = 実質プロセス開始直後)
	kpiStartTime_ = std::chrono::high_resolution_clock::now();
	{
		FILETIME ftCreate, ftExit, ftKernel, ftUser;
		if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser)) {
			ULARGE_INTEGER k{}, u{};
			k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
			u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
			kpiStartCpuTime100ns_ = k.QuadPart + u.QuadPart;
		}
	}

	// ゲームの初期化
	Initialize();

	while (true) { // ゲームループ
		// 毎フレーム更新
		Update();

		// 終了リクエストが来たら抜ける
		if (IsEndRequest()) {
			break;
		}

		// 描画
		Draw();
	}

	// ゲームの終了
	Finalize();
}

void Framework::Initialize() {
	// セッションログ基盤を最初に起こす（以降の Log() がファイルに残る）
	SessionLogger::Instance().Initialize();

	// 中央乱数の初期化: 起動引数 --seed N があればそれを採用、無ければ random_device で生成。
	// 使用シードは session.log に記録し、リプレイ時に同一シードから再現できるようにする。
	{
		uint32_t seed = 0;
		bool seedProvided = false;
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (argv) {
			for (int i = 1; i < argc; ++i) {
				if (std::wcscmp(argv[i], L"--seed") == 0 && i + 1 < argc) {
					seed = static_cast<uint32_t>(std::wcstoul(argv[i + 1], nullptr, 10));
					seedProvided = true;
				}
			}
			::LocalFree(argv);
		}
		if (!seedProvided) {
			std::random_device rd;
			seed = rd();
		}
		RandomGenerator::Instance().Initialize(seed);
		Log("[Random] seed=" + std::to_string(seed) + (seedProvided ? " (injected)\n" : " (generated)\n"));

		// リプレイ記録の開始（dt と入力を input.log に毎フレーム記録する）
		ReplaySystem::Instance().InitializeRecord(seed);
	}

	// COMの初期化
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// クラスの生成
	convertStringClass_ = std::make_unique<ConvertStringClass>();

	// Windowsアプリケーション生成
	winApp_ = std::make_unique<WindowsApplication>();

	//========================
	// ウィンドウの初期化
	//========================
	// 初期化（タイトル）
	winApp_->Initialize(L"QR");

	// アセットローダー初期化
	// 優先順位: CLI 引数 > 環境変数 > Release ビルドのデフォルト pack > 個別ファイル直読み
	{
		bool usePack = false;
#if defined(NDEBUG) && !defined(_DEBUG)
		usePack = true;  // Release ビルドのデフォルト
#endif
		// CLI 引数 (--use-pack / --use-fs / --no-dstorage)
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (argv) {
			for (int i = 1; i < argc; ++i) {
				if (std::wcscmp(argv[i], L"--use-pack") == 0) usePack = true;
				else if (std::wcscmp(argv[i], L"--use-fs") == 0) usePack = false;
				else if (std::wcscmp(argv[i], L"--no-dstorage") == 0) noDStorage_ = true;
			}
			::LocalFree(argv);
		}
		// 環境変数
		if (size_t needed = 0; _wgetenv_s(&needed, nullptr, 0, L"USE_PACK_ASSETS") == 0 && needed > 0) {
			std::wstring buf(needed, L'\0');
			if (_wgetenv_s(&needed, buf.data(), buf.size(), L"USE_PACK_ASSETS") == 0) {
				if (!buf.empty() && buf.front() == L'1') usePack = true;
				else if (!buf.empty() && buf.front() == L'0') usePack = false;
			}
		}

		if (usePack) {
			// 候補パス（先頭から試す）:
			//   Release 配布: exe と同じ階層の Generated/Assets.pack
			//   開発時 (CWD=Project/): リポジトリルート側の ../Generated/Assets.pack
			const char* candidates[] = {
				"../Generated/Assets.pack",  // 開発時: CWD=Project/ から repo ルート Generated/
				"Generated/Assets.pack",     // Release: 配布展開先 CWD と同階層 Generated/
				"../../Assets.pack",         // 直接実行: Generated/Output/Release/ から 2 段上
			};
			bool opened = false;
			for (const char* p : candidates) {
				if (AssetLocator::GetInstance()->InitializeFromPack(p)) {
					Log(std::string("[AssetLocator] mode = pack (") + p + ")\n");
					opened = true;
					break;
				}
			}
			if (!opened) {
				AssetLocator::GetInstance()->InitializeFromFilesystem();
				Log("[AssetLocator] pack mode requested but Assets.pack not found; falling back to FS\n");
			}
		} else {
			AssetLocator::GetInstance()->InitializeFromFilesystem();
			Log("[AssetLocator] mode = filesystem\n");
		}
	}

	dxCore_ = std::make_unique<DirectXCore>();
	dxCore_->Initialize(winApp_.get());

	// DirectStorage 初期化（device 作成後すぐ）
	if (DStorageManager::GetInstance()->Initialize(dxCore_->GetDevice())) {
		// 診断: CWD と GDeflate GPU サポート状況をログ出力
		{
			std::error_code ec;
			Log("[Diag] CWD = " + std::filesystem::current_path(ec).string() + "\n");
		}
		DSTORAGE_COMPRESSION_SUPPORT gdSupport =
			DStorageManager::GetInstance()->QueryGDeflateSupport();
		Log(std::format(
			"[Diag] GDeflate support: GPU_OPTIMIZED={} GPU_FALLBACK={} CPU_FALLBACK={}\n",
			(gdSupport & DSTORAGE_COMPRESSION_SUPPORT_GPU_OPTIMIZED) ? "Y" : "N",
			(gdSupport & DSTORAGE_COMPRESSION_SUPPORT_GPU_FALLBACK)  ? "Y" : "N",
			(gdSupport & DSTORAGE_COMPRESSION_SUPPORT_CPU_FALLBACK)  ? "Y" : "N"));

		// pack ファイルを開く（dev/release で候補を順に試す）
		const char* packCandidates[] = {
			"../Generated/Assets.pack",  // 開発時: CWD=Project/ から repo ルート Generated/
			"Generated/Assets.pack",     // Release: 配布展開先 CWD と同階層 Generated/
			"../../Assets.pack",         // 直接実行: Generated/Output/Release/ から 2 段上
		};
		for (const char* p : packCandidates) {
			std::error_code ec;
			auto sz = std::filesystem::file_size(p, ec);
			Log(std::format("[Diag] pack candidate '{}' : {}\n",
				p, ec ? std::string("missing") : std::format("{} bytes", sz)));
			if (DStorageManager::GetInstance()->OpenPackFile(p)) {
				Log(std::format("[Diag] DStorage opened '{}'\n", p));
				break;
			}
		}
		// D.1 検証: pack ヘッダーを EnqueueMemoryRead で読んでログ出力
		if (auto* file = DStorageManager::GetInstance()->GetPackFile()) {
			uint32_t header[4] = {};
			DStorageManager::GetInstance()->EnqueueMemoryRead(file, 0, sizeof(header), header);
			DStorageManager::GetInstance()->SubmitAndWait();
			Log("[DStorage TEST] magic=0x" + std::format("{:08X}", header[0]) +
			    " version=" + std::to_string(header[1]) +
			    " asset_count=" + std::to_string(header[2]) +
			    " index_offset=" + std::to_string(header[3]) + "\n");
		}
		// --no-dstorage: 初期化後に経路を封じる（KPI 計測用）
		if (noDStorage_) {
			DStorageManager::GetInstance()->SetEnabled(false);
			Log("[DStorage] disabled by --no-dstorage (KPI measurement)\n");
		}
	}

	srvManager_ = std::make_unique<SRVManager>();
	srvManager_->Initialize(dxCore_.get());

	// SRV化したdepthをポストエフェクトから読めるように登録
	dxCore_->RegisterDepthSRV(srvManager_.get());

	ImGuiManager::Instance().Initialize(
		winApp_->GetHwnd(),
		dxCore_.get(),
		srvManager_.get()
	);

	//========================================
	// ここにデバッグレイヤー デバックの時だけ出る
	//========================================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();

		// GPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif // DEBUG

	//=========================
	// エラー、警告が出たら即停止
	//=========================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(dxCore_->GetDevice()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバいエラーの時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		//======================
		// エラーと警告の抑制
		//======================
		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
#endif // _DEBUG

	// dxcCompilerを初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないがincludeに対応するための設定を行う
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

	// ライトの初期化
	LightManager::GetInstance()->Initialize(dxCore_.get());

	// Spriteの共通部分の初期化
	spriteManager_ = std::make_unique<SpriteManager>();
	spriteManager_->Initialize(dxCore_.get(), srvManager_.get());

	// テクスチャマネージャの初期化
	TextureManager::GetInstance()->Initialize(spriteManager_.get(), dxCore_.get(), srvManager_.get());

	// モデルマネージャーの初期化
	ModelManager::GetInstance()->Initialize(dxCore_.get());

	// 3DObjectの共通部分の初期化
	object3DManager_ = std::make_unique<Object3DManager>();
	object3DManager_->Initialize(dxCore_.get());

	// ComputeShader版Skinningの共通部分の初期化
	skinningComputeManager_ = std::make_unique<SkinningComputeManager>();
	skinningComputeManager_->Initialize(dxCore_.get());

	// Skyboxの共通部分の初期化
	skyboxManager_ = std::make_unique<SkyboxManager>();
	skyboxManager_->Initialize(dxCore_.get());

	// パーティクルマネージャーの初期化
	ParticleManager::GetInstance()->Initialize(dxCore_.get(), srvManager_.get());

	// テキストレンダラーの初期化（フォントは MPLUS 1p Medium、bake 32px）
	TextRenderer::GetInstance()->Initialize(
		dxCore_.get(), srvManager_.get(),
		"Resources/Fonts/MPLUS1p-Medium.ttf", 32, 1024);

	// プリミティブパイプラインの初期化
	PrimitivePipeline::GetInstance()->Initialize(dxCore_.get(), srvManager_.get());

	// 線描画パイプラインの初期化
	LineRenderer::GetInstance()->Initialize(dxCore_.get());

	// サウンドマネージャーの初期化
	SoundManager::GetInstance()->Initialize();

	// 初期化時（SoundManagerのInitialize後に呼ぶ）
	CameraCapture::GetInstance()->Initialize();

	//==============================
	// Inputの初期化
	//==============================
	input_ = std::make_unique<InputManager>();
	input_->Initialize(winApp_.get());

	//==============================
	// SceneManagerの初期化
	//==============================
	SceneManager::GetInstance()->Initialize(
		spriteManager_.get(),
		object3DManager_.get(),
		skyboxManager_.get(),
		dxCore_.get(),
		srvManager_.get(),
		input_.get(),
		skinningComputeManager_.get()
	);

	//=========================
	// ViewportとScissor
	//=========================
	// ビューポート
	viewport_.Width = WindowsApplication::kClientWidth;
	viewport_.Height = WindowsApplication::kClientHeight;
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形
	scissorRect_.left = 0;
	scissorRect_.right = WindowsApplication::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WindowsApplication::kClientHeight;
}

void Framework::Update() {
	// 初回 Update で KPI を 1 度だけ出力
	// (Initialize 完了 = 最初のシーンが GPU リソース全部揃った直後の地点)
	if (!kpiLogged_) {
		const auto now = std::chrono::high_resolution_clock::now();
		const auto loadMs = std::chrono::duration<double, std::milli>(
			now - kpiStartTime_).count();

		// AssetLocator のモード文字列
		const char* mode = AssetLocator::GetInstance()->GetModeName();
		const bool dsActive = DStorageManager::GetInstance()->IsInitialized()
			&& DStorageManager::GetInstance()->GetPackFile() != nullptr;
		std::string modeStr = mode;
		if (std::string(mode) == "Pack") {
			modeStr = dsActive ? "Pack+DStorage" : "Pack+CPU";
		}

		// VRAM 取得 (HIGH_PERFORMANCE adapter から)
		uint64_t vramBytes = 0;
		Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
		if (SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&factory)))) {
			Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
			if (SUCCEEDED(factory->EnumAdapterByGpuPreference(
				0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(&adapter)))) {
				DXGI_QUERY_VIDEO_MEMORY_INFO info{};
				if (SUCCEEDED(adapter->QueryVideoMemoryInfo(
					0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
					vramBytes = info.CurrentUsage;
				}
			}
		}
		const double vramMB = static_cast<double>(vramBytes) / (1024.0 * 1024.0);

		// プロセス RAM (Working Set / Private Bytes)
		double wsMB = 0.0, privMB = 0.0;
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		if (GetProcessMemoryInfo(GetCurrentProcess(),
			reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
			wsMB   = static_cast<double>(pmc.WorkingSetSize)  / (1024.0 * 1024.0);
			privMB = static_cast<double>(pmc.PrivateUsage)    / (1024.0 * 1024.0);
		}

		// CPU 時間 (Run() 起点から ここまでに使った Kernel+User 時間 100ns 単位 → ms)
		double cpuMs = 0.0;
		double cpuPct = 0.0;
		{
			FILETIME ftCreate, ftExit, ftKernel, ftUser;
			if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser)) {
				ULARGE_INTEGER k{}, u{};
				k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
				u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
				const uint64_t cur100ns = k.QuadPart + u.QuadPart;
				const uint64_t delta100ns = (cur100ns >= kpiStartCpuTime100ns_)
					? (cur100ns - kpiStartCpuTime100ns_) : 0;
				cpuMs = static_cast<double>(delta100ns) / 10000.0;  // 100ns → ms
				cpuPct = (loadMs > 0.0) ? (cpuMs / loadMs * 100.0) : 0.0;
			}
		}

		// pack ファイルサイズ (KPI モードが Pack 系のときのみ)
		uint64_t packBytes = 0;
		if (std::string(mode) == "Pack") {
			std::error_code ec;
			for (const char* p : { "../Generated/Assets.pack", "Generated/Assets.pack" }) {
				auto sz = std::filesystem::file_size(p, ec);
				if (!ec) { packBytes = sz; break; }
			}
		}
		const double packMB = static_cast<double>(packBytes) / (1024.0 * 1024.0);

		Log(std::format(
			"[KPI] mode={} loadTime={:.1f}ms vram={:.1f}MB "
			"ram={:.1f}MB(WS)/{:.1f}MB(Priv) cpuTime={:.1f}ms({:.0f}%) pack={:.1f}MB\n",
			modeStr, loadMs, vramMB, wsMB, privMB, cpuMs, cpuPct, packMB));

		kpiLogged_ = true;
	}

	// ウィンドウメッセージ処理
	if (!winApp_->ProcessMessage()) {
		endRequest_ = true;
		return;
	}

	// WM_SIZE があったらここで Swapchain を作り直す（描画コマンドを積む前の安全地点）
	{
		int32_t newW = 0, newH = 0;
		if (winApp_->ConsumePendingResize(newW, newH)) {
			dxCore_->Resize(newW, newH);
		}
	}

	// ===== ImGuiフレーム開始 =====
	ImGuiManager::Instance().BeginFrame();

	// 入力の更新
	input_->Update();

	// シーンマネージャーの更新
	SceneManager::GetInstance()->Update();

	// リプレイ記録：このフレームが実際に使った dt と入力を input.log へ。
	// 入力・シーン更新の後（dt 確定済み、UpdateFixFPS は Draw 後なので今フレームの値）に記録する。
	ReplaySystem::Instance().RecordFrame(dxCore_->GetDeltaTime(), input_.get());
}

void Framework::Finalize() {
	// 出力ウィンドウへの文字出力
	Log("DirectX12isNotUseful\n");

	// COMの終了
	CoUninitialize();

	//=============
	// 解放処理
	//=============
	// 生成と逆順に行う

	// シーンマネージャーの終了処理
	SceneManager::GetInstance()->Finalize();

	// 入力を解放
	input_->Finalize();

	// 終了時（SoundManagerのFinalize前に呼ぶ）
	CameraCapture::GetInstance()->Finalize();

	// 音声データ解放
	SoundManager::GetInstance()->Finalize();

	// 線描画パイプライン終了処理
	LineRenderer::GetInstance()->Finalize();

	// プリミティブパイプライン終了処理
	PrimitivePipeline::GetInstance()->Finalize();

	// パーティクル終了処理
	ParticleManager::GetInstance()->Finalize();

	// テキストレンダラー終了処理
	TextRenderer::GetInstance()->Finalize();

	// DirectXCoreよりも先に解放
	TextureManager::GetInstance()->Finalize();
	LightManager::GetInstance()->Finalize();

	// DirectStorage 終了（dxCore より先に Queue/Fence を解放）
	DStorageManager::GetInstance()->Finalize();

	// ImGui終了処理
	ImGuiManager::Instance().Shutdown();

	dxCore_->Finalize();

	CloseWindow(winApp_->GetHwnd());
	winApp_->Finalize();

	// セッションログを閉じる（全ファイルを flush）
	SessionLogger::Instance().Finalize();
}