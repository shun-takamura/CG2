#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Score/LoadoutState.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

class Camera;
class FlightUnitPart;
class InputManager;
class JsonValue;
struct UIPointer;

/// <summary>
/// 機体設定（装備画面。20_Loadout.md）の UI と入力。コクピットと同じ緑のネオンで、文字と塗りつぶしの四角だけで描く（TextRenderer）。
///  左上：メイン武器 / 盾 / 飛行ユニットのタブ、その下に選んでいるタブの一覧（付けている物に E）
///  右上：フレームの合計（攻撃力・HP 上限・移動速度）。カーソルの物に替えたときの値も並べる
///  右下：カーソルの物の性能と説明文。飛行ユニットは段階（Lv1〜3）を左右で選べる
/// 操作：タブ＝LB/RB・Q/E・クリック、一覧＝上下・W/S・カーソルを合わせる、付ける＝A・Enter・Space・クリック、
///       段階＝左右・A/D・◀ ▶ のクリック、戻る＝B・Esc・BackSpace・「戻る」のクリック。
/// 付けるとすぐ LoadoutState を書き換える（モデルの入れ替えは持ち主がノイズの山で行う）。
/// 調整値は Hub.json の "loadoutScreen"（飛行ユニットの表示名・技の名前・説明文もここ。一覧の口が無いため）。
/// </summary>
class LoadoutScreen {
public:
	enum class Result {
		None,
		Equipped,       // 付け替えた（モデルが変わる）
		LevelChanged,   // 飛行ユニットの段階を変えた（モデルは変わらない）
		Back,           // 戻る
	};

	LoadoutScreen();
	void ResetToDefaults();
	/// <summary>開くとき：タブはメイン武器、カーソルは付けている物へ</summary>
	void Open();

	/// <summary>入力を読む（inputEnabled の間だけ）。screen は戻るボタンの位置（マウスの当たり）に使う</summary>
	Result Update(InputManager* input, const UIPointer& pointer, const Vector2& screen, bool inputEnabled);
	/// <summary>マウスが UI の枠の上にある（枠の外のドラッグは機体を回す）</summary>
	bool IsOverPanel(const Vector2& position, const Vector2& screen) const;
	/// <summary>選んでいるタブ（0 メイン武器 / 1 盾 / 2 飛行ユニット。カメラの位置と付け替えのノイズの部分に使う）</summary>
	int GetTab() const { return tab_; }

	/// <summary>
	/// 背景（神殿を消した後の暗い色・中央の輪・足元の輪）。3D より先に積んで Flush する。
	/// floorCenter は足元の輪の中心（ワールド）、radius はその半径 [m]
	/// </summary>
	void DrawBackdrop(const Vector2& screen, const Camera& camera, const Vector3& floorCenter, float radius, float time);
	/// <summary>
	/// UI を積む（Flush は呼び出し側）。uiTime は出始めてからの秒（負は描かない）、collapse は戻るときに潰れる 0..1。
	/// unit は飛行ユニットの値を読むため。glow が false なら発光の層には積まない
	/// </summary>
	void Draw(const Vector2& screen, float uiTime, float collapse, const FlightUnitPart* unit, bool glow);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	enum Tab : int { kTabMain = 0, kTabShield, kTabUnit, kTabCount };

	struct Rect {
		float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
		bool Contains(const Vector2& p) const { return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height; }
	};
	// 飛行ユニットの表示の文字（系統ごと。Hub.json で書き換えられる）
	struct UnitText {
		std::string name;
		std::string skillName;     // RB の技（Wings は空）
		std::string description;
	};
	// フレームの合計
	struct FrameStats {
		int attack = 0;
		int maxHp = 0;
		float moveScale = 1.0f;
	};
	struct StatLine {
		std::string label;
		std::string value;
	};

	// 一覧
	int ItemCount(int tab) const;
	int EquippedIndex(int tab) const;
	std::string ItemName(int tab, int index) const;
	std::string ItemSummary(int tab, int index, const FlightUnitPart* unit) const;
	FlightUnitKind UnitKindAt(int index) const { return static_cast<FlightUnitKind>(index); }
	// 付ける（付けている物なら false）
	bool Equip(int tab, int index);
	bool ChangeLevel(int index, int delta);

	// 値
	FrameStats ComputeFrame(int tab, int index, const FlightUnitPart* unit) const;
	std::vector<StatLine> ItemStats(int tab, int index, const FlightUnitPart* unit) const;
	std::string ItemDescription(int tab, int index) const;

	// 配置（UI の仮想解像度のピクセル）
	Rect TabRect(int tab) const;
	Rect ItemRect(int index) const;
	Rect LevelArrowRect(int index, int direction) const;
	Rect BackButtonRect(const Vector2& screen) const;
	Rect LeftPanelRect() const;
	Rect RightPanelRect(const Vector2& screen) const;

	// 描く
	void Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength);
	void Rect4(const Rect& r, const Vector4& color, float glowStrength);
	void Frame(const Rect& r, const Vector4& color, float thickness);
	void Arrow(const Vector2& center, float size, bool right, const Vector4& color, float glowStrength);
	void DrawLeft(float appear, const FlightUnitPart* unit);
	void DrawRight(const Vector2& screen, float appear, const FlightUnitPart* unit);
	void DrawFooter(const Vector2& screen, float appear);
	// 幅に収まるように折り返す（UTF-8 の文字ごと）
	std::vector<std::string> Wrap(std::string_view text, float scale, float maxWidth) const;

	int tab_ = kTabMain;
	std::array<int, kTabCount> cursor_{};
	int hoverArrow_ = 0;          // マウスが段階の ◀(-1) ▶(+1) の上
	bool hoverBack_ = false;
	bool glow_ = true;
	std::array<UnitText, static_cast<size_t>(FlightUnitKind::Count)> unitTexts_;

	// 調整値
	Vector4 color_ = { 0.35f, 1.0f, 0.6f, 1.0f };       // 緑のネオン（コクピットと同じ）
	Vector4 dimColor_ = { 0.12f, 0.4f, 0.22f, 1.0f };
	Vector4 upColor_ = { 0.55f, 1.0f, 0.95f, 1.0f };     // 替えると上がる値
	Vector4 downColor_ = { 1.0f, 0.45f, 0.4f, 1.0f };    // 替えると下がる値
	Vector4 backdropColor_ = { 0.006f, 0.016f, 0.024f, 1.0f };
	float textGlow_ = 1.4f;
	float lineGlow_ = 1.0f;
	float marginX_ = 48.0f;
	float topY_ = 112.0f;
	float leftWidth_ = 430.0f;
	float rightWidth_ = 450.0f;
	float tabHeight_ = 54.0f;
	float itemHeight_ = 86.0f;
	float itemGap_ = 8.0f;
	float panelStagger_ = 0.08f;   // 枠ごとにチラついて出る間隔
	float headerLineRightInset_ = 400.0f;   // 見出しの線の右端を画面の右端からどれだけ手前で止めるか（所持エーテルの枠の分）
};
