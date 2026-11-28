#include "MeleeComboPart.h"

#include "InputAction.h"
#include "Config/GameActions.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/MeleeCombo.json";
	// JSON の字下げ
	constexpr int kJsonIndentWidth = 2;
	// 技のプレハブが空・見つからない時に使うプレハブ
	constexpr const char* kFallbackPrefab = "TemporaryPlayerMelee";
	// 既定の基本技のプレハブ（B=今までの弱 / Y=今までの強）
	constexpr const char* kDefaultPrefabB = "TemporaryPlayerMelee";
	constexpr const char* kDefaultPrefabY = "PlayerMeleeStrong";
	// 既定の必殺技ゲージの増え方（今までの StagePlay.json の弱/強の既定値）
	constexpr float kDefaultGaugeGainB = 3.0f;
	constexpr float kDefaultGaugeGainY = 2.0f;
	// 既定の溜め攻撃：段階ごとの溜める時間 [秒]・威力の倍率・ゲージ
	constexpr std::array<float, MeleeChargeSet::kLevelCount> kDefaultChargeHold = { 0.0f, 0.6f, 1.2f };
	constexpr std::array<float, MeleeChargeSet::kLevelCount> kDefaultChargeGauge = { 4.0f, 6.0f, 8.0f };
	constexpr std::array<std::array<float, MeleeChargeSet::kLevelCount>, static_cast<size_t>(MeleeComboPart::ChargeKind::Count)>
		kDefaultChargeDamage = { {
			{ 1.5f, 2.0f, 3.0f },  // 単独
			{ 2.0f, 2.8f, 4.0f },  // B4 段目から
			{ 2.5f, 3.5f, 5.0f },  // Y4 段目から
			{ 1.5f, 2.0f, 2.8f },  // RT+Y+B の大技（爆発）
		} };
	// 既定の、大技の回転斬りが同じ相手にもう一度当たれるまでの実時間 [秒]（銃のチャージ 無し / 1 段階 / 2 段階）。
	// 無しは回転斬りの長さより長くして 1 回だけ
	constexpr std::array<float, MeleeComboPart::kGunChargeLevels> kDefaultBurstRehit = { 0.5f, 0.18f, 0.09f };
	// 既定の強化（単独の溜め）：段階ごとの攻撃力・移動の速さ・ジャスト回避の受付の延長 [秒]・続く時間 [秒]
	constexpr std::array<float, MeleeChargeSet::kLevelCount> kDefaultBuffAttack = { 1.1f, 1.15f, 1.175f };
	constexpr std::array<float, MeleeChargeSet::kLevelCount> kDefaultBuffMove = { 1.075f, 1.09f, 1.1f };
	constexpr float kDefaultBuffJustWindowAdd = 0.05f;
	constexpr std::array<float, MeleeChargeSet::kLevelCount> kDefaultBuffDuration = { 15.0f, 18.0f, 20.0f };
	// 既定の、強化が切れた後に掛け直せない時間 [秒] と、かける時のクリップの間の行動禁止 [秒]
	constexpr float kDefaultBuffLockout = 5.0f;
	constexpr float kDefaultBuffReleaseLock = 0.85f;
	// 既定の、強化中のビームの刃の色（赤く戦闘的に）
	constexpr Vector3 kDefaultBuffBladeCore{ 1.0f, 0.12f, 0.06f };
	constexpr Vector3 kDefaultBuffBladeEdge{ 1.0f, 0.55f, 0.35f };
	// 色の成分の数
	constexpr size_t kColorComponents = 3;

	/// <summary>[r, g, b] の配列を読む（無い・短い時は元のまま）</summary>
	/// <param name="node">JSON の配列</param>
	/// <param name="v">読んだ色を書く先</param>
	void ReadColor(const JsonValue& node, Vector3& v) {
		if (!node.IsArray() || node.Size() < kColorComponents) return;
		v.x = static_cast<float>(node[0].AsDouble(v.x));
		v.y = static_cast<float>(node[1].AsDouble(v.y));
		v.z = static_cast<float>(node[2].AsDouble(v.z));
	}

	/// <summary>色を [r, g, b] の配列にする</summary>
	/// <param name="v">色</param>
	/// <returns>JSON の配列</returns>
	JsonValue MakeColor(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
	// 溜め攻撃の種類ごとの JSON のキーと ImGui の見出し（ChargeKind の順）
	constexpr const char* kChargeKeys[] = { "solo", "afterB4", "afterY4", "burst" };
	constexpr const char* kChargeLabels[] = { "Charge Solo (Buff)", "Charge After B4", "Charge After Y4", "Charge Burst (RT+Y+B)" };

	// 押した順番の文字
	constexpr char kRouteCharB = 'B';
	constexpr char kRouteCharY = 'Y';
	// 文字列の入力欄の長さ
	constexpr size_t kTextBufferSize = 256;

	/// <summary>ボタンを押した順番の 1 文字にする</summary>
	/// <param name="b">ボタン</param>
	/// <returns>文字</returns>
	char RouteChar(MeleeButton b) {
		return b == MeleeButton::Y ? kRouteCharY : kRouteCharB;
	}

	/// <summary>押した順番として正しいか（B と Y だけで 1〜kMaxStage 文字）</summary>
	/// <param name="route">押した順番</param>
	/// <returns>正しければ true</returns>
	bool IsValidRoute(const std::string& route) {
		if (route.empty() || route.size() > static_cast<size_t>(MeleeComboPart::kMaxStage)) return false;
		return std::all_of(route.begin(), route.end(), [](char c) { return c == kRouteCharB || c == kRouteCharY; });
	}

	// 攻撃中の移動の種類の JSON の名前（MeleeMotionMode の順）
	constexpr const char* kMotionModeNames[] = { "none", "forward", "back", "free" };
	constexpr int kMotionModeCount = static_cast<int>(sizeof(kMotionModeNames) / sizeof(kMotionModeNames[0]));

	void ReadFloat(const JsonValue& node, const char* key, float& v) {
		v = static_cast<float>(node[key].AsDouble(v));
	}

	/// <summary>攻撃中の移動の種類を名前から（知らない名前は None）</summary>
	/// <param name="name">JSON の名前</param>
	/// <returns>種類</returns>
	MeleeMotionMode MotionModeFromName(const std::string& name) {
		for (int i = 0; i < kMotionModeCount; ++i) {
			if (name == kMotionModeNames[i]) return static_cast<MeleeMotionMode>(i);
		}
		return MeleeMotionMode::None;
	}

	void ReadMove(const JsonValue& n, MeleeMove& m) {
		if (!n.IsObject()) return;
		m.prefab = n["prefab"].AsString(m.prefab);
		ReadFloat(n, "damageScale", m.damageScale);
		ReadFloat(n, "gaugeGain", m.gaugeGain);
		m.clip = n["clip"].AsString(m.clip);
		ReadFloat(n, "clipSpeed", m.clipSpeed);

		m.hits.clear();
		const JsonValue& hits = n["hits"];
		if (hits.IsArray()) {
			for (size_t i = 0; i < hits.Size(); ++i) {
				const JsonValue& h = hits[i];
				if (!h.IsObject()) continue;
				MeleeExtraHit hit;
				ReadFloat(h, "time", hit.time);
				hit.prefab = h["prefab"].AsString(hit.prefab);
				ReadFloat(h, "damageScale", hit.damageScale);
				m.hits.push_back(std::move(hit));
			}
			std::sort(m.hits.begin(), m.hits.end(),
				[](const MeleeExtraHit& a, const MeleeExtraHit& b) { return a.time < b.time; });
		}

		m.motions.clear();
		const JsonValue& motions = n["motions"];
		if (motions.IsArray()) {
			for (size_t i = 0; i < motions.Size(); ++i) {
				const JsonValue& mo = motions[i];
				if (!mo.IsObject()) continue;
				MeleeMotion motion;
				ReadFloat(mo, "start", motion.start);
				ReadFloat(mo, "end", motion.end);
				motion.mode = MotionModeFromName(mo["mode"].AsString(std::string(kMotionModeNames[0])));
				ReadFloat(mo, "speed", motion.speed);
				m.motions.push_back(motion);
			}
		}

		m.guard = MeleeGuard{};
		const JsonValue& guard = n["guard"];
		if (guard.IsObject()) {
			ReadFloat(guard, "start", m.guard.start);
			ReadFloat(guard, "end", m.guard.end);
			ReadFloat(guard, "angle", m.guard.angle);
		}
	}

	void WriteMove(JsonValue& n, const MeleeMove& m) {
		n["prefab"] = JsonValue(m.prefab);
		n["damageScale"] = JsonValue(static_cast<double>(m.damageScale));
		n["gaugeGain"] = JsonValue(static_cast<double>(m.gaugeGain));
		n["clip"] = JsonValue(m.clip);
		n["clipSpeed"] = JsonValue(static_cast<double>(m.clipSpeed));
		// 2 回目以降の判定・移動・ガードは、ある技だけ書く
		if (!m.hits.empty()) {
			JsonValue hits = JsonValue::MakeArray();
			for (const MeleeExtraHit& hit : m.hits) {
				JsonValue h = JsonValue::MakeObject();
				h["time"] = JsonValue(static_cast<double>(hit.time));
				h["prefab"] = JsonValue(hit.prefab);
				h["damageScale"] = JsonValue(static_cast<double>(hit.damageScale));
				hits.Push(std::move(h));
			}
			n["hits"] = std::move(hits);
		}
		if (!m.motions.empty()) {
			JsonValue motions = JsonValue::MakeArray();
			for (const MeleeMotion& motion : m.motions) {
				JsonValue mo = JsonValue::MakeObject();
				mo["start"] = JsonValue(static_cast<double>(motion.start));
				mo["end"] = JsonValue(static_cast<double>(motion.end));
				mo["mode"] = JsonValue(std::string(kMotionModeNames[static_cast<int>(motion.mode)]));
				mo["speed"] = JsonValue(static_cast<double>(motion.speed));
				motions.Push(std::move(mo));
			}
			n["motions"] = std::move(motions);
		}
		if (m.guard.end > m.guard.start) {
			JsonValue guard = JsonValue::MakeObject();
			guard["start"] = JsonValue(static_cast<double>(m.guard.start));
			guard["end"] = JsonValue(static_cast<double>(m.guard.end));
			guard["angle"] = JsonValue(static_cast<double>(m.guard.angle));
			n["guard"] = std::move(guard);
		}
	}

#ifdef _DEBUG
	// ImGui の値の範囲
	constexpr float kImGuiTimeMax = 2.0f;
	constexpr float kImGuiHoldMax = 5.0f;
	constexpr float kImGuiScaleMax = 20.0f;
	constexpr float kImGuiGaugeMax = 100.0f;
	constexpr float kImGuiSpeedMax = 5.0f;
	constexpr float kImGuiMoveTimeMax = 3.0f;
	constexpr float kImGuiMotionSpeedMax = 40.0f;
	constexpr float kImGuiGuardAngleMax = 180.0f;
	constexpr float kImGuiBuffScaleMax = 3.0f;
	constexpr float kImGuiBuffTimeMax = 60.0f;

	/// <summary>文字列の入力欄</summary>
	/// <param name="label">見出し</param>
	/// <param name="s">編集する文字列</param>
	void InputString(const char* label, std::string& s) {
		char buf[kTextBufferSize];
		std::snprintf(buf, sizeof(buf), "%s", s.c_str());
		if (ImGui::InputText(label, buf, sizeof(buf))) s = buf;
	}

	/// <summary>技 1 つ分の編集欄</summary>
	/// <param name="m">技</param>
	void DrawMove(MeleeMove& m) {
		InputString("Prefab", m.prefab);
		ImGui::DragFloat("Damage Scale", &m.damageScale, 0.01f, 0.0f, kImGuiScaleMax, "%.2f x atk");
		ImGui::DragFloat("Gauge Gain", &m.gaugeGain, 0.1f, 0.0f, kImGuiGaugeMax, "%.1f");
		InputString("Clip", m.clip);
		ImGui::DragFloat("Clip Speed", &m.clipSpeed, 0.01f, 0.0f, kImGuiSpeedMax, "%.2f");

		// 2 回目以降の判定
		if (ImGui::TreeNode("Extra Hits")) {
			for (size_t i = 0; i < m.hits.size();) {
				ImGui::PushID(static_cast<int>(i));
				MeleeExtraHit& hit = m.hits[i];
				ImGui::DragFloat("Time", &hit.time, 0.01f, 0.0f, kImGuiMoveTimeMax, "%.2f s");
				InputString("Hit Prefab", hit.prefab);
				ImGui::DragFloat("Hit Damage Scale", &hit.damageScale, 0.01f, 0.0f, kImGuiScaleMax, "%.2f x atk");
				const bool erase = ImGui::Button("Remove");
				ImGui::Separator();
				ImGui::PopID();
				if (erase) m.hits.erase(m.hits.begin() + static_cast<std::ptrdiff_t>(i));
				else ++i;
			}
			if (ImGui::Button("Add Hit")) m.hits.push_back(MeleeExtraHit{});
			ImGui::TreePop();
		}

		// 攻撃中の移動
		if (ImGui::TreeNode("Motions")) {
			for (size_t i = 0; i < m.motions.size();) {
				ImGui::PushID(static_cast<int>(i));
				MeleeMotion& motion = m.motions[i];
				ImGui::DragFloat("Start", &motion.start, 0.01f, 0.0f, kImGuiMoveTimeMax, "%.2f s");
				ImGui::DragFloat("End", &motion.end, 0.01f, 0.0f, kImGuiMoveTimeMax, "%.2f s");
				int mode = static_cast<int>(motion.mode);
				if (ImGui::Combo("Mode", &mode, kMotionModeNames, kMotionModeCount)) motion.mode = static_cast<MeleeMotionMode>(mode);
				ImGui::DragFloat("Speed", &motion.speed, 0.05f, 0.0f, kImGuiMotionSpeedMax, "%.2f");
				ImGui::TextDisabled("forward / back は m/秒、free はいつもの移動に掛ける倍率");
				const bool erase = ImGui::Button("Remove");
				ImGui::Separator();
				ImGui::PopID();
				if (erase) m.motions.erase(m.motions.begin() + static_cast<std::ptrdiff_t>(i));
				else ++i;
			}
			if (ImGui::Button("Add Motion")) m.motions.push_back(MeleeMotion{});
			ImGui::TreePop();
		}

		// 前方ガード
		if (ImGui::TreeNode("Guard")) {
			ImGui::DragFloat("Guard Start", &m.guard.start, 0.01f, 0.0f, kImGuiMoveTimeMax, "%.2f s");
			ImGui::DragFloat("Guard End", &m.guard.end, 0.01f, 0.0f, kImGuiMoveTimeMax, "%.2f s");
			ImGui::DragFloat("Guard Angle", &m.guard.angle, 1.0f, 0.0f, kImGuiGuardAngleMax, "%.0f deg");
			ImGui::TreePop();
		}
	}
#endif
}

