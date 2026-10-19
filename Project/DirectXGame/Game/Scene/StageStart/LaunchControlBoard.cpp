#include "LaunchControlBoard.h"

#include "LaunchChecklist.h"
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
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 板ポリ（幅 2 × 高さ 1、表が -Z）。タイトルのロゴ・HUB の HatchSign と共有
	constexpr const char* kBoardModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kBoardMesh = "logo_plane.mesh";
	// RenderTexture をモデルのテクスチャとして引く名前
	constexpr const char* kTextureName = "LaunchControlBoard_RenderTexture";

	// 板の座標の割り付け
	constexpr float kPad = 40.0f;          // 左右の余白
	constexpr float kRowTop = 118.0f;      // 1 行目の上
	constexpr float kRowHeight = 112.0f;
	constexpr float kWidgetX = 440.0f;     // 行の真ん中（キーの表示・ゲージ・ランプ）
	constexpr float kStatusW = 200.0f;     // 行の右の CLEAR の箱
	constexpr float kStatusH = 60.0f;
	// 主な文字の大きさ（2026-10-19 ユーザー指示で少し大きく）
	constexpr float kTitleSize = 1.65f;    // LAUNCH CONTROL
	constexpr float kLabelSize = 1.15f;    // 項目名
	constexpr float kStatusSize = 1.15f;   // 右の箱（ON-LINE / CHECK / CLEAR）
	constexpr float kFooterSize = 1.4f;    // 下の段（SYSTEM CHECK など）
	constexpr float kGlyphHeight = 32.0f;  // 大きさ 1 の文字の高さ [px]（TextRenderer の pixelHeight）
	constexpr float kFooterTop = kRowTop + kRowHeight * 4.0f + 10.0f;

	const Vector4 kInk = { 0.02f, 0.07f, 0.04f, 1.0f };   // 白地・黄地の上の文字

	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
	JsonValue MakeVector4(const Vector4& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
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
	Vector4 Mix(const Vector4& a, const Vector4& b, float t) {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
	}
	// 点滅（0.5〜1）
	float Blink(float time, float hz) {
		return 0.75f + 0.25f * std::cos(time * hz * 2.0f * kPi);
	}
}

LaunchControlBoard::LaunchControlBoard() {
	ResetToDefaults();
}

LaunchControlBoard::~LaunchControlBoard() = default;

void LaunchControlBoard::ResetToDefaults() {
	offsetFromEye_ = { -0.24f, -0.15f, 0.62f };
	rotationDegrees_ = { 10.0f, -14.0f, 0.0f };
	width_ = 0.62f;
	panelScreenAnchor_ = { -0.36f, 0.40f };
	panelDistance_ = 20.0f;
	panelFaceEye_ = true;
	panelAngleOffsetDegrees_ = { 0.0f, -10.0f, 0.0f };
	panelScreenWidth_ = 0.385f;
	emissive_ = 0.06f;
	bloomScale_ = 1.2f;
	appearSeconds_ = 0.35f;
	backgroundAlpha_ = 0.85f;
	green_ = { 0.35f, 1.0f, 0.6f, 1.0f };
	dimGreen_ = { 0.12f, 0.45f, 0.25f, 1.0f };
	yellow_ = { 1.0f, 0.82f, 0.2f, 1.0f };
	red_ = { 1.0f, 0.25f, 0.38f, 1.0f };
}

void LaunchControlBoard::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager) {
	texture_ = std::make_unique<RenderTexture>();
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	texture_->Initialize(dxCore, srvManager, static_cast<uint32_t>(kWidth), static_cast<uint32_t>(kHeight),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
	TextureManager::GetInstance()->RegisterExternalTexture(kTextureName, texture_->GetResource(), texture_->GetSRVIndex());

	board_ = std::make_unique<Object3DInstance>();
	board_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchControlBoard");
	panel_ = std::make_unique<Object3DInstance>();
	panel_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "LaunchControlPanel");
	checklist_ = nullptr;
	appearTime_ = -1.0f;
}

void LaunchControlBoard::Finalize() {
	// 板のモデルはタイトルのロゴ・HatchSign と共有なので発光を戻す（テクスチャは使う側が毎フレーム貼り直す）
	if (board_) {
		if (ModelInstance* model = board_->GetModelInstance()) model->ClearEmissive();
	}
	TextureManager::GetInstance()->UnregisterExternalTexture(kTextureName);
	if (texture_) {
		texture_->Finalize();
		texture_.reset();
	}
	board_.reset();
	panel_.reset();
}

