#include "IImGuiEditable.h"
#include "ImGuiManager.h"

IImGuiEditable::IImGuiEditable() {
    // ImGuiManagerに自動登録
    ImGuiManager::Instance().Register(this);
}

IImGuiEditable::~IImGuiEditable() {
    // ImGuiManagerから自動登録解除
    ImGuiManager::Instance().Unregister(this);
}
