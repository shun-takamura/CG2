#pragma once
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"
#include <vector>

// 前方宣言
class ImGuiManager;

/// <summary>
/// オブジェクト一覧表示ウィンドウ
/// </summary>
class HierarchyWindow : public IImGuiWindow {
public:
    /// <summary>"Hierarchy" ウィンドウとして作る</summary>
    /// <param name="manager">登録された編集対象と選択状態を持つ ImGuiManager</param>
    HierarchyWindow(ImGuiManager* manager) 
        : IImGuiWindow("Hierarchy"), manager_(manager) {}

protected:
    /// <summary>
    /// 登録された編集対象をグループごとに一覧表示する（名前・型名で検索できる）。
    /// 行を選ぶと選択し、表示の ON/OFF を切り替え、動的オブジェクトは "x" で消せる
    /// </summary>
    void OnDraw() override;

private:
    ImGuiManager* manager_;
};
