#include "StageStartSkip.h"

#include "TextRenderer.h"
#include "MathUtility.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr float kGlyphHeight = 32.0f;   // 大きさ 1 の文字の高さ [px]（TextRenderer の pixelHeight）

	Vector4 WithAlpha(const Vector4& c, float alpha) { return { c.x, c.y, c.z, c.w * alpha }; }

	void ReadVector4(const JsonValue& node, Vector4& value) {
		if (!node.IsArray() || node.Size() < 4) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)), static_cast<float>(node[3].AsDouble(value.w)) };
	}
	JsonValue MakeVector4(const Vector4& v) {
		JsonValue node = JsonValue::MakeArray();
		node.Push(JsonValue(static_cast<double>(v.x)));
		node.Push(JsonValue(static_cast<double>(v.y)));
		node.Push(JsonValue(static_cast<double>(v.z)));
		node.Push(JsonValue(static_cast<double>(v.w)));
		return node;
	}
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
	if (margin.IsArray() && margin.Size() >= 2) {
		margin_ = { static_cast<float>(margin[0].AsDouble(margin_.x)), static_cast<float>(margin[1].AsDouble(margin_.y)) };
	}
	read("textScale", textScale_);
	read("outline", outline_);
	read("ringRadius", ringRadius_);
	ringSegments_ = static_cast<int>(node["ringSegments"].AsDouble(ringSegments_));
	read("ringDot", ringDot_);
	ReadVector4(node["color"], color_);
	ReadVector4(node["outlineColor"], outlineColor_);
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
	JsonWriter::WriteFile(kPath, node, { true, 2 });
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
	return holdSeconds_ > 1e-3f ? std::clamp(held_ / holdSeconds_, 0.0f, 1.0f) : 1.0f;
}

void StageStartSkip::Draw(float screenWidth, float alpha) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || alpha <= 0.0f) return;
	const Vector4 color = WithAlpha(color_, alpha);
	const Vector4 outline = WithAlpha(outlineColor_, alpha);
	const float textH = kGlyphHeight * textScale_;
	const float boxH = textH + 12.0f;
	// 右端にそろえる：全体の幅（キーの枠 or 円形のゲージ ＋ 間 ＋ SKIP）を先に測る
	const bool showKey = !pressed_ && GetProgress() <= 0.0f;
	const float leadW = showKey ? tr->MeasureWidth("LSHIFT", textScale_) + 20.0f : (ringRadius_ + ringDot_) * 2.0f;
	const float totalW = leadW + 16.0f + tr->MeasureWidth("SKIP", textScale_);
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
		const float keyW = tr->MeasureWidth(key, textScale_) + 20.0f;
		constexpr float kLine = 2.0f;
		rect(x, y, keyW, kLine);
		rect(x, y + boxH - kLine, keyW, kLine);
		rect(x, y, kLine, boxH);
		rect(x + keyW - kLine, y, kLine, boxH);
		tr->DrawText(key, { x + 10.0f, y + 6.0f }, textScale_, color, outline_, outline);
		x += keyW + 16.0f;
	} else {
		// 円形のゲージ：上から時計回りに埋まる。まだの所は薄く
		const float cx = x + ringRadius_ + ringDot_;
		const float cy = y + boxH * 0.5f;
		const int segments = (std::max)(ringSegments_, 4);
		const float progress = GetProgress();
		for (int i = 0; i < segments; ++i) {
			const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
			const float a = 2.0f * kPi * u;
			const float px = cx + std::sin(a) * ringRadius_;
			const float py = cy - std::cos(a) * ringRadius_;
			const bool lit = u <= progress;
			tr->DrawRect({ px - ringDot_ * 0.5f - outline_, py - ringDot_ * 0.5f - outline_ },
				{ ringDot_ + outline_ * 2.0f, ringDot_ + outline_ * 2.0f }, WithAlpha(outline, lit ? 1.0f : 0.5f));
			tr->DrawRect({ px - ringDot_ * 0.5f, py - ringDot_ * 0.5f }, { ringDot_, ringDot_ }, WithAlpha(color, lit ? 1.0f : 0.3f));
		}
		x += (ringRadius_ + ringDot_) * 2.0f + 16.0f;
	}
	tr->DrawText("SKIP", { x, y + 6.0f }, textScale_, color, outline_, outline);
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
