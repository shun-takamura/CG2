#include "LaunchTachometer.h"

#include "LaunchControlBoard.h"
#include "Cockpit/CockpitModel.h"
#include "Hub/CockpitReveal.h"
#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "Material.h"
#include "RenderTexture.h"
#include "TextureManager.h"
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
	// 板ポリ（幅 2 × 高さ 1、表が -Z）。確認パネル・タイトルのロゴ・HatchSign と共有
	constexpr const char* kBoardModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kBoardMesh = "logo_plane.mesh";
	constexpr const char* kTextureName = "LaunchTachometer_RenderTexture";

	constexpr float kGlyphHeight = 32.0f;   // 大きさ 1 の文字の高さ [px]
	constexpr float kPad = 28.0f;
	// 弧の中心と半径（RenderTexture のピクセル。y は下向き）
	constexpr float kCenterX = LaunchTachometer::kSize * 0.5f;
	constexpr float kCenterY = 300.0f;
	constexpr float kArcInner = 196.0f;
	constexpr float kArcOuter = 236.0f;
	constexpr float kBlock = 10.0f;

	const Vector4 kInk = { 0.02f, 0.07f, 0.04f, 1.0f };

	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
	JsonValue MakeVector4(const Vector4& v) {
		JsonValue a = MakeVector3({ v.x, v.y, v.z });
		a.Push(JsonValue(static_cast<double>(v.w)));
		return a;
	}
	Vector3 ReadVector3(const JsonValue& a, const Vector3& fallback) {
		if (!a.IsArray() || a.Size() < 3) return fallback;
		return { static_cast<float>(a[0].AsDouble(fallback.x)), static_cast<float>(a[1].AsDouble(fallback.y)),
			static_cast<float>(a[2].AsDouble(fallback.z)) };
	}
	Vector4 ReadVector4(const JsonValue& a, const Vector4& fallback) {
		if (!a.IsArray() || a.Size() < 4) return fallback;
		return { static_cast<float>(a[0].AsDouble(fallback.x)), static_cast<float>(a[1].AsDouble(fallback.y)),
			static_cast<float>(a[2].AsDouble(fallback.z)), static_cast<float>(a[3].AsDouble(fallback.w)) };
	}
	Vector4 WithAlpha(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}
	float Blink(float time, float hz) {
		return 0.75f + 0.25f * std::cos(time * hz * 2.0f * kPi);
	}
}

LaunchTachometer::LaunchTachometer() {
	ResetToDefaults();
}

LaunchTachometer::~LaunchTachometer() = default;

void LaunchTachometer::ResetToDefaults() {
	offsetFromEye_ = { 0.27f, -0.17f, 0.62f };
	rotationDegrees_ = { 10.0f, 18.0f, 0.0f };
	width_ = 0.36f;
	panelScreenAnchor_ = { 0.62f, -0.5f };
	panelDistance_ = 3.0f;
	panelFaceEye_ = true;
	panelAngleOffsetDegrees_ = { 0.0f, 10.0f, 0.0f };
	panelScreenWidth_ = 0.2f;
	emissive_ = 0.06f;
	bloomScale_ = 1.2f;
	appearSeconds_ = 0.35f;
	backgroundAlpha_ = 0.85f;
	segmentCount_ = 36;
	sweepDegrees_ = 270.0f;
	green_ = { 0.35f, 1.0f, 0.6f, 1.0f };
	dimGreen_ = { 0.12f, 0.45f, 0.25f, 1.0f };
	yellow_ = { 1.0f, 0.82f, 0.2f, 1.0f };
	red_ = { 1.0f, 0.25f, 0.38f, 1.0f };
	yellowFrom_ = 0.65f;
}

