#include "Enemy/Boss/Special/BossGravityRocks.h"
#include "Json/JsonValue.h"
#include "Log.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// 1 回の描画で描ける岩の数の上限（rockCount の上限も兼ねる）
	constexpr int kMaxRocks = 12;
	// 岩の数の下限
	constexpr int kMinRocks = 2;
	// 割り方の乱数の種（全部の岩で共通の割れ方）
	constexpr uint32_t kFractureRandomSeed = 7u;
	// 浮き上がり始める前に岩の頂上を地面より下げておく量 [m]
	constexpr float kBuriedMargin = 0.2f;
	// アリーナの縁から岩を離す量 [m]
	constexpr float kArenaEdgeMargin = 1.0f;
	// 破片が地面に当たる高さ（重心から、包む半径のこの割合だけ下を底とみなす）
	constexpr float kFragmentBottomRatio = 0.5f;
	// 地面に当たったときに回転を残す割合
	constexpr float kGroundSpinKeep = 0.6f;
	// これより遅くなった破片は地面で止める（縦の速さ [m/s]・横の速さの 2 乗）
	constexpr float kRestVerticalSpeed = 1.0f;
	constexpr float kRestHorizontalSpeedSq = 0.5f;
	// ばらつきの範囲（速さ・回転・消え始め）
	constexpr float kSpeedJitterMin = 0.5f;
	constexpr float kSweepJitterMin = 0.7f;
	constexpr float kSweepJitterMax = 1.2f;
	constexpr float kSweepUpJitterMin = 0.5f;
	constexpr float kShieldFadeJitterMin = 0.6f;
	constexpr float kShieldFadeJitterMax = 1.4f;
	// 浮き上がりで使う時間の最小 [s]（岩ごとのずれを引いた残り）
	constexpr float kMinRiseTime = 0.1f;
	// 長さ 0 とみなす距離 [m]
	constexpr float kNearZero = 1e-4f;

	inline Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline Vector3 Mul(const Vector3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }

	float EaseOutCubic(float t) {
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}

	// 調整値の float と JSON のキー・ImGui の名前の組
	struct FloatField {
		const char* key;
		const char* label;
		float BossGravityRocks::Tuning::* member;
		float speed;
	};
	constexpr FloatField kFloatFields[] = {
		{ "ringRadius", "円の半径", &BossGravityRocks::Tuning::ringRadius, 0.1f },
		{ "rockScale", "岩の大きさ", &BossGravityRocks::Tuning::rockScale, 0.05f },
		{ "rockGap", "岩どうしの隙間", &BossGravityRocks::Tuning::rockGap, 0.05f },
		{ "hoverHeight", "浮く高さ", &BossGravityRocks::Tuning::hoverHeight, 0.1f },
		{ "hoverHeightJitter", "浮く高さのばらつき", &BossGravityRocks::Tuning::hoverHeightJitter, 0.05f },
		{ "riseStagger", "浮き始めのずれ", &BossGravityRocks::Tuning::riseStagger, 0.01f },
		{ "bobAmplitude", "上下の揺れ", &BossGravityRocks::Tuning::bobAmplitude, 0.01f },
		{ "bobSpeed", "上下の揺れの速さ", &BossGravityRocks::Tuning::bobSpeed, 0.05f },
		{ "driftSpin", "浮いている間の回転", &BossGravityRocks::Tuning::driftSpin, 0.01f },
		{ "crumbleSpeed", "空中で崩れる速さ", &BossGravityRocks::Tuning::crumbleSpeed, 0.05f },
		{ "crumbleSpin", "空中で崩れる回転", &BossGravityRocks::Tuning::crumbleSpin, 0.05f },
		{ "antiGravity", "反重力の加速度", &BossGravityRocks::Tuning::antiGravity, 0.01f },
		{ "drag", "反重力中の減速", &BossGravityRocks::Tuning::drag, 0.01f },
		{ "gravity", "重力", &BossGravityRocks::Tuning::gravity, 0.1f },
		{ "bounce", "跳ね返り", &BossGravityRocks::Tuning::bounce, 0.01f },
		{ "friction", "地面の摩擦（残す割合）", &BossGravityRocks::Tuning::friction, 0.01f },
		{ "sweepSpeed", "閃光で飛ばす速さ", &BossGravityRocks::Tuning::sweepSpeed, 0.1f },
		{ "sweepUp", "閃光で飛ばす上向き", &BossGravityRocks::Tuning::sweepUp, 0.1f },
		{ "sweepFadeTime", "飛ばした破片が消える時間", &BossGravityRocks::Tuning::sweepFadeTime, 0.01f },
		{ "shieldCrumbleSpeed", "盾が崩れる速さ", &BossGravityRocks::Tuning::shieldCrumbleSpeed, 0.05f },
		{ "shieldFadeDelay", "盾が消え始めるまで", &BossGravityRocks::Tuning::shieldFadeDelay, 0.01f },
		{ "shieldFadeTime", "盾が消える時間", &BossGravityRocks::Tuning::shieldFadeTime, 0.01f },
		{ "judgeRadiusScale", "盾の判定の半径（比）", &BossGravityRocks::Tuning::judgeRadiusScale, 0.01f },
		{ "collideRadiusScale", "押し出しの半径（比）", &BossGravityRocks::Tuning::collideRadiusScale, 0.01f },
		{ "playerRadius", "プレイヤーの半径", &BossGravityRocks::Tuning::playerRadius, 0.01f },
		{ "playerHeight", "プレイヤーの高さ", &BossGravityRocks::Tuning::playerHeight, 0.01f },
	};

	// 選べる岩のモデル
	constexpr const char* kModelPaths[] = {
		"Resources/Models/Rocks/BossRock_A.glb",
		"Resources/Models/Rocks/BossRock_B.glb",
		"Resources/Models/Rocks/BossRock_C.glb",
	};
}

