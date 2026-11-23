#pragma once
#include "Vector2.h"
#include "Vector4.h"
#include "VerticalMenu.h"
#include <string_view>

class InputActionMap;
class JsonValue;
struct UIPointer;

/// <summary>
/// コクピットの緑のネオンの UI（ステージ選択。18_HubTransitions.md ①の IN の 5〜6、②の OUT）。
/// 文字と塗りつぶしの四角だけで描く（TextRenderer）。光らせる所は発光の層にも同じ物を積む。
///  - 背景：画面の上下の枠の線・中央の円の計器・飾りの文字（外が透けて見える上に重ねる）
///  - メニュー：チュートリアル / ステージ1 / 機体設定 / HUB に戻る（チュートリアルはステージが無いうちは選べない。機体設定は 20_Loadout.md）
///  - シャットダウン：ブラウン管が切れるように縦に潰れて線になり、点になって消える
/// 時間軸（出る・潰れる）は CockpitReveal が決め、ここは渡された値で描くだけ。調整値は Hub.json の "cockpitConsole"。
/// </summary>
class CockpitConsole {
public:
	enum class Result {
		None,
		Stage1,
		Loadout,     // 機体設定（装備画面）
		BackToHub,   // 「HUB に戻る」かキャンセル
	};

	/// <summary>調整値を既定にする</summary>
	CockpitConsole();
	/// <summary>調整値（色・大きさ・メニューの位置など）を既定に戻す</summary>
	void ResetToDefaults();
	/// <summary>メニューの項目を作る（チュートリアルはステージができるまで選べない）</summary>
	void Initialize();

	/// <summary>メニューを置き直し、inputEnabled の間だけ入力を読む</summary>
	/// <param name="actions">入力アクション（nullptr なら読まない）</param>
	/// <param name="pointer">マウスの当たり</param>
	/// <param name="screenSize">画面の大きさ [px]（メニューの位置に使う）</param>
	/// <param name="inputEnabled">入力を受け付けるか</param>
	/// <returns>決定した項目（キャンセルは BackToHub）。何も決まらなければ None</returns>
	Result Update(InputActionMap* actions, const UIPointer* pointer, const Vector2& screenSize, bool inputEnabled);

	/// <summary>カーソルを item の行へ動かす（デモモードの自動選択）</summary>
	/// <param name="item">Stage1 / Loadout / BackToHub</param>
	/// <param name="playSound">true ならカーソルの音を鳴らす</param>
	void SelectItem(Result item, bool playSound);

	/// <summary>背景とメニューを TextRenderer に積む（Flush は呼び出し側）</summary>
	/// <param name="screenSize">画面の大きさ [px]</param>
	/// <param name="backgroundAlpha">背景 UI の出 0〜1（CockpitReveal::State の値）</param>
	/// <param name="menuTime">メニューが出始めてからの秒 [秒]（負はまだ。CockpitReveal::State の値）</param>
	/// <param name="collapse">シャットダウンで潰れる進み 0〜1（CockpitReveal::State の値）</param>
	/// <param name="time">回る計器用の経過 [秒]</param>
	/// <param name="glow">false なら発光の層には積まない（トランジション中など）</param>
	void Draw(const Vector2& screenSize, float backgroundAlpha, float menuTime, float collapse, float time, bool glow);

	/// <summary>調整値（Hub.json の "cockpitConsole"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>文字を通常の層と発光の層の両方に積む</summary>
	/// <param name="text">文字列</param>
	/// <param name="pos">位置 [px]</param>
	/// <param name="scale">文字の大きさの倍率</param>
	/// <param name="color">色</param>
	/// <param name="glowStrength">発光の層に積む色の倍率（0 なら光らせない）</param>
	void Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength);
	/// <summary>塗りつぶしの四角を通常の層と発光の層の両方に積む</summary>
	/// <param name="pos">左上 [px]</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="color">色</param>
	/// <param name="glowStrength">発光の層に積む色の倍率（0 なら光らせない）</param>
	void Rect(const Vector2& pos, const Vector2& size, const Vector4& color, float glowStrength);

	/// <summary>背景（上下の枠の線・中央の円の計器・飾りの文字）を積む</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="alpha">出 0〜1</param>
	/// <param name="time">回る計器用の経過 [秒]</param>
	void DrawBackground(const Vector2& screen, float alpha, float time);
	/// <summary>メニューを積む</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	/// <param name="menuTime">メニューが出始めてからの秒 [秒]</param>
	void DrawMenu(const Vector2& screen, float menuTime);
	/// <summary>メニューの大きさ・行の高さ・位置を画面に合わせて設定する</summary>
	/// <param name="screen">画面の大きさ [px]</param>
	void PlaceMenu(const Vector2& screen);

	VerticalMenu menu_;
	bool glow_ = true;
	float brightness_ = 1.0f;   // シャットダウンで潰れる間は明るくする

	// 調整値
	Vector4 color_ = { 0.35f, 1.0f, 0.6f, 1.0f };        // 緑のネオン
	Vector4 dimColor_ = { 0.12f, 0.4f, 0.22f, 1.0f };    // 暗い緑（選べない項目・飾り）
	float textGlow_ = 1.6f;          // 文字の発光の強さ
	float lineGlow_ = 1.2f;          // 線の発光の強さ
	float menuCenterY_ = 0.54f;      // メニューの縦の中心（画面の高さに対する割合）
	float menuScale_ = 1.4f;
	float menuLineHeight_ = 64.0f;
	float panelWidth_ = 0.34f;       // メニューの枠の幅（画面の幅に対する割合）
	float itemStagger_ = 0.09f;      // 項目が 1 行ずつ出る間隔
	float ringRadius_ = 0.36f;       // 中央の円の計器の半径（画面の高さに対する割合）
	float ringSpeed_ = 0.15f;        // 回る速さ [rad/s]
};