void LaunchTachometer::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager) {
	texture_ = std::make_unique<RenderTexture>();
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	texture_->Initialize(dxCore, srvManager, static_cast<uint32_t>(kSize), static_cast<uint32_t>(kSize),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
	TextureManager::GetInstance()->RegisterExternalTexture(kTextureName, texture_->GetResource(), texture_->GetSRVIndex());

	board_ = std::make_unique<Object3DInstance>();
	board_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchTachometerBoard");
	panel_ = std::make_unique<Object3DInstance>();
	panel_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchTachometerPanel");
	appearTime_ = -1.0f;
}

void LaunchTachometer::Finalize() {
	// 板のモデルの発光・テクスチャは確認パネル（LaunchControlBoard::Finalize）が戻す
	TextureManager::GetInstance()->UnregisterExternalTexture(kTextureName);
	if (texture_) {
		texture_->Finalize();
		texture_.reset();
	}
	board_.reset();
	panel_.reset();
}

void LaunchTachometer::SetState(float thrust, float rpm, float maxRpm, float redZone, Mode mode, float modeTime, float appearTime) {
	thrust_ = std::clamp(thrust, 0.0f, 1.0f);
	rpm_ = rpm;
	maxRpm_ = (std::max)(maxRpm, 1.0f);
	redZone_ = redZone;
	mode_ = mode;
	modeTime_ = modeTime;
	appearTime_ = appearTime;
}

void LaunchTachometer::Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye,
	const Vector3& thirdPersonTarget, float fovY, float aspect) {
	time_ += deltaTime;
	if (!board_ || !panel_) return;
	// 板ポリは 2 × 1。中身は正方形なので高さ＝幅
	const Vector3 eye = CockpitModel::GetSeatedEye();
	board_->SetTranslate({ mechOrigin.x + eye.x + offsetFromEye_.x, mechOrigin.y + eye.y + offsetFromEye_.y,
		mechOrigin.z + eye.z + offsetFromEye_.z });
	board_->SetRotate({ DegToRad(rotationDegrees_.x), DegToRad(rotationDegrees_.y), DegToRad(rotationDegrees_.z) });
	board_->SetScale({ width_ * 0.5f, width_ * 0.5f, 1.0f });
	board_->Update();

	LaunchControlBoard::PlaceOnScreen(*panel_, panelScreenAnchor_, panelDistance_, panelFaceEye_, panelAngleOffsetDegrees_,
		panelScreenWidth_, 1.0f, thirdPersonEye, thirdPersonTarget, fovY, aspect);
	panel_->Update();
	ApplyMaterial();
}

void LaunchTachometer::ApplyMaterial() {
	ModelInstance* model = board_ ? board_->GetModelInstance() : nullptr;
	if (!model || !model->IsGPUReady()) return;
	// 確認パネルと同じ設定（共有のモデル）。テクスチャは描く直前に貼る
	for (const RenderSubmesh& sm : model->GetSubmeshes()) {
		if (!sm.material) continue;
		sm.material->enableLighting = 0;
		sm.material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		sm.material->dissolveEnable = kDissolveOff;
	}
	model->SetEmissiveByMaterialName("", { emissive_, emissive_, emissive_ }, 1.0f);
}

void LaunchTachometer::BindTexture(Object3DInstance& target) {
	if (ModelInstance* model = target.GetModelInstance(); model && model->IsGPUReady()) model->SetTextureFilePath(kTextureName);
}

void LaunchTachometer::RenderMeterTexture(ID3D12GraphicsCommandList* commandList) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!texture_ || !tr->IsInitialized() || !IsShown()) return;
	texture_->BeginRender(commandList);
	tr->BeginOffscreen(kSize, kSize);
	DrawContents();
	tr->FlushOffscreen();
	texture_->EndRender(commandList);
}

void LaunchTachometer::Draw(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	BindTexture(*target);
	target->Draw(dxCore);
}

void LaunchTachometer::DrawBloom(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	BindTexture(*target);
	target->DrawBloomPass(dxCore, bloomScale_);
}

// ---------------------------------------------------------------- 積む（RenderTexture のピクセル）

void LaunchTachometer::Text(std::string_view text, float x, float y, float size, const Vector4& color) {
	TextRenderer::GetInstance()->DrawText(text, { x, y }, size, WithAlpha(color, alpha_));
}