void LaunchControlBoard::SetState(const LaunchChecklist* checklist, Status status, float statusTime, float appearTime,
	bool cablePurged) {
	checklist_ = checklist;
	status_ = status;
	statusTime_ = statusTime;
	appearTime_ = appearTime;
	cablePurged_ = cablePurged;
}

void LaunchControlBoard::Update(float deltaTime, const Vector3& mechOrigin, const Vector3& thirdPersonEye,
	const Vector3& thirdPersonTarget, float fovY, float aspect) {
	time_ += deltaTime;
	if (!board_ || !panel_) return;
	// 板ポリは 2 × 1。表示の縦横比に合わせる
	constexpr float kAspect = kHeight / kWidth;

	const Vector3 eye = CockpitModel::GetSeatedEye();
	board_->SetTranslate({ mechOrigin.x + eye.x + offsetFromEye_.x, mechOrigin.y + eye.y + offsetFromEye_.y,
		mechOrigin.z + eye.z + offsetFromEye_.z });
	board_->SetRotate({ DegToRad(rotationDegrees_.x), DegToRad(rotationDegrees_.y), DegToRad(rotationDegrees_.z) });
	board_->SetScale({ width_ * 0.5f, width_ * kAspect * 0.5f, 1.0f });
	board_->Update();

	// 据え付けたパネル：三人称のカメラの画面の panelScreenAnchor に見える所
	PlaceOnScreen(*panel_, panelScreenAnchor_, panelDistance_, panelFaceEye_, panelAngleOffsetDegrees_, panelScreenWidth_, kAspect,
		thirdPersonEye, thirdPersonTarget, fovY, aspect);
	panel_->Update();
	ApplyMaterial();
}

void LaunchControlBoard::PlaceOnScreen(Object3DInstance& panel, const Vector2& anchor, float distance, bool faceEye,
	const Vector3& angleOffsetDegrees, float screenWidth, float heightPerWidth,
	const Vector3& eye, const Vector3& target, float fovY, float aspect) {
	// カメラの基底（左手系。right = up × forward、up = forward × right）から、画面の anchor に見える向きへ distance 進んだ所
	const Vector3 f = Normalize(Vector3{ target.x - eye.x, target.y - eye.y, target.z - eye.z });
	const Vector3 r = Normalize(Vector3{ f.z, 0.0f, -f.x });
	const Vector3 u = { f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x };
	const float tanV = std::tan(fovY * 0.5f);
	const float sx = anchor.x * tanV * aspect;
	const float sy = anchor.y * tanV;
	const Vector3 dir = Normalize(Vector3{ f.x + r.x * sx + u.x * sy, f.y + r.y * sx + u.y * sy, f.z + r.z * sx + u.z * sy });
	panel.SetTranslate({ eye.x + dir.x * distance, eye.y + dir.y * distance, eye.z + dir.z * distance });
	// 板ポリの表 -Z をカメラの側へ。yaw 0 が +Z・pitch は正が下向き（StageStartPart::ApplyCamera と同じ）。
	// 目へ向けるなら板からカメラへの向き（-dir）、画面と平行ならカメラの前（f）を使う。そこから少しずらす
	const Vector3& face = faceEye ? dir : f;
	const float yaw = std::atan2(face.x, face.z);
	const float pitch = -std::atan2(face.y, std::sqrt(face.x * face.x + face.z * face.z));
	panel.SetRotate({ pitch + DegToRad(angleOffsetDegrees.x), yaw + DegToRad(angleOffsetDegrees.y), DegToRad(angleOffsetDegrees.z) });
	// 画面の幅（その距離で 2·tanH·距離）に対する割合からワールドの幅を出す
	const float width = screenWidth * 2.0f * tanV * aspect * distance;
	panel.SetScale({ width * 0.5f, width * heightPerWidth * 0.5f, 1.0f });
}

