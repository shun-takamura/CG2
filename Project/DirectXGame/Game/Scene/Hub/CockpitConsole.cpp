#include "CockpitConsole.h"

#include "CockpitReveal.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 緑ネオンの色の初期値
	constexpr Vector4 kDefaultColor = { 0.35f, 1.0f, 0.6f, 1.0f };
	// 暗い緑の色の初期値
	constexpr Vector4 kDefaultDimColor = { 0.12f, 0.4f, 0.22f, 1.0f };
	// 文字の光の強さの初期値
	constexpr float kDefaultTextGlow = 1.6f;
	// 線の光の強さの初期値
	constexpr float kDefaultLineGlow = 1.2f;
	// メニューの中心の高さ（画面の高さに対する割合）の初期値
	constexpr float kDefaultMenuCenterY = 0.54f;
	// メニューの文字の大きさの初期値
	constexpr float kDefaultMenuScale = 1.4f;
	// メニューの行の高さの初期値 [px]
	constexpr float kDefaultMenuLineHeight = 64.0f;
	// メニューの枠の幅（画面の幅に対する割合）の初期値
	constexpr float kDefaultPanelWidth = 0.34f;
	// 項目が 1 行ずつ出る間隔の初期値 [秒]
	constexpr float kDefaultItemStagger = 0.09f;
	// 中央の円の計器の半径（画面の高さに対する割合）の初期値
	constexpr float kDefaultRingRadius = 0.36f;
	// 中央の円の計器が回る速さの初期値 [rad/秒]
	constexpr float kDefaultRingSpeed = 0.15f;
	// 切れる演出の前半（縦に潰れる）が終わる進み（0〜1）
	constexpr float kCollapseSquashEnd = 0.6f;
	// 切れる演出の後半（横線が縮む）の長さ（進み 0〜1）
	constexpr float kCollapseShrinkSpan = 0.4f;
	// 縦に潰れ切ったときの縦の倍率
	constexpr float kCollapsedScaleY = 0.004f;
	// 横線が縮み切ったときの横の倍率の下限
	constexpr float kMinCollapseScaleX = 0.002f;
	// 縦に潰れるときに明るさに足す量
	constexpr float kCollapseBrightnessBoost = 1.5f;
	// 潰れていく間の横線の幅（画面の幅に対する割合）
	constexpr float kCollapseLineWidthRatio = 0.92f;
	// 潰れていく間の横線の最小の太さ [px]
	constexpr float kCollapseLineMinHeight = 3.0f;
	// 潰れ始めの横線に足す太さ [px]
	constexpr float kCollapseLineExtraHeight = 4.0f;
	// 横線が縮む間に薄くする割合
	constexpr float kCollapseLineShrinkFade = 0.3f;
	// 潰れていく間の横線の光の強さ
	constexpr float kCollapseLineGlow = 2.5f;
	// 最後の点の縮み始めに足す大きさ [px]
	constexpr float kCollapseDotExtraSize = 10.0f;
	// 最後の点の最小の大きさ [px]
	constexpr float kCollapseDotMinSize = 4.0f;
	// 最後の点の光の強さ
	constexpr float kCollapseDotGlow = 4.0f;
	// 背景の枠の線の不透明度
	constexpr float kBackgroundLineAlpha = 0.8f;
	// 背景の薄い飾りの不透明度
	constexpr float kBackgroundFaintAlpha = 0.45f;
	// 枠の線の左端（画面の幅に対する割合）。飾りの文字の左端も同じ
	constexpr float kFrameLeft = 0.03f;
	// 枠の線の中央の段の下がり始め（画面の幅に対する割合）
	constexpr float kFrameStepStart = 0.30f;
	// 枠の線の中央の段の左端（画面の幅に対する割合）
	constexpr float kFrameInnerLeft = 0.32f;
	// 枠の線の中央の段の右端（画面の幅に対する割合）
	constexpr float kFrameInnerRight = 0.68f;
	// 枠の線の中央の段の上がり終わり（画面の幅に対する割合）
	constexpr float kFrameStepEnd = 0.70f;
	// 枠の線の右端（画面の幅に対する割合）
	constexpr float kFrameRight = 0.97f;
	// 上の枠の線の高さ（画面の高さに対する割合）
	constexpr float kTopFrameY = 0.075f;
	// 枠の線の中央を内側へ下げる量 [px]
	constexpr float kFrameInwardStep = 10.0f;
	// 下の枠の線の高さ（画面の高さに対する割合）
	constexpr float kBottomFrameY = 0.925f;
	// 外の輪の 1 区切りの点の数
	constexpr int kOuterDotsPerSegment = 30;
	// 外の輪の 1 区切りのうち描く点の数（残りが切れ目）
	constexpr int kOuterDotsShownPerSegment = 26;
	// 外の輪で大きい点を置く間隔 [点]
	constexpr int kOuterMajorDotInterval = 10;
	// 外の輪の大きい点の大きさ [px]
	constexpr float kOuterMajorDotSize = 5.0f;
	// 外の輪の小さい点の大きさ [px]
	constexpr float kOuterMinorDotSize = 3.0f;
	// 外の輪の点の光の強さ
	constexpr float kOuterDotGlow = 0.8f;
	// 内の輪の半径（外の輪に対する割合）
	constexpr float kInnerRingRadiusRatio = 0.84f;
	// 内の輪の回る速さ（外の輪に対する割合）
	constexpr float kInnerRingSpeedRatio = 0.6f;
	// 内の輪の点の大きさ [px]
	constexpr Vector2 kInnerDotSize = { 2.0f, 2.0f };
	// 左の目盛りの外端（輪の半径に対する割合）
	constexpr float kTickLeftStart = 1.16f;
	// 左右の目盛りの長さ（輪の半径に対する割合）
	constexpr float kTickHorizontalLength = 0.12f;
	// 目盛りの光の強さ
	constexpr float kTickGlow = 0.6f;
	// 右・下の目盛りの内端（輪の半径に対する割合）
	constexpr float kTickNearStart = 1.04f;
	// 上の目盛りの外端（輪の半径に対する割合）
	constexpr float kTickTopStart = 1.12f;
	// 上下の目盛りの長さ（輪の半径に対する割合）
	constexpr float kTickVerticalLength = 0.08f;
	// 飾りの文字の大きさ
	constexpr float kDecorLabelScale = 0.5f;
	// 上の飾りの文字を上の枠の線からどれだけ下に置くか [px]
	constexpr float kDecorLabelBelowFrame = 10.0f;
	// 右上の飾りの文字の左端（画面の幅に対する割合）
	constexpr float kDecorRightLabelX = 0.80f;
	// 下の飾りの文字を下の枠の線からどれだけ上に置くか [px]
	constexpr float kDecorLabelAboveFrame = 26.0f;
	// 時刻の飾りの文字の左端（画面の幅に対する割合）
	constexpr float kTimeCodeX = 0.90f;
	// メニューの枠がチカチカして出る長さ [秒]
	constexpr float kPanelFlickerSeconds = 0.2f;
	// メニューの枠の高さに足す上下の余白の合計 [px]
	constexpr float kPanelExtraHeight = 70.0f;
	// メニューの枠を下へずらす量 [px]
	constexpr float kPanelOffsetY = 8.0f;
	// メニューの枠の薄い塗りの赤
	constexpr float kPanelFillRed = 0.01f;
	// メニューの枠の薄い塗りの緑
	constexpr float kPanelFillGreen = 0.05f;
	// メニューの枠の薄い塗りの青
	constexpr float kPanelFillBlue = 0.03f;
	// メニューの枠の薄い塗りの不透明度
	constexpr float kPanelFillAlpha = 0.55f;
	// メニューの枠の細い線の太さ [px]
	constexpr float kPanelLineThickness = 1.5f;
	// 見出しの札の幅（枠の幅に対する割合）
	constexpr float kTabWidthRatio = 0.4f;
	// 見出しの札の高さ [px]
	constexpr float kTabHeight = 24.0f;
	// 見出しの札の不透明度
	constexpr float kTabAlpha = 0.8f;
	// 見出しの札の光の強さ
	constexpr float kTabGlow = 0.6f;
	// 見出しの文字の大きさ
	constexpr float kHeadingScale = 0.6f;
	// 見出しの文字（札の上の暗い緑）の緑
	constexpr float kHeadingTextGreen = 0.08f;
	// 見出しの文字（札の上の暗い緑）の青
	constexpr float kHeadingTextBlue = 0.04f;
	// 枠が出てから最初の項目が出るまで [秒]
	constexpr float kItemAppearDelay = 0.12f;
	// 項目がチカチカして出る長さ [秒]
	constexpr float kItemFlickerSeconds = 0.15f;
	// 選んだ項目の帯の左の余白 [px]
	constexpr float kHighlightInsetX = 12.0f;
	// 選んだ項目の帯を文字から上へずらす量 [px]
	constexpr float kHighlightOffsetY = 4.0f;
	// 選んだ項目の帯を枠の幅から縮める量（左右の余白の合計）[px]
	constexpr float kHighlightWidthTrim = 24.0f;
	// 選んだ項目の帯の高さ（行の高さに対する割合）
	constexpr float kHighlightHeightRatio = 0.78f;
	// 選んだ項目の帯の不透明度
	constexpr float kHighlightAlpha = 0.16f;
	// 選んだ項目の帯の光の強さ
	constexpr float kHighlightGlow = 0.15f;
	// 選んでいない項目の文字の不透明度
	constexpr float kUnselectedAlpha = 0.75f;
	// 選んでいない項目の文字の光の強さ（選んだ項目に対する割合）
	constexpr float kUnselectedGlowRatio = 0.35f;
	// LOCKED を枠の右端からどれだけ左に置くか [px]
	constexpr float kLockedLabelOffsetX = 90.0f;
	// LOCKED を項目の文字からどれだけ下に置くか [px]
	constexpr float kLockedLabelOffsetY = 14.0f;
	// LOCKED の文字の大きさ
	constexpr float kLockedLabelScale = 0.5f;

	enum ConsoleItem : int {
		kItemTutorial = 0,
		kItemStage1,
		kItemLoadout,
		kItemBackToHub,
	};

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
}

