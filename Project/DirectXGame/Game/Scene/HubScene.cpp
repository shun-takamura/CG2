#include "HubScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "Skybox.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "Hub/HubMechModel.h"
#include "Hub/HubTemple.h"
#include "Hub/HatchSign.h"
#include "Cockpit/CockpitLighting.h"
#include "Hub/CockpitConsole.h"
#include "Hub/HubLiftDescent.h"
#include "StageStart/StageStartSkip.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "PostEffect.h"
#include "RadialBlurEffect.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "Vector4.h"
#include "AttractMode.h"
#include "UIPointer.h"
#include "FadeTransition.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum HubItem : int {
		kHubSortie = 0,   // 搭乗（必殺技の切り替えは後で足す。17 番 §0）
		kHubToTitle,      // タイトルに戻る（白フェード。扉の方を向く演出は後で）
	};

	constexpr const char* kTuningPath = "Resources/Json/Tuning/Hub.json";
	// メニューは画面の下の方（機体の胸を隠さない）
	constexpr float kMenuHeightRatio = 0.78f;

	// 丸窓から見える空はタイトルの空（映り込みの IBL は神殿のもの。HubTemple）
	constexpr const char* kSkyboxPath = "Resources/Cubemaps/title_clear_sky.dds";
	constexpr float kCameraFarClip = 500.0f;

	// 機体の正面（+Z）を前上から照らす（光の進む向き）
	const Vector3 kSunLightDirection = { -0.191f, -0.815f, -0.547f };
	const Vector4 kSunLightColor = { 1.0f, 0.96f, 0.9f, 1.0f };
}

HubScene::HubScene(Entry entry) : entry_(entry) {}
HubScene::~HubScene() = default;

void HubScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetFarClip(kCameraFarClip);
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, kSkyboxPath);
	mech_ = std::make_unique<HubMechModel>();
	mech_->Initialize(object3DManager_, skinningComputeManager_, dxCore_, srvManager_);

	// 神殿（回廊・足場・輪・扉・鏡の床）。機体も床に映す
	temple_ = std::make_unique<HubTemple>();
	temple_->Initialize(object3DManager_, dxCore_, srvManager_);
	temple_->AddReflectionTarget(mech_->GetBody());
	temple_->AddReflectionTarget(mech_->GetCockpit());
	temple_->AddReflectionTarget(mech_->GetHatch());
	// ③ のリフト（16_StageStart.md F2）。固定台は降りる前から機体の両脚の下に見えている（16 番 §2.1）
	stageGeometry_.Load();
	liftDescent_ = std::make_unique<HubLiftDescent>();
	liftDescent_->Initialize(object3DManager_, dxCore_, stageGeometry_);
	temple_->AddReflectionTarget(liftDescent_->GetClamp(0));
	temple_->AddReflectionTarget(liftDescent_->GetClamp(1));
	// PBR の IBL（神殿を機体の胸の位置から撮ったもの）。無いと金属は真っ黒になる
	object3DManager_->SetEnvironmentTexture(temple_->GetIblPath());
	// 距離フォグはステージ（StageEnvironment）が点けたまま残るので、神殿では切る
	object3DManager_->SetFogParams(Object3DManager::FogParams{});

	hatchSign_ = std::make_unique<HatchSign>();
	hatchSign_->Initialize(object3DManager_, dxCore_, srvManager_);
	cockpitLighting_ = std::make_unique<CockpitLighting>();
	cockpitLighting_->Initialize();
	console_ = std::make_unique<CockpitConsole>();
	console_->Initialize();

	LoadTuning();

	menu_.SetItems({ "搭乗", "タイトルに戻る" });

	boarding_.Stop();
	disembark_.Stop();
	entrySequence_.Stop();
	liftDescent_->Reset();
	reveal_.Reset();
	state_ = State::Waiting;
	leaving_ = false;
	if (entry_ == Entry::Temple) {
		// タイトルから：白フェードが晴れる間に時計の門の扉の前から歩いて入る（18 番 ⑧）
		entrySequence_.Start(boarding_.GetHomeKey());
		state_ = State::Entering;
	} else if (entry_ == Entry::Cockpit) {
		// ポーズの「ステージセレクトに戻る」（⑩）：座った所から起動を演じる
		EnterCockpit();
	} else if (entry_ == Entry::Disembark) {
		// ⑦：墨（黒フェード）の黒から真っ暗なコクピットへつなぐ。覆われている間は Update を止めるので、
		// 墨が消えた最初のフレームはここで作った真っ暗な見え方になる
		EnterArrive();
	}
	ApplyPose();
	camera_->Update();
	skybox_->Update(0.0f);
	hatchSign_->Update(0.0f, mech_->GetPosition());

	SoundManager::GetInstance()->Play2DSoundLooped("bgm_title", 0.5f);
	attractMenu_.Reset();
	cockpitFromBoarding_ = false;
}