void BossGravityRocks::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager) {
	renderer_.Initialize(dxCore, srvManager, object3DManager, kMaxRocks);
	rendererReady_ = true;
	Rebuild();
}

void BossGravityRocks::Finalize() {
	rocks_.clear();
	if (rendererReady_) renderer_.Finalize();
	rendererReady_ = false;
}

void BossGravityRocks::Rebuild() {
	fragments_.clear();
	VoronoiFracture::SourceMesh mesh;
	if (!VoronoiFracture::LoadSourceMesh(tuning_.modelPath, mesh)) {
		Log("[BossGravityRocks] 岩のモデルを読めない: " + tuning_.modelPath + "\n");
		renderer_.SetMesh(VoronoiFracture::Result{});
		return;
	}
	// 置き方と判定に使うモデルの大きさ
	meshMinY_ = mesh.positions.front().y;
	meshMaxY_ = mesh.positions.front().y;
	meshHorizontal_ = 0.0f;
	for (const Vector3& p : mesh.positions) {
		meshMinY_ = (std::min)(meshMinY_, p.y);
		meshMaxY_ = (std::max)(meshMaxY_, p.y);
		meshHorizontal_ = (std::max)(meshHorizontal_, std::sqrt(p.x * p.x + p.z * p.z));
	}

	VoronoiFracture::Settings settings;
	settings.seedCount = static_cast<uint32_t>((std::max)(tuning_.fragmentSeeds, 2));
	settings.randomSeed = kFractureRandomSeed;
	const VoronoiFracture::Result result = VoronoiFracture::Build(mesh, settings);
	renderer_.SetMesh(result);
	fragments_ = result.fragments;
	Log("[BossGravityRocks] fragments=" + std::to_string(fragments_.size()) +
		" ms=" + std::to_string(result.buildMilliseconds) + "\n");
	// 割れ方が変わったので、置いてある岩は捨てる
	rocks_.clear();
}

Matrix4x4 BossGravityRocks::RockWorld(const Rock& rock) const {
	const float s = tuning_.rockScale;
	return MakeAffineMatrix(Transform{ { s, s, s }, { 0.0f, rock.yaw, 0.0f }, rock.position });
}

