#pragma once
#include "BaseScene.h"
#include <memory>

class Camera;
class Skybox;
class SplineCurveActor;
class RailCameraController;

/// <summary>
/// ステージプレイシーン
/// 道中レールSTG(2-3分) → ボス戦3Dアクション(3-5分) をシームレスに繋ぐ
/// 内部に Phase（Rail / Landing / Boss）を持つ予定
/// </summary>
class StagePlayScene : public BaseScene {
public:
	enum class Phase {
		Rail,       // レールシューティング道中
		Landing,    // 着地遷移（カメラ補間）
		Boss,       // ボス戦3Dアクション
	};

	StagePlayScene();
	~StagePlayScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	// シーンタイムラインのシーク。RailCamera の progress を経過秒から再計算する。
	void Seek(float seconds) override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;

	// レールカメラ用の 2 本のスプライン（cameraPath=位置 / lookAtPath=注視点）
	std::unique_ptr<SplineCurveActor> cameraPath_;
	std::unique_ptr<SplineCurveActor> lookAtPath_;
	std::unique_ptr<RailCameraController> railCamera_;

	Phase phase_ = Phase::Rail;

	// ポーズ状態（後でメニュー実装）
	bool paused_ = false;
};
