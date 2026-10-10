#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <wrl.h>

// D3D12 型は前方宣言にとどめ、d3d12.h は .cpp 側にだけ含める（ヘッダを軽く保つ）
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12QueryHeap;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;

/// <summary>
/// GPU 区間時間を D3D12 タイムスタンプクエリで計測する（シングルトン）。
/// 区間の前後で EndQuery を積み、フレーム末に ResolveQueryData → readback で
/// GPU 実時間を読み、Profiler へ gpu_ms として渡す。
/// このエンジンは EndDraw で毎フレーム GPU 完了を待つ完全同期型なので、
/// その待ちに相乗りすればリードバック遅延は不要（追加の GPU ストールなし）。
/// </summary>
class GpuProfiler {
public:
    static GpuProfiler& Instance();

    /// <summary>クエリヒープと readback バッファを作る。DirectXCore::Initialize の末尾で1回。</summary>
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue);

    /// <summary>GPU 区間の開始（begin タイムスタンプを積む）。</summary>
    void BeginScope(ID3D12GraphicsCommandList* commandList, const char* name);

    /// <summary>直近に開始した GPU 区間を閉じる（end タイムスタンプを積む）。</summary>
    void EndScope(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// パイプライン統計の区間を開始する（PS の実行回数＝描いた画素数×重なり、などを数える）。
    /// 統計の区間は入れ子にしない（中で別の統計区間を開いても無視する）。
    /// 結果は Profiler のカウンタ「名前.PSInv」「名前.Prims」になる。
    /// </summary>
    void BeginStatsScope(ID3D12GraphicsCommandList* commandList, const char* name);

    /// <summary>BeginStatsScope で開いた区間を閉じる。</summary>
    void EndStatsScope(ID3D12GraphicsCommandList* commandList);

    /// <summary>積んだタイムスタンプを readback バッファへ解決する。コマンドリストを閉じる直前に。</summary>
    void ResolveTimestamps(ID3D12GraphicsCommandList* commandList);

    /// <summary>GPU 完了待ちの後に readback を読んで Profiler へ反映し、次フレーム用にリセットする。</summary>
    void ReadbackAndReport();

private:
    GpuProfiler() = default;
    ~GpuProfiler();
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    static constexpr uint32_t kMaxSlots = 512;     // タイムスタンプ枠（1区間=2枠）
    static constexpr uint32_t kMaxStatsSlots = 32; // パイプライン統計の枠（1区間=1枠）
    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFFu;

    // 計測中の区間（GPU はネスト可。LIFO で閉じる）
    struct Pending {
        std::string name;
        uint32_t beginSlot = kInvalidSlot;
    };
    // 閉じ終えた区間（readback でこの begin/end のtick差から ms を出す）
    struct Done {
        std::string name;
        uint32_t beginSlot = 0;
        uint32_t endSlot = 0;
    };

    bool initialized_ = false;
    double gpuTickToMs_ = 0.0;       // GPU tick → ms 変換係数
    uint32_t slotCount_ = 0;         // このフレームで使った枠数
    std::vector<Pending> stack_;
    std::vector<Done> done_;

    // パイプライン統計（PS の実行回数など）
    struct StatsDone {
        std::string name;
        uint32_t slot = 0;
    };
    bool statsInitialized_ = false;
    bool statsActive_ = false;       // 統計の区間を開いている間 true（入れ子を防ぐ）
    bool statsSkipped_ = false;      // 入れ子・枠不足で開かなかった区間（End で釣り合いだけ取る）
    uint32_t statsSlotCount_ = 0;
    std::vector<StatsDone> statsDone_;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> statsHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> statsReadback_;
};

/// <summary>GPU 区間計測を RAII で行う。PEPPER_GPU_SCOPE マクロが内部で生成する。</summary>
class GpuProfileScope {
public:
    GpuProfileScope(ID3D12GraphicsCommandList* commandList, const char* name)
        : commandList_(commandList) {
        GpuProfiler::Instance().BeginScope(commandList_, name);
    }
    ~GpuProfileScope() {
        GpuProfiler::Instance().EndScope(commandList_);
    }
    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    ID3D12GraphicsCommandList* commandList_;
};

/// <summary>GPU 区間の時間＋パイプライン統計を RAII で取る。PEPPER_GPU_STATS_SCOPE マクロが生成する。</summary>
class GpuStatsScope {
public:
    GpuStatsScope(ID3D12GraphicsCommandList* commandList, const char* name)
        : commandList_(commandList) {
        GpuProfiler::Instance().BeginScope(commandList_, name);
        GpuProfiler::Instance().BeginStatsScope(commandList_, name);
    }
    ~GpuStatsScope() {
        GpuProfiler::Instance().EndStatsScope(commandList_);
        GpuProfiler::Instance().EndScope(commandList_);
    }
    GpuStatsScope(const GpuStatsScope&) = delete;
    GpuStatsScope& operator=(const GpuStatsScope&) = delete;

private:
    ID3D12GraphicsCommandList* commandList_;
};
