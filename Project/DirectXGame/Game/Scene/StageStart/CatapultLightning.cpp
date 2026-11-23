#include "CatapultLightning.h"

#include "Camera.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/MeshData.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 長さがこれ以下のベクトルは向きを決めない
	constexpr float kMinNormalizeLength = 1e-6f;
	// 雷の 1 点あたりの頂点の数（左の縁・芯・右の縁）
	constexpr int kVertsPerPoint = 3;
	// 時間の長さの下限 [秒]（0 で割らないように）
	constexpr float kMinFadeSeconds = 1e-3f;
	// 雷を溝の底からわずかに浮かせる高さ [m]
	constexpr float kBottomLift = 0.03f;
	// 点け直しの雷を射出口の奥まで出し続ける余裕 [m]
	constexpr float kSweepEndMargin = 2.0f;
	// 左右のレール（固定台）の数
	constexpr int kRailCount = 2;
	// 点け直しの雷の始まりを溝の幅のどこまで散らすか（半幅に対する割合）
	constexpr float kSweepStartSpread = 0.8f;
	// 点け直しの雷の終わりの高さの最小 [m]
	constexpr float kSweepRiseMin = 0.1f;
	// 点け直しの雷の終わりの高さの最大 [m]
	constexpr float kSweepRiseMax = 0.9f;
	// 点け直しの雷が前へ伸びる長さの最大 [m]
	constexpr float kSweepReachMax = 3.5f;
	// 点け直しの雷の寿命の最小 [秒]
	constexpr float kSweepLifeMin = 0.05f;
	// 点け直しの雷の寿命の最大 [秒]
	constexpr float kSweepLifeMax = 0.12f;
	// 機体の近くに寄せる強さの下限
	constexpr float kMinIdleNearBias = 0.1f;
	// 左右どちらへ跳ねるかの確率
	constexpr float kSideChance = 0.5f;
	// 跳ね上がる大きい弧になる確率
	constexpr float kBigArcChance = 0.5f;
	// 放電の始まりを溝の幅のどこまで散らすか（半幅に対する割合）
	constexpr float kIdleStartSpread = 0.6f;
	// 大きい弧が溝の縁から外へ出る距離の最小 [m]
	constexpr float kBigArcOutMin = 0.2f;
	// 大きい弧が溝の縁から外へ出る距離の最大 [m]
	constexpr float kBigArcOutMax = 1.5f;
	// 大きい弧の高さの最小 [m]
	constexpr float kBigArcHeightMin = 0.2f;
	// 大きい弧の高さの倍率の基本
	constexpr float kBigArcHeightBase = 0.6f;
	// 大きい弧の高さの倍率に出力に応じて足す量
	constexpr float kBigArcHeightThrust = 0.4f;
	// 大きい弧の高さの倍率にさらに足す一定の量
	constexpr float kBigArcHeightExtra = 0.4f;
	// 大きい弧の前後のばらつき（±）[m]
	constexpr float kBigArcSpreadZ = 2.0f;
	// 小さい弧の終わりを溝の幅のどこに置くか（半幅に対する割合）
	constexpr float kSmallArcReach = 0.9f;
	// 小さい弧の終わりの高さの最大 [m]
	constexpr float kSmallArcRise = 0.3f;
	// 放電の寿命の最小 [秒]
	constexpr float kIdleLifeMin = 0.05f;
	// 放電の寿命の最大 [秒]
	constexpr float kIdleLifeMax = 0.14f;
	// 出力に応じて放電を太くする割合
	constexpr float kIdleWidthThrust = 0.6f;
	// 固定台の雷の上の端の高さの最大 [m]
	constexpr float kClampTopMax = 0.4f;
	// 固定台の雷の上の端の前後のばらつき（±）[m]
	constexpr float kClampSpreadZ = 1.2f;
	// 固定台の雷の下の端が後ろへ伸びる長さの最大 [m]
	constexpr float kClampReachBack = 3.0f;
	// 固定台の雷の下の端が前へ伸びる長さの最大 [m]
	constexpr float kClampReachFront = 0.5f;
	// 固定台の雷の寿命の最小 [秒]
	constexpr float kClampLifeMin = 0.05f;
	// 固定台の雷の寿命の最大 [秒]
	constexpr float kClampLifeMax = 0.1f;
	// この長さより短い区間にはレールを走る雷を出さない [m]
	constexpr float kMinRunRange = 0.5f;
	// レールを走る雷の左右のばらつき（±）[m]
	constexpr float kRunSideSpread = 0.35f;
	// レールを走る雷の始まりの高さの最大 [m]
	constexpr float kRunStartRise = 0.1f;
	// レールを走る雷の終わりの高さの最大 [m]
	constexpr float kRunEndRise = 0.4f;
	// レールを走る雷の寿命の最小 [秒]
	constexpr float kRunLifeMin = 0.06f;
	// レールを走る雷の寿命の最大 [秒]
	constexpr float kRunLifeMax = 0.16f;
	// 残る放電の両端を溝の幅のどこに置くか（半幅に対する割合）
	constexpr float kTrailSpan = 0.9f;
	// 残る放電の高さの最大 [m]
	constexpr float kTrailRise = 0.3f;
	// 残る放電の前後のばらつき（±）[m]
	constexpr float kTrailSpreadZ = 1.5f;
	// 残る放電の寿命の最小 [秒]
	constexpr float kTrailLifeMin = 0.1f;
	// 残る放電の寿命の最大 [秒]
	constexpr float kTrailLifeMax = 0.25f;
	// 残る放電・溜まる雷を太くする倍率
	constexpr float kWideBoltScale = 1.5f;
	// 射出中にレールを走る雷の明るさ
	constexpr float kLaunchRunBrightness = 1.6f;
	// 向きの Y がこれ以上なら上の向きの代わりに X 軸を使う
	constexpr float kNearVerticalCos = 0.9f;

	constexpr const char* kWhiteTexture = "Resources/Textures/white1x1.dds";
	constexpr int kPointsPerBolt = CatapultLightning::kSegments + 1;
	constexpr int kVertsPerBolt = kPointsPerBolt * 3;   // 帯の左の縁・芯・右の縁

	/// <summary>2 つのベクトルを足す</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a + b</returns>
	Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	/// <summary>ベクトルを引く</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a − b</returns>
	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	/// <summary>ベクトルを s 倍する</summary>
	/// <param name="v">ベクトル</param>
	/// <param name="s">倍率</param>
	/// <returns>v × s</returns>
	Vector3 Mul(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
	/// <summary>外積を求める</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a × b</returns>
	Vector3 Cross(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	/// <summary>ベクトルの長さを求める</summary>
	/// <param name="v">ベクトル</param>
	/// <returns>長さ</returns>
	float Len(const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
	/// <summary>ベクトルを正規化する</summary>
	/// <param name="v">正規化するベクトル</param>
	/// <param name="fallback">長さがほぼ 0 のときに返す値</param>
	/// <returns>長さ 1 のベクトル。長さがほぼ 0 なら fallback</returns>
	Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback) {
		const float l = Len(v);
		return l > kMinNormalizeLength ? Mul(v, 1.0f / l) : fallback;
	}
}

CatapultLightning::CatapultLightning() = default;
CatapultLightning::~CatapultLightning() = default;

void CatapultLightning::Initialize(const StageStartGeometry& geometry) {
	geometry_ = geometry;
	bolts_.assign(kMaxBolts, Bolt{});

	MeshData data;
	data.vertices.resize(static_cast<size_t>(kMaxBolts) * kVertsPerBolt);
	for (auto& v : data.vertices) v.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (uint32_t b = 0; b < static_cast<uint32_t>(kMaxBolts); ++b) {
		const uint32_t base = b * kVertsPerBolt;
		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(kPointsPerBolt); ++i) {
			const uint32_t p = base + i * kVertsPerPoint;
			const uint32_t q = p + kVertsPerPoint;
			// 左の縁〜芯、芯〜右の縁の 2 枚
			data.indices.insert(data.indices.end(), { p, q, p + 1, p + 1, q, q + 1 });
			data.indices.insert(data.indices.end(), { p + 1, q + 1, p + 2, p + 2, q + 1, q + 2 });
		}
	}
	mesh_ = std::make_unique<PrimitiveMesh>();
	mesh_->Initialize(data);
	mesh_->SetTexture(kWhiteTexture);
	mesh_->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
	mesh_->SetDepthWrite(false);
	mesh_->SetCullBackface(false);
	vertices_ = data.vertices;
	Reset();
}

