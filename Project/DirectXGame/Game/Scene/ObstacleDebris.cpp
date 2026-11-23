#include "ObstacleDebris.h"
#include "PepperMacros.h"

#include "IImGuiEditable.h"
#include "Json/JsonParser.h"
#include "LogBuffer.h"
#include "MathUtility.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include "Json/JsonVector.h"

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/ObstacleDebris.json";
	// 倒す向きを調べるために試しに傾ける角度 [rad]
	constexpr float kTipProbeAngleRad = 0.1f;
	// 試しに傾けた 2 つの先端の高さの差がこれ以下なら同じとみなす [m]
	constexpr float kTipHeightDiffEpsilon = 0.01f;
	// 破片をはじく向きの Y の下限の大きさ（上寄りにする。−0.2〜1）
	constexpr float kChunkDirMinY = 0.2f;
	// 長さがこれ以下なら 0 とみなす（0 割りを避ける）
	constexpr float kTinyEpsilon = 1e-4f;
	// 破片の初めの回転の上限 [rad]（ほぼ 1 周）
	constexpr float kChunkMaxInitialRotateRad = 6.28f;
	// 破片の回転の速さの上限 [rad/秒]（−6〜6）
	constexpr float kChunkMaxSpinRadPerSec = 6.0f;

	/// <summary>JSON のオブジェクトから数値を 1 つ読む</summary>
	/// <param name="j">読むオブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="fallback">無い・数値でない時の値</param>
	/// <returns>読んだ値</returns>
	float ReadFloat(const JsonValue& j, const char* key, float fallback) {
		const JsonValue& v = j[key];
		return v.IsNumber() ? static_cast<float>(v.AsDouble(fallback)) : fallback;
	}

	/// <summary>部品のローカルの点をワールドへ（回転は描画と同じ MakeAffineMatrix の Rx·Ry·Rz）</summary>
	/// <param name="local">ローカルの点</param>
	/// <param name="rotate">部品の回転 [rad]</param>
	/// <returns>回転した点（平行移動は含まない）</returns>
	Vector3 Rotate(const Vector3& local, const Vector3& rotate) {
		const Matrix4x4 m = MakeAffineMatrix(Transform{ { 1.0f, 1.0f, 1.0f }, rotate, { 0.0f, 0.0f, 0.0f } });
		return TransformMatrix(local, m);
	}

	/// <summary>ベクトルの和</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a + b</returns>
	Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	/// <summary>ベクトルの差</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a − b</returns>
	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	/// <summary>ベクトルのスカラー倍</summary>
	/// <param name="a">ベクトル</param>
	/// <param name="s">倍率</param>
	/// <returns>a × s</returns>
	Vector3 Scale(const Vector3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
}

void ObstacleDebris::Initialize(IObstacleDebrisHost* host) {
	host_ = host;
	LoadTuning();
}

void ObstacleDebris::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) {
		LogBuffer::GetInstance()->Add(std::string("ObstacleDebris: JSON を読めない: ") + kTuningPath, LogBuffer::Level::Warning);
		return;
	}
	const JsonValue& root = result.value;
	gravity_ = ReadFloat(root, "gravity", gravity_);
	killY_ = ReadFloat(root, "killY", killY_);
	maxLife_ = ReadFloat(root, "maxLife", maxLife_);

	defs_.clear();
	const JsonValue& breaks = root["breaks"];
	if (!breaks.IsObject()) return;
	for (const auto& [name, b] : breaks.AsObject()) {
		BreakDef def;
		const JsonValue& stays = b["stays"];
		if (stays.IsArray()) {
			for (size_t i = 0; i < stays.Size(); ++i) def.stays.push_back(stays[i].AsString());
		}
		const JsonValue& falls = b["falls"];
		if (falls.IsArray()) {
			for (size_t i = 0; i < falls.Size(); ++i) {
				const JsonValue& f = falls[i];
				FallDef fd;
				fd.prefab = f["prefab"].AsString();
				fd.pivot = ReadVector3(f["pivot"], fd.pivot);
				fd.tip = ReadVector3(f["tip"], fd.tip);
				fd.hingeAccel = ReadFloat(f, "hingeAccel", fd.hingeAccel);
				fd.releaseDeg = ReadFloat(f, "releaseDeg", fd.releaseDeg);
				def.falls.push_back(fd);
			}
		}
		const JsonValue& c = b["chunks"];
		if (c.IsObject()) {
			ChunkDef& cd = def.chunks;
			cd.prefab = c["prefab"].AsString();
			cd.count = static_cast<int>(c["count"].AsInt(0));
			cd.origin = ReadVector3(c["origin"], cd.origin);
			cd.spread = ReadFloat(c, "spread", cd.spread);
			const Vector3 speed = ReadVector3(c["speed"], { cd.speedMin, cd.speedMax, 0.0f });
			cd.speedMin = speed.x;
			cd.speedMax = speed.y;
			const Vector3 scale = ReadVector3(c["scale"], { cd.scaleMin, cd.scaleMax, 0.0f });
			cd.scaleMin = scale.x;
			cd.scaleMax = scale.y;
		}
		defs_[name] = std::move(def);
	}
}

