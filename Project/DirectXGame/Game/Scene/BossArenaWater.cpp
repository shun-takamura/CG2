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
	// 足場の水：待機中のさざ波の高さ [m]（タイトルより控えめ）
	constexpr float kArenaRingAmplitude = 0.01f;
	// 足場の水：待機中のさざ波が出る間隔 [秒]
	constexpr float kArenaRingInterval = 6.0f;
	// 足場の水：さざ波の間隔のばらつき（0..1）
	constexpr float kArenaRingJitter = 0.7f;
	// 海の水の色 R
	constexpr float kSeaWaterColorR = 0.02f;
	// 海の水の色 G
	constexpr float kSeaWaterColorG = 0.09f;
	// 海の水の色 B
	constexpr float kSeaWaterColorB = 0.13f;
	// 海の吸収係数 R
	constexpr float kSeaAbsorptionR = 0.45f;
	// 海の吸収係数 G
	constexpr float kSeaAbsorptionG = 0.18f;
	// 海の吸収係数 B
	constexpr float kSeaAbsorptionB = 0.12f;
	// 海の広いうねりの高さ [m]
	constexpr float kSeaNoiseAmplitude = 0.08f;
	// 海のうねりのノイズの細かさ
	constexpr float kSeaNoiseScale = 0.12f;
	// 海のうねりの流れる速さ
	constexpr float kSeaNoiseSpeed = 0.6f;
	// 足場の水の板を円の直径より広げる量 [m]
	constexpr float kArenaWaterSizeMargin = 2.0f;
	// 着水の波紋の高さ [m]
	constexpr float kSplashRingAmplitude = 0.05f;
	// 着水の波紋の波長 [m]
	constexpr float kSplashRingWavelength = 1.2f;
	// 着水の波紋の束の長さ（波長の数）
	constexpr float kSplashRingPacketLength = 3.0f;
	// 着水の波紋が届く距離 [m]
	constexpr float kSplashRingMaxRadius = 25.0f;
}

BossArenaWater::BossArenaWater() = default;
BossArenaWater::~BossArenaWater() = default;

void BossArenaWater::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager) {
	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	water_->SetFollowCamera(false);
	{
		WaterSurface::Params p = water_->GetParams();
		p.floorTiling = 1.0f / kFloorTileMeters;
		water_->SetParams(p);
	}
	water_->SetFloorMaps(kFloorNormalMapPath, kFloorHeightMapPath, kFloorParallaxDepth);
	{
		// 待機中のさざ波はタイトルより控えめ（戦闘中に水面がうるさくならないように）
		WaterSurface::Params p = water_->GetParams();
		p.ringAmplitude = kArenaRingAmplitude;
		p.ringInterval = kArenaRingInterval;
		p.ringJitter = kArenaRingJitter;
		water_->SetParams(p);
	}
	ApplyHeight();

	sea_ = std::make_unique<WaterSurface>();
	sea_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	sea_->SetSize(kSeaSize);
	sea_->SetFollowCamera(true);
	WaterSurface::Params sp = sea_->GetParams();
	sp.depth = kSeaDepth;
	sp.waterColor = { kSeaWaterColorR, kSeaWaterColorG, kSeaWaterColorB, 1.0f };
	sp.absorption = { kSeaAbsorptionR, kSeaAbsorptionG, kSeaAbsorptionB };
	sp.ambient = 0.0f;
	sp.ringAmplitude = 0.0f;      // 海に同心円の波は出さない
	sp.noiseAmplitude = kSeaNoiseAmplitude;    // 広いうねり
	sp.noiseScale = kSeaNoiseScale;
	sp.noiseSpeed = kSeaNoiseSpeed;
	sea_->SetParams(sp);
	sea_->SetClipMinZ(seaClipMinZ_);
}

void BossArenaWater::SetArena(const Vector3& center, float groundY, float waterRadius, float seaY) {
	groundY_ = groundY;
	ApplyHeight();
	if (water_) {
		water_->SetCenter({ center.x, 0.0f, center.z });
		water_->SetSize(waterRadius * 2.0f + kArenaWaterSizeMargin);
		water_->SetKeepCircle(center, waterRadius);
		water_->SetRippleCenter(center);
	}
	if (sea_) {
		sea_->SetWaterHeight(seaY);
		sea_->SetClipMinZ(seaClipMinZ_);
	}
}

void BossArenaWater::EmitSplash(const Vector3& position) {
	if (water_) water_->EmitRing(position, kSplashRingAmplitude, kSplashRingWavelength, kSplashRingPacketLength, kSplashRingMaxRadius);
}

void BossArenaWater::ApplyHeight() {
	if (!water_) return;
	WaterSurface::Params p = water_->GetParams();
	p.depth = depth_;
	water_->SetParams(p);
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
