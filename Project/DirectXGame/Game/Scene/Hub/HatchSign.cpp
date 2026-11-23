#include "HatchSign.h"

#include "Cockpit/CockpitModel.h"
#include "CockpitReveal.h"
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
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// バーコードの線どうしのすき間 [px]
	constexpr float kBarcodeGap = 4.0f;
	// 座った目から表示板までのずれの初期値 [m]
	constexpr Vector3 kDefaultOffsetFromEye = { 0.0f, -0.08f, 0.55f };
	// 表示板の幅の初期値 [m]
	constexpr float kDefaultWidth = 0.56f;
	// UNLOCK の色の初期値
	constexpr Vector4 kDefaultUnlockColor = { 1.0f, 0.28f, 0.55f, 1.0f };
	// LOCKED の色の初期値
	constexpr Vector4 kDefaultLockedColor = { 0.35f, 1.0f, 0.65f, 1.0f };
	// 文字の後ろの暗い帯の不透明度の初期値
	constexpr float kDefaultBackgroundAlpha = 0.45f;
	// 表示板の発光の初期値
	constexpr float kDefaultEmissive = 0.15f;
	// 表示板のブルームの倍率の初期値
	constexpr float kDefaultBloomScale = 8.0f;
	// LOCKED / UNLOCK の切り替えでチカチカする長さの初期値 [秒]
	constexpr float kDefaultSwitchFlickerSeconds = 0.3f;
	// 切り替えのチカチカが終わった状態にするための十分前の時刻 [秒]
	constexpr float kSettledSwitchTime = 10.0f;
	// 板の高さを幅の何倍にするか（表示が 4 : 1 なので 1/4）
	constexpr float kBoardHeightPerWidth = 0.25f;
	// バーコード・細かい文字の暗い色の倍率
	constexpr float kDimColorScale = 0.45f;
	// 文字の後ろの暗い帯の上の余白 [px]
	constexpr float kBandMarginY = 30.0f;
	// 文字の後ろの暗い帯を表示の高さから縮める量（上下の余白の合計）[px]
	constexpr float kBandHeightTrim = 60.0f;
	// 文字の後ろの暗い帯の赤
	constexpr float kBandRed = 0.02f;
	// 文字の後ろの暗い帯の緑
	constexpr float kBandGreen = 0.03f;
	// 文字の後ろの暗い帯の青
	constexpr float kBandBlue = 0.05f;
	// 上下の斜めの縞に並べる "/" の数
	constexpr int kStripeCharCount = 48;
	// 上下の斜めの縞が流れる速さ [px/秒]
	constexpr float kStripeScrollSpeed = 30.0f;
	// 下の縞を表示の下端からどれだけ上に置くか [px]
	constexpr float kBottomStripeOffset = 40.0f;
	// 中央の板の左右の余白 [px]
	constexpr float kPlateMarginX = 150.0f;
	// 中央の板の上下の余白 [px]
	constexpr float kPlateMarginY = 62.0f;
	// 中央の板の薄い塗りの色の倍率
	constexpr float kPlateFillScale = 0.12f;
	// 中央の板の枠の太さ [px]
	constexpr float kPlateFrameThickness = 3.0f;
	// バーコードの左右の余白 [px]
	constexpr float kBarcodeMarginX = 22.0f;
	// バーコードの上端 [px]
	constexpr float kBarcodeY = 88.0f;
	// バーコードの線の高さ [px]
	constexpr float kBarcodeHeight = 80.0f;
	// 右のバーコードを置くために見込むバーコードの幅 [px]
	constexpr float kBarcodeWidth = 120.0f;
	// 本文の文字の大きさ
	constexpr float kLabelScale = 2.2f;
	// ALERT の文字の大きさ
	constexpr float kSuffixScale = 1.1f;
	// 本文と ALERT のすき間 [px]
	constexpr float kSuffixGap = 16.0f;
	// 本文の上端 [px]
	constexpr float kLabelY = 82.0f;
	// ALERT の上端 [px]
	constexpr float kSuffixY = 112.0f;
	// 細かい文字の大きさ
	constexpr float kDetailScale = 0.45f;
	// 細かい文字を中央の板の下端からどれだけ上に置くか [px]
	constexpr float kDetailBottomOffset = 26.0f;
	// "^" を中央から左へずらす量 [px]
	constexpr float kCaretOffsetX = 8.0f;
	// "^" を中央の板の上端から下へずらす量 [px]
	constexpr float kCaretOffsetY = 2.0f;
	// "^" の文字の大きさ
	constexpr float kCaretScale = 0.6f;
	// ImGui に出す表示の写しの幅 [px]
	constexpr float kPreviewImageWidth = 256.0f;
	// ImGui に出す表示の写しの高さ [px]
	constexpr float kPreviewImageHeight = 64.0f;

	// 板ポリ（tools/Python/gen_title_logo_plane.py。幅 2 × 高さ 1、表が -Z）。タイトルのロゴと共有
	constexpr const char* kBoardModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kBoardMesh = "logo_plane.mesh";
	// RenderTexture をモデルのテクスチャとして引く名前
	constexpr const char* kTextureName = "HatchSign_RenderTexture";
	// 表示の解像度（幅 : 高さ = 4 : 1。板もこの比で置く）
	constexpr float kTexWidth = 1024.0f;
	constexpr float kTexHeight = 256.0f;

	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">元の色</param>
	/// <param name="alpha">α に掛ける倍率</param>
	/// <returns>α だけ変えた色</returns>
	Vector4 Scaled(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}

	/// <summary>四角の枠線（4 辺）を描く</summary>
	/// <param name="tr">描画に使うテキストレンダラ</param>
	/// <param name="x0">左端 [px]</param>
	/// <param name="y0">上端 [px]</param>
	/// <param name="x1">右端 [px]</param>
	/// <param name="y1">下端 [px]</param>
	/// <param name="thickness">線の太さ [px]</param>
	/// <param name="color">色</param>
	void DrawFrame(TextRenderer* tr, float x0, float y0, float x1, float y1, float thickness, const Vector4& color) {
		tr->DrawRect({ x0, y0 }, { x1 - x0, thickness }, color);
		tr->DrawRect({ x0, y1 - thickness }, { x1 - x0, thickness }, color);
		tr->DrawRect({ x0, y0 }, { thickness, y1 - y0 }, color);
		tr->DrawRect({ x1 - thickness, y0 }, { thickness, y1 - y0 }, color);
	}

	/// <summary>バーコードのような縦線の列を描く（幅は決まった並び）</summary>
	/// <param name="tr">描画に使うテキストレンダラ</param>
	/// <param name="x">左端 [px]</param>
	/// <param name="y">上端 [px]</param>
	/// <param name="height">線の高さ [px]</param>
	/// <param name="color">色</param>
	void DrawBarcode(TextRenderer* tr, float x, float y, float height, const Vector4& color) {
		static const float kWidths[] = { 3, 6, 2, 2, 8, 3, 2, 5, 2, 9, 3, 2, 4, 2, 7 };
		float pen = x;
		for (size_t i = 0; i < std::size(kWidths); ++i) {
			tr->DrawRect({ pen, y }, { kWidths[i], height }, color);
			pen += kWidths[i] + kBarcodeGap;
		}
	}
}

