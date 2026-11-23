#include "LoadoutScreen.h"
#include "Score/GameSession.h"

#include "CockpitReveal.h"
#include "Camera.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "UIPointer.h"
#include "InputAction.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "Config/GameActions.h"
#include "MathUtility.h"
#include "Components/PrefabManager.h"
#include "FlightUnit/FlightUnitPart.h"
#include "Score/MainWeaponCatalog.h"
#include "Score/ShieldCatalog.h"
#include "Score/CraftingCatalog.h"
#include "Score/LootCatalog.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 割合を百分率にする倍率
	constexpr float kPercentScale = 100.0f;
	// この w 以下はカメラの後ろとみなす
	constexpr float kMinClipW = 1e-4f;
	// UTF-8 の 1 バイト文字の先頭のバイトの上限（これ未満）
	constexpr int kUtf8SingleByteLimit = 0x80;
	// UTF-8 の 2 バイト文字の先頭を見分けるために右へずらす量 [ビット]
	constexpr int kUtf8Lead2Shift = 5;
	// UTF-8 の 2 バイト文字の先頭のビット（110）
	constexpr int kUtf8Lead2Pattern = 0x6;
	// UTF-8 の 3 バイト文字の先頭を見分けるために右へずらす量 [ビット]
	constexpr int kUtf8Lead3Shift = 4;
	// UTF-8 の 3 バイト文字の先頭のビット（1110）
	constexpr int kUtf8Lead3Pattern = 0xE;
	// UTF-8 の 4 バイト文字の先頭を見分けるために右へずらす量 [ビット]
	constexpr int kUtf8Lead4Shift = 3;
	// UTF-8 の 4 バイト文字の先頭のビット（11110）
	constexpr int kUtf8Lead4Pattern = 0x1E;
	// 緑ネオンの色の初期値
	constexpr Vector4 kDefaultColor = { 0.35f, 1.0f, 0.6f, 1.0f };
	// 暗い緑の色の初期値
	constexpr Vector4 kDefaultDimColor = { 0.12f, 0.4f, 0.22f, 1.0f };
	// 値が上がるときの色の初期値
	constexpr Vector4 kDefaultUpColor = { 0.55f, 1.0f, 0.95f, 1.0f };
	// 値が下がるときの色の初期値
	constexpr Vector4 kDefaultDownColor = { 1.0f, 0.45f, 0.4f, 1.0f };
	// 背景の色の初期値
	constexpr Vector4 kDefaultBackdropColor = { 0.006f, 0.016f, 0.024f, 1.0f };
	// 文字の光の強さの初期値
	constexpr float kDefaultTextGlow = 1.4f;
	// 左右の余白の初期値 [px]
	constexpr float kDefaultMarginX = 48.0f;
	// パネルの上端の初期値 [px]
	constexpr float kDefaultTopY = 112.0f;
	// 左のパネルの幅の初期値 [px]
	constexpr float kDefaultLeftWidth = 430.0f;
	// 右のパネルの幅の初期値 [px]
	constexpr float kDefaultRightWidth = 450.0f;
	// タブの高さの初期値 [px]
	constexpr float kDefaultTabHeight = 54.0f;
	// 一覧の 1 行の高さの初期値 [px]
	constexpr float kDefaultItemHeight = 86.0f;
	// 一覧の行どうしのすき間の初期値 [px]
	constexpr float kDefaultItemGap = 8.0f;
	// パネルが順に出る間隔の初期値 [秒]
	constexpr float kDefaultPanelStagger = 0.08f;
	// 見出しの線を右端からどれだけ手前で止めるかの初期値 [px]
	constexpr float kDefaultHeaderLineRightInset = 400.0f;
	// 倍率が 1 からこれ以上離れていれば補正として出す
	constexpr float kScaleEpsilon = 1e-4f;
	// タブどうしのすき間 [px]
	constexpr float kTabGap = 4.0f;
	// 一覧をタブの下からどれだけ下に置くか [px]
	constexpr float kListOffsetY = 36.0f;
	// 段階を下げる ◀ を行の右端からどれだけ左に置くか [px]
	constexpr float kLevelDownArrowOffset = 150.0f;
	// 段階を上げる ▶ を行の右端からどれだけ左に置くか [px]
	constexpr float kLevelUpArrowOffset = 34.0f;
	// 戻るボタンを画面の下端からどれだけ上に置くか [px]
	constexpr float kBackButtonBottomOffset = 78.0f;
	// 戻るボタンの幅 [px]
	constexpr float kBackButtonWidth = 170.0f;
	// 戻るボタンの高さ [px]
	constexpr float kBackButtonHeight = 44.0f;
	// 右のパネルの下の余白 [px]
	constexpr float kRightPanelBottomMargin = 110.0f;
	// 三角の根元を中心からずらす量（大きさに対する割合）
	constexpr float kArrowTipOffsetRatio = 0.25f;
	// 走査線の不透明度
	constexpr float kScanlineAlpha = 0.025f;
	// 走査線の間隔 [px]
	constexpr float kScanlineSpacing = 6.0f;
	// 中央の点の輪の中心の高さ（画面の高さに対する割合）
	constexpr float kRingCenterY = 0.48f;
	// 中央の点の輪の不透明度
	constexpr float kRingAlpha = 0.16f;
	// 中央の点の輪の半径（画面の高さに対する割合）
	constexpr float kRingRadiusRatio = 0.42f;
	// 中央の点の輪の 1 区切りの点の数
	constexpr int kRingDotsPerSegment = 36;
	// 中央の点の輪の 1 区切りのうち描く点の数（残りが切れ目）
	constexpr int kRingDotsShownPerSegment = 32;
	// 中央の点の輪が回る速さ [rad/秒]
	constexpr float kRingSpinSpeed = 0.05f;
	// 中央の点の輪で大きい点を置く間隔 [点]
	constexpr int kRingMajorDotInterval = 12;
	// 中央の点の輪の大きい点の大きさ [px]
	constexpr float kRingMajorDotSize = 4.0f;
	// 中央の点の輪の小さい点の大きさ [px]
	constexpr float kRingMinorDotSize = 2.0f;
	// 足元の輪が回る速さ [rad/秒]
	constexpr float kFloorRingSpinSpeed = 0.1f;
	// 足元の輪（真ん中）の点の数
	constexpr int kFloorRingDots = 160;
	// 足元の輪（真ん中）の不透明度
	constexpr float kFloorRingAlpha = 0.55f;
	// 足元の輪（真ん中）の点の大きさ [px]
	constexpr float kFloorRingDotSize = 3.0f;
	// 足元の内の輪の半径（真ん中の輪に対する割合）
	constexpr float kFloorInnerRingRadiusRatio = 0.78f;
	// 足元の内の輪の点の数
	constexpr int kFloorInnerRingDots = 120;
	// 足元の内の輪の不透明度
	constexpr float kFloorInnerRingAlpha = 0.25f;
	// 足元の内・外の輪の点の大きさ [px]
	constexpr float kFloorSubRingDotSize = 2.0f;
	// 足元の外の輪の半径（真ん中の輪に対する割合）
	constexpr float kFloorOuterRingRadiusRatio = 1.25f;
	// 足元の外の輪の点の数
	constexpr int kFloorOuterRingDots = 200;
	// 足元の外の輪の不透明度
	constexpr float kFloorOuterRingAlpha = 0.12f;
	// 切れる演出の前半（縦に潰れる）が終わる進み（0〜1）
	constexpr float kCollapseSquashEnd = 0.6f;
	// 切れる演出の後半（横線が縮む）の長さ（進み 0〜1）
	constexpr float kCollapseShrinkSpan = 0.4f;
	// 横線が縮み切ったときの横の倍率の下限
	constexpr float kMinCollapseScaleX = 0.002f;
	// 縦に潰れ切ったときの縦の倍率
	constexpr float kCollapsedScaleY = 0.004f;
	// パネルがチカチカして出る長さ [秒]
	constexpr float kPanelFlickerSeconds = 0.2f;
	// 見出し「機体設定」の上端 [px]
	constexpr float kTitleY = 38.0f;
	// 見出し「機体設定」の文字の大きさ
	constexpr float kTitleScale = 0.9f;
	// 上の中央の案内の文字を小さい文字より大きくする量
	constexpr float kMessageScaleBoost = 0.05f;
	// 上の中央の案内の上端 [px]
	constexpr float kMessageY = 50.0f;
	// 上の中央の案内の不透明度
	constexpr float kMessageAlpha = 0.8f;
	// 見出しの下の横の線の高さ [px]
	constexpr float kHeaderLineY = 90.0f;
	// 見出しの下の横の線の太さ [px]
	constexpr float kHeaderLineThickness = 2.0f;
	// 見出しの下の横の線の不透明度
	constexpr float kHeaderLineAlpha = 0.6f;
	// パネルの暗い塗りの赤
	constexpr float kPanelFillRed = 0.01f;
	// パネルの暗い塗りの緑
	constexpr float kPanelFillGreen = 0.05f;
	// パネルの暗い塗りの青
	constexpr float kPanelFillBlue = 0.03f;
	// パネルの暗い塗りの不透明度
	constexpr float kPanelFillAlpha = 0.62f;
	// 選んだタブの塗りの不透明度
	constexpr float kSelectedTabAlpha = 0.85f;
	// 選んだタブの光の強さ
	constexpr float kSelectedTabGlow = 0.4f;
	// パネル・タブ・ボタンの枠の太さ [px]
	constexpr float kFrameThickness = 1.5f;
	// 明るい塗りの上に置く暗い文字の緑
	constexpr float kDarkTextGreen = 0.08f;
	// 明るい塗りの上に置く暗い文字の青
	constexpr float kDarkTextBlue = 0.04f;
	// 選んでいないタブの文字の不透明度
	constexpr float kTabTextAlpha = 0.8f;
	// TextRenderer のフォントの高さ（文字の大きさ 1 のとき）[px]
	constexpr float kFontPixelHeight = 32.0f;
	// 英字の見出しをタブの下からどれだけ下に置くか [px]
	constexpr float kCaptionOffsetY = 8.0f;
	// 英字の見出しの左の余白 [px]
	constexpr float kCaptionInsetX = 4.0f;
	// 英字の見出しの不透明度
	constexpr float kCaptionAlpha = 0.6f;
	// 並びの線を英字の見出しからどれだけ下に置くか [px]
	constexpr float kCaptionLineOffsetY = 22.0f;
	// 選んだ行の塗りの不透明度
	constexpr float kSelectedItemAlpha = 0.16f;
	// 選んだ行の光の強さ
	constexpr float kSelectedItemGlow = 0.12f;
	// 選んだ行の枠の太さ [px]
	constexpr float kSelectedItemFrameThickness = 2.0f;
	// 付けている物の E の札の幅 [px]
	constexpr float kEquippedBadgeWidth = 26.0f;
	// E の札の不透明度
	constexpr float kEquippedBadgeAlpha = 0.9f;
	// E の札の光の強さ
	constexpr float kEquippedBadgeGlow = 0.4f;
	// E の文字を札の左からどれだけ右に置くか [px]
	constexpr float kEquippedLetterOffsetX = 6.0f;
	// E の文字を札の上からどれだけ下に置くか [px]
	constexpr float kEquippedLetterOffsetY = 8.0f;
	// E の文字の大きさ
	constexpr float kEquippedLetterScale = 0.6f;
	// 行の文字を行の左からどれだけ右に置くか [px]
	constexpr float kItemTextOffsetX = 40.0f;
	// 名前を行の上からどれだけ下に置くか [px]
	constexpr float kItemNameOffsetY = 12.0f;
	// 選んでいない行の名前の不透明度
	constexpr float kUnselectedTextAlpha = 0.8f;
	// 選んでいない行の名前の光の強さ（選んだ行に対する割合）
	constexpr float kUnselectedGlowRatio = 0.3f;
	// 要約を行の上からどれだけ下に置くか [px]
	constexpr float kItemSummaryOffsetY = 52.0f;
	// 要約の不透明度
	constexpr float kItemSummaryAlpha = 0.55f;
	// 段階の ◀ ▶ の大きさ [px]
	constexpr float kLevelArrowSize = 18.0f;
	// 段階の ◀ ▶ を指していないときの不透明度
	constexpr float kLevelArrowIdleAlpha = 0.7f;
	// 段階の ◀ ▶ を指しているときの光の強さ
	constexpr float kLevelArrowHoverGlow = 1.5f;
	// 右のパネルの左右の余白 [px]
	constexpr float kPanelPaddingX = 16.0f;
	// 欄の見出しの帯の不透明度
	constexpr float kSectionBarAlpha = 0.8f;
	// 欄の見出しの帯の光の強さ
	constexpr float kSectionBarGlow = 0.35f;
	// 欄の見出しの文字を帯の上からどれだけ下に置くか [px]
	constexpr float kSectionTitleOffsetY = 3.0f;
	// 欄の見出しの帯の下のすき間 [px]
	constexpr float kSectionGap = 8.0f;
	// フレームの欄・素材の行の名前の不透明度
	constexpr float kFrameLabelAlpha = 0.85f;
	// 変わらない値の光の強さ（文字の光に対する割合）
	constexpr float kFrameValueGlowRatio = 0.4f;
	// 替えた後の値と「→」の間 [px]
	constexpr float kChangeArrowGap = 8.0f;
	// 「→」と今の値の不透明度
	constexpr float kOldValueAlpha = 0.6f;
	// 「→」と今の値の間 [px]
	constexpr float kOldValueGap = 16.0f;
	// フレームの欄・性能の 1 行の高さ [px]
	constexpr float kStatRowHeight = 34.0f;
	// 欄と欄の間に足すすき間 [px]
	constexpr float kSectionExtraGap = 10.0f;
	// 右のパネルの名前を一覧の名前より大きくする量
	constexpr float kNameScaleBoost = 0.06f;
	// 右のパネルの名前の行の高さ [px]
	constexpr float kNameRowHeight = 46.0f;
	// 性能・素材の行の左の字下げ [px]
	constexpr float kStatIndentX = 8.0f;
	// 性能・素材の行の文字を小さい文字より大きくする量
	constexpr float kStatScaleBoost = 0.04f;
	// 性能の名前・初期装備の文字の不透明度
	constexpr float kStatLabelAlpha = 0.8f;
	// 性能の値の光の強さ（文字の光に対する割合）
	constexpr float kStatValueGlowRatio = 0.3f;
	// 行の下の線を行の上からどれだけ下に置くか [px]
	constexpr float kRowUnderlineOffsetY = 27.0f;
	// 行の下の線の不透明度
	constexpr float kRowUnderlineAlpha = 0.7f;
	// 説明文を「作成／強化に必要」の欄からどれだけ離すか [px]
	constexpr float kDescriptionBottomMargin = 28.0f;
	// 説明文の不透明度
	constexpr float kDescriptionAlpha = 0.7f;
	// 説明文の行の高さ [px]
	constexpr float kDescriptionLineHeight = 25.0f;
	// 「作成／強化に必要」の欄の下の余白 [px]
	constexpr float kCostBottomPadding = 8.0f;
	// 強化と呼ぶ最初の段階
	constexpr int kFirstUpgradeLevel = 2;
	// 素材の色の印を行の上からどれだけ下に置くか [px]
	constexpr float kCostMarkOffsetY = 7.0f;
	// 素材の色の印の一辺 [px]
	constexpr float kCostMarkSize = 12.0f;
	// 素材の色の印の光の強さ
	constexpr float kCostMarkGlow = 0.4f;
	// 素材の名前を左からどれだけ右に置くか [px]
	constexpr float kCostLabelOffsetX = 30.0f;
	// 戻るボタンを指しているときの塗りの不透明度
	constexpr float kBackHoverAlpha = 0.25f;
	// 戻るボタンを指しているときの光の強さ
	constexpr float kBackHoverGlow = 0.3f;
	// 戻るボタンの ◀ をボタンの左からどれだけ右に置くか [px]
	constexpr float kBackArrowOffsetX = 24.0f;
	// 戻るボタンの ◀ の大きさ [px]
	constexpr float kBackArrowSize = 16.0f;
	// 「戻る」をボタンの左からどれだけ右に置くか [px]
	constexpr float kBackLabelOffsetX = 48.0f;
	// 戻るボタンを指しているときの文字の光の強さ（文字の光に対する割合）
	constexpr float kBackLabelGlowRatio = 0.6f;
	// 右下の操作の案内の文字の大きさ
	constexpr float kHintScale = 0.45f;
	// 右下の操作の案内を画面の下端からどれだけ上に置くか [px]
	constexpr float kHintBottomOffset = 66.0f;
	// 右下の操作の案内の不透明度
	constexpr float kHintAlpha = 0.6f;

	// 基準の値が読めないときの値（player.json の既定と同じ）
	constexpr int kFallbackAttack = 10;
	constexpr int kFallbackMaxHp = 100;
	constexpr float kNameScale = 0.72f;
	constexpr float kSmallScale = 0.5f;
	constexpr float kLabelScale = 0.58f;
	constexpr float kSectionHeight = 28.0f;

	const char* kTabLabels[] = { "メイン武器", "盾", "飛行ユニット" };
	const char* kTabCaptions[] = { "MAIN WEAPON", "SHIELD", "FLIGHT UNIT" };
	const char* kTabMessages[] = { "メイン武器を選択します。", "盾を選択します。", "飛行ユニットを選択します。" };

	/// <summary>0〜1 に切る</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 の値</returns>
	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}
	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">元の色</param>
	/// <param name="alpha">α に掛ける倍率</param>
	/// <returns>α だけ変えた色</returns>
	Vector4 WithAlpha(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}

	/// <summary>printf 形式で文字列を作る（128 バイトまで）</summary>
	/// <param name="format">書式</param>
	/// <returns>作った文字列</returns>
	std::string Format(const char* format, ...) {
		char buffer[128];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		return buffer;
	}
	/// <summary>割合を符号付きの百分率の文字列にする（0 は ±0%）</summary>
	/// <param name="ratio">割合（0.1 で +10%）</param>
	/// <returns>例：+10%</returns>
	std::string SignedPercent(float ratio) {
		const int p = static_cast<int>(std::lround(ratio * kPercentScale));
		if (p == 0) return "±0%";
		return Format("%+d%%", p);
	}

	/// <summary>ワールドの点を UI の画面の座標へ投影する</summary>
	/// <param name="camera">投影に使うカメラ</param>
	/// <param name="p">ワールドの点</param>
	/// <param name="screen">UI の画面の大きさ [px]</param>
	/// <param name="out">画面の座標 [px] の受け取り先</param>
	/// <returns>カメラの後ろなら false</returns>
	bool ProjectToScreen(const Camera& camera, const Vector3& p, const Vector2& screen, Vector2& out) {
		const Matrix4x4& vp = camera.GetViewProjectionMatrix();
		const float x = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
		const float y = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
		const float w = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
		if (w <= kMinClipW) return false;
		out = { (x / w * 0.5f + 0.5f) * screen.x, (0.5f - y / w * 0.5f) * screen.y };
		return true;
	}

	/// <summary>UTF-8 の 1 文字のバイト数を先頭のバイトから求める</summary>
	/// <param name="lead">文字の先頭のバイト</param>
	/// <returns>バイト数 1〜4（不正なバイトは 1）</returns>
	size_t CharLength(unsigned char lead) {
		if (lead < kUtf8SingleByteLimit) return 1;
		if ((lead >> kUtf8Lead2Shift) == kUtf8Lead2Pattern) return 2;
		if ((lead >> kUtf8Lead3Shift) == kUtf8Lead3Pattern) return 3;
		if ((lead >> kUtf8Lead4Shift) == kUtf8Lead4Pattern) return 4;
		return 1;
	}
}

