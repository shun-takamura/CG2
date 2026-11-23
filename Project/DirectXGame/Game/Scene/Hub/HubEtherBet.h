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
class GameSession;
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
	/// <summary>シーンをまたいで残るプレイの状態を設定する（作った直後、Initialize より前に HubScene が呼ぶ）</summary>
	/// <param name="gameSession">Game が持つプレイの状態（参照のみ）</param>
	void SetGameSession(GameSession* gameSession) { gameSession_ = gameSession; }

	enum class Result {
		None,
		Confirmed,   // 賭けた（EtherState::PlaceBet 済み）。演出が終わってから返す
		Canceled,    // 戻る（賭けない）
	};

	HubEtherBet();
	~HubEtherBet();

	/// <summary>器の間（EtherVesselRoom）を用意し、GameOver.json の器・水面の調整値を読む</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="spriteManager">スプライトの管理</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager, SpriteManager* spriteManager);
	/// <summary>器の間の終了処理をする</summary>
	void Finalize();

	/// <summary>器の間に入る（黒フェードで覆われている間に呼ぶ）。値は前回の純度から（払える範囲に収める）</summary>
	void Begin();
	/// <summary>入力で純度を動かす・決定する・戻るを受け、器と UI を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="input">入力</param>
	/// <param name="inputEnabled">false の間は入力を読まない（フェード中）</param>
	/// <param name="attract">デモモード（少し待って今の値で決める）</param>
	/// <returns>決定（演出が終わってから）・戻る・まだ（None）</returns>
	Result Update(float deltaTime, InputManager* input, bool inputEnabled, bool attract);
	/// <summary>器の間と UI を描く</summary>
	void Draw();
	/// <summary>
	/// 右上の所持エーテルの枠（斜めの暗い枠・しずくの印・金の縁の数字）を積む。Flush は呼び出し側。
	/// 賭ける画面と機体設定（HubScene）で同じ物を出す
	/// </summary>
	/// <param name="alpha">不透明度 0〜1</param>
	void DrawHandPanel(float alpha) const;
	/// <summary>発光パスで器の間を描く</summary>
	void DrawBloom();
	/// <summary>器の間のカメラを返す</summary>
	/// <returns>カメラ。Initialize 前は nullptr</returns>
	Camera* GetCamera() const;

	/// <summary>調整値（Hub.json の "etherBet"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	// シーンをまたいで残るプレイの状態（Game が持つ。参照のみ）
	GameSession* gameSession_ = nullptr;
	// 0.1 刻みの数 1 つあたりの純度
	static constexpr float kValuePerTenth = 0.1f;

	enum class Step {
		Adjust,
		Confirming,
		Finished,
	};

	/// <summary>値を 0.1 単位で動かし（払える範囲に収める）、上げるなら支柱の光を昇らせ・下げるなら下ろす</summary>
	/// <param name="tenths">動かす量（0.1 単位。負で下げる）</param>
	/// <returns>動いたら true</returns>
	bool Step10(int tenths);
	/// <summary>今の純度で賭けて（EtherState::PlaceBet）、決定の演出を始める</summary>
	void Confirm();
	/// <summary>針と光を値へなめらかに寄せ、液面の量を消費に合わせる</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void UpdateVessel(float deltaTime);
	/// <summary>所持エーテル・純度の札・下の帯（◀ 使用するエーテル ▶）を積む</summary>
	void DrawUi();
	/// <summary>しずくの印（下が円、上へすぼまって尖る）を 1px の横線を重ねて積む</summary>
	/// <param name="center">中心 [px]（UI 座標）</param>
	/// <param name="height">高さ [px]</param>
	/// <param name="alpha">不透明度 0〜1</param>
	void DrawDroplet(const Vector2& center, float height, float alpha) const;
	/// <summary>◀ ▶ を四角を重ねた三角で積む（フォントに無いかもしれないため）</summary>
	/// <param name="center">中心 [px]（UI 座標）</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="right">true で ▶、false で ◀</param>
	/// <param name="alpha">不透明度 0〜1</param>
	/// <param name="highlighted">マウスが重なっていれば true（明るくする）</param>
	void DrawArrow(const Vector2& center, float size, bool right, float alpha, bool highlighted) const;
	/// <summary>見やすいように後ろに暗い帯（左右の端と上下はぼかす）を積む</summary>
	/// <param name="centerY">帯の中心の高さ [px]（UI 座標）</param>
	/// <param name="height">帯の高さ [px]</param>
	/// <param name="alpha">不透明度 0〜1</param>
	void DrawBand(float centerY, float height, float alpha) const;
	// 下の帯の ◀ ▶ と中央（使用するエーテル）の当たり。UI 座標
	struct Rect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		/// <summary>点が四角の中にあるかを返す</summary>
		/// <param name="p">点（UI 座標）</param>
		/// <returns>中（縁を含む）なら true</returns>
		bool Contains(const Vector2& p) const { return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height; }
	};
	/// <summary>下の帯の ◀ ▶ と中央（使用するエーテル）の当たりの四角を求める（UI 座標）</summary>
	/// <param name="left">◀ の当たりの受け取り先</param>
	/// <param name="center">中央の当たりの受け取り先</param>
	/// <param name="right">▶ の当たりの受け取り先</param>
	void LayoutBand(Rect& left, Rect& center, Rect& right) const;
	/// <summary>今の純度を返す</summary>
	/// <returns>純度 1.0〜9.0</returns>
	float GetValue() const { return static_cast<float>(valueTenths_) * kValuePerTenth; }

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
