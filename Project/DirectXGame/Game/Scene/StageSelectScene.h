#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include <memory>

class Camera;

/// <summary>
/// ステージセレクトシーン（機体のコクピットの中。17_HubScene.md §3）。
/// 「チュートリアル / ステージ1 / HUB に戻る」を縦のメニューで選ぶ。チュートリアルはステージが無いうちは選べない。
/// 見た目（緑のネオンの灯り・UI の起動とシャットダウン）はチャット C（18 番 ①の IN / ②の OUT）で作る。
/// </summary>
class StageSelectScene : public GameScene {
public:
	StageSelectScene();
	~StageSelectScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
	VerticalMenu menu_;
};