void LaunchTachometer::TextCentered(std::string_view text, float cx, float y, float size, const Vector4& color) {
	const float width = TextRenderer::GetInstance()->MeasureWidth(text, size);
	Text(text, cx - width * 0.5f, y, size, color);
}

void LaunchTachometer::Rect(float x, float y, float w, float h, const Vector4& color) {
	TextRenderer::GetInstance()->DrawRect({ x, y }, { w, h }, WithAlpha(color, alpha_));
}

void LaunchTachometer::Frame(float x, float y, float w, float h, float thickness, const Vector4& color) {
	Rect(x, y, w, thickness, color);
	Rect(x, y + h - thickness, w, thickness, color);
	Rect(x, y, thickness, h, color);
	Rect(x + w - thickness, y, thickness, h, color);
}

void LaunchTachometer::DrawContents() {
	alpha_ = CockpitReveal::Flicker(appearTime_, appearSeconds_);
	if (alpha_ <= 0.0f) return;
	const float right = kSize - kPad;

	// 地・枠・四隅の鉤（表示板と同じ）
	Rect(0.0f, 0.0f, kSize, kSize, { 0.01f, 0.05f, 0.03f, backgroundAlpha_ });
	Frame(0.0f, 0.0f, kSize, kSize, 1.5f, dimGreen_);
	constexpr float kCorner = 26.0f;
	constexpr float kCornerThick = 4.0f;
	for (const float cx : { 0.0f, kSize - kCorner }) {
		for (const float cy : { 0.0f, kSize - kCornerThick }) Rect(cx, cy, kCorner, kCornerThick, green_);
	}
	for (const float cx : { 0.0f, kSize - kCornerThick }) {
		for (const float cy : { 0.0f, kSize - kCorner }) Rect(cx, cy, kCornerThick, kCorner, green_);
	}
	Text("THRUST", kPad, 18.0f, 1.1f, green_);
	{
		const char* unit = "x1000 r/min";
		Text(unit, right - TextRenderer::GetInstance()->MeasureWidth(unit, 0.5f), 30.0f, 0.5f, dimGreen_);
	}

	// 出力 v（0..1）の弧の角度：左下（225°）から右下（−45°）へ時計回り。画面の y は下向き
	const float startDeg = 90.0f + sweepDegrees_ * 0.5f;
	auto pointAt = [&](float v, float radius) {
		const float a = DegToRad(startDeg - sweepDegrees_ * v);
		return Vector2{ kCenterX + std::cos(a) * radius, kCenterY - std::sin(a) * radius };
	};
	auto zoneColor = [&](float v) -> const Vector4& {
		if (v >= redZone_) return red_;
		if (v >= yellowFrom_) return yellow_;
		return green_;
	};

	// LED のセグメント（1 つ＝半径方向に並べた小さい四角）。点いていないものは暗く
	const int segments = (std::max)(segmentCount_, 4);
	for (int i = 0; i < segments; ++i) {
		const float v = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
		const bool lit = v <= thrust_ + 0.5f / static_cast<float>(segments) && thrust_ > 0.0f;
		const Vector4& zone = zoneColor(v);
		const Vector4 color = lit ? zone : WithAlpha(v >= redZone_ ? red_ : dimGreen_, 0.35f);
		for (float r = kArcInner; r <= kArcOuter + 0.1f; r += kBlock + 3.0f) {
			const Vector2 p = pointAt(v, r);
			Rect(p.x - kBlock * 0.5f, p.y - kBlock * 0.5f, kBlock, kBlock, color);
		}
	}
	// 目盛りの数字（2000 rpm ごと）
	const int maxK = static_cast<int>(maxRpm_ / 1000.0f + 0.5f);
	for (int k = 0; k <= maxK; k += 2) {
		const float v = static_cast<float>(k) * 1000.0f / maxRpm_;
		const Vector2 p = pointAt(v, kArcInner - 34.0f);
		char label[8];
		std::snprintf(label, sizeof(label), "%d", k);
		TextCentered(label, p.x, p.y - kGlyphHeight * 0.55f * 0.5f, 0.55f, v >= redZone_ ? red_ : dimGreen_);
	}

	// 針（点を並べる）と中心の軸
	{
		const float v = std::clamp(rpm_ / maxRpm_, 0.0f, 1.05f);
		const Vector4 needle = v >= redZone_ ? red_ : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		for (float r = 26.0f; r <= kArcInner - 58.0f; r += 9.0f) {
			const Vector2 p = pointAt(v, r);
			const float s = r < 60.0f ? 8.0f : 6.0f;
			Rect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, needle);
		}
		Rect(kCenterX - 14.0f, kCenterY - 14.0f, 28.0f, 28.0f, dimGreen_);
		Frame(kCenterX - 14.0f, kCenterY - 14.0f, 28.0f, 28.0f, 2.0f, green_);
	}

	// 数字：回転数と出力
	{
		char rpm[16];
		std::snprintf(rpm, sizeof(rpm), "%5.0f", rpm_);
		const Vector4& col = thrust_ >= redZone_ ? red_ : green_;
		TextCentered(rpm, kCenterX, kCenterY + 52.0f, 1.5f, col);
		TextCentered("r/min", kCenterX, kCenterY + 52.0f + kGlyphHeight * 1.5f + 2.0f, 0.5f, dimGreen_);
		char percent[8];
		std::snprintf(percent, sizeof(percent), "%3d%%", static_cast<int>(thrust_ * 100.0f + 0.5f));
		TextCentered(percent, kCenterX, kCenterY - 96.0f, 0.8f, col);
	}

	// 下の段：SPACE の案内
	const float boxW = kSize - kPad * 2.0f;
	const float boxH = 64.0f;
	const float boxY = kSize - kPad - boxH;
	switch (mode_) {
	case Mode::Standby: {
		Frame(kPad, boxY, boxW, boxH, 2.0f, WithAlpha(dimGreen_, 0.8f));
		TextCentered("STANDBY", kCenterX, boxY + (boxH - kGlyphHeight * 1.0f) * 0.5f, 1.0f, dimGreen_);
		break;
	}
	case Mode::Ready: {
		// 充電が済んだらチラついて出る。押している間は点滅を止める
		const float on = CockpitReveal::Flicker(modeTime_, 0.3f);
		const float blink = thrust_ > 0.0f ? 1.0f : Blink(time_, 1.6f);
		Rect(kPad, boxY, boxW, boxH, WithAlpha(yellow_, on * blink));
		TextCentered("HOLD [SPACE]", kCenterX, boxY + (boxH - kGlyphHeight * 1.1f) * 0.5f, 1.1f, WithAlpha(kInk, on));
		Text("SPACE 長押しで出力を上げる", kPad, boxY - 30.0f, 0.5f, WithAlpha(green_, on * 0.8f));
		break;
	}
	case Mode::Launch: {
		Rect(kPad, boxY, boxW, boxH, red_);
		TextCentered("LAUNCH", kCenterX, boxY + (boxH - kGlyphHeight * 1.1f) * 0.5f, 1.1f, kInk);
		break;
	}
	}
}

