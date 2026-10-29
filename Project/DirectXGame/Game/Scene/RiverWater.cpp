#include "RiverWater.h"
#include "PepperMacros.h"

#include "Water/WaterSurface.h"
#include "Water/WaterReflection.h"
#include "Water/RippleSimulation.h"
#include "Game.h"
#include "PostEffect.h"
#include "Object3DInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Components/Gameplay.h"
#include "Effect/EffectManager.h"
#include "Primitive/LineRenderer.h"
#include "TextureManager.h"
#include "DStorageManager.h"
#include "Camera.h"
#include "Log.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// L2 が書き出す川のマップの対応情報（12_TerrainRendering.md。作り直しは gen_terrain.py --river-map）
	constexpr const char* kRiverInfoPath = "Resources/Json/Terrain/RiverTerrain.json";
	// 調整値（L3 の所有。Tuning/StagePlay.json は L5 の所有なので混ぜない）
	constexpr const char* kTuningPath = "Resources/Json/Tuning/RiverWater.json";

	Vector4 ReadColor(const JsonValue& j, const Vector4& fallback) {
		if (!j.IsArray() || j.Size() < 3) return fallback;
		return {
			static_cast<float>(j[0].AsDouble(fallback.x)),
			static_cast<float>(j[1].AsDouble(fallback.y)),
			static_cast<float>(j[2].AsDouble(fallback.z)),
			fallback.w,
		};
	}

	JsonValue WriteColor(const Vector4& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}

	float ReadFloat(const JsonValue& root, const char* key, float fallback) {
		return static_cast<float>(root[key].AsDouble(fallback));
	}

	// 映すタグの初期値（13 番 §4.4）。L6 の岩・L10 の木はタグを付けてここ（JSON）に足す
	constexpr EntityTag kDefaultReflectTags[] = {
		EntityTag::Terrain, EntityTag::Player, EntityTag::Enemy, EntityTag::Boss,
	};
	// 反射 RT を波で傾いた反射の向きで引くときの、映る物までの想定距離 [m]（低空から山まで 300〜500m）
	constexpr float kDefaultReflectionDistance = 300.0f;

	float Smoothstep(float edge0, float edge1, float x) {
		const float t = std::clamp((x - edge0) / (std::max)(edge1 - edge0, 1e-4f), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
}

RiverWater::RiverWater() = default;

RiverWater::~RiverWater() {
	// シーンを抜ける時にまだ読んでいたら、終わるまで待ってから破棄する
	if (loadThread_.joinable()) loadThread_.join();
}

void RiverWater::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	const CloudLayer* cloudLayer) {
	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore, srvManager, object3DManager, "");
	water_->InitializeRiverMode();
	water_->SetCloudLayer(cloudLayer);
	// 板はカメラに追従させず、川のマップの範囲を覆う1枚にする（範囲外はシェーダが描かない）。
	// 範囲は ApplyRiverMap で JSON の値に合わせ直す
	water_->SetFollowCamera(false);
	water_->SetCenter({ 0.0f, 0.0f, (loaded_.zMin + loaded_.zMax) * 0.5f });
	water_->SetSize((std::max)(loaded_.xMax - loaded_.xMin, loaded_.zMax - loaded_.zMin));

	for (EntityTag t : kDefaultReflectTags) reflectTags_[static_cast<int>(t)] = true;
	// 映った山・地形も空と同じ揺れ方にする（13 番 §4.6）。タイトルの水面は 0 のまま
	water_->GetParams().reflectionDistance = kDefaultReflectionDistance;

	// 航跡（13 番 §8）：波はワールドに残し、範囲だけ自機について動かす。
	// 64m 四方で 1 テクセル 0.125m。波の速さの上限は CFL で 0.67·Δx·刻み数 なので、毎秒 120 刻みで約 10m/s まで
	{
		RippleSimulation* sim = water_->GetRippleSimulation();
		sim->SetScrollWithCenter(true);
		RippleSimulation::Settings& s = sim->GetSettings();
		s.simSize = 64.0f;
		s.waveSpeed = 7.0f;       // 自機 26m/s で V 字の半分の角度 asin(7/26) ≈ 16°
		s.damping = 0.995f;
		s.stepsPerSecond = 120.0f;
		s.edgeFade = 0.08f;
		s.normalScale = 1.0f;
	}
	// 波源が通った後の深いへこみで法線が傾きすぎ、反射が空の一色（平らな水色）になるのを防ぐ
	water_->GetParams().simMaxSlope = 0.3f;
	LoadTuning();

	// 反射 RT の解像度は作る時に決まるので、調整値を読んでから作る
	reflection_ = std::make_unique<WaterReflection>();
	reflection_->Initialize(dxCore, srvManager, object3DManager, reflectionScale_);
	reflection_->SetWaterHeight(waterHeight_);
	reflection_->SetFarClip(reflectionFarClip_);
	// エフェクト（GPU パーティクル・Primitive）も映す（13 番 §4.4）
	reflection_->SetDrawEffects(true);
}