void HubScene::Finalize() {
	SoundManager::GetInstance()->Stop2DSound("bgm_title");
	if (auto* blur = Game::GetPostEffect()->radialBlur) blur->SetEnabled(false);
	if (cockpitLighting_) cockpitLighting_->Finalize(mech_ ? &mech_->GetCockpitModel() : nullptr);
	if (hatchSign_) hatchSign_->Finalize();
}

Vector2 HubScene::GetScreenSize() const {
	return { static_cast<float>(dxCore_->GetUIWidth()), static_cast<float>(dxCore_->GetUIHeight()) };
}

bool HubScene::IsGlowAllowed() const {
	return !SceneManager::GetInstance()->IsTransitioning();
}

void HubScene::EnterCockpit() {
	state_ = State::Cockpit;
	boarding_.Stop();   // 視点は乗り込みの最後の姿勢（ApplyPose）
	reveal_.StartBoot();
	cockpitTime_ = 0.0f;
	attractMenu_.Reset();
}

void HubScene::EnterArrive() {
	// 視点・ハッチ・ステージ選択の UI の扱いはコクピットと同じ（入力は起動中しか受け付けないので読まれない）。
	// 帰着が終わると Off になり、UpdateCockpit がシャットダウンの後と同じく降りる
	state_ = State::Cockpit;
	boarding_.Stop();
	reveal_.StartArrive();
	cockpitTime_ = 0.0f;
}

void HubScene::StartDescent() {
	// ③ の OUT：コクピットの中の視点のまま、機体ごとリフトで縦穴を降りる（16_StageStart.md §0・F2）。
	// 外が透けた全天周のモニターに縦穴が映っている扱い。ステージ選択の UI はたたんで消す
	state_ = State::Descending;
	liftDescent_->Start();
	// 降りている間に StagePlay のアセットを裏で読んでおく（16 番 F7）。差し替えは読み終えてから
	SceneManager::GetInstance()->PreloadScene("STAGEPLAY");
	if (!skip_) skip_ = std::make_unique<StageStartSkip>();
	skip_->Reset();
	attractMenu_.Reset();
}

void HubScene::Update() {
	// 覆われている間だけ止める。開いている間は演出を進める（入力は各状態で止める）
	if (SceneManager::GetInstance()->IsTransitionCovering()) {
		return;
	}

	const float dt = GetScaledDeltaTime();

	switch (state_) {
	case State::Entering:
		UpdateEntering(dt);
		break;
	case State::Waiting:
		UpdateWaiting();
		break;
	case State::Boarding: {
		// 乗り込み中は、何か押したら最後（ハッチが閉じ切ってコクピットの起動）まで飛ばす（2026-10-24 ユーザー「長いからスキップできるように」）。
		// コクピットは暗い所から起動するので、外の景色から切り替わっても不自然になりにくい。デモモード中は飛ばさない
		if (IsSkipPressed() && !boarding_.IsPreviewing()) {
			EnterCockpit();
			cockpitFromBoarding_ = true;
			break;
		}
		boarding_.Update(dt);
		if (boarding_.ConsumeSitImpact()) {
			camera_->Shake(boarding_.GetSitShakeIntensity(), boarding_.GetSitShakeSeconds());
		}
		if (boarding_.ConsumeCloseImpact()) {
			camera_->Shake(boarding_.GetCloseShakeIntensity(), boarding_.GetCloseShakeSeconds());
		}
		if (boarding_.IsFinished()) {
			// 閉じ切ったらそのままコクピットの起動へ（シーンは切り替えない）
			EnterCockpit();
			cockpitFromBoarding_ = true;
		}
		break;
	}
	case State::Cockpit:
		UpdateCockpit(dt);
		break;
	case State::Disembarking:
		UpdateDisembarking(dt);
		break;
	case State::Descending:
		UpdateDescending(dt);
		break;
	}
	// 降下していない間も ImGui のシークを見え方に反映する
	if (state_ != State::Descending) liftDescent_->Update(0.0f);
	// リフトが床の高さにある間は穴を開けない（機体の映り込みが消えるため。16 番 §2.1）
	temple_->SetFloorHole(liftDescent_->IsHoleOpen(), stageGeometry_.octagonHalfWidth, stageGeometry_.octagonChamfer);

	ApplyPose();
	camera_->UpdateShake(dt);
	camera_->Update();
	skybox_->Update(dt);
	temple_->Update(dt);
	hatchSign_->Update(dt, mech_->GetPosition());
}

