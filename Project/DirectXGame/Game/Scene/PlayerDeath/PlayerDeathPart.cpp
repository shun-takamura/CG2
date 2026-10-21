#include "PlayerDeathPart.h"

#include "EtherStrands.h"
#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "Effect/EffectManager.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Material.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// エフェクト（Resources/Json/Effects）。displayName が kEtherPrefix で始まる部品はエーテルの色で塗る
	constexpr const char* kAscendEffect = "PlayerEtherAscend";    // 昇る光の粒（ループ。ほどけ終わりで止める）
	constexpr const char* kLightEffect = "PlayerEtherLight";      // エーテルの色の点光源
	constexpr const char* kFeatherEffect = "PlayerFeatherBurn";   // 焼ける羽根の小さな粒子（オレンジの縁のディゾルブ。塗らない）
	constexpr const char* kEtherPrefix = "Ether";

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	float WrapAngle(float a) {
		while (a > kPi) a -= 2.0f * kPi;
		while (a < -kPi) a += 2.0f * kPi;
		return a;
	}

	bool IsEther(const std::string& name) {
		return name.rfind(kEtherPrefix, 0) == 0;
	}

	void Tint(Vector4& color, const Vector3& tint) {
		color.x *= tint.x;
		color.y *= tint.y;
		color.z *= tint.z;
	}

	Vector3 Translation(const Matrix4x4& m) {
		return { m.m[3][0], m.m[3][1], m.m[3][2] };
	}

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue node = JsonValue::MakeArray();
		node.Push(JsonValue(static_cast<double>(v.x)));
		node.Push(JsonValue(static_cast<double>(v.y)));
		node.Push(JsonValue(static_cast<double>(v.z)));
		return node;
	}
}

PlayerDeathPart::PlayerDeathPart()
	: strands_(std::make_unique<EtherStrands>()) {
}

PlayerDeathPart::~PlayerDeathPart() = default;

void PlayerDeathPart::Initialize(IPlayerDeathHost* host) {
	host_ = host;
	strands_->Initialize();
}

void PlayerDeathPart::Finalize() {
	StopEffects();
	state_ = State::Idle;
	player_ = nullptr;
}

void PlayerDeathPart::Begin(Kind kind, AnimatedObject3DInstance* player, bool preview) {
	if (!player) return;
	if (IsActive()) Cancel();
	kind_ = kind;
	player_ = player;
	preview_ = preview;
	strandCarry_ = 0.0f;
	featherCarry_ = 0.0f;
	elapsed_ = 0.0f;
	wingBurnTime_ = -1.0f;
	strands_->Reset();
	Enter(PreludeSeconds() > 0.0f ? State::Prelude : State::Glow);
}

void PlayerDeathPart::Cancel() {
	if (!IsActive()) return;
	StopEffects();
	strands_->Reset();
	RestorePlayer();
	state_ = State::Idle;
	stateTime_ = 0.0f;
	wingBurnTime_ = -1.0f;
}

void PlayerDeathPart::RestorePlayer() {
	if (!player_) return;
	player_->ClearDissolve();
	player_->SetVisible(true);
	dissolving_ = false;
}

float PlayerDeathPart::PreludeSeconds() const {
	return kind_ == Kind::Boss ? preludeBossSeconds_ : preludeRailSeconds_;
}

