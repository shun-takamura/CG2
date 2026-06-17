#include "MemoryProbe.h"

#include <Windows.h>
#include <dxgi1_6.h>
#include <psapi.h>

#include "Profiler.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxgi.lib")

MemoryProbe& MemoryProbe::Instance() {
    static MemoryProbe instance;
    return instance;
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
    if (lastQueryMs_ == 0 || now - lastQueryMs_ >= 1000) {
        lastQueryMs_ = now;

        // RAM（WorkingSet / Private）
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            ramWsMb_ = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            ramPrivMb_ = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        }

        // VRAM（ローカルメモリの現在使用量）
        if (EnsureAdapter()) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                vramMb_ = static_cast<double>(info.CurrentUsage) / (1024.0 * 1024.0);
            }
        }
    }

    // ゲージは毎フレーム更新（各ウィンドウに必ず値が入るように）
    Profiler::Instance().SetGauge("RAM_WS_MB", ramWsMb_);
    Profiler::Instance().SetGauge("RAM_Priv_MB", ramPrivMb_);
    Profiler::Instance().SetGauge("VRAM_MB", vramMb_);
}
