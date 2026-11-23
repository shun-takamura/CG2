#pragma once
#include "GameScene.h"
#include "Vector2.h"
#include "Vector4.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class InputActionMap;
class SpriteInstance;
struct LootItemDef;
struct UIPointer;

/// <summary>
/// リザルトシーン（23_StageClear.md §2）。アイスボーンのクエストクリア画面の配置。
///   背景：撃破の瞬間の 1 枚（StageClearRecord のキャプチャ）。無いときは仮の背景（暗い色＋色の帯）。ゆっくり寄る
///   左上：ステージ名・純度・クリアタイム・スコア・撃破数、その下に獲得エーテル
///   右上：所持エーテル（EtherState。無限の扱いの間は 99,999。売ると「+N」が出る）
///   右のパネル：ステージ報酬のマス → 部位破壊報酬のマス → 選んだ素材の説明 →「ステージ選択へ」
///   下：操作の案内。キャンセルで「背景を見る」（UI を全部隠す切り替え）
/// マスは十字キー（上下左右）かマウスで選ぶ。マスで決定 → 売る数の小窓（↑↓ で数・←→ で［売る］［やめる］）→ 決定で売ってエーテルにする。
/// 「ステージ選択へ」（決定・クリック）か Esc／Start でステージ選択へ。
/// 数え上げの途中の決定・クリックは、まず最後まで飛ばす。
/// </summary>
class ResultScene : public GameScene {
public:
	ResultScene();
	~ResultScene() override;

	/// <summary>クリアの記録・スコア・獲得エーテルを写し、報酬のマスと撃破の瞬間の背景を用意する</summary>
	void Initialize() override;
	/// <summary>背景のスプライトを捨てる</summary>
	void Finalize() override;
	/// <summary>数え上げ・背景を見る切り替え・マスの選択・売る小窓・ステージ選択への入力を進める（トランジション中は止める）</summary>
	void Update() override;
	/// <summary>背景 → 左上の記録・獲得エーテル → 右上の所持 → 右のパネル → 操作の案内の順に描く</summary>
	void Draw() override;

	/// <summary>シーンのカメラを取得</summary>
	/// <returns>シーンのカメラ</returns>
	Camera* GetCamera() override;

private:
	// 報酬のマス 1 つ
	struct RewardSlot {
		const LootItemDef* item = nullptr;
		int count = 0;
		float startTime = 0.0f;   // 数え始める時刻 [s]
	};

	// 右のパネルの並び（slideX は出てくるときの横のずれ）
	struct PanelLayout {
		float panelX = 0.0f;
		float innerX = 0.0f;
		float innerW = 0.0f;
		float stageHeaderY = 0.0f;
		float stageGridY = 0.0f;
		int stageRows = 0;
		float partHeaderY = 0.0f;
		float partGridY = 0.0f;
		int partRows = 0;
		float descY = 0.0f;
		float descH = 0.0f;
		float buttonY = 0.0f;
	};

	struct Rect {
		Vector2 pos{};
		Vector2 size{};
		/// <summary>点が四角の中（縁を含む）にあるか</summary>
		/// <param name="p">調べる点（UI 座標）</param>
		/// <returns>中にあれば true</returns>
		bool Contains(const Vector2& p) const {
			return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
		}
	};

	/// <summary>手に入れた素材だけをステージ報酬・部位破壊報酬のマスにし、数え始める時刻をずらして決める</summary>
	void BuildRewards();
	/// <summary>撃破の瞬間のキャプチャがあれば背景のスプライトを作る（無ければ仮の背景）</summary>
	void SetupCaptureSprite();
	/// <summary>数え上げ（獲得エーテル・全部のマス）が終わる時刻</summary>
	/// <returns>出てからの時刻 [秒]</returns>
	float CountUpEndTime() const;
	/// <summary>数え上げの途中か</summary>
	/// <returns>途中なら true</returns>
	bool IsCounting() const { return time_ < CountUpEndTime(); }