void PlayerDeathPart::Enter(State state) {
	state_ = state;
	stateTime_ = 0.0f;
	EffectManager* em = EffectManager::GetInstance();
	switch (state) {
	case State::Glow:
		if (player_) lightHandle_ = PlayEther(kLightEffect, player_->GetTranslate());
		// シューティング：羽根が開いた形のまま焼け落ちる（オレンジの縁のディゾルブ＋羽根の関節から焼ける羽根の粒子）。
		// 機体のエーテル化と同時に進める（§1.1）
		if (kind_ == Kind::Rail && host_) {
			host_->BurnPlayerWings(wingBurnSeconds_);
			wingBurnTime_ = 0.0f;
		}
		break;
	case State::Unravel:
		if (player_) ascendHandle_ = PlayEther(kAscendEffect, player_->GetTranslate());
		break;
	case State::Linger:
		// 機体はほどけ切った。影のパスはディゾルブを見ないので、隠して影も消す
		if (player_) {
			player_->ClearDissolve();
			player_->SetVisible(false);
		}
		dissolving_ = false;
		if (ascendHandle_ != 0) {
			em->Stop(ascendHandle_);
			ascendHandle_ = 0;
		}
		break;
	case State::Finished:
		StopEffects();
		if (preview_) {
			// 調整用：ゲームオーバーへは行かず元に戻す（羽根は StagePlay がレールの間に出し直す）
			strands_->Reset();
			RestorePlayer();
			state_ = State::Idle;
		} else if (host_) {
			host_->FinishPlayerDeath();
		}
		break;
	default:
		break;
	}
}

bool PlayerDeathPart::RefreshBody(float& minY, float& maxY) {
	if (!player_ || !player_->HasSkeleton()) return false;
	const Skeleton& skeleton = player_->GetSkeleton();
	const size_t count = skeleton.joints.size();
	if (count == 0) return false;
	jointPositions_.resize(count);
	jointParents_.resize(count);
	minY = 1.0e30f;
	maxY = -1.0e30f;
	for (size_t i = 0; i < count; ++i) {
		const Joint& joint = skeleton.joints[i];
		jointPositions_[i] = Translation(player_->GetJointWorldMatrix(joint.name));
		jointParents_[i] = joint.parent ? *joint.parent : -1;
		minY = (std::min)(minY, jointPositions_[i].y);
		maxY = (std::max)(maxY, jointPositions_[i].y);
	}
	const float s = player_->GetScale().x;
	minY -= bottomMargin_ * s;
	maxY += topMargin_ * s;
	return true;
}

void PlayerDeathPart::ApplyDissolve(float planeHeight) {
	if (!player_) return;
	const float s = player_->GetScale().x;
	DissolvePlane d{};
	d.height = planeHeight;
	d.edgeWidth = edgeWidth_ * s;
	d.edgeColor = { etherColor_.x * edgeIntensity_, etherColor_.y * edgeIntensity_, etherColor_.z * edgeIntensity_ };
	d.noiseScale = s > 1e-4f ? noiseScale_ / s : noiseScale_;
	d.noiseAmplitude = noiseAmplitude_ * s;
	player_->SetDissolvePlane(d);
	dissolving_ = true;
}

void PlayerDeathPart::EmitStrands(float planeHeight, float deltaTime) {
	if (!player_ || jointPositions_.empty()) return;
	strandCarry_ += strandRate_ * deltaTime;
	const int count = static_cast<int>(strandCarry_);
	strandCarry_ -= static_cast<float>(count);

	const float s = player_->GetScale().x;
	const float band = strandBand_ * s;
	const float radius = strandRadius_ * s;
	std::uniform_int_distribution<size_t> pickJoint(0, jointPositions_.size() - 1);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (int n = 0; n < count; ++n) {
		// 骨（関節と親を結ぶ線）の上で、ほどける面の高さの近くの点を探す。見つからなければこの 1 本は出さない
		constexpr int kTries = 12;
		for (int t = 0; t < kTries; ++t) {
			const size_t i = pickJoint(random_);
			const int32_t parent = jointParents_[i];
			const Vector3& a = jointPositions_[i];
			const Vector3& b = parent >= 0 ? jointPositions_[parent] : a;
			const float u = unit(random_);
			Vector3 p = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u };
			if (p.y < planeHeight - band || p.y > planeHeight + band) continue;
			// 骨の線から水平に散らす（体の表面のあたりから出す）
			const float angle = unit(random_) * 6.2831853f;
			const float r = radius * std::sqrt(unit(random_));
			p.x += std::cos(angle) * r;
			p.z += std::sin(angle) * r;
			strands_->Emit(p, s);
			break;
		}
	}
}

