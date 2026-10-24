#include "GameOverScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "UIPointer.h"
#include "Score/BossRetryState.h"
#include "GameOver/EtherVessel.h"
#include "GameOver/EtherPillarFlow.h"
#include "Water/WaterSurface.h"
#include "LightManager.h"
#include "PostEffect.h"
#include "TextureManager.h"
#include "AssetLocator.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "MathUtility.h"   // kPi
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/GameOver.json";
	// 器の映り込み。神殿の IBL（無ければタイトルの空）
	constexpr const char* kIblPath = "Resources/Cubemaps/hub_temple_ibl.dds";
	constexpr const char* kFallbackIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";
	// 水底はタイトル・ボス戦と同じ石畳（tools/Python/gen_flagstone_material.py）。暗い水の底にうっすら
	constexpr const char* kFloorTexturePath = "Resources/Textures/Terrain/Flagstone_BaseColor.dds";
	constexpr const char* kFloorNormalMapPath = "Resources/Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.dds";
	constexpr const char* kFloorHeightMapPath = "Resources/Textures/MaskTexture/Terrain/Flagstone_Height.dds";
	constexpr float kFloorTileMeters = 3.0f;
	constexpr float kFloorParallaxDepth = (0.0068f + 0.013f) / kFloorTileMeters;
	constexpr float kWaterSize = 400.0f;   // カメラ追従
	constexpr float kDegToRad = kPi / 180.0f;
	// TextRenderer のフォントの高さ（Framework::Initialize の MPLUS1p 32px）
	constexpr float kFontPixelHeight = 32.0f;
	// 器の原点（土台の脚の下）。水面の真ん中に立つ
	const Vector3 kVesselOrigin{ 0.0f, 0.0f, 0.0f };

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}
	float SmootherStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
	}

	void ReadFloat(const JsonValue& node, const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	}
	void WriteFloat(JsonValue& node, const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	}
	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}
	void ReadColor3(const JsonValue& node, float (&value)[3]) {
		if (!node.IsArray() || node.Size() < 3) return;
		for (int i = 0; i < 3; ++i) value[i] = static_cast<float>(node[static_cast<size_t>(i)].AsDouble(value[i]));
	}
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}
	JsonValue MakeColor3(const float (&c)[3]) {
		return MakeVector3({ c[0], c[1], c[2] });
	}
}

GameOverScene::GameOverScene() = default;
GameOverScene::~GameOverScene() = default;

void GameOverScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	iblPath_ = AssetLocator::GetInstance()->Exists(kIblPath) ? kIblPath : kFallbackIblPath;
	TextureManager::GetInstance()->LoadTexture(iblPath_);
	object3DManager_->SetEnvironmentTexture(iblPath_);
	// ステージの距離フォグはシーンをまたいで残るので切る
	object3DManager_->DisableFog();

	backgroundSprite_ = std::make_unique<SpriteInstance>();
	backgroundSprite_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "GameOverBackground");
	backgroundSprite_->SetPosition({ 0.0f, 0.0f });
	backgroundSprite_->SetSize({ static_cast<float>(dxCore_->GetUIWidth()), static_cast<float>(dxCore_->GetUIHeight()) });

	vessel_ = std::make_unique<EtherVessel>();
	vessel_->Initialize(dxCore_, srvManager_, object3DManager_);
	vessel_->SetOrigin(kVesselOrigin);
	InitializeWater();
	LoadTuning();
	ApplyWaterTuning();   // JSON が無くても既定値を水面へ

	hasBossCheckpoint_ = BossRetryState::GetInstance()->HasCheckpoint();
	options_.clear();
	if (hasBossCheckpoint_) options_.push_back(Option::BossRetry);
	options_.push_back(Option::Restart);
	options_.push_back(Option::Withdraw);
	selectedIndex_ = 0;

	// 死んだ分は入口で黙って減らす（§3：どれだけ減ったかは画面に出さない）
	EtherState* ether = EtherState::GetInstance();
	death_ = ether->ApplyDeath();
	fillBefore_ = ether->BetToFill(death_.betBefore);
	fillAfter_ = ether->BetToFill(death_.betAfter);
	difficultyBefore01_ = ether->DifficultyTo01(death_.difficultyBefore);
	difficultyAfter01_ = ether->DifficultyTo01(death_.difficultyAfter);
	StartFlow();
}

void GameOverScene::Finalize() {
	if (vessel_) vessel_->Finalize();
}