LoadoutScreen::LoadoutScreen() {
	ResetToDefaults();
}

void LoadoutScreen::ResetToDefaults() {
	color_ = kDefaultColor;
	dimColor_ = kDefaultDimColor;
	upColor_ = kDefaultUpColor;
	downColor_ = kDefaultDownColor;
	backdropColor_ = kDefaultBackdropColor;
	textGlow_ = kDefaultTextGlow;
	lineGlow_ = 1.0f;
	marginX_ = kDefaultMarginX;
	topY_ = kDefaultTopY;
	leftWidth_ = kDefaultLeftWidth;
	rightWidth_ = kDefaultRightWidth;
	tabHeight_ = kDefaultTabHeight;
	itemHeight_ = kDefaultItemHeight;
	itemGap_ = kDefaultItemGap;
	panelStagger_ = kDefaultPanelStagger;
	headerLineRightInset_ = kDefaultHeaderLineRightInset;
	unitTexts_[static_cast<size_t>(FlightUnitKind::Wings)] = { "初期の羽根", "",
		"最初から付いている羽根。機動力を上げるだけで、RB の技は無い。" };
	unitTexts_[static_cast<size_t>(FlightUnitKind::Assault)] = { "攻撃ユニット", "ファンネル展開",
		"背中のファンネルがレティクルの先へ自動で支援射撃する。RB でファンネル 4 基を放ち、狙った敵を優先して撃つ。" };
	unitTexts_[static_cast<size_t>(FlightUnitKind::Guard)] = { "防御ユニット", "耐久バリア",
		"一定の間隔で被ダメージを軽減する。RB で耐久力付きのバリアを張り、敵弾を消す。バリアの間はジャスト回避が起きない。" };
	unitTexts_[static_cast<size_t>(FlightUnitKind::Mobility)] = { "機動ユニット", "時間減速",
		"移動が速くなり、ジャスト回避の受付が延びる。RB で数秒だけ世界を遅くし、自分は普通の速さで動ける。" };
}

