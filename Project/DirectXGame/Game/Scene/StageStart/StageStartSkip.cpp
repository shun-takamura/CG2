#include "StageStartSkip.h"

#include "TextRenderer.h"
#include "MathUtility.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// Vector2 の JSON 配列に要る要素の数
	constexpr int kVector2ComponentCount = 2;
	// 調整値の JSON を書き出すときの字下げの幅 [文字]
	constexpr int kJsonIndentWidth = 2;
	// 長押しの長さの下限 [秒]（これ以下ならすぐ満ちたものとして扱う）
	constexpr float kMinHoldSeconds = 1e-3f;
	// 枠の高さに足す上下の余白の合計 [px]
	constexpr float kBoxPaddingY = 12.0f;
	// キーの枠の幅に足す左右の余白の合計 [px]
	constexpr float kKeyBoxPaddingX = 20.0f;
	// キー（または輪）と SKIP の間 [px]
	constexpr float kItemGap = 16.0f;
	// キーの文字を枠の左からどれだけ右に置くか [px]
	constexpr float kKeyTextOffsetX = 10.0f;
	// 文字を枠の上からどれだけ下に置くか [px]
	constexpr float kTextOffsetY = 6.0f;
	// 長押しの輪の点の最小の数
	constexpr int kMinRingSegments = 4;
	// まだ満ちていない輪の点の縁取りの不透明度
	constexpr float kUnlitOutlineAlpha = 0.5f;
	// まだ満ちていない輪の点の不透明度
	constexpr float kUnlitDotAlpha = 0.3f;

	constexpr float kGlyphHeight = 32.0f;   // 大きさ 1 の文字の高さ [px]（TextRenderer の pixelHeight）

	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">元の色</param>
	/// <param name="alpha">α に掛ける倍率</param>
	/// <returns>α だけ変えた色</returns>
	Vector4 WithAlpha(const Vector4& c, float alpha) { return { c.x, c.y, c.z, c.w * alpha }; }
}

StageStartSkip::StageStartSkip() {
	Load();
}

void StageStartSkip::Load() {
	if (!std::filesystem::exists(kPath)) return;
	const auto result = JsonParser::ParseFile(kPath);
	if (!result.success) return;
	const JsonValue& node = result.value;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("holdSeconds", holdSeconds_);
	const JsonValue& margin = node["margin"];
	if (margin.IsArray() && margin.Size() >= kVector2ComponentCount) {
		margin_ = { static_cast<float>(margin[0].AsDouble(margin_.x)), static_cast<float>(margin[1].AsDouble(margin_.y)) };
	}
	read("textScale", textScale_);
	read("outline", outline_);
	read("ringRadius", ringRadius_);
	ringSegments_ = static_cast<int>(node["ringSegments"].AsDouble(ringSegments_));
	read("ringDot", ringDot_);
	color_ = ReadVector4(node["color"], color_);
	outlineColor_ = ReadVector4(node["outlineColor"], outlineColor_);
}

void StageStartSkip::Save() const {
	JsonValue node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("holdSeconds", holdSeconds_);
	JsonValue margin = JsonValue::MakeArray();
	margin.Push(JsonValue(static_cast<double>(margin_.x)));
	margin.Push(JsonValue(static_cast<double>(margin_.y)));
	node["margin"] = margin;
	write("textScale", textScale_);
	write("outline", outline_);
	write("ringRadius", ringRadius_);
	node["ringSegments"] = JsonValue(static_cast<double>(ringSegments_));
	write("ringDot", ringDot_);
	node["color"] = MakeVector4(color_);
	node["outlineColor"] = MakeVector4(outlineColor_);
	JsonWriter::WriteFile(kPath, node, { true, kJsonIndentWidth });
}

bool StageStartSkip::Update(float deltaTime, bool held) {
	pressed_ = held && !done_;
	if (done_) return false;
	// 押している間だけためる。離したら最初から
	held_ = held ? held_ + deltaTime : 0.0f;
	if (held_ >= holdSeconds_) {
		done_ = true;
		return true;
	}
	return false;
}

float StageStartSkip::GetProgress() const {
	return holdSeconds_ > kMinHoldSeconds ? std::clamp(held_ / holdSeconds_, 0.0f, 1.0f) : 1.0f;
}

