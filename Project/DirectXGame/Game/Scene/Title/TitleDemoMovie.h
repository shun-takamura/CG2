#pragma once
#include <chrono>
#include <memory>
#include <string>

class SpriteManager;
class SpriteInstance;
class InputManager;
class MoviePlayer;

/// <summary>
/// タイトルの放置デモ（展示用）。タイトルで一定時間（TitleDemo.json の idleSeconds）何も触らないと、
/// 黒へフェードしてデモ映像（mp4）をループで流す。マウスを動かす・キー・ゲームパッドのどれかでタイトルへ戻る。
/// 戻るきっかけの入力はタイトルに渡さない（BlocksTitleInput の間、タイトルは入力を読まない）。
/// 音はタイトルの BGM のまま（動画の音は読まない）。時間は実時間で数える。5_TitleScene.md「放置デモ」
/// </summary>
class TitleDemoMovie {
public:
	TitleDemoMovie();
	~TitleDemoMovie();

	void Initialize(SpriteManager* spriteManager, InputManager* input);
	void Finalize();

	/// <summary>
	/// 毎フレーム、タイトルの入力処理より前に呼ぶ。
	/// canIdle が false の間（扉に入る途中・トランジション中など）は放置の時間を数えない
	/// </summary>
	void Update(bool canIdle);

	/// <summary>デモ映像を出している間と、戻った直後の少しの間は true（タイトルは入力を読まない）</summary>
	bool BlocksTitleInput() const;

	/// <summary>シーンの描画先に、黒い幕とデモ映像を全画面で重ねる（Draw の最後で呼ぶ）</summary>
	void Draw();

	/// <summary>黒へのフェードを始める（動画が無ければ何もしない）</summary>
	void StartNow();

	// ----- TitleDemo.json -----
	float idleSeconds = 30.0f;          // 何も触らずにこれだけ経ったら流す
	float fadeInSeconds = 1.0f;         // 黒へフェードする長さ
	float movieFadeInSeconds = 0.5f;    // 黒の上に映像が出てくる長さ
	float returnFadeSeconds = 0.3f;     // 触ってからタイトルが見えるまでの長さ
	float inputGraceSeconds = 0.3f;     // タイトルが見えてから入力を受け付けるまで
	std::string moviePath = "Resources/Movies/TitleDemo.mp4";

private:
	enum class State {
		Idle,       // 放置の時間を数えている
		FadeIn,     // 黒へフェード（映像は裏で開いている）
		Playing,    // 映像を流している
		FadeOut,    // 触ったのでタイトルへ戻る途中
	};

	void LoadTuning();
	bool AnyActivity() const;
	void ChangeState(State next);
	void UpdateMovieSprite();

	SpriteManager* spriteManager_ = nullptr;
	InputManager* input_ = nullptr;
	std::unique_ptr<MoviePlayer> player_;
	std::unique_ptr<SpriteInstance> blackSprite_;
	std::unique_ptr<SpriteInstance> movieSprite_;

	State state_ = State::Idle;
	float idleTime_ = 0.0f;
	float stateTime_ = 0.0f;
	float movieTime_ = 0.0f;       // 最初のフレームが出てからの秒（映像のフェードイン用）
	float graceTime_ = 0.0f;       // 戻ってからの残りの入力無視 [s]
	std::chrono::steady_clock::time_point lastTime_{};
	bool hasLastTime_ = false;
};