void HubScene::UpdateWaiting() {
	// 白フェードで開いている途中などは入力を読まない
	if (SceneManager::GetInstance()->IsTransitioning()) return;

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	const Vector2 screen = GetScreenSize();
	menu_.SetPosition({ screen.x * 0.5f, screen.y * kMenuHeightRatio });
	const UIPointer pointer = UIPointer::FromInput(input_);
	VerticalMenu::Result result = menu_.Update(actions, &pointer);

	// デモモード：神殿を見せてから「搭乗」を選ぶ（一周の始まり。タイトルへは戻らない）
	if (AttractMode* attract = AttractMode::GetInstance(); attract->IsEnabled()) {
		if (attractMenu_.Advance(dxCore_->GetDeltaTime(), attract->hubWait, attract->menuConfirmDelay)
			== AttractMenuTimer::Step::Confirm && result == VerticalMenu::Result::None) {
			menu_.SetSelectedIndex(kHubSortie);
			SoundManager::GetInstance()->Play2DSound("se_ui_decide");
			result = VerticalMenu::Result::Confirmed;
		}
	} else {
		attractMenu_.Reset();
	}

	if (result == VerticalMenu::Result::Canceled) {
		ChangeToTitle();
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		attractMenu_.Reset();
		switch (menu_.GetSelectedIndex()) {
		case kHubSortie:
			// 乗り込み（18 番 ①の OUT）→ 閉じ切ったらコクピット（①の IN）
			boarding_.Start();
			state_ = State::Boarding;
			return;
		case kHubToTitle:
			ChangeToTitle();
			return;
		}
	}
}

void HubScene::UpdateEntering(float dt) {
	// 入場中は入力を読まない（デモモードの待ち時間も入場が終わってから数える）
	entrySequence_.Update(dt);
	if (entrySequence_.IsFinished()) {
		entrySequence_.Stop();
		state_ = State::Waiting;
		attractMenu_.Reset();
	}
}

void HubScene::UpdateCockpit(float dt) {
	cockpitTime_ += dt;
	reveal_.Update(dt);
	if (reveal_.IsShutdownFinished()) {
		// ②の IN：降りる（機体に背を向けて乗るときと同じ道を戻る）
		state_ = State::Disembarking;
		disembark_.Start(boarding_.GetHomeKey(), boarding_.GetMinLightScale());
		return;
	}
	// ②（ステージ選択 → HUB）のシャットダウン中は、何か押したら降り終わった所まで飛ばす（2026-10-24 ユーザー）
	if (reveal_.GetPhase() == CockpitReveal::Phase::ShuttingDown && IsSkipPressed()) {
		FinishDisembark();
		return;
	}
	if (leaving_ || SceneManager::GetInstance()->IsTransitioning()) return;

	auto* actions = input_->GetActionMap();
	if (!actions) return;
	const CockpitReveal::State st = reveal_.Evaluate();
	const bool inputEnabled = st.inputEnabled && reveal_.GetPhase() == CockpitReveal::Phase::Booting;
	const UIPointer pointer = UIPointer::FromInput(input_);
	CockpitConsole::Result result = console_->Update(actions, &pointer, GetScreenSize(), inputEnabled);

	// デモモード：乗り込んで来ても、ポーズから戻って来ても「ステージ1」を選ぶ（ステージ選択から下を繰り返す）
	if (AttractMode* attract = AttractMode::GetInstance(); attract->IsEnabled() && inputEnabled) {
		const CockpitConsole::Result pick = CockpitConsole::Result::Stage1;
		switch (attractMenu_.Advance(dxCore_->GetDeltaTime(), attract->menuWait, attract->menuConfirmDelay)) {
		case AttractMenuTimer::Step::MoveCursor:
			console_->SelectItem(pick, true);
			break;
		case AttractMenuTimer::Step::Confirm:
			if (result == CockpitConsole::Result::None) {
				console_->SelectItem(pick, false);
				SoundManager::GetInstance()->Play2DSound("se_ui_decide");
				result = pick;
			}
			break;
		default:
			break;
		}
	}
	if (result != CockpitConsole::Result::None) attractMenu_.Reset();

	switch (result) {
	case CockpitConsole::Result::Stage1:
		// ③ リフトで縦穴を降りてから StagePlay へ（UpdateDescending）
		StartDescent();
		break;
	case CockpitConsole::Result::BackToHub:
		reveal_.StartShutdown();   // ②の OUT
		break;
	default:
		break;
	}
}

