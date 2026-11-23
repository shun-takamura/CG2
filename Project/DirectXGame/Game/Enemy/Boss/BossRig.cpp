#include "BossRig.h"

#include <algorithm>
#include <cmath>

#include "IImGuiEditable.h"
#include "Components/Gameplay.h"
#include "Effect/EffectManager.h"
#include "SoundManager.h"
#include "Primitive/PrimitiveInstance.h"
#include "Quaternion.h"
#include "MathUtility.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kArmPrefab = "boss_arm";
	constexpr const char* kFistPrefab = "boss_fist";
	constexpr const char* kLegPrefab = "boss_leg";
	constexpr const char* kArmDebrisPrefab = "boss_arm_debris";
	constexpr const char* kBreakEffect = "Explosion_00";

	// 左右の数（腕・脚）
	constexpr int kSideCount = 2;
	// これより短いベクトル・水平成分は長さ 0 とみなす
	constexpr float kNearZeroLength = 1e-6f;
	// 回転軸の長さがこれより短いときは X 軸を使う
	constexpr float kNearZeroAxisLength = 1e-5f;
	// これより短い寄る時間は即座に寄せる [秒]
	constexpr float kMinSmoothTime = 1e-4f;
	// これより短い経過時間では歩きを更新しない [秒]
	constexpr float kMinWalkDeltaTime = 1e-5f;
	// 胴の向きが本体の向きへ寄る時間 [秒]
	constexpr float kBodyYawSmoothTime = 0.08f;
	// 接地とみなす高さの余裕 [m]
	constexpr float kGroundedTolerance = 0.3f;
	// 足踏みが最大になる移動の速さ [m/s]
	constexpr float kFullWalkSpeed = 6.0f;
	// 足踏みの強さが目標へ寄る時間 [秒]
	constexpr float kWalkAmountSmoothTime = 0.15f;
	// 歩幅の下限 [m]（0 割りを防ぐ）
	constexpr float kMinStrideLength = 0.1f;
	// 待機中の腕の揺れの速さ [rad/秒]
	constexpr float kIdleSwaySpeed = 1.7f;
	// 待機中の腕の揺れの左右のずれ [rad]
	constexpr float kIdleSwaySidePhase = 1.3f;
	// 待機中に腕が構えへ寄る時間 [秒]
	constexpr float kIdleArmSmoothTime = 0.2f;
	// 腕が壊れたときに弾く向きの前方成分（外向き 1 に対して）
	constexpr float kBreakKickForward = 0.3f;
	// 腕が壊れたときに破片を弾く水平の速さ [m/s]
	constexpr float kBreakKickHorizontalSpeed = 5.0f;
	// 腕が壊れたときに破片を弾く上向きの速さ [m/s]
	constexpr float kBreakKickUpSpeed = 8.0f;
	// 拳の破片の弾く水平の速さの倍率（腕の破片に対して）
	constexpr float kFistKickHorizontalScale = 1.2f;
	// 拳の破片の弾く上向きの速さの倍率（腕の破片に対して）
	constexpr float kFistKickUpScale = 0.9f;
	// 撃破で脚が破片になるときの上向きの速さ [m/s]
	constexpr float kLegDebrisUpSpeed = 4.0f;
	// 破片の回転の速さ [rad/秒]
	constexpr float kDebrisSpinSpeed = 6.0f;
	// 破片が止まる床の高さ（足場からの高さ）[m]
	constexpr float kDebrisFloorOffset = 0.5f;
	// 破片が床で跳ね返るときの縦の速さの倍率
	constexpr float kDebrisBounceRestitution = 0.25f;
	// 破片が床に当たったときの横の速さの倍率
	constexpr float kDebrisBounceFriction = 0.6f;
	// 破片が床に当たったときの回転の速さの倍率
	constexpr float kDebrisBounceSpinDamping = 0.5f;

	/// <summary>左右の符号</summary>
	/// <param name="side">左右</param>
	/// <returns>右なら +1、左なら −1</returns>
	float SideSign(BossRig::Side side) { return side == BossRig::Side::Right ? 1.0f : -1.0f; }

	/// <summary>ベクトルを正規化する</summary>
	/// <param name="v">正規化するベクトル</param>
	/// <returns>単位ベクトル。長さがほぼ 0 なら真下 (0, −1, 0)</returns>
	Vector3 SafeNormalize(const Vector3& v) {
		const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return l > kNearZeroLength ? Vector3{ v.x / l, v.y / l, v.z / l } : Vector3{ 0.0f, -1.0f, 0.0f };
	}

	/// <summary>指数で目標へ寄せる時の、このフレームの補間率を求める</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="smoothTime">寄る時間の目安 [秒]（0 に近ければ即座に寄る）</param>
	/// <returns>補間率（0〜1）</returns>
	float ExpBlend(float dt, float smoothTime) {
		return smoothTime > kMinSmoothTime ? 1.0f - std::exp(-dt / smoothTime) : 1.0f;
	}

	/// <summary>
	/// 実体に回転を当てる。当たり判定（CollisionGeometry）はオイラー角を MakeRotateMatrix（Rz·Ry·Rx）で組み、
	/// ローカル +Y が手足の向きになる。描画は組み方が違い（MakeAffineMatrix のオイラーは Rx·Ry·Rz、
	/// QuaternionFromEuler は転置になる）、ピッチとヨーが混ざると見た目だけ向きがずれる（メモ「回転順の罠」）。
	/// 描画には「+Y を手足の向きへ回す軸と角度」のクォータニオンを渡す（実機で確認、2026-11-10）
	/// </summary>
	/// <param name="e">回転を当てる実体（nullptr なら何もしない）</param>
	/// <param name="rot">当たり判定用のオイラー角 [ラジアン]</param>
	void ApplyRotate(IImGuiEditable* e, const Vector3& rot) {
		if (!e) return;
		e->SetRotate(rot);
		auto* prim = dynamic_cast<PrimitiveInstance*>(e);
		if (!prim) return;
		const Matrix4x4 m = MakeRotateMatrix(rot);
		const Vector3 dir{ m.m[1][0], m.m[1][1], m.m[1][2] }; // 当たり判定の +Y（手足の向き）
		// 軸 = Y × dir、角度 = acos(dir.y)
		Vector3 axis{ dir.z, 0.0f, -dir.x };
		const float len = std::sqrt(axis.x * axis.x + axis.z * axis.z);
		if (len < kNearZeroAxisLength) {
			axis = { 1.0f, 0.0f, 0.0f };
		} else {
			axis.x /= len;
			axis.z /= len;
		}
		const float angle = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
		prim->GetMesh().SetRotateQuaternion(MakeRotateAxisAngleQuaternion(axis, angle));
	}

	/// <summary>角度を −π〜π に収める</summary>
	/// <param name="a">角度 [ラジアン]</param>
	/// <returns>収めた角度 [ラジアン]</returns>
	float WrapAngle(float a) {
		while (a > kPi) a -= kTwoPi;
		while (a < -kPi) a += kTwoPi;
		return a;
	}
}

