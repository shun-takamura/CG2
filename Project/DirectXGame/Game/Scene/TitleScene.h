#pragma once
#include "GameScene.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "VerticalMenu.h"
#include <array>
#include <cstdint>
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class WaterReflection;
class WaterSurface;
class CloudLayer;
struct Material;

/// <summary>
/// タイトルシーン
/// 水面に浮かぶロゴの周りをカメラが回る → Press でロゴが消え、扉が現れて正面で止まる →
/// メニュー（スタート / ゲーム終了）→ 扉が開いて HUB へ。流れは 5_TitleScene.md §10.2。
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

	// ④ の扉のアウトライン（MaskedOutline）用に、扉を ID パスへ描く
	bool HasExtraIdPassObjects() const override;
	void DrawExtraIdPassObjects() override;

private:
	// §10.2 の ①〜⑥
	enum class Phase {
		Idle,         // ① 待機：ロゴが浮遊、PRESS ANY BUTTON
		LogoVanish,   // ② ロゴが消える（大きな波を出し続ける）
		DoorOutline,  // ③ 扉の線画（カメラが減速を始める）
		DoorForm,     // ④ 扉が実体化
		Ready,        // ⑤ 扉の正面で止まり、メニューを出す
		Enter,        // ⑥ 扉が開いて HUB へ
	};

	void ResetSequence();
	void ChangePhase(Phase next);
	void UpdatePhase(float dt);
	void UpdateInput();
	bool IsPressExcludedInput() const;
	void UpdateMouseRipples();
	bool PickWaterPoint(Vector3& out) const;
	void UpdateCameraAndLogo(float dt);
	void UpdateDoor();
	void ApplyDoorMaterials(float baseHeight);
	void ApplyLogoMaterial();
	void ApplyDoorLineMaterials(float baseHeight);
	void ApplyDoorOutline();
	void SetLogoVisible(bool visible);
	void SetDoorVisible(bool visible);
	void SetDoorLinesVisible(bool visible);
	void SetDoorLightVisible(bool visible);
	void UpdateEnter();
	void ApplyEnterLighting();
	void ReleaseDoorPointLight();
	void SetSunYaw(float cameraStopAngle);
	void UpdatePostEffect();
	float GetPostEffectFade() const;
	void OnImGuiTuning();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<Object3DInstance> logo_;

	// 扉（枠＋左右の扉板）。扉板の原点は蝶番の軸で、奥（-Z）へ開く
	std::unique_ptr<Object3DInstance> doorFrame_;
	std::unique_ptr<Object3DInstance> doorLeafL_;
	std::unique_ptr<Object3DInstance> doorLeafR_;
	// 扉の線画（③）。gen_title_door.py --export-lines で作った特徴線の細い角柱。並びは 枠 / 左 / 右
	std::array<std::unique_ptr<Object3DInstance>, 3> doorLines_;
	// 光の部屋（⑥）。扉の奥の、開口部の形をした内向きの白い筒（gen_title_door.py --export-light）
	std::unique_ptr<Object3DInstance> doorLight_;

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

	Phase phase_ = Phase::Idle;
	float phaseTime_ = 0.0f;                 // 今のフェーズに入ってからの秒
	// シーン開始からの経過秒（ロゴ登場・浮遊・点滅の基準）
	float elapsed_ = 0.0f;
	bool logoVisible_ = true;
	bool doorVisible_ = false;
	bool doorLinesVisible_ = false;
	bool doorLightVisible_ = false;
	// ディゾルブの進み具合（見えている割合 0..1。Material::dissolveProgress）
	float logoDissolve_ = 1.0f;
	float doorDissolve_ = 1.0f;
	float doorLineDissolve_ = 1.0f;
	// 線画と扉のアウトラインの濃さ（0..1）。④ で出し、⑤ でそろって消える
	float doorLineAlpha_ = 1.0f;
	float doorOutlineIntensity_ = 0.0f;
	bool enterTransitionStarted_ = false;

	// カメラの停止（③〜④）。止まる角度を ③ の開始で決め、扉をその角度に向けて立てる
	// （扉は ③ まで存在しないので、カメラを扉の正面へ回り込ませる必要がない）
	float orbitStopElapsed_ = 0.0f;
	float orbitStopStartAngle_ = 0.0f;
	float orbitStopEndAngle_ = 0.0f;

	// ----- 演出パラメータ（Debug では "Title Tuning" ウィンドウで調整可） -----
	// カメラは周回中心（水面上。ロゴの真下＝さざ波の中心）の周りを回る。
	Vector3 orbitCenter_ = { 0.0f, 0.0f, 0.0f };
	float orbitAngle_ = 0.0f;                // 周回角（＝カメラの Yaw）[rad]
	float orbitSpeed_ = 0.04f;               // 周回の角速度 [rad/s]

	// カメラの構図（§10.7）。④ の間にロゴ用→扉用へ補間する
	struct CameraFraming {
		float fovY;         // 縦の視野角 [rad]
		float radius;       // 周回中心からカメラまでの水平距離 [m]
		float height;       // 水面からのカメラの高さ [m]
		float aimHeight;    // カメラが見る点の水面からの高さ [m]（カメラより高いと見上げ）
	};
	// ロゴ用：望遠で少し見下ろす。ロゴより下を見るとロゴが画面上寄り＋映り込みが入る
	CameraFraming logoFraming_{ 0.45f, 20.0f, 1.5f, 0.55f };
	// 扉用：すずめの戸締まり風に、水面すれすれから広角で扉の上部を見上げる（地平線が画面の下寄り、空が広い）
	CameraFraming doorFraming_{ 0.75f, 10.0f, 0.65f, 2.75f };
	// ⑥ 扉に入る：扉の中心の高さで水平に、枠の手前まで寄る
	CameraFraming enterFraming_{ 0.85f, 0.5f, 1.6f, 1.6f };
	float framingBlend_ = 0.0f;              // 0=ロゴ用 / 1=扉用
	float logoHeight_ = 1.2f;                // ロゴ（板の中心）の水面からの高さ [m]
	float logoScale_ = 2.0f;                 // 板（幅 2 × 高さ 1）の倍率
	float logoBobAmplitude_ = 0.12f;         // 上下の浮遊幅
	float logoBobSpeed_ = 1.4f;
	float logoSwayAmplitude_ = 0.12f;        // 左右の首振り幅 [rad]
	float logoSwaySpeed_ = 0.7f;
	float introDuration_ = 1.4f;             // ロゴ登場の長さ
	float vignetteIntensity_ = 0.7f;
	// ⑤ のメニュー（スタート / ゲーム終了）の縦位置（画面の高さに対する割合）。扉に重ならないよう下寄せ
	float menuHeightRatio_ = 0.94f;

	// ----- フェーズの長さ [s] -----
	float logoVanishSeconds_ = 2.0f;
	float doorOutlineSeconds_ = 1.5f;
	float doorFormSeconds_ = 2.5f;
	// ⑥ カメラが前進を始めてから真っ白になるまでが 1.5 秒（0.4s → 1.9s）
	float enterOpenSeconds_ = 1.3f;          // ⑥ 扉が開ききるまで
	float enterTransitionDelay_ = 1.1f;      // ⑥ 開き始めてから白フェード（HUB への遷移）を始めるまで
	float enterMoveDelay_ = 0.4f;            // ⑥ 開き始めてからカメラが前進を始めるまで
	float enterMoveSeconds_ = 1.6f;          // ⑥ カメラが扉の手前まで進む長さ（ease-in）
	float whiteFadeSeconds_ = 0.8f;          // 白くなるまで（カメラが扉に届く前に真っ白になるよう逆算）
	float whiteHoldSeconds_ = 0.3f;
	float enterBgmFadeSeconds_ = 2.0f;       // ⑥ の開始から BGM が消えるまで
	float bgmVolume_ = 0.5f;
	bool enterMoveSoundPlayed_ = false;
	float enterBlend_ = 0.0f;                // 0=扉用の構図 / 1=扉に入る構図

	// マウスカーソルでなぞった所に波を立てる（WaterSurface の波のシミュレーションに波源を入れる）
	bool hasPrevMousePoint_ = false;
	Vector3 prevMousePoint_{};
	float mouseRippleRadius_ = 0.25f;        // 1 つの波源の半径 [m]
	float mouseRippleStrength_ = 0.03f;      // 1m 動かしたときのへこみの深さ [m]（動かした距離に比例）
	float mouseRippleMaxStrength_ = 0.015f;  // 1 つの波源のへこみの上限 [m]
	float mouseRippleSpacing_ = 0.15f;       // 速く動かしたとき、軌跡に沿って波源を置く間隔 [m]

	// ② 〜 ④ の大きな波（WaterSurface::StartRingBurst）
	float burstInterval_ = 1.2f;
	float burstAmplitude_ = 0.12f;
	float burstWavelength_ = 2.5f;

	float doorYaw_ = 3.1415927f;             // 扉の向き [rad]。0 で正面が +Z。③ の開始でカメラの停止角に合わせて決め直す
	float doorOpenDegrees_ = 0.0f;           // 扉板の開き角 [deg]。奥へ開く
	float doorOpenTargetDegrees_ = 85.0f;    // ⑥ で開く角度

	// ディゾルブの見た目（Object3D/Dissolve.hlsli）
	struct DissolveStyle {
		float edgeWidth;    // 境目の発光帯の幅
		float noiseScale;   // ノイズの細かさ [1/m]
		float noiseWeight;  // 0=高さだけ / 1=ノイズだけ
		Vector3 edgeColor;
	};
	// ④ 扉は下から、ノイズで縁を崩しながら現れる。高さの範囲は台座の底〜アーチの頂点
	DissolveStyle doorDissolveStyle_{ 0.08f, 4.0f, 0.35f, { 1.0f, 0.88f, 0.55f } };
	float doorDissolveHeight_ = 3.8f;
	// ② ロゴはノイズだけで消える
	DissolveStyle logoDissolveStyle_{ 0.08f, 6.0f, 1.0f, { 0.75f, 0.9f, 1.0f } };
	// ③ 線画は高さだけ（ノイズなし）で下から現れる
	DissolveStyle doorLineDissolveStyle_{ 0.04f, 4.0f, 0.0f, { 1.0f, 1.0f, 1.0f } };
	Vector4 doorLineColor_ = { 1.0f, 0.86f, 0.5f, 1.0f };       // 線画の色（ライティング無し）
	Vector4 doorOutlineColor_ = { 1.0f, 0.9f, 0.62f, 1.0f };    // ④ の扉のアウトラインの色
	float doorOutlineFadeInSeconds_ = 0.3f;                      // ④ の開始でアウトラインを出す長さ
	float doorLineFadeSeconds_ = 0.8f;                           // ⑤ で線画とアウトラインを消す長さ
	float doorOutlineEdgeStrength_ = 1.5f;                       // 折れ目の線の強さ（MaskedOutline）

	// ⑥ 扉の中の光。開く量（0..1）に合わせて強くする
	float lightShaftIntensity_ = 1.2f;                           // ライトシャフト（LightShaftEffect）の強さ
	Vector3 lightShaftColor_ = { 1.0f, 0.92f, 0.78f };
	float lightCenterHeight_ = 1.6f;                             // 光源の中心（台座の底から）
	float lightCenterDepth_ = 0.7f;                              // 光源の中心（扉の正面から奥へ）
	uint32_t doorPointLight_ = UINT32_MAX;                       // LightManager から借りた枠（kInvalidLightSlot）
	float doorPointLightIntensity_ = 3.0f;                       // 金の装飾に光を映すポイントライト
	float doorPointLightRadius_ = 6.0f;
	Vector4 doorPointLightColor_ = { 1.0f, 0.9f, 0.72f, 1.0f };
	float enterOpenRatio_ = 0.0f;

	static void ApplyDissolve(Material& material, float progress, const DissolveStyle& style,
		float heightMin, float heightMax);
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
};
