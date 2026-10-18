#include "StageStartPart.h"

#include "StageStart/CatapultFacility.h"
#include "Cockpit/CockpitModel.h"
#include "Cockpit/CockpitLighting.h"
#include "Camera.h"
#include "ModelManager.h"
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "Object3DManager.h"
#include "TextureManager.h"
#include "AssetLocator.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// HUB から IBL が来なかったときの縦穴の IBL（HubTemple と同じ）
	constexpr const char* kFallbackIblPath = "Resources/Cubemaps/hub_temple_ibl.dds";
	// 自機の新モデル（rig_player_mech.py --body-clips で書き出して cook。共有ノート player_model_export.md）
	constexpr const char* kMechDir = "Resources/Models/Player/PlayerModel";
	constexpr const char* kMechFile = "PlayerModel.mesh";
	// 機体確認の間は直立（Stand）→ 発進でエンジンを吹かすと前傾へ（Ignite、1 回）→ 前傾の保持（Catapult）。
	// 1 回きりのクリップの最後のコマは次のクリップの最初のコマと同じ姿勢なので、フェードなしでつなぐ
	constexpr const char* kMechStandClip = "Resources/Models/Player/PlayerModel/PlayerModel_Stand.anim";
	constexpr const char* kMechIgniteClip = "Resources/Models/Player/PlayerModel/PlayerModel_Ignite.anim";
	constexpr const char* kMechCatapultClip = "Resources/Models/Player/PlayerModel/PlayerModel_Catapult.anim";

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

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	const char* StateName(StageStartPart::State state) {
		switch (state) {
		case StageStartPart::State::Descent:    return "Descent";
		case StageStartPart::State::Inspection: return "Inspection";
		case StageStartPart::State::Launch:     return "Launch";
		case StageStartPart::State::WhiteOut:   return "WhiteOut";
		case StageStartPart::State::CloudTop:   return "CloudTop";
		case StageStartPart::State::Finished:   return "Finished";
		}
		return "";
	}
}

// 調整値は Initialize より前に読まれる（StagePlayScene::LoadTuningFromJson）ので、部品の器はここで作る
StageStartPart::StageStartPart()
	: facility_(std::make_unique<CatapultFacility>()),
	  cockpit_(std::make_unique<CockpitModel>()),
	  cockpitLighting_(std::make_unique<CockpitLighting>()) {
}
StageStartPart::~StageStartPart() = default;

void StageStartPart::Initialize(IStageStartHost* host, Camera* camera, Object3DManager* object3DManager, DirectXCore* dxCore,
	SkinningComputeManager* skinningComputeManager, SRVManager* srvManager) {
	host_ = host;
	camera_ = camera;
	object3DManager_ = object3DManager;
	geometry_.Load();

	facility_->Initialize(object3DManager, dxCore, geometry_);
	facility_->SetOrigin(origin_);
	cockpit_->Initialize(object3DManager, dxCore);
	cockpit_->SetHatchOpenRatio(0.0f);   // 閉じ切り（外は全天周のモニターに映っている扱い）
	// 三人称の機体。クリップが複数あるので既定のアニメは付かない → 直立（Stand）を差す
	if (AssetLocator::GetInstance()->Exists(std::string(kMechDir) + "/" + kMechFile)) {
		mechModel_ = std::make_unique<AnimatedModelInstance>();
		mechModel_->Initialize(ModelManager::GetInstance()->GetModelCore(), kMechDir, kMechFile);
		mech_ = std::make_unique<AnimatedObject3DInstance>();
		mech_->Initialize(object3DManager, skinningComputeManager, dxCore, srvManager, mechModel_.get(), "StageStartMech");
		PlayMechPose(MechPose::Stand);
		mech_->SetVisible(false);
	}
	// カタパルトの IBL（gen_catapult.py --ibl）。配布物では pack の中にしか無いので AssetLocator で確かめる
	if (!catapultIblPath_.empty() && !AssetLocator::GetInstance()->Exists(catapultIblPath_)) catapultIblPath_.clear();
	// 降下の途中（天井を抜けたとき）で読むとカクつくので、ここで読んでおく
	if (!catapultIblPath_.empty()) TextureManager::GetInstance()->LoadTexture(catapultIblPath_);
	active_ = false;
	state_ = State::Finished;
}