void BossRig::Initialize(IBossRigHost* host) {
	host_ = host;
}

void BossRig::Spawn(IImGuiEditable* body) {
	Clear();
	body_ = body;
	if (!host_ || !body_) return;
	const Vector3* bp = body_->GetEditableTranslate();
	if (!bp) return;
	bodyPos_ = *bp;
	prevBodyPos_ = *bp;
	hasPrevBody_ = true;
	groundY_ = bp->y - bodyHeight_;
	if (const Vector3* r = body_->GetEditableRotate()) bodyYaw_ = r->y;
	walkPhase_ = 0.0f;
	walkAmount_ = 0.0f;

	for (int i = 0; i < kSideCount; ++i) {
		Arm& a = arms_[i];
		a = Arm{};
		a.arm = host_->SpawnRigPiece(kArmPrefab, *bp);
		a.fist = host_->SpawnRigPiece(kFistPrefab, *bp);
		if (a.arm) a.lastHp = Gameplay::Of(a.arm).GetHP().currentHP;
		Leg& l = legs_[i];
		l = Leg{};
		l.leg = host_->SpawnRigPiece(kLegPrefab, *bp);
		if (l.leg) l.lastHp = Gameplay::Of(l.leg).GetHP().currentHP;
		// 部位のダメージは本体へ流れるので、多段ヒットの重複判定は本体単位で数える
		for (IImGuiEditable* piece : { a.arm, a.fist, l.leg }) {
			if (piece) Gameplay::Of(piece).SetDamageOwner(body_);
		}
	}
	Update(0.0f);
}

