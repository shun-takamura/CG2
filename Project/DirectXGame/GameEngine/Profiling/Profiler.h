#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

/// <summary>
/// P.E.P.P.E.R. の計測本体（シングルトン）。
/// 名前付き区間ごとに CPU 時間（QueryPerformanceCounter）を計測し、
/// 1秒ウィンドウで集計（avg/max/sum/calls）して SessionLogger の
/// Profile カテゴリ（profile.log）へ書き出す。毎フレーム書くとログが爆発するため、
/// 区間ごとに集計してウィンドウ終端でまとめて1行ずつ出力する。
/// メインスレッド前提（区間スタックの順序保証のため）。
/// </summary>
class Profiler {
public:
    static Profiler& Instance();

    /// <summary>
    /// 区間の計測開始。__FILE__/__LINE__ を一緒に記録し、後で該当箇所へ飛べるようにする。
    /// 同名区間は1フレーム内で合算（呼び出し回数も集計）。
    /// </summary>
    void BeginSection(const char* name, const char* file, int line);

    /// <summary>
    /// 直近に開始した区間を閉じ、経過時間を加算する（RAII で対応）。
    /// </summary>
    void EndSection();

    /// <summary>
    /// 1フレームの集計をウィンドウへ畳み込む。1秒経過していればウィンドウを
    /// profile.log へ書き出してリセットする。フレームループの末尾で1回だけ呼ぶ。
    /// </summary>
    void EndFrame();

    /// <summary>
    /// GPU 計測結果（区間名・ms）を現フレームの集計へ加算する。
    /// GpuProfiler がフレーム末リードバック時に呼ぶ。CPU と同名なら同じ行にまとまる。
    /// </summary>
    void AddGpuMs(const char* name, double gpuMs);

    /// <summary>
    /// 名前付きカウンタを n だけ増やす（DrawCall 回数など「個数」の計測）。
    /// 時間ではないので区間とは別系統。ウィンドウごとに total/avg/max を出す。
    /// </summary>
    void Count(const char* name, int64_t n = 1);

    /// <summary>
    /// 名前付きゲージに「現在値」をセットする（RAM/VRAM/オブジェクト数など、合計でなくレベル）。
    /// 毎フレーム上書き。ウィンドウごとに cur(最新)/min/max/avg を出す。
    /// </summary>
    void SetGauge(const char* name, double value);

    /// <summary>
    /// 未出力のウィンドウを強制的に書き出す。終了処理（Framework::Finalize）で呼ぶ。
    /// </summary>
    void Flush();

    // ============================================================
    // ライブ表示用 API（PEPPER ImGui ウィンドウが毎フレーム読む）。
    // 直近に確定した1秒ウィンドウの集計スナップショットを保持する（毎秒更新＝数値は安定）。
    // 将来 EMA(毎フレーム平滑化) へ移行する場合も、ウィンドウUI・グラフ・カウンタ/ゲージは
    // そのまま流用でき、区間テーブルのデータソースだけ差し替えればよい設計。
    // 計測層はメインスレッド専用、ImGui もメインスレッド描画なのでロック不要。
    // ============================================================
    struct LiveSection {
        std::string name;
        const char* file = nullptr;
        int line = 0;
        int depth = 0;
        int presentFrames = 0;   // ウィンドウ内で出現したフレーム数
        int64_t totalCalls = 0;  // ウィンドウ内の総呼び出し回数
        double cpuAvgMs = 0.0;   // 1フレームあたり平均 CPU ms
        double cpuMaxMs = 0.0;   // 1フレームあたり最大 CPU ms（スパイク把握）
        double gpuAvgMs = 0.0;
        double gpuMaxMs = 0.0;
    };
    struct LiveCounter {
        std::string name;
        int64_t total = 0;
        int64_t maxPerFrame = 0;
        double avgPerFrame = 0.0;
    };
    struct LiveGauge {
        std::string name;
        double cur = 0.0;
        double minv = 0.0;
        double maxv = 0.0;
        double avg = 0.0;
    };

    const std::vector<LiveSection>& GetLiveSections() const { return liveSections_; }
    const std::vector<LiveCounter>& GetLiveCounters() const { return liveCounters_; }
    const std::vector<LiveGauge>&   GetLiveGauges()   const { return liveGauges_; }
    double GetLiveWindowSec()    const { return liveWindowSec_; }
    int    GetLiveWindowFrames() const { return liveWindowFrames_; }

