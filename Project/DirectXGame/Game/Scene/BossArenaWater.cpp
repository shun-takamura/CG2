#include "BossArenaWater.h"

#include "Water/WaterSurface.h"
#include "Vector3.h"

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
	constexpr float kSeaSize = 6000.0f; // 海はカメラ追従。地平線まで張る
	// 海の水底は深さで見えなくなる（吸収で水の色になりきる）ので、床のテクスチャは何でもよい
	constexpr float kSeaDepth = 40.0f;
}

BossArenaWater::BossArenaWater() = default;
BossArenaWater::~BossArenaWater() = default;

void BossArenaWater::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager) {
	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	water_->SetFollowCamera(false);
	water_->GetParams().floorTiling = 1.0f / kFloorTileMeters;
	water_->SetFloorMaps(kFloorNormalMapPath, kFloorHeightMapPath, kFloorParallaxDepth);
	// 待機中のさざ波はタイトルより控えめ（戦闘中に水面がうるさくならないように）
	water_->GetParams().ringAmplitude = 0.01f;
	water_->GetParams().ringInterval = 6.0f;
	water_->GetParams().ringJitter = 0.7f;
	ApplyHeight();

	sea_ = std::make_unique<WaterSurface>();
	sea_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	sea_->SetSize(kSeaSize);
	sea_->SetFollowCamera(true);
	WaterSurface::Params& sp = sea_->GetParams();
	sp.depth = kSeaDepth;
	sp.waterColor = { 0.02f, 0.09f, 0.13f, 1.0f };
	sp.absorption = { 0.45f, 0.18f, 0.12f };
	sp.ambient = 0.0f;
	sp.ringAmplitude = 0.0f;      // 海に同心円の波は出さない
	sp.noiseAmplitude = 0.08f;    // 広いうねり
	sp.noiseScale = 0.12f;
	sp.noiseSpeed = 0.6f;
	sea_->SetClipMinZ(seaClipMinZ_);
}

void BossArenaWater::SetArena(const Vector3& center, float groundY, float waterRadius, float seaY) {
	groundY_ = groundY;
	ApplyHeight();
	if (water_) {
		water_->SetCenter({ center.x, 0.0f, center.z });
		water_->SetSize(waterRadius * 2.0f + 2.0f);
		water_->SetKeepCircle(center, waterRadius);
		water_->SetRippleCenter(center);
	}
	if (sea_) {
		sea_->SetWaterHeight(seaY);
		sea_->SetClipMinZ(seaClipMinZ_);
	}
}

void BossArenaWater::EmitSplash(const Vector3& position) {
	if (water_) water_->EmitRing(position, 0.05f, 1.2f, 3.0f, 25.0f);
}

void BossArenaWater::ApplyHeight() {
	if (!water_) return;
	water_->GetParams().depth = depth_;
	water_->SetWaterHeight(groundY_ + depth_);
}

void BossArenaWater::Update(float deltaTime) {
	if (water_) water_->Update(deltaTime);
	if (sea_) sea_->Update(deltaTime);
}

void BossArenaWater::DispatchSimulation() {
	if (water_) water_->DispatchSimulation();
	if (sea_) sea_->DispatchSimulation();
}

void BossArenaWater::Draw(const Camera& camera, const std::string& skyCubemapPath) {
	if (sea_) sea_->Draw(camera, skyCubemapPath, nullptr);
	if (water_) water_->Draw(camera, skyCubemapPath, nullptr);
}

void BossArenaWater::OnImGui() {
#ifdef _DEBUG
	if (!water_) return;
	// 床の高さを固定したまま水深だけ変える（WaterSurface 側の Water Height / Depth は個別に動く）
	if (ImGui::DragFloat("Boss Water Depth", &depth_, 0.005f, 0.0f, 1.0f, "%.3f")) {
		ApplyHeight();
	}
	if (ImGui::TreeNode("Floor Water")) {
		water_->OnImGui();
		ImGui::TreePop();
	}
	if (sea_ && ImGui::TreeNode("Sea")) {
		ImGui::DragFloat("Sea Clip Min Z", &seaClipMinZ_, 1.0f, 0.0f, 6000.0f, "%.0f");
		sea_->OnImGui();
		ImGui::TreePop();
	}
#endif
}
