#pragma once

#include <cstddef>

/// <summary>
/// 時間スケールのグループ識別子。
/// シーンが用途別に独立した時間倍率を持てるようにする。
///   World  : 敵・地形・敵弾・背景エフェクト（シーンのスローに従う）
///   Player : プレイヤー、自機弾、自機関連エフェクト（独立制御可能）
///   UI     : UI演出（基本リアル時間、ポーズ中でも進める）
/// 用途例：
///   通常ヒットストップ        : World=0.05, Player=0.05, UI=1.0
///   ジャスト回避スロー         : World=0.3,  Player=1.0, UI=1.0
///   BotWラッシュ的演出         : World=0.2,  Player=1.5, UI=1.0
///   ディスラプター（全停止演出） : World=0.0,  Player=0.0, UI=1.0
/// </summary>
enum class TimeGroup : int {
    World = 0,
    Player,
    UI,
    Count
};

inline const char* GetTimeGroupName(TimeGroup g) {
    switch (g) {
    case TimeGroup::World:  return "World";
    case TimeGroup::Player: return "Player";
    case TimeGroup::UI:     return "UI";
    default:                return "";
    }
}
