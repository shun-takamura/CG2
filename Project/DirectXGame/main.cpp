#include "Game.h"

// リリースチェック
struct D3DResourceLeakCheker {
	~D3DResourceLeakCheker() {
		// リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	D3DResourceLeakCheker leakCheck;

	Game game;

	// ゲームの初期化
	game.Initialize();

	while (true) { // ゲームループ

		// 毎フレーム更新
		game.Update();

		// 終了リクエストが来たら抜ける
		if (game.IsEndRequest()) {
			break;
		}

		// 描画
		game.Draw();
	}

	// ゲームの終了
	game.Finalize();
	
	return 0;
}