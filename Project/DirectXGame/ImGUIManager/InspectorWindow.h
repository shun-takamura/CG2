#pragma once
#include "IImGuiWindow.h"
#include "IImGuiEditable.h"

// 前方宣言
class ImGuiManager;

/// <summary>
/// 選択オブジェクト編集ウィンドウ
/// </summary>
class InspectorWindow : public IImGuiWindow {
public:
    /// <summary>"Inspector" ウィンドウとして作る</summary>
    /// <param name="manager">選択状態とグループの名前を持つ ImGuiManager</param>
    InspectorWindow(ImGuiManager* manager) 
        : IImGuiWindow("Inspector"), manager_(manager) {}

protected:
    /// <summary>選択中のオブジェクトの名前・グループ・表示・コライダーと、オブジェクト自身の編集項目・ホストが登録したセクションを出す</summary>
    void OnDraw() override;

private:
    ImGuiManager* manager_;
};
