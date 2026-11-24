#include "FlightUnitAssault.h"

#include "IImGuiEditable.h"
#include "MathUtility.h"
#include "Object3DInstance.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 長さがこれより短いベクトルは向きを決めない
	constexpr float kMinNormalizeLength = 1e-5f;
	// 上の 2 基のファンネルの大きさの初期値
	constexpr Vector3 kUpperFunnelScale = { 0.8f, 0.8f, 0.8f };
	// 上の 2 基のファンネルの左右の位置の初期値（バックパックから）[m]
	constexpr float kUpperFunnelOffsetX = 0.2f;
	// 上の 2 基のファンネルの高さの初期値（バックパックから）[m]
	constexpr float kUpperFunnelOffsetY = 0.6f;
	// 下の 2 基のファンネルの大きさの初期値
	constexpr Vector3 kLowerFunnelScale = { 0.6f, 0.6f, 0.6f };
	// 下の 2 基のファンネルの左右の位置の初期値（バックパックから）[m]
	constexpr float kLowerFunnelOffsetX = 0.4f;
	// 下の 2 基のファンネルを下げる量の初期値（バックパックから）[m]
	constexpr float kLowerFunnelOffsetY = 0.3f;
	// 時間の長さの下限 [秒]（これ以下なら割合を出さない）
	constexpr float kMinSeconds = 1e-4f;
	// 狙う向きと付ける所の前の内積がこれより大きければその向きを使う
	constexpr float kMountFacingDot = 0.3f;
	// 周回の輪を縦につぶす割合（自機の上下を隠さない）
	constexpr float kOrbitVerticalSquash = 0.6f;
	// 周回の輪を自機の中心から後ろへずらす量 [m]
	constexpr float kOrbitBackOffset = 0.3f;
	// 狙う敵を散らす線形合同法の掛ける数
	constexpr uint32_t kLcgMultiplier = 1664525u;
	// 狙う敵を散らす線形合同法の足す数
	constexpr uint32_t kLcgIncrement = 1013904223u;
	// 線形合同法の値の上の方のビットを使うためにずらす量 [ビット]
	constexpr int kLcgHighBitsShift = 16;
	// ファンネルの向きを寄せる速さ [1/秒]
	constexpr float kAimTurnRate = 12.0f;
	// smoothstep（3k² − 2k³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3k² − 2k³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;

	constexpr const char* kFunnelDir = "Resources/Models/Weapons/FlightUnit/Attack/Export";
	constexpr const char* kFunnelLargeFile = "FunnelLarge.mesh";
	constexpr const char* kFunnelSmallFile = "FunnelSmall.mesh";

	/// <summary>ベクトルを正規化する</summary>
	/// <param name="v">正規化するベクトル</param>
	/// <param name="fallback">長さがほぼ 0 のときに返す値</param>
	/// <returns>長さ 1 のベクトル。長さがほぼ 0 なら fallback</returns>
	Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < kMinNormalizeLength) return fallback;
		return { v.x / len, v.y / len, v.z / len };
	}
	/// <summary>外積を求める</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a × b</returns>
	Vector3 CrossV(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	/// <summary>内積を求める</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a・b</returns>
	float DotV(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	/// <summary>行列の r 行目の xyz を取り出す（行＝右・上・前・位置）</summary>
	/// <param name="m">行列</param>
	/// <param name="r">行の番号 0〜3</param>
	/// <returns>r 行目の xyz</returns>
	Vector3 Row(const Matrix4x4& m, int r) { return { m.m[r][0], m.m[r][1], m.m[r][2] }; }
	/// <summary>前（+Z）を forward、上を upHint に寄せたワールド行列を作る（行＝右・上・前・位置）</summary>
	/// <param name="pos">位置</param>
	/// <param name="forward">前に向ける方向</param>
	/// <param name="upHint">上に寄せる方向</param>
	/// <param name="scale">3 軸そろえた拡大率</param>
	/// <returns>ワールド行列</returns>
	Matrix4x4 MakeLookWorld(const Vector3& pos, const Vector3& forward, const Vector3& upHint, float scale) {
		const Vector3 f = Normalized(forward, { 0.0f, 0.0f, 1.0f });
		const Vector3 r = Normalized(CrossV(upHint, f), { 1.0f, 0.0f, 0.0f });
		const Vector3 u = CrossV(f, r);
		Matrix4x4 m = MakeIdentity4x4();
		m.m[0][0] = r.x * scale; m.m[0][1] = r.y * scale; m.m[0][2] = r.z * scale;
		m.m[1][0] = u.x * scale; m.m[1][1] = u.y * scale; m.m[1][2] = u.z * scale;
		m.m[2][0] = f.x * scale; m.m[2][1] = f.y * scale; m.m[2][2] = f.z * scale;
		m.m[3][0] = pos.x; m.m[3][1] = pos.y; m.m[3][2] = pos.z;
		return m;
	}
}

FlightUnitAssault::FlightUnitAssault() {
	// 付ける所の初期値（バックパックのローカル。バックパックは装甲の面＝+Z を後ろへ向けて背負うので、前は -Z。
	// ファンネルの前（+Z）を自機の前へ向けるため Y を 180°）。ImGui で合わせて FlightUnit.json に保存する
	const Vector3 front{ 0.0f, kPi, 0.0f };
	funnels_[0].mount = { kUpperFunnelScale, front, { -kUpperFunnelOffsetX, kUpperFunnelOffsetY, 0.0f } };
	funnels_[1].mount = { kUpperFunnelScale, front, { kUpperFunnelOffsetX, kUpperFunnelOffsetY, 0.0f } };
	funnels_[2].mount = { kLowerFunnelScale, front, { -kLowerFunnelOffsetX, -kLowerFunnelOffsetY, 0.0f } };
	funnels_[3].mount = { kLowerFunnelScale, front, { kLowerFunnelOffsetX, -kLowerFunnelOffsetY, 0.0f } };
}

FlightUnitAssault::~FlightUnitAssault() = default;

void FlightUnitAssault::Initialize(IFlightUnitHost* host, Camera* camera) {
	FlightUnitSkill::Initialize(host, camera);
}

void FlightUnitAssault::InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) {
	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const bool large = i < kLargeCount;
		f.model = std::make_unique<Object3DInstance>();
		f.model->Initialize(object3DManager, dxCore, kFunnelDir, large ? kFunnelLargeFile : kFunnelSmallFile,
			large ? "FunnelLarge" : "FunnelSmall");
		f.model->SetCamera(camera_);
	}
}