void RiverWater::Update(float stageSec, float deltaTime, const Vector3* playerPosition) {
	PEPPER_SCOPE("RiverWater::Update");
	if (!water_) return;
	if (state_ == LoadState::NotStarted && stageSec >= loadStartSec_) {
		StartLoad();
	}
	if (state_ == LoadState::LoadingCPU && cpuDone_.load(std::memory_order_acquire)) {
		BeginUpload();
	}
	if (state_ == LoadState::UploadingGPU && DStorageManager::GetInstance()->IsFenceComplete(uploadFence_)) {
		TextureManager::GetInstance()->FinishTextureGPU(loaded_.riverMapPath);
		ApplyRiverMap();
	}
	if (state_ == LoadState::Ready) {
		// 範囲の中心を決めてから水面を進める（WaterSurface::Update がシミュレーションの中心を合わせる）
		UpdateWake(playerPosition, deltaTime);
		water_->Update(deltaTime);
	}

#ifdef _DEBUG
	// 反射の確認用：一定間隔でエフェクトを出し続ける
	if (testEffectAuto_ && state_ == LoadState::Ready) {
		testEffectTimer_ -= deltaTime;
		if (testEffectTimer_ <= 0.0f) {
			PlayTestEffect();
			testEffectTimer_ = testEffectInterval_;
		}
	}
	// 最後に出した位置に 10m の軸の線を出す（赤 X / 緑 Y / 青 Z）
	if (testEffectMarkerTimer_ > 0.0f) {
		testEffectMarkerTimer_ -= deltaTime;
		const Vector3& p = testEffectLastPosition_;
		auto* lr = LineRenderer::GetInstance();
		lr->AddLine({ p.x - 10.0f, p.y, p.z }, { p.x + 10.0f, p.y, p.z }, { 1.0f, 0.2f, 0.2f, 1.0f });
		lr->AddLine({ p.x, p.y - 10.0f, p.z }, { p.x, p.y + 10.0f, p.z }, { 0.2f, 1.0f, 0.2f, 1.0f });
		lr->AddLine({ p.x, p.y, p.z - 10.0f }, { p.x, p.y, p.z + 10.0f }, { 0.2f, 0.4f, 1.0f, 1.0f });
	}
#endif
}