void LoadoutScreen::Open() {
	tab_ = kTabMain;
	for (int t = 0; t < kTabCount; ++t) cursor_[t] = (std::max)(EquippedIndex(t), 0);
	hoverArrow_ = 0;
	hoverBack_ = false;
}

//==================== 一覧 ====================

int LoadoutScreen::ItemCount(int tab) const {
	switch (tab) {
	case kTabMain:   return MainWeaponCatalog::GetInstance()->GetCount();
	case kTabShield: return ShieldCatalog::GetInstance()->GetCount();
	case kTabUnit:   return static_cast<int>(FlightUnitKind::Count);
	default:         return 0;
	}
}

int LoadoutScreen::EquippedIndex(int tab) const {
	const LoadoutState* loadout = gameSession_->GetLoadoutState();
	switch (tab) {
	case kTabMain:   return MainWeaponCatalog::GetInstance()->IndexOf(loadout->GetMainWeaponId());
	case kTabShield: return ShieldCatalog::GetInstance()->IndexOf(loadout->GetShieldId());
	case kTabUnit:   return static_cast<int>(loadout->GetFlightUnit());
	default:         return -1;
	}
}

std::string LoadoutScreen::ItemName(int tab, int index) const {
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) return d->displayName;
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) return d->displayName;
		break;
	case kTabUnit:
		if (index >= 0 && index < static_cast<int>(unitTexts_.size())) return unitTexts_[index].name;
		break;
	default:
		break;
	}
	return "";
}

std::string LoadoutScreen::ItemSummary(int tab, int index, const FlightUnitPart* unit) const {
	(void)unit;
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) {
			return Format("攻撃力 %+d   チャージ %.1f / %.1f 秒", d->attackBonus, d->chargeStage1Time, d->chargeStage2Time);
		}
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) {
			return Format("HP 上限 %+d   軽減 %d%%", d->maxHpAdd, static_cast<int>(std::lround(d->damageCut * kPercentScale)));
		}
		break;
	case kTabUnit:
		if (index >= 0 && index < static_cast<int>(unitTexts_.size())) {
			const std::string& skill = unitTexts_[index].skillName;
			return skill.empty() ? std::string("RB の技なし") : "RB：" + skill;
		}
		break;
	default:
		break;
	}
	return "";
}

