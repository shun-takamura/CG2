#include "LaunchCable.h"

#include "Camera.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/MeshData.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kWhiteTexture = "Resources/Textures/white1x1.dds";

	Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vector3 Mul(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
	Vector3 Cross(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	float Len(const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
	Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback) {
		const float l = Len(v);
		return l > 1e-6f ? Mul(v, 1.0f / l) : fallback;
	}
	Vector3 LerpV(const Vector3& a, const Vector3& b, float t) { return Add(a, Mul(Sub(b, a), t)); }

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}
	void ReadVector4(const JsonValue& node, Vector4& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)), node.Size() >= 4 ? static_cast<float>(node[3].AsDouble(value.w)) : value.w };
	}
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue node = JsonValue::MakeArray();
		node.Push(JsonValue(static_cast<double>(v.x)));
		node.Push(JsonValue(static_cast<double>(v.y)));
		node.Push(JsonValue(static_cast<double>(v.z)));
		return node;
	}
	JsonValue MakeVector4(const Vector4& v) {
		JsonValue node = MakeVector3({ v.x, v.y, v.z });
		node.Push(JsonValue(static_cast<double>(v.w)));
		return node;
	}

	// 頂点の並び：点 i の輪（kSides 個）を順に。最後に両端のふたの中心 2 つ
	constexpr int kRingVertexCount = LaunchCable::kPointCount * LaunchCable::kSides;
	constexpr int kVertexCount = kRingVertexCount + 2;
}

LaunchCable::LaunchCable() = default;
LaunchCable::~LaunchCable() = default;

void LaunchCable::Initialize() {
	MeshData data;
	data.vertices.resize(kVertexCount);
	for (auto& v : data.vertices) v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	const uint32_t S = static_cast<uint32_t>(kSides);
	for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(kPointCount); ++i) {
		for (uint32_t s = 0; s < S; ++s) {
			const uint32_t a = i * S + s;
			const uint32_t b = i * S + (s + 1) % S;
			const uint32_t c = (i + 1) * S + s;
			const uint32_t d = (i + 1) * S + (s + 1) % S;
			data.indices.insert(data.indices.end(), { a, c, b, b, c, d });
		}
	}
	// 両端のふた（パージした後の端が見える）
	const uint32_t startCap = static_cast<uint32_t>(kRingVertexCount);
	const uint32_t endCap = startCap + 1;
	const uint32_t last = static_cast<uint32_t>(kPointCount - 1) * S;
	for (uint32_t s = 0; s < S; ++s) {
		data.indices.insert(data.indices.end(), { startCap, (s + 1) % S, s });
		data.indices.insert(data.indices.end(), { endCap, last + s, last + (s + 1) % S });
	}
	mesh_ = std::make_unique<PrimitiveMesh>();
	mesh_->Initialize(data);
	mesh_->SetTexture(kWhiteTexture);
	mesh_->SetBlendMode(PrimitivePipeline::kBlendModeNone);
	mesh_->SetDepthWrite(true);
	mesh_->SetCullBackface(false);
	vertices_ = data.vertices;
	valid_ = false;
}

void LaunchCable::Reset(const Vector3& anchor, const Vector3& attach, const Vector3& restAttach) {
	attached_ = true;
	purgeTime_ = 0.0f;
	// 長さは機体が前へ出ていないときの距離で決める（前へ出たままつなぎ直すと、戻ったときに長すぎて床に垂れた）
	baseLength_ = Len(Sub(restAttach, anchor)) * (std::max)(slack_, 1.0f);
	const float d = Len(Sub(attach, anchor));
	length_ = (std::max)(baseLength_, d * stretchSlack_);
	// 長さ L・距離 d の放物線のたるみ s ≈ sqrt(3·d·(L − d) / 8)
	const float sag = std::sqrt((std::max)(3.0f * d * (length_ - d) / 8.0f, 0.0f));
	for (int i = 0; i < kPointCount; ++i) {
		const float u = static_cast<float>(i) / static_cast<float>(kPointCount - 1);
		Vector3 p = LerpV(anchor, attach, u);
		p.y -= sag * 4.0f * u * (1.0f - u);
		points_[i] = p;
		previous_[i] = p;
	}
	valid_ = true;
}

