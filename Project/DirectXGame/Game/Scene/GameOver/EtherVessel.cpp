#include "EtherVessel.h"

#include "EtherVesselGlass.h"
#include "EtherPillarFlow.h"
#include "PlayerDeath/EtherStrands.h"
#include "Glass/LiquidGlassSphere.h"
#include "Object3DInstance.h"
#include "Effect/EffectManager.h"
#include "AssetLocator.h"
#include "Camera.h"
#include "MathUtility.h"   // kPi
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// G4 の gen_ether_vessel.py が書く寸法（部品の位置・球・文字盤）
	constexpr const char* kGeometryPath = "Resources/Json/Tuning/EtherVesselGeometry.json";
	constexpr const char* kPartNames[] = { "Table", "Pillar", "RingStand", "Dial", "Swing" };
	// 死亡演出の昇る粒（G1）。displayName が Ether で始まる部品だけエーテルの色で塗る
	constexpr const char* kAscendEffect = "PlayerEtherAscend";
	constexpr const char* kEtherPrefix = "Ether";
	constexpr float kDegToRad = kPi / 180.0f;

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	void Tint(Vector4& color, const Vector3& tint) {
		color.x *= tint.x;
		color.y *= tint.y;
		color.z *= tint.z;
	}

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
}

EtherVessel::EtherVessel() = default;
EtherVessel::~EtherVessel() {
	StopEffects();
}

void EtherVessel::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager) {
	dxCore_ = dxCore;
	glass_ = std::make_unique<EtherVesselGlass>();
	glass_->Initialize(dxCore, srvManager);
	strands_ = std::make_unique<EtherStrands>();
	strands_->Initialize();
	pillarFlow_ = std::make_unique<EtherPillarFlow>();
	pillarFlow_->Initialize();
	LoadGeometry(object3DManager);
	SetOrigin(origin_);
	SetDifficulty01(difficulty01_, true);
}

void EtherVessel::Finalize() {
	StopEffects();
	if (strands_) strands_->Reset();
}

void EtherVessel::LoadGeometry(Object3DManager* object3DManager) {
	parts_.clear();
	swing_ = nullptr;
	if (!std::filesystem::exists(kGeometryPath)) return;
	const auto result = JsonParser::ParseFile(kGeometryPath);
	if (!result.success) return;
	iDegrees_ = static_cast<float>(result.value["dial"]["iDegrees"].AsDouble(iDegrees_));
	const JsonValue& parts = result.value["parts"];
	for (const char* name : kPartNames) {
		const JsonValue& part = parts[name];
		const std::string model = part["model"].AsString(std::string());
		const size_t slash = model.find_last_of('/');
		if (slash == std::string::npos || !AssetLocator::GetInstance()->Exists(model)) continue;
		Part p;
		ReadVector3(part["position"], p.position);
		p.object = std::make_unique<Object3DInstance>();
		p.object->Initialize(object3DManager, dxCore_, model.substr(0, slash), model.substr(slash + 1),
			std::string("EtherVessel") + name);
		if (std::string(name) == "Swing") swing_ = p.object.get();
		parts_.push_back(std::move(p));
	}
}

void EtherVessel::SetOrigin(const Vector3& origin) {
	origin_ = origin;
	if (glass_) glass_->SetOrigin(origin);
	if (pillarFlow_) pillarFlow_->SetOrigin(origin);
	for (Part& p : parts_) {
		p.object->SetTranslate({ origin.x + p.position.x, origin.y + p.position.y, origin.z + p.position.z });
	}
}

Vector3 EtherVessel::GetSphereCenter() const {
	return glass_ ? glass_->GetSphereCenter() : origin_;
}

void EtherVessel::SetFill(float fill, bool immediate) {
	if (glass_) glass_->SetFill(fill, immediate);
}

void EtherVessel::SetDifficulty01(float difficulty01, bool immediate) {
	difficulty01 = std::clamp(difficulty01, 0.0f, 1.0f);
	// I（0）は真上から +X 側へ iDegrees、IX（1）は −X 側へ。+ の傾きが I の側
	const float toDeg = iDegrees_ * (1.0f - 2.0f * difficulty01);
	const float nowDeg = glass_ ? glass_->GetTilt() / kDegToRad : toDeg;
	if (immediate) {
		tickTimer_ = -1.0f;
		glowTimer_ = -1.0f;
		tiltFromDeg_ = tiltToDeg_ = toDeg;
		glowFrom01_ = glowTo01_ = difficulty01;
		if (glass_) {
			glass_->SetTilt(toDeg * kDegToRad);
			glass_->SetDifficulty(difficulty01);
		}
	} else {
		tiltFromDeg_ = nowDeg;
		tiltToDeg_ = toDeg;
		tickTimer_ = 0.0f;
		glowFrom01_ = difficulty01_;
		glowTo01_ = difficulty01;
		glowTimer_ = 0.0f;
	}
	difficulty01_ = difficulty01;
}

void EtherVessel::SetGlowBoost(float boost) {
	if (glass_) glass_->SetGlowBoost(boost);
}

