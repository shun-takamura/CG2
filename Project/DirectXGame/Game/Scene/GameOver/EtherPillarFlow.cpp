#include "EtherPillarFlow.h"

#include "Camera.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/MeshData.h"
#include "Json/JsonValue.h"
#include "MathUtility.h"   // kPi
#include <algorithm>
#include <cmath>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 長さがこれ以下のベクトルは向きを決めない
	constexpr float kMinNormalizeLength = 1e-6f;
	// 光の筋の 1 点あたりの頂点の数（左の縁・芯・右の縁）
	constexpr int kVertsPerPoint = 3;
	// 道（高さと半径の並び）に要る点の数
	constexpr int kMinProfilePoints = 2;
	// 道の 1 区間の長さの下限 [m]（0 で割らないように）
	constexpr float kMinSegmentLength = 1e-5f;
	// 光を出す間隔のばらつき（倍率）の最小
	constexpr float kIntervalJitterMin = 0.5f;
	// 光を出す間隔のばらつき（倍率）の最大
	constexpr float kIntervalJitterMax = 1.5f;
	// 筋の番号を本数未満に収めるための余裕
	constexpr float kLineIndexEpsilon = 0.001f;
	// 泉の気配の光の速さのばらつき（倍率）の最小
	constexpr float kIdleSpeedJitterMin = 0.8f;
	// 泉の気配の光の速さのばらつき（倍率）の最大
	constexpr float kIdleSpeedJitterMax = 1.2f;
	// 泉の気配の光の明るさのばらつき（倍率）の最小
	constexpr float kIdleIntensityJitterMin = 0.6f;
	// 光を出す間隔の下限 [秒]
	constexpr float kMinInterval = 0.005f;
	// 明るい光の速さのばらつき（倍率）の最小
	constexpr float kSpeedJitterMin = 0.85f;
	// 明るい光の速さのばらつき（倍率）の最大
	constexpr float kSpeedJitterMax = 1.15f;
	// 明るい光の明るさのばらつき（倍率）の最小
	constexpr float kIntensityJitterMin = 0.7f;
	// 明るい光の長さのばらつき（倍率）の最小
	constexpr float kLengthJitterMin = 0.8f;
	// 明るい光の長さのばらつき（倍率）の最大
	constexpr float kLengthJitterMax = 1.6f;
	// 泉の気配の光より明るいかを見分ける余裕（倍率）
	constexpr float kIdleIntensityMargin = 1.01f;
	// 泉の気配の明るさの息づきの真ん中
	constexpr float kBreathMid = 0.75f;
	// 泉の気配の明るさの息づきの振れ幅
	constexpr float kBreathAmplitude = 0.25f;
	// 半回りの角度 [度]（度 → rad）
	constexpr float kDegreesPerHalfTurn = 180.0f;
	// 光の長さの下限 [m]（0 で割らないように）
	constexpr float kMinPulseLength = 1e-3f;
	// 道の点（高さ・半径）の JSON 配列に要る要素の数
	constexpr int kVector2ComponentCount = 2;

	constexpr const char* kWhiteTexture = "Resources/Textures/white1x1.dds";
	constexpr int kVertsPerLine = EtherPillarFlow::kSamples * 3;   // 帯の左の縁・芯・右の縁

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
	/// <summary>ベクトルを正規化する</summary>
	/// <param name="v">正規化するベクトル</param>
	/// <param name="fallback">長さがほぼ 0 のときに返す値</param>
	/// <returns>長さ 1 のベクトル。長さがほぼ 0 なら fallback</returns>
	Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback) {
		const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return l > kMinNormalizeLength ? Mul(v, 1.0f / l) : fallback;
	}

	// G4 の器（EtherVesselGeometry.json）に合わせた既定の道：球の下 → 台座の中央の柱 → 支柱の柱（半径約 0.033）→ 歯車の中へ。
	// 高さは器の原点から。歯車・天板の中に入った所は深度で隠れる
	const Vector2 kDefaultProfile[] = {
		{ 2.56f, 0.025f }, { 1.97f, 0.036f }, { 1.58f, 0.040f }, { 1.16f, 0.060f },
	};
}

EtherPillarFlow::EtherPillarFlow() = default;
EtherPillarFlow::~EtherPillarFlow() = default;

