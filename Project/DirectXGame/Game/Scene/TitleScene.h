#pragma once
#include "GameScene.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Vector2.h"
#include "MathUtility.h"
#include <array>
#include <cstdint>
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class WaterReflection;
class WaterSurface;
class CloudLayer;
class TitleDemoMovie;
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

	/// <summary>カメラ・空と IBL・ロゴ・扉・水面と反射・遠景の雲・放置デモを用意し、BGM を鳴らして ① 待機から始める</summary>
	void Initialize() override;
	/// <summary>ポストエフェクト・BGM・放置デモ・扉のポイントライトを止め、共有の Object3DManager から雲を外す</summary>
	void Finalize() override;
	/// <summary>フェーズ・カメラとロゴ・扉・水面・エフェクト・ポストエフェクトを進め、放置デモとトランジションの間以外は入力を読む</summary>
	void Update() override;
	/// <summary>波のシミュレーション → 反射 RT → 空 → 扉 → 水面 → ロゴと線画 → エフェクト → 文字 → 放置デモの順に描く</summary>
	void Draw() override;

	/// <summary>シーンのカメラを取得</summary>
	/// <returns>シーンのカメラ</returns>
	Camera* GetCamera() override;

	/// <summary>ロゴはシーンのコンテナではなくメンバで持つので、影を落とすために足す</summary>
	void DrawShadowCasters() override;

	/// <summary>④ の扉のアウトライン（MaskedOutline）と ⑥ の光の部屋のライトシャフト用に、ID パスへ描く物があるか</summary>
	/// <returns>扉のアウトラインを出している間か、光の部屋が見えていれば true</returns>
	bool HasExtraIdPassObjects() const override;
	/// <summary>扉（アウトラインを出している間）と光の部屋（見えている間）を ID パスへ描く</summary>
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

	/// <summary>演出を ① 待機（ロゴだけ見える状態）に戻す</summary>
	void ResetSequence();
	/// <summary>フェーズを切り替え、入口でやること（波・扉の向きと光・線画・扉・メニュー・扉の光）をする</summary>
	/// <param name="next">次のフェーズ</param>
	void ChangePhase(Phase next);
	/// <summary>今のフェーズのディゾルブ・構図・線画の消え方・扉の開きを進め、終われば次のフェーズへ</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdatePhase(float dt);
	/// <summary>フェーズごとの入力（Press・途中のスキップ・扉に入る / ゲーム終了）を読む。デモモードは時間で自動で進める</summary>
	void UpdateInput();
	/// <summary>「PRESS ANY BUTTON」に数えない入力か（マウスのボタンは波を立てる操作、F8 はデモモードの切り替え）</summary>
	/// <returns>数えない入力が押されたら true</returns>
	bool IsPressExcludedInput() const;
	/// <summary>②〜④ を飛ばして扉が出切った所（⑤）へ</summary>
	void SkipToReady();
	/// <summary>「ゲーム終了」の上か（当たりは文字の四角に quitHitPadding_ を足した範囲）</summary>
	/// <param name="position">マウスの位置（UI 座標）</param>
	/// <returns>上にあれば true</returns>
	bool IsOverQuit(const Vector2& position) const;
	/// <summary>マウスカーソルでなぞった水面に、動かした距離に比例して軌跡に沿って波源を置く</summary>
	void UpdateMouseRipples();
	/// <summary>マウスの位置から描画カメラのレイを飛ばし、水面との交点を求める</summary>
	/// <param name="out">交点（ワールド座標）の書き込み先</param>
	/// <returns>水面に当たれば true（マウスが画面外・空を指していれば false）</returns>
	bool PickWaterPoint(Vector3& out) const;
	/// <summary>周回（③〜④ は減速して止める）と構図の補間でカメラを置き、ロゴを浮遊させてカメラへ向ける</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateCameraAndLogo(float dt);
	/// <summary>扉の枠・扉板（蝶番の軸まわりに開く）・線画を台座の底に合わせて置き、マテリアルを上書きする</summary>
	void UpdateDoor();
	/// <summary>扉の大理石と金のマテリアル（ディゾルブ・IBL 係数・金の PBR 値）を毎フレーム上書きする（遅延ロードのため）</summary>
	/// <param name="baseHeight">台座の底の高さ（ディゾルブの高さの下端）[m]</param>
	void ApplyDoorMaterials(float baseHeight);
	/// <summary>ロゴのテクスチャを差し替え、ライティング無し・ノイズのディゾルブにする（GPU の準備後）</summary>
	void ApplyLogoMaterial();
	/// <summary>線画のマテリアル（ライティング無しの色・濃さ・高さのディゾルブ）を上書きする</summary>
	/// <param name="baseHeight">台座の底の高さ（ディゾルブの高さの下端）[m]</param>
	void ApplyDoorLineMaterials(float baseHeight);
	/// <summary>扉のアウトライン（MaskedOutline）の有効・色・強さを濃さに合わせて設定する</summary>
	void ApplyDoorOutline();
	/// <summary>ロゴの表示を切り替え、水面の反射の対象に足す・外す</summary>
	/// <param name="visible">見せるなら true</param>
	void SetLogoVisible(bool visible);
	/// <summary>扉（枠と扉板）の表示を切り替え、水面の反射の対象に足す・外す</summary>
	/// <param name="visible">見せるなら true</param>
	void SetDoorVisible(bool visible);
	/// <summary>扉の線画の表示を切り替え、水面の反射の対象に足す・外す</summary>
	/// <param name="visible">見せるなら true</param>
	void SetDoorLinesVisible(bool visible);
	/// <summary>光の部屋の表示を切り替え、水面の反射の対象に足す・外す</summary>
	/// <param name="visible">見せるなら true</param>
	void SetDoorLightVisible(bool visible);
	/// <summary>⑥：扉を開き、カメラを扉の中へ前進させ、BGM を消し、途中で白フェードして HUB へ移る</summary>
	void UpdateEnter();
	/// <summary>⑥ の扉の中の光（ライトシャフト・金の装飾を照らすポイントライト・光の部屋の白）を開く量に合わせて設定する</summary>
	void ApplyEnterLighting();
	/// <summary>LightManager から借りた扉のポイントライトを返す</summary>
	void ReleaseDoorPointLight();
	/// <summary>扉・カメラ・光の関係が同じになるよう、平行光源の向きをカメラの止まる角度だけ Y 軸まわりに回す</summary>
	/// <param name="cameraStopAngle">カメラの止まる周回角 [rad]</param>
	void SetSunYaw(float cameraStopAngle);
	/// <summary>ヴィネットの強さを白フェードに合わせて設定する</summary>
	void UpdatePostEffect();
	/// <summary>「ゲーム終了」と「PRESS ANY BUTTON」（Flush まで）</summary>
	void DrawTexts();
	/// <summary>ポストエフェクトの強さの倍率（⑥ の白フェードに合わせて弱め、真っ白で 0）</summary>
	/// <returns>倍率（0〜1）</returns>
	float GetPostEffectFade() const;
	/// <summary>演出パラメータの「Title Tuning」窓（Debug のみ）</summary>
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

	// 放置したらデモ映像を流す（展示用。TitleDemo.json）
	std::unique_ptr<TitleDemoMovie> demoMovie_;

	// デバッグカメラで差し替わる前のゲームカメラ（反射はこの視点で作る）
	Matrix4x4 gameViewProjection_{};
	Vector3 gameEyePosition_{};
	// Debug: デバッグカメラ中、反射をデバッグカメラ基準にする（既定はゲームカメラ基準で見え方を確認）
	bool reflectFromDebugCamera_ = false;

	// ⑤ の「ゲーム終了」にカーソルが乗っている / キーで選んでいる
	bool quitFocused_ = false;

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
	float vignetteIntensity_ = 0.0f;         // 0 で無効（2026-10-06：扉の見上げ構図で画面が暗く見えたので切った）
	// ⑤ の「ゲーム終了」（左上。2026-10-24 ユーザー：「スタート」は消して、ほかの所をクリックしたら扉に入る）
	Vector2 quitMargin_ = { 32.0f, 28.0f };  // 画面の左上からの位置 [px]
	float quitScale_ = 1.0f;
	float quitHitPadding_ = 12.0f;           // マウスの当たりを文字の四角から広げる量 [px]

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

	float doorYaw_ = kPi;            // 扉の向き [rad]。0 で正面が +Z。③ の開始でカメラの停止角に合わせて決め直す
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

	/// <summary>マテリアルにディゾルブの値を書く（見えきっている間は無効にしてノイズの計算を省く）</summary>
	/// <param name="material">書き込むマテリアル</param>
	/// <param name="progress">見えている割合（0..1）</param>
	/// <param name="style">ディゾルブの見た目</param>
	/// <param name="heightMin">高さのディゾルブの下端 [m]</param>
	/// <param name="heightMax">高さのディゾルブの上端 [m]</param>
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
