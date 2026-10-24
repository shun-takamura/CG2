#pragma once
#include <string>
#include <vector>

class JsonValue;

/// <summary>
/// StageStreaming が StagePlayScene に頼むこと（GameScene のストリーミングの関数の窓口。11 番 L9）
/// </summary>
class IStageStreamingHost {
public:
	virtual ~IStageStreamingHost() = default;
	/// <summary>取っておいた group の配置を今すぐ作る。作った数を返す</summary>
	virtual size_t CreateStreamGroup(const std::string& group) = 0;
	/// <summary>まだ作っていない group の配置の数</summary>
	virtual size_t GetPendingStreamEntityCount(const std::string& group) const = 0;
};

/// <summary>
/// 区間ごとのストリーミングの時刻表（11 番 L9 §6）。
/// シーン JSON の streamGroup 付きの配置は開始時に作らず、グループごとに
///   preloadSec を過ぎたら ScenePreloader でアセットを裏で読み（マニフェスト Resources/Json/Preload/<シーン>@<グループ>.json）、
///   showSec を過ぎたら配置を作る（キャッシュに当たるだけ。先読みが間に合っていなければ残りを済ませてから作る）。
/// 先読みは一度に 1 本なので、グループは頼んだ順に 1 つずつ読む。
/// マニフェストは Debug で配置を作ったときに読まれた物を記録して書く（無いうちは showSec に同期で作る）。
/// 調整値は StagePlay.json の "streaming"。
/// </summary>
class StageStreaming {
public:
	void Initialize(IStageStreamingHost* host, const std::string& sceneName);

	/// <summary>時刻表にあるグループか（無いグループの配置はすぐ作る）</summary>
	bool HasGroup(const std::string& group) const;

	/// <summary>Rail の間、毎フレーム。経過秒で先読みと作成を進める</summary>
	void Update(float stageSec);
	/// <summary>時刻に関係なく全部作る（着陸・ボス戦）</summary>
	void CreateAll();
	/// <summary>シーク：showSec より後なら作る、preloadSec より後なら先読みを頼む（前へ戻っても捨てない）</summary>
	void Seek(float stageSec);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	/// <summary>値を書き換えたら true（呼ぶ側が保存する）</summary>
	bool OnImGui();

private:
	enum class State {
		Waiting,    // preloadSec 前
		Queued,     // 先読みの順番待ち（ScenePreloader が他を読んでいる）
		Preloading, // 先読み中
		Ready,      // 先読みが終わった（マニフェストが無い時もここ）
		Created,    // 配置を作った
	};
	struct Group {
		std::string name;
		float preloadSec = 0.0f;
		float showSec = 0.0f;
		State state = State::Waiting;
		double preloadBeginMs = 0.0;
	};

	std::string ManifestName(const Group& g) const { return sceneName_ + "@" + g.name; }
	/// <summary>先読みが終わっていたら Ready にする</summary>
	void PollPreload();
	void StartQueuedPreload();
	void Create(size_t index);
	static const char* StateName(State s);

	IStageStreamingHost* host_ = nullptr;
	std::string sceneName_;
	int preloadingIndex_ = -1; // いま ScenePreloader が読んでいるグループ（groups_ の添字）
	std::vector<Group> groups_ = {
		// 雲の中で読み、雲の下面の少し手前で作る（カメラは 61〜62 秒で雲を抜ける。2026-10-25）
		{ "LowAltitude", 51.0f, 59.0f },
		{ "Plateau",     51.0f, 59.0f }, // 縁の向こうの台地。72 秒から見えるので低空と同じ時刻
	};
};
