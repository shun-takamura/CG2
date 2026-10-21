#include "EtherStrands.h"

#include "Camera.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/MeshData.h"
#include "Json/JsonValue.h"
#include "MathUtility.h"   // kPi
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kWhiteTexture = "Resources/Textures/white1x1.dds";
	constexpr int kPointsPerStrand = EtherStrands::kSegments + 1;
	constexpr int kVertsPerStrand = kPointsPerStrand * 3;   // 帯の左の縁・芯・右の縁

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
}

EtherStrands::EtherStrands() = default;
EtherStrands::~EtherStrands() = default;

void EtherStrands::Initialize() {
	strands_.assign(kMaxStrands, Strand{});

	MeshData data;
	data.vertices.resize(static_cast<size_t>(kMaxStrands) * kVertsPerStrand);
	for (auto& v : data.vertices) v.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (uint32_t s = 0; s < static_cast<uint32_t>(kMaxStrands); ++s) {
		const uint32_t base = s * kVertsPerStrand;
		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(kPointsPerStrand); ++i) {
			const uint32_t p = base + i * 3;
			const uint32_t q = p + 3;
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

void EtherStrands::Reset() {
	for (Strand& s : strands_) s.alive = false;
	nextSlot_ = 0;
	built_ = false;
}

float EtherStrands::Rand(float lo, float hi) {
	if (hi < lo) std::swap(lo, hi);
	std::uniform_real_distribution<float> d(lo, hi);
	return d(random_);
}

void EtherStrands::Emit(const Vector3& root, float scale) {
	if (strands_.empty()) return;
	// 空きを前回の続きから探す（埋まっていたら出さない）
	for (int n = 0; n < kMaxStrands; ++n) {
		Strand& s = strands_[(nextSlot_ + n) % kMaxStrands];
		if (s.alive) continue;
		nextSlot_ = (nextSlot_ + n + 1) % kMaxStrands;
		s.alive = true;
		s.root = Sub(root, anchor_);
		s.scale = (std::max)(scale, 1e-3f);
		s.age = 0.0f;
		s.life = Rand(lifeMin_, lifeMax_);
		s.speed = Rand(speedMin_, speedMax_);
		s.length = Rand(lengthMin_, lengthMax_);
		s.width = Rand(widthMin_, widthMax_);
		s.brightness = Rand(0.6f, 1.0f);
		s.amplitude = amplitude_ * Rand(0.5f, 1.5f);
		s.freqA = Rand(frequencyMin_, frequencyMax_);
		s.freqB = Rand(frequencyMin_, frequencyMax_);
		s.phaseA = Rand(0.0f, 2.0f * kPi);
		s.phaseB = Rand(0.0f, 2.0f * kPi);
		const float a = Rand(0.0f, 2.0f * kPi);
		s.flareDir = { std::cos(a), 0.0f, std::sin(a) };
		return;
	}
}

void EtherStrands::Update(float deltaTime) {
	time_ += deltaTime;
	for (Strand& s : strands_) {
		if (!s.alive) continue;
		s.age += deltaTime;
		if (s.age >= s.life) s.alive = false;
	}
}

bool EtherStrands::HasAlive() const {
	for (const Strand& s : strands_) {
		if (s.alive) return true;
	}
	return false;
}

Vector3 EtherStrands::SwayAt(const Strand& s, float h) const {
	// 細かさは体の大きさ 1 の単位で持つので、高さを体の大きさで割ってから掛ける
	const float hu = h / s.scale;
	const float amp = (s.amplitude + amplitudeGrow_ * hu) * s.scale;
	const float x = std::sin(s.freqA * hu - time_ * swaySpeed_ + s.phaseA) * amp;
	const float z = std::sin(s.freqB * hu - time_ * swaySpeed_ * 1.3f + s.phaseB) * amp;
	return Add(Mul(s.flareDir, flare_ * h), { x, 0.0f, z });
}

void EtherStrands::BuildVertices(const Vector3& cameraPos, const Vector3& etherColor) {
	const Vector3 core = {
		etherColor.x + (1.0f - etherColor.x) * coreWhite_,
		etherColor.y + (1.0f - etherColor.y) * coreWhite_,
		etherColor.z + (1.0f - etherColor.z) * coreWhite_,
	};
	for (int n = 0; n < kMaxStrands; ++n) {
		const Strand& s = strands_[n];
		MeshVertex* v = &vertices_[static_cast<size_t>(n) * kVertsPerStrand];
		if (!s.alive) {
			for (int i = 0; i < kVertsPerStrand; ++i) {
				v[i].position = anchor_;
				v[i].color = { 0.0f, 0.0f, 0.0f, 0.0f };
			}
			continue;
		}
		// 頭は加速しながら昇り、伸び切ると尾も根元を離れて昇る
		const float head = (s.speed * s.age + 0.5f * acceleration_ * s.age * s.age) * s.scale;
		const float tail = (std::max)(head - s.length * s.scale, 0.0f);
		const Vector3 base = Add(anchor_, s.root);
		// 出てすぐ明るく、消える前に暗く
		const float fadeIn = fadeInSeconds_ > 1e-3f ? (std::min)(s.age / fadeInSeconds_, 1.0f) : 1.0f;
		const float t = s.age / (std::max)(s.life, 1e-3f);
		const float fade = fadeIn * (1.0f - t * t);

		Vector3 points[kPointsPerStrand];
		for (int i = 0; i < kPointsPerStrand; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(kSegments);
			const float h = tail + (head - tail) * u;
			points[i] = Add(Add(base, { 0.0f, h, 0.0f }), SwayAt(s, h));
		}
		for (int i = 0; i < kPointsPerStrand; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(kSegments);
			// 両端を細く・暗く（糸の先がふっと消える）
			const float envelope = std::sqrt((std::max)(std::sin(u * kPi), 0.0f));
			const Vector3 tangent = SafeNormalize(Sub(points[(std::min)(i + 1, kSegments)], points[(std::max)(i - 1, 0)]),
				{ 0.0f, 1.0f, 0.0f });
			const Vector3 view = SafeNormalize(Sub(cameraPos, points[i]), { 0.0f, 0.0f, -1.0f });
			const float halfWidth = s.width * s.scale * (0.35f + 0.65f * envelope) * 0.5f;
			const Vector3 side = Mul(SafeNormalize(Cross(tangent, view), { 1.0f, 0.0f, 0.0f }), halfWidth);
			const float k = brightness_ * s.brightness * fade * envelope;
			MeshVertex& left = v[i * 3];
			MeshVertex& mid = v[i * 3 + 1];
			MeshVertex& right = v[i * 3 + 2];
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

void EtherStrands::Draw(Camera* camera, const Vector3& etherColor) {
	built_ = false;
	if (!mesh_ || !camera || !HasAlive()) return;
	BuildVertices(camera->GetTranslate(), etherColor);
	mesh_->UpdateVertices(vertices_.data(), static_cast<uint32_t>(vertices_.size()));
	mesh_->Update(camera, 0.0f);
	mesh_->Draw();
	built_ = true;
}

void EtherStrands::DrawBloom() {
	if (mesh_ && built_) mesh_->DrawBloomPass(bloomIntensity_);
}

void EtherStrands::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("lifeMin", lifeMin_);
	read("lifeMax", lifeMax_);
	read("speedMin", speedMin_);
	read("speedMax", speedMax_);
	read("acceleration", acceleration_);
	read("lengthMin", lengthMin_);
	read("lengthMax", lengthMax_);
	read("widthMin", widthMin_);
	read("widthMax", widthMax_);
	read("amplitude", amplitude_);
	read("amplitudeGrow", amplitudeGrow_);
	read("frequencyMin", frequencyMin_);
	read("frequencyMax", frequencyMax_);
	read("swaySpeed", swaySpeed_);
	read("flare", flare_);
	read("coreWhite", coreWhite_);
	read("brightness", brightness_);
	read("fadeInSeconds", fadeInSeconds_);
	read("bloomIntensity", bloomIntensity_);
}

void EtherStrands::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("lifeMin", lifeMin_);
	write("lifeMax", lifeMax_);
	write("speedMin", speedMin_);
	write("speedMax", speedMax_);
	write("acceleration", acceleration_);
	write("lengthMin", lengthMin_);
	write("lengthMax", lengthMax_);
	write("widthMin", widthMin_);
	write("widthMax", widthMax_);
	write("amplitude", amplitude_);
	write("amplitudeGrow", amplitudeGrow_);
	write("frequencyMin", frequencyMin_);
	write("frequencyMax", frequencyMax_);
	write("swaySpeed", swaySpeed_);
	write("flare", flare_);
	write("coreWhite", coreWhite_);
	write("brightness", brightness_);
	write("fadeInSeconds", fadeInSeconds_);
	write("bloomIntensity", bloomIntensity_);
}

void EtherStrands::OnImGui() {
#ifdef _DEBUG
	int alive = 0;
	for (const Strand& s : strands_) alive += s.alive ? 1 : 0;
	ImGui::Text("strands %d / %d", alive, kMaxStrands);
	ImGui::DragFloat("Life Min (s)", &lifeMin_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Life Max (s)", &lifeMax_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Speed Min (m/s)", &speedMin_, 0.01f, 0.0f, 20.0f);
	ImGui::DragFloat("Speed Max (m/s)", &speedMax_, 0.01f, 0.0f, 20.0f);
	ImGui::DragFloat("Acceleration (m/s2)", &acceleration_, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("Length Min (m)", &lengthMin_, 0.01f, 0.05f, 20.0f);
	ImGui::DragFloat("Length Max (m)", &lengthMax_, 0.01f, 0.05f, 20.0f);
	ImGui::DragFloat("Width Min (m)", &widthMin_, 0.001f, 0.001f, 0.5f);
	ImGui::DragFloat("Width Max (m)", &widthMax_, 0.001f, 0.001f, 0.5f);
	ImGui::DragFloat("Amplitude (m)", &amplitude_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Amplitude Grow (m per m)", &amplitudeGrow_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Frequency Min (rad/m)", &frequencyMin_, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Frequency Max (rad/m)", &frequencyMax_, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Sway Speed (rad/s)", &swaySpeed_, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Flare (m per m)", &flare_, 0.005f, 0.0f, 2.0f);
	ImGui::SliderFloat("Core White", &coreWhite_, 0.0f, 1.0f);
	ImGui::DragFloat("Brightness", &brightness_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Fade In (s)", &fadeInSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Intensity", &bloomIntensity_, 0.05f, 0.0f, 20.0f);
#endif // _DEBUG
}