void EtherVessel::Splash(float strength) {
	if (glass_) glass_->Splash(strength);
}

void EtherVessel::BeginRelease(float rateScale) {
	releaseRateScale_ = rateScale;
	if (releasing_) return;
	releasing_ = true;
	strandAccum_ = 0.0f;

	// 昇る粒：死亡演出の粒を球の大きさに合わせ、球の中心から流す（ループ。StopRelease で止める）
	EffectManager* em = EffectManager::GetInstance();
	const EffectDef* source = em->FindDef(kAscendEffect);
	if (!source || !glass_) return;
	EffectDef def = *source;
	const float radius = glass_->GetSphereRadius();
	// 液面のあたりから（糸と同じく液面から抜けていく）
	const float surface = glass_->GetSphere() ? glass_->GetSphere()->GetSurfaceOffset() : 0.0f;
	for (EffectParticleComponent& p : def.particles) {
		p.offset = { 0.0f, surface, 0.0f };
		p.emitRadius = radius * ascendRadiusScale_;
		p.scaleMin = { p.scaleMin.x * ascendSizeScale_, p.scaleMin.y * ascendSizeScale_ };
		p.scaleMax = { p.scaleMax.x * ascendSizeScale_, p.scaleMax.y * ascendSizeScale_ };
		p.particleLifeTime *= ascendLifeScale_;
		if (p.displayName.rfind(kEtherPrefix, 0) != 0) continue;
		Tint(p.startColor, etherColor_);
		Tint(p.endColor, etherColor_);
		for (EffectColorKey& key : p.colorKeys) Tint(key.color, etherColor_);
	}
	ascendHandle_ = em->PlayWithDef(def, GetSphereCenter());
}

void EtherVessel::StopRelease() {
	releasing_ = false;
	StopEffects();
}

void EtherVessel::StopEffects() {
	if (ascendHandle_ != kInvalidEffectHandle) EffectManager::GetInstance()->Stop(ascendHandle_);
	ascendHandle_ = kInvalidEffectHandle;
}

void EtherVessel::EmitStrand() {
	if (!glass_) return;
	// 液面のすぐ下から。架空の物なのでガラスをすり抜けて天へ昇る（§4.5）。
	// 液面に小さな波紋を立てて「液面から抜けていく」ように見せる（中のあちこちから出すと液面が反応せず、減ったように見えなかった）
	LiquidGlassSphere* sphere = glass_->GetSphere();
	if (!sphere) return;
	const Vector3 c = GetSphereCenter();
	const float radius = glass_->GetSphereRadius();
	const float surface = sphere->GetSurfaceOffset();
	std::uniform_real_distribution<float> d01(0.0f, 1.0f);
	const float y = surface - d01(random_) * strandDepth_ * radius;
	// その高さでの球の断面の半径の内側
	const float ring = std::sqrt((std::max)(radius * radius - y * y, 0.0f)) * strandSpread_;
	const float angle = d01(random_) * 2.0f * kPi;
	const float dist = std::sqrt(d01(random_)) * ring;
	const float x = std::cos(angle) * dist;
	const float z = std::sin(angle) * dist;
	strands_->Emit({ c.x + x, c.y + y, c.z + z }, strandScale_);
	if (strandRippleStrength_ > 0.0f) {
		sphere->AddRipple({ c.x + x, c.y + surface, c.z + z }, strandRippleRadius_, strandRippleStrength_);
	}
}

void EtherVessel::Update(float deltaTime) {
	if (!glass_) return;

	// 針：目で追える速さで動き、少し行き過ぎて戻る（ease-out-back。2026-10-24 ユーザー「針の動きがわかりやすいように」）。
	// 止まった所から液面が遅れて揺れる（G5 の揺れ）
	if (tickTimer_ >= 0.0f) {
		tickTimer_ += deltaTime;
		const float t = tickSeconds_ > 1e-3f ? std::clamp(tickTimer_ / tickSeconds_, 0.0f, 1.0f) : 1.0f;
		const float s = t - 1.0f;
		const float eased = 1.0f + (tickBack_ + 1.0f) * s * s * s + tickBack_ * s * s;
		glass_->SetTilt((tiltFromDeg_ + (tiltToDeg_ - tiltFromDeg_) * eased) * kDegToRad);
		if (t >= 1.0f) tickTimer_ = -1.0f;
	}
	// 光：新しい難易度の強さへゆっくり寄る
	if (glowTimer_ >= 0.0f) {
		glowTimer_ += deltaTime;
		const float t = glowFadeSeconds_ > 1e-3f ? std::clamp(glowTimer_ / glowFadeSeconds_, 0.0f, 1.0f) : 1.0f;
		glass_->SetDifficulty(glowFrom01_ + (glowTo01_ - glowFrom01_) * SmoothStep01(t));
		if (t >= 1.0f) glowTimer_ = -1.0f;
	}

	if (releasing_) {
		strandAccum_ += (std::max)(strandRate_ * releaseRateScale_, 0.0f) * deltaTime;
		while (strandAccum_ >= 1.0f) {
			strandAccum_ -= 1.0f;
			EmitStrand();
		}
	}
	strands_->SetAnchor(GetSphereCenter());
	strands_->Update(deltaTime);
	pillarFlow_->Update(deltaTime);
	if (ascendHandle_ != kInvalidEffectHandle) EffectManager::GetInstance()->SetPosition(ascendHandle_, GetSphereCenter());

	glass_->Update(deltaTime);
	for (Part& p : parts_) {
		if (p.object.get() == swing_) p.object->SetRotate({ 0.0f, 0.0f, glass_->GetRotationZ() });
		p.object->Update();
	}
}

