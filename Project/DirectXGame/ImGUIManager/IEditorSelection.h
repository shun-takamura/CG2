#pragma once

class IImGuiEditable;

/// <summary>
/// エディタの「選択中オブジェクト」を抽象化するインターフェース（依存性の逆転）。
/// ImGuiManager が実装し、各エディタウィンドウ（EffectEditorWindow 等）へ注入する。
/// これにより個々のエディタが ImGuiManager の具象型を知らずに済む。
/// </summary>
class IEditorSelection {
public:
    virtual ~IEditorSelection() = default;

    /// <summary>現在選択中の編集可能オブジェクト（無ければ nullptr）。</summary>
    virtual IImGuiEditable* GetSelected() const = 0;

    /// <summary>選択中オブジェクトを設定する（nullptr で選択解除）。</summary>
    virtual void SetSelected(IImGuiEditable* editable) = 0;
};
