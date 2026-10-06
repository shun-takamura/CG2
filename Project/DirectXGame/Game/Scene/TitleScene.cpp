#include "TitleScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "TextureManager.h"
#include "Skybox.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "PostEffect.h"
#include "VignetteEffect.h"
#include "RadialBlurEffect.h"
#include "MaskedOutlineEffect.h"
#include "LightShaftEffect.h"
#include "FadeTransition.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include "Water/WaterReflection.h"
#include "Water/WaterSurface.h"
#include "Cloud/CloudLayer.h"
#include "Effect/EffectManager.h"
#include "AttractMode.h"
#include "UIPointer.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "WindowsApplication.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 雲なしの昼の青空（tools/BlenderPipeline/gen_title_sky.py で生成。太陽の円盤は描いていない）
	constexpr const char* kTitleSkyboxPath = "Resources/Cubemaps/title_clear_sky.dds";
	// 映り込み（PBR の IBL）専用。地平線より下を石っぽい暖色にしてあり、カメラより低い所の金が
	// 明るい水色を映して白っぽくなるのを防ぐ（gen_title_sky.py --ibl）。背景・水面は通常版を使う
	constexpr const char* kTitleIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";
	// 空の太陽の向きから求めた光の進む向き（cubemap 上の太陽は仰角 55°、方向 (-0.191, 0.815, -0.547)）
	const Vector3 kSunLightDirection = { 0.191f, -0.815f, 0.547f };
	// 日差しは少し暖色にして、空の青い環境光で白い大理石が青く染まるのを打ち消す
	const Vector4 kSunLightColor = { 1.0f, 0.96f, 0.9f, 1.0f };

	// 水底の床：石畳（ベースカラー・法線マップ・ハイトマップ。1 枚 3m 四方）。
	// 凹凸は高さ -0.013〜+0.0068m（Assets/Textures/MaskTexture/Terrain/Flagstone_Height.json）
	constexpr const char* kWaterFloorTexturePath = "Resources/Textures/Terrain/Flagstone_BaseColor.dds";
	constexpr const char* kWaterFloorNormalMapPath = "Resources/Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.dds";
	constexpr const char* kWaterFloorHeightMapPath = "Resources/Textures/MaskTexture/Terrain/Flagstone_Height.dds";
	constexpr float kWaterFloorTileMeters = 3.0f;
	constexpr float kWaterFloorParallaxDepth = (0.0068f + 0.013f) / kWaterFloorTileMeters; // 深さ[m] ÷ 1 枚の長さ[m]
	constexpr float kWaterHeight = 0.0f;
	constexpr float kWaterDepth = 0.1f;
	constexpr float kCameraFarClip = 1000.0f;
	constexpr float kWaterSize = 1500.0f; // 半径 750m < ファークリップ

	// 遠景の雲のノイズ（tools/Python/gen_cloud_noise.py で生成。R=形 / G=マスク）
	constexpr const char* kCloudNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";

	// ロゴは板ポリ（tools/Python/gen_title_logo_plane.py。幅 2 × 高さ 1、表が -Z）に PNG を貼る。
	// 板はテクスチャを持たないので、ロード後に差し替える
	constexpr const char* kLogoModelDir = "Resources/Models/TitleLogo";
	constexpr const char* kLogoTexturePath = "Resources/Textures/Title/TitleRogo.dds";

	// 扉（tools/BlenderPipeline/gen_title_door.py で生成）。枠の原点は台座の底面の中央で、正面が +Z。
	// 扉板の蝶番の軸（枠のローカル）。左右は正面から見た向き（エンジンの +X が正面から見て左）
	constexpr const char* kDoorModelDir = "Resources/Models/TitleDoor";
	const Vector3 kDoorHingeL = { 0.9f, 0.0f, -0.2f };
	const Vector3 kDoorHingeR = { -0.9f, 0.0f, -0.2f };
	// 扉の線画（gen_title_door.py --export-lines）。並びは doorLines_ と同じ 枠 / 左 / 右
	constexpr const char* kDoorLineMeshes[] = { "door_frame_lines.mesh", "door_leaf_L_lines.mesh", "door_leaf_R_lines.mesh" };
	// MaskedOutline は ID==1 の物を colorFire で縁取る
	constexpr uint8_t kDoorOutlineId = 1;
	// ライトシャフトの光源（光の部屋）。アウトラインの 1 / 2 と重ならない値
	constexpr uint8_t kDoorLightId = 3;
	constexpr const char* kTitleBgm = "bgm_title_scene";

	enum TitleMenuItem : int {
		kTitleStart = 0,
		kTitleQuit,
	};

	// 終端で少しだけ行き過ぎて戻る ease-out-back（ロゴの「ポン」と出る感じ）
	float EaseOutBack(float t) {
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;
		const float u = t - 1.0f;
		return 1.0f + c3 * u * u * u + c1 * u * u;
	}

	float EaseOutCubic(float t) {
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}

	float Progress(float time, float duration) {
		return (duration > 0.0f) ? std::clamp(time / duration, 0.0f, 1.0f) : 1.0f;
	}

	// Y 軸まわりに yaw だけ回す（Yaw=θ の前方 (sinθ, 0, cosθ) と同じ向きの取り方）
	Vector3 RotateYaw(const Vector3& v, float yaw) {
		const float c = std::cos(yaw);
		const float s = std::sin(yaw);
		return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
	}

	float SmoothStep01(float t) {
		return t * t * (3.0f - 2.0f * t);
	}
}

TitleScene::TitleScene() = default;
TitleScene::~TitleScene() = default;

void TitleScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();
	// EffectManager は全シーン共通。前のシーンで鳴っていたエフェクトをタイトルに持ち込まない
	EffectManager::GetInstance()->StopAll();

	camera_ = std::make_unique<Camera>();
	// 位置と向きは UpdateCameraAndLogo で周回中心から決める。
	// 既定の 100m だと水面が途中で切れて地平線の手前に空が覗くので、水面の端より遠くまで取る
	camera_->SetFarClip(kCameraFarClip);
	orbitAngle_ = 0.0f;
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, kTitleSkyboxPath);
	// PBR の IBL（t1）。無いと金属は真っ黒、大理石も環境光が無く暗くなる
	TextureManager::GetInstance()->LoadTexture(kTitleIblPath);
	object3DManager_->SetEnvironmentTexture(kTitleIblPath);

	TextureManager::GetInstance()->LoadTexture(kLogoTexturePath);
	logo_ = std::make_unique<Object3DInstance>();
	logo_->Initialize(object3DManager_, dxCore_, kLogoModelDir, "logo_plane.mesh", "TitleLogo");

	waterReflection_ = std::make_unique<WaterReflection>();
	waterReflection_->Initialize(dxCore_, srvManager_, object3DManager_, 1.0f);
	waterReflection_->SetWaterHeight(kWaterHeight);

	doorFrame_ = std::make_unique<Object3DInstance>();
	doorFrame_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_frame.mesh", "TitleDoorFrame");
	doorLeafL_ = std::make_unique<Object3DInstance>();
	doorLeafL_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_leaf_L.mesh", "TitleDoorLeafL");
	doorLeafR_ = std::make_unique<Object3DInstance>();
	doorLeafR_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_leaf_R.mesh", "TitleDoorLeafR");
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		part->SetObjectId(kDoorOutlineId);
	}
	doorLight_ = std::make_unique<Object3DInstance>();
	doorLight_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_light.mesh", "TitleDoorLight");
	doorLight_->SetObjectId(kDoorLightId);
	for (size_t i = 0; i < doorLines_.size(); ++i) {
		doorLines_[i] = std::make_unique<Object3DInstance>();
		doorLines_[i]->Initialize(object3DManager_, dxCore_, kDoorModelDir, kDoorLineMeshes[i],
			"TitleDoorLine" + std::to_string(i));
	}

	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore_, srvManager_, object3DManager_, kWaterFloorTexturePath);
	water_->SetWaterHeight(kWaterHeight);
	water_->GetParams().depth = kWaterDepth;
	water_->SetSize(kWaterSize);
	water_->GetParams().floorTiling = 1.0f / kWaterFloorTileMeters;
	water_->SetFloorMaps(kWaterFloorNormalMapPath, kWaterFloorHeightMapPath, kWaterFloorParallaxDepth);
	// 待機中は「ごく小さな波がたまに出る」程度に抑える（大きな波は演出側で EmitRing / StartRingBurst）
	water_->GetParams().ringAmplitude = 0.013f;
	water_->GetParams().ringInterval = 6.0f;
	water_->GetParams().ringJitter = 0.7f;

	cloudLayer_ = std::make_unique<CloudLayer>();
	cloudLayer_->Initialize(dxCore_, kCloudNoisePath);
	cloudLayer_->SetSunDirection({ -kSunLightDirection.x, -kSunLightDirection.y, -kSunLightDirection.z });
	// --no-clouds：雲なしで起動（tools/Python/run_cloud_bench.py で雲あり/なしの GPU 時間を比べる用）。
	// enabled=0 ならシェーダは先頭で抜けるので、差がそのまま雲のコストになる
	if (std::wstring(::GetCommandLineW()).find(L"--no-clouds") != std::wstring::npos) {
		cloudLayer_->GetParams().enabled = false;
	}
	skybox_->SetCloudLayer(cloudLayer_.get());
	// PBR の鏡面反射（金の装飾）にも同じ雲を映す。シーンをまたいで残るので Finalize で外す
	object3DManager_->SetCloudLayer(cloudLayer_.get());
	water_->SetCloudLayer(cloudLayer_.get());

	// 平行光源の既定 intensity は 0 なので、ロゴを照らす光をここで必ず設定する
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(kSunLightDirection);
		lm->SetDirectionalLightColor(kSunLightColor);
		lm->SetDirectionalLightIntensity(sunIntensity_);
	}

	if (auto* pe = Game::GetPostEffect()) {
		if (pe->vignette) {
			pe->vignette->SetEnabled(true);
			pe->vignette->SetIntensity(vignetteIntensity_);
		}
	}

	menu_.SetItems({ "スタート", "ゲーム終了" });
	SoundManager::GetInstance()->Play2DSoundLooped(kTitleBgm, bgmVolume_);

	// まだどちらも反射に登録していない状態から、ロゴだけを出す（扉は ③ で出す）
	logoVisible_ = false;
	doorVisible_ = false;
	doorLinesVisible_ = false;
	doorLightVisible_ = false;
	ResetSequence();
}

void TitleScene::Finalize() {
	Game::GetPostEffect()->ResetEffects();
	SoundManager::GetInstance()->Stop2DSound(kTitleBgm);
	ReleaseDoorPointLight();
	// cloudLayer_ はこのシーンと一緒に消えるので、共有の Object3DManager から外しておく
	object3DManager_->SetCloudLayer(nullptr);
	EffectManager::GetInstance()->StopAll();
}

void TitleScene::ResetSequence() {
	phase_ = Phase::Idle;
	phaseTime_ = 0.0f;
	elapsed_ = 0.0f;
	framingBlend_ = 0.0f;
	logoDissolve_ = 1.0f;
	doorDissolve_ = 1.0f;
	doorLineDissolve_ = 1.0f;
	doorLineAlpha_ = 1.0f;
	doorOutlineIntensity_ = 0.0f;
	doorOpenDegrees_ = 0.0f;
	enterTransitionStarted_ = false;
	SetLogoVisible(true);
	SetDoorVisible(false);
	SetDoorLinesVisible(false);
	SetDoorLightVisible(false);
	SetSunYaw(0.0f);
	ReleaseDoorPointLight();
	enterBlend_ = 0.0f;
	enterOpenRatio_ = 0.0f;
	SoundManager::GetInstance()->Set2DSoundVolume(kTitleBgm, bgmVolume_);
	if (water_) {
		water_->StopRingBurst();
		water_->SetAmbientRingScale(1.0f);
	}
}

void TitleScene::ChangePhase(Phase next) {
	phase_ = next;
	phaseTime_ = 0.0f;

	switch (next) {
	case Phase::LogoVanish: {
		SoundManager::GetInstance()->Play2DSound("se_ui_decide");
		// ロゴの中心から広がる揺らぎ（歪み）をディゾルブに重ねる
		const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
		EffectManager::GetInstance()->Play("TitleLogoRipple",
			{ orbitCenter_.x, waterHeight + logoHeight_, orbitCenter_.z });
		if (water_) {
			water_->StartRingBurst(orbitCenter_, burstInterval_, burstAmplitude_, burstWavelength_);
			// 待機中の小さな波を引っ込めて、大きな波だけにする
			water_->SetAmbientRingScale(0.0f, 0.5f);
		}
		break;
	}

	case Phase::DoorOutline: {
		// ③〜④ で ease-out して止める。今の角速度から始めて 0 で止まる2次の減速なので、
		// 進む角度は「角速度 × 時間 / 2」。止まる角度が先に決まるので、扉をそこへ向けて立てる
		const float stopDuration = doorOutlineSeconds_ + doorFormSeconds_;
		orbitStopElapsed_ = 0.0f;
		orbitStopStartAngle_ = orbitAngle_;
		orbitStopEndAngle_ = orbitAngle_ + orbitSpeed_ * stopDuration * 0.5f;
		// カメラは角度 θ のとき中心の -(sinθ, cosθ) 側にいるので、扉の正面（+Z を yaw だけ回した向き）を θ+π に向ける
		doorYaw_ = orbitStopEndAngle_ + kPi;
		// 扉が逆光にならないよう、光も同じだけ回す（扉はまだ見えていないので切り替えは目に入らない）
		SetSunYaw(orbitStopEndAngle_);
		// 線画を高さだけのディゾルブで下から出す
		SetDoorLinesVisible(true);
		doorLineDissolve_ = 0.0f;
		doorLineAlpha_ = 1.0f;
		break;
	}

	case Phase::DoorForm:
		SetDoorVisible(true);
		doorDissolve_ = 0.0f;
		doorLineDissolve_ = 1.0f;
		break;

	case Phase::Ready:
		doorDissolve_ = 1.0f;
		framingBlend_ = 1.0f;
		if (water_) {
			water_->StopRingBurst();
			water_->SetAmbientRingScale(1.0f, 1.5f);
		}
		menu_.SetSelectedIndex(kTitleStart);
		break;

	case Phase::Enter:
		enterTransitionStarted_ = false;
		enterMoveSoundPlayed_ = false;
		SetDoorLightVisible(true);
		SoundManager::GetInstance()->Play2DSound("se_title_open_door");
		// 金の装飾に中の光を映す。枠が無ければ光らせないだけ
		if (auto* lm = LightManager::GetInstance(); lm && doorPointLight_ == kInvalidLightSlot) {
			doorPointLight_ = lm->AcquirePointLight();
		}
		break;

	default:
		break;
	}
}