bool ObstacleDebris::Break(const std::string& prefab, const Vector3& pos, const Vector3& rotate, const Vector3& awayFrom) {
	PEPPER_SCOPE("ObstacleDebris::Break");
	if (!host_) return false;
	const auto it = defs_.find(prefab);
	if (it == defs_.end()) return false;
	const BreakDef& def = it->second;

	for (const std::string& p : def.stays) {
		if (IImGuiEditable* e = host_->SpawnDebrisPiece(p, pos)) {
			e->SetRotate(rotate);
			stays_.push_back(e);
		}
	}
	for (const FallDef& f : def.falls) SpawnFall(f, pos, rotate, awayFrom);
	SpawnChunks(def.chunks, pos, rotate);
	return true;
}

void ObstacleDebris::SpawnFall(const FallDef& def, const Vector3& pos, const Vector3& rotate, const Vector3& awayFrom) {
	IImGuiEditable* e = host_->SpawnDebrisPiece(def.prefab, pos);
	if (!e) return;

	Moving m;
	m.entity = e;
	m.rotate = rotate;
	m.position = pos;
	m.hinged = true;
	m.pivotLocal = def.pivot;
	m.pivotWorld = Add(pos, Rotate(def.pivot, rotate));
	m.baseRoll = rotate.z;
	m.hingeAccel = def.hingeAccel;
	m.releaseAngle = DegToRad(def.releaseDeg);
	m.angularVelocity = 0.0f;

	// 倒す向き：tip が下がる方。どちらも同じだけ下がる（まっすぐ立つ岩）なら、awayFrom から離れる方
	const Vector3 arm = Sub(def.tip, def.pivot);
	auto tipAt = [&](float sign) {
		Vector3 r = rotate;
		r.z += sign * kTipProbeAngleRad;
		return Add(m.pivotWorld, Rotate(arm, r));
	};
	const Vector3 plus = tipAt(1.0f);
	const Vector3 minus = tipAt(-1.0f);
	if (std::fabs(plus.y - minus.y) > kTipHeightDiffEpsilon) {
		m.direction = plus.y < minus.y ? 1.0f : -1.0f;
	} else {
		auto dist2 = [&](const Vector3& p) {
			const float dx = p.x - awayFrom.x, dz = p.z - awayFrom.z;
			return dx * dx + dz * dz;
		};
		m.direction = dist2(plus) > dist2(minus) ? 1.0f : -1.0f;
	}
	// 外れた後に回る中心は tip（部品の真ん中あたり）
	m.centerLocal = def.tip;
	Apply(m);
	moving_.push_back(m);
}

