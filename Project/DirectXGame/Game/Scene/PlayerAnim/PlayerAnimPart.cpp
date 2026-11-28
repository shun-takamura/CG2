#include "PlayerAnimPart.h"

#include "AnimatedObject3DInstance.h"
#include "AnimatedMeshCache.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 状態の JSON のキー（PlayerAnimState の順）
	constexpr const char* kStateKeys[] = {
		"railStand", "railGlide", "bossIdle", "bossRunForward", "bossRunBack", "bossRunLeft", "bossRunRight",
		"chargeHold", "precisionAim", "shieldGuard", "stormLock", "shatterlineCharge", "shatterlineSlash", "shatterlineRecover",
		"shoot", "chargeRelease",
		"railDodgeNeutral", "railDodgeLeft", "railDodgeRight", "railDodgeUp", "railDodgeDown",
		"groundDodgeNeutral", "groundDodgeForward", "groundDodgeBack", "groundDodgeLeft", "groundDodgeRight",
		"railHit", "groundHit", "heal", "landingTouchdown", "victory", "railDeath",
	};
	static_assert(std::size(kStateKeys) == static_cast<size_t>(PlayerAnimState::Count), "kStateKeys と PlayerAnimState の数が違う");

	// JSON を置くフォルダ（中の *.json を全部読む。まとまりごとに 1 ファイル）
	constexpr const char* kTuningDir = "Resources/Json/Tuning/PlayerAnim";
	constexpr const char* kJsonExtension = ".json";
	constexpr const char* kClipExtension = ".anim";
	// "startAt" に書くと最後のコマで止める
	constexpr const char* kStartAtEnd = "end";
	// 最後のコマで止める時の時刻（ループしないクリップは長さで止まる）
	constexpr float kPastEndTime = 1.0e3f;
	// fallback をたどる回数の上限（ぐるりと戻る書き間違いで止まらないように）
	constexpr int kMaxFallbackDepth = static_cast<int>(PlayerAnimState::Count);
	// 候補の印：上位の桁に種類と状態、下位に来た順
	constexpr uint64_t kKeyKindShift = 48;
	constexpr uint64_t kKeyStateShift = 32;
	constexpr uint64_t kKeyKindState = 1;
	constexpr uint64_t kKeyKindMelee = 2;
	constexpr uint64_t kKeyStateMask = (1ull << (kKeyKindShift - kKeyStateShift)) - 1;
#ifdef _DEBUG
	// 読み込みの警告の色
	constexpr float kWarnColor[] = { 1.0f, 0.6f, 0.3f, 1.0f };
#endif

	/// <summary>キーから状態を探す</summary>
	/// <param name="key">JSON のキー</param>
	/// <returns>状態（無ければ Count）</returns>
	PlayerAnimState StateFromKey(const std::string& key) {
		for (size_t i = 0; i < std::size(kStateKeys); ++i) {
			if (key == kStateKeys[i]) return static_cast<PlayerAnimState>(i);
		}
		return PlayerAnimState::Count;
	}

	/// <summary>状態の JSON のキー</summary>
	/// <param name="s">状態</param>
	/// <returns>キー（Count なら "-"）</returns>
	const char* StateKey(PlayerAnimState s) {
		const size_t i = static_cast<size_t>(s);
		return i < std::size(kStateKeys) ? kStateKeys[i] : "-";
	}

	/// <summary>候補の印を作る</summary>
	/// <param name="kind">種類（状態・近接）</param>
	/// <param name="state">状態の番号</param>
	/// <param name="order">来た順</param>
	/// <returns>印</returns>
	uint64_t MakeKey(uint64_t kind, uint64_t state, uint32_t order) {
		return (kind << kKeyKindShift) | (state << kKeyStateShift) | order;
	}
}

void PlayerAnimPart::Initialize(AnimatedObject3DInstance* player) {
	player_ = player;
	currentKey_ = kNoKey;
	LoadTuning();
}

