#include "SceneFactory.h"
#include "TitleScene.h"
#include "GameScene.h"
#include "DemoScene.h"
#include "CharacterSelect.h"
#include "ClearScene.h"
#include "GameOver.h"

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {

    if (sceneName == "TITLE") {

        return std::make_unique<TitleScene>();

    } else if (sceneName == "GAMEPLAY") {

        return std::make_unique<GameScene>();

    } else if (sceneName=="DEMO"){

        return std::make_unique<DemoScene>();

    } else if (sceneName == "CHARACTERSELECT") {

        return std::make_unique<CharacterSelect>();

    } else if (sceneName == "CLEAR") {

        return std::make_unique<ClearScene>();

    } else if (sceneName == "GAMEOVER") {

        return std::make_unique<GameOver>();

    }

    return nullptr;
}