void MeleeComboPart::Initialize(IMeleeComboHost* host) {
	host_ = host;
	for (auto& m : baseB_) { m.prefab = kDefaultPrefabB; m.gaugeGain = kDefaultGaugeGainB; }
	for (auto& m : baseY_) { m.prefab = kDefaultPrefabY; m.gaugeGain = kDefaultGaugeGainY; }
	routes_.clear();
	for (size_t k = 0; k < charges_.size(); ++k) {
		MeleeChargeSet& set = charges_[k];
		set.holdClip.clear();
		for (int i = 0; i < MeleeChargeSet::kLevelCount; ++i) {
			MeleeChargeLevel& lv = set.levels[i];
			lv.holdTime = kDefaultChargeHold[i];
			lv.move.prefab = kDefaultPrefabY;
			lv.move.damageScale = kDefaultChargeDamage[k][i];
			lv.move.gaugeGain = kDefaultChargeGauge[i];
		}
	}
	for (int i = 0; i < MeleeChargeSet::kLevelCount; ++i) {
		MeleeBuffLevel& b = buffLevels_[static_cast<size_t>(i)];
		b.attackScale = kDefaultBuffAttack[i];
		b.moveScale = kDefaultBuffMove[i];
		b.justWindowAdd = kDefaultBuffJustWindowAdd;
		b.duration = kDefaultBuffDuration[i];
	}
	buffLockout_ = kDefaultBuffLockout;
	buffReleaseLock_ = kDefaultBuffReleaseLock;
	buffBladeCoreColor_ = kDefaultBuffBladeCore;
	buffBladeEdgeColor_ = kDefaultBuffBladeEdge;
	burstRehitIntervals_ = kDefaultBurstRehit;
	LoadTuning();
	Reset();
}