void PlayerAnimPart::LoadTuning() {
	defs_ = {};
	loadMessages_.clear();
	std::array<std::string, kStateCount> definedIn{};
	namespace fs = std::filesystem;
	std::vector<fs::path> files;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(kTuningDir, ec)) {
		if (entry.is_regular_file() && entry.path().extension() == kJsonExtension) files.push_back(entry.path());
	}
	if (ec) loadMessages_.push_back(std::string(kTuningDir) + " が読めない");
	std::sort(files.begin(), files.end());
	for (const fs::path& file : files) {
		const auto result = JsonParser::ParseFile(file.string());
		const std::string name = file.filename().string();
		if (!result.success) {
			loadMessages_.push_back(name + " の JSON が壊れている");
			continue;
		}
		// 同じ状態を 2 つのファイルに書いたら先に読んだ方を使う（ファイル名の順）
		const auto before = defs_;
		ReadFile(result.value, name);
		for (size_t i = 0; i < kStateCount; ++i) {
			if (!result.value["states"].Contains(kStateKeys[i])) continue;
			if (!definedIn[i].empty()) {
				loadMessages_.push_back(std::string(kStateKeys[i]) + " が " + definedIn[i] + " と " + name + " にある（" + definedIn[i] + " を使う）");
				defs_[i] = before[i];
				continue;
			}
			definedIn[i] = name;
		}
	}
	// クリップが無いものは空として扱う（fallback へ）
	for (size_t i = 0; i < kStateCount; ++i) {
		StateDef& d = defs_[i];
		std::vector<std::string*> clips = { &d.clip, &d.railClip, &d.groundClip };
		for (auto& baseClip : d.baseClips) clips.push_back(&baseClip.second);
		for (std::string* clip : clips) {
			if (clip->empty()) continue;
			if (ClipDuration(ClipPath(*clip)) <= 0.0f) {
				loadMessages_.push_back(std::string(kStateKeys[i]) + " のクリップ " + ClipPath(*clip) + " が無い");
				clip->clear();
			}
		}
	}
	currentKey_ = kNoKey;
}

void PlayerAnimPart::ReadFile(const JsonValue& root, const std::string& fileName) {
	if (root.Contains("meleePriority")) meleePriority_ = static_cast<int>(root["meleePriority"].AsInt(meleePriority_));
	if (root.Contains("runThreshold")) runThreshold_ = static_cast<float>(root["runThreshold"].AsDouble(runThreshold_));
	if (root.Contains("defaultFade")) defaultFade_ = static_cast<float>(root["defaultFade"].AsDouble(defaultFade_));
	if (root.Contains("clipPrefix")) clipPrefix_ = root["clipPrefix"].AsString(clipPrefix_);

	const JsonValue& states = root["states"];
	if (!states.IsObject()) return;
	for (const auto& entry : states.AsObject()) {
		const PlayerAnimState s = StateFromKey(entry.first);
		if (s == PlayerAnimState::Count) {
			loadMessages_.push_back(fileName + " の " + entry.first + " は知らない状態（PlayerAnimState に無い）");
			continue;
		}
		const JsonValue& n = entry.second;
		StateDef d;
		d.clip = n["clip"].AsString(d.clip);
		d.railClip = n["railClip"].AsString(d.railClip);
		d.groundClip = n["groundClip"].AsString(d.groundClip);
		const JsonValue& baseClips = n["baseClips"];
		if (baseClips.IsObject()) {
			for (const auto& baseEntry : baseClips.AsObject()) {
				const PlayerAnimState base = StateFromKey(baseEntry.first);
				if (base == PlayerAnimState::Count) {
					loadMessages_.push_back(entry.first + " の baseClips の " + baseEntry.first + " は知らない状態");
					continue;
				}
				d.baseClips.emplace_back(base, baseEntry.second.AsString(std::string()));
			}
		}
		d.loop = n["loop"].AsBool(d.loop);
		d.speed = static_cast<float>(n["speed"].AsDouble(d.speed));
		d.fade = static_cast<float>(n["fade"].AsDouble(d.fade));
		d.priority = static_cast<int>(n["priority"].AsInt(d.priority));
		d.length = static_cast<float>(n["length"].AsDouble(d.length));
		const JsonValue& startAt = n["startAt"];
		if (startAt.IsString()) d.startAtEnd = (startAt.AsString() == kStartAtEnd);
		else d.startTime = static_cast<float>(startAt.AsDouble(d.startTime));
		const std::string fallback = n["fallback"].AsString(std::string());
		if (!fallback.empty()) {
			d.fallback = StateFromKey(fallback);
			if (d.fallback == PlayerAnimState::Count) loadMessages_.push_back(entry.first + " の fallback " + fallback + " は知らない状態");
		}
		defs_[static_cast<size_t>(s)] = d;
	}
}