void CatapultLightning::Reset() {
	for (Bolt& bolt : bolts_) bolt.alive = false;
	sweepCarry_ = idleCarry_ = clampCarry_ = trailCarry_ = runCarry_ = 0.0f;
	launching_ = false;
	launchTime_ = 0.0f;
}

float CatapultLightning::Rand(float lo, float hi) {
	std::uniform_real_distribution<float> d(lo, hi);
	return d(random_);
}

void CatapultLightning::Jitter(Bolt& bolt) {
	const float amp = (std::min)(Len(Sub(bolt.b, bolt.a)) * jag_, jagMax_);
	for (int i = 0; i < kPointsPerBolt; ++i) {
		// 両端は動かさない（真ん中ほど大きくずらす）
		const float u = static_cast<float>(i) / static_cast<float>(kSegments);
		const float envelope = std::sin(u * kPi);
		bolt.offsets[i][0] = Rand(-1.0f, 1.0f) * amp * envelope;
		bolt.offsets[i][1] = Rand(-1.0f, 1.0f) * amp * envelope;
	}
}

void CatapultLightning::Spawn(const Vector3& a, const Vector3& b, float life, float width, float brightness, bool followClamp,
	float travel) {
	for (Bolt& bolt : bolts_) {
		if (bolt.alive) continue;
		bolt.alive = true;
		bolt.a = a;
		bolt.b = b;
		bolt.age = 0.0f;
		bolt.life = life;
		bolt.width = width;
		bolt.brightness = brightness;
		bolt.followClamp = followClamp;
		bolt.spawnTravel = travel;
		bolt.jitterTimer = 0.0f;
		Jitter(bolt);
		return;
	}
}

