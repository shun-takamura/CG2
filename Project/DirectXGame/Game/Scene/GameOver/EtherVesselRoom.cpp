#include "EtherVesselRoom.h"

#include "EtherVessel.h"
#include "EtherPillarFlow.h"
#include "Camera.h"
#include "Object3DManager.h"
#include "Water/WaterSurface.h"
#include "LightManager.h"
#include "PostEffect.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextureManager.h"
#include "AssetLocator.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "TextRenderer.h"
#include "MathUtility.h"   // kPi
#include "Json/JsonValue.h"
#include "Vector4.h"
#include <cmath>
#include <cstdio>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 器の映り込み。神殿の IBL（無ければタイトルの空）
	constexpr const char* kIblPath = "Resources/Cubemaps/hub_temple_ibl.dds";
	constexpr const char* kFallbackIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";
	// 水底はタイトル・ボス戦と同じ石畳（tools/Python/gen_flagstone_material.py）。暗い水の底にうっすら
	constexpr const char* kFloorTexturePath = "Resources/Textures/Terrain/Flagstone_BaseColor.dds";
	constexpr const char* kFloorNormalMapPath = "Resources/Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.dds";
	constexpr const char* kFloorHeightMapPath = "Resources/Textures/MaskTexture/Terrain/Flagstone_Height.dds";
	constexpr float kFloorTileMeters = 3.0f;
	constexpr float kFloorParallaxDepth = (0.0068f + 0.013f) / kFloorTileMeters;
	constexpr float kWaterSize = 400.0f;   // カメラ追従
	constexpr float kDegToRad = kPi / 180.0f;
	// TextRenderer のフォントの高さ（Framework::Initialize の MPLUS1p 32px）
	constexpr float kFontPixelHeight = 32.0f;

	void ReadFloat(const JsonValue& node, const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	}
	void WriteFloat(JsonValue& node, const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	}
	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}
	void ReadColor3(const JsonValue& node, float (&value)[3]) {
		if (!node.IsArray() || node.Size() < 3) return;
		for (int i = 0; i < 3; ++i) value[i] = static_cast<float>(node[static_cast<size_t>(i)].AsDouble(value[i]));
	}
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
	JsonValue MakeColor3(const float (&c)[3]) {
		return MakeVector3({ c[0], c[1], c[2] });
	}
}

EtherVesselRoom::EtherVesselRoom() = default;
EtherVesselRoom::~EtherVesselRoom() = default;

void EtherVesselRoom::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	SpriteManager* spriteManager) {
	dxCore_ = dxCore;
	object3DManager_ = object3DManager;
	spriteManager_ = spriteManager;

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });

	iblPath_ = AssetLocator::GetInstance()->Exists(kIblPath) ? kIblPath : kFallbackIblPath;
	TextureManager::GetInstance()->LoadTexture(iblPath_);

	backgroundSprite_ = std::make_unique<SpriteInstance>();
	backgroundSprite_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "EtherVesselRoomBackground");
	backgroundSprite_->SetPosition({ 0.0f, 0.0f });
	backgroundSprite_->SetSize({ static_cast<float>(dxCore_->GetUIWidth()), static_cast<float>(dxCore_->GetUIHeight()) });

	// Object3DInstance は作った瞬間の既定のカメラで描かれる。HUB は自分のカメラが既定のまま器の間を作るので、
	// 器の部品を作る間だけこの部屋のカメラを既定にする（しないと HUB のカメラで描かれて器が画面に出なかった）
	Camera* previousCamera = object3DManager_->GetDefaultCamera();
	object3DManager_->SetDefaultCamera(camera_.get());
	vessel_ = std::make_unique<EtherVessel>();
	vessel_->Initialize(dxCore_, srvManager, object3DManager_);
	vessel_->SetOrigin(origin_);
	object3DManager_->SetDefaultCamera(previousCamera);

	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore_, srvManager, object3DManager_, kFloorTexturePath);
	water_->SetSize(kWaterSize);
	water_->SetFollowCamera(true);
	water_->GetParams().floorTiling = 1.0f / kFloorTileMeters;
	water_->SetFloorMaps(kFloorNormalMapPath, kFloorHeightMapPath, kFloorParallaxDepth);
	water_->SetRippleCenter(origin_);
	ApplyWaterTuning();   // JSON が無くても既定値を水面へ
}

void EtherVesselRoom::Finalize() {
	if (vessel_) vessel_->Finalize();
}

void EtherVesselRoom::Activate() {
	object3DManager_->SetDefaultCamera(camera_.get());
	object3DManager_->SetEnvironmentTexture(iblPath_);
	// ステージの距離フォグはシーンをまたいで残るので切る
	object3DManager_->DisableFog();
}

void EtherVesselRoom::SetCameraPose(const Vector3& offset, const Vector3& rotateDegrees) {
	camera_->SetTranslate({ origin_.x + offset.x, origin_.y + offset.y, origin_.z + offset.z });
	camera_->SetRotate({ rotateDegrees.x * kDegToRad, rotateDegrees.y * kDegToRad, rotateDegrees.z * kDegToRad });
	camera_->Update();
}