std::string PlayerAnimPart::ClipPath(const std::string& clip) const {
	if (clip.empty() || clip.find('/') != std::string::npos) return clip;
	return clipPrefix_ + clip + kClipExtension;
}

const std::string& PlayerAnimPart::ModeClip(const StateDef& d) const {
	if (!d.baseClips.empty()) {
		const PlayerAnimState base = ChooseBase();
		for (const auto& baseClip : d.baseClips) {
			if (baseClip.first == base && !baseClip.second.empty()) return baseClip.second;
		}
	}
	const std::string& modeClip = (mode_ == PlayerAnimMode::Rail) ? d.railClip : d.groundClip;
	return modeClip.empty() ? d.clip : modeClip;
}

float PlayerAnimPart::ClipDuration(const std::string& path) {
	auto clip = AnimatedMeshCache::GetInstance()->GetAnimation(path);
	return clip ? clip->duration : 0.0f;
}

PlayerAnimState PlayerAnimPart::Resolve(PlayerAnimState state) const {
	for (int depth = 0; depth < kMaxFallbackDepth && state != PlayerAnimState::Count; ++depth) {
		const StateDef& d = defs_[static_cast<size_t>(state)];
		if (!ModeClip(d).empty()) return state;
		state = d.fallback;
	}
	return PlayerAnimState::Count;
}

PlayerAnimState PlayerAnimPart::ChooseBase() const {
	if (mode_ == PlayerAnimMode::Rail) return railGlide_ ? PlayerAnimState::RailGlide : PlayerAnimState::RailStand;
	const float len = std::sqrt(groundMove_.x * groundMove_.x + groundMove_.y * groundMove_.y);
	if (len < runThreshold_) return PlayerAnimState::BossIdle;
	if (std::fabs(groundMove_.y) >= std::fabs(groundMove_.x)) {
		return groundMove_.y >= 0.0f ? PlayerAnimState::BossRunForward : PlayerAnimState::BossRunBack;
	}
	return groundMove_.x >= 0.0f ? PlayerAnimState::BossRunRight : PlayerAnimState::BossRunLeft;
}

bool PlayerAnimPart::MakeChoice(PlayerAnimState state, float elapsed, uint32_t order, Choice& out) const {
	const PlayerAnimState resolved = Resolve(state);
	if (resolved == PlayerAnimState::Count) return false;
	const StateDef& d = defs_[static_cast<size_t>(resolved)];
	// 印は fallback をたどった後の状態で作る（走りの向きが替わっても、同じ待機へ落ちるなら鳴らし直さない）
	out.key = MakeKey(kKeyKindState, static_cast<uint64_t>(resolved), order);
	out.clip = ClipPath(ModeClip(d));
	out.loop = d.loop;
	out.speed = d.speed;
	out.fade = d.fade >= 0.0f ? d.fade : defaultFade_;
	out.time = d.startTime + elapsed * d.speed;
	out.atEnd = d.startAtEnd;
	out.priority = d.priority;
	out.order = order;
	return true;
}