float CatapultLightning::LaunchLevelAt(float z) const {
	if (!launching_) return 1.0f;
	const float start = geometry_.liftGrooveStartZ;
	const float length = (std::max)(geometry_.catapultTunnelEndZ - start, 1.0f);
	const int b = std::clamp(static_cast<int>((z - start) / length * kPassBuckets), 0, kPassBuckets - 1);
	if (passTime_[b] < 0.0f) return 1.0f;
	return std::exp(-(launchTime_ - passTime_[b]) / (std::max)(afterFadeSeconds_, kMinFadeSeconds));
}

bool CatapultLightning::CullByLaunch(float z, float& brightness) {
	const float level = LaunchLevelAt(z);
	if (level < 1.0f && Rand(0.0f, 1.0f) > level) return true;
	brightness *= std::sqrt(level);
	return false;
}

void CatapultLightning::SpawnSweep(float frontZ, float count) {
	// 点け直しの先頭のすぐ後ろで、溝の底から前へ這うように伸びる雷
	const float bottom = -geometry_.grooveDepth + kBottomLift;
	const float gw = geometry_.grooveHalfWidth;
	if (frontZ < geometry_.liftGrooveStartZ || frontZ > geometry_.catapultTunnelEndZ + kSweepEndMargin) return;
	for (int n = 0; n < static_cast<int>(count); ++n) {
		const float x = geometry_.railX[n % kRailCount];
		const float z0 = frontZ - Rand(0.0f, sweepTail_);
		Spawn({ x + Rand(-gw, gw) * kSweepStartSpread, bottom, z0 }, { x + Rand(-gw, gw), bottom + Rand(kSweepRiseMin, kSweepRiseMax), z0 + Rand(1.0f, kSweepReachMax) },
			Rand(kSweepLifeMin, kSweepLifeMax), sweepWidth_, 1.0f, false, 0.0f);
	}
}

