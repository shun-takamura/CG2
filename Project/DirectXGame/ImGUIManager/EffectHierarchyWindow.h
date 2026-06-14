#pragma once
#include "IImGuiWindow.h"

class EffectEditorWindow;
class IEditorSelection;

/// <summary>
/// EffectEditor で編集中エフェクトのコンポーネントを種類別に表示するヒエラルキー風ウィンドウ。
/// Scene の HierarchyWindow とは別系統。
/// </summary>
class EffectHierarchyWindow : public IImGuiWindow {
public:
    EffectHierarchyWindow(IEditorSelection* selection, EffectEditorWindow* editor);
    ~EffectHierarchyWindow() override = default;

protected:
    void OnDraw() override;

private:
    IEditorSelection*   selection_ = nullptr;
    EffectEditorWindow* editor_ = nullptr;
};