bool PlayerAnimPart::Choose(Choice& out) const {
	bool found = false;
	auto consider = [&](const Choice& c) {
		if (!found || c.priority > out.priority || (c.priority == out.priority && c.order >= out.order)) {
			out = c;
			found = true;
		}
	};
	Choice c;
	if (MakeChoice(ChooseBase(), 0.0f, 0, c)) consider(c);
	for (size_t i = 0; i < kStateCount; ++i) {
		if (held_[i] && MakeChoice(static_cast<PlayerAnimState>(i), 0.0f, heldOrder_[i], c)) consider(c);
	}
	for (const OneShot& o : oneShots_) {
		if (MakeChoice(o.state, o.elapsed, o.order, c)) consider(c);
	}
	if (meleeActive_ && !meleeClip_.empty()) {
		c.key = MakeKey(kKeyKindMelee, 0, meleeOrder_);
		c.clip = meleeClip_;
		c.loop = meleeLoop_;
		c.speed = meleeSpeed_;
		c.fade = meleeFade_;
		c.time = meleeElapsed_ * meleeSpeed_;
		c.atEnd = false;
		c.priority = meleePriority_;
		c.order = meleeOrder_;
		consider(c);
	}
	return found;
}

void PlayerAnimPart::Apply(bool force) {
	if (!player_ || suspended_) return;
	Choice c;
	if (!Choose(c)) return;   // 鳴らせる物が無い：姿勢はそのまま
	if (!force && c.key == currentKey_ && c.clip == currentClip_) return;
	player_->PlayAnimation(c.clip, c.fade);
	currentClip_ = c.clip;
	player_->SetLoop(c.loop);
	player_->SetPlaybackSpeed(c.speed);
	player_->SetAnimationTime(c.atEnd ? kPastEndTime : c.time);
	currentKey_ = c.key;
	const uint64_t kind = c.key >> kKeyKindShift;
	currentState_ = (kind == kKeyKindState)
		? static_cast<PlayerAnimState>((c.key >> kKeyStateShift) & kKeyStateMask)
		: PlayerAnimState::Count;
}

void PlayerAnimPart::Update(float dt) {
	for (OneShot& o : oneShots_) o.elapsed += dt;
	oneShots_.erase(std::remove_if(oneShots_.begin(), oneShots_.end(),
		[](const OneShot& o) { return o.elapsed >= o.length; }), oneShots_.end());
	if (meleeActive_) meleeElapsed_ += dt;
	Apply(false);
}

void PlayerAnimPart::Replay() {
	Apply(true);
}

void PlayerAnimPart::SetHeld(PlayerAnimState state, bool on) {
	const size_t i = static_cast<size_t>(state);
	if (i >= kStateCount || held_[i] == on) return;
	held_[i] = on;
	if (on) heldOrder_[i] = nextOrder_++;
}

void PlayerAnimPart::Trigger(PlayerAnimState state) {
	const size_t i = static_cast<size_t>(state);
	if (i >= kStateCount) return;
	const PlayerAnimState resolved = Resolve(state);
	if (resolved == PlayerAnimState::Count) return;   // クリップがまだ無い
	const StateDef& d = defs_[static_cast<size_t>(resolved)];
	OneShot o;
	o.state = state;
	o.order = nextOrder_++;
	o.length = d.length > 0.0f
		? d.length
		: (std::max)(ClipDuration(ClipPath(ModeClip(d))) - d.startTime, 0.0f) / (std::max)(d.speed, std::numeric_limits<float>::epsilon());
	// 同じ状態が鳴っている途中なら最初から
	oneShots_.erase(std::remove_if(oneShots_.begin(), oneShots_.end(),
		[state](const OneShot& x) { return x.state == state; }), oneShots_.end());
	oneShots_.push_back(o);
	Apply(false);
}

void PlayerAnimPart::Stop(PlayerAnimState state) {
	const size_t before = oneShots_.size();
	oneShots_.erase(std::remove_if(oneShots_.begin(), oneShots_.end(),
		[state](const OneShot& x) { return x.state == state; }), oneShots_.end());
	if (oneShots_.size() != before) Apply(false);
}

