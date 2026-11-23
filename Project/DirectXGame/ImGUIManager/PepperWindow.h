#pragma once
#include "IImGuiWindow.h"

#ifdef USE_PEPPER
#include "Profiler.h"
#include "LoadProfiler.h"
#include <cstdio>
#endif

/// <summary>
/// P.E.P.P.E.R. リアルタイム表示ウィンドウ（Unity プロファイラ風）。
/// フレーム時間グラフ・区間テーブル(CPU/GPU)・カウンタ・メモリ/リソースゲージを表示する。
/// 数値テーブルは直近1秒ウィンドウの集計（avg/max）＝毎秒更新で安定。
/// グラフは毎フレームのフレーム時間。
/// 二重ガード: ImGui 本体は _DEBUG、計測データ読み出しは USE_PEPPER。
/// </summary>
class PepperWindow : public IImGuiWindow {
public:
    /// <summary>"PEPPER Profiler" ウィンドウとして作る（初めの大きさ 540×640）</summary>
    PepperWindow() : IImGuiWindow("PEPPER Profiler") {
        SetInitialSize(ImVec2(kInitialWidth, kInitialHeight));
    }

protected:
    /// <summary>
    /// フレーム時間と FPS・折れ線グラフ・低 FPS の警告・ロード計測と、直近 1 秒ウィンドウの区間・カウンタ・ゲージの表を出す
    /// （_DEBUG かつ USE_PEPPER の時だけ。USE_PEPPER が無ければその旨だけ出す）
    /// </summary>
    void OnDraw() override {
#ifdef _DEBUG
#ifdef USE_PEPPER
        Profiler& p = *Profiler::GetInstance();

        // フレーム予算は「快適に遊べる下限 = 60fps(16.67ms)」で固定。モニタの
        // リフレッシュレートに依存させると、低スペックPC+高Hzモニタで常時フレーム落ち
        // 扱いになるため、プレイ体験として許容できるかを見る固定基準にする。
        const double budgetMs = kMsPerSecond / kComfortFps_;
        const double warnMs   = kMsPerSecond / kWarnFps_;

        // ===== フレーム時間 / FPS =====
        const double frameMs = p.GetLastFrameMs();
        const double fps = (frameMs > 0.0) ? (kMsPerSecond / frameMs) : 0.0;
        ImGui::Text("Frame: %.3f ms  (%.1f fps)", frameMs, fps);
        ImGui::SameLine();
        ImGui::TextDisabled("| target %.0ffps(%.1fms) | window %.2fs / %d frames",
            kComfortFps_, budgetMs, p.GetLiveWindowSec(), p.GetLiveWindowFrames());

        // フレーム時間の折れ線グラフ（直近 N フレーム）。Y上限は快適ラインの2倍。
        {
            char overlay[kOverlayBufferSize];
            std::snprintf(overlay, sizeof(overlay), "%.2f ms", frameMs);
            ImGui::PlotLines("##FrameMs", p.GetFrameMsRing(), p.GetFrameMsRingSize(),
                p.GetFrameMsRingHead(), overlay, 0.0f, static_cast<float>(budgetMs * 2.0),
                ImVec2(-1, kFrameGraphHeight));
        }

        // 低fps判定。60Hzモニタが VSync で 16.6ms に張り付いても点滅しないよう、
        // 警告は 50fps(20ms) 超でのみ。行は常に確保し色と文言だけ変える
        // （下の UI が上下にガクつくのを防ぐ）。
        if (frameMs > warnMs) {
            ImGui::TextColored(kLowFpsTextColor,
                "LOW FPS (%.2f ms / %.1f fps < %.0ffps)", frameMs, fps, kWarnFps_);
        } else {
            ImGui::TextColored(kOkTextColor,
                "OK (%.2f ms / %.1f fps)", frameMs, fps);
        }

        // ロード計測（フレーム集計とは別系統。起動直後から表示できるよう Collecting の前に置く）
        DrawLoadSection();

        if (p.GetLiveWindowFrames() == 0) {
            ImGui::Separator();
            ImGui::TextDisabled("Collecting... (first 1s window not flushed yet)");
            return;
        }

        ImGui::Separator();

        // ===== 区間テーブル（直近1秒ウィンドウの平均/最大）=====
        if (ImGui::CollapsingHeader("Sections (avg over last window)",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            const ImGuiTableFlags flags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("pepper_sections", kSectionColumnCount, flags, ImVec2(0, kSectionTableHeight))) {
                ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, kMsColumnWidth);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, kMsColumnWidth);
                ImGui::TableSetupColumn("max ms", ImGuiTableColumnFlags_WidthFixed, kMaxMsColumnWidth);
                ImGui::TableSetupColumn("calls", ImGuiTableColumnFlags_WidthFixed, kCallsColumnWidth);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& s : p.GetLiveSections()) {
                    ImGui::TableNextRow();

                    // Section（depth ぶん半角スペースでインデント）
                    ImGui::TableSetColumnIndex(0);
                    char nameBuf[kSectionNameBufferSize];
                    std::snprintf(nameBuf, sizeof(nameBuf), "%*s%s",
                        s.depth * kIndentSpacesPerDepth, "", s.name.c_str());
                    ImGui::TextUnformatted(nameBuf);

                    ImGui::TableSetColumnIndex(1);
                    DrawColoredMs(s.cpuAvgMs);

                    ImGui::TableSetColumnIndex(2);
                    if (s.gpuAvgMs > 0.0) {
                        ImGui::Text("%.3f", s.gpuAvgMs);
                    } else {
                        ImGui::TextDisabled("-");
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", s.cpuMaxMs);

                    ImGui::TableSetColumnIndex(4);
                    const double callsPerFrame =
                        (s.presentFrames > 0)
                        ? (static_cast<double>(s.totalCalls) / s.presentFrames)
                        : 0.0;
                    ImGui::Text("%.0f", callsPerFrame);
                }
                ImGui::EndTable();
            }
        }

        // ===== カウンタ（DrawCall 回数など）=====
        if (!p.GetLiveCounters().empty() &&
            ImGui::CollapsingHeader("Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("pepper_counters", kCounterColumnCount,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("avg/frame", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
                ImGui::TableSetupColumn("max/frame", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
                ImGui::TableHeadersRow();
                for (const auto& c : p.GetLiveCounters()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(c.name.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", c.avgPerFrame);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%lld", static_cast<long long>(c.maxPerFrame));
                }
                ImGui::EndTable();
            }
        }

        // ===== ゲージ（メモリ/VRAM/オブジェクト数など現在値）=====
        if (!p.GetLiveGauges().empty() &&
            ImGui::CollapsingHeader("Memory / Resources (gauges)",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("pepper_gauges", kGaugeColumnCount,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Gauge", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("cur", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
                ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
                ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
                ImGui::TableHeadersRow();
                for (const auto& g : p.GetLiveGauges()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(g.name.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", g.cur);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", g.minv);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", g.maxv);
                }
                ImGui::EndTable();
            }
        }
#else
        ImGui::TextDisabled("USE_PEPPER is not defined in this build.");
#endif // USE_PEPPER
#endif // _DEBUG
    }

private:
    // ウィンドウの初めの大きさ [px]
    static constexpr int kInitialWidth = 540;
    static constexpr int kInitialHeight = 640;

#ifdef USE_PEPPER
    // 快適に遊べる目安の上限fps（緑の基準線）と、これを割ったら警告にする下限fps。
    // 60Hzモニタが VSync で張り付く 16.6ms で点滅しないよう、警告は 50fps(20ms) に置く。
    static constexpr double kComfortFps_ = 60.0;
    static constexpr double kWarnFps_    = 50.0;

    // 1 秒のミリ秒数
    static constexpr double kMsPerSecond = 1000.0;
    // フレーム時間のグラフの文字列のバッファの大きさ [byte] とグラフの高さ [px]
    static constexpr int kOverlayBufferSize = 32;
    static constexpr int kFrameGraphHeight = 60;
    // 低 FPS の警告の文字の色（赤）と、問題ないときの文字の色（緑）
    static constexpr ImVec4 kLowFpsTextColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    static constexpr ImVec4 kOkTextColor = ImVec4(0.5f, 0.7f, 0.5f, 1.0f);
    // 区間の表の列の数と高さ [px]
    static constexpr int kSectionColumnCount = 5;
    static constexpr int kSectionTableHeight = 230;
    // 区間の名前のバッファの大きさ [byte] と、深さ 1 段あたりの字下げの空白の数
    static constexpr int kSectionNameBufferSize = 256;
    static constexpr int kIndentSpacesPerDepth = 2;
    // カウンタ / ゲージ / ロードのフェーズ / テクスチャ計測の表の列の数
    static constexpr int kCounterColumnCount = 3;
    static constexpr int kGaugeColumnCount = 4;
    static constexpr int kLoadPhaseColumnCount = 4;
    static constexpr int kLoadBenchColumnCount = 6;
    // 表の列の幅 [px]：CPU/GPU ms / max ms / calls / 数値 / テクスチャ数 / テクスチャ計測の数値 / MB/s
    static constexpr int kMsColumnWidth = 64;
    static constexpr int kMaxMsColumnWidth = 60;
    static constexpr int kCallsColumnWidth = 50;
    static constexpr int kValueColumnWidth = 80;
    static constexpr int kBenchCountColumnWidth = 40;
    static constexpr int kBenchValueColumnWidth = 70;
    static constexpr int kBenchThroughputColumnWidth = 60;
    // サイクル数を M（百万）単位にする割る数と、1 MB のバイト数
    static constexpr double kCyclesPerMegaCycle = 1.0e6;
    static constexpr double kBytesPerMB = 1024.0 * 1024.0;
    // CPU 時間の色分けの境目 [ms]（これ以下は軽い / これ以下は中くらい / それより上は重い）
    static constexpr double kLightMsThreshold = 8.0;
    static constexpr double kMediumMsThreshold = 16.6;
    // CPU 時間の色（軽い: 緑 / 中: 黄 / 重い: 赤）
    static constexpr ImVec4 kLightMsColor = ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
    static constexpr ImVec4 kMediumMsColor = ImVec4(1.0f, 0.9f, 0.4f, 1.0f);
    static constexpr ImVec4 kHeavyMsColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

    /// <summary>
    /// 起動フェーズ / シーンロード / テクスチャ計測の結果（LoadProfiler）を折りたたみの表で出す（記録が無ければ何も出さない）。
    /// Debug の値は参考値（正式な比較は Development/Release で tools/Python/run_load_bench.py）
    /// </summary>
    static void DrawLoadSection() {
        const LoadProfiler* lp = LoadProfiler::GetInstance();
        const auto& phases = lp->GetPhases();
        const auto& bench = lp->GetBenchResults();
        if (phases.empty() && bench.empty()) return;
        if (!ImGui::CollapsingHeader("Load (startup / scene / texture bench)")) return;
        ImGui::TextDisabled("build: %s  (Debug values are for reference only)", LoadProfiler::BuildConfigName());

        if (!phases.empty() && ImGui::BeginTable("pepper_load_phases", kLoadPhaseColumnCount,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("wall ms", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
            ImGui::TableSetupColumn("cpu ms", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
            ImGui::TableSetupColumn("cycles(M)", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
            ImGui::TableHeadersRow();
            for (const auto& ph : phases) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(ph.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", ph.wallMs);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", ph.cpuMs);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", static_cast<double>(ph.cpuCycles) / kCyclesPerMegaCycle);
            }
            ImGui::EndTable();
        }

        if (!bench.empty() && ImGui::BeginTable("pepper_load_bench", kLoadBenchColumnCount,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Texture bench", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("tex", ImGuiTableColumnFlags_WidthFixed, kBenchCountColumnWidth);
            ImGui::TableSetupColumn("disk MB", ImGuiTableColumnFlags_WidthFixed, kBenchValueColumnWidth);
            ImGui::TableSetupColumn("wall ms", ImGuiTableColumnFlags_WidthFixed, kBenchValueColumnWidth);
            ImGui::TableSetupColumn("cycles(M)", ImGuiTableColumnFlags_WidthFixed, kValueColumnWidth);
            ImGui::TableSetupColumn("MB/s", ImGuiTableColumnFlags_WidthFixed, kBenchThroughputColumnWidth);
            ImGui::TableHeadersRow();
            for (const auto& r : bench) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.mode.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%d", r.textureCount);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", static_cast<double>(r.diskBytes) / kBytesPerMB);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", r.wallMs);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f", static_cast<double>(r.cpuCycles) / kCyclesPerMegaCycle);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%.0f", r.ThroughputMBps());
            }
            ImGui::EndTable();
        } else if (bench.empty()) {
            ImGui::TextDisabled("texture bench: not run (launch with --bench-textures)");
        }
    }

    /// <summary>CPU 時間を負荷に応じて色分けして出す（8ms 以下は緑・16.6ms 以下は黄・それより上は赤）</summary>
    /// <param name="ms">CPU 時間 [ms]</param>
    static void DrawColoredMs(double ms) {
        ImVec4 col = kLightMsColor;      // 軽い: 緑
        if (ms > kLightMsThreshold)  col = kMediumMsColor;  // 中: 黄
        if (ms > kMediumMsThreshold) col = kHeavyMsColor;  // 重い: 赤
        ImGui::TextColored(col, "%.3f", ms);
    }
#endif // USE_PEPPER
};
