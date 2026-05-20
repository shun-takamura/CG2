#pragma once
#include <vector>
#include <memory>
#include <string>
#include "IEnemyCommand.h"

struct WaveEntry;

namespace EnemyCommandFactory {
	/// <summary>
	/// WaveEntry の enemyType からコマンド列を生成して返す。
	/// 未知の enemyType は空リストを返す（エラーにはしない）。
	/// </summary>
	std::vector<std::unique_ptr<IEnemyCommand>> Create(const WaveEntry& entry);
}
