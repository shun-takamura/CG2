#pragma once

class IImGuiEditable;

/// <summary>
/// エディタの「選択中オブジェクト」を抽象化するインターフェース（依存性の逆転）。
/// ImGuiManager（Game 側）が実装し、EngineCore 側のエディタ（EffectEditorWindow 等）に
/// 注入する。これによりエンジン側のエディタが ImGuiManager の具象型を知らずに済む。
/// </summary>
class IEditorSelection {
public:
    virtual ~IEditorSelection() = default;

    /// <summary>現在選択中の編集可能オブジェクト（無ければ nullptr）。</summary>
    virtual IImGuiEditable* GetSelected() const = 0;

    /// <summary>選択中オブジェクトを設定する（nullptr で選択解除）。</summary>
    virtual void SetSelected(IImGuiEditable* editable) = 0;
};
