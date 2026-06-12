#pragma once
#include <string>

/// <summary>
/// 未捕捉例外（アクセス違反などのクラッシュ）時に minidump(.dmp) を出力する。
/// SEH のトップレベル例外フィルタ（SetUnhandledExceptionFilter）で捕まえる。
/// .pdb（シンボル）と組み合わせて原因箇所を辿れる。H.A.P.P.Y. のクラッシュ報告とも共用予定。
/// </summary>
namespace CrashHandler {
    /// <summary>
    /// 起動時に1回呼ぶ。トップレベル例外フィルタを登録する。
    /// </summary>
    void Install();

    /// <summary>
    /// ダンプ出力先フォルダ（セッションフォルダ）を通知する。
    /// クラッシュ時に SessionLogger の mutex を触らないよう、ここでパスをキャッシュしておく。
    /// 未設定のままクラッシュした場合はカレントに crash_<時刻>.dmp を出す。
    /// </summary>
    void SetDumpDir(const std::string& dir);
}