bool FlightUnitAssault::Activate() {
	if (!host_) return false;
	// 戻る途中に出し直したら、今の離れ具合から出ていく
	const float k = ReleaseRatio();
	active_ = true;
	returning_ = false;
	timer_ = k * funnelDeploy_;
	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		if (k <= 0.0f) f.aimDir = (i < kLargeCount) ? f.turretDir : f.mountForward;
		// 撃つ間隔をずらして、一斉ではなく順に撃たせる
		f.fireTimer = funnelDeploy_ + funnelFireInterval_ * static_cast<float>(i) / static_cast<float>(kFunnelCount);
	}
	return true;
}

void FlightUnitAssault::Cancel(bool restoreTime) {
	(void)restoreTime;
	if (active_) {
		// 輪からバックパックへ戻る（出ていく途中なら、その離れ具合から）
		returnTimer_ = (1.0f - ReleaseRatio()) * funnelReturn_;
		returning_ = true;
	}
	active_ = false;
	timer_ = 0.0f;
}

float FlightUnitAssault::GetActiveRatio() const {
	const float d = funnelDuration_[LevelIndex()];
	return d > kMinSeconds ? std::clamp(1.0f - timer_ / d, 0.0f, 1.0f) : 0.0f;
}

float FlightUnitAssault::ReleaseRatio() const {
	if (active_) return funnelDeploy_ > kMinSeconds ? (std::min)(timer_ / funnelDeploy_, 1.0f) : 1.0f;
	if (returning_) return funnelReturn_ > kMinSeconds ? std::clamp(1.0f - returnTimer_ / funnelReturn_, 0.0f, 1.0f) : 0.0f;
	return 0.0f;
}

Vector3 FlightUnitAssault::MuzzlePosition(const Funnel& f) const {
	const Vector3 p = Row(f.world, 3);
	const Vector3 z = Row(f.world, 2);   // 大きさ込みの +Z
	return { p.x + z.x * muzzleZ_, p.y + z.y * muzzleZ_, p.z + z.z * muzzleZ_ };
}

void FlightUnitAssault::Update(float dt, bool canAct) {
	if (!host_) return;
	UpdateFunnels(dt, canAct);
	UpdatePassive(dt, canAct);
}