void EtherVesselRoom::EmitCursorRing() {
	water_->EmitRing(origin_, cursorRingAmplitude_, cursorRingWavelength_, 2.5f, cursorRingRadius_);
}

bool EtherVesselRoom::ProjectToUi(const Vector3& world, Vector2& out) const {
	const Vector3 view = TransformCoordinate(world, camera_->GetViewMatrix());
	if (view.z <= 0.01f) return false;
	const Vector3 ndc = TransformCoordinate(world, camera_->GetViewProjectionMatrix());
	const float w = static_cast<float>(dxCore_->GetUIWidth());
	const float h = static_cast<float>(dxCore_->GetUIHeight());
	out = { (ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h };
	return true;
}

void EtherVesselRoom::DrawPurityTag(float purity, float alpha, float scale, float offset, const float (&gold)[3]) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || alpha <= 0.0f) return;
	Vector2 tip, center;
	if (!ProjectToUi(vessel_->GetPointerTipWorld(), tip) || !ProjectToUi(vessel_->GetSphereCenter(), center)) return;
	Vector2 dir{ tip.x - center.x, tip.y - center.y };
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	dir = len > 1e-3f ? Vector2{ dir.x / len, dir.y / len } : Vector2{ 0.0f, -1.0f };
	const Vector2 tag{ tip.x + dir.x * offset, tip.y + dir.y * offset };
	const Vector4 goldColor{ gold[0], gold[1], gold[2], alpha };
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%.1f", purity);
	const float textH = kFontPixelHeight * scale;
	const float textW = tr->MeasureWidth(buf, scale);
	const float padX = 14.0f;
	const float padY = 4.0f;
	const Vector2 boxPos{ tag.x - textW * 0.5f - padX, tag.y - textH * 0.5f - padY };
	const Vector2 boxSize{ textW + padX * 2.0f, textH + padY * 2.0f };
	tr->DrawRect(boxPos, boxSize, { 0.0f, 0.01f, 0.03f, 0.75f * alpha });
	tr->DrawRect(boxPos, { boxSize.x, 2.0f }, goldColor);
	tr->DrawRect({ boxPos.x, boxPos.y + boxSize.y - 2.0f }, { boxSize.x, 2.0f }, goldColor);
	tr->DrawRect(boxPos, { 2.0f, boxSize.y }, goldColor);
	tr->DrawRect({ boxPos.x + boxSize.x - 2.0f, boxPos.y }, { 2.0f, boxSize.y }, goldColor);
	tr->DrawText(buf, { tag.x - textW * 0.5f, tag.y - textH * 0.5f }, scale, { 1.0f, 1.0f, 1.0f, alpha }, 2.0f, goldColor);
	// 呼び方は札の上に小さく
	constexpr const char* kName = "純度";
	const float nameScale = scale * 0.5f;
	const float nameW = tr->MeasureWidth(kName, nameScale);
	tr->DrawText(kName, { tag.x - nameW * 0.5f, boxPos.y - kFontPixelHeight * nameScale - 4.0f }, nameScale,
		{ 0.85f, 0.88f, 0.92f, alpha }, 1.5f, { 0.0f, 0.0f, 0.0f, alpha * 0.8f });
}

void EtherVesselRoom::Update(float deltaTime) {
	water_->GetParams().waterColor = { waterColor_.x, waterColor_.y, waterColor_.z, 1.0f };
	water_->Update(deltaTime);
	vessel_->Update(deltaTime);

	if (EtherPillarFlow* flow = vessel_->GetPillarFlow()) {
		// 支柱を下りた光が泉に着いたら、足元の水面に波紋
		for (int n = 0; n < flow->GetArrivals(); ++n) {
			water_->EmitRing(origin_, arrivalRingAmplitude_, cursorRingWavelength_, 2.5f, cursorRingRadius_);
		}
		// 支柱を昇った光が球に着いたら、液面が小さく揺れる（賭ける）
		if (flow->GetTopArrivals() > 0 && topArrivalSplash_ > 0.0f) vessel_->Splash(topArrivalSplash_);
	}

	backgroundSprite_->SetColor({ background_[0], background_[1], background_[2], 1.0f });
	backgroundSprite_->Update();
}

void EtherVesselRoom::Draw() {
	auto* commandList = dxCore_->GetCommandList();
	vessel_->DispatchSimulation();
	water_->DispatchSimulation();

	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(dxCore_->GetRenderWidth()),
		static_cast<float>(dxCore_->GetRenderHeight()), 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(dxCore_->GetRenderWidth()), static_cast<LONG>(dxCore_->GetRenderHeight()) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto dsvHandle = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
	// 背景の板（スプライトは深度を書くので、塗った後に深度を作成時と同じ 1.0 で消す）
	spriteManager_->DrawSetting();
	backgroundSprite_->Draw();
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	vessel_->DrawOpaque(dxCore_);
	// 足元の暗い水面（不透明物の後。ルートシグネチャを替える）
	water_->Draw(*camera_, iblPath_, nullptr);
	// ガラスの球・支柱の光・糸は深度を見るだけ
	vessel_->DrawTransparent(camera_.get(), iblPath_);
}