void RiverWater::UpdateWake(const Vector3* playerPosition, float deltaTime) {
	if (!playerPosition || !wake_.enabled) {
		wakeHasPrev_ = false;
		wakeLastFactor_ = 0.0f;
		dropletTarget_ = 0.0f;
		return;
	}
	const Vector3 position = *playerPosition;

	float moveX = 0.0f;
	float moveZ = 0.0f;
	float moved = 0.0f;
	if (wakeHasPrev_) {
		moveX = position.x - wakePrevPosition_.x;
		moveZ = position.z - wakePrevPosition_.z;
		moved = std::sqrt(moveX * moveX + moveZ * moveZ);
		if (moved > wake_.teleportDistance) {
			// シーク・ワープ。前の位置から線を引かない
			ResetWake();
			moved = 0.0f;
		}
	}
	if (moved > 1e-3f) wakeForward_ = { moveX / moved, 0.0f, moveZ / moved };

	// 波源は自機より leadDistance 前（見た目のため。前のフレームの位置にも同じだけ足して線を引く）。
	// 範囲の中心は波源の後ろ寄り（航跡は後ろへ伸びる。前は少しあれば足りる）
	const float leadX = wakeForward_.x * wake_.leadDistance;
	const float leadZ = wakeForward_.z * wake_.leadDistance;
	const Vector3 center{
		position.x + leadX - wakeForward_.x * wake_.backOffset,
		waterHeight_,
		position.z + leadZ - wakeForward_.z * wake_.backOffset };
	water_->SetRippleCenter(center);
	RippleSimulation* sim = water_->GetRippleSimulation();
	sim->SetCenter({ center.x, center.z }); // 下の AddRippleImpulse が今の範囲で判定するように

	// 水面に近いほど・速いほど強い（11 番 §3.4：触れた時ではなく、近くを速く通った風で波立つ）
	const float height = position.y - waterHeight_;
	const float speed = deltaTime > 0.0f ? moved / deltaTime : 0.0f;
	// 水面より下（縁を越えて谷へ落ちた後）は川の上ではないので出さない
	const float factor = (1.0f - Smoothstep(wake_.fullHeight, wake_.maxHeight, height))
	                   * Smoothstep(wake_.minSpeed, wake_.fullSpeed, speed)
	                   * Smoothstep(-2.0f, 0.0f, height);
	wakeLastHeight_ = height;
	wakeLastSpeed_ = speed;
	wakeLastFactor_ = factor;
	// 画面の水滴は波と同期させる（波が出始める高さで小さな水滴が付き始め、波が最大の高さで濡れきる。13 番 §9.5）
	dropletTarget_ = factor;

	if (wakeHasPrev_ && moved > 1e-3f && factor > 0.0f) {
		// 速いと 1 フレームで 0.4m 以上進むので、軌跡に沿って細かく置く（点線にならないように）
		const int count = std::clamp(static_cast<int>(std::ceil(moved / (std::max)(wake_.spacing, 0.01f))),
			1, static_cast<int>(RippleSimulation::kMaxImpulsesPerFrame));
		const float strength = (std::min)(wake_.strengthPerMeter * moved / static_cast<float>(count), wake_.maxStrength) * factor;
		for (int i = 1; i <= count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(count);
			water_->AddRippleImpulse({ wakePrevPosition_.x + leadX + moveX * t, waterHeight_, wakePrevPosition_.z + leadZ + moveZ * t },
				wake_.radius, strength);
		}
	}

	wakePrevPosition_ = position;
	wakeHasPrev_ = true;
}

void RiverWater::ResetWake() {
	wakeHasPrev_ = false;
	if (water_) water_->GetRippleSimulation()->Clear();
}

void RiverWater::DispatchSimulation() {
	if (!water_ || state_ != LoadState::Ready) return;
	water_->DispatchSimulation();
}

void RiverWater::PlayTestEffect() {
	const Vector3 position{
		lastCameraPosition_.x,
		waterHeight_ + testEffectHeight_,
		lastCameraPosition_.z + testEffectAhead_ };
	testEffectLastPosition_ = position;
	testEffectLastHandle_ = EffectManager::GetInstance()->Play("Death_Drone", position);
	testEffectMarkerTimer_ = 3.0f;
}

void RiverWater::Seek(float stageSec) {
	ResetWake();
	if (!water_ || stageSec < loadStartSec_) return;
	// 窓より後へ飛んだら、その場で読み終える（雲の中で読むのを待てないため）
	if (state_ == LoadState::NotStarted) StartLoad();
	FinishLoadNow();
}

void RiverWater::StartLoad() {
	state_ = LoadState::LoadingCPU;
	cpuDone_.store(false, std::memory_order_relaxed);
	loadThread_ = std::thread([this]() {
		LoadedInfo info;
		auto result = JsonParser::ParseFile(kRiverInfoPath);
		if (result.success) {
			const JsonValue& root = result.value;
			info.riverMapPath = root["riverMap"].AsString("");
			info.xMin = ReadFloat(root, "x_min", info.xMin);
			info.xMax = ReadFloat(root, "x_max", info.xMax);
			info.zMin = ReadFloat(root, "z_min", info.zMin);
			info.zMax = ReadFloat(root, "z_max", info.zMax);
			info.maxDepth = ReadFloat(root, "maxDepth", info.maxDepth);
			info.waterHeight = ReadFloat(root, "waterHeight", info.waterHeight);
			info.ok = !info.riverMapPath.empty();
		}
		if (info.ok) {
			// 水深・流れは色ではないので sRGB として読まない
			TextureManager::GetInstance()->LoadTextureCPU(info.riverMapPath, true);
		}
		loaded_ = info;
		cpuDone_.store(true, std::memory_order_release);
	});
}

