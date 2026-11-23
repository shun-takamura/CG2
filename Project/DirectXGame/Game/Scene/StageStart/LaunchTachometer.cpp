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
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 点滅の明るさの真ん中
	constexpr float kBlinkMid = 0.75f;
	// 点滅の明るさの振れ幅
	constexpr float kBlinkAmplitude = 0.25f;
	// 座った目からタコメーターまでのずれの初期値 [m]
	constexpr Vector3 kDefaultOffsetFromEye = { 0.27f, -0.17f, 0.62f };
	// タコメーターの傾きの初期値 [度]
	constexpr Vector3 kDefaultRotationDegrees = { 10.0f, 18.0f, 0.0f };
	// タコメーターの幅の初期値 [m]
	constexpr float kDefaultWidth = 0.36f;
	// 三人称でタコメーターを置く画面の位置の初期値
	constexpr Vector2 kDefaultPanelScreenAnchor = { 0.62f, -0.5f };
	// 三人称でタコメーターを置くカメラからの距離の初期値 [m]
	constexpr float kDefaultPanelDistance = 3.0f;
	// 三人称でタコメーターに足す傾きの初期値 [度]
	constexpr Vector3 kDefaultPanelAngleOffsetDegrees = { 0.0f, 10.0f, 0.0f };
	// 三人称でのタコメーターの幅（画面に対する割合）の初期値
	constexpr float kDefaultPanelScreenWidth = 0.2f;
	// タコメーターの発光の初期値
	constexpr float kDefaultEmissive = 0.06f;
	// タコメーターのブルームの倍率の初期値
	constexpr float kDefaultBloomScale = 1.2f;
	// タコメーターがチカチカして出る長さの初期値 [秒]
	constexpr float kDefaultAppearSeconds = 0.35f;
	// 地の不透明度の初期値
	constexpr float kDefaultBackgroundAlpha = 0.85f;
	// LED のセグメントの数の初期値
	constexpr int kDefaultSegmentCount = 36;
	// 弧の広がりの初期値 [度]
	constexpr float kDefaultSweepDegrees = 270.0f;
	// 緑の初期値
	constexpr Vector4 kDefaultGreen = { 0.35f, 1.0f, 0.6f, 1.0f };
	// 暗い緑の初期値
	constexpr Vector4 kDefaultDimGreen = { 0.12f, 0.45f, 0.25f, 1.0f };
	// 黄色の初期値
	constexpr Vector4 kDefaultYellow = { 1.0f, 0.82f, 0.2f, 1.0f };
	// 赤の初期値
	constexpr Vector4 kDefaultRed = { 1.0f, 0.25f, 0.38f, 1.0f };
	// 黄色になる出力の初期値
	constexpr float kDefaultYellowFrom = 0.65f;
	// 地の赤
	constexpr float kBackgroundRed = 0.01f;
	// 地の緑
	constexpr float kBackgroundGreen = 0.05f;
	// 地の青
	constexpr float kBackgroundBlue = 0.03f;
	// 外の枠の太さ [px]
	constexpr float kFrameThickness = 1.5f;
	// THRUST の上端 [px]
	constexpr float kTitleY = 18.0f;
	// THRUST の文字の大きさ
	constexpr float kTitleScale = 1.1f;
	// 単位の文字の大きさ
	constexpr float kUnitLabelScale = 0.5f;
	// 単位の文字の上端 [px]
	constexpr float kUnitLabelY = 30.0f;
	// 真上の角度 [度]（弧はここを中心に左右へ広がる）
	constexpr float kArcUpDegrees = 90.0f;
	// LED のセグメントの最小の数
	constexpr int kMinSegments = 4;
	// 点いていないセグメントの不透明度
	constexpr float kUnlitSegmentAlpha = 0.35f;
	// 弧の外側の四角まで並べるための余裕 [px]
	constexpr float kArcLoopEpsilon = 0.1f;
	// 半径方向に並べた四角どうしのすき間 [px]
	constexpr float kBlockGap = 3.0f;
	// 目盛りの数字 1 あたりの回転数 [rpm]
	constexpr float kRpmPerScaleUnit = 1000.0f;
	// 目盛りの数字を置く間隔（2000 rpm ごと）
	constexpr int kScaleLabelStep = 2;
	// 目盛りの数字を弧の内側からどれだけ内に置くか [px]
	constexpr float kScaleLabelInset = 34.0f;
	// 目盛りの数字の大きさ
	constexpr float kScaleLabelScale = 0.55f;
	// 針が振り切れる出力の上限
	constexpr float kMaxNeedleValue = 1.05f;
	// 針の点を並べ始める半径 [px]
	constexpr float kNeedleStartRadius = 26.0f;
	// 針の点を弧の内側からどれだけ手前で止めるか [px]
	constexpr float kNeedleEndInset = 58.0f;
	// 針の点の間隔 [px]
	constexpr float kNeedleDotSpacing = 9.0f;
	// この半径より内側の針の点を大きくする [px]
	constexpr float kNeedleThickRadius = 60.0f;
	// 針の内側の点の大きさ [px]
	constexpr float kNeedleThickDotSize = 8.0f;
	// 針の外側の点の大きさ [px]
	constexpr float kNeedleThinDotSize = 6.0f;
	// 中心の軸の半分の大きさ [px]
	constexpr float kHubHalfSize = 14.0f;
	// 中心の軸の大きさ [px]
	constexpr float kHubSize = 28.0f;
	// 中心の軸・下の段の枠の太さ [px]
	constexpr float kThickFrameThickness = 2.0f;
	// 回転数の数字を中心からどれだけ下に置くか [px]
	constexpr float kRpmTextOffsetY = 52.0f;
	// 回転数の数字の大きさ
	constexpr float kRpmTextScale = 1.5f;
	// 回転数の数字と r/min の間 [px]
	constexpr float kRpmUnitGap = 2.0f;
	// r/min の文字の大きさ
	constexpr float kRpmUnitScale = 0.5f;
	// 割合を百分率にする倍率
	constexpr float kPercentScale = 100.0f;
	// 出力の百分率を中心からどれだけ上に置くか [px]
	constexpr float kPercentTextOffsetY = 96.0f;
	// 出力の百分率の文字の大きさ
	constexpr float kPercentTextScale = 0.8f;
	// 下の段の SPACE の案内の高さ [px]
	constexpr float kPromptBoxHeight = 64.0f;
	// STANDBY の枠の不透明度
	constexpr float kStandbyFrameAlpha = 0.8f;
	// HOLD [SPACE] がチカチカして出る長さ [秒]
	constexpr float kReadyFlickerSeconds = 0.3f;
	// HOLD [SPACE] の点滅の速さ [回/秒]
	constexpr float kReadyBlinkHz = 1.6f;
	// 下の段の文字の大きさ
	constexpr float kPromptTextScale = 1.1f;
	// 操作の案内を下の段からどれだけ上に置くか [px]
	constexpr float kHintOffsetY = 30.0f;
	// 操作の案内の文字の大きさ
	constexpr float kHintScale = 0.5f;
	// 操作の案内の不透明度
	constexpr float kHintAlpha = 0.8f;

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

	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">元の色</param>
	/// <param name="alpha">α に掛ける倍率</param>
	/// <returns>α だけ変えた色</returns>
	Vector4 WithAlpha(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}
	/// <summary>点滅の明るさを求める（0.5〜1 を cos で行き来する）</summary>
	/// <param name="time">経過 [秒]</param>
	/// <param name="hz">点滅の速さ [回/秒]</param>
	/// <returns>明るさ 0.5〜1</returns>
	float Blink(float time, float hz) {
		return kBlinkMid + kBlinkAmplitude * std::cos(time * hz * 2.0f * kPi);
	}
}