// ---------------------------------------------------------------- 調整値

void LaunchTachometer::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	offsetFromEye_ = ReadVector3(node["offsetFromEye"], offsetFromEye_);
	rotationDegrees_ = ReadVector3(node["rotationDegrees"], rotationDegrees_);
	read("width", width_);
	{
		const Vector3 anchor = ReadVector3(node["panelScreenAnchor"], { panelScreenAnchor_.x, panelScreenAnchor_.y, 0.0f });
		panelScreenAnchor_ = { anchor.x, anchor.y };
	}
	read("panelDistance", panelDistance_);
	panelFaceEye_ = node["panelFaceEye"].AsBool(panelFaceEye_);
	read("panelScreenWidth", panelScreenWidth_);
	panelAngleOffsetDegrees_ = ReadVector3(node["panelAngleOffsetDegrees"], panelAngleOffsetDegrees_);
	read("emissive", emissive_);
	read("bloomScale", bloomScale_);
	read("appearSeconds", appearSeconds_);
	read("backgroundAlpha", backgroundAlpha_);
	segmentCount_ = static_cast<int>(node["segmentCount"].AsDouble(segmentCount_));
	read("sweepDegrees", sweepDegrees_);
	read("yellowFrom", yellowFrom_);
	green_ = ReadVector4(node["green"], green_);
	dimGreen_ = ReadVector4(node["dimGreen"], dimGreen_);
	yellow_ = ReadVector4(node["yellow"], yellow_);
	red_ = ReadVector4(node["red"], red_);
}