void PlayerDeathPart::EmitFeatherBits(float deltaTime) {
	if (wingBurnTime_ < 0.0f) return;
	wingBurnTime_ += deltaTime;
	if (wingBurnTime_ >= wingBurnSeconds_) {
		wingBurnTime_ = -1.0f;
		return;
	}
	if (!host_) return;
	host_->CollectWingBurnPoints(wingPoints_);
	if (wingPoints_.empty()) return;
	featherCarry_ += featherBitRate_ * deltaTime;
	const int count = static_cast<int>(featherCarry_);
	featherCarry_ -= static_cast<float>(count);
	std::uniform_int_distribution<size_t> pick(0, wingPoints_.size() - 1);
	EffectManager* em = EffectManager::GetInstance();
	for (int n = 0; n < count; ++n) em->Play(kFeatherEffect, wingPoints_[pick(random_)]);
}

void PlayerDeathPart::Update(float deltaTime) {
	if (!IsActive()) return;
	stateTime_ += deltaTime;
	elapsed_ += deltaTime;
	EmitFeatherBits(deltaTime);

	// 糸と粒は自機について動く（レールでは自機ごとカメラと一緒に進むので、ワールドに置くと置いていかれる）
	EffectManager* em = EffectManager::GetInstance();
	if (player_) {
		strands_->SetAnchor(player_->GetTranslate());
		if (ascendHandle_ != 0) em->SetPosition(ascendHandle_, player_->GetTranslate());
		if (lightHandle_ != 0) em->SetPosition(lightHandle_, player_->GetTranslate());
	}
	strands_->Update(deltaTime);

	switch (state_) {
	case State::Prelude:
		// G3：ボス戦の膝つき・スラスターの咳き込みはここで進める
		if (stateTime_ >= PreludeSeconds()) Enter(State::Glow);
		break;
	case State::Glow:
		if (stateTime_ >= glowSeconds_) Enter(State::Unravel);
		break;
	case State::Unravel: {
		float minY = 0.0f;
		float maxY = 0.0f;
		if (!RefreshBody(minY, maxY)) {
			Enter(State::Linger);
			break;
		}
		const float s = player_->GetScale().x;
		// 面は足元の少し下（何も消えていない）から、頭の上へ縁とゆらぎが抜け切るまで上げる
		const float from = minY - (noiseAmplitude_ + 0.01f) * s;
		const float to = maxY + (edgeWidth_ + noiseAmplitude_) * s;
		const float u = unravelSeconds_ > 1e-3f ? stateTime_ / unravelSeconds_ : 1.0f;
		planeHeight_ = from + (to - from) * SmoothStep01(u);
		ApplyDissolve(planeHeight_);
		EmitStrands(planeHeight_, deltaTime);
		if (u >= 1.0f) Enter(State::Linger);
		break;
	}
	case State::Linger:
		if (stateTime_ >= lingerSeconds_) Enter(State::Finished);
		break;
	default:
		break;
	}
}

void PlayerDeathPart::Draw(Camera* camera) {
	if (!IsActive() && !strands_->HasAlive()) return;
	strands_->Draw(camera, etherColor_);
}

bool PlayerDeathPart::HasBloom() const {
	return (dissolving_ && player_ && player_->GetVisible()) || strands_->IsBuilt();
}

void PlayerDeathPart::DrawBloom(DirectXCore* dxCore) {
	if (dissolving_ && player_ && player_->GetVisible()) player_->DrawBloomPass(dxCore, bloomScale_);
	strands_->DrawBloom();
}

