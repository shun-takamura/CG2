#include "PlayerBeamPart.h"

#include "Effect/EffectManager.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/PlayerBeam.json";
	// JSON の字下げ
	constexpr int kJsonIndentWidth = 2;
	// 種類ごとの JSON のキー（PlayerBeamPart::Kind の順）
	constexpr const char* kKindKeys[] = { "charge1", "charge2", "funnelLarge" };
	// ImGui の見出し（PlayerBeamPart::Kind の順）
	constexpr const char* kKindLabels[] = { "Charge 1", "Charge 2", "Funnel Large" };
	// 時間の長さの下限 [秒]（0 割りを防ぐ）
	constexpr float kMinSeconds = 1e-4f;
	// 文字列の入力欄の長さ
	constexpr size_t kNameBufferSize = 128;
	// smoothstep（3t² − 2t³）の係数
	constexpr float kSmoothstepSquareCoef = 3.0f;
	constexpr float kSmoothstepCubeCoef = 2.0f;

	// ----- 既定値（JSON が無いとき）-----
	// チャージ 1 段目：今のチャージ弾 1（倍率 8）と同じくらいの合計
	constexpr float kCharge1Duration = 0.4f;
	constexpr float kCharge1Radius = 1.2f;
	constexpr float kCharge1DamageScale = 1.0f;
	// チャージ 2 段目：今のチャージ弾 2（倍率 20）と同じくらいの合計
	constexpr float kCharge2Duration = 0.8f;
	constexpr float kCharge2Radius = 2.2f;
	constexpr float kCharge2DamageScale = 1.25f;
	// ファンネル大
	constexpr float kFunnelDuration = 0.6f;
	constexpr float kFunnelRadius = 0.8f;
	constexpr float kFunnelDamageScale = 0.5f;
	constexpr float kFunnelTickInterval = 0.08f;

	/// <summary>向きをオイラー角（ピッチ→ヨー。Object3D / EffectInstance のビームの向きと同じ）にする</summary>
	/// <param name="dir">向き（正規化済み）</param>
	/// <returns>オイラー角 [ラジアン]</returns>
	Vector3 DirectionToEuler(const Vector3& dir) {
		const float yaw = std::atan2(dir.x, dir.z);
		const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
		const float pitch = std::atan2(-dir.y, horizLen);
		return { pitch, yaw, 0.0f };
	}
	/// <summary>smoothstep（3t² − 2t³）</summary>
	/// <param name="t">0〜1</param>
	/// <returns>なめらかにした値</returns>
	float Smooth01(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (kSmoothstepSquareCoef - kSmoothstepCubeCoef * t);
	}
	/// <summary>JSON の数を float で読む</summary>
	/// <param name="node">読む欄</param>
	/// <param name="key">キー</param>
	/// <param name="v">読み込み先（無ければそのまま）</param>
	void ReadFloat(const JsonValue& node, const char* key, float& v) {
		v = static_cast<float>(node[key].AsDouble(v));
	}
}

void PlayerBeamPart::Initialize(IPlayerBeamHost* host) {
	host_ = host;
	PlayerBeamProfile& c1 = profiles_[static_cast<size_t>(Kind::Charge1)];
	c1.effect = "PlayerChargeBeam1";
	c1.hitEffect = "HitPlayerBeam";
	c1.duration = kCharge1Duration;
	c1.radius = kCharge1Radius;
	c1.damageScale = kCharge1DamageScale;
	PlayerBeamProfile& c2 = profiles_[static_cast<size_t>(Kind::Charge2)];
	c2.effect = "PlayerChargeBeam2";
	c2.hitEffect = "HitPlayerBeam";
	c2.duration = kCharge2Duration;
	c2.radius = kCharge2Radius;
	c2.damageScale = kCharge2DamageScale;
	PlayerBeamProfile& fl = profiles_[static_cast<size_t>(Kind::FunnelLarge)];
	fl.effect = "FunnelBeam";
	fl.hitEffect = "HitPlayerBeam";
	fl.duration = kFunnelDuration;
	fl.radius = kFunnelRadius;
	fl.damageScale = kFunnelDamageScale;
	fl.tickInterval = kFunnelTickInterval;
	LoadTuning();
}

