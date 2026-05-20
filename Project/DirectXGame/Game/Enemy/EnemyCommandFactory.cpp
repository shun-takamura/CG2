#include "EnemyCommandFactory.h"
#include "Wave/WaveDef.h"
#include "Commands/ShootAtPlayerCommand.h"
#include "Commands/RetreatCommand.h"
#include "Commands/ChargeRushCommand.h"
#include "Commands/SpawnDroneCommand.h"

namespace EnemyCommandFactory {

	std::vector<std::unique_ptr<IEnemyCommand>> Create(const WaveEntry& entry) {
		std::vector<std::unique_ptr<IEnemyCommand>> cmds;

		if (entry.enemyType == "Drone") {
			// 射撃し続け、退避トリガーで RetreatCommand へジャンプ
			cmds.push_back(std::make_unique<ShootAtPlayerCommand>());
			cmds.push_back(std::make_unique<RetreatCommand>());

		} else if (entry.enemyType == "Carrier") {
			// ザコを生成し続け、退避トリガーで RetreatCommand へジャンプ
			cmds.push_back(std::make_unique<SpawnDroneCommand>());
			cmds.push_back(std::make_unique<RetreatCommand>());

		} else if (entry.enemyType == "Rusher") {
			// スプライン終端到達後に溜め→突進（退避なし）
			cmds.push_back(std::make_unique<ChargeRushCommand>());
		}

		return cmds;
	}

}
