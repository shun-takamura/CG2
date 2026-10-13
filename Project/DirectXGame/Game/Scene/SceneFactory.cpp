#include "SceneFactory.h"
#include "TitleScene.h"
#include "HubScene.h"
#include "StagePlayScene.h"
#include "ResultScene.h"
#include "GameOverScene.h"
#include "DemoScene.h"
#include "SceneManager.h"
#include "SceneRoutes.h"
#include "TransitionManager.h"

std::unique_ptr<Scene> SceneFactory::CreateScene(const std::string& sceneName) {

    if (sceneName == "TITLE") {
        return std::make_unique<TitleScene>();
    }
    else if (sceneName == "HUB") {
        // 入口は来た道で決める（18_HubTransitions.md ⑦）。開く側をシーンに任せる道（今はステージからデバッグの F3）なら
        // 真っ暗なコクピットから降りる。タイトルから（白フェード）・デバッグの起動は神殿で待機から。
        // ここではまだ切り替える前なので、今のシーン名＝前のシーン
        const std::string& from = SceneManager::GetInstance()->GetCurrentSceneName();
        const bool revealByScene = SceneRoutes::Find(from, sceneName).reveal == TransitionManager::kRevealByScene;
        return std::make_unique<HubScene>(revealByScene ? HubScene::Entry::Disembark : HubScene::Entry::Temple);
    }
    else if (sceneName == "STAGESELECT") {
        // ステージ選択は HUB のコクピットの中（18_HubTransitions.md ①「C で決まったこと」）。起動から始める
        return std::make_unique<HubScene>(HubScene::Entry::Cockpit);
    }
    else if (sceneName == "STAGEPLAY") {
        return std::make_unique<StagePlayScene>();
    }
    else if (sceneName == "RESULT") {
        return std::make_unique<ResultScene>();
    }
    else if (sceneName == "GAMEOVER") {
        return std::make_unique<GameOverScene>();
    }
    else if (sceneName == "DEMO") {
        // 技術開発用：絶対に削除しない
        return std::make_unique<DemoScene>();
    }

    return nullptr;
}