void FlightUnitAssault::UpdatePassive(float dt, bool canAct) {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 aim = host_->GetReticleAimPoint();

	// ファンネル大の砲身を、付いたままレティクルへ向ける（後ろ寄りの狙いは追わない）
	const float turn = 1.0f - std::exp(-dt * turretTurnSpeed_);
	for (int i = 0; i < kLargeCount; ++i) {
		Funnel& f = funnels_[i];
		Vector3 desired = f.mountForward;
		if (canAct && f.hasWorld) {
			const Vector3 p = Row(f.world, 3);
			const Vector3 d = Normalized({ aim.x - p.x, aim.y - p.y, aim.z - p.z }, f.mountForward);
			if (DotV(d, f.mountForward) > kMountFacingDot) desired = d;
		}
		f.turretDir = Normalized(Lerp(f.turretDir, desired, turn), f.mountForward);
	}

	// 撃っているファンネル大のビームを付け直す（付いている間はレティクルへ、離れている間は狙った敵へ）
	const bool released = ReleaseRatio() > 0.0f;
	for (int i = 0; i < kLargeCount; ++i) {
		Funnel& f = funnels_[i];
		if (f.beamId == 0) continue;
		if (!host_->IsSupportBeamActive(f.beamId)) { f.beamId = 0; continue; }
		const Vector3 from = f.hasWorld ? MuzzlePosition(f) : host_->GetFlightUnitAnchor();
		const Vector3 dir = released ? f.aimDir : Normalized({ aim.x - from.x, aim.y - from.y, aim.z - from.z }, forward);
		host_->SetSupportBeamPose(f.beamId, from, dir);
	}

	// 付いている間のビームのクールタイム（離れている間は UpdateFunnels が数える）
	if (active_) return;
	for (int i = 0; i < kLargeCount; ++i) {
		if (funnels_[i].fireTimer > 0.0f) funnels_[i].fireTimer -= dt;
	}
}

void FlightUnitAssault::FireLargeBeam(int i, const Vector3& dir, float damageScale) {
	Funnel& f = funnels_[i];
	const Vector3 from = f.hasWorld ? MuzzlePosition(f) : host_->GetFlightUnitAnchor();
	f.beamId = host_->FireSupportBeam(from, dir, damageScale);
	f.fireTimer = largeBeamCooldown_[LevelIndex()];
}

void FlightUnitAssault::OnPlayerShot(bool canAct) {
	// ファンネルが離れている間（戻るまで）は撃たない
	if (!host_ || !canAct || active_ || returning_) return;
	const Vector3 aim = host_->GetReticleAimPoint();
	// クールタイムが明けているファンネル大を、左右交互に 1 基だけ撃たせる
	for (int k = 0; k < kLargeCount; ++k) {
		const int i = (passiveNext_ + k) % kLargeCount;
		const Funnel& f = funnels_[i];
		if (f.fireTimer > 0.0f || f.beamId != 0) continue;
		const Vector3 from = f.hasWorld ? MuzzlePosition(f) : host_->GetFlightUnitAnchor();
		FireLargeBeam(i, Normalized({ aim.x - from.x, aim.y - from.y, aim.z - from.z }, f.mountForward),
			passiveDamageScale_[LevelIndex()]);
		passiveNext_ = (i + 1) % kLargeCount;
		return;
	}
}

Vector3 FlightUnitAssault::FunnelHome(int i) const {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const float a = spinAngle_ + kTwoPi * static_cast<float>(i) / static_cast<float>(kFunnelCount);
	const float c = std::cos(a) * funnelRadius_;
	const float s = std::sin(a) * funnelRadius_ * kOrbitVerticalSquash;   // 縦に少しつぶす（自機の上下を隠さない）
	return {
		anchor.x + right.x * c + up.x * s - forward.x * kOrbitBackOffset,
		anchor.y + right.y * c + up.y * s - forward.y * kOrbitBackOffset,
		anchor.z + right.z * c + up.z * s - forward.z * kOrbitBackOffset,
	};
}

IImGuiEditable* FlightUnitAssault::PickFunnelTarget(int i) {
	// レティクルが重なっている敵を優先
	if (IImGuiEditable* t = host_->GetReticleTarget(); t && host_->IsEnemyAlive(t)) return t;
	if (screenEnemies_.empty()) return nullptr;
	// 重なっていなければ画面上の敵へ散らす（ファンネルごとに違う敵を狙いやすく）
	pickSeed_ = pickSeed_ * kLcgMultiplier + kLcgIncrement;
	const size_t idx = (static_cast<size_t>(i) + (pickSeed_ >> kLcgHighBitsShift)) % screenEnemies_.size();
	return screenEnemies_[idx];
}

