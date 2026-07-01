#include "EnemyCommandFactory.h"
#include "Wave/WaveDef.h"
#include "Components/Prefab.h"
#include "Commands/ShootAtPlayerCommand.h"
#include "Commands/RetreatCommand.h"
#include "Commands/ChargeRushCommand.h"
#include "Commands/SpawnDroneCommand.h"
#include "Commands/HoverStationCommand.h"

namespace EnemyCommandFactory {

	std::vector<std::unique_ptr<IEnemyCommand>> Create(const WaveEntry& entry, const PrefabDef* prefab) {
		std::vector<std::unique_ptr<IEnemyCommand>> cmds;

		// 移動方法はプレハブが優先（hasMovement なら movementType で分岐）
		if (prefab && prefab->hasMovement) {
			switch (prefab->movementType) {
			case MovementType::ScreenHover:
				// 飛来→停止して攻撃→退避
				cmds.push_back(std::make_unique<HoverStationCommand>());
				cmds.push_back(std::make_unique<RetreatCommand>());
				return cmds;
			case MovementType::SplineFollow:
			case MovementType::Drift:
			case MovementType::Static:
			default:
				// 以下の enemyType ベースのフォールバックへ
				break;
			}
		}

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