PlayerBeamPart::BeamId PlayerBeamPart::Fire(Kind kind, const Vector3& from, const Vector3& dir, int attackPower, float radiusScale) {
	if (kind == Kind::Count) return kInvalidBeam;
	const PlayerBeamProfile& p = GetProfile(kind);
	Beam b{};
	b.id = nextId_++;
	if (nextId_ == kInvalidBeam) nextId_ = 1;
	b.kind = kind;
	b.from = from;
	b.dir = dir;
	b.radiusScale = radiusScale;
	b.damage = attackPower > 0 ? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(attackPower) * p.damageScale))) : 0;
	if (!p.effect.empty()) {
		b.effectHandle = EffectManager::GetInstance()->Play(p.effect, from);
	}
	ApplyEffectPose(b, 0.0f);
	beams_.push_back(std::move(b));
	return beams_.back().id;
}

PlayerBeamPart::Beam* PlayerBeamPart::Find(BeamId id) {
	for (auto& b : beams_) if (b.id == id) return &b;
	return nullptr;
}

void PlayerBeamPart::SetPose(BeamId id, const Vector3& from, const Vector3& dir) {
	if (Beam* b = Find(id)) {
		b->from = from;
		b->dir = dir;
	}
}

bool PlayerBeamPart::IsActive(BeamId id) const {
	for (const auto& b : beams_) if (b.id == id) return !b.ending;
	return false;
}

void PlayerBeamPart::Stop(BeamId id) {
	if (Beam* b = Find(id)) b->ending = true;
}

void PlayerBeamPart::StopAll() {
	for (auto& b : beams_) {
		if (b.effectHandle != 0) EffectManager::GetInstance()->Release(b.effectHandle);
	}
	beams_.clear();
}

void PlayerBeamPart::ApplyEffectPose(const Beam& b, float length) const {
	if (b.effectHandle == 0) return;
	EffectManager* em = EffectManager::GetInstance();
	em->SetPosition(b.effectHandle, b.from);
	em->SetRotation(b.effectHandle, DirectionToEuler(b.dir));
	em->SetBeamLength(b.effectHandle, length);
}

void PlayerBeamPart::Update(float dt) {
	if (beams_.empty()) return;
	for (auto& b : beams_) {
		const PlayerBeamProfile& p = GetProfile(b.kind);
		b.timer += dt;
		if (p.duration > 0.0f && b.timer >= p.duration) b.ending = true;
		if (b.ending) continue;

		// 判定もエフェクトと同じ速さで先まで伸びる
		const float extend = p.extendTime > kMinSeconds ? Smooth01(b.timer / p.extendTime) : 1.0f;
		const float length = p.length * extend;
		// エフェクトの長さは伸び切った長さを渡す（伸びる動きはエフェクトの Extend Time が付ける）
		ApplyEffectPose(b, p.length);

		for (auto& kv : b.cooldowns) kv.second -= dt;
		if (!host_ || b.damage <= 0 || length <= 0.0f) continue;
		const Vector3 to{ b.from.x + b.dir.x * length, b.from.y + b.dir.y * length, b.from.z + b.dir.z * length };
		hitsScratch_.clear();
		host_->CollectBeamHits(b.from, to, p.radius * b.radiusScale, hitsScratch_);
		for (const PlayerBeamHit& hit : hitsScratch_) {
			IImGuiEditable* root = hit.damageRoot ? hit.damageRoot : hit.target;
			auto it = b.cooldowns.find(root);
			const bool firstHit = (it == b.cooldowns.end());
			if (!firstHit && it->second > 0.0f) continue;
			host_->ApplyBeamHit(hit, b.damage, p.hitEffect, firstHit);
			b.cooldowns[root] = p.tickInterval;
		}
	}

	// 撃ち終えたビームはエフェクトを細くして消し、一覧から外す
	for (auto it = beams_.begin(); it != beams_.end();) {
		if (!it->ending) { ++it; continue; }
		if (it->effectHandle != 0) EffectManager::GetInstance()->Release(it->effectHandle);
		it = beams_.erase(it);
	}
}

