#include "TitleDemoMovie.h"

#include "Media/MoviePlayer.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "MouseInput.h"
#include "ControllerInput.h"
#include "WindowsApplication.h"
#include "Json/JsonParser.h"
#include "LogBuffer.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/TitleDemo.json";
	constexpr const char* kWhiteTexture = "Resources/Textures/white1x1.dds";
	constexpr const char* kMovieTextureName = "##TitleDemoMovie##";
	// マウスの移動を「触った」と数える量（1 フレームの移動量の合計 [カウント]。机の振動などで戻らないように少し持たせる）
	constexpr long kMouseMoveThreshold = 3;
	// スティックを「触った」と数える倒し具合（デッドゾーンの後の 0..1）
	constexpr float kStickThreshold = 0.3f;
	// 1 フレームの経過時間の上限（ロードなどで止まったときに一気に進めない）
	constexpr float kMaxDeltaSeconds = 0.1f;

	float ReadFloat(const JsonValue& j, const char* key, float fallback) {
		const JsonValue& v = j[key];
		return v.IsNumber() ? static_cast<float>(v.AsDouble(fallback)) : fallback;
	}

	float Ratio(float time, float duration) {
		return (duration > 0.0f) ? std::clamp(time / duration, 0.0f, 1.0f) : 1.0f;
	}
}

TitleDemoMovie::TitleDemoMovie() = default;
TitleDemoMovie::~TitleDemoMovie() = default;

void TitleDemoMovie::Initialize(SpriteManager* spriteManager, InputManager* input) {
	spriteManager_ = spriteManager;
	input_ = input;
	LoadTuning();
	player_ = std::make_unique<MoviePlayer>();

	TextureManager::GetInstance()->LoadTexture(kWhiteTexture);
	blackSprite_ = std::make_unique<SpriteInstance>();
	blackSprite_->Initialize(spriteManager_, kWhiteTexture, "TitleDemoBlack");
	blackSprite_->SetPosition({ 0.0f, 0.0f });
	blackSprite_->SetSize({ static_cast<float>(WindowsApplication::kUIWidth), static_cast<float>(WindowsApplication::kUIHeight) });
	blackSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });

	state_ = State::Idle;
	idleTime_ = 0.0f;
	graceTime_ = 0.0f;
	hasLastTime_ = false;
}

void TitleDemoMovie::Finalize() {
	if (player_) player_->Stop();
	movieSprite_.reset();
	blackSprite_.reset();
}

void TitleDemoMovie::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) {
		LogBuffer::Instance().Add(std::string("TitleDemoMovie: JSON を読めない: ") + kTuningPath, LogBuffer::Level::Warning);
		return;
	}
	const JsonValue& root = result.value;
	idleSeconds = ReadFloat(root, "idleSeconds", idleSeconds);
	fadeInSeconds = ReadFloat(root, "fadeInSeconds", fadeInSeconds);
	movieFadeInSeconds = ReadFloat(root, "movieFadeInSeconds", movieFadeInSeconds);
	returnFadeSeconds = ReadFloat(root, "returnFadeSeconds", returnFadeSeconds);
	inputGraceSeconds = ReadFloat(root, "inputGraceSeconds", inputGraceSeconds);
	if (const JsonValue& path = root["moviePath"]; path.IsString()) moviePath = path.AsString();
}

bool TitleDemoMovie::AnyActivity() const {
	if (!input_) return false;
	if (auto* actionMap = input_->GetActionMap(); actionMap && actionMap->AnyInputTriggered()) return true;
	if (auto* mouse = input_->GetMouse()) {
		if (std::abs(mouse->GetDeltaX()) + std::abs(mouse->GetDeltaY()) >= kMouseMoveThreshold) return true;
		if (mouse->GetDeltaWheel() != 0) return true;
	}
	if (auto* pad = input_->GetController(); pad && pad->IsConnected()) {
		if (pad->GetLeftStick().magnitude >= kStickThreshold) return true;
		if (pad->GetRightStick().magnitude >= kStickThreshold) return true;
	}
	return false;
}

void TitleDemoMovie::ChangeState(State next) {
	state_ = next;
	stateTime_ = 0.0f;
}

void TitleDemoMovie::StartNow() {
	if (state_ != State::Idle) return;
	// 映像はフェードの後で開く（裏で再生が進んで冒頭が欠けないように）。ここでは在るかだけ見る
	if (!std::filesystem::exists(moviePath)) {
		LogBuffer::Instance().Add("TitleDemoMovie: 動画が無い: " + moviePath, LogBuffer::Level::Warning);
		idleTime_ = 0.0f;
		return;
	}
	movieTime_ = 0.0f;
	ChangeState(State::FadeIn);
}

