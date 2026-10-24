#pragma once
#include "GameScene.h"
#include "Score/EtherState.h"
#include "Vector2.h"
#include "Vector3.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class EtherVessel;
class SpriteInstance;
class WaterSurface;

/// <summary>
/// ゲームオーバー画面（19_GameOverAndDifficulty.md §4.6、G6）。暗い水面に立つエーテルの器で、守れている賭け分を見せ、
/// 再挑戦するか引き上げるかを委ねる（§3：失った量・損・数字は出さない。難易度を下げるか聞かない）。
///   1 浮かび上がる：着水（15 番 T4）の波紋から。カメラは液面のすぐ上から引く
///   2 少し減る　　：カメラが引き終わったら、針が下がった難易度へ動く・液面が下がる・液面から糸と粒が天へ昇る、を同時に（2026-10-24 ユーザー）
///   3 収まる　　　：針が少し行き過ぎて戻り、液体の揺れが収まる。光も少し弱まる（減る量・下がる幅は EtherState が入口で黙って決める）
///   4 選択肢　　　：「ボス戦から／はじめから／引き上げる」（チェックポイントが無いときは「もう一度／引き上げる」）
///   5a 再挑戦　　 ：球の光が強まる（糸・粒は出さない。2026-10-24 ユーザー）→ 羽根の飛翔
///   5b 引き上げる ：残ったエーテルが支柱を伝って泉へ下りる → ステージ選択
/// 2〜3 の間に決定を押すと選択肢まで早送り（2026-10-24 ユーザー）。調整値は Resources/Json/Tuning/GameOver.json。
/// </summary>
class GameOverScene : public GameScene {
public:
	enum class Option {
		BossRetry,   // ボス戦から
		Restart,     // はじめから／もう一度
		Withdraw,    // 引き上げる
	};

	GameOverScene();
	~GameOverScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

	bool HasBloomSources() const override;
	void DrawBloomSources() override;

private:
	enum class Step {
		Emerge,   // 1 浮かび上がる
		Drain,    // 2 少し減る（針・液面・糸が同時）
		Tick,     // 3 揺れが収まるのを待つ
		Choose,   // 4 選択肢
		Rise,     // 5a 再挑戦
		Return,   // 5b 引き上げる
		Leaving,  // シーンを切り替え中
	};

