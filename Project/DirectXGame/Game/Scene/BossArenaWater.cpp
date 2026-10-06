#include "BossArenaWater.h"

#include "Water/WaterSurface.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 石畳はタイトルの水底と同じ素材（tools/Python/gen_flagstone_material.py）
	// 凹凸は高さ -0.013〜+0.0068m（Assets/Textures/MaskTexture/Terrain/Flagstone_Height.json）
	constexpr const char* kFloorTexturePath = "Resources/Textures/Terrain/Flagstone_BaseColor.dds";
	constexpr const char* kFloorNormalMapPath = "Resources/Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.dds";
	constexpr const char* kFloorHeightMapPath = "Resources/Textures/MaskTexture/Terrain/Flagstone_Height.dds";
	constexpr float kFloorTileMeters = 3.0f;
	constexpr float kFloorParallaxDepth = (0.0068f + 0.013f) / kFloorTileMeters; // 深さ[m] ÷ 1 枚の長さ[m]
	constexpr float kWaterSize = 1500.0f; // カメラ追従。地平線まで水を張る
}

BossArenaWater::BossArenaWater() = default;
BossArenaWater::~BossArenaWater() = default;

void BossArenaWater::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager) {
	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	water_->SetSize(kWaterSize);
	water_->SetFollowCamera(true);
	water_->GetParams().floorTiling = 1.0f / kFloorTileMeters;
	water_->SetFloorMaps(kFloorNormalMapPath, kFloorHeightMapPath, kFloorParallaxDepth);
	// 待機中のさざ波はタイトルより控えめ（戦闘中に水面がうるさくならないように）
	water_->GetParams().ringAmplitude = 0.01f;
	water_->GetParams().ringInterval = 6.0f;
	water_->GetParams().ringJitter = 0.7f;
	ApplyHeight();
}

void BossArenaWater::SetGroundY(float groundY) {
	groundY_ = groundY;
	ApplyHeight();
}

void BossArenaWater::SetArenaCenter(float x, float z) {
	if (water_) water_->SetRippleCenter({ x, 0.0f, z });
}

void BossArenaWater::ApplyHeight() {
	if (!water_) return;
	water_->GetParams().depth = depth_;
	water_->SetWaterHeight(groundY_ + depth_);
}

void BossArenaWater::Update(float deltaTime) {
	if (water_) water_->Update(deltaTime);
}

void BossArenaWater::DispatchSimulation() {
	if (water_) water_->DispatchSimulation();
}

void BossArenaWater::Draw(const Camera& camera, const std::string& skyCubemapPath) {
	if (water_) water_->Draw(camera, skyCubemapPath, nullptr);
}

void BossArenaWater::OnImGui() {
#ifdef _DEBUG
	if (!water_) return;
	// 床の高さを固定したまま水深だけ変える（WaterSurface 側の Water Height / Depth は個別に動く）
	if (ImGui::DragFloat("Boss Water Depth", &depth_, 0.005f, 0.0f, 1.0f, "%.3f")) {
		ApplyHeight();
	}
	water_->OnImGui();
#endif
}
