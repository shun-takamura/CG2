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
class GameSession;
struct CraftCost;
struct UIPointer;

/// <summary>
/// 機体設定（装備画面。20_Loadout.md）の UI と入力。コクピットと同じ緑のネオンで、文字と塗りつぶしの四角だけで描く（TextRenderer）。
///  左上：メイン武器 / 盾 / 飛行ユニットのタブ、その下に選んでいるタブの一覧（付けている物に E）
///  右上：フレームの合計（攻撃力・HP 上限・移動速度）。カーソルの物に替えたときの値も並べる
///  右下：カーソルの物の性能と説明文。飛行ユニットは段階（Lv1〜3）を左右で選べる。
///        下の端に「作成／強化に必要」な素材とエーテル（22_ItemDrop.md。展示用で、表示するだけ）
/// 操作：タブ＝LB/RB・Q/E・クリック、一覧＝上下・W/S・カーソルを合わせる、付ける＝A・Enter・Space・クリック、
///       段階＝左右・A/D・◀ ▶ のクリック、戻る＝B・Esc・BackSpace・「戻る」のクリック。
/// 付けるとすぐ LoadoutState を書き換える（モデルの入れ替えは持ち主がノイズの山で行う）。
/// 調整値は Hub.json の "loadoutScreen"（飛行ユニットの表示名・技の名前・説明文もここ。一覧の口が無いため）。
/// </summary>
class LoadoutScreen {
public:
	/// <summary>シーンをまたいで残るプレイの状態を設定する（作った直後、Initialize より前に HubScene が呼ぶ）</summary>
	/// <param name="gameSession">Game が持つプレイの状態（参照のみ）</param>
	void SetGameSession(GameSession* gameSession) { gameSession_ = gameSession; }

	enum class Result {
		None,
		Equipped,       // 付け替えた（モデルが変わる）
		LevelChanged,   // 飛行ユニットの段階を変えた（モデルは変わらない）
		Back,           // 戻る
	};

	/// <summary>調整値を既定にする</summary>
	LoadoutScreen();
	/// <summary>調整値（配置・色・飛行ユニットの表示の文字）を既定に戻す</summary>
	void ResetToDefaults();
	/// <summary>開くとき：タブはメイン武器、カーソルは付けている物へ</summary>
	void Open();

	/// <summary>入力を読む（inputEnabled の間だけ）。タブ・一覧・段階・付ける・戻るを受ける</summary>
	/// <param name="input">入力（nullptr なら読まない）</param>
	/// <param name="pointer">マウスの当たり</param>
	/// <param name="screen">画面の大きさ [px]（戻るボタンの位置に使う）</param>
	/// <param name="inputEnabled">入力を受け付けるか</param>
	/// <returns>付け替えた・段階を変えた・戻る・何もなし（None）</returns>
	Result Update(InputManager* input, const UIPointer& pointer, const Vector2& screen, bool inputEnabled);
	/// <summary>マウスが UI の枠の上にあるかを返す（枠の外のドラッグは機体を回す）</summary>
	/// <param name="position">マウスの位置 [px]</param>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <returns>左右の枠か戻るボタンの上なら true</returns>
	bool IsOverPanel(const Vector2& position, const Vector2& screen) const;
	/// <summary>選んでいるタブを返す（カメラの位置と付け替えのノイズの部分に使う）</summary>
	/// <returns>0 メイン武器 / 1 盾 / 2 飛行ユニット</returns>
	int GetTab() const { return tab_; }

	/// <summary>背景（神殿を消した後の暗い色・中央の輪・足元の輪）を積む。3D より先に積んで Flush する</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="camera">足元の輪を投影するカメラ</param>
	/// <param name="floorCenter">足元の輪の中心（ワールド）</param>
	/// <param name="radius">足元の輪の半径 [m]</param>
	/// <param name="time">輪を回す経過 [秒]</param>
	void DrawBackdrop(const Vector2& screen, const Camera& camera, const Vector3& floorCenter, float radius, float time);
	/// <summary>UI を積む（Flush は呼び出し側）</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="uiTime">出始めてからの秒 [秒]（負は描かない）</param>
	/// <param name="collapse">戻るときに潰れる進み 0〜1</param>
	/// <param name="unit">飛行ユニット（値を読むため）</param>
	/// <param name="glow">false なら発光の層には積まない</param>
	void Draw(const Vector2& screen, float uiTime, float collapse, const FlightUnitPart* unit, bool glow);