void MeleeComboPart::Update(const InputActionMap* actions, float dt, bool acceptInput) {
	// ----- タイマー -----
	if (lockTimer_ > 0.0f) lockTimer_ = (std::max)(0.0f, lockTimer_ - dt);
	sinceStepStart_ += dt;
	if (bufferedB_) bufferedBAge_ += dt;
	if (bufferedY_) bufferedYAge_ += dt;
	UpdateBuff(dt);

	// 判定の出待ち：startup が過ぎたら出す
	if (pending_) {
		startupTimer_ -= dt;
		if (startupTimer_ <= 0.0f) SpawnPending();
	}
	// 2 回目以降の判定と、技の終わり
	UpdateMoveTimeline(dt);

	// 溜め中は溜めだけを進める（入力を受け付けない間は離したとみなして放つ）
	if (charging_) {
		UpdateCharge(acceptInput ? actions : nullptr, dt);
		return;
	}

	// 次を待つ時間が切れたら 1 段目に戻し、技のクリップを流していたら姿勢を戻す
	if (comboTimer_ > 0.0f) {
		comboTimer_ -= dt;
		if (comboTimer_ <= 0.0f) {
			comboTimer_ = 0.0f;
			stage_ = 0;
			route_.clear();
			if (clipPlayed_ && host_) {
				host_->RestoreComboPose();
				clipPlayed_ = false;
			}
		}
	}

	if (!actions || !acceptInput) {
		ClearBuffer();
		return;
	}

	const bool trigB = actions->IsTriggered(static_cast<int>(Action::MeleeWeak));
	const bool trigY = actions->IsTriggered(static_cast<int>(Action::MeleeStrong));

	// 同時押し：判定が出る前にもう片方が押されたら、出した段を取り消して溜めへ
	// （入れない溜め＝強化の待ち中などは取り消さず、出した段をそのまま出す）
	if (cancelable_ && pending_ && sinceStepStart_ <= chordWindow_) {
		const bool otherTrig = (stepButton_ == MeleeButton::B) ? trigY : trigB;
		const ChargeKind kind = ChordChargeKind(actions, prevStage_, prevRoute_);
		if (otherTrig && CanCharge(kind)) {
			pending_ = false;
			moveActive_ = false;
			lockTimer_ = 0.0f;
			stage_ = prevStage_;
			route_ = prevRoute_;
			comboTimer_ = prevComboTimer_;
			BeginCharge(kind);
			return;
		}
	}

	// 発生〜スキ中：終わり際の入力だけ覚えておく
	if (lockTimer_ > 0.0f) {
		if (lockTimer_ <= inputBuffer_) {
			if (trigB) { bufferedB_ = true; bufferedBAge_ = 0.0f; lastBuffered_ = MeleeButton::B; }
			if (trigY) { bufferedY_ = true; bufferedYAge_ = 0.0f; lastBuffered_ = MeleeButton::Y; }
		}
		return;
	}

	// 行動できる：技が決まるこのフレームに Y と B を両方押していれば溜め、そうでなければ次の段。
	// 押すきっかけは、今押した・受付（スキの終わりの inputBuffer 秒）の中で押した・段を出した後から押し続けている、のどれか。
	// 受付の中で両方押しても、ここで離していれば溜めにしない（間違えて Y+B になってすぐ離した時は普通の段）
	const bool pressedBoth = actions->IsPressed(static_cast<int>(Action::MeleeWeak))
		&& actions->IsPressed(static_cast<int>(Action::MeleeStrong));
	const ChargeKind chordKind = ChordChargeKind(actions, stage_, route_);
	const bool chord = pressedBoth && (trigB || trigY || bufferedB_ || bufferedY_ || holdChordOnUnlock_)
		&& CanCharge(chordKind);
	holdChordOnUnlock_ = false;
	if (chord) {
		BeginCharge(chordKind);
		return;
	}
	MeleeButton next = MeleeButton::None;
	if (trigB) next = MeleeButton::B;
	else if (trigY) next = MeleeButton::Y;
	else next = lastBuffered_;
	ClearBuffer();
	if (next != MeleeButton::None) BeginStep(next, true);
}