void LaunchControlBoard::ApplyMaterial() {
	ModelInstance* model = board_ ? board_->GetModelInstance() : nullptr;
	// テクスチャの差し替えは GPU の準備後（準備時に .mat のパスで上書きされるため）
	if (!model || !model->IsGPUReady()) return;
	if (model->GetTextureFilePath() != kTextureName) model->SetTextureFilePath(kTextureName);
	for (const RenderSubmesh& sm : model->GetSubmeshes()) {
		if (!sm.material) continue;
		// 光の当たり方で明暗が出ないよう、描いた色をそのまま出す
		sm.material->enableLighting = 0;
		sm.material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		sm.material->dissolveEnable = kDissolveOff;
	}
	// 文字の所だけ光らせる（テクスチャを掛けた発光）
	model->SetEmissiveByMaterialName("", { emissive_, emissive_, emissive_ }, 1.0f);
}

void LaunchControlBoard::RenderBoardTexture(ID3D12GraphicsCommandList* commandList) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!texture_ || !tr->IsInitialized() || !IsShown()) return;
	texture_->BeginRender(commandList);   // 透明でクリア
	tr->BeginOffscreen(kWidth, kHeight);
	DrawContents();
	tr->FlushOffscreen();
	texture_->EndRender(commandList);
}

void LaunchControlBoard::Draw(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	// 板のモデルはタコメーターと共有なので、描く直前にこの板の絵を貼る（テクスチャは描くときに引かれる）
	if (ModelInstance* model = target->GetModelInstance(); model && model->IsGPUReady()) model->SetTextureFilePath(kTextureName);
	target->Draw(dxCore);
}

void LaunchControlBoard::DrawBloom(DirectXCore* dxCore, bool firstPerson) {
	Object3DInstance* target = firstPerson ? board_.get() : panel_.get();
	if (!target || !IsShown()) return;
	if (ModelInstance* model = target->GetModelInstance(); model && model->IsGPUReady()) model->SetTextureFilePath(kTextureName);
	target->DrawBloomPass(dxCore, bloomScale_);
}

// ---------------------------------------------------------------- 積む（RenderTexture のピクセル）

void LaunchControlBoard::Text(std::string_view text, float x, float y, float size, const Vector4& color) {
	TextRenderer::GetInstance()->DrawText(text, { x, y }, size, WithAlpha(color, alpha_));
}

void LaunchControlBoard::TextCentered(std::string_view text, float cx, float y, float size, const Vector4& color) {
	const float width = TextRenderer::GetInstance()->MeasureWidth(text, size);
	Text(text, cx - width * 0.5f, y, size, color);
}

void LaunchControlBoard::Rect(float x, float y, float w, float h, const Vector4& color) {
	TextRenderer::GetInstance()->DrawRect({ x, y }, { w, h }, WithAlpha(color, alpha_));
}

void LaunchControlBoard::Frame(float x, float y, float w, float h, float thickness, const Vector4& color) {
	Rect(x, y, w, thickness, color);
	Rect(x, y + h - thickness, w, thickness, color);
	Rect(x, y, thickness, h, color);
	Rect(x + w - thickness, y, thickness, h, color);
}

void LaunchControlBoard::DrawStatusBox(float x, float y, float w, float h, bool cleared, float sinceCleared) {
	// CLEAR になった瞬間は蛍光灯のようにチラついて白地に変わる
	const float on = cleared ? CockpitReveal::Flicker(sinceCleared, 0.25f) : 0.0f;
	if (on > 0.0f) {
		Rect(x, y, w, h, WithAlpha({ 1.0f, 1.0f, 1.0f, 1.0f }, on));
		TextCentered("CLEAR", x + w * 0.5f, y + (h - kGlyphHeight * kStatusSize) * 0.5f, kStatusSize, WithAlpha(kInk, on));
	}
	if (on < 1.0f) {
		const float blink = Blink(time_, 1.2f) * (1.0f - on);
		Frame(x, y, w, h, 2.0f, WithAlpha(red_, blink));
		TextCentered("CHECK", x + w * 0.5f, y + (h - kGlyphHeight * kStatusSize) * 0.5f, kStatusSize, WithAlpha(red_, blink));
	}
}