void TitleDemoMovie::Update(bool canIdle) {
	// 実時間で数える（タイトルの時間の倍率・ヒットストップの影響を受けない）
	const auto now = std::chrono::steady_clock::now();
	float dt = 0.0f;
	if (hasLastTime_) {
		dt = (std::min)(std::chrono::duration<float>(now - lastTime_).count(), kMaxDeltaSeconds);
	}
	lastTime_ = now;
	hasLastTime_ = true;

	const bool activity = AnyActivity();
	stateTime_ += dt;
	graceTime_ = (std::max)(0.0f, graceTime_ - dt);

	switch (state_) {
	case State::Idle:
		if (!canIdle || activity) {
			idleTime_ = 0.0f;
			return;
		}
		idleTime_ += dt;
		if (idleTime_ >= idleSeconds) StartNow();
		return;

	case State::FadeIn:
	case State::Playing: {
		if (state_ == State::FadeIn && stateTime_ >= fadeInSeconds) {
			ChangeState(State::Playing);
			player_->Start(moviePath, kMovieTextureName);
		}
		if (state_ == State::Playing && player_->HasFrame()) movieTime_ += dt;
		// 触った・映像が読めなくなった・タイトルが別の所へ動いた → タイトルへ戻る
		const bool failed = (state_ == State::Playing) && player_->HasFailed();
		if (activity || failed || !canIdle) {
			if (failed) LogBuffer::Instance().Add("TitleDemoMovie: 動画を読めない: " + moviePath, LogBuffer::Level::Warning);
			// フェードインの途中なら、今の黒の濃さから戻す
			const float alpha = (state_ == State::FadeIn) ? Ratio(stateTime_, fadeInSeconds) : 1.0f;
			player_->Stop();
			ChangeState(State::FadeOut);
			stateTime_ = (1.0f - alpha) * returnFadeSeconds;
		}
		return;
	}

	case State::FadeOut:
		// 戻る途中に触っても数えない（戻るきっかけの続きの入力）
		if (stateTime_ >= returnFadeSeconds) {
			ChangeState(State::Idle);
			idleTime_ = 0.0f;
			graceTime_ = inputGraceSeconds;
		}
		return;
	}
}

bool TitleDemoMovie::BlocksTitleInput() const {
	return state_ != State::Idle || graceTime_ > 0.0f;
}

void TitleDemoMovie::UpdateMovieSprite() {
	if (!player_->HasFrame()) return;
	const std::string& name = player_->GetTextureName();
	if (!movieSprite_) {
		movieSprite_ = std::make_unique<SpriteInstance>();
		movieSprite_->Initialize(spriteManager_, name, "TitleDemoMovie");
	} else {
		movieSprite_->SetTexture(name);
	}

	// 縦横比を保って画面に収める（余りは黒い幕が見える）
	const float screenW = static_cast<float>(WindowsApplication::kUIWidth);
	const float screenH = static_cast<float>(WindowsApplication::kUIHeight);
	const float movieW = static_cast<float>(player_->GetWidth());
	const float movieH = static_cast<float>(player_->GetHeight());
	const float scale = (std::min)(screenW / movieW, screenH / movieH);
	const float w = movieW * scale;
	const float h = movieH * scale;
	movieSprite_->SetPosition({ (screenW - w) * 0.5f, (screenH - h) * 0.5f });
	movieSprite_->SetSize({ w, h });
	movieSprite_->SetTextureLeftTop({ 0.0f, 0.0f });
	movieSprite_->SetTextureSize({ movieW, movieH });
	movieSprite_->SetColor({ 1.0f, 1.0f, 1.0f, Ratio(movieTime_, movieFadeInSeconds) });
}

void TitleDemoMovie::Draw() {
	if (state_ == State::Idle || !blackSprite_) return;

	float blackAlpha = 1.0f;
	if (state_ == State::FadeIn) blackAlpha = Ratio(stateTime_, fadeInSeconds);
	if (state_ == State::FadeOut) blackAlpha = 1.0f - Ratio(stateTime_, returnFadeSeconds);

	// 新しいフレームをテクスチャへ（コピーをコマンドリストに積むので描画の前に）
	const bool showMovie = (state_ == State::Playing);
	if (showMovie) {
		player_->UpdateTexture();
		UpdateMovieSprite();
	}

	spriteManager_->DrawSetting();
	blackSprite_->SetColor({ 0.0f, 0.0f, 0.0f, blackAlpha });
	blackSprite_->Update();
	blackSprite_->Draw();
	if (showMovie && movieSprite_ && player_->HasFrame()) {
		movieSprite_->Update();
		movieSprite_->Draw();
	}
}