void CatapultLightning::SpawnIdle(float thrust, float count) {
	// 溝のあちこちで細かく放電（機体の近くほど多い）。溝を横切る短い雷
	const float bottom = -geometry_.grooveDepth + kBottomLift;
	const float gw = geometry_.grooveHalfWidth;
	const float start = geometry_.liftGrooveStartZ;
	const float range = geometry_.catapultTunnelEndZ - start;
	for (int n = 0; n < static_cast<int>(count); ++n) {
		const float x = geometry_.railX[n % kRailCount];
		const float z = start + range * std::pow(Rand(0.0f, 1.0f), (std::max)(idleNearBias_, kMinIdleNearBias));
		const float side = Rand(0.0f, 1.0f) < kSideChance ? -1.0f : 1.0f;
		// 溝の底から溝の縁の外・上へ跳ね上がる弧（半分は溝を横切るだけの小さいもの）
		const bool big = Rand(0.0f, 1.0f) < kBigArcChance;
		const Vector3 a = { x + Rand(-gw, gw) * kIdleStartSpread, bottom, z };
		const Vector3 b = big
			? Vector3{ x + side * (gw + Rand(kBigArcOutMin, kBigArcOutMax)), Rand(kBigArcHeightMin, idleArcHeight_) * (kBigArcHeightBase + kBigArcHeightThrust * thrust + kBigArcHeightExtra), z + Rand(-kBigArcSpreadZ, kBigArcSpreadZ) }
			: Vector3{ x + side * gw * kSmallArcReach, bottom + Rand(0.0f, kSmallArcRise), z + Rand(-1.0f, 1.0f) };
		float brightness = 1.0f + thrust;
		if (CullByLaunch(z, brightness)) continue;
		Spawn(a, b, Rand(kIdleLifeMin, kIdleLifeMax), idleWidth_ * (1.0f + kIdleWidthThrust * thrust), brightness, false, 0.0f);
	}
}

void CatapultLightning::SpawnClamp(float clampTravel, float count, float width, float brightness) {
	// 固定台（溝にはまる台車）の底とレールのあいだ
	const float bottom = -geometry_.grooveDepth + kBottomLift;
	const float gw = geometry_.grooveHalfWidth;
	for (int n = 0; n < static_cast<int>(count); ++n) {
		const Vector3& clamp = geometry_.clamps[n % kRailCount];
		const float z = clamp.z + clampTravel;
		Spawn({ clamp.x + Rand(-1.0f, 1.0f), Rand(0.0f, kClampTopMax), z + Rand(-kClampSpreadZ, kClampSpreadZ) },
			{ clamp.x + Rand(-gw, gw), bottom, z + Rand(-kClampReachBack, kClampReachFront) },
			Rand(kClampLifeMin, kClampLifeMax), width, brightness, true, clampTravel);
	}
}

void CatapultLightning::SpawnRailRun(float zMin, float zMax, float count, float brightness) {
	// 溝の底のレールに沿って縦に走る長い雷（レール全体に電気が走っている感じ）
	const float bottom = -geometry_.grooveDepth + kBottomLift;
	if (zMax - zMin < kMinRunRange) return;
	for (int n = 0; n < static_cast<int>(count); ++n) {
		const float x = geometry_.railX[n % kRailCount];
		const float length = Rand(runLengthMin_, runLengthMax_);
		const float z0 = Rand(zMin, (std::max)(zMax - length * 0.5f, zMin));
		const float z1 = (std::min)(z0 + length, zMax);
		float b = brightness;
		if (CullByLaunch((z0 + z1) * 0.5f, b)) continue;
		Spawn({ x + Rand(-kRunSideSpread, kRunSideSpread), bottom + Rand(0.0f, kRunStartRise), z0 }, { x + Rand(-kRunSideSpread, kRunSideSpread), bottom + Rand(0.0f, kRunEndRise), z1 },
			Rand(kRunLifeMin, kRunLifeMax), runWidth_, b, false, 0.0f);
	}
}

void CatapultLightning::SpawnTrail(float clampTravel, float count) {
	// 射出中、走り抜けた固定台の後ろの溝に残る放電
	const float bottom = -geometry_.grooveDepth + kBottomLift;
	const float gw = geometry_.grooveHalfWidth;
	for (int n = 0; n < static_cast<int>(count); ++n) {
		const Vector3& clamp = geometry_.clamps[n % kRailCount];
		const float z = clamp.z + clampTravel - Rand(0.0f, trailLength_);
		if (z < geometry_.liftGrooveStartZ) continue;
		Spawn({ clamp.x - gw * kTrailSpan, bottom + Rand(0.0f, kTrailRise), z }, { clamp.x + gw * kTrailSpan, bottom + Rand(0.0f, kTrailRise), z + Rand(-kTrailSpreadZ, kTrailSpreadZ) },
			Rand(kTrailLifeMin, kTrailLifeMax), idleWidth_ * kWideBoltScale, 1.0f, false, 0.0f);
	}
}

