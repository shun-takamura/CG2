#pragma once

#include "TimeGroup.h"

class ITimeScaleProvider;

/// <summary>
/// エンジン内のグローバルな時間スケール窓口。
/// ゲーム側（SceneManager）が現在のシーンを ITimeScaleProvider として登録し、
/// エンジン側はシーンの具体型を知らずにグループ別 dt を引ける（依存性の逆転）。
/// </summary>
namespace EngineTime {

    /// <summary>
    /// 現在の時間スケール供給元を登録する。シーン切替時にゲーム側が呼ぶ。
    /// nullptr を渡すと供給元なし（fallback のみ）になる。
    /// </summary>
    void SetProvider(ITimeScaleProvider* provider);

    /// <summary>
    /// 供給元があればグループ別のスケール済み dt を、無ければ fallback を返す。
    /// </summary>
    float ScaledDeltaTime(TimeGroup g, float fallback);

} // namespace EngineTime