void GameOverScene::InitializeWater() {
	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore_, srvManager_, object3DManager_, kFloorTexturePath);
	water_->SetSize(kWaterSize);
	water_->SetFollowCamera(true);
	water_->GetParams().floorTiling = 1.0f / kFloorTileMeters;
	water_->SetFloorMaps(kFloorNormalMapPath, kFloorHeightMapPath, kFloorParallaxDepth);
	water_->SetRippleCenter(kVesselOrigin);
}

void GameOverScene::StartFlow() {
	step_ = Step::Emerge;
	stepTime_ = 0.0f;
	pullTimer_ = 0.0f;
	tickFired_ = false;
	optionsAlpha_ = 0.0f;
	vessel_->StopRelease();
	vessel_->SetGlowBoost(1.0f);
	vessel_->SetFill(fillBefore_, true);
	vessel_->SetDifficulty01(difficultyBefore01_, true);
}

void GameOverScene::SkipToChoose() {
	vessel_->StopRelease();
	vessel_->SetFill(fillAfter_, true);
	if (!tickFired_) vessel_->SetDifficulty01(difficultyAfter01_, false);
	tickFired_ = true;
	step_ = Step::Choose;
	stepTime_ = 0.0f;
}

void GameOverScene::UpdateFlow(float deltaTime) {
	const bool transitioning = SceneManager::GetInstance()->IsTransitioning();
	switch (step_) {
	case Step::Emerge:
		// 着水の波紋の中から浮かび上がる間は待つ。カメラが引き終わってから減らす（動いている間は減ったのが見えない）
		if (transitioning) break;
		stepTime_ += deltaTime;
		if (pullTimer_ >= pullDelay_ + pullSeconds_ + drainDelay_) {
			step_ = Step::Drain;
			stepTime_ = 0.0f;
			// 針の動き・液面の減り・昇る糸を同時に始める（2026-10-24 ユーザー）。下げますか？とは聞かない
			vessel_->SetDifficulty01(difficultyAfter01_, false);
			tickFired_ = true;
			if (fillAfter_ < fillBefore_) vessel_->BeginRelease(1.0f);
		}
		break;
	case Step::Drain: {
		stepTime_ += deltaTime;
		const float t = drainSeconds_ > 1e-3f ? std::clamp(stepTime_ / drainSeconds_, 0.0f, 1.0f) : 1.0f;
		// 始めからすっと下がり、最後だけゆるめる（ease-out。smoothstep は出だしが溜まって糸だけ先に見えた）
		const float u = 1.0f - (1.0f - t) * (1.0f - t);
		vessel_->SetFill(fillBefore_ + (fillAfter_ - fillBefore_) * u, true);
		// 糸の本数は液面の下がる速さ（傾き 2(1−t)）に合わせる＝始めがいちばん多く、止まる所で 0
		vessel_->SetReleaseRate(1.0f - t);
		// 粒は寿命があるので少し前に止め、液面が止まるころに消えるように
		if (t >= particleStopRatio_) vessel_->StopReleaseParticles();
		if (t >= 1.0f) {
			vessel_->StopRelease();
			step_ = Step::Tick;
			stepTime_ = 0.0f;
		}
		break;
	}
	case Step::Tick:
		// 液体の揺れが収まるのを待つ
		stepTime_ += deltaTime;
		if (stepTime_ >= tickSettleSeconds_) {
			step_ = Step::Choose;
			stepTime_ = 0.0f;
		}
		break;
	case Step::Choose:
		stepTime_ += deltaTime;
		optionsAlpha_ = optionsFadeSeconds_ > 1e-3f ? (std::min)(optionsAlpha_ + deltaTime / optionsFadeSeconds_, 1.0f) : 1.0f;
		break;
	case Step::Rise: {
		stepTime_ += deltaTime;
		optionsAlpha_ = optionsFadeOutSeconds_ > 1e-3f ? (std::max)(optionsAlpha_ - deltaTime / optionsFadeOutSeconds_, 0.0f) : 0.0f;
		// 球の光が強まる（賭けたエーテルで機体を組み直す）
		const float t = riseSeconds_ > 1e-3f ? std::clamp(stepTime_ / riseSeconds_, 0.0f, 1.0f) : 1.0f;
		vessel_->SetGlowBoost(1.0f + (riseGlowBoost_ - 1.0f) * SmoothStep01(t));
		if (t >= 1.0f) {
			step_ = Step::Leaving;
			if (chosen_ == Option::BossRetry) BossRetryState::GetInstance()->RequestBossRetry();
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY");
		}
		break;
	}
	case Step::Return: {
		stepTime_ += deltaTime;
		optionsAlpha_ = optionsFadeOutSeconds_ > 1e-3f ? (std::max)(optionsAlpha_ - deltaTime / optionsFadeOutSeconds_, 0.0f) : 0.0f;
		// 残ったエーテルが支柱を伝って泉へ下りる（＝手元に戻る）
		const float t = returnSeconds_ > 1e-3f ? std::clamp(stepTime_ / returnSeconds_, 0.0f, 1.0f) : 1.0f;
		vessel_->SetFill(returnFillFrom_ * (1.0f - SmoothStep01(t)), true);
		if (t >= 1.0f) vessel_->GetPillarFlow()->StopDescend();
		if (stepTime_ >= returnSeconds_ + returnTailSeconds_) {
			step_ = Step::Leaving;
			// ポーズの「ステージセレクトに戻る」（⑩）と同じく、コクピットの起動から
			SceneManager::GetInstance()->ChangeScene("STAGESELECT");
		}
		break;
	}
	case Step::Leaving:
		optionsAlpha_ = optionsFadeOutSeconds_ > 1e-3f ? (std::max)(optionsAlpha_ - deltaTime / optionsFadeOutSeconds_, 0.0f) : 0.0f;
		break;
	}

	// 支柱を下りた光が泉に着いたら、足元の水面に波紋
	if (EtherPillarFlow* flow = vessel_->GetPillarFlow()) {
		for (int n = 0; n < flow->GetArrivals(); ++n) {
			water_->EmitRing(kVesselOrigin, arrivalRingAmplitude_, cursorRingWavelength_, 2.5f, cursorRingRadius_);
		}
	}
}