void RiverWater::BeginUpload() {
	if (loadThread_.joinable()) loadThread_.join();

	if (!loaded_.ok) {
		Log(std::string("[RiverWater] 川のマップの情報を読めない: ") + kRiverInfoPath + "\n");
		state_ = LoadState::Failed;
		return;
	}
	TextureManager* tm = TextureManager::GetInstance();
	if (!tm->IsCPUReady(loaded_.riverMapPath)) {
		Log("[RiverWater] 川のマップを読めない: " + loaded_.riverMapPath + "\n");
		state_ = LoadState::Failed;
		return;
	}
	// GPU フェーズ（リソース・SRV の作成）。DirectStorage 経路は積んで待たずに送り、fence が終わったら Update で仕上げる。
	// それ以外の経路はその場で GPU まで済む（13 番 §2.2、11 番 L9 S4）
	if (tm->BeginTextureGPUAsync(loaded_.riverMapPath)) {
		uploadFence_ = DStorageManager::GetInstance()->SubmitWithFence();
		state_ = LoadState::UploadingGPU;
		return;
	}
	ApplyRiverMap();
}

void RiverWater::FinishLoadNow() {
	if (state_ == LoadState::LoadingCPU) BeginUpload();
	if (state_ == LoadState::UploadingGPU) {
		// 送った途中の物は DirectStorage を待ってから仕上げる
		TextureManager::GetInstance()->LoadTextureGPU(loaded_.riverMapPath);
		ApplyRiverMap();
	}
}

void RiverWater::ApplyRiverMap() {
	WaterSurface::RiverMapInfo info;
	info.xMin = loaded_.xMin;
	info.xMax = loaded_.xMax;
	info.zMin = loaded_.zMin;
	info.zMax = loaded_.zMax;
	info.maxDepth = loaded_.maxDepth;
	water_->SetRiverMap(loaded_.riverMapPath, info);

	waterHeight_ = loaded_.waterHeight;
	water_->SetWaterHeight(waterHeight_);
	if (reflection_) reflection_->SetWaterHeight(waterHeight_);
	water_->SetCenter({ (loaded_.xMin + loaded_.xMax) * 0.5f, 0.0f, (loaded_.zMin + loaded_.zMax) * 0.5f });
	water_->SetSize((std::max)(loaded_.xMax - loaded_.xMin, loaded_.zMax - loaded_.zMin));

	state_ = water_->IsRiverMode() ? LoadState::Ready : LoadState::Failed;
}

void RiverWater::RenderReflection(const Camera& camera,
	const std::vector<std::unique_ptr<Object3DInstance>>& objects,
	const std::vector<std::unique_ptr<AnimatedObject3DInstance>>& animated) {
	reflectionActive_ = false;
	lastCameraPosition_ = camera.GetTranslate();
	if (!water_ || !reflection_ || state_ != LoadState::Ready) return;

	// 高い所から見下ろすと水面の反射率（フレネル）が低く映り込みはほぼ見えないので、パスごと飛ばす。
	// 足切りの手前 fadeWidth で薄くして、切り替わりを見せない
	const float height = camera.GetTranslate().y - waterHeight_;
	if (height <= 0.0f) return; // 水面の下（谷の中）。水面自体も描かない
	const float weight = std::clamp((reflectionCutoffHeight_ - height) / (std::max)(reflectionFadeWidth_, 1e-3f), 0.0f, 1.0f);
	if (weight <= 0.0f) return;

	// 敵・弾は出たり消えたりするので、毎フレーム登録し直す
	auto isReflected = [this](const auto& entity) {
		const int tag = static_cast<int>(Gameplay::Of(entity).GetTag());
		return tag >= 0 && tag < static_cast<int>(EntityTag::Count) && reflectTags_[tag];
	};
	reflection_->ClearTargets();
	for (const auto& o : objects) {
		if (isReflected(o)) reflection_->AddTarget(o.get());
	}
	for (const auto& a : animated) {
		if (isReflected(a)) reflection_->AddTarget(a.get());
	}
	reflection_->SetFarClip(reflectionFarClip_);
	reflection_->Render(camera);

	water_->GetParams().reflectionRtWeight = weight;
	reflectionActive_ = true;
}

