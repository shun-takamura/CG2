#pragma once
#include "GameScene.h"
#include "Vector3.h"
#include "VerticalMenu.h"
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;

/// <summary>
/// タイトルシーン
/// ゆっくり旋回する Skybox を背景に、3D ロゴ（title.mesh）を登場させて浮遊させる。
/// Press Any Button でメニュー（スタート / ゲーム終了）を出す2段構え。
/// 一定時間無操作でデモプレイ動画を再生する仕様（未実装）
/// </summary>
class TitleScene : public GameScene {
public:
	TitleScene();
	~TitleScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	void UpdateInput();
	void UpdateCameraAndLogo(float dt);
	void UpdateIntroPostEffect();
	void OnImGuiTuning();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<Object3DInstance> logo_;

	VerticalMenu menu_;
	bool menuOpen_ = false;

	// シーン開始からの経過秒（ロゴ登場・浮遊・点滅の基準）
	float elapsed_ = 0.0f;

	// ----- 演出パラメータ（Debug では "Title Tuning" ウィンドウで調整可） -----
	float cameraYawSpeed_ = 0.04f;           // 背景の旋回速度 [rad/s]
	float cameraPitch_ = -0.05f;
	float logoDistance_ = 8.0f;              // カメラ前方の距離
	float logoHeight_ = 0.4f;                // 画面中央からの持ち上げ量（カメラ上方向）
	float logoScale_ = 3.0f;
	// 横倒しの立体文字を正面へ起こす。X=+90°（-90°だと上下も反転）＋ Y=180°（書き出しで左右反転しているため裏側から見せる）
	Vector3 logoBaseRotate_ = { 1.5707963f, 3.1415927f, 0.0f };
	// メッシュの見た目の中心（モデル空間）。title.mesh は原点が中心から外れているので、配置時にこの分を打ち消す
	Vector3 logoPivot_ = { 0.064f, 0.0f, -0.276f };
	float logoBobAmplitude_ = 0.12f;         // 上下の浮遊幅
	float logoBobSpeed_ = 1.4f;
	float logoSwayAmplitude_ = 0.12f;        // 左右の首振り幅 [rad]
	float logoSwaySpeed_ = 0.7f;
	float introDuration_ = 1.4f;             // ロゴ登場とラジアルブラー収束の長さ
	float introBlurWidth_ = 0.08f;
	float vignetteIntensity_ = 0.7f;

	// 無操作タイマー（秒）。閾値を超えたらデモ動画再生に遷移する予定
	float idleSeconds_ = 0.0f;
	static constexpr float kDemoTriggerSeconds = 10.0f;
};
