#include "HatchSign.h"

#include "HubMechModel.h"
#include "CockpitReveal.h"
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
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 板ポリ（tools/Python/gen_title_logo_plane.py。幅 2 × 高さ 1、表が -Z）。タイトルのロゴと共有
	constexpr const char* kBoardModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kBoardMesh = "logo_plane.mesh";
	// RenderTexture をモデルのテクスチャとして引く名前
	constexpr const char* kTextureName = "HatchSign_RenderTexture";
	// 表示の解像度（幅 : 高さ = 4 : 1。板もこの比で置く）
	constexpr float kTexWidth = 1024.0f;
	constexpr float kTexHeight = 256.0f;

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

	Vector4 Scaled(const Vector4& c, float alpha) {
		return { c.x, c.y, c.z, c.w * alpha };
	}

	// 枠線（四角の 4 辺）
	void DrawFrame(TextRenderer* tr, float x0, float y0, float x1, float y1, float thickness, const Vector4& color) {
		tr->DrawRect({ x0, y0 }, { x1 - x0, thickness }, color);
		tr->DrawRect({ x0, y1 - thickness }, { x1 - x0, thickness }, color);
		tr->DrawRect({ x0, y0 }, { thickness, y1 - y0 }, color);
		tr->DrawRect({ x1 - thickness, y0 }, { thickness, y1 - y0 }, color);
	}

	// バーコードのような縦線の列（幅は決まった並び）
	void DrawBarcode(TextRenderer* tr, float x, float y, float height, const Vector4& color) {
		static const float kWidths[] = { 3, 6, 2, 2, 8, 3, 2, 5, 2, 9, 3, 2, 4, 2, 7 };
		float pen = x;
		for (size_t i = 0; i < std::size(kWidths); ++i) {
			tr->DrawRect({ pen, y }, { kWidths[i], height }, color);
			pen += kWidths[i] + 4.0f;
		}
	}
}

HatchSign::HatchSign() {
	ResetToDefaults();
}

HatchSign::~HatchSign() = default;

void HatchSign::ResetToDefaults() {
	offsetFromEye_ = { 0.0f, -0.08f, 0.55f };
	rotationDegrees_ = { 0.0f, 0.0f, 0.0f };
	width_ = 0.56f;
	unlockColor_ = { 1.0f, 0.28f, 0.55f, 1.0f };
	lockedColor_ = { 0.35f, 1.0f, 0.65f, 1.0f };
	backgroundAlpha_ = 0.45f;
	emissive_ = 0.15f;
	bloomScale_ = 8.0f;
	switchFlickerSeconds_ = 0.3f;
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
	switchTime_ = 10.0f;
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
	const Vector3 eye = HubMechModel::GetSeatedEye();
	board_->SetTranslate({ mechOrigin.x + eye.x + offsetFromEye_.x, mechOrigin.y + eye.y + offsetFromEye_.y,
		mechOrigin.z + eye.z + offsetFromEye_.z });
	board_->SetRotate({ DegToRad(rotationDegrees_.x), DegToRad(rotationDegrees_.y), DegToRad(rotationDegrees_.z) });
	// 板は 2 × 1。表示は 4 : 1 なので高さを幅の 1/4 に
	board_->SetScale({ width_ * 0.5f, width_ * 0.25f, 1.0f });
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
	const Vector4 dim = Scaled(color, 0.45f);

	// 文字の後ろの暗い帯
	tr->DrawRect({ 0.0f, 30.0f }, { kTexWidth, kTexHeight - 60.0f }, { 0.02f, 0.03f, 0.05f, backgroundAlpha_ * visible });

	// 上下の斜めの縞（"/" を並べて流す）
	const std::string stripes(48, '/');
	const float stripeScale = 1.0f;
	const float period = tr->MeasureWidth("//", stripeScale);
	const float scroll = std::fmod(time_ * 30.0f, (std::max)(period, 1.0f));
	const float stripeWidth = tr->MeasureWidth(stripes, stripeScale);
	const float stripeX = (kTexWidth - stripeWidth) * 0.5f - scroll;
	tr->DrawText(stripes, { stripeX, 0.0f }, stripeScale, color);
	tr->DrawText(stripes, { stripeX, kTexHeight - 40.0f }, stripeScale, color);

	// 中央の板：枠と薄い塗り
	const float plateX0 = 150.0f, plateX1 = kTexWidth - 150.0f, plateY0 = 62.0f, plateY1 = kTexHeight - 62.0f;
	tr->DrawRect({ plateX0, plateY0 }, { plateX1 - plateX0, plateY1 - plateY0 }, Scaled(color, 0.12f));
	DrawFrame(tr, plateX0, plateY0, plateX1, plateY1, 3.0f, color);

	// 左右のバーコード
	DrawBarcode(tr, 22.0f, 88.0f, 80.0f, dim);
	DrawBarcode(tr, kTexWidth - 22.0f - 120.0f, 88.0f, 80.0f, dim);

	// 本文
	const char* label = locked_ ? "HATCH LOCKED" : "HATCH UNLOCK";
	const float labelScale = 2.2f;
	const float labelWidth = tr->MeasureWidth(label, labelScale);
	const char* suffix = locked_ ? "" : "ALERT";
	const float suffixScale = 1.1f;
	const float suffixWidth = locked_ ? 0.0f : tr->MeasureWidth(suffix, suffixScale) + 16.0f;
	const float labelX = (kTexWidth - labelWidth - suffixWidth) * 0.5f;
	tr->DrawText(label, { labelX, 82.0f }, labelScale, color);
	if (!locked_) {
		tr->DrawText(suffix, { labelX + labelWidth + 16.0f, 112.0f }, suffixScale, color);
	}

	// 細かい文字（飾り）
	const char* detail = locked_ ? "SEAL 01-02-03  PRESSURE NOMINAL  //  COCKPIT ISOLATED"
		: "SEAL OPEN  //  KEEP CLEAR OF HATCH  //  BOARDING";
	const float detailScale = 0.45f;
	const float detailWidth = tr->MeasureWidth(detail, detailScale);
	tr->DrawText(detail, { (kTexWidth - detailWidth) * 0.5f, plateY1 - 26.0f }, detailScale, dim);
	tr->DrawText("^", { kTexWidth * 0.5f - 8.0f, plateY0 + 2.0f }, 0.6f, dim);

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
			ImVec2(256.0f, 64.0f));
	}
#endif // _DEBUG
}
