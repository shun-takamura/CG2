// J.A.R.V.I.S.
#include "Game.h"

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    std::unique_ptr<Framework> gameInstance = std::make_unique<Game>();

    gameInstance->Run();

    return 0;
}
