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
    /// <summary>選択の窓口と、編集中のエフェクトを持つエディタを受け取る</summary>
    /// <param name="selection">選択の窓口</param>
    /// <param name="editor">エフェクトエディタ（nullptr なら中身を出さない）</param>
    EffectHierarchyWindow(IEditorSelection* selection, EffectEditorWindow* editor);
    ~EffectHierarchyWindow() override = default;

protected:
    /// <summary>
    /// 編集中エフェクトのコンポーネントを種類ごとの折りたたみに並べる。
    /// 行を選ぶと選択し、"+" で複製、"x" で削除する
    /// </summary>
    void OnDraw() override;

private:
    IEditorSelection*   selection_ = nullptr;
    EffectEditorWindow* editor_ = nullptr;
};