void BossGravityRocks::Place(const Vector3& center, float groundY, float maxRadius, std::mt19937& rng) {
	rocks_.clear();
	groundY_ = groundY;
	gravityOn_ = false;
	shieldLanded_ = false;
	rng_.seed(rng());
	if (fragments_.empty()) return;

	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const int count = std::clamp(tuning_.rockCount, kMinRocks, kMaxRocks);
	const float s = tuning_.rockScale;
	const float rockRadius = meshHorizontal_ * s;

	// アリーナに収まる半径へ縮める（岩どうしが重ならない最小の半径は下回らない）
	float radius = tuning_.ringRadius;
	if (maxRadius > 0.0f) radius = (std::min)(radius, maxRadius - rockRadius - kArenaEdgeMargin);
	const float step = kTwoPi / static_cast<float>(count);
	const float chord = 2.0f * rockRadius + tuning_.rockGap;
	const float minRadius = chord / (2.0f * std::sin(step * 0.5f));
	radius = (std::max)(radius, minRadius);

	// 扇形ごとに 1 個。隣と重ならない角度の差を残して、扇形の中だけで揺らす
	const float minAngle = 2.0f * std::asin((std::min)(chord / (2.0f * radius), 1.0f));
	const float jitter = (std::max)(step - minAngle, 0.0f);
	const float offset = unit(rng) * kTwoPi;
	const int shieldIndex = std::uniform_int_distribution<int>(0, count - 1)(rng);

	rocks_.resize(count);
	for (int i = 0; i < count; ++i) {
		Rock& rock = rocks_[i];
		const float angle = offset + step * static_cast<float>(i) + (unit(rng) - 0.5f) * jitter;
		rock.position = { center.x + std::cos(angle) * radius, 0.0f, center.z + std::sin(angle) * radius };
		rock.yaw = unit(rng) * kTwoPi;
		rock.spinSpeed = (unit(rng) * 2.0f - 1.0f) * tuning_.driftSpin;
		rock.restY = groundY - meshMinY_ * s;
		rock.buriedY = groundY - meshMaxY_ * s - kBuriedMargin;
		rock.hoverY = groundY + tuning_.hoverHeight - meshMinY_ * s + (unit(rng) * 2.0f - 1.0f) * tuning_.hoverHeightJitter;
		rock.riseDelay = unit(rng) * tuning_.riseStagger;
		rock.bobPhase = unit(rng) * kTwoPi;
		rock.isShield = (i == shieldIndex);
		rock.state = RockState::Buried;
		rock.position.y = rock.buriedY;
	}
}

void BossGravityRocks::BeginRise(float duration) {
	riseDuration_ = (std::max)(duration, kMinRiseTime);
	for (Rock& rock : rocks_) {
		if (rock.state != RockState::Buried) continue;
		rock.state = RockState::Rising;
		rock.riseTimer = 0.0f;
	}
}

void BossGravityRocks::BreakRock(Rock& rock, float speed, float spin) {
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	rock.broken = true;
	rock.brokenWorld = RockWorld(rock);
	rock.brokenWorldInverse = Inverse(rock.brokenWorld);
	rock.fragments.assign(fragments_.size(), FragmentState{});
	rock.poses.assign(fragments_.size(), FracturedMeshRenderer::FragmentPose{});
	for (size_t f = 0; f < fragments_.size(); ++f) {
		FragmentState& fs = rock.fragments[f];
		fs.position = TransformCoordinate(fragments_[f].center, rock.brokenWorld);
		Vector3 dir = Sub(fs.position, rock.position);
		const float len = Length(dir);
		dir = len > kNearZero ? Mul(dir, 1.0f / len) : Vector3{ 0.0f, 1.0f, 0.0f };
		fs.velocity = Mul(dir, speed * (kSpeedJitterMin + (1.0f - kSpeedJitterMin) * unit(rng_)));
		const Vector3 axis{ unit(rng_) * 2.0f - 1.0f, unit(rng_) * 2.0f - 1.0f, unit(rng_) * 2.0f - 1.0f };
		fs.spinAxis = Length(axis) > kNearZero ? Normalize(axis) : Vector3{ 0.0f, 1.0f, 0.0f };
		fs.spinSpeed = spin * (kSpeedJitterMin + (1.0f - kSpeedJitterMin) * unit(rng_));
	}
}

void BossGravityRocks::Crumble() {
	for (Rock& rock : rocks_) {
		if (rock.isShield || rock.broken) continue;
		BreakRock(rock, tuning_.crumbleSpeed, tuning_.crumbleSpin);
	}
}

void BossGravityRocks::Drop() {
	gravityOn_ = true;
	for (Rock& rock : rocks_) {
		if (rock.broken) continue;
		if (rock.state == RockState::Rising || rock.state == RockState::Hovering || rock.state == RockState::Buried) {
			rock.state = RockState::Falling;
			rock.fallSpeed = 0.0f;
		}
	}
}

