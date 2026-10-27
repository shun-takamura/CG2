#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <memory>
#include <string>

class Camera;
class DirectXCore;
class SRVManager;
class Object3DManager;
class SpriteManager;
class InputManager;
class JsonValue;
class EtherVesselRoom;
struct UIPointer;

/// <summary>
/// ステージの前にエーテルを器に賭けて「純度」（本気の度合い＝難易度）を決める画面（19_GameOverAndDifficulty.md §4.7。2026-10-27）。
/// 場所はゲームオーバー画面の器の間をそのまま使う（EtherVesselRoom）。HubScene がコクピットから黒フェードで連れて来て、
/// 決定したら黒フェードでコクピットに戻ってリフトの降下へ、戻るならステージ選択へ戻る。
///   - 純度は 1.0〜9.0 の 0.1 刻み（文字盤 I〜IX）。左右・A/D で動かし、押し続けると速くなる。マウスは ◀ ▶ のクリック（長押しで連続）、中央のクリックで決定
///   - 針・光・液面は値をなめらかに追う。上げている間は泉から支柱を光が昇り、下げると泉へ下りる（§4.5）
///   - 画面（2026-10-27 ユーザーのスケッチ）：右上に所持エーテル（今は無限＝99,999）、針の先に純度の札、
///     下の黒い帯に使用するエーテル（消費 125(n−1)(n+2)）。決定したら所持の下に「− 消費」が出る
///   - 急かさない・強制しない（§3）：待ち時間で勝手に決まらない（デモモードだけ自動で決める）。戻ればいつでもやめられる
/// 調整値は Hub.json の "etherBet"（器・水面は GameOver.json をそのまま読む）。
/// </summary>
class HubEtherBet {
public:
	enum class Result {
		None,
		Confirmed,   // 賭けた（EtherState::PlaceBet 済み）。演出が終わってから返す
		Canceled,    // 戻る（賭けない）
	};

	HubEtherBet();
	~HubEtherBet();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager, SpriteManager* spriteManager);
	void Finalize();

	/// <summary>器の間に入る（黒フェードで覆われている間に呼ぶ）。値は前回の純度から</summary>
	void Begin();
	/// <summary>inputEnabled=false の間は入力を読まない（フェード中）。attract はデモモード（少し待って今の値で決める）</summary>
	Result Update(float deltaTime, InputManager* input, bool inputEnabled, bool attract);
	void Draw();
	void DrawBloom();
	Camera* GetCamera() const;

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	enum class Step {
		Adjust,
		Confirming,
		Finished,
	};

	/// <summary>値を tenths（0.1 単位）だけ動かす。動いたら true</summary>
	bool Step10(int tenths);
	void Confirm();
	void UpdateVessel(float deltaTime);
	void DrawUi();
	void DrawDroplet(const Vector2& center, float height, float alpha) const;
	void DrawArrow(const Vector2& center, float size, bool right, float alpha, bool highlighted) const;
	void DrawBand(float centerY, float height, float alpha) const;
	// 下の帯の ◀ ▶ と中央（使用するエーテル）の当たり。UI 座標
	struct Rect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		bool Contains(const Vector2& p) const { return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height; }
	};
	void LayoutBand(Rect& left, Rect& center, Rect& right) const;
	float GetValue() const { return static_cast<float>(valueTenths_) * 0.1f; }

	DirectXCore* dxCore_ = nullptr;
	std::unique_ptr<EtherVesselRoom> room_;

	Step step_ = Step::Adjust;
	int valueTenths_ = 50;          // 純度 ×10（10〜90）
	float shownValue_ = 5.0f;       // 針・光・液面が追う値（なめらかに寄せる）
	float uiAlpha_ = 0.0f;
	float stepTime_ = 0.0f;
	int heldDirection_ = 0;         // 押し続けている向き（−1 / +1）
	float holdTime_ = 0.0f;
	float repeatTimer_ = 0.0f;
	float flowTimer_ = 0.0f;        // 最後に値が動いてから（支柱の光を止めるまで）
	int flowDirection_ = 0;
	int hoverArrow_ = 0;            // マウスが乗っている ◀(−1) / ▶(+1)
	float attractTimer_ = 0.0f;
	float attractStepTimer_ = 0.0f;
	float confirmedCost_ = 0.0f;

	// ----- 調整値（Hub.json の "etherBet"） -----
	Vector3 cameraPos_{ 0.0f, 3.801f, 2.801f };     // 器の原点から（既定は GameOver.json の camera.farPos）
	Vector3 cameraRotDeg_{ 15.0f, 180.0f, 0.0f };
	float uiFadeSeconds_ = 0.5f;
	float followSpeed_ = 10.0f;     // 針・光が値へ寄る速さ [1/s]
	float repeatDelay_ = 0.4f;      // 押し続けてから連続で動き始めるまで
	float repeatIntervalStart_ = 0.09f;
	float repeatIntervalEnd_ = 0.02f;
	float repeatAccelSeconds_ = 1.5f;   // 間隔が始め → 終わりへ縮むまで
	float flowHoldSeconds_ = 0.35f;     // 値が止まってから支柱の光を止めるまで
	float confirmSeconds_ = 1.4f;       // 決定してから戻るまで
	float confirmGlowBoost_ = 1.8f;
	// 文字・帯（UI 座標。1600×900 基準）
	float bandYRatio_ = 0.86f;
	float bandHeight_ = 90.0f;
	float bandAlpha_ = 0.6f;
	float bandEdgeFade_ = 0.3f;
	float costScale_ = 2.0f;           // 使用するエーテルの数字の大きさ
	float arrowSpread_ = 230.0f;       // 中央から ◀ ▶ まで [px]
	float arrowSize_ = 26.0f;
	float hintScale_ = 0.6f;
	float handPanelWidth_ = 330.0f;
	float handPanelHeight_ = 64.0f;
	float handPanelMargin_ = 36.0f;
	float handScale_ = 1.4f;
	float tagScale_ = 1.4f;
	float tagOffset_ = 46.0f;          // 針の先から札の中心まで（針の向きへ）[px]
	float outlineGold_[3] = { 1.0f, 0.78f, 0.34f };
};
