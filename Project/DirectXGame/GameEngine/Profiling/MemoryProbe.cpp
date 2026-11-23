#include "MemoryProbe.h"

#include <Windows.h>
#include <dxgi1_6.h>
#include <psapi.h>

#include "Profiler.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace {
    // メモリの使用量を問い合わせる間隔 [ms]
    constexpr unsigned long long kQueryIntervalMs = 1000;
    // 1 MB のバイト数
    constexpr double kBytesPerMB = 1024.0 * 1024.0;
}

MemoryProbe* MemoryProbe::GetInstance() {
    static MemoryProbe instance;
    return &instance;
}

MemoryProbe::~MemoryProbe() = default;

bool MemoryProbe::EnsureAdapter() {
    if (adapter_) {
        return true;
    }
    if (triedAdapter_) {
        return false;  // 一度失敗したら毎フレーム試さない
    }
    triedAdapter_ = true;

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    // KPI と同じく HIGH_PERFORMANCE アダプタの VRAM を見る
    if (FAILED(factory->EnumAdapterByGpuPreference(
            0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)))) {
        return false;
    }
    return true;
}

void MemoryProbe::Sample() {
    const unsigned long long now = GetTickCount64();
    if (lastQueryMs_ == 0 || now - lastQueryMs_ >= kQueryIntervalMs) {
        lastQueryMs_ = now;

        // RAM（WorkingSet / Private）
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            ramWsMb_ = static_cast<double>(pmc.WorkingSetSize) / kBytesPerMB;
            ramPrivMb_ = static_cast<double>(pmc.PrivateUsage) / kBytesPerMB;
        }

        // VRAM（ローカルメモリの現在使用量）
        if (EnsureAdapter()) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                vramMb_ = static_cast<double>(info.CurrentUsage) / kBytesPerMB;
            }
        }
    }

    // ゲージは毎フレーム更新（各ウィンドウに必ず値が入るように）
    Profiler::GetInstance()->SetGauge("RAM_WS_MB", ramWsMb_);
    Profiler::GetInstance()->SetGauge("RAM_Priv_MB", ramPrivMb_);
    Profiler::GetInstance()->SetGauge("VRAM_MB", vramMb_);
}