void StageStartPart::Finalize() {
	ReleaseLights();
	active_ = false;
}

void StageStartPart::AcquireLights() {
	if (lightsHeld_) return;
	facility_->AcquireLights();
	cockpitLighting_->Initialize();
	lightsHeld_ = true;
}

void StageStartPart::ReleaseLights() {
	if (!lightsHeld_) return;
	// 共有のマテリアル（コクピットの発光・六角形、液晶の UV）も元に戻す
	cockpitLighting_->Finalize(cockpit_.get());
	facility_->Finalize();
	lightsHeld_ = false;
}

StageStartHandoff StageStartPart::MakeDefaultHandoff() const {
	// HUB の降下が一定の速さになった後と同じ見え方（灯り・六角形はコクピットの起動の後、外の光は縦穴の中）
	StageStartHandoff h;
	h.shaftPhase = 0.0f;
	h.descentSpeed = defaultDescentSpeed_;
	h.cockpitLightLevel = 1.0f;
	h.revealAmount = 1.0f;
	h.sunScale = 0.2f;
	h.eyeOffset = CockpitModel::GetSeatedEye();
	h.eyeYaw = 0.0f;
	h.eyePitch = 0.0f;
	h.fovY = DegToRad(60.0f);
	h.sunDirection = { -0.191f, -0.815f, -0.547f };
	h.sunColor = { 1.0f, 0.96f, 0.9f, 1.0f };
	h.sunIntensity = 0.2f;
	h.iblPath = kFallbackIblPath;
	return h;
}

void StageStartPart::BeginDescent(const StageStartHandoff& handoff) {
	handoff_ = handoff;
	if (handoff_.iblPath.empty()) handoff_.iblPath = kFallbackIblPath;
	active_ = true;
	AcquireLights();
	// ステージのフォグ（StageEnvironment）は閉じた施設に合わない。終わったら RestoreStageEnvironment で戻る
	if (object3DManager_) object3DManager_->DisableFog();
	view_ = View::FirstPerson;
	lcdScroll_ = 0.0f;
	mechTravel_ = 0.0f;
	smoothedTargetValid_ = false;
	PlayMechPose(MechPose::Stand);
	arrived_ = false;
	currentIblPath_.clear();

	// 始めの高さ：カタパルトの縦穴の枠（下の端 y = base + H·k）でも、リフトの上面が枠の境目から shaftPhase 上にある所。
	// HUB と同じ位相なので、切れ目で壁の板と光の帯の見え方がずれない
	const float unitH = (std::max)(geometry_.shaftUnitHeight, 1e-3f);
	const float phase = std::clamp(handoff_.shaftPhase, 0.0f, unitH);
	startLiftY_ = geometry_.catapultShaftBaseY + unitH * static_cast<float>((std::max)(startUnitsAboveShaftBase_, 0)) + phase;

	// 一定の速さで降り、最後の brakeDistance で smoothstep の形に減速して床（y=0）で止まる
	cruiseSpeed_ = handoff_.descentSpeed > 0.1f ? handoff_.descentSpeed : defaultDescentSpeed_;
	brakeDistance_ = std::clamp(brakeDistanceTuning_, 0.0f, startLiftY_);
	cruiseSeconds_ = (startLiftY_ - brakeDistance_) / cruiseSpeed_;
	brakeSeconds_ = 2.0f * brakeDistance_ / cruiseSpeed_;   // 速さ v·(1 − smoothstep(u)) の積分 = v·T/2

	Enter(State::Descent);
	UpdateDescent(0.0f);
}

void StageStartPart::BeginFromCloudTop() {
	active_ = true;
	AcquireLights();
	view_ = View::ThirdPerson;
	Enter(State::CloudTop);
}

void StageStartPart::Enter(State state) {
	state_ = state;
	stateTime_ = 0.0f;
	// 発進（エンジンを吹かす）で前傾へ
	if (state == State::Launch) PlayMechPose(MechPose::Ignite);
}