void MeleeComboPart::StartStep(MeleeButton button, int stage) {
	if (button == MeleeButton::None) return;
	stage = std::clamp(stage, 1, kMaxStage);
	charging_ = false;
	ClearBuffer();
	stage_ = stage - 1;
	route_.assign(static_cast<size_t>(stage_), RouteChar(button));
	BeginStep(button, false);
}

void MeleeComboPart::StartChargeAttack(ChargeKind kind, int level) {
	if (kind == ChargeKind::Count) return;
	ClearBuffer();
	chargeKind_ = kind;
	chargeLevel_ = std::clamp(level, 1, MeleeChargeSet::kLevelCount);
	chargeTimer_ = 0.0f;
	if (autoChargeTime_ <= 0.0f) {
		ReleaseCharge();
		return;
	}
	// 溜めのクリップを、最後の段階に着くまでの時間が autoChargeTime_ になる速さで流してから放つ
	autoCharge_ = true;
	autoLevel_ = chargeLevel_;
	charging_ = true;
	pending_ = false;
	moveActive_ = false;
	lockTimer_ = 0.0f;
	cancelable_ = false;
	if (kind == ChargeKind::Burst) TakeBurstGunCharge();
	if (host_) {
		const MeleeChargeSet& set = charges_[static_cast<size_t>(kind)];
		const float fullHold = set.levels[MeleeChargeSet::kLevelCount - 1].holdTime;
		if (!set.holdClip.empty()) {
			const float speed = (fullHold > 0.0f) ? fullHold / autoChargeTime_ : 1.0f;
			host_->PlayComboClip(set.holdClip, speed, clipFade_, true);
			clipPlayed_ = true;
		}
		host_->OnComboAttackStarted();
	}
}

void MeleeComboPart::ResetCombo() {
	stage_ = 0;
	route_.clear();
}

void MeleeComboPart::Reset() {
	ResetCombo();
	comboTimer_ = 0.0f;
	lockTimer_ = 0.0f;
	startupTimer_ = 0.0f;
	pending_ = false;
	pendingPrefab_.clear();
	moveActive_ = false;
	currentGaugeGain_ = 0.0f;
	cancelable_ = false;
	stepButton_ = MeleeButton::None;
	charging_ = false;
	chargeTimer_ = 0.0f;
	chargeLevel_ = 0;
	autoCharge_ = false;
	buffLevel_ = 0;
	buffTimer_ = 0.0f;
	buffLockoutTimer_ = 0.0f;
	ClearBuffer();
	if (clipPlayed_ && host_) host_->RestoreComboPose();
	clipPlayed_ = false;
}

std::string MeleeComboPart::GetReachPrefab() const {
	return baseB_[0].prefab.empty() ? std::string(kFallbackPrefab) : baseB_[0].prefab;
}

const MeleeMove& MeleeComboPart::FindMove(MeleeButton button, int stage, const std::string& route) const {
	for (const auto& r : routes_) {
		if (r.first == route) return r.second;
	}
	const auto& base = (button == MeleeButton::Y) ? baseY_ : baseB_;
	return base[static_cast<size_t>(std::clamp(stage, 1, kMaxStage) - 1)];
}

