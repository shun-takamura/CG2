#pragma once

#include "AbstractSceneFactory.h"

/// <summary>
/// このゲーム用のシーン工場
/// 具象クラス - ゲーム固有のシーン生成処理を記述
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
	/// <summary>
	/// シーン名に対応するシーンを作る。HUB は来た道（SceneRoutes）で入口を決め、STAGESELECT はコクピットから始める HubScene
	/// </summary>
	/// <param name="sceneName">シーン名（TITLE / HUB / STAGESELECT / STAGEPLAY / RESULT / GAMEOVER / DEMO）</param>
	/// <returns>生成したシーン。知らない名前なら nullptr</returns>
	std::unique_ptr<Scene> CreateScene(const std::string& sceneName) override;
};