void GameOverScene::UpdateCamera(float deltaTime) {
	// トランジションの間は近い位置のまま（着水の波紋 → 液面の波紋へつなぐ）。明けたら遅れて引く
	if (SceneManager::GetInstance()->IsTransitioning() && step_ == Step::Emerge) {
		pullTimer_ = 0.0f;
	} else {
		pullTimer_ += deltaTime;
	}
	const float t = SmootherStep01((pullTimer_ - pullDelay_) / (std::max)(pullSeconds_, 0.01f));
	auto lerp = [t](const Vector3& a, const Vector3& b) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	};
	const Vector3 pos = lerp(cameraNearPos_, cameraFarPos_);
	const Vector3 rotDeg = lerp(cameraNearRotDeg_, cameraFarRotDeg_);
	camera_->SetTranslate({ kVesselOrigin.x + pos.x, kVesselOrigin.y + pos.y, kVesselOrigin.z + pos.z });
	camera_->SetRotate({ rotDeg.x * kDegToRad, rotDeg.y * kDegToRad, rotDeg.z * kDegToRad });
	camera_->Update();
}

void GameOverScene::EmitCursorRing() {
	water_->EmitRing(kVesselOrigin, cursorRingAmplitude_, cursorRingWavelength_, 2.5f, cursorRingRadius_);
}

void GameOverScene::Confirm(Option option) {
	chosen_ = option;
	stepTime_ = 0.0f;
	if (option == Option::Withdraw) {
		EtherState::GetInstance()->Withdraw();
		returnFillFrom_ = fillAfter_;
		vessel_->GetPillarFlow()->BeginDescend();
		step_ = Step::Return;
	} else {
		// 再挑戦は糸も粒も出さない（2026-10-24 ユーザー）。光が強まるだけ
		vessel_->Splash(0.006f);
		step_ = Step::Rise;
	}
}

