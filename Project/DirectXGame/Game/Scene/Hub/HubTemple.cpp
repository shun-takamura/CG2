#include "HubTemple.h"

#include "Object3DInstance.h"
#include "Camera.h"
#include "ModelInstance.h"
#include "Material.h"
#include "TextureManager.h"
#include "AssetLocator.h"
#include "MathUtility.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "Water/WaterSurface.h"
#include "Water/WaterReflection.h"
#include "Json/JsonValue.h"
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// tools/BlenderPipeline/gen_hub_temple.py --export Assets/Models/HubTemple を cook したもの
	constexpr const char* kModelDir = "Resources/Models/HubTemple";
	constexpr const char* kFloorTexturePath = "Resources/Models/HubTemple/HubFloor_BaseColor.dds";
	// 床の模様は原点がテクスチャの角に来るように描いてある。この長さで 1 周（gen_hub_temple.py の FLOOR_TILE_M）
	constexpr float kFloorTileMeters = 44.0f;
	// 壁の内側の半径 22m を覆う正方形（角は壁の外に出るが見えない）
	constexpr float kFloorSize = 46.0f;
	// 磨いた石なので水の層はほぼ無い（屈折で床がずれない）
	constexpr float kFloorDepth = 0.002f;
	// 反射 RT の解像度。半分にして映り込みを少しぼかす（17 番 §2）
	constexpr float kReflectionScale = 0.5f;

	// 神殿の IBL（gen_hub_temple.py --ibl → convert_hdr_to_dds.py）。無ければタイトルの空の IBL
	constexpr const char* kIblPath = "Resources/Cubemaps/hub_temple_ibl.dds";
	constexpr const char* kFallbackIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";

	// タイトルの扉（5_TitleScene.md §10.5）。扉板の原点＝蝶番の軸（枠の奥の面）
	constexpr const char* kDoorModelDir = "Resources/Models/TitleDoor";
	const Vector3 kDoorHingeL = { 0.9f, 0.0f, -0.2f };
	const Vector3 kDoorHingeR = { -0.9f, 0.0f, -0.2f };

	// 時計の門（gen_hub_temple.py の GATE_CENTER）。面は XY、−Z 側（神殿の中）を向く
	const Vector3 kGateCenter = { 0.0f, 19.1f, 19.8f };

	struct GateMoverDef {
		const char* mesh;
		Vector3 offset;          // 門の中心から（z は奥行き。− が手前）
		float scale;
		float spinDegPerSec;
		float startDeg;
	};
	// gen_hub_temple.py の GATE_MOVERS と同じ
	constexpr GateMoverDef kGateMoverDefs[] = {
		{ "HubGate_MinuteHand.mesh", { 0.0f, 0.0f, -1.25f }, 1.0f, 6.0f, 40.0f },
		{ "HubGate_HourHand.mesh", { 0.0f, 0.0f, -1.15f }, 1.0f, 0.5f, 300.0f },
		{ "HubGate_SmallHand.mesh", { -3.4f, 4.3f, -1.12f }, 1.0f, 12.0f, 0.0f },
		{ "HubGate_SmallHand.mesh", { 3.4f, 4.3f, -1.12f }, 1.0f, -8.0f, 120.0f },
		{ "HubGate_SmallHand.mesh", { -5.0f, -1.7f, -1.12f }, 1.0f, 18.0f, 240.0f },
		{ "HubGear.mesh", { 0.0f, 0.0f, 0.45f }, 3.2f, 5.0f, 0.0f },
		{ "HubGear.mesh", { 3.15f, -2.0f, 0.25f }, 1.6f, -10.0f, 7.0f },
		{ "HubGear.mesh", { -2.75f, 1.85f, 0.25f }, 1.45f, -11.0f, 3.0f },
		{ "HubGear.mesh", { 1.9f, 3.05f, 0.1f }, 1.0f, -16.0f, 0.0f },
		{ "HubGear.mesh", { -2.9f, -2.5f, 0.1f }, 1.2f, -13.0f, 11.0f },
	};

	struct RingDef {
		const char* mesh;
		Vector3 center;
		float tiltY;
		float tiltZ;
		float spinDegPerSec;
	};
	// gen_hub_temple.py の RINGS と同じ（輪の傾きは足場・機体とぶつからないことをスクリプトで確かめてある）
	constexpr RingDef kRingDefs[] = {
		{ "HubRing_A.mesh", { 0.0f, 12.0f, 0.0f }, 35.0f, 90.0f, 4.0f },
		{ "HubRing_B.mesh", { 0.0f, 12.0f, 0.0f }, -35.0f, 120.0f, -3.0f },
		{ "HubRing_C.mesh", { 0.0f, 12.0f, 0.0f }, 50.0f, 90.0f, 2.0f },
		{ "HubRing_D.mesh", { 0.0f, 2.0f, 0.0f }, 0.0f, 90.0f, -1.5f },
	};
}

