#include "CockpitConsole.h"

#include "CockpitReveal.h"
#include "TextRenderer.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum ConsoleItem : int {
		kItemTutorial = 0,
		kItemStage1,
		kItemBackToHub,
	};

	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}

	Vector4 WithAlpha(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}

	JsonValue MakeVector4(const Vector4& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		a.Push(JsonValue(static_cast<double>(v.w)));
		return a;
	}
	Vector4 ReadVector4(const JsonValue& a, const Vector4& fallback) {
		if (!a.IsArray() || a.Size() < 4) return fallback;
		return { static_cast<float>(a[0].AsDouble(fallback.x)), static_cast<float>(a[1].AsDouble(fallback.y)),
			static_cast<float>(a[2].AsDouble(fallback.z)), static_cast<float>(a[3].AsDouble(fallback.w)) };
	}
}

CockpitConsole::CockpitConsole() {
	ResetToDefaults();
}

void CockpitConsole::ResetToDefaults() {
	color_ = { 0.35f, 1.0f, 0.6f, 1.0f };
	dimColor_ = { 0.12f, 0.4f, 0.22f, 1.0f };
	textGlow_ = 1.6f;
	lineGlow_ = 1.2f;
	menuCenterY_ = 0.54f;
	menuScale_ = 1.4f;
	menuLineHeight_ = 64.0f;
	panelWidth_ = 0.34f;
	itemStagger_ = 0.09f;
	ringRadius_ = 0.36f;
	ringSpeed_ = 0.15f;
}

void CockpitConsole::Initialize() {
	menu_.SetItems({ "チュートリアル", "ステージ1", "HUB に戻る" }, kItemStage1);
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
		case kItemBackToHub: return Result::BackToHub;
		default: break;
		}
	}
	return Result::None;
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
	const float squash = Clamp01(collapse / 0.6f);
	const float shrink = Clamp01((collapse - 0.6f) / 0.4f);
	const float scaleY = 1.0f + (0.004f - 1.0f) * squash * squash;
	const float scaleX = (std::max)(1.0f - shrink * shrink, 0.002f);
	brightness_ = 1.0f + 1.5f * squash;
	if (collapse > 0.0f) {
		tr->SetTransform(center, { scaleX, scaleY });
	}

	if (backgroundAlpha > 0.0f) DrawBackground(screenSize, backgroundAlpha, time);
	if (menuTime >= 0.0f) DrawMenu(screenSize, menuTime);
	tr->ResetTransform();

	// 潰れていく間の横線と、最後の点
	if (collapse > 0.0f) {
		const float lineWidth = screenSize.x * 0.92f * scaleX;
		const float lineHeight = 3.0f + 4.0f * (1.0f - squash);
		const float alpha = squash * (1.0f - shrink * 0.3f);
		Rect({ center.x - lineWidth * 0.5f, center.y - lineHeight * 0.5f }, { lineWidth, lineHeight },
			WithAlpha(color_, alpha), 2.5f);
		if (shrink > 0.0f) {
			const float dot = 10.0f * (1.0f - shrink) + 4.0f;
			Rect({ center.x - dot * 0.5f, center.y - dot * 0.5f }, { dot, dot }, WithAlpha(color_, 1.0f - shrink), 4.0f);
		}
	}
	brightness_ = 1.0f;
}