void HubScene::UpdateDisembarking(float dt) {
	// 降りている間は、何か押したら降り終わった所まで飛ばす（2026-10-24 ユーザー）。ほかの入力は読まない
	if (IsSkipPressed() && !disembark_.IsPreviewing()) {
		FinishDisembark();
		return;
	}
	disembark_.Update(dt);
	if (disembark_.IsFinished()) FinishDisembark();
}

void HubScene::FinishDisembark() {
	// 振り返り終えた姿勢は待機の視点（乗り込みのキー 0）と同じ
	disembark_.Stop();
	boarding_.Stop();
	reveal_.Reset();
	state_ = State::Waiting;
	attractMenu_.Reset();   // デモモードはここで一周。また神殿を見せてから「搭乗」
}

bool HubScene::IsSkipPressed() const {
	// 乗り込み・HUB へ戻る所のスキップ：何か押したら（キー・マウスのボタン・パッドのボタン）。デモモード中・白フェード中は飛ばさない
	if (AttractMode::GetInstance()->IsEnabled() || SceneManager::GetInstance()->IsTransitioning()) return false;
	const InputActionMap* actions = input_ ? input_->GetActionMap() : nullptr;
	return actions && actions->AnyInputTriggered();
}

void HubScene::UpdateDescending(float dt) {
	// 降りている間はスキップ（LSHIFT 長押し）だけ読む
	liftDescent_->Update(dt);
	if (liftDescent_->ConsumeReleaseImpact()) {
		camera_->Shake(liftDescent_->GetReleaseShakeIntensity(), liftDescent_->GetReleaseShakeSeconds());
	}
	if (!leaving_ && skip_ && !AttractMode::GetInstance()->IsEnabled()) {
		auto* actions = input_ ? input_->GetActionMap() : nullptr;
		if (skip_->Update(dt, actions && actions->IsPressed(static_cast<int>(Action::SkipIntro)))) {
			// 雲の上から始めるよう印を付けて、白フェードで StagePlay へ（2026-10-19 ユーザー）
			StageStartHandoff handoff = liftDescent_->MakeHandoff();
			handoff.skipToCloudTop = true;
			StageStartHandoff::Store(handoff);
			leaving_ = true;
			if (auto* fade = TransitionManager::GetInstance()->GetTransition<FadeTransition>(TransitionType::Fade)) {
				fade->SetNextFade({ 1.0f, 1.0f, 1.0f }, whiteFadeSeconds_, whiteHoldSeconds_);
			}
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade, TransitionType::Fade);
			return;
		}
	}
	if (leaving_ || !descentChangesScene_ || !liftDescent_->ShouldChangeScene()) return;
	// 裏の先読みが終わるまで降り続ける（縦穴は何周でも降りられる。位相は差し替えるフレームの値を渡す）。
	// 待ちすぎたら諦めて差し替える（残りはシーンを読む前に同期で済ませる）
	if (SceneManager::GetInstance()->IsPreloading() && !liftDescent_->IsPreloadWaitOver()) return;

	// ③ の切れ目：縦穴の位相・速さ・灯りを StagePlay へ渡して差し替える（16 番 §4.2。アセットは降下中に先読み済み）。
	// 組み合わせ表は覆う・開くともシーン（None / None）なので暗転せずに差し替わる。StagePlay は StageStartPart が続きを降りる（F3）
	StageStartHandoff handoff = liftDescent_->MakeHandoff();
	const CockpitReveal::State st = reveal_.Evaluate();
	handoff.cockpitLightLevel = st.lightLevel;
	handoff.revealAmount = st.reveal;
	handoff.sunScale = liftDescent_->GetSunScale();
	// 視点（コクピットの中の視点＝乗り込みの最後の姿勢）と光は今のフレームの値をそのまま渡す
	const HubBoardingSequence::Pose pose = boarding_.Evaluate(boarding_.GetEndTime());
	handoff.eyeOffset = pose.eye;
	handoff.eyeYaw = pose.yaw;
	handoff.eyePitch = pose.pitch;
	handoff.fovY = DegToRad(fovYDegrees_);
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		const auto* light = lm->GetDirectionalLightData();
		handoff.sunDirection = light->direction;
		handoff.sunColor = light->color;
		handoff.sunIntensity = light->intensity;
	}
	handoff.iblPath = object3DManager_->GetEnvironmentTexturePath();
	StageStartHandoff::Store(handoff);
	leaving_ = true;
	SceneManager::GetInstance()->ChangeScene("STAGEPLAY");
}

