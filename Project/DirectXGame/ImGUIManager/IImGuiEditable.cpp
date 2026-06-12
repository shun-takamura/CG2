#include "IImGuiEditable.h"

namespace {
    // 0 を予約とし、1..255 を循環で配布する（uint8 が一周しても挙動はする）
    uint8_t NextObjectId() {
        static uint8_t s_counter = 0; // 1 から始めるため pre-increment
        ++s_counter;
        if (s_counter == 0) s_counter = 1; // 0 はスキップ
        return s_counter;
    }
}

IImGuiEditable::LifecycleHook IImGuiEditable::s_onConstructed;
IImGuiEditable::LifecycleHook IImGuiEditable::s_onDestroyed;

void IImGuiEditable::SetHooks(LifecycleHook onConstructed, LifecycleHook onDestroyed) {
    s_onConstructed = std::move(onConstructed);
    s_onDestroyed   = std::move(onDestroyed);
}

IImGuiEditable::IImGuiEditable() {
    objectId_ = NextObjectId();
    // 生成フック（ゲーム側で CollisionManager 登録・GameplayComponents 確保・ImGuiManager 登録など）
    if (s_onConstructed) s_onConstructed(this);
}

IImGuiEditable::IImGuiEditable(NoAutoRegister) {
    objectId_ = NextObjectId();
    // 何もしない（Hierarchy/Collision どちらにも登録しない）
    // EffectEditor 等、独立した Hierarchy 配下で管理する派生用
}

IImGuiEditable::~IImGuiEditable() {
    // 破棄フック（ゲーム側で登録解除・GameplayComponents 破棄など）
    if (s_onDestroyed) s_onDestroyed(this);
}
