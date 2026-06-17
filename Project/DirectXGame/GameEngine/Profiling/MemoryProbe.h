#pragma once

#include <wrl.h>

struct IDXGIAdapter3;

/// <summary>
/// プロセスのRAM（WorkingSet/Private）とVRAM使用量を継続サンプリングし、
/// Profiler のゲージへ流すプローブ（シングルトン）。
/// OS への問い合わせは重いので1秒に1回だけ行い、ゲージ更新は毎フレーム（キャッシュ値）。
/// </summary>
class MemoryProbe {
public:
    static MemoryProbe& Instance();

    /// <summary>毎フレーム呼ぶ。1秒経過時のみOS問い合わせ、毎フレーム Profiler::SetGauge する。</summary>
    void Sample();

private:
    MemoryProbe() = default;
    ~MemoryProbe();
    MemoryProbe(const MemoryProbe&) = delete;
    MemoryProbe& operator=(const MemoryProbe&) = delete;

    bool EnsureAdapter();  // VRAM 取得用アダプタの遅延生成

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    bool triedAdapter_ = false;
    unsigned long long lastQueryMs_ = 0;  // 最後にOS問い合わせした時刻（GetTickCount64）
    double ramWsMb_ = 0.0;
    double ramPrivMb_ = 0.0;
    double vramMb_ = 0.0;
};
