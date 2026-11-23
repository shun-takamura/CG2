// 脱Discord化テスト
// S.U.N.D.A.Y.
#include "Game.h"
#include "CrashHandler.h"

/// <summary>Windows アプリのエントリーポイント。クラッシュ時のダンプ出力を仕込み、Game を作って終了まで回す</summary>
/// <returns>終了コード（常に 0）</returns>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 最初にクラッシュ時の minidump 出力を仕込む（以降の未捕捉例外で .dmp を残す）
    CrashHandler::Install();

    std::unique_ptr<Framework> gameInstance = std::make_unique<Game>();

    gameInstance->Run();

    return 0;
}