	// ----- 選ぶ -----
	/// <summary>選べるマス（中身のあるマス）の数。ステージ報酬 → 部位破壊報酬の順。この番号が「ステージ選択へ」</summary>
	/// <returns>選べるマスの数</returns>
	int SelectableCount() const;
	/// <summary>「ステージ選択へ」の選択番号</summary>
	/// <returns>SelectableCount と同じ値</returns>
	int ButtonIndex() const { return SelectableCount(); }
	/// <summary>選択番号のマスを取得</summary>
	/// <param name="index">選択番号</param>
	/// <returns>マス。範囲外（「ステージ選択へ」も）なら nullptr</returns>
	RewardSlot* SelectableAt(int index);
	/// <summary>選択番号のマスを取得（読むだけ）</summary>
	/// <param name="index">選択番号</param>
	/// <returns>マス。範囲外（「ステージ選択へ」も）なら nullptr</returns>
	const RewardSlot* SelectableAt(int index) const;
	/// <summary>選択番号が部位破壊報酬のマスか</summary>
	/// <param name="index">選択番号</param>
	/// <returns>部位破壊報酬なら true</returns>
	bool IsPartBreakIndex(int index) const;
	/// <summary>十字キーの上下：隣の段の、いちばん近い列のマスへ（空の段は飛ばす。いちばん下は「ステージ選択へ」）</summary>
	/// <param name="dir">-1 で上、+1 で下</param>
	void MoveVertical(int dir);
	/// <summary>十字キーの左右：マスの間を順に動く（端で折り返す。「ステージ選択へ」の上では動かない）</summary>
	/// <param name="dir">-1 で左、+1 で右</param>
	void MoveHorizontal(int dir);
	/// <summary>選ぶマスを変える（同じなら何もしない）</summary>
	/// <param name="index">選択番号（0〜ButtonIndex に収める）</param>
	/// <param name="playSound">カーソルの音を鳴らすなら true</param>
	void Select(int index, bool playSound);

	/// <summary>右のパネルの並びを求める</summary>
	/// <param name="slideX">出てくるときの横のずれ [px]</param>
	/// <returns>パネルの並び</returns>
	PanelLayout MakeLayout(float slideX) const;
	/// <summary>マスの四角</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="selectableIndex">マスの選択番号</param>
	/// <returns>マスの四角（UI 座標）</returns>
	Rect SlotRect(const PanelLayout& layout, int selectableIndex) const;
	/// <summary>「ステージ選択へ」の四角</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <returns>ボタンの四角（UI 座標）</returns>
	Rect ButtonRect(const PanelLayout& layout) const;

	// ----- 売る -----
	/// <summary>選んでいるマスの売る数の小窓を開く（売れない素材なら音だけ）</summary>
	void OpenSell();
	/// <summary>売る小窓の入力（↑↓・▲▼で数、←→・マウスで［売る］［やめる］、決定・キャンセル）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="pointer">マウスの状態（UI 座標）</param>
	void UpdateSell(InputActionMap* actions, const UIPointer& pointer);
	/// <summary>選んだ数を売って所持エーテルへ足す（この画面の数だけ減らし、売りきったマスは詰める）</summary>
	void ExecuteSell();
	/// <summary>売る数の ▲（dir > 0）・▼（dir < 0）の四角</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="dir">+1 で ▲、-1 で ▼</param>
	/// <returns>矢印の四角（UI 座標）</returns>
	Rect SellArrowRect(const PanelLayout& layout, int dir) const;
	/// <summary>小窓の［売る］・［やめる］の四角</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="confirm">true で［売る］、false で［やめる］</param>
	/// <returns>ボタンの四角（UI 座標）</returns>
	Rect SellButtonRect(const PanelLayout& layout, bool confirm) const;

	/// <summary>決定の音を鳴らしてステージ選択（コクピットの起動から）へ移る</summary>
	void GoToStageSelect();