void BossRig::Clear() {
	if (host_) {
		for (Arm& a : arms_) {
			if (a.arm) host_->DestroyRigPiece(a.arm);
			if (a.fist) host_->DestroyRigPiece(a.fist);
		}
		for (Leg& l : legs_) {
			if (l.leg) host_->DestroyRigPiece(l.leg);
		}
		for (Debris& d : debris_) {
			if (d.entity) host_->DestroyRigPiece(d.entity);
		}
	}
	for (Arm& a : arms_) a = Arm{};
	for (Leg& l : legs_) l = Leg{};
	debris_.clear();
	brokenQueue_.clear();
	body_ = nullptr;
	hasPrevBody_ = false;
}

Vector3 BossRig::ToWorldDir(const Vector3& local) const {
	// 胴の基底：前 = (sin yaw, 0, cos yaw)、右 = (cos yaw, 0, −sin yaw)
	const float s = std::sin(bodyYaw_), c = std::cos(bodyYaw_);
	return { local.x * c + local.z * s, local.y, -local.x * s + local.z * c };
}

Vector3 BossRig::ToWorldPoint(const Vector3& local) const {
	const Vector3 d = ToWorldDir(local);
	return { bodyPos_.x + d.x, bodyPos_.y + d.y, bodyPos_.z + d.z };
}

Vector3 BossRig::ShoulderLocal(Side side) const {
	return { SideSign(side) * shoulderX_, shoulderY_, 0.0f };
}

Vector3 BossRig::ArmDirLocal(Side side, float raise, float swing) const {
	const float s = SideSign(side);
	// 垂らした腕を前へ上げ（raise）、外へ少し開く。そこから体の内側へ薙ぐ向き（swing）に Y まわりで回す
	Vector3 d = SafeNormalize({ s * armSplay_, -std::cos(raise), std::sin(raise) });
	const float a = -s * swing;
	const float ca = std::cos(a), sa = std::sin(a);
	return SafeNormalize({ d.x * ca + d.z * sa, d.y, -d.x * sa + d.z * ca });
}

Vector3 BossRig::RotateYAxisTo(const Vector3& dir) {
	// MakeRotateMatrix(Rz·Ry·Rx) の 2 行目（ローカル +Y）は x = 0 のとき (−sin z·cos y, cos z, sin z·sin y)
	const Vector3 d = SafeNormalize(dir);
	const float z = std::acos(std::clamp(d.y, -1.0f, 1.0f));
	const float y = (std::abs(d.x) + std::abs(d.z) > kNearZeroLength) ? std::atan2(d.z, -d.x) : 0.0f;
	return { 0.0f, y, z };
}

void BossRig::SetArmTarget(Side side, float raise, float swing, float smoothTime) {
	Arm& a = arms_[static_cast<int>(side)];
	a.targetRaise = raise;
	a.targetSwing = swing;
	a.smoothTime = smoothTime;
	a.targetSet = true;
}

bool BossRig::IsArmAlive(Side side) const {
	const Arm& a = arms_[static_cast<int>(side)];
	return a.arm != nullptr && !a.broken;
}