void RiverWater::Draw(const Camera& camera, const std::string& skyCubemapPath) {
	if (!water_ || state_ != LoadState::Ready) return;
	// 谷の中など、カメラが水面より下にいる時は描かない（板は両面描画なので天井に見える）
	if (camera.GetTranslate().y <= waterHeight_) return;
	water_->Draw(camera, skyCubemapPath, reflectionActive_ ? reflection_.get() : nullptr);
}

void RiverWater::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;
	WaterSurface::Params& p = water_->GetParams();

	loadStartSec_ = ReadFloat(root, "loadStartSec", loadStartSec_);

	const JsonValue& river = root["river"];
	p.riverDepthThreshold = ReadFloat(river, "depthThreshold", p.riverDepthThreshold);
	p.riverShoreFade = ReadFloat(river, "shoreFade", p.riverShoreFade);
	p.riverOpacity = ReadFloat(river, "opacity", p.riverOpacity);
	p.riverMinCos = ReadFloat(river, "minCos", p.riverMinCos);
	p.riverShallowColor = ReadColor(river["shallowColor"], p.riverShallowColor);
	p.riverDeepColor = ReadColor(river["deepColor"], p.riverDeepColor);
	p.riverDeepDepth = ReadFloat(river, "deepDepth", p.riverDeepDepth);

	const JsonValue& surface = root["surface"];
	p.fresnelF0 = ReadFloat(surface, "fresnelF0", p.fresnelF0);
	p.reflectionIntensity = ReadFloat(surface, "reflectionIntensity", p.reflectionIntensity);
	p.skyIntensity = ReadFloat(surface, "skyIntensity", p.skyIntensity);
	p.ambient = ReadFloat(surface, "ambient", p.ambient);
	p.noiseAmplitude = ReadFloat(surface, "noiseAmplitude", p.noiseAmplitude);
	p.noiseScale = ReadFloat(surface, "noiseScale", p.noiseScale);
	p.noiseSpeed = ReadFloat(surface, "noiseSpeed", p.noiseSpeed);
	p.distortion = ReadFloat(surface, "distortion", p.distortion);
	p.reflectionDistance = ReadFloat(surface, "reflectionDistance", p.reflectionDistance);

	const JsonValue& refl = root["reflection"];
	reflectionScale_ = ReadFloat(refl, "resolutionScale", reflectionScale_);
	reflectionFarClip_ = ReadFloat(refl, "farClip", reflectionFarClip_);
	reflectionCutoffHeight_ = ReadFloat(refl, "cutoffHeight", reflectionCutoffHeight_);
	reflectionFadeWidth_ = ReadFloat(refl, "fadeWidth", reflectionFadeWidth_);
	const JsonValue& tags = refl["tags"];
	if (tags.IsArray()) {
		for (bool& b : reflectTags_) b = false;
		for (size_t i = 0; i < tags.Size(); ++i) {
			const EntityTag t = TagFromName(tags[i].AsString(""));
			if (t != EntityTag::None) reflectTags_[static_cast<int>(t)] = true;
		}
	}

	const JsonValue& wake = root["wake"];
	if (wake.IsObject()) {
		WakeTuning& w = wake_;
		w.enabled = wake["enabled"].AsBool(w.enabled);
		w.leadDistance = ReadFloat(wake, "leadDistance", w.leadDistance);
		w.backOffset = ReadFloat(wake, "backOffset", w.backOffset);
		w.maxHeight = ReadFloat(wake, "maxHeight", w.maxHeight);
		w.fullHeight = ReadFloat(wake, "fullHeight", w.fullHeight);
		w.minSpeed = ReadFloat(wake, "minSpeed", w.minSpeed);
		w.fullSpeed = ReadFloat(wake, "fullSpeed", w.fullSpeed);
		w.radius = ReadFloat(wake, "radius", w.radius);
		w.strengthPerMeter = ReadFloat(wake, "strengthPerMeter", w.strengthPerMeter);
		w.maxStrength = ReadFloat(wake, "maxStrength", w.maxStrength);
		w.spacing = ReadFloat(wake, "spacing", w.spacing);
		w.teleportDistance = ReadFloat(wake, "teleportDistance", w.teleportDistance);
		RippleSimulation::Settings& s = water_->GetRippleSimulation()->GetSettings();
		s.simSize = ReadFloat(wake, "simSize", s.simSize);
		s.waveSpeed = ReadFloat(wake, "waveSpeed", s.waveSpeed);
		s.damping = ReadFloat(wake, "damping", s.damping);
		s.stepsPerSecond = ReadFloat(wake, "stepsPerSecond", s.stepsPerSecond);
		s.edgeFade = ReadFloat(wake, "edgeFade", s.edgeFade);
		s.normalScale = ReadFloat(wake, "normalScale", s.normalScale);
		p.simMaxSlope = ReadFloat(wake, "maxSlope", p.simMaxSlope);
	}
}