void PlayerDeathPart::ApplyCamera(Camera& camera) const {
	if (!IsActive() || !player_) return;
	const float w = cameraBlendSeconds_ > 1e-3f ? SmoothStep01(elapsed_ / cameraBlendSeconds_) : 1.0f;
	if (w <= 0.0f) return;
	const float s = player_->GetScale().x;
	const Vector3& p = player_->GetTranslate();

	// 見上げる：ほどけ始め（Prelude＋Glow の後）から注視点を胸から頭の上へ上げる
	const float lookStart = PreludeSeconds() + glowSeconds_;
	const float up = cameraLookUpSeconds_ > 1e-3f ? SmoothStep01((elapsed_ - lookStart) / cameraLookUpSeconds_) : 1.0f;
	const float lookHeight = (cameraLookFrom_ + (cameraLookTo_ - cameraLookFrom_) * up) * s;
	const Vector3 pivot = { p.x, p.y + cameraLookFrom_ * s, p.z };
	const Vector3 target = { p.x, p.y + lookHeight, p.z };

	// 寄る：胸までの距離を縮める
	const Vector3 eye = camera.GetTranslate();
	const float keep = 1.0f - cameraApproach_ * w;
	const Vector3 newEye = { pivot.x + (eye.x - pivot.x) * keep, pivot.y + (eye.y - pivot.y) * keep,
		pivot.z + (eye.z - pivot.z) * keep };

	const Vector3 look = { target.x - newEye.x, target.y - newEye.y, target.z - newEye.z };
	const float lookYaw = std::atan2(look.x, look.z);
	const float lookPitch = -std::atan2(look.y, std::sqrt(look.x * look.x + look.z * look.z));
	const Vector3 rot = camera.GetRotate();
	camera.SetTranslate(newEye);
	camera.SetRotate({
		rot.x + WrapAngle(lookPitch - rot.x) * w,
		rot.y + WrapAngle(lookYaw - rot.y) * w,
		rot.z * (1.0f - w),
	});
}

Vector3 PlayerDeathPart::GetBodyGlow() const {
	float k = 0.0f;
	switch (state_) {
	case State::Glow:
		k = glowSeconds_ > 1e-3f ? SmoothStep01(stateTime_ / glowSeconds_) : 1.0f;
		break;
	case State::Unravel:
		k = 1.0f;
		break;
	default:
		break;
	}
	k *= glowIntensity_;
	return { etherColor_.x * k, etherColor_.y * k, etherColor_.z * k };
}

uint64_t PlayerDeathPart::PlayEther(const char* effectName, const Vector3& position) const {
	EffectManager* em = EffectManager::GetInstance();
	const EffectDef* source = em->FindDef(effectName);
	if (!source) return kInvalidEffectHandle;
	EffectDef def = *source;
	for (EffectParticleComponent& p : def.particles) {
		if (!IsEther(p.displayName)) continue;
		Tint(p.startColor, etherColor_);
		Tint(p.endColor, etherColor_);
		for (EffectColorKey& key : p.colorKeys) Tint(key.color, etherColor_);
	}
	for (EffectLightComponent& l : def.lights) {
		if (IsEther(l.displayName)) Tint(l.color, etherColor_);
	}
	return em->PlayWithDef(def, position);
}

void PlayerDeathPart::StopEffects() {
	EffectManager* em = EffectManager::GetInstance();
	if (ascendHandle_ != kInvalidEffectHandle) em->Stop(ascendHandle_);
	if (lightHandle_ != kInvalidEffectHandle) em->Stop(lightHandle_);
	ascendHandle_ = kInvalidEffectHandle;
	lightHandle_ = kInvalidEffectHandle;
}

void PlayerDeathPart::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	ReadVector3(node["etherColor"], etherColor_);
	read("preludeRailSeconds", preludeRailSeconds_);
	read("preludeBossSeconds", preludeBossSeconds_);
	read("glowSeconds", glowSeconds_);
	read("unravelSeconds", unravelSeconds_);
	read("lingerSeconds", lingerSeconds_);
	read("glowIntensity", glowIntensity_);
	read("bottomMargin", bottomMargin_);
	read("topMargin", topMargin_);
	read("edgeWidth", edgeWidth_);
	read("edgeIntensity", edgeIntensity_);
	read("noiseScale", noiseScale_);
	read("noiseAmplitude", noiseAmplitude_);
	read("bloomScale", bloomScale_);
	read("strandRate", strandRate_);
	read("strandBand", strandBand_);
	read("strandRadius", strandRadius_);
	read("wingBurnSeconds", wingBurnSeconds_);
	read("featherBitRate", featherBitRate_);
	read("cameraBlendSeconds", cameraBlendSeconds_);
	read("cameraApproach", cameraApproach_);
	read("cameraLookFrom", cameraLookFrom_);
	read("cameraLookTo", cameraLookTo_);
	read("cameraLookUpSeconds", cameraLookUpSeconds_);
	strands_->LoadTuning(node["strands"]);
}