void HubScene::Draw() {
	auto* commandList = dxCore_->GetCommandList();

	// 表示板の中身を RenderTexture へ（RTV とビューポートを切り替えるので、シーンの RT を描き始める前に）
	if (hatchSign_) hatchSign_->RenderSignTexture(commandList);
	// 鏡の床に映す物を反射 RT へ（これも RT を切り替えるので、シーンの RT を描き始める前に）
	if (temple_) temple_->RenderReflection(*camera_);

	// シーンの RT とビューポートを戻す（シーン RT は描画解像度）
	const Vector2 renderSize = { static_cast<float>(dxCore_->GetRenderWidth()), static_cast<float>(dxCore_->GetRenderHeight()) };
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, renderSize.x, renderSize.y, 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(renderSize.x), static_cast<LONG>(renderSize.y) };
	// メニュー・コンソールの配置は UI の仮想解像度
	const Vector2 screen = GetScreenSize();
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	// 空を最初に描く（深度書き込みなしの ReadOnly DSV）
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);
	skyboxManager_->DrawSetting();
	if (skybox_) skybox_->Draw(dxCore_);

	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	if (temple_) temple_->Draw(dxCore_);
	if (liftDescent_) liftDescent_->Draw(dxCore_);
	if (mech_) mech_->Draw(dxCore_);
	// 鏡の床は不透明物の後（ルートシグネチャを替えるので、表示板の前に Object3D を貼り直す）
	if (temple_) temple_->DrawFloor(*camera_);
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	// 表示板は半透明なので最後に
	if (hatchSign_) hatchSign_->Draw(dxCore_);

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	if (state_ == State::Waiting && !boarding_.IsPreviewing()) {
		// 待機のメニュー（決定したら消して乗り込みを見せる）
		menu_.SetPosition({ screen.x * 0.5f, screen.y * kMenuHeightRatio });
		menu_.Draw();
	} else if (state_ == State::Cockpit && console_ && reveal_.GetPhase() != CockpitReveal::Phase::Arriving) {
		const CockpitReveal::State st = reveal_.Evaluate();
		console_->Draw(screen, st.backgroundAlpha, st.menuTime, st.collapse, cockpitTime_, IsGlowAllowed());
	} else if (state_ == State::Descending && console_ && liftDescent_->GetUiCollapse() < 1.0f) {
		// ステージを決めたら UI をシャットダウンと同じ形でたたむ
		const CockpitReveal::State st = reveal_.Evaluate();
		console_->Draw(screen, st.backgroundAlpha, st.menuTime, (std::max)(st.collapse, liftDescent_->GetUiCollapse()),
			cockpitTime_, IsGlowAllowed());
	}
	// スキップの案内（左上）。デモモードでも「スキップできる」ことを見せるため出す（長押しの判定はデモ中は止めたまま）
	if (state_ == State::Descending && skip_ && !leaving_) skip_->Draw(screen.x);
	tr->Flush();
}

