#pragma once

#include "TimeGroup.h"

/// <summary>
/// グループ別のスケール済みデルタタイムを供給する側のインターフェース。
/// エンジン（パーティクル等）はこの抽象だけに依存し、具体的なシーン実装
/// （ゲーム側の GameScene）を名指ししない。実体はゲーム側が EngineTime に登録する。
/// </summary>
class ITimeScaleProvider {
public:
    virtual ~ITimeScaleProvider() = default;

    /// <summary>
    /// 指定 TimeGroup のスケール済みデルタタイムを返す。
    /// </summary>
    /// <param name="g">グループ</param>
    /// <returns>スケール済みの経過時間 [秒]</returns>
    virtual float GetScaledDeltaTime(TimeGroup g) const = 0;
};