Vector3 BossRig::GetFistPos(Side side) const {
	const Arm& a = arms_[static_cast<int>(side)];
	if (!a.arm || a.broken) return bodyPos_;
	const Vector3 sh = ShoulderLocal(side);
	const Vector3 d = ArmDirLocal(side, a.raise, a.swing);
	return ToWorldPoint({ sh.x + d.x * fistOffset_, sh.y + d.y * fistOffset_, sh.z + d.z * fistOffset_ });
}

bool BossRig::PopBrokenArm(Side& outSide, Vector3& outPos) {
	if (brokenQueue_.empty()) return false;
	outSide = brokenQueue_.front().side;
	outPos = brokenQueue_.front().pos;
	brokenQueue_.erase(brokenQueue_.begin());
	return true;
}

int BossRig::ForwardDamage(IImGuiEditable* part, int& lastHp, bool keepAlive) {
	HP& hp = Gameplay::Of(part).GetHP();
	const int dealt = lastHp - hp.currentHP;
	if (dealt > 0 && body_) {
		const int toBody = static_cast<int>(std::lround(static_cast<float>(dealt) * damageToBody_));
		if (toBody > 0) Gameplay::Of(body_).GetHP().TakeDamage(toBody);
	}
	if (keepAlive) hp.currentHP = hp.maxHP;
	lastHp = hp.currentHP;
	return hp.currentHP;
}

void BossRig::Update(float dt) {
	time_ += dt;
	UpdateDebris(dt);
	if (!body_) return;
	const Vector3* bp = body_->GetEditableTranslate();
	if (!bp) return;

	// ----- 胴の位置・向き・歩き -----
	bodyPos_ = *bp;
	if (const Vector3* r = body_->GetEditableRotate()) {
		bodyYaw_ += WrapAngle(r->y - bodyYaw_) * ExpBlend(dt, kBodyYawSmoothTime);
	}
	if (hasPrevBody_ && dt > kMinWalkDeltaTime) {
		const float dx = bodyPos_.x - prevBodyPos_.x;
		const float dz = bodyPos_.z - prevBodyPos_.z;
		const float dist = std::sqrt(dx * dx + dz * dz);
		const bool grounded = bodyPos_.y <= groundY_ + bodyHeight_ + kGroundedTolerance;
		const float target = grounded ? std::clamp(dist / dt / kFullWalkSpeed, 0.0f, 1.0f) : 0.0f;
		walkAmount_ += (target - walkAmount_) * ExpBlend(dt, kWalkAmountSmoothTime);
		walkPhase_ += dist / (std::max)(strideLength_, kMinStrideLength) * kTwoPi;
	}
	prevBodyPos_ = bodyPos_;
	hasPrevBody_ = true;

	// ----- 部位のダメージを本体へ・腕が壊れたか -----
	for (int i = 0; i < kSideCount; ++i) {
		Arm& a = arms_[i];
		if (a.arm && !a.broken && ForwardDamage(a.arm, a.lastHp, false) <= 0) BreakArm(static_cast<Side>(i));
		if (legs_[i].leg) ForwardDamage(legs_[i].leg, legs_[i].lastHp, true);
	}

	// ----- 腕 -----
	for (int i = 0; i < kSideCount; ++i) {
		const Side side = static_cast<Side>(i);
		Arm& a = arms_[i];
		if (!a.arm) continue;
		if (!a.targetSet) {
			a.targetRaise = idleRaise_ + idleSwayAmp_ * std::sin(time_ * kIdleSwaySpeed + static_cast<float>(i) * kIdleSwaySidePhase);
			a.targetSwing = 0.0f;
			a.smoothTime = kIdleArmSmoothTime;
		}
		const float k = ExpBlend(dt, a.smoothTime);
		a.raise += (a.targetRaise - a.raise) * k;
		a.swing += (a.targetSwing - a.swing) * k;
		a.targetSet = false;

		const Vector3 sh = ShoulderLocal(side);
		const Vector3 dl = ArmDirLocal(side, a.raise, a.swing);
		const Vector3 dirW = ToWorldDir(dl);
		const Vector3 rot = RotateYAxisTo(dirW);
		const float half = upperArmLength_ * 0.5f;
		if (Vector3* t = a.arm->GetEditableTranslate()) {
			*t = ToWorldPoint({ sh.x + dl.x * half, sh.y + dl.y * half, sh.z + dl.z * half });
		}
		ApplyRotate(a.arm, rot);
		if (a.fist) {
			if (Vector3* t = a.fist->GetEditableTranslate()) *t = GetFistPos(side);
			ApplyRotate(a.fist, rot);
		}
	}

	// ----- 脚（足踏み）-----
	for (int i = 0; i < kSideCount; ++i) {
		Leg& l = legs_[i];
		if (!l.leg) continue;
		const float s = SideSign(static_cast<Side>(i));
		const float w = s * walkSwingAmp_ * walkAmount_ * std::sin(walkPhase_);
		const Vector3 dl{ 0.0f, -std::cos(w), std::sin(w) };
		const float half = legLength_ * 0.5f;
		if (Vector3* t = l.leg->GetEditableTranslate()) {
			*t = ToWorldPoint({ s * hipX_ + dl.x * half, hipY_ + dl.y * half, dl.z * half });
		}
		ApplyRotate(l.leg, RotateYAxisTo(ToWorldDir(dl)));
	}
}

