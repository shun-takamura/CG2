#include "StageStreaming.h"
#include "ScenePreloader.h"
#include "SessionLogger.h"
#include "Profiling/LoadProfiler.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <format>
#include "imgui.h"

void StageStreaming::Initialize(IStageStreamingHost* host, const std::string& sceneName) {
	host_ = host;
	sceneName_ = sceneName;
	preloadingIndex_ = -1;
	for (Group& g : groups_) g.state = State::Waiting;
}

bool StageStreaming::HasGroup(const std::string& group) const {
	return std::any_of(groups_.begin(), groups_.end(), [&](const Group& g) { return g.name == group; });
}

void StageStreaming::Update(float stageSec) {
	for (Group& g : groups_) {
		if (g.state == State::Waiting && stageSec >= g.preloadSec) g.state = State::Queued;
	}
	PollPreload();
	StartQueuedPreload();
	for (size_t i = 0; i < groups_.size(); ++i) {
		if (groups_[i].state != State::Created && stageSec >= groups_[i].showSec) Create(i);
	}
}

void StageStreaming::CreateAll() {
	for (size_t i = 0; i < groups_.size(); ++i) {
		if (groups_[i].state != State::Created) Create(i);
	}
}

void StageStreaming::Seek(float stageSec) {
	for (size_t i = 0; i < groups_.size(); ++i) {
		Group& g = groups_[i];
		if (g.state == State::Created) continue;
		// 作る時刻より後へ飛んだら、その場で作る（先読みを待てないため）
		if (stageSec >= g.showSec) Create(i);
		else if (g.state == State::Waiting && stageSec >= g.preloadSec) g.state = State::Queued;
	}
	StartQueuedPreload();
}

void StageStreaming::PollPreload() {
	if (preloadingIndex_ < 0 || ScenePreloader::GetInstance()->IsBusy()) return;
	Group& g = groups_[preloadingIndex_];
	if (g.state == State::Preloading) {
		g.state = State::Ready;
		SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=stream_preload group={} wall_ms={:.2f}", g.name, LoadProfiler::NowMs() - g.preloadBeginMs));
	}
	preloadingIndex_ = -1;
}

void StageStreaming::StartQueuedPreload() {
	// 先読みは一度に 1 本。シーンの先読みなど他の物を読んでいる間も待つ
	if (preloadingIndex_ >= 0 || ScenePreloader::GetInstance()->IsBusy()) return;
	for (size_t i = 0; i < groups_.size(); ++i) {
		Group& g = groups_[i];
		if (g.state != State::Queued) continue;
		g.preloadBeginMs = LoadProfiler::NowMs();
		ScenePreloader::GetInstance()->Begin(ManifestName(g));
		if (ScenePreloader::GetInstance()->IsBusy()) {
			g.state = State::Preloading;
			preloadingIndex_ = static_cast<int>(i);
		} else {
			// マニフェストが無い（まだ Debug で一度も作っていない）：作る時刻に同期で作る
			g.state = State::Ready;
		}
		return;
	}
}

void StageStreaming::Create(size_t index) {
	Group& g = groups_[index];
	if (g.state == State::Created || !host_) return;

	// 先読みの途中の物を同期で触ると壊れる（16 番 §2.8）ので、何を読んでいても先に残りを済ませる。
	// 地形の層の配列のように、別のグループと同じテクスチャを使う物もある
	auto* preloader = ScenePreloader::GetInstance();
	if (preloader->IsBusy()) {
		const double start = LoadProfiler::NowMs();
		preloader->CompleteNow();
		SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=stream_complete_now group={} wall_ms={:.2f}", g.name, LoadProfiler::NowMs() - start));
	}
	PollPreload();

#ifdef _DEBUG
	// 作るときに読まれた物を記録して、このグループの先読みのマニフェストにする
	preloader->BeginRecording();
#endif
	host_->CreateStreamGroup(g.name);
#ifdef _DEBUG
	preloader->EndRecording(ManifestName(g));
#endif
	g.state = State::Created;
}

void StageStreaming::LoadTuning(const JsonValue& node) {
	const JsonValue& groups = node["groups"];
	if (!groups.IsArray()) return;
	std::vector<Group> loaded;
	for (const JsonValue& e : groups.AsArray()) {
		if (!e.IsObject()) continue;
		Group g;
		g.name = e["name"].AsString(std::string());
		g.preloadSec = static_cast<float>(e["preloadSec"].AsDouble(0.0));
		// S1 の "loadSec"（作る時刻）も読む
		g.showSec = static_cast<float>(e["showSec"].AsDouble(e["loadSec"].AsDouble(g.preloadSec)));
		if (!g.name.empty()) loaded.push_back(std::move(g));
	}
	groups_ = std::move(loaded);
	preloadingIndex_ = -1;
}

void StageStreaming::SaveTuning(JsonValue& node) const {
	JsonValue groups = JsonValue::MakeArray();
	for (const Group& g : groups_) {
		JsonValue e = JsonValue::MakeObject();
		e["name"] = g.name;
		e["preloadSec"] = static_cast<double>(g.preloadSec);
		e["showSec"] = static_cast<double>(g.showSec);
		groups.Push(std::move(e));
	}
	node = JsonValue::MakeObject();
	node["groups"] = std::move(groups);
}

const char* StageStreaming::StateName(State s) {
	switch (s) {
	case State::Waiting:    return "待ち";
	case State::Queued:     return "先読みの順番待ち";
	case State::Preloading: return "先読み中";
	case State::Ready:      return "先読み済み";
	case State::Created:    return "作成済み";
	}
	return "";
}

bool StageStreaming::OnImGui() {
	bool changed = false;
#ifdef _DEBUG
	ImGui::TextDisabled("streamGroup 付きの配置：Preload (s) で裏で読み、Show (s) で作る。前へシークしても捨てない");
	ImGui::TextDisabled("先読みのマニフェストは Debug で作ったときに Preload/%s@<グループ>.json へ記録", sceneName_.c_str());
	for (size_t i = 0; i < groups_.size(); ++i) {
		Group& g = groups_[i];
		ImGui::PushID(static_cast<int>(i));
		const bool created = g.state == State::Created;
		ImGui::TextColored(created ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
			"%-12s %s（まだ作っていない配置 %zu）", g.name.c_str(), StateName(g.state),
			host_ ? host_->GetPendingStreamEntityCount(g.name) : size_t{ 0 });
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("Preload (s)", &g.preloadSec, 0.5f, 0.0f, 1800.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("Show (s)", &g.showSec, 0.5f, 0.0f, 1800.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (!created) {
			ImGui::SameLine();
			if (ImGui::SmallButton("今作る")) Create(i);
		}
		ImGui::PopID();
	}
#endif
	return changed;
}