void MeleeComboPart::BeginStep(MeleeButton button, bool cancelable) {
	prevStage_ = stage_;
	prevRoute_ = route_;
	prevComboTimer_ = comboTimer_;
	if (stage_ >= kMaxStage) {
		stage_ = 0;
		route_.clear();
	}
	++stage_;
	route_.push_back(RouteChar(button));
	stepButton_ = button;
	sinceStepStart_ = 0.0f;
	BeginMove(FindMove(button, stage_, route_));
	// BeginMove で判定が同じフレームに出た時は取り消せない
	cancelable_ = cancelable && pending_;
	holdChordOnUnlock_ = true;
}

void MeleeComboPart::BeginMove(const MeleeMove& move) {
	const std::string prefab = move.prefab.empty() ? std::string(kFallbackPrefab) : move.prefab;
	const PrefabDef* def = PrefabManager::GetInstance()->Find(prefab);
	const PrefabDef& melee = (def && def->hasMelee) ? *def : PrefabDef::Defaults();
	const float total = melee.meleeStartup + melee.meleeActiveDuration + melee.meleeRecovery;

	// 発生〜スキまで他の行動を禁止し、スキ明けから comboWindow 秒だけ次の段を受け付ける
	lockTimer_ = total;
	comboTimer_ = total + melee.meleeComboWindow;
	startupTimer_ = melee.meleeStartup;
	pending_ = true;
	pendingPrefab_ = prefab;
	pendingDamageScale_ = move.damageScale;
	pendingGaugeGain_ = move.gaugeGain;
	// 2 回目以降の判定・移動・ガードのために技を写しておく
	moveActive_ = true;
	moveElapsed_ = 0.0f;
	nextHit_ = 0;
	moveRehitInterval_ = 0.0f;
	moveSuperArmor_ = false;
	currentMove_ = move;
	currentPrefab_ = prefab;

	if (host_) {
		if (!move.clip.empty()) {
			host_->PlayComboClip(move.clip, move.clipSpeed, clipFade_, false);
			clipPlayed_ = true;
		}
		host_->OnComboAttackStarted();
	}
	// startup が 0 なら同じフレームで出す（1 フレーム遅れを避ける）
	if (startupTimer_ <= 0.0f) SpawnPending();
}

void MeleeComboPart::SpawnPending() {
	pending_ = false;
	startupTimer_ = 0.0f;
	cancelable_ = false;
	currentGaugeGain_ = pendingGaugeGain_;
	if (host_) host_->SpawnComboMelee(pendingPrefab_, pendingDamageScale_, 0.0f);
}

void MeleeComboPart::UpdateMoveTimeline(float dt) {
	if (!moveActive_) return;
	moveElapsed_ += dt;
	// 2 回目以降の判定は、同時押しで溜めへ取り消せる間だけ待つ（取り消した技は moveActive_ が落ちている）。
	// 1 回目より前に出してよい（Y1 の突進中の判定など）
	const bool canStillCancel = cancelable_ && sinceStepStart_ <= chordWindow_;
	while (!canStillCancel && nextHit_ < currentMove_.hits.size() && moveElapsed_ >= currentMove_.hits[nextHit_].time) {
		const MeleeExtraHit& hit = currentMove_.hits[nextHit_];
		currentGaugeGain_ = currentMove_.gaugeGain;
		if (host_) host_->SpawnComboMelee(hit.prefab.empty() ? currentPrefab_ : hit.prefab, hit.damageScale, moveRehitInterval_);
		++nextHit_;
	}
	if (lockTimer_ <= 0.0f) moveActive_ = false;
}

MeleeMotion MeleeComboPart::GetCurrentMotion() const {
	if (moveActive_) {
		for (const MeleeMotion& motion : currentMove_.motions) {
			if (moveElapsed_ >= motion.start && moveElapsed_ < motion.end) return motion;
		}
	}
	return MeleeMotion{};
}

bool MeleeComboPart::IsGuarding(float& outAngle) const {
	const MeleeGuard& g = currentMove_.guard;
	if (!moveActive_ || g.end <= g.start || moveElapsed_ < g.start || moveElapsed_ >= g.end) return false;
	outAngle = g.angle;
	return true;
}

MeleeComboPart::ChargeKind MeleeComboPart::ResolveChargeKind(int stage, const std::string& route) {
	// 4 段目を出した後（次を待つ間）なら、4 段目のボタンで派生を分ける
	if (stage == kMaxStage && !route.empty()) {
		return (route.back() == kRouteCharY) ? ChargeKind::AfterY4 : ChargeKind::AfterB4;
	}
	return ChargeKind::Solo;
}

MeleeComboPart::ChargeKind MeleeComboPart::ChordChargeKind(const InputActionMap* actions, int stage, const std::string& route) {
	if (actions && actions->IsPressed(static_cast<int>(Action::Fire))) return ChargeKind::Burst;
	return ResolveChargeKind(stage, route);
}

void MeleeComboPart::TakeBurstGunCharge() {
	const int gun = host_ ? std::clamp(host_->ConsumeGunCharge(), 0, kGunChargeLevels - 1) : 0;
	burstRehitInterval_ = burstRehitIntervals_[static_cast<size_t>(gun)];
}

bool MeleeComboPart::CanCharge(ChargeKind kind) const {
	if (kind != ChargeKind::Solo) return true;
	return buffLockoutTimer_ <= 0.0f && buffLevel_ < MeleeChargeSet::kLevelCount;
}

void MeleeComboPart::BeginCharge(ChargeKind kind) {
	chargeKind_ = kind;
	if (kind == ChargeKind::Burst) TakeBurstGunCharge();
	autoCharge_ = false;
	charging_ = true;
	chargeTimer_ = 0.0f;
	chargeLevel_ = 1;
	pending_ = false;
	moveActive_ = false;
	lockTimer_ = 0.0f;
	cancelable_ = false;
	ClearBuffer();
	if (host_) {
		const MeleeChargeSet& set = charges_[static_cast<size_t>(chargeKind_)];
		if (!set.holdClip.empty()) {
			host_->PlayComboClip(set.holdClip, 1.0f, clipFade_, true);
			clipPlayed_ = true;
		}
		host_->OnComboAttackStarted();
	}
}