void EtherVesselRoom::DrawBloom() {
	vessel_->DrawBloom(camera_.get(), iblPath_);
}

void EtherVesselRoom::ApplyWaterTuning() {
	WaterSurface::Params& p = water_->GetParams();
	p.depth = waterDepth_;
	p.skyIntensity = waterSkyIntensity_;
	p.ringAmplitude = waterRingAmplitude_;
	p.ringInterval = waterRingInterval_;
	p.waterColor = { waterColor_.x, waterColor_.y, waterColor_.z, 1.0f };
	water_->SetWaterHeight(origin_.y + waterHeight_);
}

void EtherVesselRoom::LoadTuning(const JsonValue& root) {
	ReadColor3(root["background"], background_);
	const JsonValue& water = root["water"];
	ReadFloat(water, "height", waterHeight_);
	ReadFloat(water, "depth", waterDepth_);
	ReadVector3(water["color"], waterColor_);
	ReadFloat(water, "skyIntensity", waterSkyIntensity_);
	ReadFloat(water, "ringAmplitude", waterRingAmplitude_);
	ReadFloat(water, "ringInterval", waterRingInterval_);
	ReadFloat(water, "cursorRingAmplitude", cursorRingAmplitude_);
	ReadFloat(water, "cursorRingWavelength", cursorRingWavelength_);
	ReadFloat(water, "cursorRingRadius", cursorRingRadius_);
	ReadFloat(water, "arrivalRingAmplitude", arrivalRingAmplitude_);
	ReadFloat(water, "topArrivalSplash", topArrivalSplash_);
	vessel_->LoadTuning(root["vessel"]);
	ApplyWaterTuning();
}

void EtherVesselRoom::SaveTuning(JsonValue& root) const {
	root["background"] = MakeColor3(background_);
	JsonValue& water = root["water"];
	water = JsonValue::MakeObject();
	WriteFloat(water, "height", waterHeight_);
	WriteFloat(water, "depth", waterDepth_);
	water["color"] = MakeVector3(waterColor_);
	WriteFloat(water, "skyIntensity", waterSkyIntensity_);
	WriteFloat(water, "ringAmplitude", waterRingAmplitude_);
	WriteFloat(water, "ringInterval", waterRingInterval_);
	WriteFloat(water, "cursorRingAmplitude", cursorRingAmplitude_);
	WriteFloat(water, "cursorRingWavelength", cursorRingWavelength_);
	WriteFloat(water, "cursorRingRadius", cursorRingRadius_);
	WriteFloat(water, "arrivalRingAmplitude", arrivalRingAmplitude_);
	WriteFloat(water, "topArrivalSplash", topArrivalSplash_);
	vessel_->SaveTuning(root["vessel"]);
}

void EtherVesselRoom::OnImGuiWater() {
#ifdef _DEBUG
	WaterSurface::Params& p = water_->GetParams();
	if (ImGui::DragFloat("Height [m]", &waterHeight_, 0.005f, -1.0f, 2.0f)) water_->SetWaterHeight(origin_.y + waterHeight_);
	if (ImGui::DragFloat("Depth [m]", &waterDepth_, 0.01f, 0.0f, 5.0f)) p.depth = waterDepth_;
	ImGui::ColorEdit3("Water Color", &waterColor_.x, ImGuiColorEditFlags_Float);
	if (ImGui::DragFloat("Sky Intensity", &waterSkyIntensity_, 0.01f, 0.0f, 3.0f)) p.skyIntensity = waterSkyIntensity_;
	if (ImGui::DragFloat("Ambient Ring Amplitude", &waterRingAmplitude_, 0.0005f, 0.0f, 0.1f, "%.4f")) p.ringAmplitude = waterRingAmplitude_;
	if (ImGui::DragFloat("Ambient Ring Interval", &waterRingInterval_, 0.05f, 0.1f, 30.0f)) p.ringInterval = waterRingInterval_;
	ImGui::DragFloat("Cursor Ring Amplitude", &cursorRingAmplitude_, 0.0005f, 0.0f, 0.2f, "%.4f");
	ImGui::DragFloat("Cursor Ring Wavelength", &cursorRingWavelength_, 0.005f, 0.01f, 3.0f);
	ImGui::DragFloat("Cursor Ring Radius", &cursorRingRadius_, 0.05f, 0.1f, 50.0f);
	ImGui::DragFloat("Arrival Ring Amplitude", &arrivalRingAmplitude_, 0.0005f, 0.0f, 0.2f, "%.4f");
	ImGui::DragFloat("Top Arrival Splash (bet)", &topArrivalSplash_, 0.0001f, 0.0f, 0.05f, "%.4f");
	if (ImGui::Button("Cursor Ring")) EmitCursorRing();
	ImGui::ColorEdit3("Background", background_);
	if (ImGui::TreeNode("WaterSurface (raw)")) {
		water_->OnImGui();
		ImGui::TreePop();
	}
#endif
}
