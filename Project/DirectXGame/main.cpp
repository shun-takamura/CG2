// S.U.N.D.A.Y.
#include "Game.h"
#include "CrashHandler.h"

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 最初にクラッシュ時の minidump 出力を仕込む（以降の未捕捉例外で .dmp を残す）
    CrashHandler::Install();

    std::unique_ptr<Framework> gameInstance = std::make_unique<Game>();

    gameInstance->Run();

    return 0;
}
