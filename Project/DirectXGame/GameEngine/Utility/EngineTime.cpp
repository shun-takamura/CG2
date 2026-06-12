#include "EngineTime.h"
#include "ITimeScaleProvider.h"

namespace {
    // 現在の供給元（所有はしない。寿命はゲーム側が管理）
    ITimeScaleProvider* g_provider = nullptr;
}

namespace EngineTime {

    void SetProvider(ITimeScaleProvider* provider) {
        g_provider = provider;
    }

    float ScaledDeltaTime(TimeGroup g, float fallback) {
        return g_provider ? g_provider->GetScaledDeltaTime(g) : fallback;
    }

} // namespace EngineTime
