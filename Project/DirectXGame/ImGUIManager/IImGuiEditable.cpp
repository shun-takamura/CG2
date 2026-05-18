#include "IImGuiEditable.h"
#include "ImGuiManager.h"
#include "Components/CollisionManager.h"

namespace {
    // 0 を予約とし、1..255 を循環で配布する（uint8 が一周しても挙動はする）
    uint8_t NextObjectId() {
        static uint8_t s_counter = 0; // 1 から始めるため pre-increment
        ++s_counter;
        if (s_counter == 0) s_counter = 1; // 0 はスキップ
        return s_counter;
    }
}

IImGuiEditable::IImGuiEditable() {
    objectId_ = NextObjectId();
    // ImGuiManagerに自動登録（Debug ビルドのみ実体的に効く）
    ImGuiManager::Instance().Register(this);
    // CollisionManager にも登録（Release でも動く必要があるため別系統）
    CollisionManager::GetInstance()->Register(this);
}

IImGuiEditable::IImGuiEditable(NoAutoRegister) {
    objectId_ = NextObjectId();
    // 何もしない（Hierarchy/Collision どちらにも登録しない）
    // EffectEditor 等、独立した Hierarchy 配下で管理する派生用
}

IImGuiEditable::~IImGuiEditable() {
    CollisionManager::GetInstance()->Unregister(this);
    ImGuiManager::Instance().Unregister(this);
}