CockpitConsole::CockpitConsole() {
	ResetToDefaults();
}

void CockpitConsole::ResetToDefaults() {
	color_ = kDefaultColor;
	dimColor_ = kDefaultDimColor;
	textGlow_ = kDefaultTextGlow;
	lineGlow_ = kDefaultLineGlow;
	menuCenterY_ = kDefaultMenuCenterY;
	menuScale_ = kDefaultMenuScale;
	menuLineHeight_ = kDefaultMenuLineHeight;
	panelWidth_ = kDefaultPanelWidth;
	itemStagger_ = kDefaultItemStagger;
	ringRadius_ = kDefaultRingRadius;
	ringSpeed_ = kDefaultRingSpeed;
}

void CockpitConsole::Initialize() {
	menu_.SetItems({ "チュートリアル", "ステージ1", "機体設定", "HUB に戻る" }, kItemStage1);
	// チュートリアルのステージができるまでは表示だけ（7_Tutorial.md）
	menu_.SetItemEnabled(kItemTutorial, false);
}

void CockpitConsole::PlaceMenu(const Vector2& screen) {
	menu_.SetScale(menuScale_);
	menu_.SetLineHeight(menuLineHeight_);
	menu_.SetPosition({ screen.x * 0.5f, screen.y * menuCenterY_ });
}