void StageStartPart::PlayMechPose(MechPose pose) {
	mechPose_ = pose;
	if (!mech_) return;
	switch (pose) {
	case MechPose::Stand:    mech_->PlayAnimation(kMechStandClip, 0.0f);    mech_->SetLoop(true);  break;
	case MechPose::Ignite:   mech_->PlayAnimation(kMechIgniteClip, 0.0f);   mech_->SetLoop(false); break;
	case MechPose::Catapult: mech_->PlayAnimation(kMechCatapultClip, 0.0f); mech_->SetLoop(true);  break;
	}
}

void StageStartPart::UpdateMechPose(float deltaTime) {
	if (!mech_) return;
	mech_->Update(deltaTime);
	// Ignite は最後のコマで止まる（IsPlaying が false）→ 前傾の保持へ
	if (mechPose_ == MechPose::Ignite && !mech_->IsPlaying()) PlayMechPose(MechPose::Catapult);
}

void StageStartPart::Finish() {
	Enter(State::Finished);
	active_ = false;
	if (mech_) mech_->SetVisible(false);
	// 演出の灯りを返してから、ステージの環境へ戻す
	ReleaseLights();
	if (host_) host_->RestoreStageEnvironment();
	currentIblPath_.clear();
}

float StageStartPart::DescentDistanceAt(float time) const {
	if (time <= cruiseSeconds_) return cruiseSpeed_ * time;
	const float cruise = startLiftY_ - brakeDistance_;
	if (brakeSeconds_ <= 1e-4f) return startLiftY_;
	const float u = std::clamp((time - cruiseSeconds_) / brakeSeconds_, 0.0f, 1.0f);
	// ∫(1 − (3u² − 2u³)) = u − u³ + u⁴/2
	return cruise + cruiseSpeed_ * brakeSeconds_ * (u - u * u * u + 0.5f * u * u * u * u);
}

void StageStartPart::UpdateDescent(float deltaTime) {
	(void)deltaTime;
	const float liftY = (std::max)(startLiftY_ - DescentDistanceAt(stateTime_), 0.0f);
	facility_->SetLiftY(liftY);
	if (!arrived_ && stateTime_ >= cruiseSeconds_ + brakeSeconds_) {
		arrived_ = true;
		camera_->Shake(arriveShakeIntensity_, arriveShakeSeconds_);
	}
	if (arrived_ && stateTime_ >= cruiseSeconds_ + brakeSeconds_ + settleSeconds_) {
		Enter(State::Inspection);
	}
}

void StageStartPart::Update(InputActionMap* actions, float deltaTime, const Vector2& screenSize) {
	if (!active_) return;
	stateTime_ += deltaTime;

	switch (state_) {
	case State::Descent:
		UpdateDescent(deltaTime);
		break;
	case State::Inspection:
		// 機体確認の中身は F4。今は決定キーで先へ（仮）
		if (actions && actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) Enter(State::Launch);
		break;
	case State::Launch:     // F5
		Enter(State::WhiteOut);
		break;
	case State::WhiteOut:   // F5
		Enter(State::CloudTop);
		break;
	case State::CloudTop:   // F6
		Finish();
		return;
	case State::Finished:
		return;
	}

	// 一人称 ↔ 三人称。降りている間は一人称のまま（HUB からの続き）
	if (state_ != State::Descent && actions && actions->IsTriggered(static_cast<int>(Action::ViewToggle))) {
		view_ = (view_ == View::FirstPerson) ? View::ThirdPerson : View::FirstPerson;
		smoothedTargetValid_ = false;   // 切り替えは瞬時（遅れの続きから追わない）
	}

	lcdScroll_ += lcdScrollSpeed_ * deltaTime;
	ApplyCamera(deltaTime);
	ApplyLighting(screenSize);
	UpdateMechPose(deltaTime);
}

