#include "CrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>
#include <cstdio>

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

    // クラッシュしたスレッドのスタックを .pdb でシンボル化して
    // 関数名 + file:line のテキストに書き出す（SUNDAY のローカルLLM原因究明用）。
    // dbghelp は CreateFile/StackWalk64 を使うため、クラッシュ時でも比較的安全。
    void WriteStackTrace(EXCEPTION_POINTERS* ep, const std::string& path) {
        FILE* fp = nullptr;
        if (fopen_s(&fp, path.c_str(), "w") != 0 || !fp) {
            return;
        }

        const HANDLE process = GetCurrentProcess();
        const HANDLE thread = GetCurrentThread();

        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);  // exe 横の .pdb を読む

        // 例外の種類とアドレス（この時点で flush し、以降フォールトしても最低限残す）
        std::fprintf(fp, "exception_code=0x%08lX address=0x%llX\n",
            ep->ExceptionRecord->ExceptionCode,
            reinterpret_cast<unsigned long long>(ep->ExceptionRecord->ExceptionAddress));
        std::fflush(fp);

        CONTEXT context = *ep->ContextRecord;  // StackWalk64 が書き換えるのでコピー
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;       frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;    frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;    frame.AddrStack.Mode = AddrModeFlat;

        char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;

        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                    nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            const DWORD64 addr = frame.AddrPC.Offset;
            if (addr == 0) break;

            std::fprintf(fp, "#%-2d 0x%llX ", i, static_cast<unsigned long long>(addr));

            DWORD64 disp = 0;
            if (SymFromAddr(process, addr, &disp, sym)) {
                std::fprintf(fp, "%s", sym->Name);
            } else {
                std::fprintf(fp, "(unknown)");
            }

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(process, addr, &lineDisp, &line)) {
                std::fprintf(fp, "  %s:%lu", line.FileName, line.LineNumber);
            }
            std::fprintf(fp, "\n");
            std::fflush(fp);  // 1フレームごとに確実に書き出す（途中で落ちても残す）
        }

        SymCleanup(process);
        std::fclose(fp);
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

        // シンボル付きスタックトレースも出す（SUNDAY のローカルLLM原因究明 / Issue 用）
        const std::string stackPath = g_dumpDir.empty()
            ? ("crash_stack_" + TimeStampForFile() + ".txt")
            : (g_dumpDir + "/crash_stack.txt");
        WriteStackTrace(ep, stackPath);

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