HubTemple::HubTemple() = default;
HubTemple::~HubTemple() = default;

void HubTemple::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager) {
	shell_ = std::make_unique<Object3DInstance>();
	shell_->Initialize(object3DManager, dxCore, kModelDir, "HubTemple_Shell.mesh", "HubTempleShell");
	gate_ = std::make_unique<Object3DInstance>();
	gate_->Initialize(object3DManager, dxCore, kModelDir, "HubGate.mesh", "HubGate");
	for (const GateMoverDef& def : kGateMoverDefs) {
		GateMover mover;
		mover.object = std::make_unique<Object3DInstance>();
		mover.object->Initialize(object3DManager, dxCore, kModelDir, def.mesh,
			"HubGateMover" + std::to_string(gateMovers_.size()));
		mover.offset = def.offset;
		mover.scale = def.scale;
		mover.spinDegPerSec = def.spinDegPerSec;
		mover.angle = def.startDeg;
		gateMovers_.push_back(std::move(mover));
	}
	for (size_t i = 0; i < rings_.size(); ++i) {
		Ring& ring = rings_[i];
		const RingDef& def = kRingDefs[i];
		ring.object = std::make_unique<Object3DInstance>();
		ring.object->Initialize(object3DManager, dxCore, kModelDir, def.mesh, "HubRing" + std::to_string(i));
		ring.center = def.center;
		ring.tiltY = def.tiltY;
		ring.tiltZ = def.tiltZ;
		ring.spinDegPerSec = def.spinDegPerSec;
		ring.angle = 0.0f;
	}

	doorFrame_ = std::make_unique<Object3DInstance>();
	doorFrame_->Initialize(object3DManager, dxCore, kDoorModelDir, "door_frame.mesh", "HubDoorFrame");
	doorLeafL_ = std::make_unique<Object3DInstance>();
	doorLeafL_->Initialize(object3DManager, dxCore, kDoorModelDir, "door_leaf_L.mesh", "HubDoorLeafL");
	doorLeafR_ = std::make_unique<Object3DInstance>();
	doorLeafR_->Initialize(object3DManager, dxCore, kDoorModelDir, "door_leaf_R.mesh", "HubDoorLeafR");
	ApplyDoor();

	// 配布の exe ではテクスチャは Assets.pack の中にあり、ファイルとしては存在しない。
	// std::filesystem::exists で確かめるとタイトルの空（青）の IBL に落ちて暗く青くなったので、AssetLocator で確かめる
	iblPath_ = AssetLocator::GetInstance()->Exists(kIblPath) ? kIblPath : kFallbackIblPath;
	TextureManager::GetInstance()->LoadTexture(iblPath_);

	reflection_ = std::make_unique<WaterReflection>();
	reflection_->Initialize(dxCore, srvManager, object3DManager, kReflectionScale);
	reflection_->SetWaterHeight(0.0f);
	// 神殿の中は全部映す（ドームの頂点まで約 45m、斜めでも 100m あれば足りる）
	reflection_->SetFarClip(100.0f);
	reflection_->AddTarget(shell_.get());
	reflection_->AddTarget(gate_.get());
	for (GateMover& mover : gateMovers_) reflection_->AddTarget(mover.object.get());
	for (Ring& ring : rings_) reflection_->AddTarget(ring.object.get());
	reflection_->AddTarget(doorFrame_.get());
	reflection_->AddTarget(doorLeafL_.get());
	reflection_->AddTarget(doorLeafR_.get());

	floor_ = std::make_unique<WaterSurface>();
	floor_->Initialize(dxCore, srvManager, object3DManager, kFloorTexturePath);
	floor_->SetWaterHeight(0.0f);
	floor_->SetSize(kFloorSize);
	floor_->SetCenter({ 0.0f, 0.0f, 0.0f });
	WaterSurface::Params& p = floor_->GetParams();
	p.depth = kFloorDepth;
	p.floorTiling = 1.0f / kFloorTileMeters;
	// 波は全部止める（同心円・ノイズ・シミュレーション）
	p.ringAmplitude = 0.0f;
	p.ambientRingScale = 0.0f;
	p.noiseAmplitude = 0.0f;
	p.simNormalScale = 0.0f;
	p.distortion = 0.0f;
	// 屈折で床の模様がずれないように（空気と同じ）
	p.ior = 1.0f;
	ApplyFloorParams();

	Update(0.0f);
}