void BossGravityRocks::Sweep(const Vector3& origin) {
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (Rock& rock : rocks_) {
		if (rock.isShield) continue;
		if (!rock.broken) BreakRock(rock, 0.0f, tuning_.crumbleSpin);
		for (FragmentState& fs : rock.fragments) {
			Vector3 away{ fs.position.x - origin.x, 0.0f, fs.position.z - origin.z };
			const float len = Length(away);
			away = len > kNearZero ? Mul(away, 1.0f / len) : Vector3{ 1.0f, 0.0f, 0.0f };
			const float speed = tuning_.sweepSpeed * (kSweepJitterMin + (kSweepJitterMax - kSweepJitterMin) * unit(rng_));
			fs.velocity = Mul(away, speed);
			fs.velocity.y = tuning_.sweepUp * (kSweepUpJitterMin + (1.0f - kSweepUpJitterMin) * unit(rng_));
			fs.resting = false;
			fs.fadeDelay = 0.0f;
			fs.fadeSpeed = 1.0f / (std::max)(tuning_.sweepFadeTime, kNearZero);
		}
	}
}

void BossGravityRocks::CrumbleShield() {
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	for (Rock& rock : rocks_) {
		if (!rock.isShield || rock.broken) continue;
		BreakRock(rock, tuning_.shieldCrumbleSpeed, tuning_.crumbleSpin);
		for (FragmentState& fs : rock.fragments) {
			fs.fadeDelay = tuning_.shieldFadeDelay * (kShieldFadeJitterMin + (kShieldFadeJitterMax - kShieldFadeJitterMin) * unit(rng_));
			fs.fadeSpeed = 1.0f / (std::max)(tuning_.shieldFadeTime, kNearZero);
		}
	}
}

void BossGravityRocks::Clear() {
	rocks_.clear();
	gravityOn_ = false;
	shieldLanded_ = false;
}

void BossGravityRocks::UpdateRock(Rock& rock, float dt) {
	switch (rock.state) {
	case RockState::Buried:
	case RockState::Grounded:
		break;
	case RockState::Rising: {
		rock.riseTimer += dt;
		const float span = (std::max)(riseDuration_ - tuning_.riseStagger, kMinRiseTime);
		const float t = std::clamp((rock.riseTimer - rock.riseDelay) / span, 0.0f, 1.0f);
		const float e = EaseOutCubic(t);
		rock.position.y = rock.buriedY + (rock.hoverY - rock.buriedY) * e;
		rock.yaw += rock.spinSpeed * dt * e;
		if (t >= 1.0f) rock.state = RockState::Hovering;
		break;
	}
	case RockState::Hovering:
		rock.bobPhase += tuning_.bobSpeed * dt;
		rock.position.y = rock.hoverY + std::sin(rock.bobPhase) * tuning_.bobAmplitude;
		rock.yaw += rock.spinSpeed * dt;
		break;
	case RockState::Falling:
		rock.fallSpeed += tuning_.gravity * dt;
		rock.position.y -= rock.fallSpeed * dt;
		if (rock.position.y <= rock.restY) {
			rock.position.y = rock.restY;
			rock.state = RockState::Grounded;
			if (rock.isShield) shieldLanded_ = true;
		}
		break;
	}
}

void BossGravityRocks::UpdateFragments(Rock& rock, float dt) {
	const float s = tuning_.rockScale;
	for (size_t f = 0; f < rock.fragments.size(); ++f) {
		FragmentState& fs = rock.fragments[f];
		FracturedMeshRenderer::FragmentPose& pose = rock.poses[f];
		if (fs.fadeDelay >= 0.0f) {
			if (fs.fadeDelay > 0.0f) {
				fs.fadeDelay = (std::max)(fs.fadeDelay - dt, 0.0f);
			} else {
				pose.visibility = (std::max)(pose.visibility - fs.fadeSpeed * dt, 0.0f);
			}
		}
		if (fs.resting) continue;

		if (gravityOn_) {
			fs.velocity.y -= tuning_.gravity * dt;
		} else {
			// 重力が戻るまでは反重力でゆっくり浮き、少しずつ止まる
			fs.velocity.y += tuning_.antiGravity * dt;
			fs.velocity = Mul(fs.velocity, (std::max)(1.0f - tuning_.drag * dt, 0.0f));
		}
		fs.position = Add(fs.position, Mul(fs.velocity, dt));
		fs.angle += fs.spinSpeed * dt;

		if (!gravityOn_) continue;
		const float bottom = fs.position.y - fragments_[f].radius * s * kFragmentBottomRatio;
		if (bottom < groundY_) {
			fs.position.y += groundY_ - bottom;
			if (fs.velocity.y < 0.0f) fs.velocity.y = -fs.velocity.y * tuning_.bounce;
			fs.velocity.x *= tuning_.friction;
			fs.velocity.z *= tuning_.friction;
			fs.spinSpeed *= kGroundSpinKeep;
			const float horizontalSq = fs.velocity.x * fs.velocity.x + fs.velocity.z * fs.velocity.z;
			if (std::fabs(fs.velocity.y) < kRestVerticalSpeed && horizontalSq < kRestHorizontalSpeedSq) {
				fs.velocity = { 0.0f, 0.0f, 0.0f };
				fs.resting = true;
			}
		}
	}
}