void LaunchTachometer::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["offsetFromEye"] = MakeVector3(offsetFromEye_);
	node["rotationDegrees"] = MakeVector3(rotationDegrees_);
	write("width", width_);
	node["panelScreenAnchor"] = MakeVector3({ panelScreenAnchor_.x, panelScreenAnchor_.y, 0.0f });
	write("panelDistance", panelDistance_);
	node["panelFaceEye"] = JsonValue(panelFaceEye_);
	write("panelScreenWidth", panelScreenWidth_);
	node["panelAngleOffsetDegrees"] = MakeVector3(panelAngleOffsetDegrees_);
	write("emissive", emissive_);
	write("bloomScale", bloomScale_);
	write("appearSeconds", appearSeconds_);
	write("backgroundAlpha", backgroundAlpha_);
	node["segmentCount"] = JsonValue(static_cast<double>(segmentCount_));
	write("sweepDegrees", sweepDegrees_);
	write("yellowFrom", yellowFrom_);
	node["green"] = MakeVector4(green_);
	node["dimGreen"] = MakeVector4(dimGreen_);
	node["yellow"] = MakeVector4(yellow_);
	node["red"] = MakeVector4(red_);
}

void LaunchTachometer::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults##tacho")) ResetToDefaults();
	ImGui::TextDisabled("1st person (floating board, right of the check panel)");
	ImGui::DragFloat3("Offset From Eye##tacho", &offsetFromEye_.x, 0.005f);
	ImGui::DragFloat3("Rotation (deg)##tacho", &rotationDegrees_.x, 0.5f);
	ImGui::DragFloat("Width (m)##tacho", &width_, 0.005f, 0.05f, 3.0f);
	ImGui::TextDisabled("3rd person (screen bottom right)");
	ImGui::DragFloat2("Panel Screen Anchor (-1..1)##tacho", &panelScreenAnchor_.x, 0.005f, -1.5f, 1.5f);
	ImGui::DragFloat("Panel Distance (m)##tacho", &panelDistance_, 0.1f, 1.0f, 200.0f);
	ImGui::Checkbox("Panel Face Eye##tacho", &panelFaceEye_);
	ImGui::DragFloat3("Panel Angle Offset (deg)##tacho", &panelAngleOffsetDegrees_.x, 0.5f);
	ImGui::DragFloat("Panel Screen Width (ratio)##tacho", &panelScreenWidth_, 0.005f, 0.05f, 1.0f);
	ImGui::TextDisabled("Common");
	ImGui::DragFloat("Emissive##tacho", &emissive_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Scale##tacho", &bloomScale_, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Appear (s)##tacho", &appearSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Background Alpha##tacho", &backgroundAlpha_, 0.01f, 0.0f, 1.0f);
	ImGui::SliderInt("Segments##tacho", &segmentCount_, 4, 80);
	ImGui::DragFloat("Sweep (deg)##tacho", &sweepDegrees_, 1.0f, 60.0f, 340.0f);
	ImGui::SliderFloat("Yellow From##tacho", &yellowFrom_, 0.0f, 1.0f);
	ImGui::ColorEdit4("Green##tacho", &green_.x);
	ImGui::ColorEdit4("Dim Green##tacho", &dimGreen_.x);
	ImGui::ColorEdit4("Yellow##tacho", &yellow_.x);
	ImGui::ColorEdit4("Red##tacho", &red_.x);
	if (texture_) {
		ImGui::Image((ImTextureID)TextureManager::GetInstance()->GetSrvHandleGPU(kTextureName).ptr, ImVec2(200.0f, 200.0f));
	}
#endif // _DEBUG
}