void HubTemple::ApplyDoor() {
	// タイトルと同じ閉じた扉の表の面を神殿の中へ向ける（そのまま置くと飾りの無い裏の面が見えた）。
	// プレイヤーはタイトルからこの扉をくぐって入ってきた（時計の門の 6 時の通り道）
	Transform frameXf;
	frameXf.scale = { doorScale_, doorScale_, doorScale_ };
	frameXf.rotate = { 0.0f, kPi, 0.0f };
	frameXf.translate = doorPosition_;
	const Matrix4x4 frameMatrix = MakeAffineMatrix(frameXf);
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		part->SetScale(frameXf.scale);
		part->SetRotate(frameXf.rotate);
	}
	doorFrame_->SetTranslate(frameXf.translate);
	doorLeafL_->SetTranslate(TransformCoordinate(kDoorHingeL, frameMatrix));
	doorLeafR_->SetTranslate(TransformCoordinate(kDoorHingeR, frameMatrix));
	doorFrame_->Update();
	doorLeafL_->Update();
	doorLeafR_->Update();

	// 扉のモデルはファイル名でタイトルと共有していて、マテリアルにタイトルのディゾルブ（タイトルの水面の高さ基準で
	// 「ここより上は消す」）が残る。踊り場の上（10.6m〜）に置くと全部消えるので切る。
	// 雲の映り込みもタイトルの雲（CloudLayer）用なので切る（HUB には雲が無い）。
	// 環境光の強さもタイトル用（大理石 0.25・金 0.56）が残る。HUB の扉は太陽を背にしていて明るさを IBL に頼るので、
	// そのままだと暗い灰色になる → HUB の値に戻す
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		ModelInstance* model = part->GetModelInstance();
		if (!model) continue;
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (!sm.material) continue;
			sm.material->dissolveEnable = kDissolveOff;
			sm.material->cloudReflection = 0.0f;
			const bool gold = sm.matFilePath.find("_Gold") != std::string::npos;
			sm.material->environmentCoefficient = gold ? doorGoldEnv_ : doorMarbleEnv_;
		}
	}
}

void HubTemple::ApplyFloorParams() {
	WaterSurface::Params& p = floor_->GetParams();
	p.fresnelF0 = floorF0_;
	p.reflectionIntensity = floorReflection_;
	p.ambient = floorAmbient_;
}

void HubTemple::Update(float deltaTime) {
	// Object3DInstance は Update の時のカメラで WVP を作るので、動かない物も毎フレーム呼ぶ
	// （扉を ApplyDoor の 1 回だけにしていたら、初期化時のカメラの行列のまま画面の外に描かれて見えなかった）
	shell_->Update();
	gate_->Update();
	doorFrame_->Update();
	doorLeafL_->Update();
	doorLeafR_->Update();
	for (GateMover& mover : gateMovers_) {
		if (ringsSpin_) {
			mover.angle = std::fmod(mover.angle + mover.spinDegPerSec * gateSpeedScale_ * deltaTime, 360.0f);
		}
		mover.object->SetTranslate({ kGateCenter.x + mover.offset.x, kGateCenter.y + mover.offset.y,
			kGateCenter.z + mover.offset.z });
		mover.object->SetScale({ mover.scale, mover.scale, 1.0f });
		mover.object->SetRotate({ 0.0f, 0.0f, DegToRad(mover.angle) });
		mover.object->Update();
	}
	for (Ring& ring : rings_) {
		if (ringsSpin_) {
			ring.angle = std::fmod(ring.angle + ring.spinDegPerSec * deltaTime, 360.0f);
		}
		// X（輪の面の中の回転）→ Y → Z の順にかかる（MakeAffineMatrix の Rx·Ry·Rz）
		ring.object->SetTranslate(ring.center);
		ring.object->SetRotate({ DegToRad(ring.angle), DegToRad(ring.tiltY), DegToRad(ring.tiltZ) });
		ring.object->Update();
	}
	ApplyFloorParams();
	floor_->Update(deltaTime);
}