LaunchTachometer::LaunchTachometer() {
	ResetToDefaults();
}

LaunchTachometer::~LaunchTachometer() = default;

void LaunchTachometer::ResetToDefaults() {
	offsetFromEye_ = kDefaultOffsetFromEye;
	rotationDegrees_ = kDefaultRotationDegrees;
	width_ = kDefaultWidth;
	panelScreenAnchor_ = kDefaultPanelScreenAnchor;
	panelDistance_ = kDefaultPanelDistance;
	panelFaceEye_ = true;
	panelAngleOffsetDegrees_ = kDefaultPanelAngleOffsetDegrees;
	panelScreenWidth_ = kDefaultPanelScreenWidth;
	emissive_ = kDefaultEmissive;
	bloomScale_ = kDefaultBloomScale;
	appearSeconds_ = kDefaultAppearSeconds;
	backgroundAlpha_ = kDefaultBackgroundAlpha;
	segmentCount_ = kDefaultSegmentCount;
	sweepDegrees_ = kDefaultSweepDegrees;
	green_ = kDefaultGreen;
	dimGreen_ = kDefaultDimGreen;
	yellow_ = kDefaultYellow;
	red_ = kDefaultRed;
	yellowFrom_ = kDefaultYellowFrom;
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
	Rect(0.0f, 0.0f, kSize, kSize, { kBackgroundRed, kBackgroundGreen, kBackgroundBlue, backgroundAlpha_ });
	Frame(0.0f, 0.0f, kSize, kSize, kFrameThickness, dimGreen_);
	constexpr float kCorner = 26.0f;
	constexpr float kCornerThick = 4.0f;
	for (const float cx : { 0.0f, kSize - kCorner }) {
		for (const float cy : { 0.0f, kSize - kCornerThick }) Rect(cx, cy, kCorner, kCornerThick, green_);
	}
	for (const float cx : { 0.0f, kSize - kCornerThick }) {
		for (const float cy : { 0.0f, kSize - kCorner }) Rect(cx, cy, kCornerThick, kCorner, green_);
	}
	Text("THRUST", kPad, kTitleY, kTitleScale, green_);
	{
		const char* unit = "x1000 r/min";
		Text(unit, right - TextRenderer::GetInstance()->MeasureWidth(unit, kUnitLabelScale), kUnitLabelY, kUnitLabelScale, dimGreen_);
	}

	// 出力 v（0..1）の弧の角度：左下（225°）から右下（−45°）へ時計回り。画面の y は下向き
	const float startDeg = kArcUpDegrees + sweepDegrees_ * 0.5f;
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
	const int segments = (std::max)(segmentCount_, kMinSegments);
	for (int i = 0; i < segments; ++i) {
		const float v = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
		const bool lit = v <= thrust_ + 0.5f / static_cast<float>(segments) && thrust_ > 0.0f;
		const Vector4& zone = zoneColor(v);
		const Vector4 color = lit ? zone : WithAlpha(v >= redZone_ ? red_ : dimGreen_, kUnlitSegmentAlpha);
		for (float r = kArcInner; r <= kArcOuter + kArcLoopEpsilon; r += kBlock + kBlockGap) {
			const Vector2 p = pointAt(v, r);
			Rect(p.x - kBlock * 0.5f, p.y - kBlock * 0.5f, kBlock, kBlock, color);
		}
	}
	// 目盛りの数字（2000 rpm ごと）
	const int maxK = static_cast<int>(maxRpm_ / kRpmPerScaleUnit + 0.5f);
	for (int k = 0; k <= maxK; k += kScaleLabelStep) {
		const float v = static_cast<float>(k) * kRpmPerScaleUnit / maxRpm_;
		const Vector2 p = pointAt(v, kArcInner - kScaleLabelInset);
		char label[8];
		std::snprintf(label, sizeof(label), "%d", k);
		TextCentered(label, p.x, p.y - kGlyphHeight * kScaleLabelScale * 0.5f, kScaleLabelScale, v >= redZone_ ? red_ : dimGreen_);
	}

	// 針（点を並べる）と中心の軸
	{
		const float v = std::clamp(rpm_ / maxRpm_, 0.0f, kMaxNeedleValue);
		const Vector4 needle = v >= redZone_ ? red_ : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		for (float r = kNeedleStartRadius; r <= kArcInner - kNeedleEndInset; r += kNeedleDotSpacing) {
			const Vector2 p = pointAt(v, r);
			const float s = r < kNeedleThickRadius ? kNeedleThickDotSize : kNeedleThinDotSize;
			Rect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, needle);
		}
		Rect(kCenterX - kHubHalfSize, kCenterY - kHubHalfSize, kHubSize, kHubSize, dimGreen_);
		Frame(kCenterX - kHubHalfSize, kCenterY - kHubHalfSize, kHubSize, kHubSize, kThickFrameThickness, green_);
	}

	// 数字：回転数と出力
	{
		char rpm[16];
		std::snprintf(rpm, sizeof(rpm), "%5.0f", rpm_);
		const Vector4& col = thrust_ >= redZone_ ? red_ : green_;
		TextCentered(rpm, kCenterX, kCenterY + kRpmTextOffsetY, kRpmTextScale, col);
		TextCentered("r/min", kCenterX, kCenterY + kRpmTextOffsetY + kGlyphHeight * kRpmTextScale + kRpmUnitGap, kRpmUnitScale, dimGreen_);
		char percent[8];
		std::snprintf(percent, sizeof(percent), "%3d%%", static_cast<int>(thrust_ * kPercentScale + 0.5f));
		TextCentered(percent, kCenterX, kCenterY - kPercentTextOffsetY, kPercentTextScale, col);
	}

	// 下の段：SPACE の案内
	const float boxW = kSize - kPad * 2.0f;
	const float boxH = kPromptBoxHeight;
	const float boxY = kSize - kPad - boxH;
	switch (mode_) {
	case Mode::Standby: {
		Frame(kPad, boxY, boxW, boxH, kThickFrameThickness, WithAlpha(dimGreen_, kStandbyFrameAlpha));
		TextCentered("STANDBY", kCenterX, boxY + (boxH - kGlyphHeight * 1.0f) * 0.5f, 1.0f, dimGreen_);
		break;
	}
	case Mode::Ready: {
		// 充電が済んだらチラついて出る。押している間は点滅を止める
		const float on = CockpitReveal::Flicker(modeTime_, kReadyFlickerSeconds);
		const float blink = thrust_ > 0.0f ? 1.0f : Blink(time_, kReadyBlinkHz);
		Rect(kPad, boxY, boxW, boxH, WithAlpha(yellow_, on * blink));
		TextCentered("HOLD [SPACE]", kCenterX, boxY + (boxH - kGlyphHeight * kPromptTextScale) * 0.5f, kPromptTextScale, WithAlpha(kInk, on));
		Text("SPACE 長押しで出力を上げる", kPad, boxY - kHintOffsetY, kHintScale, WithAlpha(green_, on * kHintAlpha));
		break;
	}
	case Mode::Launch: {
		Rect(kPad, boxY, boxW, boxH, red_);
		TextCentered("LAUNCH", kCenterX, boxY + (boxH - kGlyphHeight * kPromptTextScale) * 0.5f, kPromptTextScale, kInk);
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