void StageStartPart::ApplyCamera(float deltaTime) {
	// liftOrigin＝リフトの上面の中心（カタパルトに固定）、mechOrigin＝機体の原点（発進で前へ走る）
	const float travel = MechTravel();
	const Vector3 liftOrigin = facility_->ToWorld({ 0.0f, facility_->GetLiftY(), 0.0f });
	const Vector3 mechOrigin = { liftOrigin.x, liftOrigin.y, liftOrigin.z + travel };
	facility_->SetClampTravel(travel);
	// 機体が射出口へ近づくほど射出口の光を弱める（ブルームのにじみに機体が埋もれないように）
	{
		const float toExit = geometry_.catapultTunnelEndZ - travel;
		const float nearness = exitDimDistance_ > 1e-3f ? 1.0f - toExit / exitDimDistance_ : (toExit <= 0.0f ? 1.0f : 0.0f);
		facility_->SetExitLightLevel(1.0f + (exitDimNear_ - 1.0f) * SmoothStep01(nearness));
	}
	camera_->SetNearClip(nearClip_);
	if (view_ == View::FirstPerson) {
		const Vector3& e = handoff_.eyeOffset;
		camera_->SetFovY(handoff_.fovY);
		camera_->SetTranslate({ mechOrigin.x + e.x, mechOrigin.y + e.y, mechOrigin.z + e.z });
		camera_->SetRotate({ handoff_.eyePitch, handoff_.eyeYaw, 0.0f });
	} else {
		const Vector3 eye = { liftOrigin.x + thirdPersonEye_.x, liftOrigin.y + thirdPersonEye_.y, liftOrigin.z + thirdPersonEye_.z };
		const Vector3 offset = ThirdPersonTargetAt(travel);
		const Vector3 target = { mechOrigin.x + offset.x, mechOrigin.y + offset.y, mechOrigin.z + offset.z };
		if (!smoothedTargetValid_ || thirdPersonTargetLag_ <= 1e-4f) {
			smoothedTarget_ = target;
			smoothedTargetValid_ = true;
		} else {
			const float k = 1.0f - std::exp(-deltaTime / thirdPersonTargetLag_);
			smoothedTarget_ = { smoothedTarget_.x + (target.x - smoothedTarget_.x) * k,
				smoothedTarget_.y + (target.y - smoothedTarget_.y) * k,
				smoothedTarget_.z + (target.z - smoothedTarget_.z) * k };
		}
		const Vector3 d = { smoothedTarget_.x - eye.x, smoothedTarget_.y - eye.y, smoothedTarget_.z - eye.z };
		// yaw 0 が +Z、pitch は正が下向き（HubScene のカメラと同じ）
		const float yaw = std::atan2(d.x, d.z);
		const float pitch = -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
		camera_->SetFovY(DegToRad(thirdPersonFovDegrees_));
		camera_->SetTranslate(eye);
		camera_->SetRotate({ pitch, yaw, 0.0f });
	}
	cockpit_->SetPosition(mechOrigin);
	cockpit_->Update();
	// 機体の原点＝リフトの上面の中心＋前へ走った分（HubMech と同じ置き方）。正面は +Z
	if (mech_) {
		mech_->SetTranslate(mechOrigin);
		mech_->SetRotate({ 0.0f, 0.0f, 0.0f });
		mech_->SetScale({ mechScale_, mechScale_, mechScale_ });
		mech_->SetVisible(view_ == View::ThirdPerson);
	}
	if (host_) host_->SetPlayerHidden(true);
}

Vector3 StageStartPart::ThirdPersonTargetAt(float travel) const {
	// 機体（原点＝前へ走った分）がカメラの z を通る所が真横のキー
	const float pass = (std::max)(thirdPersonEye_.z, 0.0f);
	if (travel <= pass) {
		const float u = pass > 1e-3f ? travel / pass : 1.0f;
		return LerpVector3(thirdPersonTarget_, thirdPersonPassTarget_, SmoothStep01(u));
	}
	const float u = thirdPersonFarDistance_ > 1e-3f ? (travel - pass) / thirdPersonFarDistance_ : 1.0f;
	return LerpVector3(thirdPersonPassTarget_, thirdPersonFarTarget_, SmoothStep01(u));
}

bool StageStartPart::DrawsCloudsThroughExit() const {
	return active_ && cloudsThroughExit_ && facility_->IsExitOpenToSky();
}

Vector3 StageStartPart::GetSkyViewOffset() const {
	const Vector3 exit = facility_->ToWorld({ 0.0f, 0.0f, geometry_.catapultTunnelEndZ });
	return { skyAnchor_.x + skyAnchorOffset_.x - exit.x, skyAnchor_.y + skyAnchorOffset_.y - exit.y,
		skyAnchor_.z + skyAnchorOffset_.z - exit.z };
}