void TitleScene::UpdatePhase(float dt) {
	phaseTime_ += dt;

	switch (phase_) {
	case Phase::LogoVanish: {
		// ノイズのディゾルブで消す（波に合わせた揺らぎは A-5 で足す）
		const float t = Progress(phaseTime_, logoVanishSeconds_);
		logoDissolve_ = 1.0f - SmoothStep01(t);
		if (t >= 1.0f) {
			SetLogoVisible(false);
			ChangePhase(Phase::DoorOutline);
		}
		break;
	}

	case Phase::DoorOutline:
		doorLineDissolve_ = SmoothStep01(Progress(phaseTime_, doorOutlineSeconds_));
		if (phaseTime_ >= doorOutlineSeconds_) ChangePhase(Phase::DoorForm);
		break;

	case Phase::DoorForm: {
		// 置いてある位置で、下からディゾルブで実体化する（光の粒の収束は A-7 で足す）
		const float t = Progress(phaseTime_, doorFormSeconds_);
		doorDissolve_ = SmoothStep01(t);
		// 扉が出きるまでに、水面近くへ下がって見上げる構図へ
		framingBlend_ = SmoothStep01(t);
		// 実体化していく扉の形を、光るアウトラインで縁取る
		doorOutlineIntensity_ = Progress(phaseTime_, doorOutlineFadeInSeconds_);
		if (t >= 1.0f) ChangePhase(Phase::Ready);
		break;
	}

	case Phase::Ready:
		// 扉が出きったら、線画とアウトラインをそろって消す
		if (doorLinesVisible_ || doorOutlineIntensity_ > 0.0f) {
			const float t = Progress(phaseTime_, doorLineFadeSeconds_);
			doorLineAlpha_ = 1.0f - t;
			doorOutlineIntensity_ = 1.0f - t;
			if (t >= 1.0f) {
				SetDoorLinesVisible(false);
				doorOutlineIntensity_ = 0.0f;
			}
		}
		break;

	case Phase::Enter:
		UpdateEnter();
		break;

	default:
		break;
	}
}

void TitleScene::Update() {
	const float dt = GetScaledDeltaTime();
	elapsed_ += dt;

	UpdatePhase(dt);

	// 遷移中も背景とロゴは動かし続ける（止まった画面を見せない）
	UpdateCameraAndLogo(dt);
	UpdateDoor();
	ApplyLogoMaterial();
	gameViewProjection_ = camera_->GetViewProjectionMatrix();
	gameEyePosition_ = camera_->GetTranslate();

	// デバッグカメラ ON ならここでカメラ行列が差し替わる。
	// Skybox / ロゴはその時点の VP を CB に焼くので、差し替えの後で Update する
	UpdateDebugCameraIfActive();
	// 描画するカメラ（デバッグカメラ中はそちら）が決まってからマウスのレイを飛ばす
	UpdateMouseRipples();
	skybox_->Update(dt);
	// 空側のレイの原点は描画カメラ（デバッグカメラ中はそちら）
	if (cloudLayer_) cloudLayer_->Update(dt, camera_->GetTranslate());
	logo_->Update();
	doorFrame_->Update();
	doorLeafL_->Update();
	doorLeafR_->Update();
	for (auto& line : doorLines_) line->Update();
	doorLight_->SetTranslate(doorFrame_->GetTranslate());
	doorLight_->SetRotate(doorFrame_->GetRotate());
	doorLight_->Update();
	if (water_) water_->Update(dt);
	// 光の粒（A-7）・後光（A-9b）など。描画カメラ（デバッグカメラ中はそちら）でビルボードを向ける
	UpdateGlobalEffects(camera_.get(), dt);

	UpdatePostEffect();
	ApplyDoorOutline();
	ApplyEnterLighting();

#ifdef _DEBUG
	OnImGuiTuning();
#endif

	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	UpdateInput();
}

void TitleScene::UpdateInput() {
	auto* actionMap = input_->GetActionMap();
	if (!actionMap) return;
	const UIPointer pointer = UIPointer::FromInput(input_);

	switch (phase_) {
	case Phase::Idle: {
		// ロゴ登場中の入力は受け付けない（演出を飛ばして即次へ進まないように）
		if (elapsed_ < introDuration_) return;
		// デモモードは一定時間で押したことにする
		auto* attract = AttractMode::GetInstance();
		const bool autoPress = attract->IsEnabled() && elapsed_ >= introDuration_ + attract->titleIdleWait;
		// マウスはゲーム画面の上での左クリックだけ数える（Debug の ImGui パネルの操作で進まないように）
		const bool pressed = (actionMap->AnyInputTriggered() && !IsPressExcludedInput()) || pointer.clicked;
		if (pressed || autoPress) ChangePhase(Phase::LogoVanish);
		return;
	}

	case Phase::Ready: {
		// デモモードは一定時間で「スタート」を決定する
		if (auto* attract = AttractMode::GetInstance(); attract->IsEnabled() && phaseTime_ >= attract->titleReadyWait) {
			menu_.SetSelectedIndex(kTitleStart);
			SoundManager::GetInstance()->Play2DSound("se_ui_decide");
			ChangePhase(Phase::Enter);
			return;
		}
		// キャンセルは無視する（ロゴは消えているので戻る先が無い）
		menu_.SetPosition({ static_cast<float>(dxCore_->GetSwapChainWidth()) * 0.5f,
			static_cast<float>(dxCore_->GetSwapChainHeight()) * menuHeightRatio_ });
		const VerticalMenu::Result result = menu_.Update(actionMap, &pointer);
		if (result != VerticalMenu::Result::Confirmed) return;
		switch (menu_.GetSelectedIndex()) {
		case kTitleStart:
			ChangePhase(Phase::Enter);
			return;
		case kTitleQuit:
			PostQuitMessage(0);
			return;
		}
		return;
	}

	default:
		// ②〜④ と ⑥ は入力を受け付けない
		return;
	}
}

bool TitleScene::IsPressExcludedInput() const {
	// マウスは水面に波を立てる操作、F8 はデモモードの切り替えなので「PRESS ANY BUTTON」には数えない
	if (auto* mouse = input_->GetMouse()) {
		using B = MouseInput::Button;
		for (B b : { B::Left, B::Right, B::Middle, B::Button4 }) {
			if (mouse->IsButtonTriggered(b)) return true;
		}
	}
	if (auto* keyboard = input_->GetKeyboard(); keyboard && keyboard->TriggerKey(DIK_F8)) return true;
	return false;
}

