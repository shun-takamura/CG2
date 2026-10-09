#include "RiverWater.h"

#include "Water/WaterSurface.h"
#include "Water/WaterReflection.h"
#include "Object3DInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Components/Gameplay.h"
#include "Effect/EffectManager.h"
#include "Primitive/LineRenderer.h"
#include "TextureManager.h"
#include "Camera.h"
#include "Log.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
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
	// 範囲は FinishLoad で JSON の値に合わせ直す
	water_->SetFollowCamera(false);
	water_->SetCenter({ 0.0f, 0.0f, (loaded_.zMin + loaded_.zMax) * 0.5f });
	water_->SetSize((std::max)(loaded_.xMax - loaded_.xMin, loaded_.zMax - loaded_.zMin));

	for (EntityTag t : kDefaultReflectTags) reflectTags_[static_cast<int>(t)] = true;
	// 映った山・地形も空と同じ揺れ方にする（13 番 §4.6）。タイトルの水面は 0 のまま
	water_->GetParams().reflectionDistance = kDefaultReflectionDistance;
	LoadTuning();

	// 反射 RT の解像度は作る時に決まるので、調整値を読んでから作る
	reflection_ = std::make_unique<WaterReflection>();
	reflection_->Initialize(dxCore, srvManager, object3DManager, reflectionScale_);
	reflection_->SetWaterHeight(waterHeight_);
	reflection_->SetFarClip(reflectionFarClip_);
	// エフェクト（GPU パーティクル・Primitive）も映す（13 番 §4.4）
	reflection_->SetDrawEffects(true);
}

void RiverWater::Update(float stageSec, float deltaTime) {
	if (!water_) return;
	if (state_ == LoadState::NotStarted && stageSec >= loadStartSec_) {
		StartLoad();
	}
	if (state_ == LoadState::LoadingCPU && cpuDone_.load(std::memory_order_acquire)) {
		FinishLoad();
	}
	if (state_ == LoadState::Ready) {
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
	if (!water_ || stageSec < loadStartSec_) return;
	// 窓より後へ飛んだら、その場で読み終える（雲の中で読むのを待てないため）
	if (state_ == LoadState::NotStarted) StartLoad();
	if (state_ == LoadState::LoadingCPU) FinishLoad();
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

void RiverWater::FinishLoad() {
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
	// GPU フェーズ（リソース・SRV の作成）。既に GPU まで済んでいれば何もしない
	tm->LoadTextureGPU(loaded_.riverMapPath);

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
	water_->OnImGui();
#endif
}