void HubTemple::AddReflectionTarget(Object3DInstance* target) {
	reflection_->AddTarget(target);
}

void HubTemple::AddReflectionTarget(AnimatedObject3DInstance* target) {
	reflection_->AddTarget(target);
}

void HubTemple::RemoveReflectionTarget(Object3DInstance* target) {
	reflection_->RemoveTarget(target);
}

void HubTemple::SetFloorHole(bool open, float halfWidth, float chamfer) {
	if (open) {
		floor_->SetOctagonHole({ 0.0f, 0.0f, 0.0f }, halfWidth, chamfer);
	} else {
		floor_->ClearHole();
	}
}

void HubTemple::RenderReflection(const Camera& camera) {
	if (!drawFloor_) return;
	// 床より下（③ の縦穴の中）からは床の裏しか見えないので、映り込みも床も描かない
	if (camera.GetTranslate().y < 0.0f) return;
	reflection_->Render(camera);
}

void HubTemple::Draw(DirectXCore* dxCore) {
	shell_->Draw(dxCore);
	gate_->Draw(dxCore);
	for (GateMover& mover : gateMovers_) mover.object->Draw(dxCore);
	if (drawRings_) {
		for (Ring& ring : rings_) ring.object->Draw(dxCore);
	}
	doorFrame_->Draw(dxCore);
	doorLeafL_->Draw(dxCore);
	doorLeafR_->Draw(dxCore);
}

void HubTemple::DrawFloor(const Camera& camera) {
	if (!drawFloor_) return;
	if (camera.GetTranslate().y < 0.0f) return;
	// 映らなかった所（反射 RT の α=0）は空ではなく神殿の IBL を引く
	floor_->Draw(camera, iblPath_, reflection_.get());
}

void HubTemple::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	const JsonValue& door = node["doorPosition"];
	if (door.IsArray() && door.Size() >= 3) {
		doorPosition_ = { static_cast<float>(door[0].AsDouble(doorPosition_.x)),
			static_cast<float>(door[1].AsDouble(doorPosition_.y)), static_cast<float>(door[2].AsDouble(doorPosition_.z)) };
	}
	gateSpeedScale_ = static_cast<float>(node["gateSpeedScale"].AsDouble(gateSpeedScale_));
	doorScale_ = static_cast<float>(node["doorScale"].AsDouble(doorScale_));
	doorMarbleEnv_ = static_cast<float>(node["doorMarbleEnv"].AsDouble(doorMarbleEnv_));
	doorGoldEnv_ = static_cast<float>(node["doorGoldEnv"].AsDouble(doorGoldEnv_));
	floorF0_ = static_cast<float>(node["floorF0"].AsDouble(floorF0_));
	floorReflection_ = static_cast<float>(node["floorReflection"].AsDouble(floorReflection_));
	floorAmbient_ = static_cast<float>(node["floorAmbient"].AsDouble(floorAmbient_));
	const JsonValue& rings = node["rings"];
	if (rings.IsArray()) {
		for (size_t i = 0; i < rings.Size() && i < rings_.size(); ++i) {
			Ring& ring = rings_[i];
			ring.tiltY = static_cast<float>(rings[i]["tiltY"].AsDouble(ring.tiltY));
			ring.tiltZ = static_cast<float>(rings[i]["tiltZ"].AsDouble(ring.tiltZ));
			ring.spinDegPerSec = static_cast<float>(rings[i]["spinDegPerSec"].AsDouble(ring.spinDegPerSec));
		}
	}
	if (doorFrame_) ApplyDoor();
}

