#include "LoadoutAttachments.h"

#include "AnimatedObject3DInstance.h"
#include "Object3DInstance.h"
#include "ModelManager.h"
#include "MathUtility.h"
#include "Effect/BeamShieldRenderer.h"
#include "FlightUnit/FlightUnitPart.h"
#include "StageStart/PlayerWingUnit.h"
#include "Score/LoadoutState.h"
#include "Score/MainWeaponCatalog.h"
#include "Score/ShieldCatalog.h"
#include "Json/JsonParser.h"
#include "Score/GameSession.h"
#include <filesystem>

namespace {
	// 背中のユニットの狙いを体の中心から前へ置く距離（モデルの拡大率 1 のとき）[m]
	constexpr float kAimForwardDistance = 100.0f;

	// 羽根の見た目の調整値はステージと同じ物を使う（StagePlay.json の "playerWings"）
	constexpr const char* kStagePlayTuningPath = "Resources/Json/Tuning/StagePlay.json";
	constexpr const char* kAnchorBone = "mixamorig:Spine2";

	/// <summary>付ける位置・大きさを k 倍した Transform を作る（回転はそのまま）</summary>
	/// <param name="translate">位置</param>
	/// <param name="rotate">回転 [ラジアン]</param>
	/// <param name="scale">大きさ</param>
	/// <param name="k">位置と大きさに掛ける倍率</param>
	/// <returns>Transform</returns>
	Transform ScaledMount(const Vector3& translate, const Vector3& rotate, const Vector3& scale, float k) {
		return Transform{ { scale.x * k, scale.y * k, scale.z * k }, rotate, { translate.x * k, translate.y * k, translate.z * k } };
	}
}

/// <summary>
/// 飛行ユニットのモデルだけを動かすための窓口。技は動かさないので、狙い・体の中心だけ返し、ほかは何もしない
/// </summary>
class LoadoutAttachments::PreviewHost : public IFlightUnitHost {
public:
	Vector3 anchor{};
	Vector3 aim{};
	// 持ち主（LoadoutAttachments）が渡すプレイの状態（装備を読む）
	GameSession* gameSession = nullptr;

	/// <summary>プレイの状態を返す（装備を読むのに使う）</summary>
	/// <returns>持ち主から預かったプレイの状態</returns>
	GameSession* GetGameSession() const override { return gameSession; }

	/// <summary>自機を返す（装備画面には無い）</summary>
	/// <returns>常に nullptr</returns>
	IImGuiEditable* GetPlayer() const override { return nullptr; }
	/// <summary>時間の倍率を返す（装備画面では止めない）</summary>
	/// <returns>常に 1</returns>
	float GetTimeScale(TimeGroup) const override { return 1.0f; }
	/// <summary>何もしない（技を動かさないので時間に触らない）</summary>
	void SetTimeScale(TimeGroup, float) override {}
	/// <summary>自機の体の中心を返す</summary>
	/// <returns>持ち主が入れた anchor</returns>
	Vector3 GetFlightUnitAnchor() const override { return anchor; }
	/// <summary>レティクルが重なっている敵を返す（装備画面には無い）</summary>
	/// <returns>常に nullptr</returns>
	IImGuiEditable* GetReticleTarget() const override { return nullptr; }
	/// <summary>狙いの点を返す</summary>
	/// <returns>持ち主が入れた aim</returns>
	Vector3 GetReticleAimPoint() const override { return aim; }
	/// <summary>何もしない（装備画面に敵はいない）</summary>
	void CollectOnScreenEnemies(std::vector<IImGuiEditable*>&) override {}
	/// <summary>敵が生きているかを返す（装備画面に敵はいない）</summary>
	/// <returns>常に false</returns>
	bool IsEnemyAlive(IImGuiEditable*) const override { return false; }
	/// <summary>何もしない（撃たない）</summary>
	void FireSupportBullet(const Vector3&, const Vector3&, IImGuiEditable*, float) override {}
	/// <summary>敵弾を消す（装備画面に敵弾はない）</summary>
	/// <returns>常に 0</returns>
	int AbsorbEnemyBulletsInSphere(const Vector3&, float) override { return 0; }
};