CockpitConsole::Result CockpitConsole::Update(InputActionMap* actions, const UIPointer* pointer,
	const Vector2& screenSize, bool inputEnabled) {
	PlaceMenu(screenSize);
	if (!inputEnabled || !actions) return Result::None;

	const VerticalMenu::Result result = menu_.Update(actions, pointer);
	if (result == VerticalMenu::Result::Canceled) return Result::BackToHub;
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kItemStage1:    return Result::Stage1;
		case kItemLoadout:   return Result::Loadout;
		case kItemBackToHub: return Result::BackToHub;
		default: break;
		}
	}
	return Result::None;
}

void CockpitConsole::SelectItem(Result item, bool playSound) {
	const int index = (item == Result::Stage1) ? kItemStage1 : (item == Result::Loadout) ? kItemLoadout : kItemBackToHub;
	if (menu_.GetSelectedIndex() == index) return;
	menu_.SetSelectedIndex(index);
	if (playSound) SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
}

void CockpitConsole::Text(std::string_view text, const Vector2& pos, float scale, const Vector4& color, float glowStrength) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector4 c = { color.x * brightness_, color.y * brightness_, color.z * brightness_, color.w };
	tr->DrawText(text, pos, scale, c);
	if (glow_ && glowStrength > 0.0f) {
		tr->DrawTextGlow(text, pos, scale, { c.x * glowStrength, c.y * glowStrength, c.z * glowStrength, c.w });
	}
}

void CockpitConsole::Rect(const Vector2& pos, const Vector2& size, const Vector4& color, float glowStrength) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector4 c = { color.x * brightness_, color.y * brightness_, color.z * brightness_, color.w };
	tr->DrawRect(pos, size, c);
	if (glow_ && glowStrength > 0.0f) {
		tr->DrawRectGlow(pos, size, { c.x * glowStrength, c.y * glowStrength, c.z * glowStrength, c.w });
	}
}

