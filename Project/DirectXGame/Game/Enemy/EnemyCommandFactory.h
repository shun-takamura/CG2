#pragma once
#include <vector>
#include <memory>
#include <string>
#include "IEnemyCommand.h"

struct WaveEntry;
struct PrefabDef;

namespace EnemyCommandFactory {
	/// <summary>
	/// コマンド列を生成して返す。
	/// 移動方法は prefab の movementType が優先（ScreenHover 等）。
	/// movementType が SplineFollow（または prefab=nullptr / hasMovement=false）の場合は
	/// 従来通り WaveEntry の enemyType（Drone/Carrier/Rusher）でコマンド列を決める。
	/// 未知の場合は空リストを返す（エラーにはしない）。
	/// </summary>
	std::vector<std::unique_ptr<IEnemyCommand>> Create(const WaveEntry& entry, const PrefabDef* prefab = nullptr);
}