void EtherPillarFlow::Initialize() {
	if (profile_.empty()) profile_.assign(std::begin(kDefaultProfile), std::end(kDefaultProfile));
	RebuildProfile();

	MeshData data;
	data.vertices.resize(static_cast<size_t>(kMaxLines) * kVertsPerLine);
	for (auto& v : data.vertices) v.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (uint32_t l = 0; l < static_cast<uint32_t>(kMaxLines); ++l) {
		const uint32_t base = l * kVertsPerLine;
		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(kSamples); ++i) {
			const uint32_t p = base + i * kVertsPerPoint;
			const uint32_t q = p + kVertsPerPoint;
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
}

void EtherPillarFlow::RebuildProfile() {
	cumulative_.assign(profile_.size(), 0.0f);
	totalLength_ = 0.0f;
	for (size_t i = 1; i < profile_.size(); ++i) {
		const float dy = profile_[i].x - profile_[i - 1].x;
		const float dr = profile_[i].y - profile_[i - 1].y;
		totalLength_ += std::sqrt(dy * dy + dr * dr);
		cumulative_[i] = totalLength_;
	}
}

float EtherPillarFlow::Rand(float lo, float hi) {
	if (hi < lo) std::swap(lo, hi);
	std::uniform_real_distribution<float> d(lo, hi);
	return d(random_);
}

Vector3 EtherPillarFlow::PointAt(float s, float angle) const {
	if (profile_.empty()) return origin_;
	float height = profile_.front().x;
	float radius = profile_.front().y;
	if (profile_.size() >= kMinProfilePoints) {
		s = std::clamp(s, 0.0f, totalLength_);
		size_t i = 1;
		while (i + 1 < profile_.size() && cumulative_[i] < s) ++i;
		const float segment = (std::max)(cumulative_[i] - cumulative_[i - 1], kMinSegmentLength);
		const float t = std::clamp((s - cumulative_[i - 1]) / segment, 0.0f, 1.0f);
		height = profile_[i - 1].x + (profile_[i].x - profile_[i - 1].x) * t;
		radius = profile_[i - 1].y + (profile_[i].y - profile_[i - 1].y) * t;
	}
	radius += surfaceLift_;
	return { origin_.x + std::cos(angle) * radius, origin_.y + height, origin_.z + std::sin(angle) * radius };
}

Vector3 EtherPillarFlow::GetBottom() const {
	if (profile_.empty()) return origin_;
	return { origin_.x, origin_.y + profile_.back().x, origin_.z };
}

void EtherPillarFlow::BeginDescend() {
	descending_ = true;
	descendTimer_ = 0.0f;
}

void EtherPillarFlow::BeginAscend() {
	ascending_ = true;
	ascendTimer_ = 0.0f;
}

void EtherPillarFlow::Update(float deltaTime) {
	time_ += deltaTime;
	arrivals_ = 0;
	topArrivals_ = 0;
	const int lines = std::clamp(lineCount_, 1, kMaxLines);

	// 下から弱い光が昇る（泉の気配）
	idleTimer_ -= deltaTime;
	if (idleTimer_ <= 0.0f && idleScale_ > 0.0f) {
		idleTimer_ = idlePulseInterval_ * Rand(kIntervalJitterMin, kIntervalJitterMax);
		pulses_.push_back({ static_cast<int>(Rand(0.0f, static_cast<float>(lines) - kLineIndexEpsilon)), totalLength_,
			-idlePulseSpeed_ * Rand(kIdleSpeedJitterMin, kIdleSpeedJitterMax), idlePulseIntensity_ * idleScale_ * Rand(kIdleIntensityJitterMin, 1.0f), pulseLength_ });
	}
	// 上から明るい光が次々に下りる
	if (descending_) {
		descendTimer_ -= deltaTime;
		while (descendTimer_ <= 0.0f) {
			descendTimer_ += (std::max)(descendInterval_, kMinInterval) * Rand(kIntervalJitterMin, kIntervalJitterMax);
			pulses_.push_back({ static_cast<int>(Rand(0.0f, static_cast<float>(lines) - kLineIndexEpsilon)), 0.0f,
				descendSpeed_ * Rand(kSpeedJitterMin, kSpeedJitterMax), descendIntensity_ * Rand(kIntensityJitterMin, 1.0f), pulseLength_ * Rand(kLengthJitterMin, kLengthJitterMax) });
		}
	}
	// 下から明るい光が次々に昇る（賭ける。速さ・間隔・明るさは下りると同じ値）
	if (ascending_) {
		ascendTimer_ -= deltaTime;
		while (ascendTimer_ <= 0.0f) {
			ascendTimer_ += (std::max)(descendInterval_, kMinInterval) * Rand(kIntervalJitterMin, kIntervalJitterMax);
			pulses_.push_back({ static_cast<int>(Rand(0.0f, static_cast<float>(lines) - kLineIndexEpsilon)), totalLength_,
				-descendSpeed_ * Rand(kSpeedJitterMin, kSpeedJitterMax), descendIntensity_ * Rand(kIntensityJitterMin, 1.0f), pulseLength_ * Rand(kLengthJitterMin, kLengthJitterMax) });
		}
	}
	for (Pulse& p : pulses_) p.position += p.speed * deltaTime;
	const float limit = totalLength_;
	pulses_.erase(std::remove_if(pulses_.begin(), pulses_.end(), [this, limit](const Pulse& p) {
		if (p.speed > 0.0f && p.position - p.length > limit) {
			++arrivals_;
			return true;
		}
		if (p.speed < 0.0f && p.position + p.length < 0.0f) {
			// 泉の気配の弱い光は数えない
			if (p.intensity > idlePulseIntensity_ * kIdleIntensityMargin) ++topArrivals_;
			return true;
		}
		return false;
	}), pulses_.end());
}

void EtherPillarFlow::BuildVertices(const Vector3& cameraPos, const Vector3& etherColor) {
	const Vector3 core = {
		etherColor.x + (1.0f - etherColor.x) * coreWhite_,
		etherColor.y + (1.0f - etherColor.y) * coreWhite_,
		etherColor.z + (1.0f - etherColor.z) * coreWhite_,
	};
	const int lines = std::clamp(lineCount_, 1, kMaxLines);
	const float breath = kBreathMid + kBreathAmplitude * std::sin(time_ * idleBreathSpeed_);
	for (int l = 0; l < kMaxLines; ++l) {
		MeshVertex* v = &vertices_[static_cast<size_t>(l) * kVertsPerLine];
		if (l >= lines) {
			for (int i = 0; i < kVertsPerLine; ++i) {
				v[i].position = origin_;
				v[i].color = { 0.0f, 0.0f, 0.0f, 0.0f };
			}
			continue;
		}
		const float angle = (angleOffsetDeg_ * kPi / kDegreesPerHalfTurn) + 2.0f * kPi * static_cast<float>(l) / static_cast<float>(lines);
		Vector3 points[kSamples];
		for (int i = 0; i < kSamples; ++i) {
			points[i] = PointAt(totalLength_ * static_cast<float>(i) / static_cast<float>(kSamples - 1), angle);
		}
		for (int i = 0; i < kSamples; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(kSamples - 1);
			const float s = totalLength_ * u;
			// 両端はふっと消える
			const float envelope = std::sqrt((std::max)(std::sin(u * kPi), 0.0f));
			float k = idleIntensity_ * idleScale_ * breath;
			for (const Pulse& p : pulses_) {
				if (p.line != l) continue;
				const float d = (s - p.position) / (std::max)(p.length, kMinPulseLength);
				k += p.intensity * std::exp(-d * d);
			}
			k *= envelope;
			const Vector3 tangent = SafeNormalize(Sub(points[(std::min)(i + 1, kSamples - 1)], points[(std::max)(i - 1, 0)]),
				{ 0.0f, -1.0f, 0.0f });
			const Vector3 view = SafeNormalize(Sub(cameraPos, points[i]), { 0.0f, 0.0f, -1.0f });
			const Vector3 side = Mul(SafeNormalize(Cross(tangent, view), { 1.0f, 0.0f, 0.0f }), width_ * 0.5f);
			MeshVertex& left = v[i * kVertsPerPoint];
			MeshVertex& mid = v[i * kVertsPerPoint + 1];
			MeshVertex& right = v[i * kVertsPerPoint + 2];
			left.position = Sub(points[i], side);
			mid.position = points[i];
			right.position = Add(points[i], side);
			left.color = { etherColor.x * k * 0.5f, etherColor.y * k * 0.5f, etherColor.z * k * 0.5f, 0.0f };
			right.color = left.color;
			mid.color = { core.x * k, core.y * k, core.z * k, (std::min)(k, 1.0f) };
			left.texcoord = { 0.0f, 0.0f };
			mid.texcoord = { 0.5f, 0.0f };
			right.texcoord = { 1.0f, 0.0f };
			left.normal = mid.normal = right.normal = view;
		}
	}
}

void EtherPillarFlow::Draw(Camera* camera, const Vector3& etherColor) {
	built_ = false;
	if (!mesh_ || !camera || profile_.size() < kMinProfilePoints) return;
	BuildVertices(camera->GetTranslate(), etherColor);
	mesh_->UpdateVertices(vertices_.data(), static_cast<uint32_t>(vertices_.size()));
	mesh_->Update(camera, 0.0f);
	mesh_->Draw();
	built_ = true;
}

void EtherPillarFlow::DrawBloom() {
	if (mesh_ && built_) mesh_->DrawBloomPass(bloomIntensity_);
}

void EtherPillarFlow::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	lineCount_ = static_cast<int>(node["lineCount"].AsDouble(static_cast<double>(lineCount_)));
	read("angleOffsetDeg", angleOffsetDeg_);
	read("surfaceLift", surfaceLift_);
	read("width", width_);
	read("idleIntensity", idleIntensity_);
	read("idleBreathSpeed", idleBreathSpeed_);
	read("idlePulseInterval", idlePulseInterval_);
	read("idlePulseSpeed", idlePulseSpeed_);
	read("idlePulseIntensity", idlePulseIntensity_);
	read("descendInterval", descendInterval_);
	read("descendSpeed", descendSpeed_);
	read("descendIntensity", descendIntensity_);
	read("pulseLength", pulseLength_);
	read("coreWhite", coreWhite_);
	read("bloomIntensity", bloomIntensity_);
	const JsonValue& profile = node["profile"];
	if (profile.IsArray() && profile.Size() >= kMinProfilePoints) {
		profile_.clear();
		for (size_t i = 0; i < profile.Size() && i < static_cast<size_t>(kMaxProfilePoints); ++i) {
			const JsonValue& p = profile[i];
			if (!p.IsArray() || p.Size() < kVector2ComponentCount) continue;
			profile_.push_back({ static_cast<float>(p[0].AsDouble()), static_cast<float>(p[1].AsDouble()) });
		}
	}
	RebuildProfile();
}