void FlightUnitAssault::UpdateFunnels(float dt, bool canAct) {
	if (returning_) {
		spinAngle_ += funnelSpin_ * dt;
		returnTimer_ += dt;
		if (returnTimer_ >= funnelReturn_) returning_ = false;
		return;
	}
	if (!active_) return;
	timer_ += dt;
	if (timer_ >= funnelDuration_[LevelIndex()]) {
		Cancel(false);
		return;
	}
	spinAngle_ += funnelSpin_ * dt;

	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	screenEnemies_.clear();
	if (canAct) host_->CollectOnScreenEnemies(screenEnemies_);

	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const Vector3 pos = f.hasWorld ? Row(f.world, 3) : FunnelHome(i);

		IImGuiEditable* target = canAct ? PickFunnelTarget(i) : nullptr;
		Vector3 desired = forward;
		if (target) {
			if (const Vector3* tp = target->GetEditableTranslate()) {
				desired = Normalized({ tp->x - pos.x, tp->y - pos.y, tp->z - pos.z }, forward);
			}
		}
		// 向きはすっと寄せる
		const float turn = 1.0f - std::exp(-dt * kAimTurnRate);
		f.aimDir = Normalized(Lerp(f.aimDir, desired, turn), forward);

		f.fireTimer -= dt;
		if (f.fireTimer <= 0.0f) {
			if (i < kLargeCount) {
				// 大：狙う敵がいて前のビームが終わっていればビーム（長いクールタイム）。いなければ明けたまま待つ
				if (target && f.beamId == 0) FireLargeBeam(i, desired, funnelDamageScale_[LevelIndex()]);
				else f.fireTimer = 0.0f;
			} else {
				// 小：自機の通常弾と同じ単発（短い間隔）
				f.fireTimer = funnelFireInterval_;
				if (target) host_->FireSupportBullet(f.hasWorld ? MuzzlePosition(f) : pos, desired, target, funnelDamageScale_[LevelIndex()]);
			}
		}
	}
}

void FlightUnitAssault::UpdateModels(const Matrix4x4& backpackWorld) {
	if (!host_) return;
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	float k = ReleaseRatio();
	k = k * k * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * k);

	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const Matrix4x4 mount = Multiply(MakeAffineMatrix(f.mount), backpackWorld);
		const Vector3 mountZ = Row(mount, 2);
		const float scale = Length(mountZ);   // バックパックと付ける所の大きさを掛けたもの
		f.mountForward = Normalized(mountZ, forward);
		// モデルだけ見せるとき（HUB の機体設定）は狙いを追わないので、砲身は付ける所の前のまま（ImGui の角度がすぐ見える）
		if (!f.hasWorld || modelPreview_) f.turretDir = f.mountForward;

		if (k <= 0.0f && i >= kLargeCount) {
			f.world = mount;
		} else {
			// 付いている形（大は砲身をレティクルへ）と輪を回る形の間
			const Vector3 restDir = (i < kLargeCount) ? f.turretDir : f.mountForward;
			const Vector3 mountUp = Normalized(Row(mount, 1), up);
			const Vector3 pos = Lerp(Row(mount, 3), FunnelHome(i), k);
			const Vector3 dir = Normalized(Lerp(restDir, f.aimDir, k), f.mountForward);
			const Vector3 upHint = Normalized(Lerp(mountUp, up, k), up);
			f.world = MakeLookWorld(pos, dir, upHint, scale);
		}
		f.hasWorld = true;
		if (f.model) {
			f.model->SetWorldMatrixOverride(f.world);
			f.model->SetTranslate(Row(f.world, 3));
			f.model->Update();
		}
	}
}

void FlightUnitAssault::DrawModels(DirectXCore* dxCore, bool idPass) {
	for (const Funnel& f : funnels_) {
		if (!f.model || !f.hasWorld) continue;
		if (idPass) f.model->DrawIdPass(dxCore); else f.model->Draw(dxCore);
	}
}