LoadoutAttachments::LoadoutAttachments() = default;
LoadoutAttachments::~LoadoutAttachments() = default;

void LoadoutAttachments::Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
	DirectXCore* dxCore, SRVManager* srvManager, Camera* camera) {
	dxCore_ = dxCore;
	object3DManager_ = object3DManager;
	camera_ = camera;

	beamShield_ = std::make_unique<BeamShieldRenderer>();
	beamShield_->Initialize(dxCore);

	host_ = std::make_unique<PreviewHost>();
	// FlightUnitPart::Initialize が装備を読むので、その前に渡す
	host_->gameSession = gameSession_;
	flightUnit_ = std::make_unique<FlightUnitPart>();
	flightUnit_->Initialize(host_.get(), camera, nullptr, object3DManager, dxCore);
	flightUnit_->SetModelPreview(true);

	wings_ = std::make_unique<PlayerWingUnit>();
	wings_->Initialize(object3DManager, skinningComputeManager, dxCore, srvManager);
	if (std::filesystem::exists(kStagePlayTuningPath)) {
		const auto result = JsonParser::ParseFile(kStagePlayTuningPath);
		if (result.success) wings_->LoadTuning(result.value["playerWings"]);
	}

	Rebuild();
}

void LoadoutAttachments::Rebuild() {
	const LoadoutState* loadout = gameSession_->GetLoadoutState();

	// 右手：作り直すと前のフレームの定数バッファを GPU が使っている間に消すので、モデルだけ差し替える（StagePlayScene と同じ）
	weaponShown_ = false;
	weaponDef_ = loadout->GetMainWeapon();
	if (const MainWeaponDef* mw = weaponDef_; mw && !mw->modelFile.empty()) {
		if (!weapon_) {
			weapon_ = std::make_unique<Object3DInstance>();
			weapon_->Initialize(object3DManager_, dxCore_, mw->modelDir, mw->modelFile, "LoadoutWeapon");
		} else {
			ModelManager::GetInstance()->LoadModel(mw->modelDir, mw->modelFile);
			weapon_->SetModel(mw->modelFile);
		}
		weapon_->SetCamera(camera_);
		weaponShown_ = true;
	}

	// 左手
	shieldShown_ = false;
	shieldDef_ = loadout->GetShield();
	if (const ShieldDef* sd = shieldDef_; sd && !sd->modelFile.empty()) {
		if (!shield_) {
			shield_ = std::make_unique<Object3DInstance>();
			shield_->Initialize(object3DManager_, dxCore_, sd->modelDir, sd->modelFile, "LoadoutShield");
		} else {
			ModelManager::GetInstance()->LoadModel(sd->modelDir, sd->modelFile);
			shield_->SetModel(sd->modelFile);
		}
		shield_->SetCamera(camera_);
		shieldShown_ = true;
	}

	// 背中：系統と段階を付け直す。バックパックを背負う系統の間は羽根を出さない（ステージと同じ）
	flightUnit_->SyncLoadout();
	if (flightUnit_->UsesBackpack()) {
		wings_->Hide();
	} else {
		wings_->ShowDeployed();
	}
}