bool TitleScene::PickWaterPoint(Vector3& out) const {
	// マウスのゲーム画面上の位置（Debug のビューポート窓の換算は UIPointer が行う）
	const UIPointer pointer = UIPointer::FromInput(input_);
	if (!pointer.valid) return false;
	const float clientX = pointer.position.x;
	const float clientY = pointer.position.y;

	// 描画しているカメラの逆行列で、近い面と遠い面の点に戻してレイにする（NDC の z は 0..1）
	const float ndcX = clientX / static_cast<float>(WindowsApplication::kClientWidth) * 2.0f - 1.0f;
	const float ndcY = 1.0f - clientY / static_cast<float>(WindowsApplication::kClientHeight) * 2.0f;
	const Matrix4x4 inverseViewProjection = Inverse(camera_->GetViewProjectionMatrix());
	const Vector3 nearPoint = TransformCoordinate({ ndcX, ndcY, 0.0f }, inverseViewProjection);
	const Vector3 farPoint = TransformCoordinate({ ndcX, ndcY, 1.0f }, inverseViewProjection);
	const Vector3 dir{ farPoint.x - nearPoint.x, farPoint.y - nearPoint.y, farPoint.z - nearPoint.z };
	if (std::abs(dir.y) < 1e-6f) return false;

	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	const float t = (waterHeight - nearPoint.y) / dir.y;
	if (t < 0.0f || t > 1.0f) return false;  // 空を指している
	out = { nearPoint.x + dir.x * t, waterHeight, nearPoint.z + dir.z * t };
	return true;
}

void TitleScene::UpdateMouseRipples() {
	if (!water_) return;
	Vector3 point{};
	if (!PickWaterPoint(point)) {
		hasPrevMousePoint_ = false;
		return;
	}
	if (!hasPrevMousePoint_) {
		// 水面に入った最初のフレームは位置を覚えるだけ（止まっている間は波を立てない）
		prevMousePoint_ = point;
		hasPrevMousePoint_ = true;
		return;
	}

	// 動かした距離に比例して、軌跡に沿って等間隔に波源を置く（速く動かしても途切れないように）
	const Vector3 delta{ point.x - prevMousePoint_.x, 0.0f, point.z - prevMousePoint_.z };
	const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
	if (distance < 1e-3f) return;
	const float spacing = (std::max)(mouseRippleSpacing_, 0.01f);
	const int count = (std::min)(static_cast<int>(std::ceil(distance / spacing)), 8);
	const float strength = (std::min)(mouseRippleStrength_ * distance / static_cast<float>(count), mouseRippleMaxStrength_);
	for (int i = 1; i <= count; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(count);
		water_->AddRippleImpulse({ prevMousePoint_.x + delta.x * t, point.y, prevMousePoint_.z + delta.z * t },
			mouseRippleRadius_, strength);
	}
	prevMousePoint_ = point;
}

void TitleScene::UpdateCameraAndLogo(float dt) {
	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	if (waterReflection_) waterReflection_->SetWaterHeight(waterHeight);

	// カメラ：周回中心の周りを回り、中心の aimHeight を見る。
	// Yaw=θ のときの前方は (sinθ, 0, cosθ) なので、中心からその逆向きに radius 離れた所に置く
	switch (phase_) {
	case Phase::Idle:
	case Phase::LogoVanish:
		orbitAngle_ += orbitSpeed_ * dt;
		break;
	case Phase::DoorOutline:
	case Phase::DoorForm: {
		orbitStopElapsed_ += dt;
		const float t = Progress(orbitStopElapsed_, doorOutlineSeconds_ + doorFormSeconds_);
		const float eased = 1.0f - (1.0f - t) * (1.0f - t);
		orbitAngle_ = orbitStopStartAngle_ + (orbitStopEndAngle_ - orbitStopStartAngle_) * eased;
		break;
	}
	default:
		// ⑤⑥ は止まったまま（Debug で Orbit Angle を動かせるよう上書きしない）
		break;
	}

	// ロゴ用 → 扉用（④）→ 扉に入る（⑥）の順に補間する
	auto blendFraming = [](const CameraFraming& a, const CameraFraming& c, float t) {
		return CameraFraming{ std::lerp(a.fovY, c.fovY, t), std::lerp(a.radius, c.radius, t),
			std::lerp(a.height, c.height, t), std::lerp(a.aimHeight, c.aimHeight, t) };
	};
	const CameraFraming framing = blendFraming(
		blendFraming(logoFraming_, doorFraming_, framingBlend_), enterFraming_, enterBlend_);
	const float pitch = std::atan2(framing.height - framing.aimHeight, framing.radius); // 正で見下ろす / 負で見上げる
	camera_->SetFovY(framing.fovY);
	camera_->SetRotate({ pitch, orbitAngle_, 0.0f });
	camera_->SetTranslate({
		orbitCenter_.x - std::sin(orbitAngle_) * framing.radius,
		waterHeight + framing.height,
		orbitCenter_.z - std::cos(orbitAngle_) * framing.radius });
	camera_->Update();

	if (water_) water_->SetRippleCenter(orbitCenter_);

	// ロゴ：周回中心の真上に固定。板の表は -Z なので、カメラと同じ回転で常にカメラを向く
	const float bob = std::sin(elapsed_ * logoBobSpeed_) * logoBobAmplitude_;
	const float sway = std::sin(elapsed_ * logoSwaySpeed_) * logoSwayAmplitude_;
	const float s = logoScale_ * EaseOutBack(Progress(elapsed_, introDuration_));
	logo_->SetTranslate({ orbitCenter_.x, waterHeight + logoHeight_ + bob, orbitCenter_.z });
	logo_->SetRotate({ pitch, orbitAngle_ + sway, 0.0f });
	logo_->SetScale({ s, s, s });
}

void TitleScene::UpdateDoor() {
	if (!doorFrame_) return;
	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	const float depth = water_ ? water_->GetParams().depth : kWaterDepth;

	// 台座の底を水底の床に合わせる（台座の上面が水面から少し出る）
	const Vector3 base{ orbitCenter_.x, waterHeight - depth, orbitCenter_.z };
	Transform frameXf;
	frameXf.scale = { 1.0f, 1.0f, 1.0f };
	frameXf.rotate = { 0.0f, doorYaw_, 0.0f };
	frameXf.translate = base;
	const Matrix4x4 frameMatrix = MakeAffineMatrix(frameXf);

	doorFrame_->SetTranslate(base);
	doorFrame_->SetRotate(frameXf.rotate);

	// 扉板は蝶番の軸まわりに回す。奥（-Z）へ開くので左右で符号が逆
	const float open = DegToRad(doorOpenDegrees_);
	doorLeafL_->SetTranslate(TransformCoordinate(kDoorHingeL, frameMatrix));
	doorLeafL_->SetRotate({ 0.0f, doorYaw_ - open, 0.0f });
	doorLeafR_->SetTranslate(TransformCoordinate(kDoorHingeR, frameMatrix));
	doorLeafR_->SetRotate({ 0.0f, doorYaw_ + open, 0.0f });

	// 線画は部位と同じ原点で作ってあるので、同じ変換で置く
	const Vector3 lineTranslates[] = { base, TransformCoordinate(kDoorHingeL, frameMatrix), TransformCoordinate(kDoorHingeR, frameMatrix) };
	const Vector3 lineRotates[] = { frameXf.rotate, { 0.0f, doorYaw_ - open, 0.0f }, { 0.0f, doorYaw_ + open, 0.0f } };
	for (size_t i = 0; i < doorLines_.size(); ++i) {
		doorLines_[i]->SetTranslate(lineTranslates[i]);
		doorLines_[i]->SetRotate(lineRotates[i]);
	}

	// マテリアルは GPU 側の準備ができてから作られる（遅延ロード）ので、初期化時ではなく毎フレーム上書きする
	ApplyDoorMaterials(base.y);
	ApplyDoorLineMaterials(base.y);
}