void CockpitConsole::Draw(const Vector2& screenSize, float backgroundAlpha, float menuTime, float collapse,
	float time, bool glow) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || collapse >= 1.0f) return;
	glow_ = glow;
	PlaceMenu(screenSize);

	const Vector2 center = { screenSize.x * 0.5f, screenSize.y * 0.5f };
	// ブラウン管が切れるように：前半で縦に潰れて横線になり、後半で横線が縮んで点になる
	const float squash = Clamp01(collapse / kCollapseSquashEnd);
	const float shrink = Clamp01((collapse - kCollapseSquashEnd) / kCollapseShrinkSpan);
	const float scaleY = 1.0f + (kCollapsedScaleY - 1.0f) * squash * squash;
	const float scaleX = (std::max)(1.0f - shrink * shrink, kMinCollapseScaleX);
	brightness_ = 1.0f + kCollapseBrightnessBoost * squash;
	if (collapse > 0.0f) {
		tr->SetTransform(center, { scaleX, scaleY });
	}

	if (backgroundAlpha > 0.0f) DrawBackground(screenSize, backgroundAlpha, time);
	if (menuTime >= 0.0f) DrawMenu(screenSize, menuTime);
	tr->ResetTransform();

	// 潰れていく間の横線と、最後の点
	if (collapse > 0.0f) {
		const float lineWidth = screenSize.x * kCollapseLineWidthRatio * scaleX;
		const float lineHeight = kCollapseLineMinHeight + kCollapseLineExtraHeight * (1.0f - squash);
		const float alpha = squash * (1.0f - shrink * kCollapseLineShrinkFade);
		Rect({ center.x - lineWidth * 0.5f, center.y - lineHeight * 0.5f }, { lineWidth, lineHeight },
			WithAlpha(color_, alpha), kCollapseLineGlow);
		if (shrink > 0.0f) {
			const float dot = kCollapseDotExtraSize * (1.0f - shrink) + kCollapseDotMinSize;
			Rect({ center.x - dot * 0.5f, center.y - dot * 0.5f }, { dot, dot }, WithAlpha(color_, 1.0f - shrink), kCollapseDotGlow);
		}
	}
	brightness_ = 1.0f;
}

