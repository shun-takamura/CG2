#pragma once
#include "GameScene.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "VerticalMenu.h"
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class WaterReflection;
class WaterSurface;
class CloudLayer;

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

	// ロゴはシーンのコンテナではなくメンバで持つので、影を落とすために足す
	void DrawShadowCasters() override;

private:
	void UpdateInput();
	void UpdateCameraAndLogo(float dt);
	void UpdateDoor();
	void ApplyDoorMaterials();
	void SetLogoVisible(bool visible);
	void UpdateIntroPostEffect();
	void OnImGuiTuning();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<Object3DInstance> logo_;

	// 扉（枠＋左右の扉板）。扉板の原点は蝶番の軸で、奥（-Z）へ開く
	std::unique_ptr<Object3DInstance> doorFrame_;
	std::unique_ptr<Object3DInstance> doorLeafL_;
	std::unique_ptr<Object3DInstance> doorLeafR_;

	// 水面（y=0 の浅い水。空とロゴを映し、水底の床が透ける）
	std::unique_ptr<WaterReflection> waterReflection_;
	std::unique_ptr<WaterSurface> water_;

	// 遠景の雲。Skybox と水面が同じ CB を挿すので、空と映り込みで形がずれない
	std::unique_ptr<CloudLayer> cloudLayer_;

	// デバッグカメラで差し替わる前のゲームカメラ（反射はこの視点で作る）
	Matrix4x4 gameViewProjection_{};
	Vector3 gameEyePosition_{};
	// Debug: デバッグカメラ中、反射をデバッグカメラ基準にする（既定はゲームカメラ基準で見え方を確認）
	bool reflectFromDebugCamera_ = false;

	VerticalMenu menu_;
	bool menuOpen_ = false;

	// シーン開始からの経過秒（ロゴ登場・浮遊・点滅の基準）
	float elapsed_ = 0.0f;

	// ----- 演出パラメータ（Debug では "Title Tuning" ウィンドウで調整可） -----
	// カメラは周回中心（水面上。ロゴの真下＝さざ波の中心）の周りを回る。
	// 扉の演出では orbitAngle_ を目標角へ減速させて止める想定。
	Vector3 orbitCenter_ = { 0.0f, 0.0f, 0.0f };
	float orbitAngle_ = 0.0f;                // 周回角（＝カメラの Yaw）[rad]
	float orbitSpeed_ = 0.04f;               // 周回の角速度 [rad/s]
	float orbitRadius_ = 20.0f;              // 周回中心からカメラまでの水平距離 [m]。扉（高さ約 3.7m）全体が入る距離
	float cameraHeight_ = 1.5f;              // 水面からのカメラの高さ [m]
	float aimHeight_ = 0.55f;                // カメラが見る点の水面からの高さ。ロゴより下を見るとロゴが画面上寄り＋映り込みが入る
	float logoHeight_ = 0.95f;               // ロゴ中心の水面からの高さ [m]
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

	// 扉の確認用（状態機械を作るまでの暫定。扉とロゴは同じ位置なので扉の確認中はロゴを隠す）
	bool showDoor_ = true;
	bool showLogo_ = false;
	float doorYaw_ = 3.1415927f;             // 扉の向き [rad]。0 で正面が +Z。開始時のカメラ（-Z 側から +Z を見る）に正面を向ける
	float doorOpenDegrees_ = 0.0f;           // 扉板の開き角 [deg]。奥へ開く
	// PBR は拡散を π で割るので、従来より強い日差しが要る（ロゴは BlinnPhong なので表示時に要確認）
	float sunIntensity_ = 4.55f;
	// 大理石の IBL 係数。空の平均色（濃い青）で白い石が青く染まるのを抑える
	float doorMarbleEnvCoefficient_ = 0.25f;
	// 金の PBR 値（.mat の値を上書き）。値は実機で調整したもの（2026-10-04）。
	// 映り込みは IBL 専用の cubemap（地平線より下＝石の暖色）と遠景の雲で金色を出す
	struct DoorGoldParams {
		float envCoefficient = 0.56f;
		float metallic = 1.0f;
		float roughness = 0.6f;
		Vector4 color = { 1.0f, 0.72f, 0.26f, 1.0f };  // (255, 184, 66)
		float cloudReflection = 1.0f;  // 遠景の雲の映り込み（空とは別の強さ）
	};
	DoorGoldParams doorGold_;

	// 無操作タイマー（秒）。閾値を超えたらデモ動画再生に遷移する予定
	float idleSeconds_ = 0.0f;
	static constexpr float kDemoTriggerSeconds = 10.0f;
};