bool LoadoutScreen::Equip(int tab, int index) {
	if (index == EquippedIndex(tab)) return false;
	LoadoutState* loadout = gameSession_->GetLoadoutState();
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) {
			loadout->SetMainWeaponId(d->id);
			return true;
		}
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) {
			loadout->SetShieldId(d->id);
			return true;
		}
		break;
	case kTabUnit:
		if (index >= 0 && index < static_cast<int>(FlightUnitKind::Count)) {
			loadout->SetFlightUnit(UnitKindAt(index));
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

bool LoadoutScreen::ChangeLevel(int index, int delta) {
	const FlightUnitKind kind = UnitKindAt(index);
	if (kind == FlightUnitKind::Wings || index < 0 || index >= static_cast<int>(FlightUnitKind::Count)) return false;
	LoadoutState* loadout = gameSession_->GetLoadoutState();
	const int before = loadout->GetFlightUnitLevel(kind);
	loadout->SetFlightUnitLevel(kind, before + delta);
	return loadout->GetFlightUnitLevel(kind) != before;
}

//==================== 値 ====================

LoadoutScreen::FrameStats LoadoutScreen::ComputeFrame(int tab, int index, const FlightUnitPart* unit) const {
	// ゲームと同じ計算（StagePlayScene::ScaledPlayerAttack / UpdateFlightUnit）。index が負なら付けている物のまま
	const LoadoutState* loadout = gameSession_->GetLoadoutState();
	const MainWeaponDef* weapon = loadout->GetMainWeapon();
	const ShieldDef* shield = loadout->GetShield();
	FlightUnitKind kind = loadout->GetFlightUnit();
	if (index >= 0) {
		if (tab == kTabMain) weapon = MainWeaponCatalog::GetInstance()->GetDef(index);
		if (tab == kTabShield) shield = ShieldCatalog::GetInstance()->GetDef(index);
		if (tab == kTabUnit) kind = UnitKindAt(index);
	}
	int baseAttack = kFallbackAttack;
	int baseHp = kFallbackMaxHp;
	if (const PrefabDef* player = PrefabManager::GetInstance()->Find("player")) {
		if (player->hasAttackPower) baseAttack = player->attackPower;
		if (player->hasHP) baseHp = player->maxHP;
	}
	const FlightUnitSkill* skill = unit ? unit->GetSkill(kind) : nullptr;
	const float attackScale = skill ? skill->GetAttackScale() : 1.0f;
	const float hpScale = skill ? skill->GetMaxHpScale() : 1.0f;
	const float moveScale = skill ? skill->GetMoveSpeedScale() : 1.0f;

	FrameStats s;
	const int attack = baseAttack + (weapon ? weapon->attackBonus : 0);
	s.attack = attack > 0 ? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(attack) * attackScale))) : 0;
	s.maxHp = static_cast<int>(std::lround(static_cast<float>(baseHp + (shield ? shield->maxHpAdd : 0)) * hpScale));
	s.moveScale = (shield ? shield->moveSpeedScale : 1.0f) * moveScale;
	return s;
}

std::vector<LoadoutScreen::StatLine> LoadoutScreen::ItemStats(int tab, int index, const FlightUnitPart* unit) const {
	std::vector<StatLine> lines;
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) {
			lines.push_back({ "攻撃力", Format("%+d", d->attackBonus) });
			lines.push_back({ "チャージ 1 段目", Format("%.1f 秒", d->chargeStage1Time) });
			lines.push_back({ "チャージ 2 段目", Format("%.1f 秒", d->chargeStage2Time) });
		}
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) {
			for (const ShieldStatLine& l : ShieldCatalog::GetStatLines(*d)) lines.push_back({ l.label, l.value });
		}
		break;
	case kTabUnit: {
		const FlightUnitKind kind = UnitKindAt(index);
		const FlightUnitSkill* skill = unit ? unit->GetSkill(kind) : nullptr;
		if (kind == FlightUnitKind::Wings || !skill) {
			lines.push_back({ "RB の技", "なし" });
			break;
		}
		lines.push_back({ "段階", Format("Lv %d / %d", gameSession_->GetLoadoutState()->GetFlightUnitLevel(kind),
			LoadoutState::kMaxFlightUnitLevel) });
		// 常時の補正（1 倍でない物だけ）
		if (std::abs(skill->GetAttackScale() - 1.0f) > kScaleEpsilon) lines.push_back({ "攻撃力", SignedPercent(skill->GetAttackScale() - 1.0f) });
		if (std::abs(skill->GetMaxHpScale() - 1.0f) > kScaleEpsilon) lines.push_back({ "HP 上限", SignedPercent(skill->GetMaxHpScale() - 1.0f) });
		if (std::abs(skill->GetMoveSpeedScale() - 1.0f) > kScaleEpsilon) lines.push_back({ "移動速度", SignedPercent(skill->GetMoveSpeedScale() - 1.0f) });
		// ジャスト回避の受付は機動系だけ（2026-11-01 ユーザー決定）
		if (kind == FlightUnitKind::Mobility) {
			lines.push_back({ "ジャスト回避の受付", Format("%+.3f 秒", skill->GetJustDodgeWindowBonus()) });
		}
		lines.push_back({ "RB の技", unitTexts_[index].skillName });
		lines.push_back({ "クールタイム", Format("%.0f 秒", skill->GetCooldown()) });
		break;
	}
	default:
		break;
	}
	return lines;
}

std::string LoadoutScreen::ItemDescription(int tab, int index) const {
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) return d->description;
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) return d->description;
		break;
	case kTabUnit:
		if (index >= 0 && index < static_cast<int>(unitTexts_.size())) return unitTexts_[index].description;
		break;
	default:
		break;
	}
	return "";
}

//==================== 配置 ====================

LoadoutScreen::Rect LoadoutScreen::TabRect(int tab) const {
	const float w = leftWidth_ / static_cast<float>(kTabCount);
	return { marginX_ + w * static_cast<float>(tab), topY_, w - kTabGap, tabHeight_ };
}

LoadoutScreen::Rect LoadoutScreen::ItemRect(int index) const {
	const float y0 = topY_ + tabHeight_ + kListOffsetY;
	return { marginX_, y0 + (itemHeight_ + itemGap_) * static_cast<float>(index), leftWidth_, itemHeight_ };
}

LoadoutScreen::Rect LoadoutScreen::LevelArrowRect(int index, int direction) const {
	const Rect item = ItemRect(index);
	constexpr float kSize = 30.0f;
	const float cy = item.y + item.height * 0.5f;
	const float cx = direction < 0 ? item.x + item.width - kLevelDownArrowOffset : item.x + item.width - kLevelUpArrowOffset;
	return { cx - kSize * 0.5f, cy - kSize * 0.5f, kSize, kSize };
}

LoadoutScreen::Rect LoadoutScreen::BackButtonRect(const Vector2& screen) const {
	return { marginX_, screen.y - kBackButtonBottomOffset, kBackButtonWidth, kBackButtonHeight };
}

LoadoutScreen::Rect LoadoutScreen::LeftPanelRect() const {
	const int count = ItemCount(tab_);
	const Rect last = ItemRect((std::max)(count - 1, 0));
	return { marginX_, topY_, leftWidth_, last.y + last.height - topY_ };
}

LoadoutScreen::Rect LoadoutScreen::RightPanelRect(const Vector2& screen) const {
	return { screen.x - marginX_ - rightWidth_, topY_, rightWidth_, screen.y - topY_ - kRightPanelBottomMargin };
}

bool LoadoutScreen::IsOverPanel(const Vector2& position, const Vector2& screen) const {
	return LeftPanelRect().Contains(position) || RightPanelRect(screen).Contains(position)
		|| BackButtonRect(screen).Contains(position);
}

//==================== 入力 ====================