void RiverWater::SaveTuning() const {
	const WaterSurface::Params& p = water_->GetParams();
	JsonValue root = JsonValue::MakeObject();
	root["loadStartSec"] = static_cast<double>(loadStartSec_);

	JsonValue river = JsonValue::MakeObject();
	river["depthThreshold"] = static_cast<double>(p.riverDepthThreshold);
	river["shoreFade"] = static_cast<double>(p.riverShoreFade);
	river["opacity"] = static_cast<double>(p.riverOpacity);
	river["minCos"] = static_cast<double>(p.riverMinCos);
	river["shallowColor"] = WriteColor(p.riverShallowColor);
	river["deepColor"] = WriteColor(p.riverDeepColor);
	river["deepDepth"] = static_cast<double>(p.riverDeepDepth);
	root["river"] = std::move(river);

	JsonValue surface = JsonValue::MakeObject();
	surface["fresnelF0"] = static_cast<double>(p.fresnelF0);
	surface["reflectionIntensity"] = static_cast<double>(p.reflectionIntensity);
	surface["skyIntensity"] = static_cast<double>(p.skyIntensity);
	surface["ambient"] = static_cast<double>(p.ambient);
	surface["noiseAmplitude"] = static_cast<double>(p.noiseAmplitude);
	surface["noiseScale"] = static_cast<double>(p.noiseScale);
	surface["noiseSpeed"] = static_cast<double>(p.noiseSpeed);
	surface["distortion"] = static_cast<double>(p.distortion);
	surface["reflectionDistance"] = static_cast<double>(p.reflectionDistance);
	root["surface"] = std::move(surface);

	JsonValue refl = JsonValue::MakeObject();
	refl["resolutionScale"] = static_cast<double>(reflectionScale_);
	refl["farClip"] = static_cast<double>(reflectionFarClip_);
	refl["cutoffHeight"] = static_cast<double>(reflectionCutoffHeight_);
	refl["fadeWidth"] = static_cast<double>(reflectionFadeWidth_);
	JsonValue tags = JsonValue::MakeArray();
	for (int i = 0; i < static_cast<int>(EntityTag::Count); ++i) {
		if (reflectTags_[i]) tags.Push(JsonValue(std::string(GetTagName(static_cast<EntityTag>(i)))));
	}
	refl["tags"] = std::move(tags);
	root["reflection"] = std::move(refl);

	const WakeTuning& w = wake_;
	const RippleSimulation::Settings& s = water_->GetRippleSimulation()->GetSettings();
	JsonValue wake = JsonValue::MakeObject();
	wake["enabled"] = w.enabled;
	wake["leadDistance"] = static_cast<double>(w.leadDistance);
	wake["backOffset"] = static_cast<double>(w.backOffset);
	wake["maxHeight"] = static_cast<double>(w.maxHeight);
	wake["fullHeight"] = static_cast<double>(w.fullHeight);
	wake["minSpeed"] = static_cast<double>(w.minSpeed);
	wake["fullSpeed"] = static_cast<double>(w.fullSpeed);
	wake["radius"] = static_cast<double>(w.radius);
	wake["strengthPerMeter"] = static_cast<double>(w.strengthPerMeter);
	wake["maxStrength"] = static_cast<double>(w.maxStrength);
	wake["spacing"] = static_cast<double>(w.spacing);
	wake["teleportDistance"] = static_cast<double>(w.teleportDistance);
	wake["simSize"] = static_cast<double>(s.simSize);
	wake["waveSpeed"] = static_cast<double>(s.waveSpeed);
	wake["damping"] = static_cast<double>(s.damping);
	wake["stepsPerSecond"] = static_cast<double>(s.stepsPerSecond);
	wake["edgeFade"] = static_cast<double>(s.edgeFade);
	wake["normalScale"] = static_cast<double>(s.normalScale);
	wake["maxSlope"] = static_cast<double>(p.simMaxSlope);
	root["wake"] = std::move(wake);

	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void RiverWater::OnImGui() {
#ifdef _DEBUG
	if (!water_) return;
	const char* stateNames[] = { "Not started", "Loading (CPU)", "Ready", "Failed" };
	ImGui::Text("State: %s", stateNames[static_cast<int>(state_)]);
	ImGui::DragFloat("Load Start [s]", &loadStartSec_, 0.5f, 0.0f, 180.0f, "%.1f");
	if (ImGui::Button("Save River Water")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload Tuning")) LoadTuning();

	if (reflection_ && ImGui::TreeNode("Reflection")) {
		ImGui::Text("Active: %s / drawn %u", reflectionActive_ ? "yes" : "no", reflection_->GetDrawnCount());
		ImGui::DragFloat("Resolution Scale (restart)", &reflectionScale_, 0.05f, 0.25f, 1.0f);
		ImGui::DragFloat("Far Clip [m]", &reflectionFarClip_, 10.0f, 10.0f, 10000.0f);
		ImGui::DragFloat("Cutoff Height [m]", &reflectionCutoffHeight_, 1.0f, 0.0f, 2000.0f);
		ImGui::DragFloat("Fade Width [m]", &reflectionFadeWidth_, 1.0f, 0.0f, 500.0f);
		ImGui::TextUnformatted("Tags");
		for (int i = 1; i < static_cast<int>(EntityTag::Count); ++i) {
			const std::string name(GetTagName(static_cast<EntityTag>(i)));
			ImGui::Checkbox(name.c_str(), &reflectTags_[i]);
		}
		ImGui::SeparatorText("Test Effect (Death_Drone)");
		if (ImGui::Button("Play Test Effect")) PlayTestEffect();
		ImGui::SameLine();
		ImGui::Checkbox("Auto Repeat", &testEffectAuto_);
		ImGui::DragFloat("Interval [s]", &testEffectInterval_, 0.05f, 0.1f, 10.0f);
		ImGui::DragFloat("Ahead [m]", &testEffectAhead_, 1.0f, 0.0f, 500.0f);
		ImGui::DragFloat("Height [m]", &testEffectHeight_, 0.1f, -10.0f, 100.0f);
		{
			auto* em = EffectManager::GetInstance();
			ImGui::Text("Def found: %s", em->HasDef("Death_Drone") ? "yes" : "NO");
			ImGui::Text("Camera: (%.1f, %.1f, %.1f)", lastCameraPosition_.x, lastCameraPosition_.y, lastCameraPosition_.z);
			ImGui::Text("Last: (%.1f, %.1f, %.1f) handle %llu %s",
				testEffectLastPosition_.x, testEffectLastPosition_.y, testEffectLastPosition_.z,
				static_cast<unsigned long long>(testEffectLastHandle_),
				(testEffectLastHandle_ != 0 && em->IsAlive(testEffectLastHandle_)) ? "(alive)" : "(ended)");
			ImGui::Text("Active effects: %zu", em->GetActiveInstanceCount());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Screen Droplets")) {
		// PostEffect の窓は有効な時しか値を出さないので、ここからも触れるようにする
		// 値は Tuning/ScreenDroplet.json（川＝Gravity・雲の縦穴＝Radial の 2 組）。ここは触りやすい置き場所
		ImGui::Text("River target %.2f (= Wake Factor. heights: Wake > Max/Full Height)", GetDropletTarget());
		if (auto* pe = Game::GetPostEffect(); pe && pe->screenDroplet) pe->screenDroplet->ShowImGui();
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Wake")) {
		WakeTuning& w = wake_;
		RippleSimulation* sim = water_->GetRippleSimulation();
		RippleSimulation::Settings& s = sim->GetSettings();
		ImGui::Checkbox("Enabled##wake", &w.enabled);
		ImGui::Text("Height %.2f m / Speed %.1f m/s / Factor %.2f", wakeLastHeight_, wakeLastSpeed_, wakeLastFactor_);
		// V 字の半分の角度 = asin(波の速さ / 自機の速さ)。波の方が速いと V にならない
		if (wakeLastSpeed_ > s.waveSpeed) {
			ImGui::Text("V half-angle: %.1f deg", std::asin(s.waveSpeed / wakeLastSpeed_) * 57.29578f);
		} else {
			ImGui::TextUnformatted("V half-angle: - (slower than waves)");
		}
		{
			// k = (c·Δt/Δx)² が 0.45 で止まる速さ（これより上げても伝わる速さは変わらない）
			const float dx = s.simSize / static_cast<float>(RippleSimulation::kResolution);
			ImGui::Text("Max wave speed (CFL): %.1f m/s", 0.67f * dx * s.stepsPerSecond);
		}
		ImGui::SeparatorText("Source");
		ImGui::DragFloat("Max Height [m]", &w.maxHeight, 0.05f, 0.0f, 20.0f);
		ImGui::DragFloat("Full Height [m]", &w.fullHeight, 0.05f, -5.0f, 20.0f);
		ImGui::DragFloat("Min Speed [m/s]", &w.minSpeed, 0.5f, 0.0f, 100.0f);
		ImGui::DragFloat("Full Speed [m/s]", &w.fullSpeed, 0.5f, 0.0f, 100.0f);
		ImGui::DragFloat("Radius [m]", &w.radius, 0.01f, 0.05f, 5.0f);
		ImGui::DragFloat("Strength / m", &w.strengthPerMeter, 0.001f, 0.0f, 0.5f, "%.4f");
		ImGui::DragFloat("Max Strength", &w.maxStrength, 0.001f, 0.0f, 0.5f, "%.4f");
		ImGui::DragFloat("Spacing [m]", &w.spacing, 0.01f, 0.01f, 5.0f);
		ImGui::DragFloat("Lead Distance [m]", &w.leadDistance, 0.5f, -20.0f, 60.0f);
		ImGui::DragFloat("Back Offset [m]", &w.backOffset, 0.5f, -50.0f, 50.0f);
		ImGui::DragFloat("Teleport Distance [m]", &w.teleportDistance, 1.0f, 1.0f, 500.0f);
		ImGui::SeparatorText("Waves");
		ImGui::DragFloat("Sim Size [m]", &s.simSize, 0.5f, 8.0f, 256.0f);
		ImGui::DragFloat("Wave Speed [m/s]", &s.waveSpeed, 0.05f, 0.05f, 30.0f);
		ImGui::SliderFloat("Damping", &s.damping, 0.9f, 1.0f, "%.4f");
		ImGui::DragFloat("Steps / s", &s.stepsPerSecond, 1.0f, 10.0f, 240.0f);
		ImGui::SliderFloat("Edge Fade", &s.edgeFade, 0.001f, 0.5f);
		ImGui::DragFloat("Normal Scale", &s.normalScale, 0.01f, 0.0f, 20.0f);
		ImGui::DragFloat("Max Slope (0=off)", &water_->GetParams().simMaxSlope, 0.005f, 0.0f, 5.0f, "%.3f");
		if (ImGui::Button("Clear Waves")) sim->Clear();
		ImGui::TreePop();
	}
	water_->OnImGui();
#endif
}