	/// <summary>調整値（Hub.json の "loadoutScreen"）を読む</summary>
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
	enum Tab : int { kTabMain = 0, kTabShield, kTabUnit, kTabCount };

	struct Rect {
		float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
		/// <summary>点が四角の中にあるかを返す</summary>
		/// <param name="p">点 [px]</param>
		/// <returns>中（縁を含む）なら true</returns>
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
	/// <summary>タブの一覧の数を返す</summary>
	/// <param name="tab">タブ</param>
	/// <returns>数（範囲外のタブは 0）</returns>
	int ItemCount(int tab) const;
	/// <summary>タブの中で付けている物の番号を返す</summary>
	/// <param name="tab">タブ</param>
	/// <returns>番号（範囲外のタブは -1）</returns>
	int EquippedIndex(int tab) const;
	/// <summary>一覧の物の表示名を返す</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <returns>表示名（無ければ空）</returns>
	std::string ItemName(int tab, int index) const;
	/// <summary>一覧の物の一行の要約（攻撃力とチャージ / HP 上限と軽減 / RB の技）を返す</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <param name="unit">飛行ユニット（今は使わない）</param>
	/// <returns>要約（無ければ空）</returns>
	std::string ItemSummary(int tab, int index, const FlightUnitPart* unit) const;
	/// <summary>飛行ユニットのタブの一覧の番号を系統にする</summary>
	/// <param name="index">一覧の番号</param>
	/// <returns>系統</returns>
	FlightUnitKind UnitKindAt(int index) const { return static_cast<FlightUnitKind>(index); }
	/// <summary>一覧の物を付ける（LoadoutState を書き換える）</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <returns>付け替えたら true（付けている物・無い物なら false）</returns>
	bool Equip(int tab, int index);
	/// <summary>飛行ユニットの段階を変える（初期の羽根は変えない）</summary>
	/// <param name="index">一覧の番号</param>
	/// <param name="delta">変える量（+1 / -1）</param>
	/// <returns>段階が変わったら true</returns>
	bool ChangeLevel(int index, int delta);

	// 値
	/// <summary>フレームの合計（攻撃力・HP 上限・移動速度）をゲームと同じ計算で求める</summary>
	/// <param name="tab">替えるタブ</param>
	/// <param name="index">替える物の番号（負なら付けている物のまま）</param>
	/// <param name="unit">飛行ユニット（系統ごとの補正を読む）</param>
	/// <returns>合計</returns>
	FrameStats ComputeFrame(int tab, int index, const FlightUnitPart* unit) const;
	/// <summary>一覧の物の性能の行を作る</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <param name="unit">飛行ユニット（段階ごとの値を読む）</param>
	/// <returns>見出しと値の行の一覧</returns>
	std::vector<StatLine> ItemStats(int tab, int index, const FlightUnitPart* unit) const;
	/// <summary>一覧の物の説明文を返す</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <returns>説明文（無ければ空）</returns>
	std::string ItemDescription(int tab, int index) const;
	/// <summary>作る・強めるのに要る物を CraftingCatalog から探す（段階の無い物は Lv1 の欄を見る）</summary>
	/// <param name="tab">タブ</param>
	/// <param name="index">一覧の番号</param>
	/// <param name="level">見た段階の受け取り先（飛行ユニットの段階。他は 1）</param>
	/// <returns>要る物。表に無ければ nullptr</returns>
	const CraftCost* ItemCost(int tab, int index, int& level) const;