void BossRig::BreakArm(Side side) {
	Arm& a = arms_[static_cast<int>(side)];
	if (!a.arm || a.broken) return;
	const Vector3 fist = GetFistPos(side); // broken を立てる前に取る（立てると胴の中心が返る）
	a.broken = true;
	Vector3 armPos = fist;
	Vector3 armRot{};
	if (const Vector3* t = a.arm->GetEditableTranslate()) armPos = *t;
	if (const Vector3* r = a.arm->GetEditableRotate()) armRot = *r;

	if (auto* em = EffectManager::GetInstance()) em->Play(kBreakEffect, armPos);
	SoundManager::GetInstance()->Play2DSound("se_explode");

	// 外へ弾かれて落ちる破片（見た目だけ）に置き換える
	const Vector3 out = ToWorldDir({ SideSign(side), 0.0f, kBreakKickForward });
	const Vector3 kick{ out.x * kBreakKickHorizontalSpeed, kBreakKickUpSpeed, out.z * kBreakKickHorizontalSpeed };
	SpawnDebris(kArmDebrisPrefab, armPos, armRot, kick);
	SpawnDebris(kFistPrefab, fist, armRot, { kick.x * kFistKickHorizontalScale, kick.y * kFistKickUpScale, kick.z * kFistKickHorizontalScale });

	if (host_) {
		host_->DestroyRigPiece(a.arm);
		if (a.fist) host_->DestroyRigPiece(a.fist);
	}
	a.arm = nullptr;
	a.fist = nullptr;
	brokenQueue_.push_back({ side, fist });
}

void BossRig::BreakAll() {
	for (int i = 0; i < kSideCount; ++i) {
		if (arms_[i].arm && !arms_[i].broken) {
			BreakArm(static_cast<Side>(i));
		}
		Leg& l = legs_[i];
		if (l.leg) {
			Vector3 pos = bodyPos_, rot{};
			if (const Vector3* t = l.leg->GetEditableTranslate()) pos = *t;
			if (const Vector3* r = l.leg->GetEditableRotate()) rot = *r;
			SpawnDebris(kArmDebrisPrefab, pos, rot, { 0.0f, kLegDebrisUpSpeed, 0.0f });
			if (host_) host_->DestroyRigPiece(l.leg);
			l.leg = nullptr;
		}
	}
	// 撃破のときは素材のドロップ・ひるみは要らない（撃破の報酬は別）
	brokenQueue_.clear();
	body_ = nullptr;
}