void GameOverScene::UpdateChoose() {
	// トランジションの間と、明けたフレームは入力を読まない（着水を飛ばした 1 回の押下が早送りや決定に化けないように）
	const bool transitioning = SceneManager::GetInstance()->IsTransitioning();
	const bool justRevealed = wasTransitioning_ && !transitioning;
	wasTransitioning_ = transitioning;
	if (transitioning || justRevealed) return;
	auto* actions = input_->GetActionMap();
	if (!actions || options_.empty()) return;
	const bool confirmed = actions->IsTriggered(static_cast<int>(Action::MenuConfirm));
	// マウス（Debug のビューポート窓・Release の黒帯の換算は UIPointer）
	const UIPointer pointer = UIPointer::FromInput(input_);
	const bool clicked = pointer.valid && pointer.clicked;

	// 減る〜カチッの途中で決定かクリックをしたら、選択肢まで早送り（押した 1 回は選ばない）
	if (step_ == Step::Emerge || step_ == Step::Drain || step_ == Step::Tick) {
		if (confirmed || clicked) SkipToChoose();
		return;
	}
	if (step_ != Step::Choose) return;

	// カーソルを乗せたら選ぶ（動かしたフレームだけ。置きっぱなしのマウスがキー・パッドの操作を奪わないように）。
	// 左クリックは乗せている選択肢で決定、選択肢の外は何もしない
	if (pointer.valid && (pointer.moved || pointer.clicked)) {
		const int hit = HitOption(pointer.position);
		if (hit >= 0 && hit != selectedIndex_) {
			selectedIndex_ = hit;
			EmitCursorRing();
		}
		if (hit >= 0 && pointer.clicked && optionsAlpha_ > 0.5f) {
			Confirm(options_[hit]);
			return;
		}
	}

	// 選択肢は横に並べる（対等に並べる＝§3）
	const int count = static_cast<int>(options_.size());
	const bool next = actions->IsTriggered(static_cast<int>(Action::MenuDown))
		|| actions->IsTriggered(static_cast<int>(Action::MenuRight));
	const bool prev = actions->IsTriggered(static_cast<int>(Action::MenuUp))
		|| actions->IsTriggered(static_cast<int>(Action::MenuLeft));
	if (next) {
		selectedIndex_ = (selectedIndex_ + 1) % count;
		EmitCursorRing();
	}
	if (prev) {
		selectedIndex_ = (selectedIndex_ + count - 1) % count;
		EmitCursorRing();
	}
	if (confirmed && optionsAlpha_ > 0.5f) Confirm(options_[selectedIndex_]);
}

void GameOverScene::Update() {
	const float dt = GetScaledDeltaTime();
	UpdateFlow(dt);
	UpdateChoose();

	UpdateCamera(dt);
	// デバッグカメラ ON ならここでカメラが差し替わる（呼ばないと上の設定が毎フレーム勝って動かせない）
	UpdateDebugCameraIfActive();

	water_->GetParams().waterColor = { waterColor_.x, waterColor_.y, waterColor_.z, 1.0f };
	water_->Update(dt);
	vessel_->Update(dt);
	UpdateGlobalEffects(camera_.get(), dt);
	backgroundSprite_->SetColor({ background_[0], background_[1], background_[2], 1.0f });
	backgroundSprite_->Update();

#ifdef _DEBUG
	OnImGui();
#endif
}

const char* GameOverScene::OptionLabel(Option option) const {
	// 「あきらめる」のような負けを感じさせる言葉は使わない（§3）。「引き上げる」＝賭け分を手元に引き上げる
	switch (option) {
	case Option::BossRetry: return "ボス戦から";
	case Option::Restart:   return hasBossCheckpoint_ ? "はじめから" : "もう一度";
	case Option::Withdraw:  return "引き上げる";
	}
	return "";
}

void GameOverScene::DrawOptions() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || options_.empty() || optionsAlpha_ <= 0.0f) return;

	const float a = SmoothStep01(optionsAlpha_);
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	std::vector<OptionRect> rects;
	LayoutOptions(rects);
	const float y = rects.front().y;
	const float textHeight = rects.front().height;
	const float centerY = y + textHeight * 0.5f;

	// 後ろに暗い帯（器の金の上に文字が重なって読めなかった。2026-10-24 ユーザー）。左右の端と上下はぼかす
	if (bandAlpha_ > 0.0f) {
		constexpr int kColumns = 32;
		constexpr int kRows = 3;   // 真ん中の濃い帯＋上下の薄い縁
		const float columnW = screenW / static_cast<float>(kColumns);
		for (int c = 0; c < kColumns; ++c) {
			const float u = (static_cast<float>(c) + 0.5f) / static_cast<float>(kColumns);
			const float edge = SmoothStep01((std::min)(u, 1.0f - u) / (std::max)(bandEdgeFade_, 1e-3f));
			for (int r = 0; r < kRows; ++r) {
				const float h = bandHeight_ * (1.0f + static_cast<float>(r) * 0.25f);
				const float k = r == 0 ? 1.0f : 0.35f;
				tr->DrawRect({ columnW * static_cast<float>(c), centerY - h * 0.5f }, { columnW + 1.0f, h },
					{ bandColor_[0], bandColor_[1], bandColor_[2], bandAlpha_ * edge * k * a });
			}
		}
	}

	for (size_t i = 0; i < options_.size(); ++i) {
		const bool selected = static_cast<int>(i) == selectedIndex_;
		const char* label = OptionLabel(options_[i]);
		const float x = rects[i].x;
		const float width = rects[i].width;
		if (selected) {
			// 白に金の縁、下に細い金の線
			tr->DrawText(label, { x, y }, optionsScale_, { 1.0f, 1.0f, 1.0f, a }, 2.0f,
				{ outlineGold_[0], outlineGold_[1], outlineGold_[2], a });
			tr->DrawRect({ x, y + textHeight + underlineOffset_ }, { width, underlineThickness_ },
				{ outlineGold_[0], outlineGold_[1], outlineGold_[2], a });
			if (glowIntensity_ > 0.0f) {
				const float g = glowIntensity_ * a;
				tr->DrawRectGlow({ x, y + textHeight + underlineOffset_ }, { width, underlineThickness_ },
					{ outlineGold_[0] * g, outlineGold_[1] * g, outlineGold_[2] * g, a });
			}
		} else {
			// 選んでいない物は落ち着いた灰色に暗い縁（対等に並べたまま、選んだ物だけが立つ）
			tr->DrawText(label, { x, y }, optionsScale_, { 0.62f, 0.64f, 0.68f, a }, 1.5f, { 0.0f, 0.0f, 0.0f, a * 0.8f });
		}
	}
	tr->Flush();
}