void MeleeComboPart::UpdateCharge(const InputActionMap* actions, float dt) {
	chargeTimer_ += dt;
	// 入力を通さない溜め：時間が来たら決めておいた段階で放つ
	if (autoCharge_) {
		if (chargeTimer_ >= autoChargeTime_) {
			autoCharge_ = false;
			chargeLevel_ = autoLevel_;
			ReleaseCharge();
		}
		return;
	}
	// Y+B の強化の溜めに入ってすぐ（同時押しとみなす間）に RT も押したら、大技の溜めに変える（押す順番を問わない）
	if (chargeKind_ == ChargeKind::Solo && chargeTimer_ <= chordWindow_
		&& actions && actions->IsPressed(static_cast<int>(Action::Fire))) {
		chargeKind_ = ChargeKind::Burst;
		TakeBurstGunCharge();
		const MeleeChargeSet& burst = charges_[static_cast<size_t>(ChargeKind::Burst)];
		if (host_ && !burst.holdClip.empty()) {
			host_->PlayComboClip(burst.holdClip, 1.0f, clipFade_, true);
			clipPlayed_ = true;
		}
	}
	const MeleeChargeSet& set = charges_[static_cast<size_t>(chargeKind_)];
	while (chargeLevel_ < MeleeChargeSet::kLevelCount && chargeTimer_ >= set.levels[chargeLevel_].holdTime) {
		++chargeLevel_;
	}
	const bool held = actions
		&& actions->IsPressed(static_cast<int>(Action::MeleeWeak))
		&& actions->IsPressed(static_cast<int>(Action::MeleeStrong));
	if (!held || chargeLevel_ >= MeleeChargeSet::kLevelCount) ReleaseCharge();
}

void MeleeComboPart::ReleaseCharge() {
	charging_ = false;
	const MeleeChargeSet& set = charges_[static_cast<size_t>(chargeKind_)];
	const int level = std::clamp(chargeLevel_, 1, MeleeChargeSet::kLevelCount);
	// 溜め攻撃を放ったらコンボは 1 段目から
	ResetCombo();
	cancelable_ = false;
	if (chargeKind_ == ChargeKind::Solo) {
		ReleaseBuff(level);
		return;
	}
	if (chargeKind_ == ChargeKind::Burst) {
		// 大技：回転斬りは当たっている間ずっと出ていて、同じ相手には溜めに入った時の銃のチャージで決めた間隔ごとに当たる
		BeginMove(set.levels[static_cast<size_t>(level - 1)].move);
		moveRehitInterval_ = burstRehitInterval_;
		moveSuperArmor_ = true;
		return;
	}
	BeginMove(set.levels[static_cast<size_t>(level - 1)].move);
}

void MeleeComboPart::ReleaseBuff(int level) {
	const size_t idx = static_cast<size_t>(std::clamp(level, 1, MeleeChargeSet::kLevelCount) - 1);
	if (buffLevel_ == 0) {
		buffLevel_ = static_cast<int>(idx) + 1;
		buffTimer_ = buffLevels_[idx].duration;
	} else if (static_cast<int>(idx) + 1 > buffLevel_) {
		// 強化中に高い段階で放った：段階を上げ、続く時間の差分だけ延ばす（低い・同じ段階なら何も変えない）
		buffTimer_ += buffLevels_[idx].duration - buffLevels_[static_cast<size_t>(buffLevel_ - 1)].duration;
		buffLevel_ = static_cast<int>(idx) + 1;
	}
	// 攻撃は出さず、かける時のクリップの間だけ他の行動を禁止する
	pending_ = false;
	moveActive_ = false;
	lockTimer_ = buffReleaseLock_;
	comboTimer_ = buffReleaseLock_;
	if (host_) {
		const MeleeMove& m = charges_[static_cast<size_t>(ChargeKind::Solo)].levels[idx].move;
		if (!m.clip.empty()) {
			host_->PlayComboClip(m.clip, m.clipSpeed, clipFade_, false);
			clipPlayed_ = true;
		}
		host_->OnComboAttackStarted();
	}
}

void MeleeComboPart::UpdateBuff(float dt) {
	if (buffLevel_ > 0) {
		buffTimer_ -= dt;
		if (buffTimer_ <= 0.0f) {
			buffLevel_ = 0;
			buffTimer_ = 0.0f;
			buffLockoutTimer_ = buffLockout_;
		}
	} else if (buffLockoutTimer_ > 0.0f) {
		buffLockoutTimer_ = (std::max)(0.0f, buffLockoutTimer_ - dt);
	}
}

void MeleeComboPart::ClearBuffer() {
	bufferedB_ = false;
	bufferedY_ = false;
	bufferedBAge_ = 0.0f;
	bufferedYAge_ = 0.0f;
	lastBuffered_ = MeleeButton::None;
	holdChordOnUnlock_ = false;
}