void HubTemple::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	JsonValue door = JsonValue::MakeArray();
	door.Push(JsonValue(static_cast<double>(doorPosition_.x)));
	door.Push(JsonValue(static_cast<double>(doorPosition_.y)));
	door.Push(JsonValue(static_cast<double>(doorPosition_.z)));
	node["doorPosition"] = std::move(door);
	node["gateSpeedScale"] = JsonValue(static_cast<double>(gateSpeedScale_));
	node["doorScale"] = JsonValue(static_cast<double>(doorScale_));
	node["doorMarbleEnv"] = JsonValue(static_cast<double>(doorMarbleEnv_));
	node["doorGoldEnv"] = JsonValue(static_cast<double>(doorGoldEnv_));
	node["floorF0"] = JsonValue(static_cast<double>(floorF0_));
	node["floorReflection"] = JsonValue(static_cast<double>(floorReflection_));
	node["floorAmbient"] = JsonValue(static_cast<double>(floorAmbient_));
	JsonValue rings = JsonValue::MakeArray();
	for (const Ring& ring : rings_) {
		JsonValue r = JsonValue::MakeObject();
		r["tiltY"] = JsonValue(static_cast<double>(ring.tiltY));
		r["tiltZ"] = JsonValue(static_cast<double>(ring.tiltZ));
		r["spinDegPerSec"] = JsonValue(static_cast<double>(ring.spinDegPerSec));
		rings.Push(std::move(r));
	}
	node["rings"] = std::move(rings);
}

void HubTemple::OnImGui() {
#ifdef _DEBUG
	ImGui::TextDisabled("IBL: %s", iblPath_.c_str());
	ImGui::SeparatorText("Rings / Clock Gate");
	ImGui::Checkbox("Spin", &ringsSpin_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::DragFloat("Gate Speed x", &gateSpeedScale_, 0.05f, 0.0f, 20.0f);
	ImGui::SameLine();
	ImGui::Checkbox("Draw Rings", &drawRings_);
	ImGui::TextDisabled("Tilt は gen_hub_temple.py で足場・機体との距離を確かめた値。大きく変えるならスクリプトで確かめる");
	for (size_t i = 0; i < rings_.size(); ++i) {
		Ring& ring = rings_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("Ring %c", static_cast<char>('A' + i));
		ImGui::DragFloat("Tilt Y", &ring.tiltY, 0.5f, -180.0f, 180.0f);
		ImGui::DragFloat("Tilt Z", &ring.tiltZ, 0.5f, -180.0f, 180.0f);
		ImGui::DragFloat("Spin (deg/s)", &ring.spinDegPerSec, 0.1f, -30.0f, 30.0f);
		ImGui::PopID();
	}
	ImGui::SeparatorText("Door");
	bool doorChanged = ImGui::DragFloat3("Door Position", &doorPosition_.x, 0.05f);
	doorChanged |= ImGui::DragFloat("Door Scale", &doorScale_, 0.05f, 0.5f, 4.0f);
	doorChanged |= ImGui::SliderFloat("Door Marble Env (IBL)", &doorMarbleEnv_, 0.0f, 2.0f);
	doorChanged |= ImGui::SliderFloat("Door Gold Env (IBL)", &doorGoldEnv_, 0.0f, 2.0f);
	if (doorChanged) ApplyDoor();
	ImGui::SeparatorText("Mirror Floor");
	ImGui::Checkbox("Draw Floor", &drawFloor_);
	ImGui::SliderFloat("Fresnel F0", &floorF0_, 0.0f, 1.0f);
	ImGui::SliderFloat("Reflection", &floorReflection_, 0.0f, 2.0f);
	ImGui::SliderFloat("Ambient", &floorAmbient_, 0.0f, 2.0f);
	if (ImGui::TreeNode("WaterSurface (raw)")) {
		floor_->OnImGui();
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Reflection (raw)")) {
		reflection_->OnImGui();
		ImGui::TreePop();
	}
#endif // _DEBUG
}