void CatapultLightning::Update(float deltaTime, const Drive& drive) {
	clampTravel_ = drive.clampTravel;
	for (Bolt& bolt : bolts_) {
		if (!bolt.alive) continue;
		bolt.age += deltaTime;
		if (bolt.age >= bolt.life) {
			bolt.alive = false;
			continue;
		}
		bolt.jitterTimer += deltaTime;
		if (bolt.jitterTimer >= jitterSeconds_) {
			bolt.jitterTimer = 0.0f;
			Jitter(bolt);
		}
	}
	auto take = [](float& carry, float rate, float dt) {
		carry += rate * dt;
		const float count = std::floor(carry);
		carry -= count;
		return count;
	};
	const float grooveStart = geometry_.liftGrooveStartZ;
	const float grooveEnd = geometry_.catapultTunnelEndZ;
	const float grooveLength = (std::max)(grooveEnd - grooveStart, 1.0f);
	if (drive.sweeping) {
		SpawnSweep(drive.sweepFrontZ, take(sweepCarry_, sweepRate_, deltaTime));
		// 点いた所（溝の始まり〜先頭）全体に縦に走る雷
		const float lit = std::clamp(drive.sweepFrontZ, grooveStart, grooveEnd);
		SpawnRailRun(grooveStart, lit, take(runCarry_, runRate_ * (lit - grooveStart) / grooveLength, deltaTime), 1.0f);
	}
	if (drive.charged && !drive.launching) {
		SpawnRailRun(grooveStart, grooveEnd, take(runCarry_, runRate_ * (1.0f + runThrustBoost_ * drive.thrust), deltaTime),
			1.0f + drive.thrust);
		SpawnIdle(drive.thrust, take(idleCarry_, idleRate_ * (1.0f + idleThrustBoost_ * drive.thrust), deltaTime));
		// SPACE で出力を上げるほど固定台の足元にも溜まる
		SpawnClamp(drive.clampTravel, take(clampCarry_, chargeClampRate_ * drive.thrust, deltaTime), idleWidth_ * kWideBoltScale, 1.0f);
	}
	if (drive.launching) {
		if (!launching_) {
			launching_ = true;
			launchTime_ = 0.0f;
			for (float& t : passTime_) t = -1.0f;
		}
		launchTime_ += deltaTime;
		// 固定台（2 台は同じ z）が通った区切りに時刻を付ける
		const float clampZ = geometry_.clamps[0].z + drive.clampTravel;
		for (int b = 0; b < kPassBuckets; ++b) {
			const float z = grooveStart + grooveLength * (static_cast<float>(b) + 0.5f) / kPassBuckets;
			if (passTime_[b] < 0.0f && z <= clampZ) passTime_[b] = launchTime_;
		}
		if (drive.clampRunning) {
			SpawnClamp(drive.clampTravel, take(clampCarry_, launchClampRate_, deltaTime), launchWidth_, launchBrightness_);
			SpawnTrail(drive.clampTravel, take(trailCarry_, trailRate_, deltaTime));
		}
		// 溝の全長も走り続け、機体が通り過ぎた所から収まっていく（CullByLaunch で間引く）
		SpawnRailRun(grooveStart, grooveEnd, take(runCarry_, runRate_ * (1.0f + runThrustBoost_), deltaTime), kLaunchRunBrightness);
		SpawnIdle(1.0f, take(idleCarry_, idleRate_ * (1.0f + idleThrustBoost_), deltaTime));
	} else {
		launching_ = false;
	}
}