bool HubScene::HasBloomSources() const {
	return IsGlowAllowed() && cockpitLighting_ && hatchSign_;
}

void HubScene::DrawBloomSources() {
	if (mech_ && cockpitLighting_) {
		mech_->DrawBloom(dxCore_, cockpitLighting_->GetBloomScale(), cockpitLighting_->IsRevealing());
	}
	if (hatchSign_) hatchSign_->DrawBloom(dxCore_);
	if (liftDescent_) liftDescent_->DrawBloom(dxCore_, 1.0f);
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}

void HubScene::ApplyPose() {
	// 視点とハッチ：降りる間（とそのシーク中）は降りる時間軸、コクピットの中（③の降下も）は乗り込みの最後の姿勢、ほかは乗り込み
	const bool cockpitView = state_ == State::Cockpit || state_ == State::Descending;
	HubBoardingSequence::Pose pose{};
	if (state_ == State::Disembarking || disembark_.IsPreviewing()) {
		const HubDisembarkSequence::Pose d = disembark_.GetCurrentPose();
		pose = { d.eye, d.yaw, d.pitch, d.hatchOpenRatio, d.lightScale, d.mech };
	} else if (state_ == State::Entering || entrySequence_.IsPreviewing()) {
		// ハッチは開いて待っている・外の光はそのまま・機体は直立（Offer の時刻 0）
		const HubEntrySequence::Pose e = entrySequence_.GetCurrentPose();
		pose = { e.eye, e.yaw, e.pitch, 1.0f, 1.0f };
	} else {
		pose = cockpitView ? boarding_.Evaluate(boarding_.GetEndTime()) : boarding_.GetCurrentPose();
	}
	const CockpitReveal::State st = reveal_.Evaluate();

	// ③ の降下で機体ごと下がる（カメラ・表示板は機体の原点からの相対なので一緒に下がる）
	mech_->SetPosition({ 0.0f, liftDescent_->GetMechY(), 0.0f });
	const Vector3& origin = mech_->GetPosition();
	camera_->SetFovY(DegToRad(fovYDegrees_));
	camera_->SetTranslate({ origin.x + pose.eye.x, origin.y + pose.eye.y, origin.z + pose.eye.z });
	camera_->SetRotate({ pose.pitch, pose.yaw, 0.0f });

	mech_->SetHatchOpenRatio(pose.hatchOpenRatio);
	mech_->SetPose(pose.mech);
	// コクピットの中にいる間は本体を描かない（外が透けると全天周のモニターの左右に腕・脚が映り込むため）。
	// 透ける前はコクピットの壁が覆っているので見た目は変わらない。降りる（Disembarking）のはシャットダウンで壁が戻った後。
	// 床の映り込みにも出ない（本体の表示フラグを映り込みも見る。StagePlay の一人称も本体を描かないので、③ の切れ目の前後でそろう）
	mech_->SetBodyHidden(cockpitView);
	mech_->Update();

	// 平行光源の既定 intensity は 0 なので毎フレーム決める。
	// ハッチが閉じるにつれて弱め、外が透ける（全天周囲モニターが外を映す）につれて戻す
	const float revealAmount = cockpitView ? st.reveal : 0.0f;
	const float minLight = boarding_.GetMinLightScale();
	// 縦穴の中では外の光を弱める（床より下へ行くほど）
	const float lightScale = (cockpitView ? (minLight + (1.0f - minLight) * revealAmount) * st.sunScale : pose.lightScale)
		* liftDescent_->GetSunScale();
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(kSunLightDirection);
		lm->SetDirectionalLightColor(kSunLightColor);
		lm->SetDirectionalLightIntensity(sunIntensity_ * lightScale);
	}

	// ハッチの表示板。LOCK はコクピットの中にいる間だけ（待機へ戻る暗転で視点が外へ出たら UNLOCK）
	hatchSign_->SetLocked(cockpitView && st.signLocked);
	hatchSign_->SetVisibility(cockpitView ? st.signAlpha : 1.0f);
	// コクピットの灯り・光る部位（表示板と同じ色）・外を映す六角形
	// 六角形のディゾルブはシェーダの SV_Position（描画解像度のピクセル）で並べるので、描画解像度を渡す
	const Vector2 renderSize = { static_cast<float>(dxCore_->GetRenderWidth()), static_cast<float>(dxCore_->GetRenderHeight()) };
	cockpitLighting_->Apply(mech_->GetCockpitModel(), st.lightLevel, revealAmount, renderSize, hatchSign_->GetColor());

	// ラジアルブラー（背景を出す直前に上げ、文字を出すときに下げる）
	if (auto* blur = Game::GetPostEffect()->radialBlur) {
		const float width = cockpitView ? st.blurWidth : 0.0f;
		blur->SetEnabled(width > 0.0f);
		blur->SetCenter(0.5f, 0.5f);
		blur->SetBlurWidth(width);
	}
}

