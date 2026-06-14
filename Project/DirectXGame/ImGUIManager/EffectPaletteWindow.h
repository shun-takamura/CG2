#pragma once
#include "IImGuiWindow.h"

/// <summary>
/// EffectEditor 用のコンポーネント追加パレット。
/// Primitive（8 meshType に展開）/ Particle / Light / Sound を
/// ドラッグ＆ドロップのソースとして並べるだけの独立ウィンドウ。
/// 投下先は EffectEditor ビューポート（EFFECT_COMP_DROP ペイロード）。
/// 編集状態は持たず純粋な D&D ソースなので、editor / mgr への依存はない。
/// </summary>
class EffectPaletteWindow : public IImGuiWindow {
public:
    EffectPaletteWindow();
    ~EffectPaletteWindow() override = default;

protected:
    void OnDraw() override;
};