void CatapultLightning::BuildVertices(const Vector3& cameraPos) {
	for (int b = 0; b < kMaxBolts; ++b) {
		const Bolt& bolt = bolts_[b];
		MeshVertex* v = &vertices_[static_cast<size_t>(b) * kVertsPerBolt];
		if (!bolt.alive) {
			for (int i = 0; i < kVertsPerBolt; ++i) {
				v[i].position = origin_;
				v[i].color = { 0.0f, 0.0f, 0.0f, 0.0f };
			}
			continue;
		}
		// 固定台について走る雷は、生まれてから台が進んだ分だけ前へ
		const float follow = bolt.followClamp ? clampTravel_ - bolt.spawnTravel : 0.0f;
		const Vector3 a = Add(Add(origin_, bolt.a), { 0.0f, 0.0f, follow });
		const Vector3 bEnd = Add(Add(origin_, bolt.b), { 0.0f, 0.0f, follow });
		const Vector3 dir = SafeNormalize(Sub(bEnd, a), { 0.0f, 0.0f, 1.0f });
		const Vector3 ref = std::fabs(dir.y) < kNearVerticalCos ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
		const Vector3 p1 = SafeNormalize(Cross(ref, dir), { 1.0f, 0.0f, 0.0f });
		const Vector3 p2 = Cross(dir, p1);
		// 出てすぐ明るく、消える前に暗く
		const float t = bolt.age / (std::max)(bolt.life, kMinFadeSeconds);
		const float fade = (1.0f - t) * (1.0f - t);
		Vector3 points[kPointsPerBolt];
		for (int i = 0; i < kPointsPerBolt; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(kSegments);
			points[i] = Add(Add(Add(a, Mul(Sub(bEnd, a), u)), Mul(p1, bolt.offsets[i][0])), Mul(p2, bolt.offsets[i][1]));
		}
		for (int i = 0; i < kPointsPerBolt; ++i) {
			const Vector3 tangent = SafeNormalize(Sub(points[(std::min)(i + 1, kSegments)], points[(std::max)(i - 1, 0)]), dir);
			const Vector3 view = SafeNormalize(Sub(cameraPos, points[i]), { 0.0f, 1.0f, 0.0f });
			const Vector3 side = Mul(SafeNormalize(Cross(tangent, view), p1), bolt.width * 0.5f);
			const float k = bolt.brightness * fade;
			MeshVertex& left = v[i * kVertsPerPoint];
			MeshVertex& core = v[i * kVertsPerPoint + 1];
			MeshVertex& right = v[i * kVertsPerPoint + 2];
			left.position = Sub(points[i], side);
			core.position = points[i];
			right.position = Add(points[i], side);
			left.color = { glowColor_.x * k, glowColor_.y * k, glowColor_.z * k, 0.0f };
			right.color = left.color;
			core.color = { coreColor_.x * k, coreColor_.y * k, coreColor_.z * k, (std::min)(fade * coreColor_.w, 1.0f) };
			left.texcoord = { 0.0f, 0.0f };
			core.texcoord = { 0.5f, 0.0f };
			right.texcoord = { 1.0f, 0.0f };
			left.normal = core.normal = right.normal = view;
		}
	}
}

void CatapultLightning::Draw(Camera* camera) {
	built_ = false;
	if (!mesh_ || !camera) return;
	bool any = false;
	for (const Bolt& bolt : bolts_) {
		if (bolt.alive) { any = true; break; }
	}
	if (!any) return;
	BuildVertices(camera->GetTranslate());
	mesh_->UpdateVertices(vertices_.data(), static_cast<uint32_t>(vertices_.size()));
	mesh_->Update(camera, 0.0f);
	mesh_->Draw();
	built_ = true;
}

void CatapultLightning::DrawBloom() {
	if (mesh_ && built_) mesh_->DrawBloomPass(bloomIntensity_);
}

void CatapultLightning::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("sweepRate", sweepRate_);
	read("sweepTail", sweepTail_);
	read("sweepWidth", sweepWidth_);
	read("runRate", runRate_);
	read("runThrustBoost", runThrustBoost_);
	read("runLengthMin", runLengthMin_);
	read("runLengthMax", runLengthMax_);
	read("runWidth", runWidth_);
	read("idleArcHeight", idleArcHeight_);
	read("jagMax", jagMax_);
	read("idleRate", idleRate_);
	read("idleThrustBoost", idleThrustBoost_);
	read("idleNearBias", idleNearBias_);
	read("idleWidth", idleWidth_);
	read("chargeClampRate", chargeClampRate_);
	read("launchClampRate", launchClampRate_);
	read("launchWidth", launchWidth_);
	read("launchBrightness", launchBrightness_);
	read("trailRate", trailRate_);
	read("trailLength", trailLength_);
	read("jag", jag_);
	read("afterFadeSeconds", afterFadeSeconds_);
	read("jitterSeconds", jitterSeconds_);
	coreColor_ = ReadVector4(node["coreColor"], coreColor_);
	glowColor_ = ReadVector4(node["glowColor"], glowColor_);
	read("bloomIntensity", bloomIntensity_);
}

