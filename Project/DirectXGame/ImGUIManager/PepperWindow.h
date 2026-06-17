#pragma once
#include "IImGuiWindow.h"

#ifdef USE_PEPPER
#include "Profiler.h"
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
    PepperWindow() : IImGuiWindow("PEPPER Profiler") {
        SetInitialSize(ImVec2(540, 640));
    }

protected:
    void OnDraw() override {
#ifdef _DEBUG
#ifdef USE_PEPPER
        Profiler& p = Profiler::Instance();

        // フレーム予算は「快適に遊べる下限 = 60fps(16.67ms)」で固定。モニタの
        // リフレッシュレートに依存させると、低スペックPC+高Hzモニタで常時フレーム落ち
        // 扱いになるため、プレイ体験として許容できるかを見る固定基準にする。
        const double budgetMs = 1000.0 / kComfortFps_;
        const double warnMs   = 1000.0 / kWarnFps_;

        // ===== フレーム時間 / FPS =====
        const double frameMs = p.GetLastFrameMs();
        const double fps = (frameMs > 0.0) ? (1000.0 / frameMs) : 0.0;
        ImGui::Text("Frame: %.3f ms  (%.1f fps)", frameMs, fps);
        ImGui::SameLine();
        ImGui::TextDisabled("| target %.0ffps(%.1fms) | window %.2fs / %d frames",
            kComfortFps_, budgetMs, p.GetLiveWindowSec(), p.GetLiveWindowFrames());

        // フレーム時間の折れ線グラフ（直近 N フレーム）。Y上限は快適ラインの2倍。
        {
            char overlay[32];
            std::snprintf(overlay, sizeof(overlay), "%.2f ms", frameMs);
            ImGui::PlotLines("##FrameMs", p.GetFrameMsRing(), p.GetFrameMsRingSize(),
                p.GetFrameMsRingHead(), overlay, 0.0f, static_cast<float>(budgetMs * 2.0),
                ImVec2(-1, 60));
        }

        // 低fps判定。60Hzモニタが VSync で 16.6ms に張り付いても点滅しないよう、
        // 警告は 50fps(20ms) 超でのみ。行は常に確保し色と文言だけ変える
        // （下の UI が上下にガクつくのを防ぐ）。
        if (frameMs > warnMs) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "LOW FPS (%.2f ms / %.1f fps < %.0ffps)", frameMs, fps, kWarnFps_);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f),
                "OK (%.2f ms / %.1f fps)", frameMs, fps);
        }

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
            if (ImGui::BeginTable("pepper_sections", 5, flags, ImVec2(0, 230))) {
                ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 64);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 64);
                ImGui::TableSetupColumn("max ms", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("calls", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& s : p.GetLiveSections()) {
                    ImGui::TableNextRow();

                    // Section（depth ぶん半角スペースでインデント）
                    ImGui::TableSetColumnIndex(0);
                    char nameBuf[256];
                    std::snprintf(nameBuf, sizeof(nameBuf), "%*s%s",
                        s.depth * 2, "", s.name.c_str());
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
            if (ImGui::BeginTable("pepper_counters", 3,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("avg/frame", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("max/frame", ImGuiTableColumnFlags_WidthFixed, 80);
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
            if (ImGui::BeginTable("pepper_gauges", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Gauge", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("cur", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 80);
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
#ifdef USE_PEPPER
    // 快適に遊べる目安の上限fps（緑の基準線）と、これを割ったら警告にする下限fps。
    // 60Hzモニタが VSync で張り付く 16.6ms で点滅しないよう、警告は 50fps(20ms) に置く。
    static constexpr double kComfortFps_ = 60.0;
    static constexpr double kWarnFps_    = 50.0;

    // CPU ms を負荷に応じて色分け表示する。
    static void DrawColoredMs(double ms) {
        ImVec4 col(0.7f, 1.0f, 0.7f, 1.0f);      // 軽い: 緑
        if (ms > 8.0)  col = ImVec4(1.0f, 0.9f, 0.4f, 1.0f);  // 中: 黄
        if (ms > 16.6) col = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // 重い: 赤
        ImGui::TextColored(col, "%.3f", ms);
    }
#endif // USE_PEPPER
};
