#pragma once
#include <string>
#include <array>
#include <fstream>
#include <mutex>

/// <summary>
/// セッション単位のファイルログ基盤（シングルトン）。
/// 1セッション1フォルダ（Logs/YYYY-MM-DD_HHMMSS/）配下に
/// カテゴリ別ファイル（input/state/event/gfx/error/session）を出力する。
/// 1行1レコード形式: "HH:MM:SS.mmm CAT LEVEL message"
/// S.U.N.D.A.Y. が追尾・解析することを前提にした設計。
/// </summary>
class SessionLogger {
public:
    /// <summary>
    /// ログカテゴリ（＝出力ファイル）
    /// </summary>
    enum class Category {
        Input,    // input.log   入力アクションの押下/解放
        State,    // state.log   フレーム/座標/HP/シーン
        Event,    // event.log   DEATH/GAMEOVER/SCENE_CHANGE/MOVE_BLOCKED
        Gfx,      // gfx.log     DirectX警告・デバイスロスト等
        Error,    // error.log   一般エラー
        Session,  // session.log KPI・診断・一般INFO（セッション全体の流れ）
        kCount
    };

    /// <summary>
    /// ログレベル（値が小さいほど深刻）
    /// </summary>
    enum class Level {
        Critical,
        Error,
        Warn,
        Info,
        Debug,
        Trace
    };

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static SessionLogger& Instance();

    /// <summary>
    /// セッションフォルダを作成し各カテゴリのファイルを開く。
    /// Framework::Initialize の最初に1回だけ呼ぶ。
    /// </summary>
    void Initialize();

    /// <summary>
    /// 1レコード出力。カテゴリ別の最低レベルで間引かれる。
    /// </summary>
    void Write(Category category, Level level, const std::string& message);

    /// <summary>
    /// カテゴリごとの「出力する最低レベル」を設定する。
    /// 例: Info を指定すると Debug/Trace は捨てられる。
    /// </summary>
    void SetCategoryLevel(Category category, Level minLevel);

    /// <summary>
    /// 全ファイルを flush して閉じる。Framework::Finalize で呼ぶ。
    /// </summary>
    void Finalize();

    /// <summary>
    /// 今回のセッションフォルダパス（例: "Logs/2026-06-11_212430"）。
    /// 未初期化なら空文字。
    /// </summary>
    const std::string& GetSessionDir() const { return sessionDir_; }

private:
    SessionLogger() = default;
    ~SessionLogger();
    SessionLogger(const SessionLogger&) = delete;
    SessionLogger& operator=(const SessionLogger&) = delete;

    static constexpr size_t kCategoryCount = static_cast<size_t>(Category::kCount);

    std::string sessionDir_;
    std::array<std::ofstream, kCategoryCount> files_;
    std::array<Level, kCategoryCount> minLevels_;
    bool initialized_ = false;
    mutable std::mutex mutex_;
};