void HubScene::ChangeToTitle() {
	// タイトル ↔ HUB は白フェード（18 番 ⑧）。Fade の既定は黒なので、色と長さを直前に決める
	if (auto* fade = TransitionManager::GetInstance()->GetTransition<FadeTransition>(TransitionType::Fade)) {
		fade->SetNextFade({ 1.0f, 1.0f, 1.0f }, whiteFadeSeconds_, whiteHoldSeconds_);
	}
	SceneManager::GetInstance()->ChangeScene("TITLE");
}

void HubScene::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;
	whiteFadeSeconds_ = static_cast<float>(root["whiteFadeSeconds"].AsDouble(whiteFadeSeconds_));
	whiteHoldSeconds_ = static_cast<float>(root["whiteHoldSeconds"].AsDouble(whiteHoldSeconds_));
	fovYDegrees_ = static_cast<float>(root["fovYDegrees"].AsDouble(fovYDegrees_));
	sunIntensity_ = static_cast<float>(root["sunIntensity"].AsDouble(sunIntensity_));
	if (mech_) mech_->LoadTuning(root["mech"]);
	if (temple_) temple_->LoadTuning(root["temple"]);
	if (liftDescent_) liftDescent_->LoadTuning(root["liftDescent"]);
	boarding_.LoadTuning(root["boarding"]);
	disembark_.LoadTuning(root["disembark"]);
	entrySequence_.LoadTuning(root["entry"]);
	reveal_.LoadTuning(root["cockpitReveal"]);
	if (cockpitLighting_) cockpitLighting_->LoadTuning(root["cockpitLighting"]);
	if (hatchSign_) hatchSign_->LoadTuning(root["hatchSign"]);
	if (console_) console_->LoadTuning(root["cockpitConsole"]);
}

