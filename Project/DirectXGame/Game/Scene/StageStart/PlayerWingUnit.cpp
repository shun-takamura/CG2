#include "PlayerWingUnit.h"

#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "ModelManager.h"
#include "AssetLocator.h"
#include "Material.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// rig_player_mech.py --export-wings で書き出して cook（共有ノート player_model_export.md）
	constexpr const char* kWingDir = "Resources/Models/Player/PlayerWings";
	constexpr const char* kWingFile = "PlayerWings.mesh";
	constexpr const char* kDeployClip = "Resources/Models/Player/PlayerWings/PlayerWings_Deploy.anim";
	constexpr const char* kStowClip = "Resources/Models/Player/PlayerWings/PlayerWings_Stow.anim";
	constexpr const char* kSocketJoint = "mixamorig:Spine2";
	// 羽根は体と同じ原点・大きさで作ってある。GetJointSocketMatrix は大きさを除くので、体の拡大率 s を入れて
	// Spine2 の基本の姿勢（位置 (0, 1.2145, 0.0272)・X まわり 12.9°）を打ち消す（player_model_export.md）
	constexpr float kSocketRotateX = 0.22521f;
	constexpr float kSocketOffsetY = -1.17778f;
	constexpr float kSocketOffsetZ = -0.29778f;

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
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

	const char* StateName(PlayerWingUnit::State state) {
		switch (state) {
		case PlayerWingUnit::State::Hidden:    return "Hidden";
		case PlayerWingUnit::State::Deploying: return "Deploying";
		case PlayerWingUnit::State::Deployed:  return "Deployed";
		case PlayerWingUnit::State::Stowing:   return "Stowing";
		}
		return "";
	}
}

PlayerWingUnit::PlayerWingUnit() = default;
PlayerWingUnit::~PlayerWingUnit() = default;

void PlayerWingUnit::Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
	DirectXCore* dxCore, SRVManager* srvManager) {
	if (!AssetLocator::GetInstance()->Exists(std::string(kWingDir) + "/" + kWingFile)) return;
	model_ = std::make_unique<AnimatedModelInstance>();
	model_->Initialize(ModelManager::GetInstance()->GetModelCore(), kWingDir, kWingFile);
	wings_ = std::make_unique<AnimatedObject3DInstance>();
	wings_->Initialize(object3DManager, skinningComputeManager, dxCore, srvManager, model_.get(), "PlayerWings");
	socket_.jointName = kSocketJoint;
	Hide();
}

void PlayerWingUnit::Deploy(float delay) {
	if (!wings_) return;
	state_ = State::Deploying;
	stateTime_ = 0.0f;
	delay_ = (std::max)(delay, 0.0f);
	// たたんだ形（Deploy の最初のコマ）で待つ。待っている間は半径 0 なので見えない
	wings_->PlayAnimation(kDeployClip, 0.0f);
	wings_->SetLoop(false);
	if (delay_ > 0.0f) wings_->Pause();
}

void PlayerWingUnit::Stow() {
	if (!wings_ || state_ == State::Hidden || state_ == State::Stowing) return;
	state_ = State::Stowing;
	stateTime_ = 0.0f;
	wings_->PlayAnimation(kStowClip, 0.0f);
	wings_->SetLoop(false);
}

void PlayerWingUnit::Hide() {
	state_ = State::Hidden;
	stateTime_ = 0.0f;
	delay_ = 0.0f;
	dissolving_ = false;
	if (!wings_) return;
	wings_->ClearDissolve();
	wings_->SetVisible(false);
}

void PlayerWingUnit::ShowDeployed() {
	if (!wings_) return;
	// 開いた形＝基本の姿勢＝Deploy の最後のコマ
	wings_->PlayAnimation(kDeployClip, 0.0f);
	wings_->SetLoop(false);
	wings_->SetAnimationTime(1.0e3f);
	state_ = State::Deployed;
	stateTime_ = 0.0f;
	delay_ = 0.0f;
}

