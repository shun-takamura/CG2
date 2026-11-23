#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include "BaseTransition.h"

class SpriteManager;
class DirectXCore;

/// <summary>
/// トランジション履歴の記録
/// </summary>
struct TransitionRecord {
	TransitionType type;			// 使用したトランジションの種類
	std::string transitionName;		// トランジション名
	std::string fromScene;			// 遷移元シーン名
	std::string toScene;			// 遷移先シーン名
};

/// <summary>
/// トランジション管理クラス
/// シングルトンにする理由：画面の切り替え演出は同時に 1 つだけで、シーンをまたいで続くため。
/// </summary>
class TransitionManager {
public:
	/// <summary>シングルトンのインスタンスを返す</summary>
	/// <returns>インスタンス</returns>
	static TransitionManager* GetInstance();

	/// <summary>
	/// 開く側に指定すると、開く演出なしで終わる（覆い切って Hold を抜けた時点で終了し、行き先のシーンが自分で開く）
	/// </summary>
	static constexpr TransitionType kRevealByScene = TransitionType::None;

	/// <summary>
	/// 覆う側に指定すると、シーンが自分で覆い切った（演じ終えた）ものとして、覆い切った状態から始める。
	/// シーンは演じ終えてから ChangeScene を呼ぶ。開く側が kRevealByScene なら、黒のまま差し替えて開かずに終わる
	/// （ただし SceneManager::ChangeScene はこの組み合わせをここへ渡さず、暗転なしで差し替える。16_StageStart.md §4.2）。
	/// kRevealByScene と同じ値（None ＝ その側はシーンが演じる）で、覆う側と開く側のどちらに書くかで区別する
	/// </summary>
	static constexpr TransitionType kCoverByScene = TransitionType::None;

