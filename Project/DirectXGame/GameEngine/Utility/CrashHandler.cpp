#include "CrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace {
    // ダンプ出力先（SetDumpDir でセッションフォルダをキャッシュ）。
    // 起動時に設定し、クラッシュ時に読むだけ＝set-once 運用なので生グローバルで持つ。
    std::string g_dumpDir;

    // YYYYMMDD_HHMMSS（フォルダ未確定時のフォールバック名用）
    std::string TimeStampForFile() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[32];
        wsprintfA(buf, "%04d%02d%02d_%02d%02d%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* ep) {
        // 出力先：セッションフォルダが分かっていれば crash.dmp、無ければカレントへ
        const std::string path = g_dumpDir.empty()
            ? ("crash_" + TimeStampForFile() + ".dmp")
            : (g_dumpDir + "/crash.dmp");

        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;

            // スタック＋スレッド情報＋間接参照メモリ（軽量だが原因追跡に十分）
            const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
                MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                type, &mei, nullptr, nullptr);
            CloseHandle(hFile);

            OutputDebugStringA(("[CrashHandler] minidump written: " + path + "\n").c_str());
        }

        // ダンプを出したらプロセスを終了させる（既定のクラッシュ処理へ）
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

namespace CrashHandler {
    void Install() {
        SetUnhandledExceptionFilter(TopLevelExceptionFilter);
    }

    void SetDumpDir(const std::string& dir) {
        g_dumpDir = dir;
    }
}