LoadoutScreen::Result LoadoutScreen::Update(InputManager* input, const UIPointer& pointer, const Vector2& screen,
	bool inputEnabled) {
	hoverArrow_ = 0;
	hoverBack_ = false;
	if (!inputEnabled || !input) return Result::None;
	InputActionMap* actions = input->GetActionMap();
	KeyboardInput* keyboard = input->GetKeyboard();
	ControllerInput* pad = input->GetController();
	SoundManager* sm = SoundManager::GetInstance();
	auto triggered = [actions](Action a) { return actions && actions->IsTriggered(static_cast<int>(a)); };
	auto key = [keyboard](BYTE k) { return keyboard && keyboard->TriggerKey(k); };
	auto button = [pad](WORD b) { return pad && pad->IsConnected() && pad->IsButtonTriggered(b); };

	const int count = ItemCount(tab_);
	int& cursor = cursor_[tab_];
	cursor = std::clamp(cursor, 0, (std::max)(count - 1, 0));

	// マウス：タブ・一覧・段階の ◀ ▶・戻る。一覧は動かしたときだけ選択を追う（止まったカーソルがキー操作を上書きしないように）
	if (pointer.valid) {
		for (int t = 0; t < kTabCount; ++t) {
			if (pointer.clicked && t != tab_ && TabRect(t).Contains(pointer.position)) {
				tab_ = t;
				sm->Play2DSound("se_ui_cursor");
				return Result::None;
			}
		}
		for (int i = 0; i < count; ++i) {
			if (!ItemRect(i).Contains(pointer.position)) continue;
			if (tab_ == kTabUnit && UnitKindAt(i) != FlightUnitKind::Wings) {
				if (LevelArrowRect(i, -1).Contains(pointer.position)) hoverArrow_ = -1;
				if (LevelArrowRect(i, +1).Contains(pointer.position)) hoverArrow_ = +1;
			}
			if (pointer.moved && cursor != i) {
				cursor = i;
				sm->Play2DSound("se_ui_cursor");
			}
			if (pointer.clicked) {
				cursor = i;
				if (hoverArrow_ != 0) {
					if (ChangeLevel(i, hoverArrow_)) {
						sm->Play2DSound("se_ui_cursor");
						return Result::LevelChanged;
					}
					return Result::None;
				}
				sm->Play2DSound("se_ui_decide");
				return Equip(tab_, i) ? Result::Equipped : Result::None;
			}
		}
		if (BackButtonRect(screen).Contains(pointer.position)) {
			hoverBack_ = true;
			if (pointer.clicked) {
				sm->Play2DSound("se_ui_cancel");
				return Result::Back;
			}
		}
	}

	// タブ：LB / RB・Q / E
	if (button(XINPUT_GAMEPAD_LEFT_SHOULDER) || key(DIK_Q)) {
		tab_ = (tab_ + kTabCount - 1) % kTabCount;
		sm->Play2DSound("se_ui_cursor");
		return Result::None;
	}
	if (button(XINPUT_GAMEPAD_RIGHT_SHOULDER) || key(DIK_E)) {
		tab_ = (tab_ + 1) % kTabCount;
		sm->Play2DSound("se_ui_cursor");
		return Result::None;
	}
	// 一覧：上下・W / S（端で回り込む）
	if (count > 0 && (triggered(Action::MenuDown) || key(DIK_S))) {
		cursor = (cursor + 1) % count;
		sm->Play2DSound("se_ui_cursor");
	}
	if (count > 0 && (triggered(Action::MenuUp) || key(DIK_W))) {
		cursor = (cursor + count - 1) % count;
		sm->Play2DSound("se_ui_cursor");
	}
	// 段階：左右・A / D（飛行ユニットのタブだけ）
	if (tab_ == kTabUnit) {
		int delta = 0;
		if (triggered(Action::MenuLeft) || key(DIK_A)) delta = -1;
		if (triggered(Action::MenuRight) || key(DIK_D)) delta = +1;
		if (delta != 0 && ChangeLevel(cursor, delta)) {
			sm->Play2DSound("se_ui_cursor");
			return Result::LevelChanged;
		}
	}
	// 付ける：A・Enter・Space
	if (count > 0 && (triggered(Action::MenuConfirm) || key(DIK_SPACE))) {
		sm->Play2DSound("se_ui_decide");
		return Equip(tab_, cursor) ? Result::Equipped : Result::None;
	}
	// 戻る：B・BackSpace・Esc
	if (triggered(Action::MenuCancel) || key(DIK_ESCAPE)) {
		sm->Play2DSound("se_ui_cancel");
		return Result::Back;
	}
	return Result::None;
}

//==================== 描く ====================

void LoadoutScreen::Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength) {
	TextRenderer* tr = TextRenderer::GetInstance();
	tr->DrawText(text, pos, scale, color);
	if (glow_ && glowStrength > 0.0f) {
		tr->DrawTextGlow(text, pos, scale, { color.x * glowStrength, color.y * glowStrength, color.z * glowStrength, color.w });
	}
}

void LoadoutScreen::Rect4(const Rect& r, const Vector4& color, float glowStrength) {
	TextRenderer* tr = TextRenderer::GetInstance();
	tr->DrawRect({ r.x, r.y }, { r.width, r.height }, color);
	if (glow_ && glowStrength > 0.0f) {
		tr->DrawRectGlow({ r.x, r.y }, { r.width, r.height },
			{ color.x * glowStrength, color.y * glowStrength, color.z * glowStrength, color.w });
	}
}

void LoadoutScreen::Frame(const Rect& r, const Vector4& color, float thickness) {
	Rect4({ r.x, r.y, r.width, thickness }, color, 0.0f);
	Rect4({ r.x, r.y + r.height - thickness, r.width, thickness }, color, 0.0f);
	Rect4({ r.x, r.y, thickness, r.height }, color, 0.0f);
	Rect4({ r.x + r.width - thickness, r.y, thickness, r.height }, color, 0.0f);
}

void LoadoutScreen::Arrow(const Vector2& center, float size, bool right, const Vector4& color, float glowStrength) {
	// 縦の細い帯を並べた三角（根元が高く、先が低い）
	constexpr float kStrip = 2.0f;
	const int steps = (std::max)(static_cast<int>(size * 0.5f / kStrip), 1);
	for (int s = 0; s < steps; ++s) {
		const float k = static_cast<float>(s) / static_cast<float>(steps);
		const float h = size * (1.0f - k);
		const float x = right ? center.x - size * kArrowTipOffsetRatio + kStrip * static_cast<float>(s)
			: center.x + size * kArrowTipOffsetRatio - kStrip * static_cast<float>(s + 1);
		Rect4({ x, center.y - h * 0.5f, kStrip, h }, color, glowStrength);
	}
}

std::vector<std::string> LoadoutScreen::Wrap(std::string_view text, float scale, float maxWidth) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	std::vector<std::string> lines;
	std::string line;
	size_t i = 0;
	while (i < text.size()) {
		const size_t len = (std::min)(CharLength(static_cast<unsigned char>(text[i])), text.size() - i);
		const std::string_view ch = text.substr(i, len);
		i += len;
		if (ch == "\n") {
			lines.push_back(line);
			line.clear();
			continue;
		}
		std::string next = line;
		next.append(ch);
		if (!line.empty() && tr->MeasureWidth(next, scale) > maxWidth) {
			lines.push_back(line);
			line.assign(ch);
		} else {
			line = std::move(next);
		}
	}
	if (!line.empty()) lines.push_back(line);
	return lines;
}