	/// <summary>白テクスチャを作り、既定のトランジション（Stripe / Fade / Ink / FeatherLand / FeatherRise）を作って登録し、調整値を読む</summary>
	/// <param name="spriteManager">スプライトの描画に使うマネージャー</param>
	/// <param name="dxCore">GPU リソースの作成と経過時間に使う</param>
	/// <param name="screenWidth">画面幅。0 以下なら UI の幅（WindowsApplication::kUIWidth）を使う</param>
	/// <param name="screenHeight">画面高さ。0 以下なら UI の高さ（WindowsApplication::kUIHeight）を使う</param>
	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth = 0.0f, float screenHeight = 0.0f);

	/// <summary>登録した全トランジションを終了して捨て、履歴も消す</summary>
	void Finalize();

	/// <summary>今のトランジションを進め、覆う側が終わったら同じフレームのうちに開く側へ引き継ぐ</summary>
	void Update();

	/// <summary>今のトランジションを描く</summary>
	void Draw();

	/// <summary>トランジションを登録する（同じ種類があれば差し替える）</summary>
	/// <param name="type">トランジションの種類</param>
	/// <param name="transition">トランジションのインスタンス（所有権を受け取る）</param>
	void RegisterTransition(TransitionType type, std::unique_ptr<BaseTransition> transition);

	/// <summary>指定したトランジションで遷移を始める（覆う側がそのまま開く）</summary>
	/// <param name="type">トランジションの種類</param>
	/// <param name="fromScene">遷移元シーン名</param>
	/// <param name="toScene">遷移先シーン名</param>
	/// <param name="onSceneChange">覆い切ったときに呼ぶシーンの差し替え</param>
	void StartTransition(TransitionType type, const std::string& fromScene,
		const std::string& toScene, std::function<void()> onSceneChange);

	/// <summary>
	/// 覆う側と開く側を別の種類にして遷移を始める（例：墨で覆ってストライプで開く）。
	/// トランジション中なら何もしない。覆う側が登録されていなければ onSceneChange をすぐ呼ぶ
	/// </summary>
	/// <param name="coverType">覆う側。kCoverByScene ならシーンが覆い終えた所（覆い切った状態）から始める</param>
	/// <param name="revealType">開く側。coverType と同じなら覆う側がそのまま開く。kRevealByScene なら開く演出なし</param>
	/// <param name="fromScene">遷移元シーン名</param>
	/// <param name="toScene">遷移先シーン名</param>
	/// <param name="onSceneChange">覆い切ったときに呼ぶシーンの差し替え</param>
	void StartTransition(TransitionType coverType, TransitionType revealType,
		const std::string& fromScene, const std::string& toScene, std::function<void()> onSceneChange);

	/// <summary>登録済みの中からランダムに選んだトランジションで遷移を始める（1 つも無ければ onSceneChange をすぐ呼ぶ）</summary>
	/// <param name="fromScene">遷移元シーン名</param>
	/// <param name="toScene">遷移先シーン名</param>
	/// <param name="onSceneChange">覆い切ったときに呼ぶシーンの差し替え</param>
	void StartRandomTransition(const std::string& fromScene,
		const std::string& toScene, std::function<void()> onSceneChange);

	/// <summary>トランジション中かどうかを返す</summary>
	/// <returns>今のトランジションが動いていれば true</returns>
	bool IsTransitioning() const;

	/// <summary>今の段階を返す。覆う側から開く側へ引き継いでも途切れない</summary>
	/// <returns>覆う / 隠したまま / 開く / 何もしていない</returns>
	TransitionState GetPhase() const;

	/// <summary>画面が覆われていく途中か、覆われているかを返す（シーンが止まっていてよい区間）</summary>
	/// <returns>覆う途中か隠したままなら true</returns>
	bool IsCovering() const;

	/// <summary>今のトランジションを入力で飛ばせるかを返す（種類で決める。今は羽根の着水だけ＝19 番 §4.2.3、2026-10-24 ユーザー）</summary>
	/// <returns>飛ばせるなら true</returns>
	bool IsSkippable() const;
	/// <summary>今のトランジションの残りを飛ばす（飛ばせる種類のときだけ。開く側が別の種類なら、開く側も飛ばす）</summary>
	void RequestSkip();

	/// <summary>現在のトランジションを返す</summary>
	/// <returns>現在のトランジション。動いていなければ nullptr のこともある</returns>
	BaseTransition* GetCurrentTransition() const { return currentTransition_; }

	/// <summary>現在のトランジションの種類を返す</summary>
	/// <returns>種類。現在のトランジションが無ければ TransitionType::None</returns>
	TransitionType GetCurrentTransitionType() const;

	/// <summary>最後に使ったトランジションの記録を返す</summary>
	/// <returns>記録</returns>
	const TransitionRecord& GetLastTransitionRecord() const { return lastRecord_; }

	/// <summary>トランジションの履歴を返す</summary>
	/// <returns>古い順の記録の一覧</returns>
	const std::vector<TransitionRecord>& GetTransitionHistory() const { return history_; }

	/// <summary>履歴を消す</summary>
	void ClearHistory() { history_.clear(); }

	/// <summary>登録されているトランジションの種類の一覧を返す</summary>
	/// <returns>種類の一覧</returns>
	std::vector<TransitionType> GetAvailableTransitions() const;

	/// <summary>調整値の保存・再読み込みと、各トランジションの調整を ImGui で表示する（Debug）</summary>
	void OnImGui();

	/// <summary>全トランジションの調整値を Resources/Json/Tuning/Transition.json から読む（トランジションごとに GetName() の欄。Initialize の最後に読む）</summary>
	void LoadTuning();
	/// <summary>全トランジションの調整値を Resources/Json/Tuning/Transition.json へ書く（調整値を持たないものは欄を作らない）</summary>
	void SaveTuning() const;

	/// <summary>特定のトランジションを型を指定して取得する（設定変更用）</summary>
	/// <param name="type">トランジションの種類</param>
	/// <returns>見つかって T に変換できればそのトランジション。無ければ nullptr</returns>
	template<typename T>
	T* GetTransition(TransitionType type) {
		auto it = transitions_.find(type);
		if (it != transitions_.end()) {
			return dynamic_cast<T*>(it->second.get());
		}
		return nullptr;
	}

private:
	TransitionManager() = default;
	~TransitionManager() = default;
	TransitionManager(const TransitionManager&) = delete;
	TransitionManager& operator=(const TransitionManager&) = delete;

	/// <summary>
	/// 覆う側が kCoverByScene のときの開始（覆い切った状態の Hold から）。
	/// 開く側もシーンが演じるなら、黒の Fade を開かずに終わらせて真っ暗のまま差し替える
	/// </summary>
	/// <param name="revealType">開く側の種類</param>
	/// <param name="fromScene">遷移元シーン名</param>
	/// <param name="toScene">遷移先シーン名</param>
	/// <param name="onSceneChange">次のフレームで呼ぶシーンの差し替え</param>
	void StartCoveredByScene(TransitionType revealType,
		const std::string& fromScene, const std::string& toScene, std::function<void()> onSceneChange);

	// 登録されたトランジション
	std::unordered_map<TransitionType, std::unique_ptr<BaseTransition>> transitions_;

	// 現在アクティブなトランジション
	BaseTransition* currentTransition_ = nullptr;

	// 覆う側が終わったら引き継ぐ開く側（覆う側と同じ種類 / 開く演出なしのときは nullptr）
	BaseTransition* pendingReveal_ = nullptr;
	// 覆う側が飛ばされた（引き継ぐ開く側も飛ばす）
	bool skipPendingReveal_ = false;

	// 最後のトランジション記録
	TransitionRecord lastRecord_;

	// トランジション履歴
	std::vector<TransitionRecord> history_;

	// マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	// Initialize で実クライアントサイズを受け取る
	float screenWidth_ = 0.0f;
	float screenHeight_ = 0.0f;
};