void LoadoutAttachments::Update(AnimatedObject3DInstance* body, float deltaTime) {
	body_ = body;
	if (!body) return;
	const float k = body->GetScale().x;

	if (weaponShown_ && weapon_) {
		const MainWeaponDef* mw = weaponDef_;
		weaponSocket_.target = body;
		weaponSocket_.jointName = "mixamorig:RightHand";
		weaponSocket_.offset = ScaledMount(mw->gripTranslate, mw->gripRotate, mw->gripScale, k);
		weapon_->SetWorldMatrixOverride(weaponSocket_.World());
		weapon_->SetTranslate(weaponSocket_.Position());
		weapon_->Update();
	}

	if (shieldShown_ && shield_) {
		const ShieldDef* sd = shieldDef_;
		shieldSocket_.target = body;
		shieldSocket_.jointName = sd->bone;
		shieldSocket_.offset = ScaledMount(sd->gripTranslate, sd->gripRotate, sd->gripScale, k);
		const Matrix4x4 shieldWorld = shieldSocket_.World();
		shield_->SetWorldMatrixOverride(shieldWorld);
		shield_->SetTranslate(shieldSocket_.Position());
		shield_->Update();
		// ビームシールド：発生装置の位置（盾のローカル）を盾の行列に重ねる（盾の行列が拡大込みなのでビームも大きくなる）
		if (sd->beam) {
			Matrix4x4 emitter = MakeIdentity4x4();
			emitter.m[3][0] = sd->beamOffset.x;
			emitter.m[3][1] = sd->beamOffset.y;
			emitter.m[3][2] = sd->beamOffset.z;
			beamShield_->SetAppearance(sd->beamLook);
			beamShield_->SetWorld(Multiply(emitter, shieldWorld));
			beamShield_->Update(deltaTime);
		}
	}

	// 背中のユニット：窓口の体の中心と狙い（大きいファンネルの砲身は前を向けておく）
	BoneSocket anchor;
	anchor.target = body;
	anchor.jointName = kAnchorBone;
	host_->anchor = anchor.Position();
	host_->aim = { host_->anchor.x, host_->anchor.y, host_->anchor.z + kAimForwardDistance * k };
	flightUnit_->SetModelScale(k);
	flightUnit_->UpdateModels(body);

	wings_->Update(body, deltaTime);
}

void LoadoutAttachments::OnImGuiFlightUnit() {
	const FlightUnitKind before = gameSession_->GetLoadoutState()->GetFlightUnit();
	flightUnit_->OnImGui();
	if (gameSession_->GetLoadoutState()->GetFlightUnit() != before) Rebuild();
}

bool LoadoutAttachments::IsShieldBeamShown() const {
	if (!shieldShown_ || !body_ || !body_->GetVisible()) return false;
	return shieldDef_ && shieldDef_->beam;
}

void LoadoutAttachments::Draw(DirectXCore* dxCore) {
	if (!body_ || !body_->GetVisible()) return;
	if (weaponShown_ && weapon_) weapon_->Draw(dxCore);
	if (shieldShown_ && shield_) shield_->Draw(dxCore);
	flightUnit_->DrawModels(dxCore);
	wings_->Draw(dxCore);
}

void LoadoutAttachments::DrawTransparent(const Camera& camera) {
	if (IsShieldBeamShown()) beamShield_->Draw(camera);
}

bool LoadoutAttachments::HasBloom() const {
	return IsShieldBeamShown() || (wings_ && wings_->HasBloom());
}

void LoadoutAttachments::DrawBloom(DirectXCore* dxCore, const Camera& camera) {
	if (!body_ || !body_->GetVisible()) return;
	if (IsShieldBeamShown()) beamShield_->DrawBloomPass(camera);
	if (wings_->HasBloom()) wings_->DrawBloom(dxCore);
}

void LoadoutAttachments::DrawIdPass(DirectXCore* dxCore, Part part) {
	if (!body_ || !body_->GetVisible()) return;
	const bool all = part == Part::All;
	if ((all || part == Part::MainWeapon) && weaponShown_ && weapon_) weapon_->DrawIdPass(dxCore);
	if ((all || part == Part::Shield) && shieldShown_ && shield_) shield_->DrawIdPass(dxCore);
	if (all || part == Part::FlightUnit) {
		flightUnit_->DrawModelsIdPass(dxCore);
		wings_->DrawIdPass(dxCore);
	}
}