void LaunchCable::Purge() {
	if (!attached_) return;
	attached_ = false;
	purgeTime_ = 0.0f;
	purgeLength_ = length_;
	// 機体の速さは捨てて、跳ね上がって後ろへ残る速さ＋螺旋の速さを与える（端ほど大きい。Verlet なので前の位置で表す）。
	// 螺旋：各点で「その点のケーブルの向き」に直交する 2 方向の中で向きを回す
	const int last = kPointCount - 1;
	const float h = 1.0f / (60.0f * static_cast<float>((std::max)(substeps_, 1)));
	for (int i = 1; i <= last; ++i) {
		const float u = static_cast<float>(i) / static_cast<float>(last);
		const float angle = kPi * waveKickHalfWaves_ * u;
		const Vector3 swirl = SwirlDirection(i, angle);
		const Vector3 v = Add(Mul(swirl, waveKick_ * u), Mul(purgeKick_, u * u));
		previous_[i] = Sub(points_[i], Mul(v, h));
	}
}

Vector3 LaunchCable::SwirlDirection(int i, float angle) const {
	const int last = kPointCount - 1;
	const Vector3 tangent = SafeNormalize(Sub(points_[(std::min)(i + 1, last)], points_[(std::max)(i - 1, 0)]), { 0.0f, 0.0f, 1.0f });
	const Vector3 ref = std::fabs(tangent.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
	const Vector3 side = SafeNormalize(Cross(ref, tangent), { 1.0f, 0.0f, 0.0f });
	const Vector3 up = Cross(tangent, side);
	// waveUpRatio が 1 で円（螺旋）、小さいほど横につぶれた楕円
	return Add(Mul(side, std::cos(angle)), Mul(up, std::sin(angle) * waveUpRatio_));
}

void LaunchCable::Update(float deltaTime, const Vector3& anchor, const Vector3& attach, const Vector3& restAttach, float floorY) {
	if (!valid_) Reset(anchor, attach, restAttach);
	if (deltaTime <= 0.0f) return;
	if (attached_) {
		// 機体が離れるとゴムのように伸びる（ほぼ張った長さ）
		length_ = (std::max)(baseLength_, Len(Sub(attach, anchor)) * stretchSlack_);
	} else {
		purgeTime_ += deltaTime;
		// 外れてしばらくは長さを保ってうねらせ、それから縮める
		const float u = retractSeconds_ > 1e-3f ? (purgeTime_ - retractDelay_) / retractSeconds_ : 1.0f;
		length_ = purgeLength_ + (retractLength_ - purgeLength_) * SmoothStep01(u);
	}
	Simulate(deltaTime, anchor, attach, floorY);
}

void LaunchCable::Simulate(float dt, const Vector3& anchor, const Vector3& attach, float floorY) {
	const int steps = (std::max)(substeps_, 1);
	const float h = dt / static_cast<float>(steps);
	const float maxMove = maxSpeed_ * h;
	const float segment = length_ / static_cast<float>(kPointCount - 1);
	const float thin = std::clamp(std::sqrt(baseLength_ / (std::max)(length_, 1e-3f)), thinMin_, 1.0f);
	const float floorLimit = floorY + radius_ * thin;
	const int last = kPointCount - 1;
	const float damping = attached_ ? damping_ : purgeDamping_;
	// 外れた後のうねり：ケーブルの軸の周りを回りながら先端から付け根へ伝わる螺旋の波。時間とともに弱める
	const float waveFade = attached_ ? 0.0f
		: 1.0f - SmoothStep01(waveFadeSeconds_ > 1e-3f ? purgeTime_ / waveFadeSeconds_ : 1.0f);
	const float bendRest = 2.0f * segment * bendRest_;
	// カメラの側へ引き寄せる強さ（時間の山）
	float sweepEnvelope = 0.0f;
	if (!attached_ && sweepSeconds_ > 1e-3f) {
		const float st = (purgeTime_ - sweepDelay_) / sweepSeconds_;
		if (st > 0.0f && st < 1.0f) sweepEnvelope = std::sin(kPi * st);
	}
	for (int step = 0; step < steps; ++step) {
		// 付け根と（つないでいる間は）機体側の端は決まった位置
		const float t = static_cast<float>(step + 1) / static_cast<float>(steps);
		const Vector3 end = attached_ ? LerpV(points_[last], attach, t) : points_[last];
		for (int i = 0; i < kPointCount; ++i) {
			if (i == 0 || (attached_ && i == last)) continue;
			Vector3 v = Mul(Sub(points_[i], previous_[i]), damping);
			const float speed = Len(v);
			if (speed > maxMove && speed > 1e-6f) v = Mul(v, maxMove / speed);
			Vector3 force = { 0.0f, -gravity_, 0.0f };
			if (waveFade > 0.0f) {
				const float u = static_cast<float>(i) / static_cast<float>(last);
				const float phase = 2.0f * kPi * (waveFrequency_ * purgeTime_ + waveCount_ * u);
				force = Add(force, Mul(SwirlDirection(i, phase), waveForce_ * waveFade * std::sqrt(u)));
			}
			if (sweepEnvelope > 0.0f) {
				const float u = static_cast<float>(i) / static_cast<float>(last);
				const float w = sweepWidth_ > 1e-3f ? (u - sweepCenter_) / sweepWidth_ : 0.0f;
				const float shape = std::exp(-w * w);
				// カメラへの向きから、ケーブルの向きの成分を除く（ケーブルを横へ振る）
				const Vector3 toTarget = Sub(sweepTarget_, points_[i]);
				const float dist = Len(toTarget);
				const Vector3 tangent = SafeNormalize(Sub(points_[(std::min)(i + 1, last)], points_[i - 1]), { 0.0f, 0.0f, 1.0f });
				const float k = toTarget.x * tangent.x + toTarget.y * tangent.y + toTarget.z * tangent.z;
				const Vector3 dir = SafeNormalize(Sub(toTarget, Mul(tangent, k)), { 0.0f, 0.0f, 0.0f });
				const float keepOut = sweepKeepOut_ > 1e-3f ? std::clamp((dist - sweepKeepOut_) / sweepKeepOut_, 0.0f, 1.0f) : 1.0f;
				force = Add(force, Mul(dir, sweepForce_ * sweepEnvelope * shape * keepOut));
			}
			previous_[i] = points_[i];
			points_[i] = Add(Add(points_[i], v), Mul(force, h * h));
		}
		previous_[0] = points_[0];
		points_[0] = anchor;
		if (attached_) {
			previous_[last] = points_[last];
			points_[last] = end;
		}
		for (int it = 0; it < iterations_; ++it) {
			for (int i = 0; i < last; ++i) {
				const bool fixedA = (i == 0);
				const bool fixedB = (attached_ && i + 1 == last);
				if (fixedA && fixedB) continue;
				const Vector3 d = Sub(points_[i + 1], points_[i]);
				const float l = Len(d);
				if (l < 1e-6f) continue;
				const Vector3 corr = Mul(d, (l - segment) / l * stiffness_);
				if (fixedA) {
					points_[i + 1] = Sub(points_[i + 1], corr);
				} else if (fixedB) {
					points_[i] = Add(points_[i], corr);
				} else {
					points_[i] = Add(points_[i], Mul(corr, 0.5f));
					points_[i + 1] = Sub(points_[i + 1], Mul(corr, 0.5f));
				}
			}
			// 曲げの硬さ：1 つ飛ばしの点が近づきすぎない（折れずに丸い弧を保つ太いホース）
			if (bendStiffness_ > 0.0f) {
				for (int i = 0; i + 2 <= last; ++i) {
					const bool fixedA = (i == 0);
					const bool fixedB = (attached_ && i + 2 == last);
					if (fixedA && fixedB) continue;
					const Vector3 d = Sub(points_[i + 2], points_[i]);
					const float l = Len(d);
					if (l >= bendRest || l < 1e-6f) continue;
					const Vector3 corr = Mul(d, (l - bendRest) / l * bendStiffness_);
					if (fixedA) {
						points_[i + 2] = Sub(points_[i + 2], corr);
					} else if (fixedB) {
						points_[i] = Add(points_[i], corr);
					} else {
						points_[i] = Add(points_[i], Mul(corr, 0.5f));
						points_[i + 2] = Sub(points_[i + 2], Mul(corr, 0.5f));
					}
				}
			}
		}
		// 床
		for (int i = 1; i < kPointCount; ++i) {
			if (points_[i].y >= floorLimit) continue;
			points_[i].y = floorLimit;
			previous_[i].y = floorLimit;
			previous_[i].x = points_[i].x - (points_[i].x - previous_[i].x) * floorFriction_;
			previous_[i].z = points_[i].z - (points_[i].z - previous_[i].z) * floorFriction_;
		}
	}
}

void LaunchCable::BuildVertices(const Vector3& cameraPos) {
	const Vector3 keyDir = SafeNormalize(keyDirection_, { 0.0f, 1.0f, 0.0f });
	const float thin = std::clamp(std::sqrt(baseLength_ / (std::max)(length_, 1e-3f)), thinMin_, 1.0f);
	auto shade = [&](const Vector3& n, const Vector3& p, const Vector4& albedo) {
		const float up = n.y * 0.5f + 0.5f;
		const Vector3 hemi = LerpV(groundLight_, skyLight_, up);
		const float diffuse = (std::max)(n.x * keyDir.x + n.y * keyDir.y + n.z * keyDir.z, 0.0f) * keyIntensity_;
		const Vector3 view = SafeNormalize(Sub(cameraPos, p), { 0.0f, 0.0f, 1.0f });
		const Vector3 half = SafeNormalize(Add(keyDir, view), keyDir);
		const float nh = (std::max)(n.x * half.x + n.y * half.y + n.z * half.z, 0.0f);
		const float spec = std::pow(nh, specularPower_) * specularIntensity_;
		return Vector4{ albedo.x * (hemi.x + diffuse) + spec, albedo.y * (hemi.y + diffuse) + spec,
			albedo.z * (hemi.z + diffuse) + spec, 1.0f };
	};

	Vector3 normal = { 0.0f, 1.0f, 0.0f };
	Vector3 firstTangent = { 0.0f, 0.0f, 1.0f };
	Vector3 lastTangent = { 0.0f, 0.0f, 1.0f };
	for (int i = 0; i < kPointCount; ++i) {
		const Vector3& p = points_[i];
		const Vector3 tangent = SafeNormalize(Sub(points_[(std::min)(i + 1, kPointCount - 1)], points_[(std::max)(i - 1, 0)]),
			i == 0 ? firstTangent : lastTangent);
		if (i == 0) {
			firstTangent = tangent;
			const Vector3 up = std::fabs(tangent.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
			normal = SafeNormalize(Cross(up, tangent), { 1.0f, 0.0f, 0.0f });
		} else {
			// 前の点の法線を今の接線に直交させて運ぶ（ねじれないように）
			const float k = normal.x * tangent.x + normal.y * tangent.y + normal.z * tangent.z;
			normal = SafeNormalize(Sub(normal, Mul(tangent, k)), normal);
		}
		lastTangent = tangent;
		const Vector3 binormal = Cross(tangent, normal);
		const bool plug = i >= kPointCount - (std::max)(plugPoints_, 0);
		const float r = radius_ * thin * (plug ? plugScale_ : 1.0f);
		const Vector4& albedo = plug ? plugColor_ : color_;
		for (int s = 0; s < kSides; ++s) {
			const float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(kSides);
			const Vector3 n = Add(Mul(normal, std::cos(a)), Mul(binormal, std::sin(a)));
			MeshVertex& v = vertices_[i * kSides + s];
			v.position = Add(p, Mul(n, r));
			v.normal = n;
			v.texcoord = { static_cast<float>(s) / static_cast<float>(kSides), static_cast<float>(i) / static_cast<float>(kPointCount - 1) };
			v.color = shade(n, v.position, albedo);
		}
	}
	MeshVertex& startCap = vertices_[kRingVertexCount];
	startCap.position = points_[0];
	startCap.normal = Mul(firstTangent, -1.0f);
	startCap.color = shade(startCap.normal, startCap.position, color_);
	MeshVertex& endCap = vertices_[kRingVertexCount + 1];
	endCap.position = points_[kPointCount - 1];
	endCap.normal = lastTangent;
	endCap.color = shade(endCap.normal, endCap.position, plugPoints_ > 0 ? plugColor_ : color_);
}

void LaunchCable::Draw(Camera* camera) {
	if (!mesh_ || !valid_ || !camera) return;
	BuildVertices(camera->GetTranslate());
	mesh_->UpdateVertices(vertices_.data(), static_cast<uint32_t>(vertices_.size()));
	mesh_->Update(camera, 0.0f);
	mesh_->Draw();
}

void LaunchCable::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	auto readInt = [&node](const char* key, int& value) {
		value = static_cast<int>(node[key].AsDouble(value));
	};
	readInt("anchorIndex", anchorIndex_);
	anchorIndex_ = std::clamp(anchorIndex_, 0, 1);
	attachBone_ = node["attachBone"].AsString(attachBone_);
	ReadVector3(node["attachOffset"], attachOffset_);
	read("radius", radius_);
	read("slack", slack_);
	read("stretchSlack", stretchSlack_);
	read("thinMin", thinMin_);
	read("gravity", gravity_);
	read("damping", damping_);
	read("stiffness", stiffness_);
	readInt("iterations", iterations_);
	readInt("substeps", substeps_);
	read("maxSpeed", maxSpeed_);
	read("retractLength", retractLength_);
	read("retractSeconds", retractSeconds_);
	read("retractDelay", retractDelay_);
	read("purgeDamping", purgeDamping_);
	read("bendStiffness", bendStiffness_);
	read("bendRest", bendRest_);
	read("waveKick", waveKick_);
	read("waveKickHalfWaves", waveKickHalfWaves_);
	read("waveForce", waveForce_);
	read("waveFrequency", waveFrequency_);
	read("waveCount", waveCount_);
	read("waveUpRatio", waveUpRatio_);
	read("waveFadeSeconds", waveFadeSeconds_);
	read("sweepForce", sweepForce_);
	read("sweepDelay", sweepDelay_);
	read("sweepSeconds", sweepSeconds_);
	read("sweepCenter", sweepCenter_);
	read("sweepWidth", sweepWidth_);
	read("sweepKeepOut", sweepKeepOut_);
	ReadVector3(node["purgeKick"], purgeKick_);
	read("floorFriction", floorFriction_);
	readInt("plugPoints", plugPoints_);
	read("plugScale", plugScale_);
	ReadVector4(node["color"], color_);
	ReadVector4(node["plugColor"], plugColor_);
	ReadVector3(node["skyLight"], skyLight_);
	ReadVector3(node["groundLight"], groundLight_);
	ReadVector3(node["keyDirection"], keyDirection_);
	read("keyIntensity", keyIntensity_);
	read("specularIntensity", specularIntensity_);
	read("specularPower", specularPower_);
}

void LaunchCable::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["anchorIndex"] = JsonValue(static_cast<double>(anchorIndex_));
	node["attachBone"] = JsonValue(attachBone_);
	node["attachOffset"] = MakeVector3(attachOffset_);
	write("radius", radius_);
	write("slack", slack_);
	write("stretchSlack", stretchSlack_);
	write("thinMin", thinMin_);
	write("gravity", gravity_);
	write("damping", damping_);
	write("stiffness", stiffness_);
	node["iterations"] = JsonValue(static_cast<double>(iterations_));
	node["substeps"] = JsonValue(static_cast<double>(substeps_));
	write("maxSpeed", maxSpeed_);
	write("retractLength", retractLength_);
	write("retractSeconds", retractSeconds_);
	write("retractDelay", retractDelay_);
	write("purgeDamping", purgeDamping_);
	write("bendStiffness", bendStiffness_);
	write("bendRest", bendRest_);
	write("waveKick", waveKick_);
	write("waveKickHalfWaves", waveKickHalfWaves_);
	write("waveForce", waveForce_);
	write("waveFrequency", waveFrequency_);
	write("waveCount", waveCount_);
	write("waveUpRatio", waveUpRatio_);
	write("waveFadeSeconds", waveFadeSeconds_);
	write("sweepForce", sweepForce_);
	write("sweepDelay", sweepDelay_);
	write("sweepSeconds", sweepSeconds_);
	write("sweepCenter", sweepCenter_);
	write("sweepWidth", sweepWidth_);
	write("sweepKeepOut", sweepKeepOut_);
	node["purgeKick"] = MakeVector3(purgeKick_);
	write("floorFriction", floorFriction_);
	node["plugPoints"] = JsonValue(static_cast<double>(plugPoints_));
	write("plugScale", plugScale_);
	node["color"] = MakeVector4(color_);
	node["plugColor"] = MakeVector4(plugColor_);
	node["skyLight"] = MakeVector3(skyLight_);
	node["groundLight"] = MakeVector3(groundLight_);
	node["keyDirection"] = MakeVector3(keyDirection_);
	write("keyIntensity", keyIntensity_);
	write("specularIntensity", specularIntensity_);
	write("specularPower", specularPower_);
}