void LaunchControlBoard::DrawContents() {
	if (!checklist_) return;
	alpha_ = CockpitReveal::Flicker(appearTime_, appearSeconds_);
	if (alpha_ <= 0.0f) return;
	const LaunchChecklist& c = *checklist_;
	const float right = kWidth - kPad;

	// 地・枠・四隅の鉤
	Rect(0.0f, 0.0f, kWidth, kHeight, { 0.01f, 0.05f, 0.03f, backgroundAlpha_ });
	Frame(0.0f, 0.0f, kWidth, kHeight, 1.5f, dimGreen_);
	constexpr float kCorner = 26.0f;
	constexpr float kCornerThick = 4.0f;
	for (const float cx : { 0.0f, kWidth - kCorner }) {
		for (const float cy : { 0.0f, kHeight - kCornerThick }) Rect(cx, cy, kCorner, kCornerThick, green_);
	}
	for (const float cx : { 0.0f, kWidth - kCornerThick }) {
		for (const float cy : { 0.0f, kHeight - kCorner }) Rect(cx, cy, kCornerThick, kCorner, green_);
	}

	// 見出し
	Text("LAUNCH CONTROL", kPad, 22.0f, kTitleSize, green_);
	const char* tag = "CATAPULT-01 // SORTIE";
	const float tagW = TextRenderer::GetInstance()->MeasureWidth(tag, 0.5f);
	Text(tag, right - tagW, 44.0f, 0.5f, dimGreen_);
	Rect(kPad, 84.0f, 150.0f, 6.0f, yellow_);
	Rect(kPad + 160.0f, 86.0f, right - kPad - 160.0f, 2.0f, green_);

	// 行
	auto rowY = [](int row) { return kRowTop + kRowHeight * static_cast<float>(row); };
	auto rowFrame = [&](int row, const char* index, const char* label, const char* help) {
		const float y = rowY(row);
		Text(index, kPad, y + 14.0f, 0.6f, dimGreen_);
		Text(label, kPad + 50.0f, y + 6.0f, kLabelSize, green_);
		Text(help, kPad + 50.0f, y + 6.0f + kGlyphHeight * kLabelSize + 8.0f, 0.55f, Mix(dimGreen_, green_, 0.4f));
		// 行の区切り（点線）
		for (float x = kPad; x < right; x += 14.0f) Rect(x, y + kRowHeight - 4.0f, 7.0f, 1.5f, dimGreen_);
	};
	auto keyHint = [&](const char* key, float x, float y, float size = 0.55f) {
		const float w = TextRenderer::GetInstance()->MeasureWidth(key, size) + 20.0f;
		const float h = kGlyphHeight * size + 12.0f;
		Frame(x, y, w, h, 1.5f, yellow_);
		Text(key, x + 10.0f, y + 6.0f, size, yellow_);
		return w;
	};
	const float statusX = right - kStatusW;
	auto statusY = [&](int row) { return rowY(row) + (kRowHeight - 4.0f - kStatusH) * 0.5f; };

	// 0. 給電ワイヤー（操作しない。黄の ON-LINE、切り離しで赤の PURGE）
	{
		const float y = rowY(0);
		rowFrame(0, "00", "POWER CABLE", "給電ワイヤー 接続中");
		for (int i = 0; i < 6; ++i) {
			const float phase = std::fmod(time_ * 2.0f + static_cast<float>(i) * 0.25f, 1.5f);
			Rect(kWidgetX + static_cast<float>(i) * 34.0f, y + 40.0f, 24.0f, 16.0f,
				WithAlpha(cablePurged_ ? red_ : yellow_, phase < 0.5f && !cablePurged_ ? 1.0f : 0.35f));
		}
		const Vector4& col = cablePurged_ ? red_ : yellow_;
		const float sy = statusY(0);
		Frame(statusX, sy, kStatusW, kStatusH, 2.0f, col);
		TextCentered(cablePurged_ ? "PURGE" : "ON-LINE", statusX + kStatusW * 0.5f, sy + (kStatusH - kGlyphHeight * kStatusSize) * 0.5f,
			kStatusSize, col);
	}

	// 1. 照準
	{
		const float y = rowY(1);
		rowFrame(1, "01", "AIM CONTROL", "マウス / 右スティックで照準を動かす");
		keyHint("MOUSE", kWidgetX, y + 8.0f);
		const float barW = 240.0f;
		Frame(kWidgetX, y + 62.0f, barW, 16.0f, 1.5f, dimGreen_);
		Rect(kWidgetX + 3.0f, y + 65.0f, (barW - 6.0f) * c.GetAimProgress(), 10.0f, green_);
		DrawStatusBox(statusX, statusY(1), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Aim),
			c.TimeSinceCleared(LaunchChecklist::Item::Aim));
	}

	// 2. スラスター（W A S D の 4 つのランプ）
	{
		const float y = rowY(2);
		rowFrame(2, "02", "THRUSTER", "W A S D / 左スティックで 4 方向");
		static const char* kKeys[LaunchChecklist::kThrusterDirCount] = { "W", "A", "S", "D" };
		for (int i = 0; i < LaunchChecklist::kThrusterDirCount; ++i) {
			const float x = kWidgetX + static_cast<float>(i) * 60.0f;
			const float by = y + 26.0f;
			if (c.IsThrusterHeld(i)) {
				Rect(x, by, 48.0f, 48.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
				TextCentered(kKeys[i], x + 24.0f, by + 8.0f, 1.0f, kInk);
			} else if (c.IsThrusterDone(i)) {
				Rect(x, by, 48.0f, 48.0f, WithAlpha(green_, 0.25f));
				Frame(x, by, 48.0f, 48.0f, 2.0f, green_);
				TextCentered(kKeys[i], x + 24.0f, by + 8.0f, 1.0f, green_);
			} else {
				Frame(x, by, 48.0f, 48.0f, 1.5f, yellow_);
				TextCentered(kKeys[i], x + 24.0f, by + 8.0f, 1.0f, yellow_);
			}
		}
		DrawStatusBox(statusX, statusY(2), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Thruster),
			c.TimeSinceCleared(LaunchChecklist::Item::Thruster));
	}

	// 3. 武装（射撃の回数）
	{
		const float y = rowY(3);
		rowFrame(3, "03", "WEAPON", "左クリック / RT で射撃");
		// L-CLICK を大きくし、撃った数のランプはその右に横に並べる（2026-10-19 ユーザー：縦に並べると小さくて読めない）
		constexpr float kHintSize = 0.8f;
		const float hintY = y + 26.0f;
		const float hintW = keyHint("L-CLICK", kWidgetX, hintY, kHintSize);
		const float lampSize = kGlyphHeight * kHintSize + 12.0f;
		for (int i = 0; i < c.GetWeaponShots(); ++i) {
			const float x = kWidgetX + hintW + 16.0f + static_cast<float>(i) * (lampSize + 8.0f);
			if (i < c.GetWeaponCount()) Rect(x, hintY, lampSize, lampSize, green_);
			else Frame(x, hintY, lampSize, lampSize, 2.0f, dimGreen_);
		}
		DrawStatusBox(statusX, statusY(3), kStatusW, kStatusH, c.IsCleared(LaunchChecklist::Item::Weapon),
			c.TimeSinceCleared(LaunchChecklist::Item::Weapon));
	}

	// 下の段：状態
	const float fy = kFooterTop;
	const float footerH = kHeight - kPad * 0.5f - fy;
	switch (status_) {
	case Status::Checking: {
		Text("SYSTEM CHECK", kPad, fy + 18.0f, kFooterSize, yellow_);
		char count[32];
		std::snprintf(count, sizeof(count), "%d / %d", c.ClearedCount(), LaunchChecklist::kItemCount);
		const float w = TextRenderer::GetInstance()->MeasureWidth(count, kFooterSize);
		Text(count, right - w, fy + 18.0f, kFooterSize, yellow_);
		Text("LAUNCH UNABLE", kPad, fy + 18.0f + kGlyphHeight * kFooterSize + 6.0f, 0.6f, red_);
		break;
	}
	case Status::PowerUp: {
		const float on = CockpitReveal::Flicker(statusTime_, 0.3f);
		TextCentered("COURSE CLEAR", kWidth * 0.5f, fy + 10.0f, kFooterSize * 1.15f, WithAlpha(green_, on));
		// 点け直しの進み（流れる点）
		const int dots = static_cast<int>(std::fmod(statusTime_ * 4.0f, 4.0f));
		std::string power = "CATAPULT POWER-UP";
		power.append(static_cast<size_t>(dots), '.');
		Text(power, kWidth * 0.5f - 170.0f, fy + 10.0f + kGlyphHeight * kFooterSize * 1.15f + 8.0f, 0.6f, WithAlpha(dimGreen_, on));
		break;
	}
	case Status::Charging:
	case Status::Ready:
	case Status::Launch: {
		// LAUNCH READY と、カタパルトの充電（2026-10-19 ユーザー：ゲージは出さず、済んだら CHARGED と出すだけ）。
		// SPACE の長押しはタコメーターの方に出す
		const float on = status_ == Status::Charging ? CockpitReveal::Flicker(statusTime_, 0.3f) : 1.0f;
		Rect(kPad, fy + 8.0f, kWidth * 0.5f - kPad, footerH - 16.0f, WithAlpha(green_, 0.9f * on));
		TextCentered("LAUNCH READY", kPad + (kWidth * 0.5f - kPad) * 0.5f, fy + (footerH - kGlyphHeight * kFooterSize) * 0.5f, kFooterSize,
			WithAlpha(kInk, on));
		const float boxX = kWidth * 0.5f + 20.0f;
		const float boxW = right - boxX;
		const float boxH = footerH - 16.0f;
		const float textSize = kFooterSize * 0.9f;
		const float textY = fy + (footerH - kGlyphHeight * textSize) * 0.5f;
		if (status_ == Status::Charging) {
			// 充電中：黄の枠と文字が点滅し、点が流れる
			const float blink = Blink(time_, 1.6f) * on;
			Frame(boxX, fy + 8.0f, boxW, boxH, 2.0f, WithAlpha(yellow_, blink));
			const int dots = static_cast<int>(std::fmod(statusTime_ * 4.0f, 4.0f));
			std::string charging = "CHARGING";
			charging.append(static_cast<size_t>(dots), '.');
			Text(charging, boxX + boxW * 0.5f - TextRenderer::GetInstance()->MeasureWidth("CHARGING", textSize) * 0.5f, textY,
				textSize, WithAlpha(yellow_, blink));
		} else {
			// 充電済み：チラついて黄地の CHARGED に変わる
			const float lit = status_ == Status::Ready ? CockpitReveal::Flicker(statusTime_, 0.3f) : 1.0f;
			Rect(boxX, fy + 8.0f, boxW, boxH, WithAlpha(yellow_, lit));
			TextCentered("CHARGED", boxX + boxW * 0.5f, textY, textSize, WithAlpha(kInk, lit));
		}
		break;
	}
	}
}