bool PlayerWingUnit::IsDrawn() const {
	return wings_ && state_ != State::Hidden && body_ && body_->GetVisible();
}

void PlayerWingUnit::ApplyDissolve(float radiusRatio) {
	if (radiusRatio >= 1.0f) {
		wings_->ClearDissolve();
		dissolving_ = false;
		return;
	}
	const float s = body_ ? body_->GetScale().x : 1.0f;
	const Matrix4x4 root = body_ ? body_->GetJointWorldMatrix(kSocketJoint) : Matrix4x4{};
	DissolveSphere d{};
	d.mode = kDissolveSphereGrow;
	d.center = { root.m[3][0] + rootOffset_.x * s, root.m[3][1] + rootOffset_.y * s, root.m[3][2] + rootOffset_.z * s };
	// 1 で羽根の全部と縁が半径の内側に入る
	d.radius = (radiusMax_ + edgeWidth_) * s * (std::max)(radiusRatio, 0.0f);
	d.edgeWidth = edgeWidth_ * s;
	d.edgeColor = edgeColor_;
	d.noiseScale = s > 1e-4f ? noiseScale_ / s : noiseScale_;
	d.noiseAmplitude = noiseAmplitude_ * s;
	wings_->SetDissolveSphere(d);
	dissolving_ = true;
}

void PlayerWingUnit::Update(AnimatedObject3DInstance* body, float deltaTime) {
	body_ = body;
	if (!wings_) return;
	if (!body || state_ == State::Hidden) {
		wings_->SetVisible(false);
		return;
	}

	float radiusRatio = 1.0f;
	switch (state_) {
	case State::Deploying:
		if (delay_ > 0.0f) {
			delay_ -= deltaTime;
			if (delay_ > 0.0f) {
				radiusRatio = 0.0f;
				break;
			}
			wings_->Play();
		}
		stateTime_ += deltaTime;
		radiusRatio = dissolveInSeconds_ > 1e-3f ? SmoothStep01(stateTime_ / dissolveInSeconds_) : 1.0f;
		if (radiusRatio >= 1.0f && !wings_->IsPlaying()) state_ = State::Deployed;
		break;
	case State::Stowing:
		stateTime_ += deltaTime;
		radiusRatio = dissolveOutSeconds_ > 1e-3f ? 1.0f - SmoothStep01(stateTime_ / dissolveOutSeconds_) : 0.0f;
		if (radiusRatio <= 0.0f) {
			Hide();
			return;
		}
		break;
	default:
		break;
	}

	// 体の拡大率で Spine2 の基本の姿勢を打ち消すずらしを作る（体の大きさが変わってもよいよう毎フレーム）
	const float s = body->GetScale().x;
	socket_.target = body;
	socket_.offset.scale = { s, s, s };
	socket_.offset.rotate = { kSocketRotateX, 0.0f, 0.0f };
	socket_.offset.translate = { 0.0f, kSocketOffsetY * s, kSocketOffsetZ * s };
	wings_->SetWorldMatrixOverride(socket_.World());
	wings_->SetVisible(body->GetVisible());
	wings_->SetEnvironmentCoefficient(environmentCoefficient_);
	if (overrideMaterial_) wings_->SetMetallicRoughness(metallic_, roughness_);
	// 体の点滅（被弾・回避無敵の半透明と発光）を羽根にも
	{
		const Vector4 c = body->GetMaterialColor();
		wings_->SetMaterialColor({ c.x * albedoGain_, c.y * albedoGain_, c.z * albedoGain_, c.w });
	}
	wings_->SetEmissive({ baseGlow_ + blinkGlow_.x, baseGlow_ + blinkGlow_.y, baseGlow_ + blinkGlow_.z });
	ApplyDissolve(radiusRatio);
	wings_->Update(deltaTime);
}