void PlayerAnimPart::ClearActions() {
	held_ = {};
	oneShots_.clear();
}

void PlayerAnimPart::PlayMelee(const std::string& clip, float speed, float fade, bool loop) {
	meleeActive_ = true;
	meleeClip_ = clip;
	meleeSpeed_ = speed;
	meleeFade_ = fade;
	meleeLoop_ = loop;
	meleeElapsed_ = 0.0f;
	meleeOrder_ = nextOrder_++;
	Apply(false);
}

void PlayerAnimPart::EndMelee() {
	if (!meleeActive_) return;
	meleeActive_ = false;
	meleeClip_.clear();
	Apply(false);
}

void PlayerAnimPart::SetSuspended(bool suspended) {
	if (suspended_ == suspended) return;
	suspended_ = suspended;
	currentKey_ = kNoKey;
	if (!suspended_) Apply(true);
}

void PlayerAnimPart::PlayAndSuspend(PlayerAnimState state) {
	suspended_ = false;
	Choice c;
	if (player_ && MakeChoice(state, 0.0f, nextOrder_++, c)) {
		player_->PlayAnimation(c.clip, c.fade);
		player_->SetLoop(c.loop);
		player_->SetPlaybackSpeed(c.speed);
		player_->SetAnimationTime(c.atEnd ? kPastEndTime : c.time);
		currentState_ = state;
	}
	suspended_ = true;
	currentKey_ = kNoKey;
}

void PlayerAnimPart::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Reload PlayerAnim JSON")) {
		LoadTuning();
		Apply(true);
	}
	ImGui::Text("mode %s  base %s  now %s%s", mode_ == PlayerAnimMode::Rail ? "Rail" : "Ground",
		StateKey(ChooseBase()), currentState_ == PlayerAnimState::Count ? (meleeActive_ ? "melee" : "-") : StateKey(currentState_),
		suspended_ ? "  (suspended)" : "");
	if (meleeActive_) ImGui::Text("melee %s  t=%.2f", meleeClip_.c_str(), meleeElapsed_);
	for (const OneShot& o : oneShots_) ImGui::Text("one-shot %s  %.2f / %.2f", StateKey(o.state), o.elapsed, o.length);
	for (size_t i = 0; i < kStateCount; ++i) {
		if (held_[i]) ImGui::Text("held %s", kStateKeys[i]);
	}
	for (const std::string& m : loadMessages_) {
		ImGui::TextColored(ImVec4(kWarnColor[0], kWarnColor[1], kWarnColor[2], kWarnColor[3]), "%s", m.c_str());
	}

	// 試し鳴らし：ゲームの条件を作らずにクリップを確かめる（1 回きりは Trigger、押している間は Hold）
	ImGui::Combo("test state", &imguiTestState_, kStateKeys, static_cast<int>(kStateCount));
	const PlayerAnimState test = static_cast<PlayerAnimState>(imguiTestState_);
	if (ImGui::Button("Trigger")) Trigger(test);
	ImGui::SameLine();
	bool held = held_[static_cast<size_t>(imguiTestState_)];
	if (ImGui::Checkbox("Hold", &held)) SetHeld(test, held);

	if (ImGui::TreeNode("States")) {
		for (size_t i = 0; i < kStateCount; ++i) {
			const StateDef& d = defs_[i];
			const PlayerAnimState r = Resolve(static_cast<PlayerAnimState>(i));
			const std::string& clip = ModeClip(d);
			ImGui::Text("%-20s p%3d %s%s%s", kStateKeys[i], d.priority, clip.empty() ? "(empty)" : clip.c_str(),
				(r != PlayerAnimState::Count && r != static_cast<PlayerAnimState>(i)) ? " -> " : "",
				(r != PlayerAnimState::Count && r != static_cast<PlayerAnimState>(i)) ? StateKey(r) : "");
		}
		ImGui::TreePop();
	}
#endif
}