void EtherPillarFlow::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	node["_comment"] = JsonValue(std::string("profile は道の輪郭 [高さ, 半径]（器の原点から、上から下へ）"));
	node["lineCount"] = JsonValue(static_cast<double>(lineCount_));
	write("angleOffsetDeg", angleOffsetDeg_);
	write("surfaceLift", surfaceLift_);
	write("width", width_);
	write("idleIntensity", idleIntensity_);
	write("idleBreathSpeed", idleBreathSpeed_);
	write("idlePulseInterval", idlePulseInterval_);
	write("idlePulseSpeed", idlePulseSpeed_);
	write("idlePulseIntensity", idlePulseIntensity_);
	write("descendInterval", descendInterval_);
	write("descendSpeed", descendSpeed_);
	write("descendIntensity", descendIntensity_);
	write("pulseLength", pulseLength_);
	write("coreWhite", coreWhite_);
	write("bloomIntensity", bloomIntensity_);
	JsonValue profile = JsonValue::MakeArray();
	for (const Vector2& p : profile_) {
		JsonValue point = JsonValue::MakeArray();
		point.Push(JsonValue(static_cast<double>(p.x)));
		point.Push(JsonValue(static_cast<double>(p.y)));
		profile.Push(point);
	}
	node["profile"] = profile;
}