void BossRig::SpawnDebris(const std::string& prefab, const Vector3& pos, const Vector3& rotate, const Vector3& kick) {
	if (!host_) return;
	IImGuiEditable* e = host_->SpawnRigPiece(prefab, pos);
	if (!e) return;
	ApplyRotate(e, rotate);
	Debris d;
	d.entity = e;
	d.velocity = kick;
	d.spin = kDebrisSpinSpeed;
	d.spinAngle = rotate.x;
	d.yaw = rotate.y;
	d.life = debrisLife_;
	debris_.push_back(d);
}

void BossRig::UpdateDebris(float dt) {
	if (dt <= 0.0f) return;
	for (Debris& d : debris_) {
		if (!d.entity) continue;
		d.life -= dt;
		Vector3* t = d.entity->GetEditableTranslate();
		if (!t) continue;
		d.velocity.y -= debrisGravity_ * dt;
		t->x += d.velocity.x * dt;
		t->y += d.velocity.y * dt;
		t->z += d.velocity.z * dt;
		// 足場の上で止まる（転がるように少しだけ滑る）
		const float floorY = groundY_ + kDebrisFloorOffset;
		if (t->y < floorY) {
			t->y = floorY;
			if (d.velocity.y < 0.0f) d.velocity.y = -d.velocity.y * kDebrisBounceRestitution;
			d.velocity.x *= kDebrisBounceFriction;
			d.velocity.z *= kDebrisBounceFriction;
			d.spin *= kDebrisBounceSpinDamping;
		}
		d.spinAngle += d.spin * dt;
		if (const Vector3* r = d.entity->GetEditableRotate()) {
			ApplyRotate(d.entity, { d.spinAngle, r->y, r->z });
		}
	}
	for (Debris& d : debris_) {
		if (d.entity && d.life <= 0.0f) {
			if (host_) host_->DestroyRigPiece(d.entity);
			d.entity = nullptr;
		}
	}
	debris_.erase(std::remove_if(debris_.begin(), debris_.end(), [](const Debris& d) { return d.entity == nullptr; }), debris_.end());
}

void BossRig::OnImGui() {
#ifdef _DEBUG
	for (int i = 0; i < kSideCount; ++i) {
		const Arm& a = arms_[i];
		const char* name = i == 0 ? "Left Arm" : "Right Arm";
		if (a.arm) {
			const HP& hp = Gameplay::Of(a.arm).GetHP();
			ImGui::Text("%s: %d / %d", name, hp.currentHP, hp.maxHP);
			ImGui::SameLine();
			ImGui::PushID(i);
			if (ImGui::SmallButton("Break")) Gameplay::Of(a.arm).GetHP().currentHP = 0;
			ImGui::PopID();
		} else {
			ImGui::Text("%s: %s", name, a.broken ? "broken" : "-");
		}
	}
	ImGui::DragFloat("Damage To Body", &damageToBody_, 0.05f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Shoulder X", &shoulderX_, 0.05f, 0.0f, 6.0f, "%.2f");
	ImGui::DragFloat("Shoulder Y", &shoulderY_, 0.05f, -3.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Fist Offset", &fistOffset_, 0.05f, 0.5f, 8.0f, "%.2f");
	ImGui::DragFloat("Arm Splay", &armSplay_, 0.01f, 0.0f, 1.5f, "%.2f");
	ImGui::DragFloat("Idle Raise", &idleRaise_, 0.01f, -1.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Hip X", &hipX_, 0.05f, 0.0f, 4.0f, "%.2f");
	ImGui::DragFloat("Hip Y", &hipY_, 0.05f, -4.0f, 0.0f, "%.2f");
	ImGui::DragFloat("Stride (m)", &strideLength_, 0.05f, 0.2f, 10.0f, "%.2f");
	ImGui::DragFloat("Walk Swing", &walkSwingAmp_, 0.01f, 0.0f, 1.5f, "%.2f");
	ImGui::Text("Walk amount %.2f", walkAmount_);
#endif
}