HatchSign::HatchSign() {
	ResetToDefaults();
}

HatchSign::~HatchSign() = default;

void HatchSign::ResetToDefaults() {
	offsetFromEye_ = kDefaultOffsetFromEye;
	rotationDegrees_ = { 0.0f, 0.0f, 0.0f };
	width_ = kDefaultWidth;
	unlockColor_ = kDefaultUnlockColor;
	lockedColor_ = kDefaultLockedColor;
	backgroundAlpha_ = kDefaultBackgroundAlpha;
	emissive_ = kDefaultEmissive;
	bloomScale_ = kDefaultBloomScale;
	switchFlickerSeconds_ = kDefaultSwitchFlickerSeconds;
}

void HatchSign::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager) {
	texture_ = std::make_unique<RenderTexture>();
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	texture_->Initialize(dxCore, srvManager, static_cast<uint32_t>(kTexWidth), static_cast<uint32_t>(kTexHeight),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clear);
	TextureManager::GetInstance()->RegisterExternalTexture(kTextureName, texture_->GetResource(), texture_->GetSRVIndex());

	board_ = std::make_unique<Object3DInstance>();
	board_->Initialize(object3DManager, dxCore, kBoardModelDir, kBoardMesh, "HatchSign");
	locked_ = false;
	switchTime_ = kSettledSwitchTime;
	time_ = 0.0f;
}