void LoadoutScreen::DrawBackdrop(const Vector2& screen, const Camera& camera, const Vector3& floorCenter, float radius,
	float time) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;
	tr->DrawRect({ 0.0f, 0.0f }, { screen.x, screen.y }, backdropColor_);

	// 走査線のような横の細い線
	const Vector4 scan = WithAlpha(color_, kScanlineAlpha);
	for (float y = 0.0f; y < screen.y; y += kScanlineSpacing) tr->DrawRect({ 0.0f, y }, { screen.x, 1.0f }, scan);

	// 中央の点の輪（ゆっくり回る。コクピットの計器と同じ形）
	const Vector2 c = { screen.x * 0.5f, screen.y * kRingCenterY };
	const Vector4 faint = WithAlpha(color_, kRingAlpha);
	const float ring = screen.y * kRingRadiusRatio;
	constexpr int kDots = 144;
	for (int i = 0; i < kDots; ++i) {
		if (i % kRingDotsPerSegment >= kRingDotsShownPerSegment) continue;
		const float a = static_cast<float>(i) / kDots * 2.0f * kPi + time * kRingSpinSpeed;
		const float size = (i % kRingMajorDotInterval == 0) ? kRingMajorDotSize : kRingMinorDotSize;
		tr->DrawRect({ c.x + std::cos(a) * ring - size * 0.5f, c.y + std::sin(a) * ring - size * 0.5f }, { size, size }, faint);
	}

	// 足元の輪（ワールドの円を画面へ。機体の後ろに描くので、体で隠れた所は見えない）
	auto drawFloorRing = [&](float r, int dots, const Vector4& color, float size) {
		for (int i = 0; i < dots; ++i) {
			const float a = static_cast<float>(i) / static_cast<float>(dots) * 2.0f * kPi - time * kFloorRingSpinSpeed;
			const Vector3 p = { floorCenter.x + std::cos(a) * r, floorCenter.y, floorCenter.z + std::sin(a) * r };
			Vector2 s;
			if (!ProjectToScreen(camera, p, screen, s)) continue;
			tr->DrawRect({ s.x - size * 0.5f, s.y - size * 0.5f }, { size, size }, color);
		}
	};
	drawFloorRing(radius, kFloorRingDots, WithAlpha(color_, kFloorRingAlpha), kFloorRingDotSize);
	drawFloorRing(radius * kFloorInnerRingRadiusRatio, kFloorInnerRingDots, WithAlpha(color_, kFloorInnerRingAlpha), kFloorSubRingDotSize);
	drawFloorRing(radius * kFloorOuterRingRadiusRatio, kFloorOuterRingDots, WithAlpha(color_, kFloorOuterRingAlpha), kFloorSubRingDotSize);
}

void LoadoutScreen::Draw(const Vector2& screen, float uiTime, float collapse, const FlightUnitPart* unit, bool glow) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || uiTime < 0.0f || collapse >= 1.0f) return;
	glow_ = glow;

	// 戻るときはコクピットの UI と同じく、ブラウン管が切れるように縦に潰れて点になる
	const Vector2 center = { screen.x * 0.5f, screen.y * 0.5f };
	if (collapse > 0.0f) {
		const float squash = Clamp01(collapse / kCollapseSquashEnd);
		const float shrink = Clamp01((collapse - kCollapseSquashEnd) / kCollapseShrinkSpan);
		tr->SetTransform(center, { (std::max)(1.0f - shrink * shrink, kMinCollapseScaleX), 1.0f + (kCollapsedScaleY - 1.0f) * squash * squash });
	}

	const float leftAppear = CockpitReveal::Flicker(uiTime, kPanelFlickerSeconds);
	const float rightAppear = CockpitReveal::Flicker(uiTime - panelStagger_, kPanelFlickerSeconds);
	const float footerAppear = CockpitReveal::Flicker(uiTime - panelStagger_ * 2.0f, kPanelFlickerSeconds);

	// 見出し：左上に「機体設定」、上の中央に案内、下に横の線
	if (leftAppear > 0.0f) {
		Text("機体設定", { marginX_, kTitleY }, kTitleScale, WithAlpha(color_, leftAppear), textGlow_);
		const char* message = kTabMessages[tab_];
		const float mw = tr->MeasureWidth(message, kSmallScale + kMessageScaleBoost);
		Text(message, { (screen.x - mw) * 0.5f, kMessageY }, kSmallScale + kMessageScaleBoost, WithAlpha(color_, kMessageAlpha * leftAppear), 0.0f);
		// 右上の所持エーテルの枠（HubEtherBet::DrawHandPanel）に掛からないよう、その手前で止める
		Rect4({ marginX_, kHeaderLineY, screen.x - marginX_ - headerLineRightInset_, kHeaderLineThickness }, WithAlpha(color_, kHeaderLineAlpha * leftAppear), lineGlow_);
	}
	if (leftAppear > 0.0f) DrawLeft(leftAppear, unit);
	if (rightAppear > 0.0f) DrawRight(screen, rightAppear, unit);
	if (footerAppear > 0.0f) DrawFooter(screen, footerAppear);

	tr->ResetTransform();
}

void LoadoutScreen::DrawLeft(float appear, const FlightUnitPart* unit) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector4 panelFill = { kPanelFillRed, kPanelFillGreen, kPanelFillBlue, kPanelFillAlpha * appear };

	// タブ
	for (int t = 0; t < kTabCount; ++t) {
		const Rect r = TabRect(t);
		const bool selected = (t == tab_);
		Rect4(r, selected ? WithAlpha(color_, kSelectedTabAlpha * appear) : panelFill, selected ? kSelectedTabGlow : 0.0f);
		Frame(r, WithAlpha(selected ? color_ : dimColor_, appear), kFrameThickness);
		const float lw = tr->MeasureWidth(kTabLabels[t], kLabelScale);
		const Vector4 textColor = selected ? Vector4{ 0.0f, kDarkTextGreen, kDarkTextBlue, appear } : WithAlpha(color_, kTabTextAlpha * appear);
		Text(kTabLabels[t], { r.x + (r.width - lw) * 0.5f, r.y + (r.height - kFontPixelHeight * kLabelScale) * 0.5f }, kLabelScale,
			textColor, 0.0f);
	}
	// 見出し（英字）と並びの線
	const float captionY = topY_ + tabHeight_ + kCaptionOffsetY;
	Text(kTabCaptions[tab_], { marginX_ + kCaptionInsetX, captionY }, kSmallScale, WithAlpha(color_, kCaptionAlpha * appear), 0.0f);
	Rect4({ marginX_, captionY + kCaptionLineOffsetY, leftWidth_, 1.0f }, WithAlpha(dimColor_, appear), 0.0f);

	// 一覧
	const int count = ItemCount(tab_);
	const int equipped = EquippedIndex(tab_);
	for (int i = 0; i < count; ++i) {
		const Rect r = ItemRect(i);
		const bool selected = (i == cursor_[tab_]);
		Rect4(r, selected ? WithAlpha(color_, kSelectedItemAlpha * appear) : panelFill, selected ? kSelectedItemGlow : 0.0f);
		Frame(r, WithAlpha(selected ? color_ : dimColor_, appear), selected ? kSelectedItemFrameThickness : 1.0f);
		// 付けている物に E の札
		if (i == equipped) {
			Rect4({ r.x, r.y, kEquippedBadgeWidth, r.height }, WithAlpha(color_, kEquippedBadgeAlpha * appear), kEquippedBadgeGlow);
			Text("E", { r.x + kEquippedLetterOffsetX, r.y + kEquippedLetterOffsetY }, kEquippedLetterScale, { 0.0f, kDarkTextGreen, kDarkTextBlue, appear }, 0.0f);
		}
		const float textX = r.x + kItemTextOffsetX;
		Text(ItemName(tab_, i), { textX, r.y + kItemNameOffsetY }, kNameScale, WithAlpha(color_, (selected ? 1.0f : kUnselectedTextAlpha) * appear),
			selected ? textGlow_ : textGlow_ * kUnselectedGlowRatio);
		Text(ItemSummary(tab_, i, unit), { textX, r.y + kItemSummaryOffsetY }, kSmallScale, WithAlpha(color_, kItemSummaryAlpha * appear), 0.0f);
		// 飛行ユニットの段階 ◀ Lv n ▶
		if (tab_ == kTabUnit && UnitKindAt(i) != FlightUnitKind::Wings) {
			const int level = gameSession_->GetLoadoutState()->GetFlightUnitLevel(UnitKindAt(i));
			const Rect left = LevelArrowRect(i, -1);
			const Rect right = LevelArrowRect(i, +1);
			const bool hl = selected && hoverArrow_ < 0;
			const bool hr = selected && hoverArrow_ > 0;
			const bool canDown = level > 1;
			const bool canUp = level < LoadoutState::kMaxFlightUnitLevel;
			Arrow({ left.x + left.width * 0.5f, left.y + left.height * 0.5f }, kLevelArrowSize, false,
				WithAlpha(canDown ? color_ : dimColor_, (hl ? 1.0f : kLevelArrowIdleAlpha) * appear), hl ? kLevelArrowHoverGlow : 0.0f);
			Arrow({ right.x + right.width * 0.5f, right.y + right.height * 0.5f }, kLevelArrowSize, true,
				WithAlpha(canUp ? color_ : dimColor_, (hr ? 1.0f : kLevelArrowIdleAlpha) * appear), hr ? kLevelArrowHoverGlow : 0.0f);
			const std::string lv = Format("Lv %d", level);
			const float lvW = tr->MeasureWidth(lv, kLabelScale);
			const float midX = (left.x + left.width + right.x) * 0.5f;
			Text(lv, { midX - lvW * 0.5f, left.y + (left.height - kFontPixelHeight * kLabelScale) * 0.5f }, kLabelScale,
				WithAlpha(color_, appear), selected ? textGlow_ * 0.5f : 0.0f);
		}
	}
}

