#include "SceneFactory.h"
#include "TitleScene.h"
#include "HubScene.h"
#include "StagePlayScene.h"
#include "ResultScene.h"
#include "DemoScene.h"

std::unique_ptr<Scene> SceneFactory::CreateScene(const std::string& sceneName) {

    if (sceneName == "TITLE") {
        return std::make_unique<TitleScene>();
    }
    else if (sceneName == "HUB") {
        return std::make_unique<HubScene>();
    }
    else if (sceneName == "STAGEPLAY") {
        return std::make_unique<StagePlayScene>();
    }
    else if (sceneName == "RESULT") {
        return std::make_unique<ResultScene>();
    }
    else if (sceneName == "DEMO") {
        // 技術開発用：絶対に削除しない
        return std::make_unique<DemoScene>();
    }

    return nullptr;
}