void HatchSign::Finalize() {
	// 板のモデルはタイトルのロゴと共有なので、発光を戻しておく（テクスチャはタイトルが貼り直す）
	if (board_) {
		if (ModelInstance* model = board_->GetModelInstance()) model->ClearEmissive();
	}
	TextureManager::GetInstance()->UnregisterExternalTexture(kTextureName);
	if (texture_) {
		texture_->Finalize();
		texture_.reset();
	}
	board_.reset();
}

void HatchSign::SetLocked(bool locked) {
	if (locked == locked_) return;
	locked_ = locked;
	switchTime_ = 0.0f;
}

void HatchSign::Update(float deltaTime, const Vector3& mechOrigin) {
	time_ += deltaTime;
	switchTime_ += deltaTime;
	if (!board_) return;
	const Vector3 eye = CockpitModel::GetSeatedEye();
	board_->SetTranslate({ mechOrigin.x + eye.x + offsetFromEye_.x, mechOrigin.y + eye.y + offsetFromEye_.y,
		mechOrigin.z + eye.z + offsetFromEye_.z });
	board_->SetRotate({ DegToRad(rotationDegrees_.x), DegToRad(rotationDegrees_.y), DegToRad(rotationDegrees_.z) });
	// 板は 2 × 1。表示は 4 : 1 なので高さを幅の 1/4 に
	board_->SetScale({ width_ * 0.5f, width_ * kBoardHeightPerWidth, 1.0f });
	board_->Update();
	ApplyMaterial();
}

void HatchSign::ApplyMaterial() {
	ModelInstance* model = board_ ? board_->GetModelInstance() : nullptr;
	// テクスチャの差し替えは GPU の準備後に行う（準備時に .mat のパスで上書きされるため）
	if (!model || !model->IsGPUReady()) return;
	if (model->GetTextureFilePath() != kTextureName) {
		model->SetTextureFilePath(kTextureName);
	}
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

void HatchSign::RenderSignTexture(ID3D12GraphicsCommandList* commandList) {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!texture_ || !tr->IsInitialized()) return;

	texture_->BeginRender(commandList);   // 透明でクリア
	tr->BeginOffscreen(kTexWidth, kTexHeight);

	const float visible = CockpitReveal::Flicker(switchTime_, switchFlickerSeconds_) * visibility_;
	const Vector4 color = Scaled(locked_ ? lockedColor_ : unlockColor_, visible);
	const Vector4 dim = Scaled(color, kDimColorScale);

	// 文字の後ろの暗い帯
	tr->DrawRect({ 0.0f, kBandMarginY }, { kTexWidth, kTexHeight - kBandHeightTrim }, { kBandRed, kBandGreen, kBandBlue, backgroundAlpha_ * visible });

	// 上下の斜めの縞（"/" を並べて流す）
	const std::string stripes(kStripeCharCount, '/');
	const float stripeScale = 1.0f;
	const float period = tr->MeasureWidth("//", stripeScale);
	const float scroll = std::fmod(time_ * kStripeScrollSpeed, (std::max)(period, 1.0f));
	const float stripeWidth = tr->MeasureWidth(stripes, stripeScale);
	const float stripeX = (kTexWidth - stripeWidth) * 0.5f - scroll;
	tr->DrawText(stripes, { stripeX, 0.0f }, stripeScale, color);
	tr->DrawText(stripes, { stripeX, kTexHeight - kBottomStripeOffset }, stripeScale, color);

	// 中央の板：枠と薄い塗り
	const float plateX0 = kPlateMarginX, plateX1 = kTexWidth - kPlateMarginX, plateY0 = kPlateMarginY, plateY1 = kTexHeight - kPlateMarginY;
	tr->DrawRect({ plateX0, plateY0 }, { plateX1 - plateX0, plateY1 - plateY0 }, Scaled(color, kPlateFillScale));
	DrawFrame(tr, plateX0, plateY0, plateX1, plateY1, kPlateFrameThickness, color);

	// 左右のバーコード
	DrawBarcode(tr, kBarcodeMarginX, kBarcodeY, kBarcodeHeight, dim);
	DrawBarcode(tr, kTexWidth - kBarcodeMarginX - kBarcodeWidth, kBarcodeY, kBarcodeHeight, dim);

	// 本文
	const char* label = locked_ ? "HATCH LOCKED" : "HATCH UNLOCK";
	const float labelScale = kLabelScale;
	const float labelWidth = tr->MeasureWidth(label, labelScale);
	const char* suffix = locked_ ? "" : "ALERT";
	const float suffixScale = kSuffixScale;
	const float suffixWidth = locked_ ? 0.0f : tr->MeasureWidth(suffix, suffixScale) + kSuffixGap;
	const float labelX = (kTexWidth - labelWidth - suffixWidth) * 0.5f;
	tr->DrawText(label, { labelX, kLabelY }, labelScale, color);
	if (!locked_) {
		tr->DrawText(suffix, { labelX + labelWidth + kSuffixGap, kSuffixY }, suffixScale, color);
	}

	// 細かい文字（飾り）
	const char* detail = locked_ ? "SEAL 01-02-03  PRESSURE NOMINAL  //  COCKPIT ISOLATED"
		: "SEAL OPEN  //  KEEP CLEAR OF HATCH  //  BOARDING";
	const float detailScale = kDetailScale;
	const float detailWidth = tr->MeasureWidth(detail, detailScale);
	tr->DrawText(detail, { (kTexWidth - detailWidth) * 0.5f, plateY1 - kDetailBottomOffset }, detailScale, dim);
	tr->DrawText("^", { kTexWidth * 0.5f - kCaretOffsetX, plateY0 + kCaretOffsetY }, kCaretScale, dim);

	tr->FlushOffscreen();
	texture_->EndRender(commandList);
}

