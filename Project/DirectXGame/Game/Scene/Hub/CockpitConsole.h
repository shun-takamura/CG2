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
///  - メニュー：チュートリアル / ステージ1 / HUB に戻る（チュートリアルはステージが無いうちは選べない）
///  - シャットダウン：ブラウン管が切れるように縦に潰れて線になり、点になって消える
/// 時間軸（出る・潰れる）は CockpitReveal が決め、ここは渡された値で描くだけ。調整値は Hub.json の "cockpitConsole"。
/// </summary>
class CockpitConsole {
public:
	enum class Result {
		None,
		Stage1,
		BackToHub,   // 「HUB に戻る」かキャンセル
	};

	CockpitConsole();
	void ResetToDefaults();
	void Initialize();

	/// <summary>入力を読む（inputEnabled の間だけ）。screenSize はメニューの位置（マウスの当たり）に使う</summary>
	Result Update(InputActionMap* actions, const UIPointer* pointer, const Vector2& screenSize, bool inputEnabled);

	/// <summary>
	/// 積む（Flush は呼び出し側）。backgroundAlpha / menuTime / collapse は CockpitReveal::State の値、
	/// time は回る計器用の経過秒。glow が false なら発光の層には積まない（トランジション中など）
	/// </summary>
	void Draw(const Vector2& screenSize, float backgroundAlpha, float menuTime, float collapse, float time, bool glow);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	// 通常の層と発光の層の両方に積む（glowStrength 倍した色で光らせる。0 なら光らせない）
	void Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength);
	void Rect(const Vector2& pos, const Vector2& size, const Vector4& color, float glowStrength);

	void DrawBackground(const Vector2& screen, float alpha, float time);
	void DrawMenu(const Vector2& screen, float menuTime);
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
