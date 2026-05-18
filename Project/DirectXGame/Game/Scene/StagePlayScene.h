#pragma once
#include "BaseScene.h"
#include "Vector2.h"
#include "Vector3.h"
#include <memory>

class Camera;
class Skybox;
class SplineCurveActor;
class RailCameraController;
class AnimatedObject3DInstance;
class Reticle;

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

	// プレイヤー：dynamicAnimated_ が所有、ここは参照用ポインタ
	// カメラのローカル空間で playerLocalOffset_ の位置に毎フレ配置する
	AnimatedObject3DInstance* player_ = nullptr;

	// ----- ImGui で編集可能な調整値（Resources/Json/Tuning/StagePlay.json に自動同期） -----
	Vector3 playerLocalOffset_{ 0.0f, -0.5f, 6.0f };  // カメラローカルの中心位置（無入力時）
	float   railCameraSpeed_ = 0.05f;                  // RailCamera の進行速度（t/秒）
	Vector2 playerMoveSpeed_{ 5.0f, 5.0f };            // 入力1秒あたりのオフセット移動量（カメラ空間X/Y）
	Vector2 playerClipMargin_{ 0.1f, 0.1f };           // クリップ空間で許す画面外マージン（X/Y）
	float   playerSmoothTime_ = 0.15f;                 // 慣性の指数減衰時定数（秒）：小さい=反応速い

	// 入力で加算するオフセット / 慣性用速度（ランタイムのみ、JSON 非保存）
	Vector2 playerInputOffset_{ 0.0f, 0.0f };
	Vector2 playerVelocity_{ 0.0f, 0.0f };

	// レティクル
	std::unique_ptr<Reticle> reticle_;

	// ----- ジャスト回避演出 -----
	bool  justDodgeActive_ = false;
	float justDodgeTimer_ = 0.0f;
	float justDodgeDuration_ = 1.5f;
	float justDodgeFadeIn_ = 0.15f;
	float justDodgeFadeOut_ = 0.3f;
	IImGuiEditable* justDodgeTarget_ = nullptr;

	void UpdateJustDodgeEffect(float dt);

public:
	/// <summary>
	/// ジャスト回避演出を発動する。プレイヤー + targetEnemy 以外のオブジェクトを
	/// duration 秒間グレースケール化する（最初/最後で短くフェード）。
	/// </summary>
	void PlayJustDodgeEffect(IImGuiEditable* targetEnemy, float duration = 1.5f);
private:

	Phase phase_ = Phase::Rail;

	void LoadTuningFromJson();
	void SaveTuningToJson() const;

public:
	// ImGuiManager の "StagePlay Tuning" タブから呼ばれる中身（ImGui::Begin/End なし）
	void OnImGuiTuning();
private:

	// ポーズ状態（後でメニュー実装）
	bool paused_ = false;
};