void TitleScene::ApplyDissolve(Material& material, float progress, const DissolveStyle& style,
	float heightMin, float heightMax) {
	// 見えきっている間は無効にして、ノイズの計算を省く
	material.dissolveEnable = (progress < 1.0f) ? 1 : 0;
	material.dissolveProgress = progress;
	material.dissolveEdgeWidth = style.edgeWidth;
	material.dissolveNoiseScale = style.noiseScale;
	material.dissolveNoiseWeight = style.noiseWeight;
	material.dissolveHeightMin = heightMin;
	material.dissolveHeightMax = heightMax;
	material.dissolveEdgeColor = style.edgeColor;
}

void TitleScene::ApplyDoorMaterials(float baseHeight) {
	// 部位ごとの .mat 名（door_*_Marble.mat / door_*_Gold.mat）で大理石と金を見分ける
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		ModelInstance* model = part ? part->GetModelInstance() : nullptr;
		if (!model) continue;
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (!sm.material) continue;
			ApplyDissolve(*sm.material, doorDissolve_, doorDissolveStyle_, baseHeight, baseHeight + doorDissolveHeight_);
			if (sm.matFilePath.find("_Gold") != std::string::npos) {
				sm.material->environmentCoefficient = doorGold_.envCoefficient;
				sm.material->metallic = doorGold_.metallic;
				sm.material->roughness = doorGold_.roughness;
				sm.material->color = doorGold_.color;
				sm.material->cloudReflection = doorGold_.cloudReflection;
			} else {
				sm.material->environmentCoefficient = doorMarbleEnvCoefficient_;
			}
		}
	}
}

void TitleScene::ApplyLogoMaterial() {
	ModelInstance* model = logo_ ? logo_->GetModelInstance() : nullptr;
	// テクスチャの差し替えは GPU の準備後に行う（準備時に .mat のパスで上書きされるため）
	if (!model || !model->IsGPUReady()) return;
	if (model->GetTextureFilePath() != kLogoTexturePath) {
		model->SetTextureFilePath(kLogoTexturePath);
	}
	for (const RenderSubmesh& sm : model->GetSubmeshes()) {
		if (!sm.material) continue;
		// 日差しの向きで明暗が出ないよう、PNG の色をそのまま出す
		sm.material->enableLighting = 0;
		// ノイズだけで消すので高さの範囲は使わない
		ApplyDissolve(*sm.material, logoDissolve_, logoDissolveStyle_, 0.0f, 1.0f);
	}
}

void TitleScene::ApplyDoorLineMaterials(float baseHeight) {
	for (auto& line : doorLines_) {
		ModelInstance* model = line ? line->GetModelInstance() : nullptr;
		if (!model) continue;
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (!sm.material) continue;
			sm.material->enableLighting = 0;
			sm.material->color = { doorLineColor_.x, doorLineColor_.y, doorLineColor_.z, doorLineColor_.w * doorLineAlpha_ };
			ApplyDissolve(*sm.material, doorLineDissolve_, doorLineDissolveStyle_, baseHeight, baseHeight + doorDissolveHeight_);
		}
	}
}

void TitleScene::ApplyDoorOutline() {
	auto* pe = Game::GetPostEffect();
	if (!pe || !pe->maskedOutline) return;
	if (doorOutlineIntensity_ <= 0.0f) {
		pe->maskedOutline->SetEnabled(false);
		return;
	}
	pe->maskedOutline->SetEnabled(true);
	// 濃さは色の α で渡す（シェーダは edge × α で合成する）。点滅はさせない
	pe->maskedOutline->SetFireColor({ doorOutlineColor_.x, doorOutlineColor_.y, doorOutlineColor_.z,
		doorOutlineColor_.w * doorOutlineIntensity_ });
	pe->maskedOutline->SetMinIntensity(1.0f);
	pe->maskedOutline->SetEdgeStrength(doorOutlineEdgeStrength_);
}

bool TitleScene::HasExtraIdPassObjects() const {
	return (doorVisible_ && doorOutlineIntensity_ > 0.0f) || doorLightVisible_;
}

void TitleScene::DrawExtraIdPassObjects() {
	if (doorVisible_ && doorOutlineIntensity_ > 0.0f) {
		for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
			if (part) part->DrawIdPass(dxCore_);
		}
	}
	// 光の部屋は見えている部分だけに ID が入る（ID パスは深度テストあり＝扉板や枠に隠れた所は書かれない）
	if (doorLightVisible_ && doorLight_) doorLight_->DrawIdPass(dxCore_);
}

void TitleScene::UpdateEnter() {
	// 扉が開く（ease-out）。開く量で中の光を強くする
	enterOpenRatio_ = EaseOutCubic(Progress(phaseTime_, enterOpenSeconds_));
	doorOpenDegrees_ = doorOpenTargetDegrees_ * enterOpenRatio_;

	// 少し遅れてカメラが扉の中へ前進する（ease-in で吸い込まれるように加速）
	const float move = Progress(phaseTime_ - enterMoveDelay_, enterMoveSeconds_);
	enterBlend_ = move * move * move;
	if (!enterMoveSoundPlayed_ && phaseTime_ >= enterMoveDelay_) {
		enterMoveSoundPlayed_ = true;
		SoundManager::GetInstance()->Play2DSound("se_title_in_door");
	}

	// BGM をフェードアウト（止めるのはシーンの Finalize）
	const float bgm = 1.0f - Progress(phaseTime_, enterBgmFadeSeconds_);
	SoundManager::GetInstance()->Set2DSoundVolume(kTitleBgm, bgmVolume_ * bgm);

	// カメラが光の部屋に届く前に真っ白になるよう、途中で白フェードを始める
	if (!enterTransitionStarted_ && phaseTime_ >= enterTransitionDelay_) {
		enterTransitionStarted_ = true;
		if (auto* fade = TransitionManager::GetInstance()->GetTransition<FadeTransition>(TransitionType::Fade)) {
			fade->SetNextFade({ 1.0f, 1.0f, 1.0f }, whiteFadeSeconds_, whiteHoldSeconds_);
		}
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
	}
}