void CockpitConsole::DrawBackground(const Vector2& screen, float alpha, float time) {
	const float w = screen.x;
	const float h = screen.y;
	const Vector4 line = WithAlpha(color_, 0.8f * alpha);
	const Vector4 faint = WithAlpha(color_, 0.45f * alpha);
	const Vector4 dim = WithAlpha(dimColor_, alpha);
	constexpr float kThick = 2.0f;

	// 上下の枠の線：中央だけ内側へ段になって下がる（台形の出っ張り）
	auto drawFrame = [&](float y, float inward) {
		const float x0 = w * 0.03f, x1 = w * 0.30f, x2 = w * 0.32f, x3 = w * 0.68f, x4 = w * 0.70f, x5 = w * 0.97f;
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
	drawFrame(h * 0.075f, 10.0f);
	drawFrame(h * 0.925f, -10.0f);

	// 中央の円の計器：点を並べた輪を 2 本（外は回り、内は逆に回る）
	const Vector2 c = { w * 0.5f, h * 0.5f };
	const float radius = ringRadius_ * h;
	constexpr int kOuterDots = 120;
	for (int i = 0; i < kOuterDots; ++i) {
		if (i % 30 >= 26) continue;   // 4 か所の切れ目
		const float a = static_cast<float>(i) / kOuterDots * 2.0f * kPi + time * ringSpeed_;
		const float size = (i % 10 == 0) ? 5.0f : 3.0f;
		Rect({ c.x + std::cos(a) * radius - size * 0.5f, c.y + std::sin(a) * radius - size * 0.5f }, { size, size },
			faint, 0.8f);
	}
	constexpr int kInnerDots = 72;
	const float inner = radius * 0.84f;
	for (int i = 0; i < kInnerDots; ++i) {
		const float a = static_cast<float>(i) / kInnerDots * 2.0f * kPi - time * ringSpeed_ * 0.6f;
		Rect({ c.x + std::cos(a) * inner - 1.0f, c.y + std::sin(a) * inner - 1.0f }, { 2.0f, 2.0f }, dim, 0.0f);
	}
	// 輪の外の十字の目盛り
	Rect({ c.x - radius * 1.16f, c.y - 1.0f }, { radius * 0.12f, kThick }, faint, 0.6f);
	Rect({ c.x + radius * 1.04f, c.y - 1.0f }, { radius * 0.12f, kThick }, faint, 0.6f);
	Rect({ c.x - 1.0f, c.y - radius * 1.12f }, { kThick, radius * 0.08f }, faint, 0.6f);
	Rect({ c.x - 1.0f, c.y + radius * 1.04f }, { kThick, radius * 0.08f }, faint, 0.6f);

	// 飾りの文字
	const float labelScale = 0.5f;
	Text("MAIN VISION", { w * 0.03f, h * 0.075f + 10.0f }, labelScale, faint, 0.0f);
	Text("EXTERIOR FEED // SANCTUM", { w * 0.80f, h * 0.075f + 10.0f }, labelScale, faint, 0.0f);
	Text("SYNC 100%  LINK STABLE", { w * 0.03f, h * 0.925f - 26.0f }, labelScale, faint, 0.0f);
	char timeCode[32];
	std::snprintf(timeCode, sizeof(timeCode), "T+%07.2f", time);
	Text(timeCode, { w * 0.90f, h * 0.925f - 26.0f }, labelScale, faint, 0.0f);
}

void CockpitConsole::DrawMenu(const Vector2& screen, float menuTime) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float panelAlpha = CockpitReveal::Flicker(menuTime, 0.2f);
	if (panelAlpha <= 0.0f) return;

	const int count = menu_.GetItemCount();
	const float panelW = screen.x * panelWidth_;
	const float panelH = menuLineHeight_ * static_cast<float>(count) + 70.0f;
	const float x0 = (screen.x - panelW) * 0.5f;
	const float y0 = screen.y * menuCenterY_ - panelH * 0.5f + 8.0f;
	const float x1 = x0 + panelW;
	const float y1 = y0 + panelH;

	// 枠：薄い塗りと細い線、四隅だけ明るい鉤
	Rect({ x0, y0 }, { panelW, panelH }, { 0.01f, 0.05f, 0.03f, 0.55f * panelAlpha }, 0.0f);
	const Vector4 frame = WithAlpha(dimColor_, panelAlpha);
	Rect({ x0, y0 }, { panelW, 1.5f }, frame, 0.0f);
	Rect({ x0, y1 - 1.5f }, { panelW, 1.5f }, frame, 0.0f);
	Rect({ x0, y0 }, { 1.5f, panelH }, frame, 0.0f);
	Rect({ x1 - 1.5f, y0 }, { 1.5f, panelH }, frame, 0.0f);
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
	const float tabW = panelW * 0.4f;
	const float tabH = 24.0f;
	Rect({ (screen.x - tabW) * 0.5f, y0 - tabH * 0.5f }, { tabW, tabH }, WithAlpha(color_, 0.8f * panelAlpha), 0.6f);
	const char* heading = "SORTIE";
	const float headingScale = 0.6f;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	Text(heading, { (screen.x - headingW) * 0.5f, y0 - tabH * 0.5f + 1.0f }, headingScale,
		{ 0.0f, 0.08f, 0.04f, panelAlpha }, 0.0f);

	// 項目：1 行ずつチラついて出る
	for (int i = 0; i < count; ++i) {
		const float appear = CockpitReveal::Flicker(menuTime - 0.12f - itemStagger_ * static_cast<float>(i), 0.15f);
		if (appear <= 0.0f) continue;
		const bool selected = (i == menu_.GetSelectedIndex());
		const bool enabled = menu_.IsItemEnabled(i);
		const Vector2 pos = menu_.GetItemDrawPosition(i);
		if (selected && enabled) {
			Rect({ x0 + 12.0f, pos.y - 4.0f }, { panelW - 24.0f, menuLineHeight_ * 0.78f },
				WithAlpha(color_, 0.16f * appear), 0.15f);
		}
		const Vector4 textColor = !enabled ? WithAlpha(dimColor_, appear)
			: WithAlpha(color_, (selected ? 1.0f : 0.75f) * appear);
		const float glow = !enabled ? 0.0f : (selected ? textGlow_ : textGlow_ * 0.35f);
		Text(menu_.GetItemLabel(i), pos, menuScale_, textColor, glow);
		if (!enabled) {
			Text("LOCKED", { x1 - 90.0f, pos.y + 14.0f }, 0.5f, WithAlpha(dimColor_, appear), 0.0f);
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