void CockpitConsole::DrawBackground(const Vector2& screen, float alpha, float time) {
	const float w = screen.x;
	const float h = screen.y;
	const Vector4 line = WithAlpha(color_, kBackgroundLineAlpha * alpha);
	const Vector4 faint = WithAlpha(color_, kBackgroundFaintAlpha * alpha);
	const Vector4 dim = WithAlpha(dimColor_, alpha);
	constexpr float kThick = 2.0f;

	// 上下の枠の線：中央だけ内側へ段になって下がる（台形の出っ張り）
	auto drawFrame = [&](float y, float inward) {
		const float x0 = w * kFrameLeft, x1 = w * kFrameStepStart, x2 = w * kFrameInnerLeft, x3 = w * kFrameInnerRight, x4 = w * kFrameStepEnd, x5 = w * kFrameRight;
		Rect({ x0, y }, { x1 - x0, kThick }, line, lineGlow_);
		Rect({ x4, y }, { x5 - x4, kThick }, line, lineGlow_);
		Rect({ x2, y + inward }, { x3 - x2, kThick }, line, lineGlow_);
		// 斜めのつなぎは小さな段で
		constexpr int kSteps = 6;
		for (int i = 0; i < kSteps; ++i) {
			const float t = static_cast<float>(i) / kSteps;
			const float stepW = (x2 - x1) / kSteps + 1.0f;
			Rect({ x1 + (x2 - x1) * t, y + inward * t }, { stepW, kThick }, line, lineGlow_);
			Rect({ x4 - (x4 - x3) * t - stepW, y + inward * t }, { stepW, kThick }, line, lineGlow_);
		}
	};
	drawFrame(h * kTopFrameY, kFrameInwardStep);
	drawFrame(h * kBottomFrameY, -kFrameInwardStep);

	// 中央の円の計器：点を並べた輪を 2 本（外は回り、内は逆に回る）
	const Vector2 c = { w * 0.5f, h * 0.5f };
	const float radius = ringRadius_ * h;
	constexpr int kOuterDots = 120;
	for (int i = 0; i < kOuterDots; ++i) {
		if (i % kOuterDotsPerSegment >= kOuterDotsShownPerSegment) continue;   // 4 か所の切れ目
		const float a = static_cast<float>(i) / kOuterDots * 2.0f * kPi + time * ringSpeed_;
		const float size = (i % kOuterMajorDotInterval == 0) ? kOuterMajorDotSize : kOuterMinorDotSize;
		Rect({ c.x + std::cos(a) * radius - size * 0.5f, c.y + std::sin(a) * radius - size * 0.5f }, { size, size },
			faint, kOuterDotGlow);
	}
	constexpr int kInnerDots = 72;
	const float inner = radius * kInnerRingRadiusRatio;
	for (int i = 0; i < kInnerDots; ++i) {
		const float a = static_cast<float>(i) / kInnerDots * 2.0f * kPi - time * ringSpeed_ * kInnerRingSpeedRatio;
		Rect({ c.x + std::cos(a) * inner - 1.0f, c.y + std::sin(a) * inner - 1.0f }, kInnerDotSize, dim, 0.0f);
	}
	// 輪の外の十字の目盛り
	Rect({ c.x - radius * kTickLeftStart, c.y - 1.0f }, { radius * kTickHorizontalLength, kThick }, faint, kTickGlow);
	Rect({ c.x + radius * kTickNearStart, c.y - 1.0f }, { radius * kTickHorizontalLength, kThick }, faint, kTickGlow);
	Rect({ c.x - 1.0f, c.y - radius * kTickTopStart }, { kThick, radius * kTickVerticalLength }, faint, kTickGlow);
	Rect({ c.x - 1.0f, c.y + radius * kTickNearStart }, { kThick, radius * kTickVerticalLength }, faint, kTickGlow);

	// 飾りの文字
	const float labelScale = kDecorLabelScale;
	Text("MAIN VISION", { w * kFrameLeft, h * kTopFrameY + kDecorLabelBelowFrame }, labelScale, faint, 0.0f);
	Text("EXTERIOR FEED // SANCTUM", { w * kDecorRightLabelX, h * kTopFrameY + kDecorLabelBelowFrame }, labelScale, faint, 0.0f);
	Text("SYNC 100%  LINK STABLE", { w * kFrameLeft, h * kBottomFrameY - kDecorLabelAboveFrame }, labelScale, faint, 0.0f);
	char timeCode[32];
	std::snprintf(timeCode, sizeof(timeCode), "T+%07.2f", time);
	Text(timeCode, { w * kTimeCodeX, h * kBottomFrameY - kDecorLabelAboveFrame }, labelScale, faint, 0.0f);
}