bool MeleeComboPart::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	ReadFloat(root, "inputBuffer", inputBuffer_);
	ReadFloat(root, "chordWindow", chordWindow_);
	ReadFloat(root, "clipFade", clipFade_);
	ReadFloat(root, "autoChargeTime", autoChargeTime_);

	const JsonValue& base = root["base"];
	const JsonValue& bB = base["B"];
	const JsonValue& bY = base["Y"];
	for (size_t i = 0; i < baseB_.size(); ++i) {
		ReadMove(bB[i], baseB_[i]);
		ReadMove(bY[i], baseY_[i]);
	}

	const JsonValue& routes = root["routes"];
	if (routes.IsObject()) {
		routes_.clear();
		for (const auto& entry : routes.AsObject()) {
			if (!IsValidRoute(entry.first)) continue;
			MeleeMove m;
			ReadMove(entry.second, m);
			routes_.emplace_back(entry.first, std::move(m));
		}
	}

	const JsonValue& charge = root["charge"];
	for (size_t k = 0; k < charges_.size(); ++k) {
		const JsonValue& c = charge[kChargeKeys[k]];
		if (!c.IsObject()) continue;
		MeleeChargeSet& set = charges_[k];
		set.holdClip = c["holdClip"].AsString(set.holdClip);
		const JsonValue& levels = c["levels"];
		for (size_t i = 0; i < set.levels.size(); ++i) {
			const JsonValue& lv = levels[i];
			if (!lv.IsObject()) continue;
			ReadFloat(lv, "holdTime", set.levels[i].holdTime);
			ReadMove(lv, set.levels[i].move);
		}
	}

	const JsonValue& rehit = root["burstRehitIntervals"];
	if (rehit.IsArray()) {
		for (size_t i = 0; i < burstRehitIntervals_.size() && i < rehit.Size(); ++i) {
			burstRehitIntervals_[i] = static_cast<float>(rehit[i].AsDouble(static_cast<double>(burstRehitIntervals_[i])));
		}
	}

	const JsonValue& buff = root["buff"];
	if (buff.IsObject()) {
		ReadFloat(buff, "lockout", buffLockout_);
		ReadFloat(buff, "releaseLock", buffReleaseLock_);
		ReadColor(buff["bladeCoreColor"], buffBladeCoreColor_);
		ReadColor(buff["bladeEdgeColor"], buffBladeEdgeColor_);
		const JsonValue& levels = buff["levels"];
		for (size_t i = 0; i < buffLevels_.size(); ++i) {
			const JsonValue& lv = levels[i];
			if (!lv.IsObject()) continue;
			ReadFloat(lv, "attackScale", buffLevels_[i].attackScale);
			ReadFloat(lv, "moveScale", buffLevels_[i].moveScale);
			ReadFloat(lv, "justWindowAdd", buffLevels_[i].justWindowAdd);
			ReadFloat(lv, "duration", buffLevels_[i].duration);
		}
	}
	return true;
}

void MeleeComboPart::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string(
		"近接コンボ（24 番）。技は routes（押した順番）→ base（ボタン × 段）の順に探す。"
		"判定の形と時間はプレハブの melee、威力は攻撃力 × damageScale。charge は Y+B の溜め（段階 1〜3）。"
		"charge.solo は攻撃を出さず buff の段階の強化を掛ける（prefab・damageScale は使わず clip だけ使う）"));
	root["inputBuffer"] = JsonValue(static_cast<double>(inputBuffer_));
	root["chordWindow"] = JsonValue(static_cast<double>(chordWindow_));
	root["clipFade"] = JsonValue(static_cast<double>(clipFade_));
	root["autoChargeTime"] = JsonValue(static_cast<double>(autoChargeTime_));

	JsonValue base = JsonValue::MakeObject();
	JsonValue bB = JsonValue::MakeArray();
	JsonValue bY = JsonValue::MakeArray();
	for (size_t i = 0; i < baseB_.size(); ++i) {
		JsonValue nb = JsonValue::MakeObject();
		WriteMove(nb, baseB_[i]);
		bB.Push(std::move(nb));
		JsonValue ny = JsonValue::MakeObject();
		WriteMove(ny, baseY_[i]);
		bY.Push(std::move(ny));
	}
	base["B"] = std::move(bB);
	base["Y"] = std::move(bY);
	root["base"] = std::move(base);

	JsonValue routes = JsonValue::MakeObject();
	for (const auto& r : routes_) {
		JsonValue n = JsonValue::MakeObject();
		WriteMove(n, r.second);
		routes[r.first] = std::move(n);
	}
	root["routes"] = std::move(routes);

	JsonValue charge = JsonValue::MakeObject();
	for (size_t k = 0; k < charges_.size(); ++k) {
		const MeleeChargeSet& set = charges_[k];
		JsonValue c = JsonValue::MakeObject();
		c["holdClip"] = JsonValue(set.holdClip);
		JsonValue levels = JsonValue::MakeArray();
		for (const auto& lv : set.levels) {
			JsonValue n = JsonValue::MakeObject();
			n["holdTime"] = JsonValue(static_cast<double>(lv.holdTime));
			WriteMove(n, lv.move);
			levels.Push(std::move(n));
		}
		c["levels"] = std::move(levels);
		charge[kChargeKeys[k]] = std::move(c);
	}
	root["charge"] = std::move(charge);

	JsonValue rehit = JsonValue::MakeArray();
	for (float t : burstRehitIntervals_) rehit.Push(JsonValue(static_cast<double>(t)));
	root["burstRehitIntervals"] = std::move(rehit);

	JsonValue buff = JsonValue::MakeObject();
	buff["lockout"] = JsonValue(static_cast<double>(buffLockout_));
	buff["releaseLock"] = JsonValue(static_cast<double>(buffReleaseLock_));
	buff["bladeCoreColor"] = MakeColor(buffBladeCoreColor_);
	buff["bladeEdgeColor"] = MakeColor(buffBladeEdgeColor_);
	JsonValue buffLevels = JsonValue::MakeArray();
	for (const MeleeBuffLevel& b : buffLevels_) {
		JsonValue n = JsonValue::MakeObject();
		n["attackScale"] = JsonValue(static_cast<double>(b.attackScale));
		n["moveScale"] = JsonValue(static_cast<double>(b.moveScale));
		n["justWindowAdd"] = JsonValue(static_cast<double>(b.justWindowAdd));
		n["duration"] = JsonValue(static_cast<double>(b.duration));
		buffLevels.Push(std::move(n));
	}
	buff["levels"] = std::move(buffLevels);
	root["buff"] = std::move(buff);

	std::filesystem::path path(kTuningPath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	JsonWriter::WriteFile(kTuningPath, root, { true, kJsonIndentWidth });
}