void StageStartPart::ApplyLighting(const Vector2& screenSize) {
	const float liftY = facility_->GetLiftY();
	// 縦穴の付け根（カタパルトの天井）より下へ降りるほど外の光を弱める
	const float ceiling = (std::max)(geometry_.catapultCeilingY, 1e-3f);
	const float hall = std::clamp((ceiling - liftY) / ceiling, 0.0f, 1.0f);
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(handoff_.sunDirection);
		lm->SetDirectionalLightColor(handoff_.sunColor);
		lm->SetDirectionalLightIntensity(handoff_.sunIntensity * (1.0f + (hallSunScale_ - 1.0f) * hall));
	}
	// 目がカタパルトの天井より下へ出たら IBL をカタパルトのものへ（縦穴の中は HUB と同じ IBL で切れ目をそろえる）
	const float eyeY = liftY + handoff_.eyeOffset.y;
	const bool inHall = eyeY < geometry_.catapultCeilingY && !catapultIblPath_.empty();
	SetEnvironmentTexture(inHall ? catapultIblPath_ : handoff_.iblPath);

	facility_->Update(state_ == State::Descent && !arrived_ ? lampLevelInShaft_ : 1.0f, lcdScroll_);

	// コクピットの灯り（一人称のときだけ。三人称では内装を描かない）
	const Vector3 sign = { cockpitSignColor_.x, cockpitSignColor_.y, cockpitSignColor_.z };
	if (view_ == View::FirstPerson) {
		cockpitLighting_->Apply(*cockpit_, handoff_.cockpitLightLevel, handoff_.revealAmount, screenSize, sign);
	} else {
		cockpitLighting_->Apply(*cockpit_, 0.0f, 0.0f, screenSize, sign);
	}
}

void StageStartPart::SetEnvironmentTexture(const std::string& path) {
	if (path.empty() || path == currentIblPath_ || !object3DManager_) return;
	// SetEnvironmentTexture は読み込まないので先に読む（読み込み済みなら何もしない）
	TextureManager::GetInstance()->LoadTexture(path);
	object3DManager_->SetEnvironmentTexture(path);
	currentIblPath_ = path;
}

void StageStartPart::Draw(DirectXCore* dxCore) {
	if (!active_) return;
	facility_->Draw(dxCore);
	if (view_ == View::FirstPerson) {
		cockpit_->Draw(dxCore);
	} else if (mech_) {
		// スキニングの CS とルートシグネチャの切り替えは Draw の中でする。最後に描く
		mech_->Draw(dxCore);
	}
}

bool StageStartPart::HasBloom() const {
	return active_;
}

void StageStartPart::DrawBloom(DirectXCore* dxCore) {
	if (!active_) return;
	facility_->DrawBloom(dxCore, 1.0f);
	if (view_ == View::FirstPerson && cockpitLighting_->HasBloom()) {
		cockpit_->DrawBloom(dxCore, cockpitLighting_->GetBloomScale(), cockpitLighting_->IsRevealing());
	}
}

void StageStartPart::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	ReadVector3(node["origin"], origin_);
	startUnitsAboveShaftBase_ = static_cast<int>(node["startUnitsAboveShaftBase"].AsDouble(startUnitsAboveShaftBase_));
	read("brakeDistance", brakeDistanceTuning_);
	read("defaultDescentSpeed", defaultDescentSpeed_);
	read("arriveShakeIntensity", arriveShakeIntensity_);
	read("arriveShakeSeconds", arriveShakeSeconds_);
	read("settleSeconds", settleSeconds_);
	read("hallSunScale", hallSunScale_);
	read("lampLevelInShaft", lampLevelInShaft_);
	catapultIblPath_ = node["catapultIblPath"].AsString(catapultIblPath_);
	read("nearClip", nearClip_);
	Vector3 sign = { cockpitSignColor_.x, cockpitSignColor_.y, cockpitSignColor_.z };
	ReadVector3(node["cockpitSignColor"], sign);
	cockpitSignColor_ = { sign.x, sign.y, sign.z, 1.0f };
	read("lcdScrollSpeed", lcdScrollSpeed_);
	ReadVector3(node["thirdPersonEye"], thirdPersonEye_);
	ReadVector3(node["thirdPersonTarget"], thirdPersonTarget_);
	ReadVector3(node["thirdPersonPassTarget"], thirdPersonPassTarget_);
	ReadVector3(node["thirdPersonFarTarget"], thirdPersonFarTarget_);
	read("thirdPersonFarDistance", thirdPersonFarDistance_);
	read("thirdPersonTargetLag", thirdPersonTargetLag_);
	read("exitDimNear", exitDimNear_);
	read("exitDimDistance", exitDimDistance_);
	cloudsThroughExit_ = node["cloudsThroughExit"].AsBool(cloudsThroughExit_);
	ReadVector3(node["skyAnchorOffset"], skyAnchorOffset_);
	read("thirdPersonFovDegrees", thirdPersonFovDegrees_);
	read("mechScale", mechScale_);
	debugDescentWithoutHandoff_ = node["debugDescentWithoutHandoff"].AsBool(debugDescentWithoutHandoff_);
	facility_->LoadTuning(node["facility"]);
	facility_->SetOrigin(origin_);
}

