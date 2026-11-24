#pragma once
#include "IImGuiEditable.h"
#include <string>

class EffectEditorWindow;

/// <summary>
/// EffectDef のコンポーネント（Primitive/Particle/Light/Sound）を Inspector で編集するためのラッパー。
/// editBuffer 内の要素を kind + index で参照する。
/// 自動登録（Scene Hierarchy / Collision）はしない。
/// </summary>
class EffectComponentEditable : public IImGuiEditable {
public:
    enum class Kind {
        Primitive,
        Particle,
        Light,
        Sound,
        Ribbon,
    };

    /// <summary>自動登録なしで、編集ウィンドウの editBuffer 内の 1 要素を指すラッパーを作る</summary>
    /// <param name="owner">editBuffer を持つエフェクト編集ウィンドウ</param>
    /// <param name="kind">コンポーネントの種類</param>
    /// <param name="index">種類ごとの配列内の位置</param>
    EffectComponentEditable(EffectEditorWindow* owner, Kind kind, int index);
    ~EffectComponentEditable() override = default;

    // IImGuiEditable
    /// <summary>表示名を返す</summary>
    /// <returns>要素の displayName。空なら "&lt;Kind&gt; #&lt;index&gt;"、owner が無ければ "(no owner)"</returns>
    std::string GetName() const override;
    /// <summary>要素の displayName を書き換え、editBuffer を変更ありにする</summary>
    /// <param name="name">新しい表示名</param>
    void SetName(const std::string& name) override;
    /// <summary>種類ごとの型名を返す</summary>
    /// <returns>"EffectPrimitive" / "EffectParticle" / "EffectLight" / "EffectSound" / "EffectRibbon"</returns>
    std::string GetTypeName() const override;
    /// <summary>種類に応じた編集 UI を描き、変えたら editBuffer を変更ありにする</summary>
    void OnImGuiInspector() override;

    /// <summary>コンポーネントの種類を返す</summary>
    /// <returns>種類</returns>
    Kind GetKind() const { return kind_; }
    /// <summary>種類ごとの配列内の位置を返す</summary>
    /// <returns>配列の添字</returns>
    int  GetIndex() const { return index_; }

    /// <summary>
    /// editBuffer 編集で要素位置が変わったときに更新する
    /// </summary>
    /// <param name="i">新しい配列の添字</param>
    void SetIndex(int i) { index_ = i; }

private:
    EffectEditorWindow* owner_;
    Kind kind_;
    int index_;
};