void MeleeComboPart::OnImGui() {
#ifdef _DEBUG
	// ----- 状態 -----
	if (charging_) {
		ImGui::Text("Charging: %s  Lv %d  (%.2f s)", kChargeKeys[static_cast<size_t>(chargeKind_)], chargeLevel_, chargeTimer_);
	} else {
		ImGui::Text("Stage %d  Route \"%s\"", stage_, route_.c_str());
	}
	ImGui::Text("Lock %.2f  Combo %.2f  Pending %s", lockTimer_, comboTimer_, pending_ ? pendingPrefab_.c_str() : "-");
	ImGui::Text("Buff Lv %d  (%.1f s)  Lockout %.1f s", buffLevel_, buffTimer_, buffLockoutTimer_);

	// ----- 入力 -----
	ImGui::DragFloat("Input Buffer", &inputBuffer_, 0.005f, 0.0f, kImGuiTimeMax, "%.3f s");
	ImGui::DragFloat("Chord Window", &chordWindow_, 0.005f, 0.0f, kImGuiTimeMax, "%.3f s");
	ImGui::TextDisabled("B の出るまでの時間より短くする（長いと B から溜めに入れない）");
	ImGui::DragFloat("Clip Fade", &clipFade_, 0.005f, 0.0f, kImGuiTimeMax, "%.3f s");
	ImGui::DragFloat("Auto Charge Time", &autoChargeTime_, 0.01f, 0.0f, kImGuiTimeMax, "%.2f s");
	ImGui::TextDisabled("ジャスト回避の近接派生で、溜めのクリップを速く流す時間（0 なら溜めずにすぐ放つ）");

	// ----- 基本技 -----
	for (int side = 0; side < 2; ++side) {
		auto& base = (side == 0) ? baseB_ : baseY_;
		const char rc = (side == 0) ? kRouteCharB : kRouteCharY;
		char label[kTextBufferSize];
		std::snprintf(label, sizeof(label), "Base %c", rc);
		if (ImGui::TreeNode(label)) {
			for (size_t i = 0; i < base.size(); ++i) {
				std::snprintf(label, sizeof(label), "%c%d", rc, static_cast<int>(i) + 1);
				if (ImGui::TreeNode(label)) {
					DrawMove(base[i]);
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}
	}

	// ----- 特別なルート -----
	if (ImGui::TreeNode("Routes")) {
		for (size_t i = 0; i < routes_.size();) {
			ImGui::PushID(static_cast<int>(i));
			bool erase = false;
			if (ImGui::TreeNode(routes_[i].first.c_str())) {
				DrawMove(routes_[i].second);
				erase = ImGui::Button("Remove");
				ImGui::TreePop();
			}
			ImGui::PopID();
			if (erase) routes_.erase(routes_.begin() + static_cast<std::ptrdiff_t>(i));
			else ++i;
		}
		static char newRoute[kTextBufferSize] = "";
		ImGui::InputText("New Route (B/Y)", newRoute, sizeof(newRoute));
		ImGui::SameLine();
		if (ImGui::Button("Add")) {
			const std::string key = newRoute;
			const bool exists = std::any_of(routes_.begin(), routes_.end(), [&](const auto& r) { return r.first == key; });
			if (IsValidRoute(key) && !exists) {
				// 足した時点では、同じ順番で今出る技を写しておく
				const MeleeButton last = (key.back() == kRouteCharY) ? MeleeButton::Y : MeleeButton::B;
				routes_.emplace_back(key, FindMove(last, static_cast<int>(key.size()), key));
				newRoute[0] = '\0';
			}
		}
		ImGui::TreePop();
	}

	// ----- 溜め攻撃 -----
	for (size_t k = 0; k < charges_.size(); ++k) {
		MeleeChargeSet& set = charges_[k];
		if (ImGui::TreeNode(kChargeLabels[k])) {
			InputString("Hold Clip", set.holdClip);
			if (static_cast<ChargeKind>(k) == ChargeKind::Burst) {
				ImGui::DragFloat3("Spin Rehit (Gun 0/1/2)", burstRehitIntervals_.data(), 0.005f, 0.0f, kImGuiTimeMax, "%.3f s");
				ImGui::TextDisabled("回転斬り（Extra Hits）が同じ相手にもう一度当たれるまでの実時間。ヒットストップ中も進む");
			}
			for (size_t i = 0; i < set.levels.size(); ++i) {
				char label[kTextBufferSize];
				std::snprintf(label, sizeof(label), "Level %d", static_cast<int>(i) + 1);
				if (ImGui::TreeNode(label)) {
					ImGui::DragFloat("Hold Time", &set.levels[i].holdTime, 0.01f, 0.0f, kImGuiHoldMax, "%.2f s");
					DrawMove(set.levels[i].move);
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}
	}

	// ----- 強化（単独の溜め） -----
	if (ImGui::TreeNode("Buff")) {
		ImGui::DragFloat("Lockout", &buffLockout_, 0.1f, 0.0f, kImGuiBuffTimeMax, "%.1f s");
		ImGui::TextDisabled("切れた後、この秒数は単独の溜めに入れない");
		ImGui::DragFloat("Release Lock", &buffReleaseLock_, 0.01f, 0.0f, kImGuiTimeMax, "%.2f s");
		ImGui::ColorEdit3("Blade Core", &buffBladeCoreColor_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Blade Edge", &buffBladeEdgeColor_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		for (size_t i = 0; i < buffLevels_.size(); ++i) {
			char label[kTextBufferSize];
			std::snprintf(label, sizeof(label), "Buff Level %d", static_cast<int>(i) + 1);
			if (ImGui::TreeNode(label)) {
				MeleeBuffLevel& b = buffLevels_[i];
				ImGui::DragFloat("Attack Scale", &b.attackScale, 0.005f, 0.0f, kImGuiBuffScaleMax, "%.3f x");
				ImGui::DragFloat("Move Scale", &b.moveScale, 0.005f, 0.0f, kImGuiBuffScaleMax, "%.3f x");
				ImGui::DragFloat("Just Window Add", &b.justWindowAdd, 0.005f, 0.0f, kImGuiTimeMax, "%.3f s");
				ImGui::DragFloat("Duration", &b.duration, 0.1f, 0.0f, kImGuiBuffTimeMax, "%.1f s");
				ImGui::TreePop();
			}
		}
		ImGui::TreePop();
	}

	if (ImGui::Button("Save##MeleeCombo")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Load##MeleeCombo")) LoadTuning();
#endif
}