    // フレーム時間（壁時計 ms、VSync 待ち込み）のリングバッファ。折れ線グラフ用。毎フレーム更新。
    static constexpr int kFrameRingSize = 240;
    const float* GetFrameMsRing() const { return frameMsRing_; }
    int    GetFrameMsRingSize()   const { return kFrameRingSize; }
    int    GetFrameMsRingHead()   const { return frameRingHead_; } // 次に書く位置(=最古)
    double GetLastFrameMs()       const { return lastFrameMs_; }

private:
    Profiler();
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // 区間ごとの「1フレーム」集計
    struct SectionStat {
        std::string name;
        const char* file = nullptr;  // __FILE__（静的文字列なのでポインタ保持で十分）
        int line = 0;
        int depth = 0;               // 区間スタックの深さ（初出時に確定）
        int calls = 0;
        double cpuMs = 0.0;
        double gpuMs = 0.0;          // GPU タイムスタンプ計測（同名区間に加算）
    };

    // 区間ごとの「1秒ウィンドウ」集計
    struct WindowStat {
        std::string name;
        const char* file = nullptr;
        int line = 0;
        int depth = 0;
        int presentFrames = 0;   // この区間が出現したフレーム数（avg の分母）
        int64_t totalCalls = 0;  // ウィンドウ内の総呼び出し回数
        double sumCpuMs = 0.0;   // 各フレームの cpu_ms 合計
        double maxCpuMs = 0.0;   // 1フレームあたり cpu_ms の最大（スパイク把握）
        double sumGpuMs = 0.0;   // 各フレームの gpu_ms 合計
        double maxGpuMs = 0.0;   // 1フレームあたり gpu_ms の最大
    };

    // 計測中の区間（開始時刻と集計先インデックス）
    struct ActiveScope {
        int64_t startTicks = 0;
        size_t index = 0;
    };

    // 名前付きカウンタの「1フレーム」値
    struct FrameCounter {
        std::string name;
        int64_t value = 0;
    };
    // 名前付きカウンタの「1秒ウィンドウ」集計
    struct WindowCounter {
        std::string name;
        int64_t total = 0;        // ウィンドウ内の合計
        int64_t maxPerFrame = 0;  // 1フレームの最大
        int presentFrames = 0;    // 出現フレーム数
    };

    // 名前付きゲージの「1フレーム」値（最新のレベル）
    struct FrameGauge {
        std::string name;
        double value = 0.0;
    };
    // 名前付きゲージの「1秒ウィンドウ」集計
    struct WindowGauge {
        std::string name;
        double last = 0.0;
        double minv = 0.0;
        double maxv = 0.0;
        double sum = 0.0;
        int count = 0;
    };

    void FlushWindow();  // 現ウィンドウを書き出してリセット

    static constexpr double kWindowMs = 1000.0;  // 集計ウィンドウ長（1秒）

    double tickToMs_ = 0.0;          // QPC tick → ms 変換係数（起動時に1回取得）
    uint64_t frame_ = 0;

    // フレーム内集計
    std::vector<SectionStat> stats_;
    std::unordered_map<std::string, size_t> indexByName_;
    std::vector<ActiveScope> activeStack_;

    // ウィンドウ集計
    std::vector<WindowStat> windowStats_;
    std::unordered_map<std::string, size_t> windowIndexByName_;
    int windowFrames_ = 0;
    int64_t windowStartTicks_ = 0;

    // カウンタ（フレーム内 / ウィンドウ）
    std::vector<FrameCounter> counters_;
    std::unordered_map<std::string, size_t> counterIndexByName_;
    std::vector<WindowCounter> windowCounters_;
    std::unordered_map<std::string, size_t> windowCounterIndexByName_;

    // ゲージ（フレーム内 / ウィンドウ）
    std::vector<FrameGauge> gauges_;
    std::unordered_map<std::string, size_t> gaugeIndexByName_;
    std::vector<WindowGauge> windowGauges_;
    std::unordered_map<std::string, size_t> windowGaugeIndexByName_;

    // ライブ表示スナップショット（FlushWindow ごとに更新）
    std::vector<LiveSection> liveSections_;
    std::vector<LiveCounter> liveCounters_;
    std::vector<LiveGauge>   liveGauges_;
    double liveWindowSec_ = 0.0;
    int    liveWindowFrames_ = 0;

    // フレーム時間リング（EndFrame ごとに更新）
    float   frameMsRing_[kFrameRingSize] = {};
    int     frameRingHead_ = 0;
    int64_t lastEndFrameTicks_ = 0;
    double  lastFrameMs_ = 0.0;
};

/// <summary>
/// 区間計測を RAII で行う一時オブジェクト。PEPPER_SCOPE マクロが内部で生成する。
/// ブロックを抜けた瞬間に区間時間が確定する。
/// </summary>
class ProfileScope {
public:
    ProfileScope(const char* name, const char* file, int line) {
        Profiler::Instance().BeginSection(name, file, line);
    }
    ~ProfileScope() {
        Profiler::Instance().EndSection();
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
};