// ---------------------------------------------------------------- 調整値

void LaunchControlBoard::LoadTuning(const JsonValue& node) {
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
	green_ = ReadVector4(node["green"], green_);
	dimGreen_ = ReadVector4(node["dimGreen"], dimGreen_);
	yellow_ = ReadVector4(node["yellow"], yellow_);
	red_ = ReadVector4(node["red"], red_);
}

void LaunchControlBoard::SaveTuning(JsonValue& node) const {
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
	node["green"] = MakeVector4(green_);
	node["dimGreen"] = MakeVector4(dimGreen_);
	node["yellow"] = MakeVector4(yellow_);
	node["red"] = MakeVector4(red_);
}

void LaunchControlBoard::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults##board")) ResetToDefaults();
	ImGui::TextDisabled("1st person (floating board)");
	ImGui::DragFloat3("Offset From Eye", &offsetFromEye_.x, 0.005f);
	ImGui::DragFloat3("Rotation (deg)", &rotationDegrees_.x, 0.5f);
	ImGui::DragFloat("Width (m)", &width_, 0.005f, 0.05f, 3.0f);
	ImGui::TextDisabled("3rd person (panel mounted in catapult)");
	ImGui::DragFloat2("Panel Screen Anchor (-1..1)", &panelScreenAnchor_.x, 0.005f, -1.5f, 1.5f);
	ImGui::DragFloat("Panel Distance (m)", &panelDistance_, 0.1f, 1.0f, 200.0f);
	ImGui::Checkbox("Panel Face Eye (off = parallel to screen)", &panelFaceEye_);
	ImGui::DragFloat3("Panel Angle Offset (deg)", &panelAngleOffsetDegrees_.x, 0.5f);
	ImGui::DragFloat("Panel Screen Width (ratio)", &panelScreenWidth_, 0.005f, 0.05f, 1.0f);
	ImGui::TextDisabled("Common");
	ImGui::DragFloat("Emissive", &emissive_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Appear (s)", &appearSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Background Alpha", &backgroundAlpha_, 0.01f, 0.0f, 1.0f);
	ImGui::ColorEdit4("Green", &green_.x);
	ImGui::ColorEdit4("Dim Green", &dimGreen_.x);
	ImGui::ColorEdit4("Yellow", &yellow_.x);
	ImGui::ColorEdit4("Red", &red_.x);
	if (texture_) {
		ImGui::Image((ImTextureID)TextureManager::GetInstance()->GetSrvHandleGPU(kTextureName).ptr, ImVec2(256.0f, 180.0f));
	}
#endif // _DEBUG
}
