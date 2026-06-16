#pragma once

// P.E.P.P.E.R. 計装マクロ。
// USE_PEPPER 定義時のみ実体化し、未定義（Release 等）では空展開でオーバーヘッドゼロ。
// 計装したい箇所は #include "PepperMacros.h" して PEPPER_SCOPE("名前") を1行入れるだけ。

#ifdef USE_PEPPER

#include "Profiler.h"

// __LINE__ を変数名に連結し、同一スコープ内で名前衝突しない一時オブジェクトを作る
#define PEPPER_CONCAT_INNER(a, b) a##b
#define PEPPER_CONCAT(a, b) PEPPER_CONCAT_INNER(a, b)

// CPU 区間計測。関数先頭などに1行。ブロックを抜けた瞬間に区間時間が確定する。
#define PEPPER_SCOPE(name) \
    ProfileScope PEPPER_CONCAT(pepperScope_, __LINE__)((name), __FILE__, __LINE__)

// GPU 区間計測。ステップ4（D3D12 タイムスタンプクエリ）で実装予定。現状は空展開。
#define PEPPER_GPU_SCOPE(commandList, name) ((void)0)

// 1フレーム分の集計をウィンドウへ畳み込み、1秒ごとに profile.log へ書き出す。フレームループ末尾で1回。
#define PEPPER_END_FRAME() Profiler::Instance().EndFrame()

// 未出力ウィンドウを強制書き出し。終了処理で1回。
#define PEPPER_FLUSH() Profiler::Instance().Flush()

#else

#define PEPPER_SCOPE(name) ((void)0)
#define PEPPER_GPU_SCOPE(commandList, name) ((void)0)
#define PEPPER_END_FRAME() ((void)0)
#define PEPPER_FLUSH() ((void)0)

#endif // USE_PEPPER
