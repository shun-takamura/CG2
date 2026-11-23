#pragma once
#include "GameScene.h"
#include "Score/EtherState.h"
#include "AttractMode.h"
#include "Vector2.h"
#include "Vector3.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class EtherVessel;
class EtherVesselRoom;

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
/// 器・水面・背景・カメラは EtherVesselRoom（HUB の賭ける画面と共用）。
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

	/// <summary>器の間を用意し、選択肢を決め、死んだ分のエーテルと難易度を入口で黙って減らして流れを始める</summary>
	void Initialize() override;
	/// <summary>器の間を片付ける</summary>
	void Finalize() override;
	/// <summary>流れ・選択・カメラの引き・器の間・純度の札のフェードを進める</summary>
	void Update() override;
	/// <summary>器の間 → エフェクトの粒 → 純度の札 → 選択肢の順に描く</summary>
	void Draw() override;

	/// <summary>器の間のカメラを取得</summary>
	/// <returns>器の間のカメラ（器の間が無ければ nullptr）</returns>
	Camera* GetCamera() override;

	/// <summary>発光を足すか（トランジションの間は羽根・波紋を隠してしまうので光らせない）</summary>
	/// <returns>器の間があり、トランジション中でなければ true</returns>
	bool HasBloomSources() const override;
	/// <summary>器の間の発光する物をブルームのマスクへ描く</summary>
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

	/// <summary>流れを 1 浮かび上がるから始め直す（器を死ぬ前の液面・針に戻す）</summary>
	void StartFlow();
	/// <summary>流れの段（浮かび上がる→減る→収まる→選択肢→再挑戦/引き上げる）を進め、終わればシーンを切り替える</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void UpdateFlow(float deltaTime);
	/// <summary>トランジションが明けたら、液面のすぐ上の近い位置から遠い位置へカメラを引く</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void UpdateCamera(float deltaTime);
	/// <summary>選択肢のキー・パッド・マウスの入力を読む（減る途中の決定は選択肢まで早送り。トランジション明けのフレームは読まない）</summary>
	void UpdateChoose();
	/// <summary>デモモードの自動選択（「もう一度」か、回数を超えたら「引き上げる」）</summary>
	/// <returns>選んだら（カーソルを動かしたフレームも）true（そのフレームは人の入力を読まない）</returns>
	bool UpdateAttract();
	/// <summary>減る・収まるを飛ばして選択肢まで早送りする（液面・針は減った後へ）</summary>
	void SkipToChoose();
	/// <summary>選択肢を決定する（引き上げるならエーテルを戻して泉へ下ろし、再挑戦なら光を強める段へ）</summary>
	/// <param name="option">決定した選択肢</param>
	void Confirm(Option option);
	/// <summary>選択肢の後ろの暗い帯と、選択肢の文字（選んだ物は白に金の縁と下線）を描く</summary>
	void DrawOptions();
	/// <summary>針の先の純度の札。数字は針が今指している値（行き過ぎの間も死ぬ前〜後の範囲に収める）</summary>
	void DrawPurityTag();
	/// <summary>選択肢の文字の左上と幅・高さ（UI 座標）。描画とマウスの当たりで同じ並びを使う</summary>
	struct OptionRect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};
	/// <summary>選択肢を横に中央そろえで並べた時の四角を求める</summary>
	/// <param name="rects">選択肢ごとの四角の書き込み先（options_ と同じ並び。文字を描けない時は空）</param>
	void LayoutOptions(std::vector<OptionRect>& rects) const;
	/// <summary>その位置にある選択肢の番号。当たりは文字の四角に hitPadding を足した範囲</summary>
	/// <param name="position">マウスの位置（UI 座標）</param>
	/// <returns>選択肢の番号（無ければ -1）</returns>
	int HitOption(const Vector2& position) const;
	/// <summary>選択肢の表示名（負けを感じさせる言葉は使わない）</summary>
	/// <param name="option">選択肢</param>
	/// <returns>表示名（チェックポイントが無ければ Restart は「もう一度」）</returns>
	const char* OptionLabel(Option option) const;
	/// <summary>カーソルを動かした合図に、器の液面へ波紋を出す</summary>
	void EmitCursorRing();

	/// <summary>カメラ・流れ・文字の調整値と器の間の調整値を GameOver.json から読む</summary>
	/// <returns>読めたら true（ファイルが無い・読めなければ false で今の値のまま）</returns>
	bool LoadTuning();
	/// <summary>今の調整値を GameOver.json へ書き出す</summary>
	void SaveTuning() const;
	/// <summary>流れの再生・調整値の編集・保存の ImGui 窓（Debug のみ中身あり）</summary>
	void OnImGui();

	// 器の間（器・足元の水面・背景の板・カメラ）
	std::unique_ptr<EtherVesselRoom> room_;
	EtherVessel* vessel_ = nullptr;   // room_ の器（参照のみ）

	std::vector<Option> options_;
	int selectedIndex_ = 0;
	bool hasBossCheckpoint_ = false;
	bool wasTransitioning_ = true;   // 前のフレームにトランジション中だった
	// デモモード：この周の 1 回目は「もう一度」、超えたら「引き上げる」を自動で選ぶ
	AttractMenuTimer attractMenu_;
	bool attractWithdraw_ = false;

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

	// ----- 調整値（GameOver.json。背景・水面・器は EtherVesselRoom） -----
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
	// 針の先の純度の札（賭ける画面と同じ。2026-10-27 ユーザー）。数字は針が今指している値
	float tagScale_ = 1.4f;
	float tagOffset_ = 46.0f;         // 針の先から札の中心まで（針の向きへ）[px]
	float tagFadeSeconds_ = 0.5f;     // トランジションが明けてから札が出るまで
	float tagAlpha_ = 0.0f;
	// 選択肢の後ろの暗い帯
	float bandColor_[3] = { 0.0f, 0.01f, 0.03f };
	float bandAlpha_ = 0.6f;
	float bandHeight_ = 90.0f;        // [px]
	float bandEdgeFade_ = 0.3f;       // 左右の端からぼかす幅（画面の幅に対して）
};