	// 配置（UI の仮想解像度のピクセル）
	/// <summary>タブの四角を求める（UI の仮想解像度のピクセル。以下の Rect も同じ）</summary>
	/// <param name="tab">タブ</param>
	/// <returns>四角</returns>
	Rect TabRect(int tab) const;
	/// <summary>一覧の行の四角を求める</summary>
	/// <param name="index">一覧の番号</param>
	/// <returns>四角</returns>
	Rect ItemRect(int index) const;
	/// <summary>段階の ◀ ▶ の四角を求める</summary>
	/// <param name="index">一覧の番号</param>
	/// <param name="direction">負で ◀、正で ▶</param>
	/// <returns>四角</returns>
	Rect LevelArrowRect(int index, int direction) const;
	/// <summary>「戻る」ボタンの四角を求める</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <returns>四角</returns>
	Rect BackButtonRect(const Vector2& screen) const;
	/// <summary>左の枠（タブと一覧）の四角を求める</summary>
	/// <returns>四角</returns>
	Rect LeftPanelRect() const;
	/// <summary>右の枠（性能と説明文）の四角を求める</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <returns>四角</returns>
	Rect RightPanelRect(const Vector2& screen) const;

	// 描く
	/// <summary>文字を通常の層と発光の層の両方に積む</summary>
	/// <param name="text">文字列</param>
	/// <param name="pos">位置 [px]</param>
	/// <param name="scale">文字の大きさの倍率</param>
	/// <param name="color">色</param>
	/// <param name="glowStrength">発光の層に積む色の倍率（0 なら光らせない）</param>
	void Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength);
	/// <summary>塗りつぶしの四角を通常の層と発光の層の両方に積む</summary>
	/// <param name="r">四角</param>
	/// <param name="color">色</param>
	/// <param name="glowStrength">発光の層に積む色の倍率（0 なら光らせない）</param>
	void Rect4(const Rect& r, const Vector4& color, float glowStrength);
	/// <summary>四角の枠線（4 辺）を積む</summary>
	/// <param name="r">四角</param>
	/// <param name="color">色</param>
	/// <param name="thickness">線の太さ [px]</param>
	void Frame(const Rect& r, const Vector4& color, float thickness);
	/// <summary>◀ ▶ を四角を重ねた三角で積む</summary>
	/// <param name="center">中心 [px]</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="right">true で ▶、false で ◀</param>
	/// <param name="color">色</param>
	/// <param name="glowStrength">発光の層に積む色の倍率</param>
	void Arrow(const Vector2& center, float size, bool right, const Vector4& color, float glowStrength);
	/// <summary>左上のタブと一覧を積む</summary>
	/// <param name="appear">出 0〜1</param>
	/// <param name="unit">飛行ユニット</param>
	void DrawLeft(float appear, const FlightUnitPart* unit);
	/// <summary>右のフレームの合計・カーソルの物の性能と説明文・作成／強化に必要な物を積む</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="appear">出 0〜1</param>
	/// <param name="unit">飛行ユニット</param>
	void DrawRight(const Vector2& screen, float appear, const FlightUnitPart* unit);
	/// <summary>下の端の「戻る」ボタンと操作の案内を積む</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="appear">出 0〜1</param>
	void DrawFooter(const Vector2& screen, float appear);
	/// <summary>右の枠の下の端に「作成／強化に必要」を積む</summary>
	/// <param name="panel">右の枠の四角</param>
	/// <param name="appear">出 0〜1</param>
	/// <param name="measureOnly">true なら描かずに高さだけ求める</param>
	/// <returns>描いた（描く）高さ [px]。要る物が無ければ 0</returns>
	float DrawCost(const Rect& panel, float appear, bool measureOnly);
	/// <summary>幅に収まるように UTF-8 の文字ごとに折り返す</summary>
	/// <param name="text">文字列</param>
	/// <param name="scale">文字の大きさの倍率</param>
	/// <param name="maxWidth">1 行の最大の幅 [px]</param>
	/// <returns>行の一覧</returns>
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