void StageStartPart::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["origin"] = MakeVector3(origin_);
	node["startUnitsAboveShaftBase"] = JsonValue(static_cast<double>(startUnitsAboveShaftBase_));
	write("brakeDistance", brakeDistanceTuning_);
	write("defaultDescentSpeed", defaultDescentSpeed_);
	write("arriveShakeIntensity", arriveShakeIntensity_);
	write("arriveShakeSeconds", arriveShakeSeconds_);
	write("settleSeconds", settleSeconds_);
	write("hallSunScale", hallSunScale_);
	write("lampLevelInShaft", lampLevelInShaft_);
	node["catapultIblPath"] = JsonValue(catapultIblPath_);
	write("nearClip", nearClip_);
	node["cockpitSignColor"] = MakeVector3({ cockpitSignColor_.x, cockpitSignColor_.y, cockpitSignColor_.z });
	write("lcdScrollSpeed", lcdScrollSpeed_);
	node["thirdPersonEye"] = MakeVector3(thirdPersonEye_);
	node["thirdPersonTarget"] = MakeVector3(thirdPersonTarget_);
	node["thirdPersonPassTarget"] = MakeVector3(thirdPersonPassTarget_);
	node["thirdPersonFarTarget"] = MakeVector3(thirdPersonFarTarget_);
	write("thirdPersonFarDistance", thirdPersonFarDistance_);
	write("thirdPersonTargetLag", thirdPersonTargetLag_);
	write("exitDimNear", exitDimNear_);
	write("exitDimDistance", exitDimDistance_);
	node["cloudsThroughExit"] = JsonValue(cloudsThroughExit_);
	node["skyAnchorOffset"] = MakeVector3(skyAnchorOffset_);
	write("thirdPersonFovDegrees", thirdPersonFovDegrees_);
	write("mechScale", mechScale_);
	node["debugDescentWithoutHandoff"] = JsonValue(debugDescentWithoutHandoff_);
	facility_->SaveTuning(node["facility"]);
}

void StageStartPart::LoadCockpitTuning(const JsonValue& node) {
	cockpitLighting_->LoadTuning(node);
}