void CatapultLightning::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("sweepRate", sweepRate_);
	write("sweepTail", sweepTail_);
	write("sweepWidth", sweepWidth_);
	write("runRate", runRate_);
	write("runThrustBoost", runThrustBoost_);
	write("runLengthMin", runLengthMin_);
	write("runLengthMax", runLengthMax_);
	write("runWidth", runWidth_);
	write("idleArcHeight", idleArcHeight_);
	write("jagMax", jagMax_);
	write("idleRate", idleRate_);
	write("idleThrustBoost", idleThrustBoost_);
	write("idleNearBias", idleNearBias_);
	write("idleWidth", idleWidth_);
	write("chargeClampRate", chargeClampRate_);
	write("launchClampRate", launchClampRate_);
	write("launchWidth", launchWidth_);
	write("launchBrightness", launchBrightness_);
	write("trailRate", trailRate_);
	write("trailLength", trailLength_);
	write("jag", jag_);
	write("afterFadeSeconds", afterFadeSeconds_);
	write("jitterSeconds", jitterSeconds_);
	node["coreColor"] = MakeVector4(coreColor_);
	node["glowColor"] = MakeVector4(glowColor_);
	write("bloomIntensity", bloomIntensity_);
}

void CatapultLightning::OnImGui() {
#ifdef _DEBUG
	int alive = 0;
	for (const Bolt& bolt : bolts_) alive += bolt.alive ? 1 : 0;
	ImGui::Text("bolts %d / %d", alive, kMaxBolts);
	ImGui::TextDisabled("Sweep (with the neon)");
	ImGui::DragFloat("Sweep Rate (/s)", &sweepRate_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Sweep Tail (m)", &sweepTail_, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Sweep Width (m)", &sweepWidth_, 0.005f, 0.005f, 2.0f);
	ImGui::TextDisabled("Rail run (along the whole groove)");
	ImGui::DragFloat("Run Rate (/s)", &runRate_, 1.0f, 0.0f, 2000.0f);
	ImGui::DragFloat("Run Thrust Boost", &runThrustBoost_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Run Length Min (m)", &runLengthMin_, 0.1f, 0.5f, 100.0f);
	ImGui::DragFloat("Run Length Max (m)", &runLengthMax_, 0.1f, 0.5f, 100.0f);
	ImGui::DragFloat("Run Width (m)", &runWidth_, 0.005f, 0.005f, 2.0f);
	ImGui::TextDisabled("Charged (groove discharge, stronger with thrust)");
	ImGui::DragFloat("Idle Rate (/s)", &idleRate_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Idle Thrust Boost", &idleThrustBoost_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Idle Near Bias", &idleNearBias_, 0.05f, 0.1f, 8.0f);
	ImGui::DragFloat("Idle Width (m)", &idleWidth_, 0.005f, 0.005f, 2.0f);
	ImGui::DragFloat("Idle Arc Height (m)", &idleArcHeight_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Charge Clamp Rate (/s)", &chargeClampRate_, 1.0f, 0.0f, 1000.0f);
	ImGui::TextDisabled("Launch (clamp)");
	ImGui::DragFloat("Launch Clamp Rate (/s)", &launchClampRate_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Launch Width (m)", &launchWidth_, 0.005f, 0.005f, 2.0f);
	ImGui::DragFloat("Launch Brightness", &launchBrightness_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Trail Rate (/s)", &trailRate_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Trail Length (m)", &trailLength_, 0.5f, 0.0f, 200.0f);
	ImGui::DragFloat("After Fade (s)", &afterFadeSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::TextDisabled("Look");
	ImGui::DragFloat("Jag", &jag_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Jag Max (m)", &jagMax_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Jitter (s)", &jitterSeconds_, 0.001f, 0.005f, 0.5f);
	ImGui::ColorEdit4("Core Color", &coreColor_.x);
	ImGui::ColorEdit4("Glow Color", &glowColor_.x);
	ImGui::DragFloat("Bloom Intensity", &bloomIntensity_, 0.05f, 0.0f, 20.0f);
#endif // _DEBUG
}