void EtherVessel::DispatchSimulation() {
	if (glass_) glass_->DispatchSimulation();
}

void EtherVessel::DrawOpaque(DirectXCore* dxCore) {
	for (Part& p : parts_) p.object->Draw(dxCore);
}

void EtherVessel::DrawTransparent(Camera* camera, const std::string& environmentCubemapPath) {
	if (!glass_ || !camera) return;
	glass_->Draw(*camera, environmentCubemapPath);
	pillarFlow_->Draw(camera, etherColor_);
	strands_->Draw(camera, etherColor_);
}

void EtherVessel::DrawBloom(Camera* camera, const std::string& environmentCubemapPath) {
	if (!glass_ || !camera) return;
	glass_->DrawBloom(*camera, environmentCubemapPath);
	pillarFlow_->DrawBloom();
	strands_->DrawBloom();
}

void EtherVessel::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	ReadVector3(node["etherColor"], etherColor_);
	read("tickSeconds", tickSeconds_);
	read("tickBack", tickBack_);
	read("glowFadeSeconds", glowFadeSeconds_);
	read("strandRate", strandRate_);
	read("strandScale", strandScale_);
	read("strandSpread", strandSpread_);
	read("strandDepth", strandDepth_);
	read("strandRippleStrength", strandRippleStrength_);
	read("strandRippleRadius", strandRippleRadius_);
	read("ascendRadiusScale", ascendRadiusScale_);
	read("ascendSizeScale", ascendSizeScale_);
	read("ascendLifeScale", ascendLifeScale_);
	if (strands_) strands_->LoadTuning(node["strands"]);
	if (pillarFlow_) pillarFlow_->LoadTuning(node["pillarFlow"]);
}

void EtherVessel::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["etherColor"] = MakeVector3(etherColor_);
	write("tickSeconds", tickSeconds_);
	write("tickBack", tickBack_);
	write("glowFadeSeconds", glowFadeSeconds_);
	write("strandRate", strandRate_);
	write("strandScale", strandScale_);
	write("strandSpread", strandSpread_);
	write("strandDepth", strandDepth_);
	write("strandRippleStrength", strandRippleStrength_);
	write("strandRippleRadius", strandRippleRadius_);
	write("ascendRadiusScale", ascendRadiusScale_);
	write("ascendSizeScale", ascendSizeScale_);
	write("ascendLifeScale", ascendLifeScale_);
	if (strands_) strands_->SaveTuning(node["strands"]);
	if (pillarFlow_) pillarFlow_->SaveTuning(node["pillarFlow"]);
}

void EtherVessel::SaveGlassTuning() const {
	if (glass_) glass_->SaveTuning();
}

void EtherVessel::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Parts: %d", static_cast<int>(parts_.size()));
	ImGui::ColorEdit3("Ether Color##Vessel", &etherColor_.x, ImGuiColorEditFlags_Float);
	ImGui::DragFloat("Needle Seconds", &tickSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Needle Overshoot", &tickBack_, 0.05f, 0.0f, 6.0f);
	ImGui::DragFloat("Glow Fade Seconds", &glowFadeSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Strand Rate [/s]", &strandRate_, 0.5f, 0.0f, 400.0f);
	ImGui::DragFloat("Strand Scale", &strandScale_, 0.01f, 0.01f, 3.0f);
	ImGui::DragFloat("Strand Spread (x section)", &strandSpread_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Strand Depth (x radius)", &strandDepth_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Strand Ripple Strength", &strandRippleStrength_, 0.0001f, 0.0f, 0.02f, "%.4f");
	ImGui::DragFloat("Strand Ripple Radius [m]", &strandRippleRadius_, 0.001f, 0.001f, 0.2f, "%.3f");
	ImGui::DragFloat("Ascend Radius (x radius)", &ascendRadiusScale_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Ascend Size Scale", &ascendSizeScale_, 0.01f, 0.01f, 3.0f);
	ImGui::DragFloat("Ascend Life Scale", &ascendLifeScale_, 0.01f, 0.05f, 3.0f);
	if (ImGui::TreeNode("Strands##Vessel")) {
		strands_->OnImGui();
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Pillar Flow##Vessel")) {
		pillarFlow_->OnImGui();
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Glass / Ether (EtherVesselGlass.json)")) {
		glass_->OnImGui();
		ImGui::TreePop();
	}
#endif
}
