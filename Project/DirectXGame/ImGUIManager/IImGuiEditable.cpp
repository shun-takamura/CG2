#include "IImGuiEditable.h"
#include "ImGuiManager.h"
#include "Components/CollisionManager.h"

IImGuiEditable::IImGuiEditable() {
    // ImGuiManagerに自動登録（Debug ビルドのみ実体的に効く）
    ImGuiManager::Instance().Register(this);
    // CollisionManager にも登録（Release でも動く必要があるため別系統）
    CollisionManager::GetInstance()->Register(this);
}

IImGuiEditable::IImGuiEditable(NoAutoRegister) {
    // 何もしない（Hierarchy/Collision どちらにも登録しない）
    // EffectEditor 等、独立した Hierarchy 配下で管理する派生用
}

IImGuiEditable::~IImGuiEditable() {
    CollisionManager::GetInstance()->Unregister(this);
    ImGuiManager::Instance().Unregister(this);
}