bool PlayerBeamPart::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	for (size_t i = 0; i < profiles_.size(); ++i) {
		const JsonValue& n = root[kKindKeys[i]];
		if (!n.IsObject()) continue;
		PlayerBeamProfile& p = profiles_[i];
		p.effect = n["effect"].AsString(p.effect);
		p.hitEffect = n["hitEffect"].AsString(p.hitEffect);
		ReadFloat(n, "duration", p.duration);
		ReadFloat(n, "length", p.length);
		ReadFloat(n, "radius", p.radius);
		ReadFloat(n, "extendTime", p.extendTime);
		ReadFloat(n, "tickInterval", p.tickInterval);
		ReadFloat(n, "damageScale", p.damageScale);
	}
	return true;
}

void PlayerBeamPart::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("自機のビーム。1 回のダメージ＝攻撃力 × damageScale、同じ敵へは tickInterval 秒ごと。見た目は effect のエフェクト"));
	for (size_t i = 0; i < profiles_.size(); ++i) {
		const PlayerBeamProfile& p = profiles_[i];
		JsonValue n = JsonValue::MakeObject();
		n["effect"] = JsonValue(p.effect);
		n["hitEffect"] = JsonValue(p.hitEffect);
		n["duration"] = JsonValue(static_cast<double>(p.duration));
		n["length"] = JsonValue(static_cast<double>(p.length));
		n["radius"] = JsonValue(static_cast<double>(p.radius));
		n["extendTime"] = JsonValue(static_cast<double>(p.extendTime));
		n["tickInterval"] = JsonValue(static_cast<double>(p.tickInterval));
		n["damageScale"] = JsonValue(static_cast<double>(p.damageScale));
		root[kKindKeys[i]] = std::move(n);
	}
	std::filesystem::path path(kTuningPath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	JsonWriter::WriteFile(kTuningPath, root, { true, kJsonIndentWidth });
}

void PlayerBeamPart::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Active beams: %d", static_cast<int>(beams_.size()));
	for (size_t i = 0; i < profiles_.size(); ++i) {
		PlayerBeamProfile& p = profiles_[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(kKindLabels[i])) {
			char buf[kNameBufferSize];
			std::snprintf(buf, sizeof(buf), "%s", p.effect.c_str());
			if (ImGui::InputText("Effect", buf, sizeof(buf))) p.effect = buf;
			std::snprintf(buf, sizeof(buf), "%s", p.hitEffect.c_str());
			if (ImGui::InputText("Hit Effect", buf, sizeof(buf))) p.hitEffect = buf;
			ImGui::DragFloat("Duration", &p.duration, 0.01f, 0.0f, 10.0f, "%.2f s");
			ImGui::DragFloat("Length", &p.length, 1.0f, 0.0f, 2000.0f, "%.0f m");
			ImGui::DragFloat("Radius", &p.radius, 0.02f, 0.0f, 20.0f, "%.2f m");
			ImGui::DragFloat("Extend Time", &p.extendTime, 0.005f, 0.0f, 2.0f, "%.3f s");
			ImGui::DragFloat("Tick Interval", &p.tickInterval, 0.005f, 0.01f, 2.0f, "%.3f s");
			ImGui::DragFloat("Damage Scale", &p.damageScale, 0.01f, 0.0f, 20.0f, "%.2f x atk / tick");
			const float ticks = p.tickInterval > kMinSeconds ? p.duration / p.tickInterval : 0.0f;
			ImGui::TextDisabled("1 体への合計 ≒ 攻撃力 × %.1f", ticks * p.damageScale);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Save##PlayerBeam")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Load##PlayerBeam")) LoadTuning();
#endif
}