void StageStartSkip::Draw(float screenWidth, float alpha) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || alpha <= 0.0f) return;
	const Vector4 color = WithAlpha(color_, alpha);
	const Vector4 outline = WithAlpha(outlineColor_, alpha);
	const float textH = kGlyphHeight * textScale_;
	const float boxH = textH + kBoxPaddingY;
	// 右端にそろえる：全体の幅（キーの枠 or 円形のゲージ ＋ 間 ＋ SKIP）を先に測る
	const bool showKey = !pressed_ && GetProgress() <= 0.0f;
	const float leadW = showKey ? tr->MeasureWidth("LSHIFT", textScale_) + kKeyBoxPaddingX : (ringRadius_ + ringDot_) * 2.0f;
	const float totalW = leadW + kItemGap + tr->MeasureWidth("SKIP", textScale_);
	float x = screenWidth - margin_.x - totalW;
	const float y = margin_.y;

	// 白い四角に黒の縁取り（黒を一回り大きく先に描く）
	auto rect = [&](float rx, float ry, float rw, float rh) {
		tr->DrawRect({ rx - outline_, ry - outline_ }, { rw + outline_ * 2.0f, rh + outline_ * 2.0f }, outline);
		tr->DrawRect({ rx, ry }, { rw, rh }, color);
	};

	if (showKey) {
		// [LSHIFT] SKIP：キーの枠
		const char* key = "LSHIFT";
		const float keyW = tr->MeasureWidth(key, textScale_) + kKeyBoxPaddingX;
		constexpr float kLine = 2.0f;
		rect(x, y, keyW, kLine);
		rect(x, y + boxH - kLine, keyW, kLine);
		rect(x, y, kLine, boxH);
		rect(x + keyW - kLine, y, kLine, boxH);
		tr->DrawText(key, { x + kKeyTextOffsetX, y + kTextOffsetY }, textScale_, color, outline_, outline);
		x += keyW + kItemGap;
	} else {
		// 円形のゲージ：上から時計回りに埋まる。まだの所は薄く
		const float cx = x + ringRadius_ + ringDot_;
		const float cy = y + boxH * 0.5f;
		const int segments = (std::max)(ringSegments_, kMinRingSegments);
		const float progress = GetProgress();
		for (int i = 0; i < segments; ++i) {
			const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
			const float a = 2.0f * kPi * u;
			const float px = cx + std::sin(a) * ringRadius_;
			const float py = cy - std::cos(a) * ringRadius_;
			const bool lit = u <= progress;
			tr->DrawRect({ px - ringDot_ * 0.5f - outline_, py - ringDot_ * 0.5f - outline_ },
				{ ringDot_ + outline_ * 2.0f, ringDot_ + outline_ * 2.0f }, WithAlpha(outline, lit ? 1.0f : kUnlitOutlineAlpha));
			tr->DrawRect({ px - ringDot_ * 0.5f, py - ringDot_ * 0.5f }, { ringDot_, ringDot_ }, WithAlpha(color, lit ? 1.0f : kUnlitDotAlpha));
		}
		x += (ringRadius_ + ringDot_) * 2.0f + kItemGap;
	}
	tr->DrawText("SKIP", { x, y + kTextOffsetY }, textScale_, color, outline_, outline);
}

void StageStartSkip::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("held %.2f / %.2f s  %s", held_, holdSeconds_, done_ ? "done" : "");
	ImGui::DragFloat("Hold (s)##skip", &holdSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat2("Margin From Top Right (px)##skip", &margin_.x, 0.5f);
	ImGui::DragFloat("Text Scale##skip", &textScale_, 0.01f, 0.1f, 4.0f);
	ImGui::DragFloat("Outline (px)##skip", &outline_, 0.1f, 0.0f, 8.0f);
	ImGui::DragFloat("Ring Radius (px)##skip", &ringRadius_, 0.1f, 2.0f, 100.0f);
	ImGui::SliderInt("Ring Segments##skip", &ringSegments_, 4, 80);
	ImGui::DragFloat("Ring Dot (px)##skip", &ringDot_, 0.1f, 1.0f, 20.0f);
	ImGui::ColorEdit4("Color##skip", &color_.x);
	ImGui::ColorEdit4("Outline Color##skip", &outlineColor_.x);
	if (ImGui::Button("Save##skip")) Save();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kPath);
#endif // _DEBUG
}