void StageStartPart::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("state = %s (%.2f s)  view = %s  active = %s", StateName(state_), stateTime_,
		view_ == View::FirstPerson ? "1st" : "3rd", active_ ? "yes" : "no");
	if (state_ == State::Descent) {
		ImGui::Text("descent: start %.2f m  cruise %.2f s  brake %.2f s", startLiftY_, cruiseSeconds_, brakeSeconds_);
	}
	ImGui::Text("handoff: phase %.2f m  speed %.2f m/s  ibl %s", handoff_.shaftPhase, handoff_.descentSpeed, handoff_.iblPath.c_str());
	// 降下から流すボタンは StagePlay 側（フェーズも一緒に切り替えるため）
	if (ImGui::Button("Next State") && active_) {
		switch (state_) {
		case State::Descent:    Enter(State::Inspection); facility_->SetLiftY(0.0f); arrived_ = true; break;
		case State::Inspection: Enter(State::Launch); break;
		default: break;
		}
	}
	ImGui::Checkbox("Descent Without Handoff (Debug start)", &debugDescentWithoutHandoff_);

	ImGui::SeparatorText("Descent");
	if (ImGui::DragFloat3("Origin", &origin_.x, 1.0f)) facility_->SetOrigin(origin_);
	ImGui::SliderInt("Start Units Above Shaft Base", &startUnitsAboveShaftBase_, 0, 6);
	ImGui::DragFloat("Brake Distance (m)", &brakeDistanceTuning_, 0.1f, 0.0f, 80.0f);
	ImGui::DragFloat("Default Speed (m/s)", &defaultDescentSpeed_, 0.05f, 0.1f, 50.0f);
	ImGui::DragFloat("Arrive Shake", &arriveShakeIntensity_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Arrive Shake (s)", &arriveShakeSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Settle (s)", &settleSeconds_, 0.01f, 0.0f, 5.0f);

	ImGui::SeparatorText("Light");
	ImGui::SliderFloat("Hall Sun Scale", &hallSunScale_, 0.0f, 1.0f);
	ImGui::SliderFloat("Lamp Level In Shaft", &lampLevelInShaft_, 0.0f, 1.0f);
	ImGui::Text("Catapult IBL: %s", catapultIblPath_.empty() ? "(none: shaft IBL)" : catapultIblPath_.c_str());
	ImGui::ColorEdit3("Cockpit Sign Color", &cockpitSignColor_.x);
	ImGui::DragFloat("LCD Scroll (m/s)", &lcdScrollSpeed_, 0.05f, -40.0f, 40.0f);

	ImGui::SeparatorText("View");
	ImGui::DragFloat("Near Clip", &nearClip_, 0.005f, 0.01f, 2.0f);
	ImGui::DragFloat3("3rd Eye (catapult)", &thirdPersonEye_.x, 0.05f);
	ImGui::DragFloat3("3rd Target Start (mech)", &thirdPersonTarget_.x, 0.05f);
	ImGui::DragFloat3("3rd Target Pass (mech)", &thirdPersonPassTarget_.x, 0.05f);
	ImGui::DragFloat3("3rd Target Far (mech)", &thirdPersonFarTarget_.x, 0.05f);
	ImGui::DragFloat("3rd Far Distance (m)", &thirdPersonFarDistance_, 0.5f, 0.0f, 200.0f);
	ImGui::TextDisabled("Pass = mech reaches eye z (travel %.1f m)", (std::max)(thirdPersonEye_.z, 0.0f));
	ImGui::DragFloat("3rd Target Lag (s)", &thirdPersonTargetLag_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Exit Dim Near", &exitDimNear_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Exit Dim Distance (m)", &exitDimDistance_, 0.5f, 0.0f, 160.0f);

	ImGui::SeparatorText("Sky Through Exit");
	ImGui::Checkbox("Clouds Through Exit", &cloudsThroughExit_);
	ImGui::Text("Sky Anchor (rail t=0): (%.1f, %.1f, %.1f)", skyAnchor_.x, skyAnchor_.y, skyAnchor_.z);
	ImGui::DragFloat3("Sky Anchor Offset", &skyAnchorOffset_.x, 1.0f);
	// 発進（F5）の前に、機体を前へずらして見送りの画角を確かめる（保存しない）
	ImGui::SliderFloat("Preview Travel (m)", &previewTravel_, 0.0f, 160.0f);
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset##travel")) previewTravel_ = 0.0f;
	// 発進（F5）の前に姿勢の移り変わりを確かめる
	ImGui::Text("Pose: %s", mechPose_ == MechPose::Stand ? "Stand" : mechPose_ == MechPose::Ignite ? "Ignite" : "Catapult");
	if (ImGui::Button("Pose: Stand")) PlayMechPose(MechPose::Stand);
	ImGui::SameLine();
	if (ImGui::Button("Pose: Ignite")) PlayMechPose(MechPose::Ignite);
	ImGui::DragFloat("3rd FovY (deg)", &thirdPersonFovDegrees_, 0.1f, 10.0f, 120.0f);
	ImGui::DragFloat("Mech Scale", &mechScale_, 0.01f, 0.01f, 100.0f);

	if (ImGui::CollapsingHeader("Facility")) facility_->OnImGui();
	if (ImGui::CollapsingHeader("Cockpit Lighting (Hub.json)")) cockpitLighting_->OnImGui();
#endif // _DEBUG
}