void LoadoutScreen::DrawRight(const Vector2& screen, float appear, const FlightUnitPart* unit) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Rect panel = RightPanelRect(screen);
	Rect4(panel, { kPanelFillRed, kPanelFillGreen, kPanelFillBlue, kPanelFillAlpha * appear }, 0.0f);
	Frame(panel, WithAlpha(dimColor_, appear), kFrameThickness);
	const Vector4 dark = { 0.0f, kDarkTextGreen, kDarkTextBlue, appear };
	const float x0 = panel.x + kPanelPaddingX;
	const float x1 = panel.x + panel.width - kPanelPaddingX;
	float y = panel.y;

	auto section = [&](const char* title) {
		Rect4({ panel.x, y, panel.width, kSectionHeight }, WithAlpha(color_, kSectionBarAlpha * appear), kSectionBarGlow);
		Text(title, { x0, y + kSectionTitleOffsetY }, kSmallScale, dark, 0.0f);
		y += kSectionHeight + kSectionGap;
	};
	auto rightAligned = [&](const std::string& text, float right, float yy, float scale, const Vector4& color, float glow) {
		const float w = tr->MeasureWidth(text, scale);
		Text(text, { right - w, yy }, scale, color, glow);
		return w;
	};

	// フレームの合計：付けている物の値。カーソルの物に替えると変わるなら「今 → 替えた後」
	section("フレーム");
	const int cursor = cursor_[tab_];
	const FrameStats now = ComputeFrame(tab_, -1, unit);
	const FrameStats next = ComputeFrame(tab_, cursor, unit);
	auto frameRow = [&](const char* label, const std::string& nowText, const std::string& nextText, int compare) {
		Text(label, { x0, y }, kLabelScale, WithAlpha(color_, kFrameLabelAlpha * appear), 0.0f);
		if (compare == 0) {
			rightAligned(nowText, x1, y, kLabelScale, WithAlpha(color_, appear), textGlow_ * kFrameValueGlowRatio);
		} else {
			const Vector4 c = WithAlpha(compare > 0 ? upColor_ : downColor_, appear);
			const float w = rightAligned(nextText, x1, y, kLabelScale, c, textGlow_ * 0.5f);
			const float arrowW = rightAligned("→", x1 - w - kChangeArrowGap, y, kLabelScale, WithAlpha(color_, kOldValueAlpha * appear), 0.0f);
			rightAligned(nowText, x1 - w - arrowW - kOldValueGap, y, kLabelScale, WithAlpha(color_, kOldValueAlpha * appear), 0.0f);
		}
		y += kStatRowHeight;
	};
	auto compareInt = [](int a, int b) { return b > a ? 1 : (b < a ? -1 : 0); };
	frameRow("攻撃力", Format("%d", now.attack), Format("%d", next.attack), compareInt(now.attack, next.attack));
	frameRow("HP 上限", Format("%d", now.maxHp), Format("%d", next.maxHp), compareInt(now.maxHp, next.maxHp));
	const int nowMove = static_cast<int>(std::lround(now.moveScale * kPercentScale));
	const int nextMove = static_cast<int>(std::lround(next.moveScale * kPercentScale));
	frameRow("移動速度", Format("%d%%", nowMove), Format("%d%%", nextMove), compareInt(nowMove, nextMove));
	y += kSectionExtraGap;

	// カーソルの物：名前・性能・説明文
	section(kTabLabels[tab_]);
	Text(ItemName(tab_, cursor), { x0, y }, kNameScale + kNameScaleBoost, WithAlpha(color_, appear), textGlow_);
	y += kNameRowHeight;
	for (const StatLine& line : ItemStats(tab_, cursor, unit)) {
		Text(line.label, { x0 + kStatIndentX, y }, kSmallScale + kStatScaleBoost, WithAlpha(color_, kStatLabelAlpha * appear), 0.0f);
		rightAligned(line.value, x1, y, kSmallScale + kStatScaleBoost, WithAlpha(color_, appear), textGlow_ * kStatValueGlowRatio);
		Rect4({ x0, y + kRowUnderlineOffsetY, x1 - x0, 1.0f }, WithAlpha(dimColor_, kRowUnderlineAlpha * appear), 0.0f);
		y += kStatRowHeight;
	}
	y += kSectionExtraGap;
	// 説明文は下の端の「作成／強化に必要」の欄にかからない所まで
	const float costHeight = DrawCost(panel, appear, true);
	for (const std::string& line : Wrap(ItemDescription(tab_, cursor), kSmallScale, x1 - x0)) {
		if (y > panel.y + panel.height - kDescriptionBottomMargin - costHeight) break;
		Text(line, { x0, y }, kSmallScale, WithAlpha(color_, kDescriptionAlpha * appear), 0.0f);
		y += kDescriptionLineHeight;
	}
	DrawCost(panel, appear, false);
}

const CraftCost* LoadoutScreen::ItemCost(int tab, int index, int& level) const {
	const CraftingCatalog* crafting = CraftingCatalog::GetInstance();
	level = 1;
	switch (tab) {
	case kTabMain:
		if (const MainWeaponDef* d = MainWeaponCatalog::GetInstance()->GetDef(index)) return crafting->FindMainWeapon(d->id);
		break;
	case kTabShield:
		if (const ShieldDef* d = ShieldCatalog::GetInstance()->GetDef(index)) return crafting->FindShield(d->id);
		break;
	case kTabUnit:
		if (index >= 0 && index < static_cast<int>(FlightUnitKind::Count)) {
			const FlightUnitKind kind = UnitKindAt(index);
			level = gameSession_->GetLoadoutState()->GetFlightUnitLevel(kind);
			const std::string id = LoadoutState::GetFlightUnitName(kind);
			// 段階の無い物（初期の羽根）は Lv1 の欄を見る
			const CraftCost* cost = crafting->FindFlightUnit(id, level);
			if (!cost) {
				cost = crafting->FindFlightUnit(id, 1);
				level = 1;
			}
			return cost;
		}
		break;
	default:
		break;
	}
	return nullptr;
}