void PlayerWingUnit::Draw(DirectXCore* dxCore) {
	if (!IsDrawn()) return;
	wings_->Draw(dxCore);
}

bool PlayerWingUnit::HasBloom() const {
	return IsDrawn() && dissolving_;
}

void PlayerWingUnit::DrawBloom(DirectXCore* dxCore) {
	if (!HasBloom()) return;
	wings_->DrawBloomPass(dxCore, bloomScale_);
}

void PlayerWingUnit::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("dissolveInSeconds", dissolveInSeconds_);
	read("dissolveOutSeconds", dissolveOutSeconds_);
	read("radiusMax", radiusMax_);
	ReadVector3(node["rootOffset"], rootOffset_);
	read("edgeWidth", edgeWidth_);
	ReadVector3(node["edgeColor"], edgeColor_);
	read("noiseScale", noiseScale_);
	read("noiseAmplitude", noiseAmplitude_);
	read("bloomScale", bloomScale_);
	read("environmentCoefficient", environmentCoefficient_);
	overrideMaterial_ = node["overrideMaterial"].AsBool(overrideMaterial_);
	read("metallic", metallic_);
	read("roughness", roughness_);
	read("baseGlow", baseGlow_);
	read("albedoGain", albedoGain_);
}

void PlayerWingUnit::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("dissolveInSeconds", dissolveInSeconds_);
	write("dissolveOutSeconds", dissolveOutSeconds_);
	write("radiusMax", radiusMax_);
	node["rootOffset"] = MakeVector3(rootOffset_);
	write("edgeWidth", edgeWidth_);
	node["edgeColor"] = MakeVector3(edgeColor_);
	write("noiseScale", noiseScale_);
	write("noiseAmplitude", noiseAmplitude_);
	write("bloomScale", bloomScale_);
	write("environmentCoefficient", environmentCoefficient_);
	node["overrideMaterial"] = JsonValue(overrideMaterial_);
	write("metallic", metallic_);
	write("roughness", roughness_);
	write("baseGlow", baseGlow_);
	write("albedoGain", albedoGain_);
}

void PlayerWingUnit::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("wings: %s (%.2f s)  model %s", StateName(state_), stateTime_, wings_ ? "loaded" : "(none)");
	if (ImGui::Button("Deploy")) Deploy(0.0f);
	ImGui::SameLine();
	if (ImGui::Button("Stow")) Stow();
	ImGui::SameLine();
	if (ImGui::Button("Show Deployed")) ShowDeployed();
	ImGui::SameLine();
	if (ImGui::Button("Hide")) Hide();
	ImGui::DragFloat("Dissolve In (s)", &dissolveInSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Dissolve Out (s)", &dissolveOutSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Radius Max (m, scale 1)", &radiusMax_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat3("Root Offset (m, scale 1)", &rootOffset_.x, 0.01f);
	ImGui::DragFloat("Edge Width (m, scale 1)", &edgeWidth_, 0.002f, 0.0f, 1.0f);
	ImGui::ColorEdit3("Edge Color", &edgeColor_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	ImGui::DragFloat("Noise Scale", &noiseScale_, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("Noise Amplitude (m, scale 1)", &noiseAmplitude_, 0.002f, 0.0f, 1.0f);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.01f, 0.0f, 10.0f);
	ImGui::SliderFloat("Environment (IBL) Coefficient", &environmentCoefficient_, 0.0f, 2.0f);
	ImGui::Checkbox("Override Metallic / Roughness", &overrideMaterial_);
	ImGui::SliderFloat("Metallic", &metallic_, 0.0f, 1.0f);
	ImGui::SliderFloat("Roughness", &roughness_, 0.0f, 1.0f);
	ImGui::SliderFloat("Albedo Gain", &albedoGain_, 0.5f, 6.0f);
	ImGui::SliderFloat("Base Glow (fill)", &baseGlow_, 0.0f, 0.5f);
#endif
}
