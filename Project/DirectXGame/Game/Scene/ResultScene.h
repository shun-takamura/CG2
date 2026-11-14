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

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

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
		bool Contains(const Vector2& p) const {
			return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
		}
	};

	void BuildRewards();
	void SetupCaptureSprite();
	float CountUpEndTime() const;
	bool IsCounting() const { return time_ < CountUpEndTime(); }

	// ----- 選ぶ -----
	/// <summary>選べるマス（中身のあるマス）の数。ステージ報酬 → 部位破壊報酬の順。この番号が「ステージ選択へ」</summary>
	int SelectableCount() const;
	int ButtonIndex() const { return SelectableCount(); }
	RewardSlot* SelectableAt(int index);
	const RewardSlot* SelectableAt(int index) const;
	bool IsPartBreakIndex(int index) const;
	/// <summary>十字キーの上下：隣の段の、いちばん近い列のマスへ（空の段は飛ばす。いちばん下は「ステージ選択へ」）</summary>
	void MoveVertical(int dir);
	void MoveHorizontal(int dir);
	void Select(int index, bool playSound);

	PanelLayout MakeLayout(float slideX) const;
	Rect SlotRect(const PanelLayout& layout, int selectableIndex) const;
	Rect ButtonRect(const PanelLayout& layout) const;

	// ----- 売る -----
	void OpenSell();
	void UpdateSell(InputActionMap* actions, const UIPointer& pointer);
	void ExecuteSell();
	Rect SellArrowRect(const PanelLayout& layout, int dir) const;
	Rect SellButtonRect(const PanelLayout& layout, bool confirm) const;

	void GoToStageSelect();

	// ----- 描く -----
	void DrawBackground(float screenW, float screenH);
	void DrawFallbackBackground(float screenW, float screenH);
	void DrawRecordPanel(float uiAlpha);
	void DrawEarnedEther(float uiAlpha);
	void DrawHandEther(float uiAlpha);
	void DrawRewardPanel(float uiAlpha);
	/// <summary>
	/// マスを並べる（左上 origin から 6 列、少なくとも minRows 段。足りない分は空のマス）。
	/// selectableOffset はこの並びの最初のマスの選択番号
	/// </summary>
	void DrawSlotGrid(const std::vector<RewardSlot>& slots, const Vector2& origin, int minRows, int selectableOffset,
		float uiAlpha);
	void DrawItemIcon(const LootItemDef& item, const Vector2& pos, float size, float alpha);
	void DrawDescription(const PanelLayout& layout, float uiAlpha);
	void DrawSellWindow(const PanelLayout& layout, float uiAlpha);
	void DrawStageSelectButton(const PanelLayout& layout, float uiAlpha);
	void DrawHint(float screenW, float screenH, float uiAlpha);
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