float LoadoutScreen::DrawCost(const Rect& panel, float appear, bool measureOnly) {
	int level = 1;
	const CraftCost* cost = ItemCost(tab_, cursor_[tab_], level);
	if (!cost) return 0.0f;

	constexpr float kRow = 30.0f;
	const int rows = cost->initial ? 1 : static_cast<int>(cost->materials.size()) + 1;
	const float height = kSectionHeight + kSectionGap + kRow * static_cast<float>(rows) + kCostBottomPadding;
	if (measureOnly) return height;

	TextRenderer* tr = TextRenderer::GetInstance();
	const float x0 = panel.x + kPanelPaddingX;
	const float x1 = panel.x + panel.width - kPanelPaddingX;
	float y = panel.y + panel.height - height;
	const Vector4 dark = { 0.0f, kDarkTextGreen, kDarkTextBlue, appear };
	auto rightAligned = [&](const std::string& text, float yy, const Vector4& color) {
		const float w = tr->MeasureWidth(text, kSmallScale + kStatScaleBoost);
		Text(text, { x1 - w, yy }, kSmallScale + kStatScaleBoost, color, textGlow_ * kStatValueGlowRatio);
	};

	// 見出し：初期装備／作成に必要／Lv○ への強化に必要
	std::string title;
	if (cost->initial) title = "入手";
	else if (level >= kFirstUpgradeLevel) title = Format("Lv%d への強化に必要", level);
	else title = "作成に必要";
	Rect4({ panel.x, y, panel.width, kSectionHeight }, WithAlpha(color_, kSectionBarAlpha * appear), kSectionBarGlow);
	Text(title, { x0, y + kSectionTitleOffsetY }, kSmallScale, dark, 0.0f);
	y += kSectionHeight + kSectionGap;

	if (cost->initial) {
		Text("初期装備（最初から持っている）", { x0 + kStatIndentX, y }, kSmallScale + kStatScaleBoost, WithAlpha(color_, kStatLabelAlpha * appear), 0.0f);
		return height;
	}
	// 素材の行：アイテムの色の印＋名前、右に個数
	const LootCatalog* loot = LootCatalog::GetInstance();
	auto row = [&](const std::string& label, const Vector4& mark, const std::string& value) {
		Rect4({ x0 + kStatIndentX, y + kCostMarkOffsetY, kCostMarkSize, kCostMarkSize }, WithAlpha(mark, appear), kCostMarkGlow);
		Text(label, { x0 + kCostLabelOffsetX, y }, kSmallScale + kStatScaleBoost, WithAlpha(color_, kFrameLabelAlpha * appear), 0.0f);
		rightAligned(value, y, WithAlpha(color_, appear));
		Rect4({ x0, y + kRowUnderlineOffsetY, x1 - x0, 1.0f }, WithAlpha(dimColor_, kRowUnderlineAlpha * appear), 0.0f);
		y += kRow;
	};
	for (const CraftCost::Material& m : cost->materials) {
		const LootItemDef* item = loot->FindItem(m.itemId);
		row(item ? item->displayName : m.itemId, item ? item->color : color_, Format("×%d", m.count));
	}
	row(loot->GetEther().displayName, loot->GetEther().color, Format("%d", cost->ether));
	return height;
}

void LoadoutScreen::DrawFooter(const Vector2& screen, float appear) {
	TextRenderer* tr = TextRenderer::GetInstance();
	// 戻るボタン
	const Rect back = BackButtonRect(screen);
	Rect4(back, hoverBack_ ? WithAlpha(color_, kBackHoverAlpha * appear) : Vector4{ kPanelFillRed, kPanelFillGreen, kPanelFillBlue, kPanelFillAlpha * appear },
		hoverBack_ ? kBackHoverGlow : 0.0f);
	Frame(back, WithAlpha(hoverBack_ ? color_ : dimColor_, appear), kFrameThickness);
	Arrow({ back.x + kBackArrowOffsetX, back.y + back.height * 0.5f }, kBackArrowSize, false, WithAlpha(color_, appear), 0.0f);
	Text("戻る", { back.x + kBackLabelOffsetX, back.y + (back.height - kFontPixelHeight * kLabelScale) * 0.5f }, kLabelScale,
		WithAlpha(color_, appear), hoverBack_ ? textGlow_ * kBackLabelGlowRatio : 0.0f);

	// 操作の案内（右下）
	const char* hint = "LB/RB：タブ   ↑↓：選択   A：装備   ←→：段階   B：戻る   右スティック/ドラッグ：回転";
	const float scale = kHintScale;
	const float w = tr->MeasureWidth(hint, scale);
	Text(hint, { screen.x - marginX_ - w, screen.y - kHintBottomOffset }, scale, WithAlpha(color_, kHintAlpha * appear), 0.0f);
}

//==================== 調整値 ====================

void LoadoutScreen::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	color_ = ReadVector4(node["color"], color_);
	dimColor_ = ReadVector4(node["dimColor"], dimColor_);
	upColor_ = ReadVector4(node["upColor"], upColor_);
	downColor_ = ReadVector4(node["downColor"], downColor_);
	backdropColor_ = ReadVector4(node["backdropColor"], backdropColor_);
	read("textGlow", textGlow_);
	read("lineGlow", lineGlow_);
	read("marginX", marginX_);
	read("topY", topY_);
	read("leftWidth", leftWidth_);
	read("rightWidth", rightWidth_);
	read("tabHeight", tabHeight_);
	read("itemHeight", itemHeight_);
	read("itemGap", itemGap_);
	read("panelStagger", panelStagger_);
	read("headerLineRightInset", headerLineRightInset_);
	const JsonValue& units = node["flightUnits"];
	if (units.IsArray()) {
		for (size_t i = 0; i < units.Size() && i < unitTexts_.size(); ++i) {
			const JsonValue& u = units[i];
			if (!u.IsObject()) continue;
			unitTexts_[i].name = u["name"].AsString(unitTexts_[i].name);
			unitTexts_[i].skillName = u["skillName"].AsString(unitTexts_[i].skillName);
			unitTexts_[i].description = u["description"].AsString(unitTexts_[i].description);
		}
	}
}

void LoadoutScreen::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["color"] = MakeVector4(color_);
	node["dimColor"] = MakeVector4(dimColor_);
	node["upColor"] = MakeVector4(upColor_);
	node["downColor"] = MakeVector4(downColor_);
	node["backdropColor"] = MakeVector4(backdropColor_);
	write("textGlow", textGlow_);
	write("lineGlow", lineGlow_);
	write("marginX", marginX_);
	write("topY", topY_);
	write("leftWidth", leftWidth_);
	write("rightWidth", rightWidth_);
	write("tabHeight", tabHeight_);
	write("itemHeight", itemHeight_);
	write("itemGap", itemGap_);
	write("panelStagger", panelStagger_);
	write("headerLineRightInset", headerLineRightInset_);
	JsonValue units = JsonValue::MakeArray();
	for (const UnitText& t : unitTexts_) {
		JsonValue u = JsonValue::MakeObject();
		u["name"] = JsonValue(t.name);
		u["skillName"] = JsonValue(t.skillName);
		u["description"] = JsonValue(t.description);
		units.Push(u);
	}
	node["flightUnits"] = units;
}

void LoadoutScreen::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults##LoadoutScreen")) ResetToDefaults();
	ImGui::ColorEdit4("Color##LoadoutScreen", &color_.x);
	ImGui::ColorEdit4("Dim Color##LoadoutScreen", &dimColor_.x);
	ImGui::ColorEdit4("Up Color", &upColor_.x);
	ImGui::ColorEdit4("Down Color", &downColor_.x);
	ImGui::ColorEdit4("Backdrop Color", &backdropColor_.x);
	ImGui::DragFloat("Text Glow##LoadoutScreen", &textGlow_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Line Glow##LoadoutScreen", &lineGlow_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Margin X", &marginX_, 1.0f, 0.0f, 400.0f);
	ImGui::DragFloat("Top Y", &topY_, 1.0f, 0.0f, 400.0f);
	ImGui::DragFloat("Left Width", &leftWidth_, 1.0f, 100.0f, 900.0f);
	ImGui::DragFloat("Right Width", &rightWidth_, 1.0f, 100.0f, 900.0f);
	ImGui::DragFloat("Tab Height", &tabHeight_, 0.5f, 16.0f, 200.0f);
	ImGui::DragFloat("Item Height", &itemHeight_, 0.5f, 16.0f, 300.0f);
	ImGui::DragFloat("Item Gap", &itemGap_, 0.5f, 0.0f, 100.0f);
	ImGui::DragFloat("Panel Stagger (s)", &panelStagger_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Header Line Right Inset", &headerLineRightInset_, 1.0f, 0.0f, 1200.0f);
	ImGui::TextDisabled("Flight unit texts: edit Hub.json \"loadoutScreen.flightUnits\"");
#endif // _DEBUG
}