void ObstacleDebris::SpawnChunks(const ChunkDef& def, const Vector3& pos, const Vector3& rotate) {
	if (def.prefab.empty() || def.count <= 0) return;
	const Vector3 center = Add(pos, Rotate(def.origin, rotate));
	for (int i = 0; i < def.count; ++i) {
		const Vector3 at{ center.x + RandRange(-def.spread, def.spread),
		                  center.y + RandRange(-def.spread, def.spread),
		                  center.z + RandRange(-def.spread, def.spread) };
		IImGuiEditable* e = host_->SpawnDebrisPiece(def.prefab, at);
		if (!e) continue;
		const float s = RandRange(def.scaleMin, def.scaleMax);
		e->SetScale({ s, s, s });

		// 上寄りの向きへはじく
		Vector3 dir{ RandRange(-1.0f, 1.0f), RandRange(-kChunkDirMinY, 1.0f), RandRange(-1.0f, 1.0f) };
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		dir = len > kTinyEpsilon ? Scale(dir, 1.0f / len) : Vector3{ 0.0f, 1.0f, 0.0f };

		Moving m;
		m.entity = e;
		m.position = at;
		m.rotate = { RandRange(0.0f, kChunkMaxInitialRotateRad), RandRange(0.0f, kChunkMaxInitialRotateRad), RandRange(0.0f, kChunkMaxInitialRotateRad) };
		m.velocity = Scale(dir, RandRange(def.speedMin, def.speedMax));
		m.spin = { RandRange(-kChunkMaxSpinRadPerSec, kChunkMaxSpinRadPerSec), RandRange(-kChunkMaxSpinRadPerSec, kChunkMaxSpinRadPerSec), RandRange(-kChunkMaxSpinRadPerSec, kChunkMaxSpinRadPerSec) };
		m.hinged = false;
		Apply(m);
		moving_.push_back(m);
	}
}

void ObstacleDebris::Update(float dt) {
	PEPPER_SCOPE("ObstacleDebris::Update");
	if (dt <= 0.0f) return;
	for (Moving& m : moving_) {
		m.life += dt;
		if (m.hinged) {
			const Vector3 centerBefore = Add(m.position, Rotate(m.centerLocal, m.rotate));
			m.angularVelocity += m.hingeAccel * dt;
			m.angle += m.angularVelocity * dt;
			m.rotate.z = m.baseRoll + m.direction * m.angle;
			// 支点が動かないように原点を置き直す
			m.position = Sub(m.pivotWorld, Rotate(m.pivotLocal, m.rotate));
			if (m.angle >= m.releaseAngle) {
				// 外れる：真ん中の今の速さを引き継ぎ、以後は真ん中を中心に回りながら落ちる
				const Vector3 centerNow = Add(m.position, Rotate(m.centerLocal, m.rotate));
				m.velocity = Scale(Sub(centerNow, centerBefore), 1.0f / dt);
				m.spin = { 0.0f, 0.0f, m.direction * m.angularVelocity };
				m.hinged = false;
			}
		} else {
			// 回る中心のワールド位置を放物線で進め、そこから原点を戻す
			Vector3 center = Add(m.position, Rotate(m.centerLocal, m.rotate));
			m.velocity.y -= gravity_ * dt;
			center = Add(center, Scale(m.velocity, dt));
			m.rotate = Add(m.rotate, Scale(m.spin, dt));
			m.position = Sub(center, Rotate(m.centerLocal, m.rotate));
		}
		Apply(m);
	}

	// 谷底より下・時間切れを片付ける
	auto done = [&](const Moving& m) { return m.position.y < killY_ || m.life > maxLife_; };
	for (Moving& m : moving_) {
		if (done(m) && m.entity) {
			host_->DestroyDebrisPiece(m.entity);
			m.entity = nullptr;
		}
	}
	moving_.erase(std::remove_if(moving_.begin(), moving_.end(), [](const Moving& m) { return !m.entity; }), moving_.end());
}

void ObstacleDebris::Clear() {
	if (!host_) return;
	for (Moving& m : moving_) {
		if (m.entity) host_->DestroyDebrisPiece(m.entity);
	}
	for (IImGuiEditable* e : stays_) {
		if (e) host_->DestroyDebrisPiece(e);
	}
	moving_.clear();
	stays_.clear();
}

bool ObstacleDebris::Owns(const IImGuiEditable* entity) const {
	if (!entity) return false;
	if (std::find(stays_.begin(), stays_.end(), entity) != stays_.end()) return true;
	return std::any_of(moving_.begin(), moving_.end(), [&](const Moving& m) { return m.entity == entity; });
}

void ObstacleDebris::Apply(Moving& m) const {
	if (!m.entity) return;
	if (Vector3* t = m.entity->GetEditableTranslate()) *t = m.position;
	m.entity->SetRotate(m.rotate);
}

float ObstacleDebris::RandRange(float a, float b) {
	std::uniform_real_distribution<float> d(a, b);
	return d(rng_);
}
