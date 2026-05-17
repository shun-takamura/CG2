#include "IImGuiEditable.h"
#include "ImGuiManager.h"
#include "Components/CollisionManager.h"

IImGuiEditable::IImGuiEditable() {
    // ImGuiManagerに自動登録（Debug ビルドのみ実体的に効く）
    ImGuiManager::Instance().Register(this);
    // CollisionManager にも登録（Release でも動く必要があるため別系統）
    CollisionManager::GetInstance()->Register(this);
}

IImGuiEditable::~IImGuiEditable() {
    CollisionManager::GetInstance()->Unregister(this);
    ImGuiManager::Instance().Unregister(this);
}
