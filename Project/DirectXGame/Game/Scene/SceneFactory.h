#pragma once

#include "AbstractSceneFactory.h"

/// <summary>
/// このゲーム用のシーン工場
/// 具象クラス - ゲーム固有のシーン生成処理を記述
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
	/// <summary>
	/// シーン生成
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <returns>生成したシーン</returns>
	BaseScene* CreateScene(const std::string& sceneName) override;
};