void BossGravityRocks::WritePoses(Rock& rock) {
	for (size_t f = 0; f < rock.fragments.size(); ++f) {
		const FragmentState& fs = rock.fragments[f];
		FracturedMeshRenderer::FragmentPose& pose = rock.poses[f];
		const Vector3 local = TransformCoordinate(fs.position, rock.brokenWorldInverse);
		pose.offset = Sub(local, fragments_[f].center);
		pose.rotation = MakeRotateAxisAngleQuaternion(fs.spinAxis, fs.angle);
	}
}

void BossGravityRocks::Update(float dt) {
	for (Rock& rock : rocks_) {
		if (rock.broken) {
			UpdateFragments(rock, dt);
			WritePoses(rock);
		} else {
			UpdateRock(rock, dt);
		}
	}
	// 全部の破片が消えた岩は捨てる
	rocks_.erase(std::remove_if(rocks_.begin(), rocks_.end(), [](const Rock& rock) {
		if (!rock.broken) return false;
		return std::all_of(rock.poses.begin(), rock.poses.end(),
			[](const FracturedMeshRenderer::FragmentPose& p) { return p.visibility <= 0.0f; });
	}), rocks_.end());
}

void BossGravityRocks::Draw(const Camera& camera) {
	if (!rendererReady_ || rocks_.empty()) return;
	drawList_.clear();
	for (const Rock& rock : rocks_) {
		if (rock.state == RockState::Buried && !rock.broken) continue;
		FracturedMeshRenderer::RockDraw draw;
		draw.world = rock.broken ? rock.brokenWorld : RockWorld(rock);
		draw.textureScale = tuning_.rockScale;
		draw.broken = rock.broken;
		draw.fragments = rock.broken ? &rock.poses : nullptr;
		drawList_.push_back(draw);
	}
	renderer_.Draw(camera, drawList_);
}

bool BossGravityRocks::ConsumeShieldLanded() {
	const bool landed = shieldLanded_;
	shieldLanded_ = false;
	return landed;
}

bool BossGravityRocks::GetShieldPosition(Vector3& out) const {
	for (const Rock& rock : rocks_) {
		if (rock.isShield) {
			out = rock.position;
			return true;
		}
	}
	return false;
}

bool BossGravityRocks::IsShielded(const Vector3& blastOrigin, const Vector3& playerPos) const {
	const Rock* shield = nullptr;
	for (const Rock& rock : rocks_) {
		if (rock.isShield && !rock.broken) shield = &rock;
	}
	if (!shield) return false;

	// XZ 平面で、着弾地点から盾へ向かう方向を軸にした扇形（盾の影）にプレイヤーの体が全部入っているか
	const float toShieldX = shield->position.x - blastOrigin.x;
	const float toShieldZ = shield->position.z - blastOrigin.z;
	const float shieldDistance = std::sqrt(toShieldX * toShieldX + toShieldZ * toShieldZ);
	if (shieldDistance < kNearZero) return false;
	const float dirX = toShieldX / shieldDistance;
	const float dirZ = toShieldZ / shieldDistance;

	const float toPlayerX = playerPos.x - blastOrigin.x;
	const float toPlayerZ = playerPos.z - blastOrigin.z;
	const float playerDistance = std::sqrt(toPlayerX * toPlayerX + toPlayerZ * toPlayerZ);
	// 盾より手前（着弾地点の側）にいたら守られない
	if (toPlayerX * dirX + toPlayerZ * dirZ < shieldDistance) return false;

	const float shieldRadius = meshHorizontal_ * tuning_.rockScale * tuning_.judgeRadiusScale;
	if (shieldRadius >= shieldDistance) return true;  // 着弾地点が盾に埋まっている
	const float shieldHalfAngle = std::asin(shieldRadius / shieldDistance);
	const float cosAngle = std::clamp((toPlayerX * dirX + toPlayerZ * dirZ) / (std::max)(playerDistance, kNearZero), -1.0f, 1.0f);
	const float playerAngle = std::acos(cosAngle);
	const float playerHalfAngle = std::asin((std::min)(tuning_.playerRadius / (std::max)(playerDistance, kNearZero), 1.0f));
	return playerAngle + playerHalfAngle <= shieldHalfAngle;
}