void HatchSign::Draw(DirectXCore* dxCore) {
	if (board_ && visibility_ > 0.0f) board_->Draw(dxCore);
}

void HatchSign::DrawBloom(DirectXCore* dxCore) {
	if (board_ && visibility_ > 0.0f) board_->DrawBloomPass(dxCore, bloomScale_);
}

void HatchSign::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	offsetFromEye_ = ReadVector3(node["offsetFromEye"], offsetFromEye_);
	rotationDegrees_ = ReadVector3(node["rotationDegrees"], rotationDegrees_);
	width_ = static_cast<float>(node["width"].AsDouble(width_));
	unlockColor_ = ReadVector4(node["unlockColor"], unlockColor_);
	lockedColor_ = ReadVector4(node["lockedColor"], lockedColor_);
	backgroundAlpha_ = static_cast<float>(node["backgroundAlpha"].AsDouble(backgroundAlpha_));
	emissive_ = static_cast<float>(node["emissive"].AsDouble(emissive_));
	bloomScale_ = static_cast<float>(node["bloomScale"].AsDouble(bloomScale_));
	switchFlickerSeconds_ = static_cast<float>(node["switchFlickerSeconds"].AsDouble(switchFlickerSeconds_));
}

void HatchSign::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["offsetFromEye"] = MakeVector3(offsetFromEye_);
	node["rotationDegrees"] = MakeVector3(rotationDegrees_);
	node["width"] = JsonValue(static_cast<double>(width_));
	node["unlockColor"] = MakeVector4(unlockColor_);
	node["lockedColor"] = MakeVector4(lockedColor_);
	node["backgroundAlpha"] = JsonValue(static_cast<double>(backgroundAlpha_));
	node["emissive"] = JsonValue(static_cast<double>(emissive_));
	node["bloomScale"] = JsonValue(static_cast<double>(bloomScale_));
	node["switchFlickerSeconds"] = JsonValue(static_cast<double>(switchFlickerSeconds_));
}

void HatchSign::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();
	ImGui::SameLine();
	if (ImGui::Button(locked_ ? "Unlock" : "Lock")) SetLocked(!locked_);
	ImGui::DragFloat3("Offset From Eye", &offsetFromEye_.x, 0.005f);
	ImGui::DragFloat3("Rotation (deg)", &rotationDegrees_.x, 0.5f);
	ImGui::DragFloat("Width (m)", &width_, 0.005f, 0.05f, 3.0f);
	ImGui::ColorEdit4("Unlock Color", &unlockColor_.x);
	ImGui::ColorEdit4("Locked Color", &lockedColor_.x);
	ImGui::DragFloat("Background Alpha", &backgroundAlpha_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Emissive", &emissive_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Switch Flicker (s)", &switchFlickerSeconds_, 0.01f, 0.0f, 2.0f);
	if (texture_) {
		ImGui::Image((ImTextureID)TextureManager::GetInstance()->GetSrvHandleGPU(kTextureName).ptr,
			ImVec2(kPreviewImageWidth, kPreviewImageHeight));
	}
#endif // _DEBUG
}
