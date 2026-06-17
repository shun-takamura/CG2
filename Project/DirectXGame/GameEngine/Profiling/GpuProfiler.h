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

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
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