void CockpitConsole::DrawMenu(const Vector2& screen, float menuTime) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float panelAlpha = CockpitReveal::Flicker(menuTime, kPanelFlickerSeconds);
	if (panelAlpha <= 0.0f) return;

	const int count = menu_.GetItemCount();
	const float panelW = screen.x * panelWidth_;
	const float panelH = menuLineHeight_ * static_cast<float>(count) + kPanelExtraHeight;
	const float x0 = (screen.x - panelW) * 0.5f;
	const float y0 = screen.y * menuCenterY_ - panelH * 0.5f + kPanelOffsetY;
	const float x1 = x0 + panelW;
	const float y1 = y0 + panelH;

	// 枠：薄い塗りと細い線、四隅だけ明るい鉤
	Rect({ x0, y0 }, { panelW, panelH }, { kPanelFillRed, kPanelFillGreen, kPanelFillBlue, kPanelFillAlpha * panelAlpha }, 0.0f);
	const Vector4 frame = WithAlpha(dimColor_, panelAlpha);
	Rect({ x0, y0 }, { panelW, kPanelLineThickness }, frame, 0.0f);
	Rect({ x0, y1 - kPanelLineThickness }, { panelW, kPanelLineThickness }, frame, 0.0f);
	Rect({ x0, y0 }, { kPanelLineThickness, panelH }, frame, 0.0f);
	Rect({ x1 - kPanelLineThickness, y0 }, { kPanelLineThickness, panelH }, frame, 0.0f);
	const Vector4 corner = WithAlpha(color_, panelAlpha);
	constexpr float kCorner = 18.0f;
	constexpr float kCornerThick = 3.0f;
	for (const float cx : { x0, x1 - kCorner }) {
		for (const float cy : { y0, y1 - kCornerThick }) {
			Rect({ cx, cy }, { kCorner, kCornerThick }, corner, lineGlow_);
		}
	}
	for (const float cx : { x0, x1 - kCornerThick }) {
		for (const float cy : { y0, y1 - kCorner }) {
			Rect({ cx, cy }, { kCornerThick, kCorner }, corner, lineGlow_);
		}
	}

	// 上の見出しの札
	const float tabW = panelW * kTabWidthRatio;
	const float tabH = kTabHeight;
	Rect({ (screen.x - tabW) * 0.5f, y0 - tabH * 0.5f }, { tabW, tabH }, WithAlpha(color_, kTabAlpha * panelAlpha), kTabGlow);
	const char* heading = "SORTIE";
	const float headingScale = kHeadingScale;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	Text(heading, { (screen.x - headingW) * 0.5f, y0 - tabH * 0.5f + 1.0f }, headingScale,
		{ 0.0f, kHeadingTextGreen, kHeadingTextBlue, panelAlpha }, 0.0f);

	// 項目：1 行ずつチラついて出る
	for (int i = 0; i < count; ++i) {
		const float appear = CockpitReveal::Flicker(menuTime - kItemAppearDelay - itemStagger_ * static_cast<float>(i), kItemFlickerSeconds);
		if (appear <= 0.0f) continue;
		const bool selected = (i == menu_.GetSelectedIndex());
		const bool enabled = menu_.IsItemEnabled(i);
		const Vector2 pos = menu_.GetItemDrawPosition(i);
		if (selected && enabled) {
			Rect({ x0 + kHighlightInsetX, pos.y - kHighlightOffsetY }, { panelW - kHighlightWidthTrim, menuLineHeight_ * kHighlightHeightRatio },
				WithAlpha(color_, kHighlightAlpha * appear), kHighlightGlow);
		}
		const Vector4 textColor = !enabled ? WithAlpha(dimColor_, appear)
			: WithAlpha(color_, (selected ? 1.0f : kUnselectedAlpha) * appear);
		const float glow = !enabled ? 0.0f : (selected ? textGlow_ : textGlow_ * kUnselectedGlowRatio);
		Text(menu_.GetItemLabel(i), pos, menuScale_, textColor, glow);
		if (!enabled) {
			Text("LOCKED", { x1 - kLockedLabelOffsetX, pos.y + kLockedLabelOffsetY }, kLockedLabelScale, WithAlpha(dimColor_, appear), 0.0f);
		}
	}
}

void CockpitConsole::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	color_ = ReadVector4(node["color"], color_);
	dimColor_ = ReadVector4(node["dimColor"], dimColor_);
	readFloat("textGlow", textGlow_);
	readFloat("lineGlow", lineGlow_);
	readFloat("menuCenterY", menuCenterY_);
	readFloat("menuScale", menuScale_);
	readFloat("menuLineHeight", menuLineHeight_);
	readFloat("panelWidth", panelWidth_);
	readFloat("itemStagger", itemStagger_);
	readFloat("ringRadius", ringRadius_);
	readFloat("ringSpeed", ringSpeed_);
}

void CockpitConsole::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["color"] = MakeVector4(color_);
	node["dimColor"] = MakeVector4(dimColor_);
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("textGlow", textGlow_);
	write("lineGlow", lineGlow_);
	write("menuCenterY", menuCenterY_);
	write("menuScale", menuScale_);
	write("menuLineHeight", menuLineHeight_);
	write("panelWidth", panelWidth_);
	write("itemStagger", itemStagger_);
	write("ringRadius", ringRadius_);
	write("ringSpeed", ringSpeed_);
}

void CockpitConsole::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();
	ImGui::ColorEdit4("Color", &color_.x);
	ImGui::ColorEdit4("Dim Color", &dimColor_.x);
	ImGui::DragFloat("Text Glow", &textGlow_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Line Glow", &lineGlow_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Menu Center Y", &menuCenterY_, 0.005f, 0.1f, 0.9f);
	ImGui::DragFloat("Menu Scale", &menuScale_, 0.01f, 0.3f, 4.0f);
	ImGui::DragFloat("Menu Line Height", &menuLineHeight_, 0.5f, 16.0f, 200.0f);
	ImGui::DragFloat("Panel Width", &panelWidth_, 0.005f, 0.1f, 1.0f);
	ImGui::DragFloat("Item Stagger (s)", &itemStagger_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Ring Radius", &ringRadius_, 0.005f, 0.05f, 0.6f);
	ImGui::DragFloat("Ring Speed", &ringSpeed_, 0.01f, -3.0f, 3.0f);
#endif // _DEBUG
}