	void InitializeWater();
	void ApplyWaterTuning();
	void StartFlow();
	void UpdateFlow(float deltaTime);
	void UpdateCamera(float deltaTime);
	void UpdateChoose();
	void SkipToChoose();
	void Confirm(Option option);
	void DrawOptions();
	/// <summary>選択肢の文字の左上と幅・高さ（UI 座標）。描画とマウスの当たりで同じ並びを使う</summary>
	struct OptionRect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};
	void LayoutOptions(std::vector<OptionRect>& rects) const;
	/// <summary>その位置にある選択肢の番号（無ければ -1）。当たりは文字の四角に hitPadding を足した範囲</summary>
	int HitOption(const Vector2& position) const;
	const char* OptionLabel(Option option) const;
	void EmitCursorRing();

	bool LoadTuning();
	void SaveTuning() const;
	void OnImGui();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<EtherVessel> vessel_;
	std::unique_ptr<WaterSurface> water_;
	std::string iblPath_;
	// 空を描かない画面なので、画面いっぱいの板でシーン RT を不透明な色に塗る（既定のクリアは α=0 で、Debug のビューポートに
	// ImGui の背景が透ける。ClearRenderTargetView で別の色に塗ると作成時のクリア値と違うという D3D12 の警告で止まる）
	std::unique_ptr<SpriteInstance> backgroundSprite_;

	std::vector<Option> options_;
	int selectedIndex_ = 0;
	bool hasBossCheckpoint_ = false;
	bool wasTransitioning_ = true;   // 前のフレームにトランジション中だった

	// 流れ
	Step step_ = Step::Emerge;
	float stepTime_ = 0.0f;
	bool tickFired_ = false;
	float optionsAlpha_ = 0.0f;
	EtherState::DeathResult death_{};
	float fillBefore_ = 0.6f;
	float fillAfter_ = 0.6f;
	float difficultyBefore01_ = 0.5f;
	float difficultyAfter01_ = 0.5f;
	float returnFillFrom_ = 0.0f;
	Option chosen_ = Option::Restart;

	// ----- 調整値（GameOver.json） -----
	float background_[3] = { 0.01f, 0.012f, 0.02f };
	// カメラ：トランジション（着水の波紋）の間は液面のすぐ上（近い）に置き、明けたら引いて器を見せる（2026-10-24 ユーザー）。
	// 位置は器の原点から、回転は度（Rx·Ry の順。Y=180 で -Z を向く）
	Vector3 cameraNearPos_{ 0.0f, 2.979f, 0.483f };
	Vector3 cameraNearRotDeg_{ 15.0f, 180.0f, 0.0f };
	Vector3 cameraFarPos_{ 0.0f, 3.801f, 2.801f };
	Vector3 cameraFarRotDeg_{ 15.0f, 180.0f, 0.0f };
	float pullDelay_ = 0.3f;          // トランジションが明けてから引き始めるまで [s]
	float pullSeconds_ = 2.5f;        // 引き切るまで [s]
	float pullTimer_ = 0.0f;
	// 流れの秒（トランジションが明けてから）
	float drainDelay_ = 0.4f;         // カメラが引き終わってから、針・液面・糸が動き出すまで
	float drainSeconds_ = 1.5f;       // 液面が下がり切るまで
	float tickSettleSeconds_ = 0.6f;  // 減り終わってから選択肢が出るまで（揺れが収まる間）
	float optionsFadeSeconds_ = 0.6f;
	float optionsFadeOutSeconds_ = 0.3f;
	float riseSeconds_ = 1.2f;        // 再挑戦：光の柱が昇ってからシーンを切り替えるまで
	float riseGlowBoost_ = 4.0f;      // 再挑戦：光の強さの倍率（最後に届く値）
	float particleStopRatio_ = 0.6f; // 減っている途中のこの割合で粒を止める（粒の寿命の分だけ早く）
	float returnSeconds_ = 1.8f;      // 引き上げる：液面が 0 まで下がる秒
	float returnTailSeconds_ = 0.5f;  // 下がり切ってからシーンを切り替えるまで
	// 水面（器の足元）
	float waterHeight_ = 0.03f;
	float waterDepth_ = 0.6f;
	Vector3 waterColor_{ 0.01f, 0.03f, 0.05f };
	float waterSkyIntensity_ = 0.35f;
	float waterRingAmplitude_ = 0.004f;   // 待機中のさざ波
	float waterRingInterval_ = 5.0f;
	float cursorRingAmplitude_ = 0.012f;  // カーソルを動かしたときの波紋
	float cursorRingWavelength_ = 0.25f;
	float cursorRingRadius_ = 6.0f;
	float arrivalRingAmplitude_ = 0.008f; // 支柱を下りた光が泉に着いたときの波紋
	// 選択肢の文字（外の世界の画面なので白に金の縁。§4.6）
	float optionsYRatio_ = 0.86f;
	float optionsGap_ = 140.0f;       // 選択肢どうしのすき間 [px]
	float optionsScale_ = 2.0f;
	float optionsRisePixels_ = 14.0f; // ふわっと出るときに下から上がる量
	float outlineGold_[3] = { 1.0f, 0.78f, 0.34f };
	float glowIntensity_ = 0.6f;      // 選んだ物の下の金の線の発光（0 で光らせない）
	float underlineOffset_ = 6.0f;    // 文字の下から金の線まで [px]
	float underlineThickness_ = 2.0f;
	float hitPadding_ = 24.0f;        // マウスの当たりを文字の四角から広げる量 [px]
	// 選択肢の後ろの暗い帯
	float bandColor_[3] = { 0.0f, 0.01f, 0.03f };
	float bandAlpha_ = 0.6f;
	float bandHeight_ = 90.0f;        // [px]
	float bandEdgeFade_ = 0.3f;       // 左右の端からぼかす幅（画面の幅に対して）
};
