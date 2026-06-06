// J.A.R.V.I.S. 統合テスト
#include "Game.h"

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	// Frameworkクラスの派生クラスGameをインスタンス化し、ユニークポインタで管理する
	std::unique_ptr<Framework> game = std::make_unique<Game>();

	// ゲームループを開始する
	game->Run();

	return 0;
}