void FlightUnitAssault::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "largeBeamCooldown", largeBeamCooldown_);
	ReadLevels(node, "passiveDamageScale", passiveDamageScale_);
	ReadLevels(node, "funnelDuration", funnelDuration_);
	ReadLevels(node, "funnelDamageScale", funnelDamageScale_);
	ReadLevels(node, "cooldown", cooldown_);
	ReadLevels(node, "attackScale", attackScale_);
	funnelFireInterval_ = static_cast<float>(node["funnelFireInterval"].AsDouble(funnelFireInterval_));
	funnelRadius_ = static_cast<float>(node["funnelRadius"].AsDouble(funnelRadius_));
	funnelSpin_ = static_cast<float>(node["funnelSpin"].AsDouble(funnelSpin_));
	funnelDeploy_ = static_cast<float>(node["funnelDeploy"].AsDouble(funnelDeploy_));
	funnelReturn_ = static_cast<float>(node["funnelReturn"].AsDouble(funnelReturn_));
	turretTurnSpeed_ = static_cast<float>(node["turretTurnSpeed"].AsDouble(turretTurnSpeed_));
	muzzleZ_ = static_cast<float>(node["muzzleZ"].AsDouble(muzzleZ_));
	const JsonValue& mounts = node["mounts"];
	if (mounts.IsArray()) {
		for (size_t i = 0; i < funnels_.size() && i < mounts.Size(); ++i) ReadTransform(mounts[i], funnels_[i].mount);
	}
	ReadVec4(node, "color", color_);
}

void FlightUnitAssault::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "largeBeamCooldown", largeBeamCooldown_);
	WriteLevels(node, "passiveDamageScale", passiveDamageScale_);
	WriteLevels(node, "funnelDuration", funnelDuration_);
	WriteLevels(node, "funnelDamageScale", funnelDamageScale_);
	WriteLevels(node, "cooldown", cooldown_);
	WriteLevels(node, "attackScale", attackScale_);
	node["funnelFireInterval"] = JsonValue(static_cast<double>(funnelFireInterval_));
	node["funnelRadius"] = JsonValue(static_cast<double>(funnelRadius_));
	node["funnelSpin"] = JsonValue(static_cast<double>(funnelSpin_));
	node["funnelDeploy"] = JsonValue(static_cast<double>(funnelDeploy_));
	node["funnelReturn"] = JsonValue(static_cast<double>(funnelReturn_));
	node["turretTurnSpeed"] = JsonValue(static_cast<double>(turretTurnSpeed_));
	node["muzzleZ"] = JsonValue(static_cast<double>(muzzleZ_));
	JsonValue mounts = JsonValue::MakeArray();
	for (const Funnel& f : funnels_) mounts.Push(WriteTransform(f.mount));
	node["mounts"] = std::move(mounts);
	WriteVec4(node, "color", color_);
}

void FlightUnitAssault::OnImGui() {
#ifdef _DEBUG
	ImGui::SeparatorText("Passive (自機の射撃に合わせてファンネル大がビーム)");
	DragLevels("Large Beam Cooldown##Assault", largeBeamCooldown_, 0.05f, 0.0f, 60.0f);
	DragLevels("Damage Scale##AssaultPassive", passiveDamageScale_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Turret Turn##Assault", &turretTurnSpeed_, 0.1f, 0.0f, 50.0f, "%.1f");
	ImGui::DragFloat("Muzzle Z##Assault", &muzzleZ_, 0.01f, -2.0f, 2.0f, "%.2f");
	ImGui::SeparatorText("RB (funnels)");
	DragLevels("Duration##Assault", funnelDuration_, 0.05f, 0.1f, 30.0f);
	DragLevels("Damage Scale##AssaultFunnel", funnelDamageScale_, 0.01f, 0.0f, 5.0f);
	DragLevels("Cooldown##Assault", cooldown_, 0.1f, 0.0f, 120.0f);
	DragLevels("Attack Scale##Assault", attackScale_, 0.01f, 0.5f, 3.0f);
	ImGui::DragFloat("Small Fire Interval##Assault", &funnelFireInterval_, 0.01f, 0.05f, 5.0f, "%.2f");
	ImGui::DragFloat("Radius##Assault", &funnelRadius_, 0.05f, 0.1f, 10.0f, "%.2f");
	ImGui::DragFloat("Spin##Assault", &funnelSpin_, 0.05f, -10.0f, 10.0f, "%.2f");
	ImGui::DragFloat("Deploy##Assault", &funnelDeploy_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Return##Assault", &funnelReturn_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::ColorEdit4("Color##Assault", &color_.x);
	// 付いている所（バックパックのローカル）。攻撃系を付けているとゲームの中でそのまま見える
	if (ImGui::TreeNode("Mounts (backpack local)##Assault")) {
		const char* names[kFunnelCount] = { "Large A", "Large B", "Small A", "Small B" };
		for (int i = 0; i < kFunnelCount; ++i) {
			ImGui::SeparatorText(names[i]);
			DragTransform(names[i], funnels_[i].mount);
		}
		ImGui::TreePop();
	}
#endif
}