void TitleScene::ApplyEnterLighting() {
	const bool active = phase_ == Phase::Enter && doorLightVisible_;
	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	const float depth = water_ ? water_->GetParams().depth : kWaterDepth;
	// 光源の中心：扉の正面から奥へ lightCenterDepth_ 入った所（扉の正面は +Z を doorYaw_ 回した向き）
	const Vector3 base{ orbitCenter_.x, waterHeight - depth, orbitCenter_.z };
	const Vector3 back = RotateYaw({ 0.0f, 0.0f, -lightCenterDepth_ }, doorYaw_);
	const Vector3 lightCenter{ base.x + back.x, base.y + lightCenterHeight_, base.z + back.z };

	if (auto* pe = Game::GetPostEffect(); pe && pe->lightShaft) {
		pe->lightShaft->SetEnabled(active);
		if (active) {
			// 画面 UV へ（反射やデバッグカメラではなく、ゲームカメラの見え方に合わせる）
			const Vector3 ndc = TransformCoordinate(lightCenter, gameViewProjection_);
			pe->lightShaft->SetLightPosition({ ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f });
			pe->lightShaft->SetTargetId(kDoorLightId);
			pe->lightShaft->SetColor(lightShaftColor_);
			pe->lightShaft->SetIntensity(lightShaftIntensity_ * enterOpenRatio_ * GetPostEffectFade());
		}
	}

	if (doorPointLight_ != kInvalidLightSlot) {
		if (auto* lm = LightManager::GetInstance()) {
			lm->SetPointLightPosition(doorPointLight_, lightCenter);
			lm->SetPointLightColor(doorPointLight_, doorPointLightColor_);
			lm->SetPointLightRadius(doorPointLight_, doorPointLightRadius_);
			lm->SetPointLightIntensity(doorPointLight_, active ? doorPointLightIntensity_ * enterOpenRatio_ : 0.0f);
		}
	}

	// 光の部屋はライティング無しの白（遅延ロードなので毎フレーム）
	if (ModelInstance* model = doorLight_ ? doorLight_->GetModelInstance() : nullptr) {
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (!sm.material) continue;
			sm.material->enableLighting = 0;
			sm.material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		}
	}
}

void TitleScene::ReleaseDoorPointLight() {
	if (doorPointLight_ == kInvalidLightSlot) return;
	if (auto* lm = LightManager::GetInstance()) lm->ReleasePointLight(doorPointLight_);
	doorPointLight_ = kInvalidLightSlot;
}

void TitleScene::SetDoorLightVisible(bool visible) {
	if (doorLightVisible_ == visible) return;
	doorLightVisible_ = visible;
	if (!waterReflection_ || !doorLight_) return;
	if (visible) {
		waterReflection_->AddTarget(doorLight_.get());
	} else {
		waterReflection_->RemoveTarget(doorLight_.get());
	}
}

void TitleScene::SetDoorLinesVisible(bool visible) {
	if (doorLinesVisible_ == visible) return;
	doorLinesVisible_ = visible;
	if (!waterReflection_) return;
	for (auto& line : doorLines_) {
		if (!line) continue;
		if (visible) {
			waterReflection_->AddTarget(line.get());
		} else {
			waterReflection_->RemoveTarget(line.get());
		}
	}
}

void TitleScene::SetSunYaw(float cameraStopAngle) {
	// 色味は「カメラが周回角 0 で止まり、扉が yaw=π」の配置で調整した。止まる角度が変わっても
	// 扉・カメラ・光の関係が同じになるよう、空の太陽の向きをその差だけ Y 軸まわりに回す。
	// 空・雲・水面の映り込みはそのまま（空に太陽の円盤は無いので食い違いは目立たない）
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(RotateYaw(kSunLightDirection, cameraStopAngle));
	}
}

void TitleScene::SetLogoVisible(bool visible) {
	if (logoVisible_ == visible) return;
	logoVisible_ = visible;
	if (!waterReflection_ || !logo_) return;
	if (visible) {
		waterReflection_->AddTarget(logo_.get());
	} else {
		waterReflection_->RemoveTarget(logo_.get());
	}
}

void TitleScene::SetDoorVisible(bool visible) {
	if (doorVisible_ == visible) return;
	doorVisible_ = visible;
	if (!waterReflection_) return;
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		if (!part) continue;
		if (visible) {
			waterReflection_->AddTarget(part);
		} else {
			waterReflection_->RemoveTarget(part);
		}
	}
}

float TitleScene::GetPostEffectFade() const {
	// トランジションの白い板はシーンと一緒に描かれ、その後でポストエフェクトが掛かる。
	// 真っ白になってもヴィネットで縁が暗く、ライトシャフトも乗ったままになり、HUB へ切り替わった瞬間に
	// それらが一気に消えて見えるので、白フェードに合わせて弱めて真っ白で 0 にする
	if (phase_ != Phase::Enter || !enterTransitionStarted_) return 1.0f;
	return 1.0f - SmoothStep01(Progress(phaseTime_ - enterTransitionDelay_, whiteFadeSeconds_));
}

void TitleScene::UpdatePostEffect() {
	auto* pe = Game::GetPostEffect();
	if (!pe) return;
	if (pe->vignette) pe->vignette->SetIntensity(vignetteIntensity_ * GetPostEffectFade());
}

void TitleScene::Draw() {
	auto* commandList = dxCore_->GetCommandList();

	// 水面に映す物を反射 RT へ（RT を切り替えるので、シーン RT のバインドより前に行う）
	// 通常はゲームカメラ基準。デバッグカメラ中もゲーム画面での映り込みが水面に焼き付くので、
	// 回り込んで確認できる（Debug で切り替えればデバッグカメラ基準の正しい反射になる）
	// 波のシミュレーションを進める（コンピュート。水面と反射を描く前に）
	if (water_) {
		water_->DispatchSimulation();
		// コンピュートでルートシグネチャが替わるので、以降の描画はそれぞれ自分で設定し直す
	}

	if (waterReflection_) {
		bool fromDebugCamera = false;
#ifdef _DEBUG
		fromDebugCamera = GetUseDebugCamera() && reflectFromDebugCamera_;
#endif
		if (fromDebugCamera) {
			waterReflection_->Render(*camera_);
		} else {
			waterReflection_->Render(gameViewProjection_, gameEyePosition_);
		}
	}

	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	if (skybox_) skybox_->Draw(dxCore_);

	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	if (doorVisible_) {
		if (doorFrame_) doorFrame_->Draw(dxCore_);
		if (doorLeafL_) doorLeafL_->Draw(dxCore_);
		if (doorLeafR_) doorLeafR_->Draw(dxCore_);
	}
	if (doorLightVisible_ && doorLight_) doorLight_->Draw(dxCore_);

	// 水面は不透明物の後（扉が水中からせり上がる間は、水面より下が隠れる）
	if (water_) water_->Draw(*camera_, kTitleSkyboxPath, waterReflection_.get());

	// ロゴは縁が、線画は消えるときに半透明になるので水面の後に重ねる。水面がルートシグネチャを替えるので貼り直す
	if ((logo_ && logoVisible_) || doorLinesVisible_) {
		object3DManager_->DrawSetting();
		LightManager::GetInstance()->BindLights(commandList);
		if (logo_ && logoVisible_) logo_->Draw(dxCore_);
		if (doorLinesVisible_) {
			for (auto& line : doorLines_) line->Draw(dxCore_);
		}
	}

	// エフェクトは加算・半透明なので不透明物と水面の後
	DrawGlobalEffects();

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	if (phase_ == Phase::Ready) {
		menu_.SetPosition({ screenW * 0.5f, screenH * menuHeightRatio_ });
		menu_.Draw();
	} else if (phase_ == Phase::Idle && elapsed_ >= introDuration_) {
		// ゆっくり明滅させて「押せる」ことを伝える
		const float blink = 0.35f + 0.65f * (0.5f + 0.5f * std::sin((elapsed_ - introDuration_) * 3.0f));
		const char* label = "PRESS ANY BUTTON";
		const float scale = 1.2f;
		const float w = tr->MeasureWidth(label, scale);
		tr->DrawText(label, { (screenW - w) * 0.5f, screenH * 0.72f }, scale,
			{ 1.0f, 1.0f, 1.0f, blink }, 2.0f, { 0.0f, 0.0f, 0.0f, blink });
	}
	tr->Flush();
}

void TitleScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Begin("Title Tuning")) {
		static const char* kPhaseNames[] = { "1 Idle", "2 LogoVanish", "3 DoorOutline", "4 DoorForm", "5 Ready", "6 Enter" };
		ImGui::Text("Phase: %s  (%.2f s)", kPhaseNames[static_cast<int>(phase_)], phaseTime_);
		if (ImGui::Button("Restart Sequence")) ResetSequence();
		ImGui::SameLine();
		if (ImGui::Button("Skip to Ready")) {
			// 演出を飛ばして ⑤ の状態にする（カメラは今の角度で止め、扉をそこへ向ける）
			SetLogoVisible(false);
			SetDoorVisible(true);
			SetDoorLinesVisible(false);
			doorOutlineIntensity_ = 0.0f;
			logoDissolve_ = 0.0f;
			framingBlend_ = 1.0f;
			orbitStopStartAngle_ = orbitStopEndAngle_ = orbitAngle_;
			doorYaw_ = orbitAngle_ + kPi;
			SetSunYaw(orbitAngle_);
			ChangePhase(Phase::Ready);
		}
		if (ImGui::CollapsingHeader("Sequence")) {
			ImGui::DragFloat("Logo Vanish (s)", &logoVanishSeconds_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Door Outline (s)", &doorOutlineSeconds_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Door Form (s)", &doorFormSeconds_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Enter Open (s)", &enterOpenSeconds_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Enter -> HUB Delay (s)", &enterTransitionDelay_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Burst Interval", &burstInterval_, 0.05f, 0.1f, 10.0f);
			ImGui::DragFloat("Burst Amplitude", &burstAmplitude_, 0.005f, 0.0f, 1.0f);
			ImGui::DragFloat("Burst Wavelength", &burstWavelength_, 0.05f, 0.1f, 10.0f);
			// 進み具合を手で動かして確認する（② / ④ の間は毎フレーム上書きされる）
			auto dissolveUi = [](const char* label, float& progress, DissolveStyle& style) {
				ImGui::PushID(label);
				ImGui::TextUnformatted(label);
				ImGui::SliderFloat("Progress", &progress, 0.0f, 1.0f);
				ImGui::DragFloat("Edge Width", &style.edgeWidth, 0.005f, 0.0f, 0.5f);
				ImGui::DragFloat("Noise Scale", &style.noiseScale, 0.05f, 0.05f, 20.0f);
				ImGui::SliderFloat("Noise Weight", &style.noiseWeight, 0.0f, 1.0f);
				ImGui::ColorEdit3("Edge Color", &style.edgeColor.x);
				ImGui::PopID();
			};
			dissolveUi("Door Dissolve", doorDissolve_, doorDissolveStyle_);
			ImGui::DragFloat("Door Dissolve Height", &doorDissolveHeight_, 0.05f, 0.1f, 10.0f);
			dissolveUi("Logo Dissolve", logoDissolve_, logoDissolveStyle_);
			dissolveUi("Door Line Dissolve", doorLineDissolve_, doorLineDissolveStyle_);
			bool linesVisible = doorLinesVisible_;
			if (ImGui::Checkbox("Door Lines Visible", &linesVisible)) SetDoorLinesVisible(linesVisible);
			ImGui::ColorEdit4("Door Line Color", &doorLineColor_.x);
			ImGui::ColorEdit4("Door Outline Color", &doorOutlineColor_.x);
			ImGui::SliderFloat("Door Outline Intensity", &doorOutlineIntensity_, 0.0f, 1.0f);
			ImGui::DragFloat("Door Outline Edge Strength", &doorOutlineEdgeStrength_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Outline Fade In (s)", &doorOutlineFadeInSeconds_, 0.05f, 0.0f, 5.0f);
			ImGui::DragFloat("Lines/Outline Fade Out (s)", &doorLineFadeSeconds_, 0.05f, 0.0f, 5.0f);
			ImGui::SliderFloat("Door Open Target (deg)", &doorOpenTargetDegrees_, 0.0f, 90.0f);
		}
		if (ImGui::CollapsingHeader("Mouse Ripple / Attract")) {
			// 波の伝わり方（速さ・減衰・範囲）は Water → Ripple Simulation (GPU)
			ImGui::DragFloat("Ripple Radius [m]", &mouseRippleRadius_, 0.01f, 0.02f, 3.0f);
			ImGui::DragFloat("Ripple Strength / m", &mouseRippleStrength_, 0.0005f, 0.0f, 0.2f, "%.4f");
			ImGui::DragFloat("Ripple Max Strength", &mouseRippleMaxStrength_, 0.0005f, 0.0f, 0.2f, "%.4f");
			ImGui::DragFloat("Ripple Spacing [m]", &mouseRippleSpacing_, 0.01f, 0.01f, 2.0f);
			ImGui::Separator();
			auto* attract = AttractMode::GetInstance();
			bool attractOn = attract->IsEnabled();
			if (ImGui::Checkbox("Attract Mode (F8)", &attractOn)) attract->SetEnabled(attractOn);
			ImGui::DragFloat("Title Idle Wait (s)", &attract->titleIdleWait, 0.1f, 0.0f, 30.0f);
			ImGui::DragFloat("Title Ready Wait (s)", &attract->titleReadyWait, 0.1f, 0.0f, 30.0f);
			ImGui::DragFloat("HUB Wait (s)", &attract->hubWait, 0.1f, 0.0f, 30.0f);
			ImGui::DragFloat("HUB Confirm Delay (s)", &attract->hubConfirmDelay, 0.05f, 0.0f, 10.0f);
		}
		if (ImGui::CollapsingHeader("Enter (6)")) {
			ImGui::DragFloat("Open (s)", &enterOpenSeconds_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Move Delay (s)", &enterMoveDelay_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("Move (s)", &enterMoveSeconds_, 0.05f, 0.1f, 10.0f);
			ImGui::DragFloat("White Fade Start (s)", &enterTransitionDelay_, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("White Fade (s)", &whiteFadeSeconds_, 0.05f, 0.05f, 5.0f);
			ImGui::DragFloat("White Hold (s)", &whiteHoldSeconds_, 0.05f, 0.0f, 5.0f);
			ImGui::DragFloat("BGM Fade (s)", &enterBgmFadeSeconds_, 0.05f, 0.05f, 10.0f);
			ImGui::SliderFloat("Light Shaft Intensity", &lightShaftIntensity_, 0.0f, 4.0f);
			ImGui::ColorEdit3("Light Shaft Color", &lightShaftColor_.x);
			ImGui::DragFloat("Light Center Height", &lightCenterHeight_, 0.05f, 0.0f, 4.0f);
			ImGui::DragFloat("Light Center Depth", &lightCenterDepth_, 0.05f, 0.0f, 3.0f);
			ImGui::DragFloat("Point Light Intensity", &doorPointLightIntensity_, 0.05f, 0.0f, 20.0f);
			ImGui::DragFloat("Point Light Radius", &doorPointLightRadius_, 0.1f, 0.1f, 30.0f);
			ImGui::ColorEdit3("Point Light Color", &doorPointLightColor_.x);
			ImGui::TextUnformatted("Enter Framing");
			ImGui::PushID("EnterFraming");
			ImGui::SliderAngle("FovY", &enterFraming_.fovY, 10.0f, 120.0f);
			ImGui::DragFloat("Radius", &enterFraming_.radius, 0.05f, 0.0f, 50.0f);
			ImGui::DragFloat("Camera Height", &enterFraming_.height, 0.02f, 0.05f, 10.0f);
			ImGui::DragFloat("Aim Height", &enterFraming_.aimHeight, 0.05f, -10.0f, 20.0f);
			ImGui::PopID();
			// 遷移せずに開く／光るところだけ確認する（White Fade Start を大きくしておく）
			bool lightVisible = doorLightVisible_;
			if (ImGui::Checkbox("Door Light Visible", &lightVisible)) SetDoorLightVisible(lightVisible);
			if (auto* pe = Game::GetPostEffect(); pe && pe->lightShaft) pe->lightShaft->ShowImGui();
		}
		ImGui::Separator();
		ImGui::DragFloat3("Orbit Center", &orbitCenter_.x, 0.1f);
		ImGui::DragFloat("Orbit Speed", &orbitSpeed_, 0.005f, -1.0f, 1.0f);
		ImGui::DragFloat("Orbit Angle", &orbitAngle_, 0.01f);
		if (ImGui::CollapsingHeader("Camera Framing", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 0=ロゴ用 / 1=扉用。④ の間は毎フレーム上書きされる（⑤ で止めて調整する）
			ImGui::SliderFloat("Framing Blend", &framingBlend_, 0.0f, 1.0f);
			auto framingUi = [](const char* label, CameraFraming& f) {
				ImGui::PushID(label);
				ImGui::TextUnformatted(label);
				ImGui::SliderAngle("FovY", &f.fovY, 10.0f, 100.0f);
				ImGui::DragFloat("Radius", &f.radius, 0.1f, 1.0f, 100.0f);
				ImGui::DragFloat("Camera Height", &f.height, 0.02f, 0.05f, 50.0f);
				ImGui::DragFloat("Aim Height", &f.aimHeight, 0.05f, -10.0f, 20.0f);
				ImGui::PopID();
			};
			framingUi("Logo Framing (1-3)", logoFraming_);
			framingUi("Door Framing (5-6)", doorFraming_);
		}
		ImGui::Separator();
		ImGui::DragFloat("Logo Height", &logoHeight_, 0.05f, -10.0f, 10.0f);
		ImGui::DragFloat("Logo Scale", &logoScale_, 0.05f, 0.1f, 20.0f);
		ImGui::DragFloat("Bob Amplitude", &logoBobAmplitude_, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Bob Speed", &logoBobSpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Sway Amplitude", &logoSwayAmplitude_, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Sway Speed", &logoSwaySpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::Separator();
		ImGui::DragFloat("Intro Duration", &introDuration_, 0.05f, 0.0f, 5.0f);
		ImGui::DragFloat("Vignette", &vignetteIntensity_, 0.01f, 0.0f, 2.0f);
		ImGui::SliderFloat("Menu Height", &menuHeightRatio_, 0.5f, 0.95f);
		if (ImGui::CollapsingHeader("Door", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool logoVisible = logoVisible_;
			if (ImGui::Checkbox("Logo Visible", &logoVisible)) SetLogoVisible(logoVisible);
			bool doorVisible = doorVisible_;
			if (ImGui::Checkbox("Door Visible", &doorVisible)) SetDoorVisible(doorVisible);
			ImGui::SliderAngle("Door Yaw", &doorYaw_, -180.0f, 180.0f);
			ImGui::SliderFloat("Door Open (deg)", &doorOpenDegrees_, 0.0f, 90.0f);
			if (ImGui::SliderFloat("Sun Intensity", &sunIntensity_, 0.0f, 8.0f)) {
				if (auto* lm = LightManager::GetInstance()) lm->SetDirectionalLightIntensity(sunIntensity_);
			}
			// マテリアルは UpdateDoor で毎フレーム反映される
			ImGui::SliderFloat("Marble Env (IBL)", &doorMarbleEnvCoefficient_, 0.0f, 1.5f);
			ImGui::SliderFloat("Gold Env (IBL)", &doorGold_.envCoefficient, 0.0f, 1.5f);
			ImGui::SliderFloat("Gold Metallic", &doorGold_.metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Gold Roughness", &doorGold_.roughness, 0.04f, 1.0f);
			ImGui::ColorEdit3("Gold Color", &doorGold_.color.x);
			ImGui::SliderFloat("Gold Cloud Reflect", &doorGold_.cloudReflection, 0.0f, 3.0f);
		}
		if (ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (GetUseDebugCamera()) {
				int source = reflectFromDebugCamera_ ? 1 : 0;
				const char* sources[] = { "Game Camera", "Debug Camera" };
				if (ImGui::Combo("Reflection Source", &source, sources, IM_ARRAYSIZE(sources))) {
					reflectFromDebugCamera_ = (source == 1);
				}
			}
			if (water_) water_->OnImGui();
			if (waterReflection_) waterReflection_->OnImGui();
		}
		if (ImGui::CollapsingHeader("Cloud")) {
			if (cloudLayer_) cloudLayer_->OnImGui();
		}
	}
	ImGui::End();
#endif
}

Camera* TitleScene::GetCamera() {
	return camera_.get();
}

void TitleScene::DrawShadowCasters() {
	GameScene::DrawShadowCasters();
	// ロゴは板ポリで、シャドウパスは透明部分を捨てられない（四角い影になる）ので影を落とさない。
	// 扉も同じ理由でディゾルブ中は影を出さず、実体化しきってから落とす
	if (doorVisible_ && doorDissolve_ >= 1.0f) {
		if (doorFrame_) doorFrame_->DrawShadowPass(dxCore_);
		if (doorLeafL_) doorLeafL_->DrawShadowPass(dxCore_);
		if (doorLeafR_) doorLeafR_->DrawShadowPass(dxCore_);
	}
}
