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
    };

    EffectComponentEditable(EffectEditorWindow* owner, Kind kind, int index);
    ~EffectComponentEditable() override = default;

    // IImGuiEditable
    std::string GetName() const override;
    void SetName(const std::string& name) override;
    std::string GetTypeName() const override;
    void OnImGuiInspector() override;

    Kind GetKind() const { return kind_; }
    int  GetIndex() const { return index_; }

    /// <summary>
    /// editBuffer 編集で要素位置が変わったときに更新する
    /// </summary>
    void SetIndex(int i) { index_ = i; }

private:
    EffectEditorWindow* owner_;
    Kind kind_;
    int index_;
};