void PlayerDeathPart::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["etherColor"] = MakeVector3(etherColor_);
	write("preludeRailSeconds", preludeRailSeconds_);
	write("preludeBossSeconds", preludeBossSeconds_);
	write("glowSeconds", glowSeconds_);
	write("unravelSeconds", unravelSeconds_);
	write("lingerSeconds", lingerSeconds_);
	write("glowIntensity", glowIntensity_);
	write("bottomMargin", bottomMargin_);
	write("topMargin", topMargin_);
	write("edgeWidth", edgeWidth_);
	write("edgeIntensity", edgeIntensity_);
	write("noiseScale", noiseScale_);
	write("noiseAmplitude", noiseAmplitude_);
	write("bloomScale", bloomScale_);
	write("strandRate", strandRate_);
	write("strandBand", strandBand_);
	write("strandRadius", strandRadius_);
	write("wingBurnSeconds", wingBurnSeconds_);
	write("featherBitRate", featherBitRate_);
	write("cameraBlendSeconds", cameraBlendSeconds_);
	write("cameraApproach", cameraApproach_);
	write("cameraLookFrom", cameraLookFrom_);
	write("cameraLookTo", cameraLookTo_);
	write("cameraLookUpSeconds", cameraLookUpSeconds_);
	strands_->SaveTuning(node["strands"]);
}

void PlayerDeathPart::OnImGui() {
#ifdef _DEBUG
	static const char* kStateNames[] = { "Idle", "Prelude", "Glow", "Unravel", "Linger", "Finished" };
	ImGui::Text("state %s  %.2fs  plane y %.2f", kStateNames[static_cast<int>(state_)], stateTime_, planeHeight_);
	ImGui::ColorEdit3("Ether Color (仮)", &etherColor_.x, ImGuiColorEditFlags_Float);
	ImGui::TextDisabled("Timeline");
	ImGui::DragFloat("Prelude Rail (s)", &preludeRailSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Prelude Boss (s)", &preludeBossSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Glow (s)", &glowSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Unravel (s)", &unravelSeconds_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Linger (s)", &lingerSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::TextDisabled("Body");
	ImGui::DragFloat("Glow Intensity", &glowIntensity_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Bottom Margin (m)", &bottomMargin_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Top Margin (m)", &topMargin_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Edge Width (m)", &edgeWidth_, 0.005f, 0.001f, 1.0f);
	ImGui::DragFloat("Edge Intensity", &edgeIntensity_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Noise Scale (1/m)", &noiseScale_, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Noise Amplitude (m)", &noiseAmplitude_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.05f, 0.0f, 10.0f);
	ImGui::TextDisabled("Strand emission");
	ImGui::DragFloat("Strand Rate (/s)", &strandRate_, 1.0f, 0.0f, 2000.0f);
	ImGui::DragFloat("Strand Band (m)", &strandBand_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Strand Radius (m)", &strandRadius_, 0.005f, 0.0f, 1.0f);
	ImGui::TextDisabled("Wing burn (Rail, G2)");
	ImGui::DragFloat("Wing Burn (s)", &wingBurnSeconds_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Feather Bits (/s)", &featherBitRate_, 0.5f, 0.0f, 200.0f);
	ImGui::TextDisabled("Camera (G2)");
	ImGui::DragFloat("Camera Blend (s)", &cameraBlendSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::SliderFloat("Camera Approach", &cameraApproach_, 0.0f, 0.9f);
	ImGui::DragFloat("Look From (m)", &cameraLookFrom_, 0.01f, -5.0f, 20.0f);
	ImGui::DragFloat("Look To (m)", &cameraLookTo_, 0.01f, -5.0f, 20.0f);
	ImGui::DragFloat("Look Up (s)", &cameraLookUpSeconds_, 0.01f, 0.0f, 10.0f);
	if (ImGui::TreeNode("Strands")) {
		strands_->OnImGui();
		ImGui::TreePop();
	}
#endif // _DEBUG
}