void GameOverScene::LayoutOptions(std::vector<OptionRect>& rects) const {
	// 描画とマウスの当たりで同じ並びを使う（文字の左上と幅・高さ。UI 座標）
	rects.clear();
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || options_.empty()) return;
	const float a = SmoothStep01(optionsAlpha_);
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const float screenH = static_cast<float>(dxCore_->GetUIHeight());
	const float y = screenH * optionsYRatio_ + (1.0f - a) * optionsRisePixels_;
	const float height = kFontPixelHeight * optionsScale_;
	float total = 0.0f;
	for (Option option : options_) {
		rects.push_back({ 0.0f, y, tr->MeasureWidth(OptionLabel(option), optionsScale_), height });
		total += rects.back().width;
	}
	total += optionsGap_ * static_cast<float>(options_.size() - 1);
	float x = (screenW - total) * 0.5f;
	for (OptionRect& r : rects) {
		r.x = x;
		x += r.width + optionsGap_;
	}
}

int GameOverScene::HitOption(const Vector2& position) const {
	std::vector<OptionRect> rects;
	LayoutOptions(rects);
	for (size_t i = 0; i < rects.size(); ++i) {
		const OptionRect& r = rects[i];
		if (position.x >= r.x - hitPadding_ && position.x <= r.x + r.width + hitPadding_
			&& position.y >= r.y - hitPadding_ && position.y <= r.y + r.height + hitPadding_) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void GameOverScene::Draw() {
	auto* commandList = dxCore_->GetCommandList();
	vessel_->DispatchSimulation();
	water_->DispatchSimulation();

	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(dxCore_->GetRenderWidth()),
		static_cast<float>(dxCore_->GetRenderHeight()), 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(dxCore_->GetRenderWidth()), static_cast<LONG>(dxCore_->GetRenderHeight()) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto dsvHandle = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
	// 背景の板（スプライトは深度を書くので、塗った後に深度を作成時と同じ 1.0 で消す）
	spriteManager_->DrawSetting();
	backgroundSprite_->Draw();
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	vessel_->DrawOpaque(dxCore_);
	// 足元の暗い水面（不透明物の後。ルートシグネチャを替える）
	water_->Draw(*camera_, iblPath_, nullptr);
	// ガラスの球・支柱の光・糸は深度を見るだけ。粒はその後
	vessel_->DrawTransparent(camera_.get(), iblPath_);
	DrawGlobalEffects();

	DrawOptions();
}

Camera* GameOverScene::GetCamera() {
	return camera_.get();
}

bool GameOverScene::HasBloomSources() const {
	// トランジション（着水の羽根・波紋、羽根の飛翔）の上に発光が足されて隠してしまうので、その間は光らせない（HubScene と同じ）
	return vessel_ != nullptr && !SceneManager::GetInstance()->IsTransitioning();
}

void GameOverScene::DrawBloomSources() {
	if (vessel_) vessel_->DrawBloom(camera_.get(), iblPath_);
}

bool GameOverScene::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	ReadColor3(root["background"], background_);

	const JsonValue& cam = root["camera"];
	ReadVector3(cam["nearPos"], cameraNearPos_);
	ReadVector3(cam["nearRotDeg"], cameraNearRotDeg_);
	ReadVector3(cam["farPos"], cameraFarPos_);
	ReadVector3(cam["farRotDeg"], cameraFarRotDeg_);
	ReadFloat(cam, "pullDelay", pullDelay_);
	ReadFloat(cam, "pullSeconds", pullSeconds_);

	const JsonValue& flow = root["flow"];
	ReadFloat(flow, "drainDelay", drainDelay_);
	ReadFloat(flow, "drainSeconds", drainSeconds_);
	ReadFloat(flow, "tickSettleSeconds", tickSettleSeconds_);
	ReadFloat(flow, "optionsFadeSeconds", optionsFadeSeconds_);
	ReadFloat(flow, "optionsFadeOutSeconds", optionsFadeOutSeconds_);
	ReadFloat(flow, "riseSeconds", riseSeconds_);
	ReadFloat(flow, "riseGlowBoost", riseGlowBoost_);
	ReadFloat(flow, "particleStopRatio", particleStopRatio_);
	ReadFloat(flow, "returnSeconds", returnSeconds_);
	ReadFloat(flow, "returnTailSeconds", returnTailSeconds_);

	const JsonValue& water = root["water"];
	ReadFloat(water, "height", waterHeight_);
	ReadFloat(water, "depth", waterDepth_);
	ReadVector3(water["color"], waterColor_);
	ReadFloat(water, "skyIntensity", waterSkyIntensity_);
	ReadFloat(water, "ringAmplitude", waterRingAmplitude_);
	ReadFloat(water, "ringInterval", waterRingInterval_);
	ReadFloat(water, "cursorRingAmplitude", cursorRingAmplitude_);
	ReadFloat(water, "cursorRingWavelength", cursorRingWavelength_);
	ReadFloat(water, "cursorRingRadius", cursorRingRadius_);
	ReadFloat(water, "arrivalRingAmplitude", arrivalRingAmplitude_);

	const JsonValue& text = root["text"];
	ReadFloat(text, "yRatio", optionsYRatio_);
	ReadFloat(text, "gap", optionsGap_);
	ReadFloat(text, "scale", optionsScale_);
	ReadFloat(text, "risePixels", optionsRisePixels_);
	ReadColor3(text["outlineGold"], outlineGold_);
	ReadFloat(text, "glowIntensity", glowIntensity_);
	ReadFloat(text, "underlineOffset", underlineOffset_);
	ReadFloat(text, "underlineThickness", underlineThickness_);
	ReadFloat(text, "hitPadding", hitPadding_);
	ReadColor3(text["bandColor"], bandColor_);
	ReadFloat(text, "bandAlpha", bandAlpha_);
	ReadFloat(text, "bandHeight", bandHeight_);
	ReadFloat(text, "bandEdgeFade", bandEdgeFade_);

	vessel_->LoadTuning(root["vessel"]);
	ApplyWaterTuning();
	return true;
}

void GameOverScene::ApplyWaterTuning() {
	WaterSurface::Params& p = water_->GetParams();
	p.depth = waterDepth_;
	p.skyIntensity = waterSkyIntensity_;
	p.ringAmplitude = waterRingAmplitude_;
	p.ringInterval = waterRingInterval_;
	p.waterColor = { waterColor_.x, waterColor_.y, waterColor_.z, 1.0f };
	water_->SetWaterHeight(kVesselOrigin.y + waterHeight_);
}

void GameOverScene::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("19_GameOverAndDifficulty.md §4.6 G6。ガラスとエーテルの見た目は EtherVesselGlass.json（G5）、エーテルの状態は EtherState.json"));
	root["background"] = MakeColor3(background_);

	JsonValue& cam = root["camera"];
	cam = JsonValue::MakeObject();
	cam["nearPos"] = MakeVector3(cameraNearPos_);
	cam["nearRotDeg"] = MakeVector3(cameraNearRotDeg_);
	cam["farPos"] = MakeVector3(cameraFarPos_);
	cam["farRotDeg"] = MakeVector3(cameraFarRotDeg_);
	WriteFloat(cam, "pullDelay", pullDelay_);
	WriteFloat(cam, "pullSeconds", pullSeconds_);

	JsonValue& flow = root["flow"];
	flow = JsonValue::MakeObject();
	WriteFloat(flow, "drainDelay", drainDelay_);
	WriteFloat(flow, "drainSeconds", drainSeconds_);
	WriteFloat(flow, "tickSettleSeconds", tickSettleSeconds_);
	WriteFloat(flow, "optionsFadeSeconds", optionsFadeSeconds_);
	WriteFloat(flow, "optionsFadeOutSeconds", optionsFadeOutSeconds_);
	WriteFloat(flow, "riseSeconds", riseSeconds_);
	WriteFloat(flow, "riseGlowBoost", riseGlowBoost_);
	WriteFloat(flow, "particleStopRatio", particleStopRatio_);
	WriteFloat(flow, "returnSeconds", returnSeconds_);
	WriteFloat(flow, "returnTailSeconds", returnTailSeconds_);

	JsonValue& water = root["water"];
	water = JsonValue::MakeObject();
	WriteFloat(water, "height", waterHeight_);
	WriteFloat(water, "depth", waterDepth_);
	water["color"] = MakeVector3(waterColor_);
	WriteFloat(water, "skyIntensity", waterSkyIntensity_);
	WriteFloat(water, "ringAmplitude", waterRingAmplitude_);
	WriteFloat(water, "ringInterval", waterRingInterval_);
	WriteFloat(water, "cursorRingAmplitude", cursorRingAmplitude_);
	WriteFloat(water, "cursorRingWavelength", cursorRingWavelength_);
	WriteFloat(water, "cursorRingRadius", cursorRingRadius_);
	WriteFloat(water, "arrivalRingAmplitude", arrivalRingAmplitude_);

	JsonValue& text = root["text"];
	text = JsonValue::MakeObject();
	WriteFloat(text, "yRatio", optionsYRatio_);
	WriteFloat(text, "gap", optionsGap_);
	WriteFloat(text, "scale", optionsScale_);
	WriteFloat(text, "risePixels", optionsRisePixels_);
	text["outlineGold"] = MakeColor3(outlineGold_);
	WriteFloat(text, "glowIntensity", glowIntensity_);
	WriteFloat(text, "underlineOffset", underlineOffset_);
	WriteFloat(text, "underlineThickness", underlineThickness_);
	WriteFloat(text, "hitPadding", hitPadding_);
	text["bandColor"] = MakeColor3(bandColor_);
	WriteFloat(text, "bandAlpha", bandAlpha_);
	WriteFloat(text, "bandHeight", bandHeight_);
	WriteFloat(text, "bandEdgeFade", bandEdgeFade_);

	vessel_->SaveTuning(root["vessel"]);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void GameOverScene::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Begin("Game Over (19 G6)")) {
		static const char* kStepNames[] = { "Emerge", "Drain", "Tick", "Choose", "Rise", "Return", "Leaving" };
		ImGui::Text("Step %s  t %.2f", kStepNames[static_cast<int>(step_)], stepTime_);
		ImGui::Text("Fill %.3f -> %.3f  Difficulty01 %.3f -> %.3f", fillBefore_, fillAfter_, difficultyBefore01_, difficultyAfter01_);
		// 死なずに流れをもう一度見る（エーテルの状態は変えない）
		if (ImGui::Button("Replay Flow")) StartFlow();
		ImGui::SameLine();
		if (ImGui::Button("Save##GameOver")) SaveTuning();
		ImGui::SameLine();
		if (ImGui::Button("Reload##GameOver")) LoadTuning();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", kTuningPath);

		if (ImGui::CollapsingHeader("Ether State")) EtherState::GetInstance()->OnImGui();
		if (ImGui::CollapsingHeader("Flow")) {
			ImGui::DragFloat("Drain Delay after Pull [s]", &drainDelay_, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Drain Seconds", &drainSeconds_, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Tick Settle [s]", &tickSettleSeconds_, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Options Fade In [s]", &optionsFadeSeconds_, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Options Fade Out [s]", &optionsFadeOutSeconds_, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Rise Seconds", &riseSeconds_, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Rise Glow Boost", &riseGlowBoost_, 0.05f, 1.0f, 20.0f);
			ImGui::SliderFloat("Particle Stop (drain ratio)", &particleStopRatio_, 0.0f, 1.0f);
			ImGui::DragFloat("Return Seconds", &returnSeconds_, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Return Tail [s]", &returnTailSeconds_, 0.01f, 0.0f, 5.0f);
		}
		if (ImGui::CollapsingHeader("Camera (near -> far)")) {
			ImGui::DragFloat3("Near Pos", &cameraNearPos_.x, 0.01f);
			ImGui::DragFloat3("Near Rot [deg]", &cameraNearRotDeg_.x, 0.1f);
			ImGui::DragFloat3("Far Pos", &cameraFarPos_.x, 0.01f);
			ImGui::DragFloat3("Far Rot [deg]", &cameraFarRotDeg_.x, 0.1f);
			ImGui::DragFloat("Pull Delay [s]", &pullDelay_, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Pull Seconds", &pullSeconds_, 0.01f, 0.01f, 10.0f);
			if (ImGui::Button("Replay Pull")) pullTimer_ = 0.0f;
			ImGui::SameLine();
			if (ImGui::Button("Near <- Debug Cam") && GetUseDebugCamera()) {
				// デバッグカメラで探した位置をそのまま写す（シーンのカメラはデバッグカメラに差し替わっている）
				const Vector3& t = camera_->GetTranslate();
				cameraNearPos_ = { t.x - kVesselOrigin.x, t.y - kVesselOrigin.y, t.z - kVesselOrigin.z };
				const Vector3& r = camera_->GetRotate();
				cameraNearRotDeg_ = { r.x / kDegToRad, r.y / kDegToRad, r.z / kDegToRad };
			}
			ImGui::SameLine();
			if (ImGui::Button("Far <- Debug Cam") && GetUseDebugCamera()) {
				const Vector3& t = camera_->GetTranslate();
				cameraFarPos_ = { t.x - kVesselOrigin.x, t.y - kVesselOrigin.y, t.z - kVesselOrigin.z };
				const Vector3& r = camera_->GetRotate();
				cameraFarRotDeg_ = { r.x / kDegToRad, r.y / kDegToRad, r.z / kDegToRad };
			}
		}
		if (ImGui::CollapsingHeader("Water")) {
			WaterSurface::Params& p = water_->GetParams();
			if (ImGui::DragFloat("Height [m]", &waterHeight_, 0.005f, -1.0f, 2.0f)) water_->SetWaterHeight(kVesselOrigin.y + waterHeight_);
			if (ImGui::DragFloat("Depth [m]", &waterDepth_, 0.01f, 0.0f, 5.0f)) p.depth = waterDepth_;
			ImGui::ColorEdit3("Water Color", &waterColor_.x, ImGuiColorEditFlags_Float);
			if (ImGui::DragFloat("Sky Intensity", &waterSkyIntensity_, 0.01f, 0.0f, 3.0f)) p.skyIntensity = waterSkyIntensity_;
			if (ImGui::DragFloat("Ambient Ring Amplitude", &waterRingAmplitude_, 0.0005f, 0.0f, 0.1f, "%.4f")) p.ringAmplitude = waterRingAmplitude_;
			if (ImGui::DragFloat("Ambient Ring Interval", &waterRingInterval_, 0.05f, 0.1f, 30.0f)) p.ringInterval = waterRingInterval_;
			ImGui::DragFloat("Cursor Ring Amplitude", &cursorRingAmplitude_, 0.0005f, 0.0f, 0.2f, "%.4f");
			ImGui::DragFloat("Cursor Ring Wavelength", &cursorRingWavelength_, 0.005f, 0.01f, 3.0f);
			ImGui::DragFloat("Cursor Ring Radius", &cursorRingRadius_, 0.05f, 0.1f, 50.0f);
			ImGui::DragFloat("Arrival Ring Amplitude", &arrivalRingAmplitude_, 0.0005f, 0.0f, 0.2f, "%.4f");
			if (ImGui::Button("Cursor Ring")) EmitCursorRing();
			ImGui::ColorEdit3("Background", background_);
			if (ImGui::TreeNode("WaterSurface (raw)")) {
				water_->OnImGui();
				ImGui::TreePop();
			}
		}
		if (ImGui::CollapsingHeader("Options Text")) {
			ImGui::SliderFloat("Y (ratio)", &optionsYRatio_, 0.0f, 1.0f);
			ImGui::DragFloat("Gap [px]", &optionsGap_, 1.0f, 0.0f, 600.0f);
			ImGui::DragFloat("Scale", &optionsScale_, 0.01f, 0.2f, 5.0f);
			ImGui::DragFloat("Rise [px]", &optionsRisePixels_, 0.5f, 0.0f, 100.0f);
			ImGui::ColorEdit3("Outline Gold", outlineGold_);
			ImGui::DragFloat("Underline Glow", &glowIntensity_, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Underline Offset [px]", &underlineOffset_, 0.5f, -50.0f, 50.0f);
			ImGui::DragFloat("Underline Thickness [px]", &underlineThickness_, 0.1f, 0.0f, 20.0f);
			ImGui::DragFloat("Mouse Hit Padding [px]", &hitPadding_, 0.5f, 0.0f, 200.0f);
			ImGui::ColorEdit3("Band Color", bandColor_);
			ImGui::SliderFloat("Band Alpha", &bandAlpha_, 0.0f, 1.0f);
			ImGui::DragFloat("Band Height [px]", &bandHeight_, 1.0f, 0.0f, 600.0f);
			ImGui::SliderFloat("Band Edge Fade", &bandEdgeFade_, 0.01f, 0.5f);
			if (ImGui::Button("Show Options")) SkipToChoose();
		}
		if (ImGui::CollapsingHeader("Vessel")) {
			if (ImGui::Button("Save Glass (EtherVesselGlass.json)")) vessel_->SaveGlassTuning();
			vessel_->OnImGui();
		}
		ImGui::Text("IBL: %s", iblPath_.c_str());
	}
	ImGui::End();
#endif
}