void EtherPillarFlow::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("pulses %d  path %.3f m", static_cast<int>(pulses_.size()), totalLength_);
	if (ImGui::Button("Descend##PillarFlow")) BeginDescend();
	ImGui::SameLine();
	if (ImGui::Button("Stop##PillarFlow")) StopDescend();
	ImGui::SliderInt("Lines", &lineCount_, 1, kMaxLines);
	ImGui::DragFloat("Angle Offset [deg]", &angleOffsetDeg_, 0.5f, -180.0f, 180.0f);
	ImGui::DragFloat("Surface Lift [m]", &surfaceLift_, 0.0005f, 0.0f, 0.1f, "%.4f");
	ImGui::DragFloat("Width [m]", &width_, 0.0005f, 0.0005f, 0.1f, "%.4f");
	ImGui::DragFloat("Idle Intensity", &idleIntensity_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Idle Breath Speed", &idleBreathSpeed_, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Idle Pulse Interval [s]", &idlePulseInterval_, 0.05f, 0.05f, 10.0f);
	ImGui::DragFloat("Idle Pulse Speed [m/s]", &idlePulseSpeed_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Idle Pulse Intensity", &idlePulseIntensity_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Descend Interval [s]", &descendInterval_, 0.005f, 0.005f, 2.0f);
	ImGui::DragFloat("Descend Speed [m/s]", &descendSpeed_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Descend Intensity", &descendIntensity_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Pulse Length [m]", &pulseLength_, 0.005f, 0.005f, 1.0f);
	ImGui::SliderFloat("Core White##PillarFlow", &coreWhite_, 0.0f, 1.0f);
	ImGui::DragFloat("Bloom Intensity##PillarFlow", &bloomIntensity_, 0.05f, 0.0f, 20.0f);
	ImGui::SeparatorText("Path profile [height, radius]");
	bool changed = false;
	for (size_t i = 0; i < profile_.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		changed |= ImGui::DragFloat2("##point", &profile_[i].x, 0.002f, -1.0f, 5.0f, "%.3f");
		ImGui::PopID();
	}
	if (profile_.size() < static_cast<size_t>(kMaxProfilePoints) && ImGui::Button("Add Point")) {
		profile_.push_back(profile_.empty() ? Vector2{ 0.0f, 0.0f } : profile_.back());
		changed = true;
	}
	ImGui::SameLine();
	if (profile_.size() > 2 && ImGui::Button("Remove Last")) {
		profile_.pop_back();
		changed = true;
	}
	if (changed) RebuildProfile();
#endif
}