bool BossGravityRocks::ResolvePlayerCollision(Vector3& playerPos) const {
	const float s = tuning_.rockScale;
	bool pushed = false;
	for (const Rock& rock : rocks_) {
		if (rock.broken || rock.state == RockState::Buried) continue;
		// 縦に重なっていなければ押さない（浮いている岩の下はくぐれる）
		const float bottom = rock.position.y + meshMinY_ * s;
		const float top = rock.position.y + meshMaxY_ * s;
		if (playerPos.y + tuning_.playerHeight < bottom || playerPos.y > top) continue;

		const float dx = playerPos.x - rock.position.x;
		const float dz = playerPos.z - rock.position.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		const float minDistance = meshHorizontal_ * s * tuning_.collideRadiusScale + tuning_.playerRadius;
		if (distance >= minDistance) continue;
		const float nx = distance > kNearZero ? dx / distance : 1.0f;
		const float nz = distance > kNearZero ? dz / distance : 0.0f;
		playerPos.x = rock.position.x + nx * minDistance;
		playerPos.z = rock.position.z + nz * minDistance;
		pushed = true;
	}
	return pushed;
}

void BossGravityRocks::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	tuning_.modelPath = node["modelPath"].AsString(tuning_.modelPath);
	tuning_.fragmentSeeds = static_cast<int>(node["fragmentSeeds"].AsInt(tuning_.fragmentSeeds));
	tuning_.rockCount = static_cast<int>(node["rockCount"].AsInt(tuning_.rockCount));
	for (const FloatField& field : kFloatFields) {
		tuning_.*field.member = static_cast<float>(node[field.key].AsDouble(tuning_.*field.member));
	}
}

void BossGravityRocks::SaveTuning(JsonValue& node) const {
	node["modelPath"] = tuning_.modelPath;
	node["fragmentSeeds"] = static_cast<int64_t>(tuning_.fragmentSeeds);
	node["rockCount"] = static_cast<int64_t>(tuning_.rockCount);
	for (const FloatField& field : kFloatFields) {
		node[field.key] = static_cast<double>(tuning_.*field.member);
	}
}

void BossGravityRocks::OnImGui(bool& changed) {
#ifdef USE_IMGUI
	bool rebuild = false;
	int modelIndex = 0;
	for (int i = 0; i < static_cast<int>(std::size(kModelPaths)); ++i) {
		if (tuning_.modelPath == kModelPaths[i]) modelIndex = i;
	}
	if (ImGui::Combo("岩のモデル", &modelIndex, "BossRock_A\0BossRock_B\0BossRock_C\0")) {
		tuning_.modelPath = kModelPaths[modelIndex];
		rebuild = true;
	}
	rebuild |= ImGui::SliderInt("破片の数", &tuning_.fragmentSeeds, 2, 48);
	changed |= ImGui::SliderInt("岩の数", &tuning_.rockCount, kMinRocks, kMaxRocks);
	for (const FloatField& field : kFloatFields) {
		changed |= ImGui::DragFloat(field.label, &(tuning_.*field.member), field.speed, 0.0f, 1000.0f, "%.2f");
	}
	if (rebuild) {
		Rebuild();
		changed = true;
	}
	ImGui::Text("破片 %zu 個 / 岩の横幅の半径 %.2f m", fragments_.size(), meshHorizontal_ * tuning_.rockScale);
	if (ImGui::TreeNode("岩の見た目")) {
		renderer_.OnImGui();
		ImGui::TreePop();
	}
#else
	(void)changed;
#endif
}