void HubScene::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["whiteFadeSeconds"] = JsonValue(static_cast<double>(whiteFadeSeconds_));
	root["whiteHoldSeconds"] = JsonValue(static_cast<double>(whiteHoldSeconds_));
	root["fovYDegrees"] = JsonValue(static_cast<double>(fovYDegrees_));
	root["sunIntensity"] = JsonValue(static_cast<double>(sunIntensity_));
	if (mech_) mech_->SaveTuning(root["mech"]);
	if (temple_) temple_->SaveTuning(root["temple"]);
	if (liftDescent_) liftDescent_->SaveTuning(root["liftDescent"]);
	boarding_.SaveTuning(root["boarding"]);
	disembark_.SaveTuning(root["disembark"]);
	entrySequence_.SaveTuning(root["entry"]);
	reveal_.SaveTuning(root["cockpitReveal"]);
	if (cockpitLighting_) cockpitLighting_->SaveTuning(root["cockpitLighting"]);
	if (hatchSign_) hatchSign_->SaveTuning(root["hatchSign"]);
	if (console_) console_->SaveTuning(root["cockpitConsole"]);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void HubScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Button("Save Tuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload Tuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
	static const char* kStateNames[] = { "Entering", "Waiting", "Boarding", "Cockpit", "Disembarking", "Descending" };
	static const char* kEntryNames[] = { "Temple", "Cockpit", "Disembark" };
	ImGui::Text("State: %s  (entry: %s)", kStateNames[static_cast<int>(state_)],
		kEntryNames[static_cast<int>(entry_)]);

	ImGui::SeparatorText("To Title (white fade)");
	ImGui::DragFloat("White Fade (s)", &whiteFadeSeconds_, 0.05f, 0.05f, 5.0f);
	ImGui::DragFloat("White Hold (s)", &whiteHoldSeconds_, 0.05f, 0.0f, 5.0f);

	ImGui::SeparatorText("View");
	ImGui::DragFloat("FovY (deg)", &fovYDegrees_, 0.5f, 20.0f, 110.0f);
	ImGui::DragFloat("Sun Intensity", &sunIntensity_, 0.01f, 0.0f, 5.0f);

	if (mech_ && ImGui::CollapsingHeader("Mech")) {
		mech_->OnImGui();
	}
	if (temple_ && ImGui::CollapsingHeader("Temple (17 H1/H2)")) {
		temple_->OnImGui();
	}
	if (ImGui::CollapsingHeader("Entry from Title (18-8)")) {
		// 最後まで再生すると待機へ（本番と同じ）
		if (ImGui::Button("Play##Entry")) {
			boarding_.Stop();
			disembark_.Stop();
			reveal_.Reset();
			entrySequence_.Start(boarding_.GetHomeKey());
			state_ = State::Entering;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop##Entry")) {
			entrySequence_.Stop();
			state_ = State::Waiting;
		}
		entrySequence_.OnImGui(boarding_.GetHomeKey());
	}
	if (ImGui::CollapsingHeader("Boarding (18 OUT-1)")) {
		// 最後まで再生するとコクピットの起動へ進む（本番と同じ）
		if (ImGui::Button("Play")) {
			reveal_.Reset();
			boarding_.Start();
			state_ = State::Boarding;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			boarding_.Stop();
			reveal_.Reset();
			state_ = State::Waiting;
		}
		boarding_.OnImGui();
	}
	if (ImGui::CollapsingHeader("Cockpit Reveal (18 IN-1 / OUT-2)", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Boot")) EnterCockpit();
		ImGui::SameLine();
		if (ImGui::Button("Shutdown") && state_ == State::Cockpit) reveal_.StartShutdown();
		ImGui::SameLine();
		// ⑦の入口と同じ：真っ暗なコクピットから灯りを点けて降りる
		if (ImGui::Button("Arrive")) EnterArrive();
		reveal_.OnImGui();
	}
	if (ImGui::CollapsingHeader("Disembark (18 IN-2)")) {
		// 最後まで再生すると待機へ戻る（本番と同じ）
		if (ImGui::Button("Play##Disembark")) {
			reveal_.Reset();
			disembark_.Start(boarding_.GetHomeKey(), boarding_.GetMinLightScale());
			state_ = State::Disembarking;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop##Disembark")) {
			disembark_.Stop();
			boarding_.Stop();
			reveal_.Reset();
			state_ = State::Waiting;
		}
		disembark_.OnImGui(boarding_.GetHomeKey(), boarding_.GetMinLightScale());
	}
	if (liftDescent_ && ImGui::CollapsingHeader("Lift Descent (16 F2 / 18-3 OUT)")) {
		// 本番と同じくコクピットの中から降りる。Reset で床の高さのコクピットへ戻す
		if (ImGui::Button("Play##Lift")) {
			if (state_ != State::Cockpit) EnterCockpit();
			StartDescent();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset##Lift")) {
			liftDescent_->Reset();
			if (state_ == State::Descending) state_ = State::Cockpit;
		}
		ImGui::SameLine();
		ImGui::Checkbox("Change Scene at End", &descentChangesScene_);
		liftDescent_->OnImGui();
	}
	if (cockpitLighting_ && ImGui::CollapsingHeader("Cockpit Lighting / Hex")) {
		cockpitLighting_->OnImGui();
	}
	if (hatchSign_ && ImGui::CollapsingHeader("Hatch Sign")) {
		hatchSign_->OnImGui();
	}
	if (console_ && ImGui::CollapsingHeader("Cockpit Console (stage select UI)")) {
		console_->OnImGui();
	}
#endif // _DEBUG
}