	// ----- 描く -----
	/// <summary>撃破の瞬間の 1 枚を画面の縦横比で切り抜いてゆっくり寄せて描き、文字を読みやすくする暗がりを重ねる（無ければ仮の背景）</summary>
	/// <param name="screenW">画面の幅 [px]</param>
	/// <param name="screenH">画面の高さ [px]</param>
	void DrawBackground(float screenW, float screenH);
	/// <summary>仮の背景（暗い紺の縦グラデーションと、ゆっくり揺れる光の帯）を描く</summary>
	/// <param name="screenW">画面の幅 [px]</param>
	/// <param name="screenH">画面の高さ [px]</param>
	void DrawFallbackBackground(float screenW, float screenH);
	/// <summary>左上の記録（ステージ名・純度・クリアタイム・スコア・撃破数）を滑り込ませて描く</summary>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawRecordPanel(float uiAlpha);
	/// <summary>記録の下に、このステージで手に入れたエーテルを数え上げて描く</summary>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawEarnedEther(float uiAlpha);
	/// <summary>右上の所持エーテル（無限の扱いの間は 99,999）と、売った分の「+N」を描く</summary>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawHandEther(float uiAlpha);
	/// <summary>右のパネル（報酬のマス・説明か売る小窓・「ステージ選択へ」）を描く</summary>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawRewardPanel(float uiAlpha);
	/// <summary>マスを並べる（左上 origin から 6 列、少なくとも minRows 段。足りない分は空のマス）</summary>
	/// <param name="slots">中身のあるマス</param>
	/// <param name="origin">並びの左上（UI 座標）</param>
	/// <param name="minRows">少なくとも並べる段の数</param>
	/// <param name="selectableOffset">この並びの最初のマスの選択番号</param>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawSlotGrid(const std::vector<RewardSlot>& slots, const Vector2& origin, int minRows, int selectableOffset,
		float uiAlpha);
	/// <summary>素材のアイコンを描く（画像がまだ無いので色付きの四角で代わりにする）</summary>
	/// <param name="item">素材</param>
	/// <param name="pos">左上（UI 座標）</param>
	/// <param name="size">一辺 [px]</param>
	/// <param name="alpha">濃さ（0〜1）</param>
	void DrawItemIcon(const LootItemDef& item, const Vector2& pos, float size, float alpha);
	/// <summary>選んだ素材の説明（名前・レア度・数・売値・説明文）を描く</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawDescription(const PanelLayout& layout, float uiAlpha);
	/// <summary>売る数の小窓（説明の欄に重ねる）を描く</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawSellWindow(const PanelLayout& layout, float uiAlpha);
	/// <summary>「ステージ選択へ」のボタンを描く（数え上げの途中は薄く）</summary>
	/// <param name="layout">右のパネルの並び</param>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawStageSelectButton(const PanelLayout& layout, float uiAlpha);
	/// <summary>下の操作の案内を、今の状態（数え上げ・売る・選択・背景を見る）に合わせて描く</summary>
	/// <param name="screenW">画面の幅 [px]</param>
	/// <param name="screenH">画面の高さ [px]</param>
	/// <param name="uiAlpha">UI の濃さ（0〜1）</param>
	void DrawHint(float screenW, float screenH, float uiAlpha);
	/// <summary>四角の枠（4 辺の線）を描く</summary>
	/// <param name="pos">左上（UI 座標）</param>
	/// <param name="size">幅と高さ [px]</param>
	/// <param name="thickness">線の太さ [px]</param>
	/// <param name="color">色</param>
	void DrawFrame(const Vector2& pos, const Vector2& size, float thickness, const Vector4& color);

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<SpriteInstance> captureSprite_;   // 撃破の瞬間の 1 枚（無ければ nullptr＝仮の背景）
	Vector2 captureSize_{};                           // キャプチャの画素数

	// 記録（Initialize で写す）
	std::string stageName_;
	float purity_ = 5.0f;
	float clearTime_ = 0.0f;
	int score_ = 0;
	int killCount_ = 0;
	int earnedEther_ = 0;

	std::vector<RewardSlot> stageSlots_;      // ステージ報酬（中身のあるマスだけ。空のマスは描くときに足す）
	std::vector<RewardSlot> partBreakSlots_;  // 部位破壊報酬
	int selected_ = 0;                        // 選んでいるマス（SelectableAt の番号。ButtonIndex は「ステージ選択へ」）
	int lastColumn_ = 0;                      // 上下で移るときの列（「ステージ選択へ」から上へ戻る列）

	// 売る小窓
	bool sellOpen_ = false;
	int sellAmount_ = 1;
	bool sellFocusCancel_ = false;  // 小窓で選んでいるボタン（false＝売る / true＝やめる）。←→ とマウスで選ぶ
	int soldEtherPopup_ = 0;      // 所持の横に出す「+N」（続けて売ると足していく）
	float soldPopupTime_ = 99.0f; // 「+N」を出してからの時間 [s]

	float time_ = 0.0f;           // 出てからの実時間 [s]
	bool backgroundOnly_ = false; // 「背景を見る」の間は UI を隠す
	float uiAlpha_ = 1.0f;        // UI の濃さ（背景を見る切り替えでなめらかに）
	float backgroundToggleTime_ = 0.0f;  // 背景を見る に切り替えてからの時間 [s]（戻り方の案内を少しだけ出す）
};
