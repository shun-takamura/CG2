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
	/// <summary>作った group の配置を消して「まだ作っていない」へ戻す（シークで戻れば作り直せる）。消した数を返す</summary>
	virtual size_t DestroyStreamGroup(const std::string& group) = 0;
	/// <summary>プレハブのモデルを読んでおく（Wave で後から出る物。作るときに呼ぶのでマニフェストにも記録される）</summary>
	virtual void WarmPrefabModels(const std::vector<std::string>& prefabs) = 0;
};

/// <summary>
/// 区間ごとのストリーミングの時刻表（11 番 L9 §6）。
/// シーン JSON の streamGroup 付きの配置は開始時に作らず、グループごとに
///   preloadSec を過ぎたら ScenePreloader でアセットを裏で読み（マニフェスト Resources/Json/Preload/<シーン>@<グループ>.json）、
///   showSec を過ぎたら配置を作る（キャッシュに当たるだけ。先読みが間に合っていなければ残りを済ませてから作る）。
///   unloadSec（0 より大きい時だけ）を過ぎたら配置を消す（低空の地形を谷に入ってから捨てる。シークで戻れば作り直す）。
/// prefabs は作るときにモデルを読んでおくプレハブ（谷の障害物など、Wave で後から出る物）。
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
	/// <summary>時刻に関係なく全部作る（着陸・ボス戦）。unloadSec のあるグループ（レールの間だけの物）は作らず、作ってあれば消す</summary>
	void CreateAll();
	/// <summary>シーク：unloadSec より後なら消す、showSec より後なら作る、preloadSec より後なら先読みを頼む</summary>
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
		Unloaded,   // unloadSec を過ぎて消した
	};
	struct Group {
		std::string name;
		float preloadSec = 0.0f;
		float showSec = 0.0f;
		float unloadSec = 0.0f; // 0 = 捨てない
		std::vector<std::string> prefabs;
		State state = State::Waiting;
		double preloadBeginMs = 0.0;
	};

	std::string ManifestName(const Group& g) const { return sceneName_ + "@" + g.name; }
	/// <summary>先読みが終わっていたら Ready にする</summary>
	void PollPreload();
	void StartQueuedPreload();
	void Create(size_t index);
	void Unload(size_t index);
	static const char* StateName(State s);

	IStageStreamingHost* host_ = nullptr;
	std::string sceneName_;
	int preloadingIndex_ = -1; // いま ScenePreloader が読んでいるグループ（groups_ の添字）
	std::vector<Group> groups_ = {
		// 雲の中で読み、雲の下面の少し手前で作る（カメラは 61〜62 秒で雲を抜ける。2026-10-25）。
		// 谷に入ったら（≈126 秒）捨てる（11 番 §3.0 の時刻表）
		{ "LowAltitude", 51.0f, 59.0f, 126.0f },
		{ "Plateau",     51.0f, 59.0f }, // 縁の向こうの台地。72 秒から見えるので低空と同じ時刻。谷の間ずっと見えるので捨てない
		// 谷の物は蛇行の山の陰で読む。谷の中が低空から初めて見えるのは 113 秒（gen_terrain.py --visibility、2026-10-29）。
		// 障害物・崩れた部品・砲台は Wave で 119 秒から出るので、作るときにモデルも読んでおく
		{ "Canyon",      95.0f, 106.0f, 0.0f, {
			"canyon_spire_l", "canyon_spire_s", "canyon_beam",
			"canyon_spire_l_stump", "canyon_spire_l_top", "canyon_spire_s_stump", "canyon_spire_s_top",
			"canyon_beam_stub_a", "canyon_beam_half_a", "canyon_beam_half_b", "canyon_beam_stub_b", "canyon_chunk",
			"turret", "HoverEnemy" } },
	};
};
