#pragma once
#include "IImGuiWindow.h"

class EffectEditorWindow;
class ImGuiManager;

/// <summary>
/// EffectEditor で編集中エフェクトのコンポーネントを種類別に表示するヒエラルキー風ウィンドウ。
/// Scene の HierarchyWindow とは別系統。
/// </summary>
class EffectHierarchyWindow : public IImGuiWindow {
public:
    EffectHierarchyWindow(ImGuiManager* mgr, EffectEditorWindow* editor);
    ~EffectHierarchyWindow() override = default;

protected:
    void OnDraw() override;

private:
    ImGuiManager*       mgr_ = nullptr;
    EffectEditorWindow* editor_ = nullptr;
};