void LaunchCable::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("%s  length %.1f m (base %.1f)  purge %.2f s", attached_ ? "attached" : "purged", length_, baseLength_, purgeTime_);
	ImGui::SliderInt("Anchor Index", &anchorIndex_, 0, 1);
	ImGui::DragFloat3("Attach Offset", &attachOffset_.x, 0.02f);
	ImGui::DragFloat("Radius (m)", &radius_, 0.005f, 0.01f, 2.0f);
	ImGui::DragFloat("Slack", &slack_, 0.005f, 1.0f, 2.0f);
	ImGui::DragFloat("Stretch Slack", &stretchSlack_, 0.002f, 0.9f, 1.5f);
	ImGui::DragFloat("Thin Min", &thinMin_, 0.01f, 0.1f, 1.0f);
	ImGui::DragFloat("Gravity", &gravity_, 0.1f, 0.0f, 60.0f);
	ImGui::DragFloat("Damping", &damping_, 0.001f, 0.8f, 1.0f);
	ImGui::DragFloat("Stiffness", &stiffness_, 0.01f, 0.05f, 1.0f);
	ImGui::SliderInt("Iterations", &iterations_, 1, 80);
	ImGui::SliderInt("Substeps", &substeps_, 1, 8);
	ImGui::DragFloat("Max Speed (m/s)", &maxSpeed_, 1.0f, 1.0f, 500.0f);
	ImGui::DragFloat("Retract Length (m)", &retractLength_, 0.1f, 0.5f, 80.0f);
	ImGui::DragFloat("Retract (s)", &retractSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat("Retract Delay (s)", &retractDelay_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat3("Purge Kick (m/s)", &purgeKick_.x, 0.2f);
	ImGui::DragFloat("Purge Damping", &purgeDamping_, 0.001f, 0.8f, 1.0f);
	ImGui::DragFloat("Bend Stiffness", &bendStiffness_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Bend Rest", &bendRest_, 0.005f, 0.5f, 1.0f);
	ImGui::TextDisabled("Wave (after purge)");
	ImGui::DragFloat("Wave Kick (m/s)", &waveKick_, 0.2f, 0.0f, 100.0f);
	ImGui::DragFloat("Wave Kick Half Waves", &waveKickHalfWaves_, 0.05f, 0.0f, 8.0f);
	ImGui::DragFloat("Wave Force (m/s2)", &waveForce_, 0.5f, 0.0f, 400.0f);
	ImGui::DragFloat("Wave Frequency (Hz)", &waveFrequency_, 0.01f, 0.0f, 6.0f);
	ImGui::DragFloat("Wave Count", &waveCount_, 0.01f, 0.0f, 6.0f);
	ImGui::DragFloat("Wave Helix Ratio", &waveUpRatio_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Wave Fade (s)", &waveFadeSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::TextDisabled("Sweep past camera (after purge)");
	ImGui::DragFloat("Sweep Force (m/s2)", &sweepForce_, 0.5f, 0.0f, 400.0f);
	ImGui::DragFloat("Sweep Delay (s)", &sweepDelay_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Sweep (s)", &sweepSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::SliderFloat("Sweep Center (u)", &sweepCenter_, 0.0f, 1.0f);
	ImGui::DragFloat("Sweep Width (u)", &sweepWidth_, 0.005f, 0.02f, 2.0f);
	ImGui::DragFloat("Sweep Keep Out (m)", &sweepKeepOut_, 0.1f, 0.0f, 30.0f);
	ImGui::DragFloat("Floor Friction", &floorFriction_, 0.01f, 0.0f, 1.0f);
	ImGui::SliderInt("Plug Points", &plugPoints_, 0, 6);
	ImGui::DragFloat("Plug Scale", &plugScale_, 0.01f, 1.0f, 3.0f);
	ImGui::ColorEdit3("Color", &color_.x);
	ImGui::ColorEdit3("Plug Color", &plugColor_.x);
	ImGui::ColorEdit3("Sky Light", &skyLight_.x);
	ImGui::ColorEdit3("Ground Light", &groundLight_.x);
	ImGui::DragFloat3("Key Direction", &keyDirection_.x, 0.01f);
	ImGui::DragFloat("Key Intensity", &keyIntensity_, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Specular", &specularIntensity_, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Specular Power", &specularPower_, 0.5f, 1.0f, 256.0f);
#endif // _DEBUG
}
