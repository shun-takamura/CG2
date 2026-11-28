#include "StagePlayScene.h"
#include "Score/GameSession.h"
#include "PepperMacros.h"
#include "Components/Gameplay.h"
#include "SoundManager.h"
#include "Physics/CollisionGeometry.h"
#include "Components/CollisionManager.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SpriteManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "Skybox.h"
#include "DirectXCore.h"
#include "PostEffect.h"
#include "MaskedGrayscaleEffect.h"
#include "Primitive/LineRenderer.h"
#include "Spline/SplineCurveActor.h"
#include "BossArenaWater.h"
#include "RiverWater.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_set>
#include "Components/EntityTag.h"
#include "AnimatedObject3DInstance.h"
#include "AnimatedModelInstance.h"
#include "Object3DInstance.h"
#include "Primitive/PrimitiveInstance.h"
#include "Primitive/PrimitivePipeline.h"
#include "Effect/LightningRuntime.h"
#include "Particle/GPUParticleManager.h"
#include "SpriteInstance.h"
#include "LightManager.h"
#include "StageStart/StageStartGeometry.h"
#include "StageStart/CloudTopIntro.h"
#include "StageStart/PlayerWingUnit.h"
#include "LogBuffer.h"
#include "SessionLogger.h"
#include "Components/SphereCollider.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "UI/Reticle.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyCommandFactory.h"
#include "Enemy/EnemyContext.h"
#include "Score/ScoreManager.h"
#include "Score/BossRetryState.h"
#include "Score/EtherState.h"
#include "Score/DifficultyProfile.h"
#include "Score/MainWeaponCatalog.h"
#include "Score/ShieldCatalog.h"
#include "Score/LootLedger.h"
#include "Score/StageClearRecord.h"
#include "Effect/BeamShieldRenderer.h"
#include "Effect/BeamBladeRenderer.h"
#include "ModelManager.h"
#include "TextRenderer.h"
#include "FontAtlas.h"
#include "Effect/EffectManager.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"

#include "MouseInput.h"
#ifdef _DEBUG
#include "ViewportWindow.h"
#endif
#include "MathUtility.h"
#include "WindowsApplication.h"
#include "UIPointer.h"
#include <algorithm>
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>
#include <format>
#include "Cloud/CloudRaymarcher.h"
#include "Profiling/LoadProfiler.h"
#include "ScenePreloader.h"
#include <shellapi.h> // CommandLineToArgvW（計測用の起動引数）
#include <cwchar>

#ifdef _DEBUG
#include "imgui.h"
#include "ImGuiManager.h"
#include "IImGuiEditable.h"
#include "Primitive/DebugDraw.h"
#endif

namespace {
	constexpr const char* kStagePlayTuningPath = "Resources/Json/Tuning/StagePlay.json";

	// 雲海の縦穴（降下の線）を拾う秒の範囲。急降下 idx17→23（51〜69 秒）の雲の層を上下にはみ出して覆う
	constexpr float kCloudShaftStartSec = 48.0f;
	constexpr float kCloudShaftEndSec = 66.0f;
	// 雲の流れの向き（自機の最初の進行方向）を取る：レールの 0 秒からこの秒までの向き
	constexpr float kCloudFlowDirectionSampleSec = 5.0f;
	// 箱の角の数
	constexpr int kBoxCornerCount = 8;
	// 角の番号のうち Y の向きを決める bit
	constexpr int kCornerBitY = 2;
	// 角の番号のうち Z の向きを決める bit
	constexpr int kCornerBitZ = 4;
	// 長さ・秒数・w がこれ以下なら 0 とみなす（0 割りを避ける）
	constexpr float kTinyEpsilon = 1e-4f;
	// JSON の配列を Vector2 として読むのに要る要素数
	constexpr size_t kVec2ComponentCount = 2;
	// JSON の配列を Vector3 として読むのに要る要素数
	constexpr size_t kVec3ComponentCount = 3;
	// JSON の配列を Vector4 として読むのに要る要素数
	constexpr size_t kVec4ComponentCount = 4;
	// 分身の数（上・右・下・左）。JSON の配列を読むループ用
	constexpr size_t kJdCloneCountSize = 4;
	// 分身の数（上・右・下・左）
	constexpr int kJdCloneCount = 4;
	// 調整値の JSON を書き出すときの字下げ [スペースの数]
	constexpr int kTuningJsonIndentWidth = 2;
	// 演出の強さがこれ以下なら掛けない
	constexpr float kVisibleEpsilon = 0.001f;
	// なめらかな補間 3t²−2t³ の t² の係数
	constexpr float kSmoothStepCubicCoeff = 3.0f;
	// アニメーションを最後のコマで止めるために入れる十分大きな時刻 [秒]
	constexpr float kAnimLastFrameTime = 1.0e3f;
	// 長さ・距離がこれより大きければ向きを求める
	constexpr float kNearZero = 1e-3f;
	// 追加回避の間、射撃禁止を延ばし続ける秒数 [秒]
	constexpr float kDodgeShootLockoutSeconds = 0.05f;
	// 被弾無敵の残り時間を無敵でない状態（0 以下）に戻すときの値の大きさ（−2）
	constexpr float kInvincibilityResetTimer = 2.0f;
	// 向きの長さがこれより小さければ代わりの向きを使う
	constexpr float kBasisEpsilon = 1e-5f;
	// ImGui：フェーズ表示の文字色 R
	constexpr float kPhaseTextR = 0.4f;
	// ImGui：フェーズ表示の文字色 G と B
	constexpr float kPhaseTextGB = 0.9f;
	// ImGui：クールタイム中の文字色 G（橙）
	constexpr float kCooldownTextG = 0.5f;
	// ImGui：クールタイム中の文字色 B（橙）
	constexpr float kCooldownTextB = 0.3f;
	// ImGui：発動できるときの文字色 R（緑）
	constexpr float kReadyTextR = 0.4f;
	// ImGui：発動できるときの文字色 B（緑）
	constexpr float kReadyTextB = 0.5f;
	// ImGui のテストで自機に与えるダメージ
	constexpr int kTestDamage = 10;
	// ImGui のテストで足すスコア
	constexpr int kTestScoreAdd = 1000;
	// 雷のテスト：始めと終わりの太さ [m]
	constexpr float kTestBoltWidth = 0.4f;
	// 雷のテスト：板の枚数
	constexpr int kTestBoltPlaneCount = 3;
	// 雷のテスト：両端を薄くする長さ [m]
	constexpr float kTestBoltFadeLength = 0.5f;
	// 雷のテスト：色 R
	constexpr float kTestBoltColorR = 0.6f;
	// 雷のテスト：色 G
	constexpr float kTestBoltColorG = 0.8f;
	// 雷のテスト：折れ曲がりを細かくする回数
	constexpr int kTestBoltGenerations = 5;
	// 雷のテスト：折れ曲がりのずれの上限（長さに対する割合）
	constexpr float kTestBoltMaxOffsetRatio = 0.15f;
	// 雷のテスト：枝分かれする確率
	constexpr float kTestBoltBranchProbability = 0.15f;
	// 雷のテスト：枝の長さの倍率
	constexpr float kTestBoltBranchLengthScale = 0.25f;
	// 雷のテスト：カメラの前方の終点までの距離 [m]
	constexpr float kTestBoltDistance = 30.0f;
	// カメラの初めの位置を原点から手前（−Z）へ離す距離 [m]
	constexpr float kInitialCameraBackZ = 20.0f;
	// BGM の音量（0〜1）
	constexpr float kBgmVolume = 0.5f;
	// ポーズ中の暗幕の濃さ
	constexpr float kPauseDimAlpha = 0.55f;
	// 仮の刃の太さの倍率（X・Y）
	constexpr float kTempBladeThickness = 0.3f;
	// 仮の刃の長さの倍率（Z）
	constexpr float kTempBladeLength = 6.0f;
	// 仮の刃を手から前へずらす量 [m]
	constexpr float kTempBladeOffsetZ = 1.5f;
	// 待機中のカメラの揺れ：pitch の速さ
	constexpr float kIdleSwayFreqPitch = 0.83f;
	// 待機中のカメラの揺れ：yaw の速さ
	constexpr float kIdleSwayFreqYaw = 0.61f;
	// 待機中のカメラの揺れ：yaw の位相のずれ [rad]
	constexpr float kIdleSwayPhaseYaw = 1.3f;
	// 待機中のカメラの揺れ：roll の速さ
	constexpr float kIdleSwayFreqRoll = 0.47f;
	// 待機中のカメラの揺れ：roll の位相のずれ [rad]
	constexpr float kIdleSwayPhaseRoll = 2.1f;
	// 待機中のカメラの揺れ：上下の速さ
	constexpr float kIdleSwayFreqLift = 1.07f;
	// 待機中のカメラの揺れ：上下の位相のずれ [rad]
	constexpr float kIdleSwayPhaseLift = 0.7f;
	// ボスの HP バーの下地の色 R と G
	constexpr float kBossHpBackRG = 0.05f;
	// ボスの HP バーの下地の色 B
	constexpr float kBossHpBackB = 0.08f;
	// ボスの HP バーの下地の不透明度
	constexpr float kBossHpBackAlpha = 0.85f;
	// ボスの HP バーの遅れて減るゲージの色 G
	constexpr float kBossHpDelayG = 0.95f;
	// ボスの HP バーの遅れて減るゲージの色 B
	constexpr float kBossHpDelayB = 0.85f;
	// ボスの HP バーのゲージの色 R（赤）
	constexpr float kBossHpFillR = 0.95f;
	// ボスの HP バーのゲージの色 G（赤）
	constexpr float kBossHpFillG = 0.25f;
	// ボスの HP バーのゲージの色 B（赤）
	constexpr float kBossHpFillB = 0.2f;
	// ボスの HP バーの発狂の目印の不透明度
	constexpr float kBossHpRageMarkAlpha = 0.9f;
	// 発狂後のゲージの色 R（赤紫）
	constexpr float kBossHpRageFillR = 0.9f;
	// 発狂後のゲージの色 G（赤紫）
	constexpr float kBossHpRageFillG = 0.1f;
	// 発狂後のゲージの色 B（赤紫）
	constexpr float kBossHpRageFillB = 0.6f;
	// 発狂の目印の幅 [px]
	constexpr float kBossHpRageMarkWidth = 2.0f;
	// ボスの HP バーの上端から「BOSS」まで [px]
	constexpr float kBossLabelOffsetY = 34.0f;
	// 「BOSS」の文字の大きさ
	constexpr float kBossLabelScale = 0.9f;
	// 文字の縁の太さ [px]
	constexpr float kTextOutline = 2.0f;
	// ポーズメニューの縦の位置（画面の高さに対する割合）
	constexpr float kPauseMenuHeightRatio = 0.58f;
	// ポーズメニューの項目：リトライ
	constexpr int kPauseItemRetry = 2;
	// ポーズメニューの項目：ステージセレクトに戻る
	constexpr int kPauseItemStageSelect = 3;
	// ポーズメニューの項目：タイトルに戻る
	constexpr int kPauseItemTitle = 4;
	// 「PAUSE」の文字の大きさ
	constexpr float kPauseHeadingScale = 2.5f;
	// 確認の見出しの文字の大きさ
	constexpr float kConfirmHeadingScale = 1.6f;
	// ポーズの見出しの縦の位置（画面の高さに対する割合）
	constexpr float kPauseHeadingHeightRatio = 0.28f;
	// ポーズの見出しの縁の太さ [px]
	constexpr float kPauseHeadingOutline = 3.0f;
	// 確認の見出しから注意書きまで [px]
	constexpr float kConfirmNoteOffsetY = 70.0f;
	// 確認の注意書きの灰色の明るさ
	constexpr float kConfirmNoteGray = 0.85f;
	// フレーム時間をこの fps の 1 フレームまでに切り詰める（ロード直後に飛ばないように）
	constexpr float kMinDeltaTimeFps = 30.0f;
	// World のフレーム時間がこれ以下ならゲームが止まっているとみなす [秒]
	constexpr float kFrozenDeltaTime = 0.0001f;
	// 撃破の後の小さな爆発の効果音の音量
	constexpr float kExplodeSeVolume = 0.6f;
	// 長さ（の 2 乗）がこれ以下なら向きを求めない
	constexpr float kSquaredEpsilon = 1e-6f;
	// Debug：銃口の印（十字）の大きさ [m]
	constexpr float kMuzzleDebugCrossSize = 0.15f;
	// 近接派生中のクリップのマージン（実質制限なし）
	constexpr float kMeleeClipMarginFree = 10.0f;
	// 今の位置を含むマージンに足す余裕・押し戻す先をマージンより内側にする量（NDC）
	constexpr float kClipMarginSlack = 0.05f;
	// カメラの上方向の Y がこれより大きいときだけ床まで持ち上げる
	constexpr float kMinCameraUpY = 0.1f;
	// チャージの 2 段目のレベル
	constexpr float kChargeLevel2 = 2.0f;
	// 連射間隔の指定が無いときの間隔 [秒]
	constexpr float kDefaultFireInterval = 0.12f;
	// スコアの表示の上限（5 桁で頭打ち）
	constexpr int kMaxDisplayScore = 99999;
	// 雲海の縦穴のためにレールの降下区間を拾う点の数
	constexpr int kCloudShaftSampleCount = 16;
	// 雲海の縦穴の点の最後の番号（点の数 −1）
	constexpr float kCloudShaftSampleLastIndex = 15.0f;
	// ダメージの指定が無い（0 以下の）ときのダメージ
	constexpr int kDefaultContactDamage = 10;
	// ゲージの上限がこれ以下なら比率を 0 にする
	constexpr float kGaugeMaxEpsilon = 0.0001f;
	// 体の楕円の半径がこれより小さければ指定が無いとみなす [m]
	constexpr float kMinEllipseRadius = 0.01f;
	// 体の楕円の横の半径の代わりの値 [m]
	constexpr float kFallbackEllipseHorizontal = 0.5f;
	// 体の楕円の縦の半径を横の何倍にするか（代わりの値）
	constexpr float kFallbackEllipseAspect = 1.8f;

	/// <summary>起動引数 "--key 値" の値を返す。計測用（tools/Python/run_cloud_bench.py）</summary>
	/// <param name="key">引数の名前（"--bench-hold" など）</param>
	/// <returns>続く値（無ければ空）</returns>
	std::wstring FindCommandLineValue(const wchar_t* key) {
		std::wstring value;
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (!argv) return value;
		for (int i = 1; i + 1 < argc; ++i) {
			if (std::wcscmp(argv[i], key) == 0) {
				value = argv[i + 1];
				break;
			}
		}
		::LocalFree(argv);
		return value;
	}

	/// <summary>コライダーを囲む箱の半幅（ローカル軸。球・OBB・カプセルの外殻）</summary>
	/// <param name="col">コライダー</param>
	/// <returns>半幅（軸ごと）</returns>
	Vector3 ColliderHalfExtents(const Collider& col) {
		switch (col.shape) {
		case ColliderShape::Sphere:  return { col.radius, col.radius, col.radius };
		case ColliderShape::OBB:     return col.halfExtents;
		case ColliderShape::Capsule: return { col.capsuleRadius, 0.5f * col.capsuleHeight + col.capsuleRadius, col.capsuleRadius };
		}
		return { 0.0f, 0.0f, 0.0f };
	}

	/// <summary>中心 center・軸 axes・半幅 he の箱の 8 つの角を vp で投影し、NDC の |x| / |y| の最大を求める</summary>
	/// <param name="center">箱の中心（ワールド）</param>
	/// <param name="axes">箱の軸 3 本（ワールド・正規化）</param>
	/// <param name="he">軸ごとの半幅</param>
	/// <param name="vp">ビュー×プロジェクション行列</param>
	/// <param name="outMaxX">NDC の |x| の最大</param>
	/// <param name="outMaxY">NDC の |y| の最大</param>
	/// <returns>求めたら true。カメラの後ろに回った角があれば false（画面に収まっているとは言えない）</returns>
	bool ColliderMaxAbsNdc(const Vector3& center, const Vector3 axes[3], const Vector3& he, const Matrix4x4& vp,
		float& outMaxX, float& outMaxY) {
		outMaxX = 0.0f;
		outMaxY = 0.0f;
		for (int i = 0; i < kBoxCornerCount; ++i) {
			const float sx = (i & 1) ? +1.0f : -1.0f;
			const float sy = (i & kCornerBitY) ? +1.0f : -1.0f;
			const float sz = (i & kCornerBitZ) ? +1.0f : -1.0f;
			const Vector3 c{
				center.x + axes[0].x * he.x * sx + axes[1].x * he.y * sy + axes[2].x * he.z * sz,
				center.y + axes[0].y * he.x * sx + axes[1].y * he.y * sy + axes[2].y * he.z * sz,
				center.z + axes[0].z * he.x * sx + axes[1].z * he.y * sy + axes[2].z * he.z * sz,
			};
			const float wx = c.x * vp.m[0][0] + c.y * vp.m[1][0] + c.z * vp.m[2][0] + vp.m[3][0];
			const float wy = c.x * vp.m[0][1] + c.y * vp.m[1][1] + c.z * vp.m[2][1] + vp.m[3][1];
			const float ww = c.x * vp.m[0][3] + c.y * vp.m[1][3] + c.z * vp.m[2][3] + vp.m[3][3];
			if (ww <= kTinyEpsilon) return false;
			outMaxX = (std::max)(outMaxX, std::fabs(wx / ww));
			outMaxY = (std::max)(outMaxY, std::fabs(wy / ww));
		}
		return true;
	}
}

StagePlayScene::StagePlayScene() = default;
StagePlayScene::~StagePlayScene() = default;

void StagePlayScene::LoadTuningFromJson() {
	if (!std::filesystem::exists(kStagePlayTuningPath)) return;
	auto result = JsonParser::ParseFile(kStagePlayTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;

	const JsonValue& off = root["player"]["localOffset"];
	if (off.IsArray() && off.Size() >= kVec3ComponentCount) {
		playerLocalOffset_ = {
			static_cast<float>(off[0].AsDouble(playerLocalOffset_.x)),
			static_cast<float>(off[1].AsDouble(playerLocalOffset_.y)),
			static_cast<float>(off[2].AsDouble(playerLocalOffset_.z)),
		};
	}
	// レールカメラのチューニング（speed/rotKeys）は RailStagePart 側で読む（キー名は不変）。
	if (railStage_) railStage_->LoadFromJson(root);
	// 環境セクション（root["sections"]）は StageEnvironment 側で読む。
	if (stageEnv_) stageEnv_->LoadFromJson(root);
	// クリップ面（RailStagePart と同じ root["camera"] に相乗り。キーは別なので競合しない）
	cameraNearClip_ = static_cast<float>(root["camera"]["nearClip"].AsDouble(cameraNearClip_));
	cameraFarClip_  = static_cast<float>(root["camera"]["farClip"].AsDouble(cameraFarClip_));

	playerSmoothTime_ = static_cast<float>(
		root["player"]["smoothTime"].AsDouble(playerSmoothTime_));
	attackMoveSpeedScale_ = static_cast<float>(
		root["player"]["attackMoveSpeedScale"].AsDouble(attackMoveSpeedScale_));
	moveStateDelay_ = static_cast<float>(
		root["player"]["moveStateDelay"].AsDouble(moveStateDelay_));
	moveStateLeanDeg_ = static_cast<float>(
		root["player"]["moveStateLeanDeg"].AsDouble(moveStateLeanDeg_));
	if (const JsonValue& look = root["player"]["look"]; look.IsObject()) {
		playerLook_.environmentCoefficient = static_cast<float>(look["environmentCoefficient"].AsDouble(playerLook_.environmentCoefficient));
		playerLook_.overrideMaterial = look["overrideMaterial"].AsBool(playerLook_.overrideMaterial);
		playerLook_.metallic = static_cast<float>(look["metallic"].AsDouble(playerLook_.metallic));
		playerLook_.roughness = static_cast<float>(look["roughness"].AsDouble(playerLook_.roughness));
		playerLook_.baseGlow = static_cast<float>(look["baseGlow"].AsDouble(playerLook_.baseGlow));
	}
	SetEnemyShotLeadRate(static_cast<float>(
		root["enemyShot"]["leadRate"].AsDouble(GetEnemyShotLeadRate())));

	// ----- skybox -----
	const JsonValue& sky = root["skybox"];
	if (sky.IsObject()) {
		const JsonValue& tint = sky["tint"];
		if (tint.IsArray() && tint.Size() >= kVec4ComponentCount) {
			skyboxTint_ = {
				static_cast<float>(tint[0].AsDouble(skyboxTint_.x)),
				static_cast<float>(tint[1].AsDouble(skyboxTint_.y)),
				static_cast<float>(tint[2].AsDouble(skyboxTint_.z)),
				static_cast<float>(tint[3].AsDouble(skyboxTint_.w)),
			};
		}
		const JsonValue& dark = sky["specialDarkColor"];
		if (dark.IsArray() && dark.Size() >= kVec4ComponentCount) {
			specialSkyboxDarkColor_ = {
				static_cast<float>(dark[0].AsDouble(specialSkyboxDarkColor_.x)),
				static_cast<float>(dark[1].AsDouble(specialSkyboxDarkColor_.y)),
				static_cast<float>(dark[2].AsDouble(specialSkyboxDarkColor_.z)),
				static_cast<float>(dark[3].AsDouble(specialSkyboxDarkColor_.w)),
			};
		}
		specialSkyboxFadeIn_     = static_cast<float>(sky["specialFadeIn"].AsDouble(specialSkyboxFadeIn_));
		specialSkyboxFadeOut_    = static_cast<float>(sky["specialFadeOut"].AsDouble(specialSkyboxFadeOut_));
		bossSkyboxBlendDuration_ = static_cast<float>(sky["bossBlendDuration"].AsDouble(bossSkyboxBlendDuration_));
		if (sky["defaultPath"].IsString()) defaultSkyboxPath_ = sky["defaultPath"].AsString();
		if (sky["bossPath"].IsString())    bossSkyboxPath_    = sky["bossPath"].AsString();
	}

	const JsonValue& spd = root["player"]["moveSpeed"];
	if (spd.IsArray() && spd.Size() >= kVec2ComponentCount) {
		playerMoveSpeed_.x = static_cast<float>(spd[0].AsDouble(playerMoveSpeed_.x));
		playerMoveSpeed_.y = static_cast<float>(spd[1].AsDouble(playerMoveSpeed_.y));
	}
	const JsonValue& mar = root["player"]["clipMargin"];
	if (mar.IsArray() && mar.Size() >= kVec2ComponentCount) {
		playerClipMargin_.x = static_cast<float>(mar[0].AsDouble(playerClipMargin_.x));
		playerClipMargin_.y = static_cast<float>(mar[1].AsDouble(playerClipMargin_.y));
	}


	// ----- aim -----
	const JsonValue& aim = root["aim"];
	if (aim.IsObject()) {
		aimPlaneDistance_     = static_cast<float>(aim["planeDistance"].AsDouble(aimPlaneDistance_));
		aimSmoothTime_        = static_cast<float>(aim["smoothTime"].AsDouble(aimSmoothTime_));
		aimAssistPixelScale_  = static_cast<float>(aim["assistPixelScale"].AsDouble(aimAssistPixelScale_));
		aimReticleOverlapRate_ = static_cast<float>(aim["reticleOverlapRate"].AsDouble(aimReticleOverlapRate_));
		aimConvergePx_        = static_cast<float>(aim["convergePx"].AsDouble(aimConvergePx_));
		reticleOuterMinPx_    = static_cast<float>(aim["reticleOuterMinPx"].AsDouble(reticleOuterMinPx_));
		reticleOuterMaxPx_    = static_cast<float>(aim["reticleOuterMaxPx"].AsDouble(reticleOuterMaxPx_));
		reticleOuterSizeMinPx_ = static_cast<float>(aim["reticleOuterSizeMinPx"].AsDouble(reticleOuterSizeMinPx_));
		reticleOuterSizeMaxPx_ = static_cast<float>(aim["reticleOuterSizeMaxPx"].AsDouble(reticleOuterSizeMaxPx_));
	}

	// ----- camera idle sway -----
	const JsonValue& sway = root["idleSway"];
	if (sway.IsObject()) {
		idleSwayEnabled_ = sway["enabled"].AsBool(idleSwayEnabled_);
		const JsonValue& ra = sway["rotAmp"];
		if (ra.IsArray() && ra.Size() >= kVec3ComponentCount) {
			idleSwayRotAmp_ = {
				static_cast<float>(ra[0].AsDouble(idleSwayRotAmp_.x)),
				static_cast<float>(ra[1].AsDouble(idleSwayRotAmp_.y)),
				static_cast<float>(ra[2].AsDouble(idleSwayRotAmp_.z)),
			};
		}
		idleSwayPosAmp_ = static_cast<float>(sway["posAmp"].AsDouble(idleSwayPosAmp_));
		idleSwaySpeed_  = static_cast<float>(sway["speed"].AsDouble(idleSwaySpeed_));
	}

	// ----- damage / invincibility -----
	const JsonValue& dmg = root["damage"];
	if (dmg.IsObject()) {
		playerInvincibilityDuration_  = static_cast<float>(dmg["invincibilityDuration"].AsDouble(playerInvincibilityDuration_));
		shootLockoutDuration_         = static_cast<float>(dmg["shootLockoutDuration"].AsDouble(shootLockoutDuration_));
		damageBlinkFrequency_         = static_cast<float>(dmg["blinkFrequency"].AsDouble(damageBlinkFrequency_));
		damageBlinkAlpha_             = static_cast<float>(dmg["blinkAlpha"].AsDouble(damageBlinkAlpha_));
		auto readGlow = [](const JsonValue& a, Vector3& v) {
			if (!a.IsArray() || a.Size() < kVec3ComponentCount) return;
			v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)), static_cast<float>(a[2].AsDouble(v.z)) };
		};
		readGlow(dmg["blinkEmissive"], damageBlinkEmissive_);
		readGlow(dmg["dodgeBlinkEmissive"], dodgeBlinkEmissive_);
		damageCameraShakeIntensity_   = static_cast<float>(dmg["cameraShakeIntensity"].AsDouble(damageCameraShakeIntensity_));
		damageCameraShakeDuration_    = static_cast<float>(dmg["cameraShakeDuration"].AsDouble(damageCameraShakeDuration_));
	}

	// ----- dodge（回避）-----
	const JsonValue& dodge = root["dodge"];
	if (dodge.IsObject()) {
		dodgeJustWindow_     = static_cast<float>(dodge["justWindow"].AsDouble(dodgeJustWindow_));
		dodgeIFrameDuration_ = static_cast<float>(dodge["iframeDuration"].AsDouble(dodgeIFrameDuration_));
		dodgeCooldown_       = static_cast<float>(dodge["cooldown"].AsDouble(dodgeCooldown_));
		dodgeActionLock_     = static_cast<float>(dodge["actionLock"].AsDouble(dodgeActionLock_));
		dodgeImpulse_.x      = static_cast<float>(dodge["impulseX"].AsDouble(dodgeImpulse_.x));
		dodgeImpulse_.y      = static_cast<float>(dodge["impulseY"].AsDouble(dodgeImpulse_.y));
	}

	// ----- melee（近接：生成高さ・サイズ倍率）-----
	const JsonValue& melee = root["melee"];
	if (melee.IsObject()) {
		meleeOriginHeight_  = static_cast<float>(melee["originHeight"].AsDouble(meleeOriginHeight_));
		meleeBossSizeScale_ = static_cast<float>(melee["bossSizeScale"].AsDouble(meleeBossSizeScale_));
	}

	// ----- justDodge（ジャスト回避スロー）-----
	const JsonValue& jd = root["justDodge"];
	if (jd.IsObject()) {
		justDodgeSlowWorld_     = static_cast<float>(jd["slowWorld"].AsDouble(justDodgeSlowWorld_));
		justDodgeReceiptWindow_ = static_cast<float>(jd["receiptWindow"].AsDouble(justDodgeReceiptWindow_));
		justDodgeFadeIn_        = static_cast<float>(jd["fadeIn"].AsDouble(justDodgeFadeIn_));
		justDodgeFadeOut_       = static_cast<float>(jd["fadeOut"].AsDouble(justDodgeFadeOut_));
		justDodgeScore_         = static_cast<int>(jd["score"].AsInt(static_cast<int64_t>(justDodgeScore_)));
		jdCloneOffset_     = static_cast<float>(jd["cloneOffset"].AsDouble(jdCloneOffset_));
		jdCamPullback_     = static_cast<float>(jd["camPullback"].AsDouble(jdCamPullback_));
		jdCamFovAdd_       = static_cast<float>(jd["camFovAdd"].AsDouble(jdCamFovAdd_));
		jdSelectThreshold_ = static_cast<float>(jd["selectThreshold"].AsDouble(jdSelectThreshold_));
		jdSpreadDuration_  = static_cast<float>(jd["spreadDuration"].AsDouble(jdSpreadDuration_));
		jdMergeDuration_   = static_cast<float>(jd["mergeDuration"].AsDouble(jdMergeDuration_));
		jdMeleeApproachDuration_ = static_cast<float>(jd["meleeApproachDuration"].AsDouble(jdMeleeApproachDuration_));
		jdMeleeReturnDuration_   = static_cast<float>(jd["meleeReturnDuration"].AsDouble(jdMeleeReturnDuration_));
		jdMeleeApproachDist_     = static_cast<float>(jd["meleeApproachDist"].AsDouble(jdMeleeApproachDist_));
		jdDodgeReturnDuration_   = static_cast<float>(jd["dodgeReturnDuration"].AsDouble(jdDodgeReturnDuration_));
		bossFacingTurnSmoothTime_ = static_cast<float>(jd["bossFacingTurnSmooth"].AsDouble(bossFacingTurnSmoothTime_));
		jdBossWarpDelay_        = static_cast<float>(jd["bossWarpDelay"].AsDouble(jdBossWarpDelay_));
		jdBossCamSwingDuration_ = static_cast<float>(jd["bossCamSwingDuration"].AsDouble(jdBossCamSwingDuration_));
		{
			const JsonValue& em = jd["dodgeExpandedMargin"];
			if (em.IsArray() && em.Size() >= kVec2ComponentCount) {
				jdDodgeExpandedMargin_.x = static_cast<float>(em[0].AsDouble(jdDodgeExpandedMargin_.x));
				jdDodgeExpandedMargin_.y = static_cast<float>(em[1].AsDouble(jdDodgeExpandedMargin_.y));
			}
			const JsonValue& mc = jd["meleeCamOffset"];
			if (mc.IsArray() && mc.Size() >= kVec3ComponentCount) {
				jdMeleeCameraOffset_ = {
					static_cast<float>(mc[0].AsDouble(jdMeleeCameraOffset_.x)),
					static_cast<float>(mc[1].AsDouble(jdMeleeCameraOffset_.y)),
					static_cast<float>(mc[2].AsDouble(jdMeleeCameraOffset_.z)),
				};
			}
			const JsonValue& ml = jd["meleeCamLookOffset"];
			if (ml.IsArray() && ml.Size() >= kVec3ComponentCount) {
				jdMeleeCameraLookOffset_ = {
					static_cast<float>(ml[0].AsDouble(jdMeleeCameraLookOffset_.x)),
					static_cast<float>(ml[1].AsDouble(jdMeleeCameraLookOffset_.y)),
					static_cast<float>(ml[2].AsDouble(jdMeleeCameraLookOffset_.z)),
				};
			}
		}
		const JsonValue& cpArr = jd["clonePaths"];
		if (cpArr.IsArray()) {
			for (size_t i = 0; i < cpArr.Size() && i < kJdCloneCountSize; ++i) {
				jdClonePath_[i] = cpArr[i].AsString(jdClonePath_[i]);
			}
		}
		const JsonValue& cc = jd["cloneColor"];
		if (cc.IsArray() && cc.Size() >= kVec4ComponentCount) {
			jdCloneColor_.x = static_cast<float>(cc[0].AsDouble(jdCloneColor_.x));
			jdCloneColor_.y = static_cast<float>(cc[1].AsDouble(jdCloneColor_.y));
			jdCloneColor_.z = static_cast<float>(cc[2].AsDouble(jdCloneColor_.z));
			jdCloneColor_.w = static_cast<float>(cc[3].AsDouble(jdCloneColor_.w));
		}
	}

	// ----- heal（回復）-----
	const JsonValue& heal = root["heal"];
	if (heal.IsObject()) {
		healAmount_       = static_cast<int>(heal["amount"].AsInt(static_cast<int64_t>(healAmount_)));
		healSmallAmount_  = static_cast<int>(heal["smallAmount"].AsInt(static_cast<int64_t>(healSmallAmount_)));
		healMaxStg_       = static_cast<int>(heal["maxStg"].AsInt(static_cast<int64_t>(healMaxStg_)));
		healMaxBoss_      = static_cast<int>(heal["maxBoss"].AsInt(static_cast<int64_t>(healMaxBoss_)));
	}
	// ----- special gauge -----
	const JsonValue& sg = root["special"];
	if (sg.IsObject()) {
		specialGaugeMaxStormLock_   = static_cast<float>(sg["gaugeMaxStormLock"].AsDouble(specialGaugeMaxStormLock_));
		specialGaugeMaxShatterline_ = static_cast<float>(sg["gaugeMaxShatterline"].AsDouble(specialGaugeMaxShatterline_));
		specialCooldown_          = static_cast<float>(sg["cooldown"].AsDouble(specialCooldown_));
		equippedSpecial_          = (sg["equipped"].AsInt(static_cast<int64_t>(equippedSpecial_)) == 1)
			? SpecialKind::Shatterline : SpecialKind::StormLock;
		// 装備中種別の実効上限をミラー
		specialGaugeMax_       = SpecialGaugeMaxFor(equippedSpecial_);
		shatterline_.LoadTuning(sg);
		dodgeSpecialGaugeGain_ = static_cast<float>(sg["dodgeGain"].AsDouble(dodgeSpecialGaugeGain_));
		meleeLateGaugeRate_    = static_cast<float>(sg["meleeLateRate"].AsDouble(meleeLateGaugeRate_));
		stormLock_.LoadTuning(sg);
		// 両方の必殺技で使う自機の体の中心と、体を囲む楕円
		specialPlayerCenterOffset_ = static_cast<float>(sg["centerOffset"].AsDouble(specialPlayerCenterOffset_));
		specialBodyEllipseH_ = static_cast<float>(sg["bodyEllipseH"].AsDouble(specialBodyEllipseH_));
		specialBodyEllipseV_ = static_cast<float>(sg["bodyEllipseV"].AsDouble(specialBodyEllipseV_));
		// ゲージ UI
		const JsonValue& bar = sg["bar"];
		if (bar.IsObject()) {
			gaugeBarMaxWidth_ = static_cast<float>(bar["maxWidth"].AsDouble(gaugeBarMaxWidth_));
			gaugeBarHeight_   = static_cast<float>(bar["height"].AsDouble(gaugeBarHeight_));
			gaugeBarPosX_     = static_cast<float>(bar["posX"].AsDouble(gaugeBarPosX_));
			gaugeBarPosY_     = static_cast<float>(bar["posY"].AsDouble(gaugeBarPosY_));
			const JsonValue& bgC = bar["bgColor"];
			if (bgC.IsArray() && bgC.Size() >= kVec4ComponentCount) {
				gaugeBarBgColor_ = {
					static_cast<float>(bgC[0].AsDouble(gaugeBarBgColor_.x)),
					static_cast<float>(bgC[1].AsDouble(gaugeBarBgColor_.y)),
					static_cast<float>(bgC[2].AsDouble(gaugeBarBgColor_.z)),
					static_cast<float>(bgC[3].AsDouble(gaugeBarBgColor_.w))
				};
			}
			const JsonValue& fgC = bar["fgColor"];
			if (fgC.IsArray() && fgC.Size() >= kVec4ComponentCount) {
				gaugeBarFgColor_ = {
					static_cast<float>(fgC[0].AsDouble(gaugeBarFgColor_.x)),
					static_cast<float>(fgC[1].AsDouble(gaugeBarFgColor_.y)),
					static_cast<float>(fgC[2].AsDouble(gaugeBarFgColor_.z)),
					static_cast<float>(fgC[3].AsDouble(gaugeBarFgColor_.w))
				};
			}
			const JsonValue& fuC = bar["fullColor"];
			if (fuC.IsArray() && fuC.Size() >= kVec4ComponentCount) {
				gaugeBarFullColor_ = {
					static_cast<float>(fuC[0].AsDouble(gaugeBarFullColor_.x)),
					static_cast<float>(fuC[1].AsDouble(gaugeBarFullColor_.y)),
					static_cast<float>(fuC[2].AsDouble(gaugeBarFullColor_.z)),
					static_cast<float>(fuC[3].AsDouble(gaugeBarFullColor_.w))
				};
			}
		}
	}

	// ----- HP bar UI -----
	const JsonValue& hpb = root["hpBar"];
	if (hpb.IsObject()) {
		hpBarMaxWidth_  = static_cast<float>(hpb["maxWidth"].AsDouble(hpBarMaxWidth_));
		hpBarHeight_    = static_cast<float>(hpb["height"].AsDouble(hpBarHeight_));
		hpBarLerpSpeed_ = static_cast<float>(hpb["lerpSpeed"].AsDouble(hpBarLerpSpeed_));
		hpBarPosX_      = static_cast<float>(hpb["posX"].AsDouble(hpBarPosX_));
		hpBarPosY_      = static_cast<float>(hpb["posY"].AsDouble(hpBarPosY_));
	}

	// ----- Score UI -----
	auto readColor = [](const JsonValue& arr, Vector4& dst) {
		if (arr.IsArray() && arr.Size() >= kVec4ComponentCount) {
			dst.x = static_cast<float>(arr[0].AsDouble(dst.x));
			dst.y = static_cast<float>(arr[1].AsDouble(dst.y));
			dst.z = static_cast<float>(arr[2].AsDouble(dst.z));
			dst.w = static_cast<float>(arr[3].AsDouble(dst.w));
		}
	};
	const JsonValue& sc = root["score"];
	if (sc.IsObject()) {
		const JsonValue& lb = sc["label"];
		if (lb.IsObject()) {
			scoreLabelScale_   = static_cast<float>(lb["scale"].AsDouble(scoreLabelScale_));
			scoreLabelOffsetX_ = static_cast<float>(lb["offsetX"].AsDouble(scoreLabelOffsetX_));
			scoreLabelOffsetY_ = static_cast<float>(lb["offsetY"].AsDouble(scoreLabelOffsetY_));
			readColor(lb["color"], scoreLabelColor_);
			scoreLabelOutlineThickness_ = static_cast<float>(lb["outlineThickness"].AsDouble(scoreLabelOutlineThickness_));
			readColor(lb["outlineColor"], scoreLabelOutlineColor_);
		}
		const JsonValue& nb = sc["number"];
		if (nb.IsObject()) {
			scoreNumberScale_   = static_cast<float>(nb["scale"].AsDouble(scoreNumberScale_));
			scoreNumberOffsetX_ = static_cast<float>(nb["offsetX"].AsDouble(scoreNumberOffsetX_));
			scoreNumberOffsetY_ = static_cast<float>(nb["offsetY"].AsDouble(scoreNumberOffsetY_));
			readColor(nb["color"], scoreNumberColor_);
			scoreNumberOutlineThickness_ = static_cast<float>(nb["outlineThickness"].AsDouble(scoreNumberOutlineThickness_));
			readColor(nb["outlineColor"], scoreNumberOutlineColor_);
		}
	}

	// ----- reticle (チャージアニメーション) -----
	const JsonValue& ret = root["reticle"];
	if (ret.IsObject()) {
		outerChargeStartRadius_     = static_cast<float>(ret["outerChargeStartRadius"].AsDouble(outerChargeStartRadius_));
		outerChargeEndRadius_       = static_cast<float>(ret["outerChargeEndRadius"].AsDouble(outerChargeEndRadius_));
		outerChargeEasingDuration_  = static_cast<float>(ret["outerChargeEasingDuration"].AsDouble(outerChargeEasingDuration_));
		outerRotationSpeed_         = static_cast<float>(ret["outerRotationSpeed"].AsDouble(outerRotationSpeed_));
	}

	// ----- precision aim (精密射撃モード) -----
	const JsonValue& pa = root["precision"];
	if (pa.IsObject()) {
		precisionFovY_            = static_cast<float>(pa["fovY"].AsDouble(precisionFovY_));
		const JsonValue& co = pa["camOffset"];
		if (co.IsArray() && co.Size() >= kVec3ComponentCount) {
			precisionCamOffset_ = {
				static_cast<float>(co[0].AsDouble(precisionCamOffset_.x)),
				static_cast<float>(co[1].AsDouble(precisionCamOffset_.y)),
				static_cast<float>(co[2].AsDouble(precisionCamOffset_.z)),
			};
		}
		precisionFadeSpeed_       = static_cast<float>(pa["fadeSpeed"].AsDouble(precisionFadeSpeed_));
		precisionStickScale_      = static_cast<float>(pa["stickScale"].AsDouble(precisionStickScale_));
		precisionVignette_        = static_cast<float>(pa["vignette"].AsDouble(precisionVignette_));
		precisionBlurIntensity_   = static_cast<float>(pa["blurIntensity"].AsDouble(precisionBlurIntensity_));
		precisionBlurInnerRadius_ = static_cast<float>(pa["blurInnerRadius"].AsDouble(precisionBlurInnerRadius_));
		precisionBlurFalloff_     = static_cast<float>(pa["blurFalloff"].AsDouble(precisionBlurFalloff_));
		precisionBlurMaxPx_       = static_cast<float>(pa["blurMaxPx"].AsDouble(precisionBlurMaxPx_));
	}

	// スタート演出（16_StageStart.md）
	if (stageStart_) stageStart_->LoadTuning(root["stageStart"]);
	if (cloudTopIntro_) cloudTopIntro_->LoadTuning(root["cloudTopIntro"]);
	if (playerWings_) playerWings_->LoadTuning(root["playerWings"]);
	playerThrusters_.LoadTuning(root["playerThrusters"]);
	// 死亡演出（19_GameOverAndDifficulty.md §1）
	if (playerDeath_) playerDeath_->LoadTuning(root["playerDeath"]);

	// ----- phase（ステージ進行ステートマシン）-----
	const JsonValue& ph = root["phase"];
	if (ph.IsObject()) {
		seekMaxSec_      = static_cast<float>(ph["seekMaxSec"].AsDouble(seekMaxSec_));
		landingDuration_ = static_cast<float>(ph["landingDurationSec"].AsDouble(landingDuration_));
	}

	// ----- streaming（区間ごとのストリーミング。11 番 L9）-----
	if (streaming_) streaming_->LoadTuning(root["streaming"]);
}

void StagePlayScene::SaveTuningToJson() const {
	JsonValue root = JsonValue::MakeObject();

	JsonValue offArr = JsonValue::MakeArray();
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.x)));
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.y)));
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.z)));
	JsonValue spdArr = JsonValue::MakeArray();
	spdArr.Push(JsonValue(static_cast<double>(playerMoveSpeed_.x)));
	spdArr.Push(JsonValue(static_cast<double>(playerMoveSpeed_.y)));
	JsonValue marArr = JsonValue::MakeArray();
	marArr.Push(JsonValue(static_cast<double>(playerClipMargin_.x)));
	marArr.Push(JsonValue(static_cast<double>(playerClipMargin_.y)));
	JsonValue playerObj = JsonValue::MakeObject();
	playerObj["localOffset"] = std::move(offArr);
	playerObj["moveSpeed"]   = std::move(spdArr);
	playerObj["clipMargin"]  = std::move(marArr);
	playerObj["smoothTime"]  = static_cast<double>(playerSmoothTime_);
	playerObj["attackMoveSpeedScale"] = static_cast<double>(attackMoveSpeedScale_);
	playerObj["moveStateDelay"] = static_cast<double>(moveStateDelay_);
	playerObj["moveStateLeanDeg"] = static_cast<double>(moveStateLeanDeg_);
	{
		JsonValue look = JsonValue::MakeObject();
		look["environmentCoefficient"] = static_cast<double>(playerLook_.environmentCoefficient);
		look["overrideMaterial"] = JsonValue(playerLook_.overrideMaterial);
		look["metallic"] = static_cast<double>(playerLook_.metallic);
		look["roughness"] = static_cast<double>(playerLook_.roughness);
		look["baseGlow"] = static_cast<double>(playerLook_.baseGlow);
		playerObj["look"] = std::move(look);
	}
	root["player"] = std::move(playerObj);
	JsonValue enemyShotObj = JsonValue::MakeObject();
	enemyShotObj["leadRate"] = static_cast<double>(GetEnemyShotLeadRate());
	root["enemyShot"] = std::move(enemyShotObj);

	// レールカメラのチューニング（speed/rotKeys）は RailStagePart 側で書く（キー名は不変）。
	if (railStage_) railStage_->SaveToJson(root);
	// RailStagePart::SaveToJson は root["camera"] を丸ごと差し替えるので、必ずその後に書く。
	root["camera"]["nearClip"] = static_cast<double>(cameraNearClip_);
	root["camera"]["farClip"]  = static_cast<double>(cameraFarClip_);
	if (stageEnv_) stageEnv_->SaveToJson(root);

	JsonValue skyObj = JsonValue::MakeObject();
	{
		JsonValue tint = JsonValue::MakeArray();
		tint.Push(JsonValue(static_cast<double>(skyboxTint_.x)));
		tint.Push(JsonValue(static_cast<double>(skyboxTint_.y)));
		tint.Push(JsonValue(static_cast<double>(skyboxTint_.z)));
		tint.Push(JsonValue(static_cast<double>(skyboxTint_.w)));
		skyObj["tint"] = std::move(tint);

		JsonValue dark = JsonValue::MakeArray();
		dark.Push(JsonValue(static_cast<double>(specialSkyboxDarkColor_.x)));
		dark.Push(JsonValue(static_cast<double>(specialSkyboxDarkColor_.y)));
		dark.Push(JsonValue(static_cast<double>(specialSkyboxDarkColor_.z)));
		dark.Push(JsonValue(static_cast<double>(specialSkyboxDarkColor_.w)));
		skyObj["specialDarkColor"] = std::move(dark);

		skyObj["specialFadeIn"]     = static_cast<double>(specialSkyboxFadeIn_);
		skyObj["specialFadeOut"]    = static_cast<double>(specialSkyboxFadeOut_);
		skyObj["bossBlendDuration"] = static_cast<double>(bossSkyboxBlendDuration_);
		skyObj["defaultPath"]       = JsonValue(defaultSkyboxPath_);
		skyObj["bossPath"]          = JsonValue(bossSkyboxPath_);
	}
	root["skybox"] = std::move(skyObj);

	JsonValue aimObj = JsonValue::MakeObject();
	aimObj["planeDistance"]      = static_cast<double>(aimPlaneDistance_);
	aimObj["smoothTime"]         = static_cast<double>(aimSmoothTime_);
	aimObj["assistPixelScale"]   = static_cast<double>(aimAssistPixelScale_);
	aimObj["reticleOverlapRate"] = static_cast<double>(aimReticleOverlapRate_);
	aimObj["convergePx"]         = static_cast<double>(aimConvergePx_);
	aimObj["reticleOuterMinPx"]  = static_cast<double>(reticle_ ? reticle_->GetLockOnMinPxOutside() : reticleOuterMinPx_);
	aimObj["reticleOuterMaxPx"]  = static_cast<double>(reticle_ ? reticle_->GetLockOnMaxPxOutside() : reticleOuterMaxPx_);
	aimObj["reticleOuterSizeMinPx"] = static_cast<double>(reticle_ ? reticle_->GetOuterSizeMin() : reticleOuterSizeMinPx_);
	aimObj["reticleOuterSizeMaxPx"] = static_cast<double>(reticle_ ? reticle_->GetOuterSizeMax() : reticleOuterSizeMaxPx_);
	root["aim"] = std::move(aimObj);

	JsonValue swayObj = JsonValue::MakeObject();
	swayObj["enabled"] = idleSwayEnabled_;
	JsonValue swayRot = JsonValue::MakeArray();
	swayRot.Push(JsonValue(static_cast<double>(idleSwayRotAmp_.x)));
	swayRot.Push(JsonValue(static_cast<double>(idleSwayRotAmp_.y)));
	swayRot.Push(JsonValue(static_cast<double>(idleSwayRotAmp_.z)));
	swayObj["rotAmp"] = std::move(swayRot);
	swayObj["posAmp"] = static_cast<double>(idleSwayPosAmp_);
	swayObj["speed"]  = static_cast<double>(idleSwaySpeed_);
	root["idleSway"] = std::move(swayObj);

	JsonValue dmgObj = JsonValue::MakeObject();
	dmgObj["invincibilityDuration"] = static_cast<double>(playerInvincibilityDuration_);
	dmgObj["shootLockoutDuration"]  = static_cast<double>(shootLockoutDuration_);
	dmgObj["blinkFrequency"]        = static_cast<double>(damageBlinkFrequency_);
	dmgObj["blinkAlpha"]            = static_cast<double>(damageBlinkAlpha_);
	auto writeGlow = [](const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	};
	dmgObj["blinkEmissive"]         = writeGlow(damageBlinkEmissive_);
	dmgObj["dodgeBlinkEmissive"]    = writeGlow(dodgeBlinkEmissive_);
	dmgObj["cameraShakeIntensity"]  = static_cast<double>(damageCameraShakeIntensity_);
	dmgObj["cameraShakeDuration"]   = static_cast<double>(damageCameraShakeDuration_);
	root["damage"] = std::move(dmgObj);

	JsonValue dodgeObj = JsonValue::MakeObject();
	dodgeObj["justWindow"]     = static_cast<double>(dodgeJustWindow_);
	dodgeObj["iframeDuration"] = static_cast<double>(dodgeIFrameDuration_);
	dodgeObj["cooldown"]       = static_cast<double>(dodgeCooldown_);
	dodgeObj["actionLock"]     = static_cast<double>(dodgeActionLock_);
	dodgeObj["impulseX"]       = static_cast<double>(dodgeImpulse_.x);
	dodgeObj["impulseY"]       = static_cast<double>(dodgeImpulse_.y);
	root["dodge"] = std::move(dodgeObj);

	JsonValue meleeObj = JsonValue::MakeObject();
	meleeObj["originHeight"]  = static_cast<double>(meleeOriginHeight_);
	meleeObj["bossSizeScale"] = static_cast<double>(meleeBossSizeScale_);
	root["melee"] = std::move(meleeObj);

	JsonValue jdObj = JsonValue::MakeObject();
	jdObj["slowWorld"]     = static_cast<double>(justDodgeSlowWorld_);
	jdObj["receiptWindow"] = static_cast<double>(justDodgeReceiptWindow_);
	jdObj["fadeIn"]        = static_cast<double>(justDodgeFadeIn_);
	jdObj["fadeOut"]       = static_cast<double>(justDodgeFadeOut_);
	jdObj["score"]         = static_cast<int64_t>(justDodgeScore_);
	jdObj["cloneOffset"]     = static_cast<double>(jdCloneOffset_);
	jdObj["camPullback"]     = static_cast<double>(jdCamPullback_);
	jdObj["camFovAdd"]       = static_cast<double>(jdCamFovAdd_);
	jdObj["selectThreshold"] = static_cast<double>(jdSelectThreshold_);
	jdObj["spreadDuration"]  = static_cast<double>(jdSpreadDuration_);
	jdObj["mergeDuration"]   = static_cast<double>(jdMergeDuration_);
	jdObj["meleeApproachDuration"] = static_cast<double>(jdMeleeApproachDuration_);
	jdObj["meleeReturnDuration"]   = static_cast<double>(jdMeleeReturnDuration_);
	jdObj["meleeApproachDist"]     = static_cast<double>(jdMeleeApproachDist_);
	jdObj["dodgeReturnDuration"]   = static_cast<double>(jdDodgeReturnDuration_);
	jdObj["bossFacingTurnSmooth"]  = static_cast<double>(bossFacingTurnSmoothTime_);
	jdObj["bossWarpDelay"]         = static_cast<double>(jdBossWarpDelay_);
	jdObj["bossCamSwingDuration"]  = static_cast<double>(jdBossCamSwingDuration_);
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(jdDodgeExpandedMargin_.x)));
		arr.Push(JsonValue(static_cast<double>(jdDodgeExpandedMargin_.y)));
		jdObj["dodgeExpandedMargin"] = std::move(arr);
	}
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraOffset_.x)));
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraOffset_.y)));
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraOffset_.z)));
		jdObj["meleeCamOffset"] = std::move(arr);
	}
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraLookOffset_.x)));
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraLookOffset_.y)));
		arr.Push(JsonValue(static_cast<double>(jdMeleeCameraLookOffset_.z)));
		jdObj["meleeCamLookOffset"] = std::move(arr);
	}
	{
		JsonValue cpArr = JsonValue::MakeArray();
		for (int i = 0; i < kJdCloneCount; ++i) cpArr.Push(JsonValue(jdClonePath_[i]));
		jdObj["clonePaths"] = std::move(cpArr);
	}
	{
		JsonValue ccArr = JsonValue::MakeArray();
		ccArr.Push(JsonValue(static_cast<double>(jdCloneColor_.x)));
		ccArr.Push(JsonValue(static_cast<double>(jdCloneColor_.y)));
		ccArr.Push(JsonValue(static_cast<double>(jdCloneColor_.z)));
		ccArr.Push(JsonValue(static_cast<double>(jdCloneColor_.w)));
		jdObj["cloneColor"] = std::move(ccArr);
	}
	root["justDodge"] = std::move(jdObj);

	JsonValue healObj = JsonValue::MakeObject();
	healObj["amount"]      = static_cast<int64_t>(healAmount_);
	healObj["smallAmount"] = static_cast<int64_t>(healSmallAmount_);
	healObj["maxStg"]      = static_cast<int64_t>(healMaxStg_);
	healObj["maxBoss"]     = static_cast<int64_t>(healMaxBoss_);
	root["heal"] = std::move(healObj);

	JsonValue sgObj = JsonValue::MakeObject();
	sgObj["gaugeMaxStormLock"]   = static_cast<double>(specialGaugeMaxStormLock_);
	sgObj["gaugeMaxShatterline"] = static_cast<double>(specialGaugeMaxShatterline_);
	sgObj["cooldown"]          = static_cast<double>(specialCooldown_);
	sgObj["equipped"]          = static_cast<int64_t>(equippedSpecial_);
	shatterline_.SaveTuning(sgObj);
	sgObj["dodgeGain"] = static_cast<double>(dodgeSpecialGaugeGain_);
	sgObj["meleeLateRate"]   = static_cast<double>(meleeLateGaugeRate_);
	stormLock_.SaveTuning(sgObj);
	sgObj["centerOffset"] = static_cast<double>(specialPlayerCenterOffset_);
	sgObj["bodyEllipseH"] = static_cast<double>(specialBodyEllipseH_);
	sgObj["bodyEllipseV"] = static_cast<double>(specialBodyEllipseV_);
	// ゲージ UI
	JsonValue barObj = JsonValue::MakeObject();
	barObj["maxWidth"] = static_cast<double>(gaugeBarMaxWidth_);
	barObj["height"]   = static_cast<double>(gaugeBarHeight_);
	barObj["posX"]     = static_cast<double>(gaugeBarPosX_);
	barObj["posY"]     = static_cast<double>(gaugeBarPosY_);
	auto pushV4 = [](const Vector4& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		arr.Push(JsonValue(static_cast<double>(v.w)));
		return arr;
	};
	barObj["bgColor"]   = pushV4(gaugeBarBgColor_);
	barObj["fgColor"]   = pushV4(gaugeBarFgColor_);
	barObj["fullColor"] = pushV4(gaugeBarFullColor_);
	sgObj["bar"] = std::move(barObj);
	root["special"] = std::move(sgObj);

	JsonValue hpbObj = JsonValue::MakeObject();
	hpbObj["maxWidth"]  = static_cast<double>(hpBarMaxWidth_);
	hpbObj["height"]    = static_cast<double>(hpBarHeight_);
	hpbObj["lerpSpeed"] = static_cast<double>(hpBarLerpSpeed_);
	hpbObj["posX"]      = static_cast<double>(hpBarPosX_);
	hpbObj["posY"]      = static_cast<double>(hpBarPosY_);
	root["hpBar"] = std::move(hpbObj);

	auto colorToArr = [](const Vector4& c) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(c.x)));
		a.Push(JsonValue(static_cast<double>(c.y)));
		a.Push(JsonValue(static_cast<double>(c.z)));
		a.Push(JsonValue(static_cast<double>(c.w)));
		return a;
	};
	JsonValue scObj = JsonValue::MakeObject();
	JsonValue lbObj = JsonValue::MakeObject();
	lbObj["scale"]            = static_cast<double>(scoreLabelScale_);
	lbObj["offsetX"]          = static_cast<double>(scoreLabelOffsetX_);
	lbObj["offsetY"]          = static_cast<double>(scoreLabelOffsetY_);
	lbObj["color"]            = colorToArr(scoreLabelColor_);
	lbObj["outlineThickness"] = static_cast<double>(scoreLabelOutlineThickness_);
	lbObj["outlineColor"]     = colorToArr(scoreLabelOutlineColor_);
	scObj["label"]   = std::move(lbObj);
	JsonValue nbObj = JsonValue::MakeObject();
	nbObj["scale"]            = static_cast<double>(scoreNumberScale_);
	nbObj["offsetX"]          = static_cast<double>(scoreNumberOffsetX_);
	nbObj["offsetY"]          = static_cast<double>(scoreNumberOffsetY_);
	nbObj["color"]            = colorToArr(scoreNumberColor_);
	nbObj["outlineThickness"] = static_cast<double>(scoreNumberOutlineThickness_);
	nbObj["outlineColor"]     = colorToArr(scoreNumberOutlineColor_);
	scObj["number"]  = std::move(nbObj);
	root["score"] = std::move(scObj);

	// ----- reticle (チャージアニメーション) -----
	JsonValue retObj = JsonValue::MakeObject();
	retObj["outerChargeStartRadius"]    = static_cast<double>(outerChargeStartRadius_);
	retObj["outerChargeEndRadius"]      = static_cast<double>(outerChargeEndRadius_);
	retObj["outerChargeEasingDuration"] = static_cast<double>(outerChargeEasingDuration_);
	retObj["outerRotationSpeed"]        = static_cast<double>(outerRotationSpeed_);
	root["reticle"] = std::move(retObj);

	// ----- precision aim (精密射撃モード) -----
	JsonValue paObj = JsonValue::MakeObject();
	paObj["fovY"]            = static_cast<double>(precisionFovY_);
	JsonValue camOffArr = JsonValue::MakeArray();
	camOffArr.Push(JsonValue(static_cast<double>(precisionCamOffset_.x)));
	camOffArr.Push(JsonValue(static_cast<double>(precisionCamOffset_.y)));
	camOffArr.Push(JsonValue(static_cast<double>(precisionCamOffset_.z)));
	paObj["camOffset"]       = std::move(camOffArr);
	paObj["fadeSpeed"]       = static_cast<double>(precisionFadeSpeed_);
	paObj["stickScale"]      = static_cast<double>(precisionStickScale_);
	paObj["vignette"]        = static_cast<double>(precisionVignette_);
	paObj["blurIntensity"]   = static_cast<double>(precisionBlurIntensity_);
	paObj["blurInnerRadius"] = static_cast<double>(precisionBlurInnerRadius_);
	paObj["blurFalloff"]     = static_cast<double>(precisionBlurFalloff_);
	paObj["blurMaxPx"]       = static_cast<double>(precisionBlurMaxPx_);
	root["precision"] = std::move(paObj);

	// ----- phase（ステージ進行ステートマシン）-----
	JsonValue phObj = JsonValue::MakeObject();
	phObj["seekMaxSec"]         = static_cast<double>(seekMaxSec_);
	phObj["landingDurationSec"] = static_cast<double>(landingDuration_);
	root["phase"] = std::move(phObj);

	// ----- streaming（区間ごとのストリーミング。11 番 L9）-----
	if (streaming_) streaming_->SaveTuning(root["streaming"]);

	if (stageStart_) stageStart_->SaveTuning(root["stageStart"]);
	if (cloudTopIntro_) cloudTopIntro_->SaveTuning(root["cloudTopIntro"]);
	if (playerWings_) playerWings_->SaveTuning(root["playerWings"]);
	playerThrusters_.SaveTuning(root["playerThrusters"]);
	if (playerDeath_) playerDeath_->SaveTuning(root["playerDeath"]);

	std::filesystem::path p(kStagePlayTuningPath);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
	}
	JsonWriter::WriteFile(kStagePlayTuningPath, root, { true, kTuningJsonIndentWidth });
}

void StagePlayScene::PlayJustDodgeEffect(IImGuiEditable* targetEnemy, float duration)
{
	justDodgeActive_ = true;
	justDodgeTimer_ = 0.0f;
	justDodgeDuration_ = duration;
	justDodgeTarget_ = targetEnemy;
	justDodgeFadeOutTimer_ = -1.0f;

	// プレイヤー + 対象敵をハイライト登録
	ClearHighlights();
	if (player_) AddHighlight(player_);
	if (targetEnemy) AddHighlight(targetEnemy);

	// PostEffect の MaskedGrayscale を有効化（intensity は Update で制御）
	if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
		pe->maskedGrayscale->SetEnabled(true);
		pe->maskedGrayscale->SetIntensity(0.0f);
		pe->maskedGrayscale->UpdateConstantBuffer();
	}
}

void StagePlayScene::TriggerJustDodge(IImGuiEditable* attacker)
{
	// 演出：プレイヤー＋攻撃元をハイライトしてグレースケール。受付期間ぶん保持する。
	PlayJustDodgeEffect(attacker, justDodgeReceiptWindow_);
	SoundManager::GetInstance()->Play2DSound("se_just_dodge");
	// World だけスロー（Player/UI は等速）。終了は UpdateJustDodgeEffect が元に戻す。
	SetTimeScale(TimeGroup::World, justDodgeSlowWorld_);
	// （ストック制廃止：左派生＝小回復・無制限、デフォルトHeal＝大回復・固定回数制に再設計）
	// スコア加点
	GetGameSession()->GetScoreManager()->AddScore(justDodgeScore_);
	// 機動系の飛行ユニット：ジャスト回避で必殺技ゲージを足す（20 番 §3.3）
	if (flightUnit_) specialGauge_ = (std::min)(specialGaugeMax_, specialGauge_ + flightUnit_->GetJustDodgeGaugeBonus());

	// 分身カウンター：4方向に分身プレビューを出し、方向入力での確定待ちに入る。
	// （上=近接強 / 右=近接弱 / 下=追加回避 / 左=回復。各アクションは Phase2 で実装）
	jdCounterTarget_        = attacker;
	jdChosen_               = CounterDir::None;
	justDodgeCounterActive_ = false; // 派生が確定するまでは false（無入力なら受付期限で自然終了）
	jdMerging_              = false;
	// 分身プレビューは STG／ボス戦どちらでも出す。
	// ボス戦では配置基底を地上（XZ射影）に切り替え（UpdateJustDodgeClones 内で phase 分岐）、
	// 各派生の「実効果」（近接詰め寄り／追加回避ダッシュ）は次イテレーションで実装する。
	// 現状ボス戦での選択確定は TriggerCloneCounterAction 側で「UIを閉じるだけ」に留める
	// （実効果を進行させると誰も EndJustDodgeCounterAction を呼ばずワールド停止で固まるため）。
	jdSelecting_ = true;
	SpawnJustDodgeClones();
}

void StagePlayScene::ApplyJustDodgeCamera(const Vector3& playerWorldPos)
{
	(void)playerWorldPos; // Phase3 の端寄せ構図補正で使用予定
	if (!camera_) return;
	const float b = jdEffectIntensity_;
	if (b <= kVisibleEpsilon) return;

	// 精密カメラ適用後の現在 eye を基準に、forward 逆方向へ引く（後退＝視野が広がる）。
	// clip 判定は引き前の通常カメラで済んでいるので、本体の可動範囲は変わらない（端固まり防止）。
	const Vector3 eye = camera_->GetTranslate();
	const Matrix4x4 rot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 forward = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };
	camera_->SetTranslate({
		eye.x - forward.x * jdCamPullback_ * b,
		eye.y - forward.y * jdCamPullback_ * b,
		eye.z - forward.z * jdCamPullback_ * b,
	});
	camera_->SetFovY(camera_->GetFovY() + jdCamFovAdd_ * b);
	camera_->Update();
}

void StagePlayScene::ToSphericalAroundPivot(const Vector3& p, const Vector3& pivot, float& outYaw, float& outPitch, float& outRadius) const
{
	Vector3 d{ p.x - pivot.x, p.y - pivot.y, p.z - pivot.z };
	outRadius = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (outRadius < kTinyEpsilon) { outYaw = 0.0f; outPitch = 0.0f; return; }
	outYaw   = std::atan2(d.x, d.z);
	outPitch = std::asin(std::clamp(d.y / outRadius, -1.0f, 1.0f));
}

Vector3 StagePlayScene::FromSphericalAroundPivot(const Vector3& pivot, float yaw, float pitch, float radius) const
{
	const float cp = std::cos(pitch);
	return {
		pivot.x + radius * std::sin(yaw) * cp,
		pivot.y + radius * std::sin(pitch),
		pivot.z + radius * std::cos(yaw) * cp,
	};
}

void StagePlayScene::ApplyJustDodgeMeleeCamera(const Vector3& playerWorldPos)
{
	if (!camera_ || !jdMeleeCameraActive_) return;
	// ボス戦：ワープ前（詰め寄り待機中）はまだ通常の自由/ロックオンカメラに任せる。
	// ワープした瞬間から jdBossMeleeCamActive_ が true になり、以降はこの関数が乗っ取る。
	if (phase_ == Phase::Boss && !jdBossMeleeCamActive_) return;

	// プレイヤー→対象敵を forward に取って、その基底（right/up/forward）で
	// jdMeleeCameraOffset_ ぶんプレイヤーから離れた位置にカメラを置く。
	// 注視点は (player+target)/2 + jdMeleeCameraLookOffset_。
	Vector3 target = playerWorldPos;
	if (IImGuiEditable* focus = CounterFocusEntity()) {
		if (Vector3* tp = focus->GetEditableTranslate()) target = *tp;
	}
	// ボス戦は自機が地面から攻撃するので、高さをそろえて水平の基底で斜め上から撮る
	// （ボスの中心の高さへ向けると見上げる基底になり、ほぼ地面しか映らなかった）
	if (phase_ == Phase::Boss) target.y = playerWorldPos.y;

	Vector3 fwd{ target.x - playerWorldPos.x, target.y - playerWorldPos.y, target.z - playerWorldPos.z };
	float flen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
	if (flen < kTinyEpsilon) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.y /= flen; fwd.z /= flen;

	const Vector3 worldUp{ 0.0f, 1.0f, 0.0f };
	Vector3 rgt{
		worldUp.y * fwd.z - worldUp.z * fwd.y,
		worldUp.z * fwd.x - worldUp.x * fwd.z,
		worldUp.x * fwd.y - worldUp.y * fwd.x,
	};
	float rlen = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
	if (rlen < kTinyEpsilon) { rgt = { 1.0f, 0.0f, 0.0f }; rlen = 1.0f; }
	rgt.x /= rlen; rgt.y /= rlen; rgt.z /= rlen;
	Vector3 up{
		fwd.y * rgt.z - fwd.z * rgt.y,
		fwd.z * rgt.x - fwd.x * rgt.z,
		fwd.x * rgt.y - fwd.y * rgt.x,
	};

	const Vector3& off = jdMeleeCameraOffset_;
	Vector3 eye{
		playerWorldPos.x + rgt.x * off.x + up.x * off.y + fwd.x * off.z,
		playerWorldPos.y + rgt.y * off.x + up.y * off.y + fwd.y * off.z,
		playerWorldPos.z + rgt.z * off.x + up.z * off.y + fwd.z * off.z,
	};

	// ボス戦：ワープ直後は対象（ボス）中心の球面座標で最短ヨーを通しながら最終フレーミングへ寄せる。
	// プレイヤーはワープ済みで player→target 方向（fwd）はもう最終値に固定されているため、
	// ここで補間するのは eye の位置だけでよい（look は下で mid ベースに計算し直され、常に対象へ収束する）。
	if (phase_ == Phase::Boss && jdBossCamSwingDuration_ > kTinyEpsilon) {
		float targetYaw, targetPitch, targetRadius;
		ToSphericalAroundPivot(eye, target, targetYaw, targetPitch, targetRadius);

		float t = jdBossCamSwingTimer_ / jdBossCamSwingDuration_;
		if (t < 1.0f) {
			const float e = t * t * (kSmoothStepCubicCoeff - 2.0f * t); // smoothstep
			float dyaw = targetYaw - jdBossCamSwingStartYaw_;
			while (dyaw >  kPi) dyaw -= kTwoPi;
			while (dyaw < -kPi) dyaw += kTwoPi;
			const float yawB    = jdBossCamSwingStartYaw_    + dyaw * e;
			const float pitchB  = jdBossCamSwingStartPitch_  + (targetPitch  - jdBossCamSwingStartPitch_)  * e;
			const float radiusB = jdBossCamSwingStartRadius_ + (targetRadius - jdBossCamSwingStartRadius_) * e;
			eye = FromSphericalAroundPivot(target, yawB, pitchB, radiusB);
		}
	}

	// 注視点 = 中点 + lookOffset（基底空間で）
	Vector3 mid{
		(playerWorldPos.x + target.x) * 0.5f,
		(playerWorldPos.y + target.y) * 0.5f,
		(playerWorldPos.z + target.z) * 0.5f,
	};
	const Vector3& lo = jdMeleeCameraLookOffset_;
	Vector3 look{
		mid.x + rgt.x * lo.x + up.x * lo.y + fwd.x * lo.z,
		mid.y + rgt.y * lo.x + up.y * lo.y + fwd.y * lo.z,
		mid.z + rgt.z * lo.x + up.z * lo.y + fwd.z * lo.z,
	};

	// look ←→ eye から yaw/pitch を逆算
	Vector3 dir{ look.x - eye.x, look.y - eye.y, look.z - eye.z };
	float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (dlen < kTinyEpsilon) return;
	dir.x /= dlen; dir.y /= dlen; dir.z /= dlen;
	float yaw   = std::atan2(dir.x, dir.z);
	float pitch = -std::asin(dir.y);

	camera_->SetTranslate(eye);
	camera_->SetRotate({ pitch, yaw, 0.0f });
	camera_->Update();
}

void StagePlayScene::SpawnJustDodgeClones()
{
	ClearJustDodgeClones();
	if (!player_ || !camera_) return;

	// 分身は Object3D（.mesh）。各方向ごとにモデルパスを持ち、あとで派生モーション初期ポーズの
	// .mesh に差し替えやすくしてある。位置は UpdateJustDodgeClones が毎フレーム本体へ追従させる。
	const Vector3 ppos = player_->GetTranslate();
	jdSpreadTimer_ = 0.0f; // 中心→各方向へ分裂するアニメをやり直し
	jdClones_.assign(kJdCloneCount, nullptr); // index: 0=Up,1=Right,2=Down,3=Left
	for (int i = 0; i < kJdCloneCount; ++i) {
		// パスを「dir/file」に分割（AddDynamicObject の引数仕様）。
		// ローダ側で dir + "/" + filename を行うため、dir 末尾の '/' は付けない（二重スラッシュ防止）。
		const std::string& full = jdClonePath_[i];
		const auto slash = full.find_last_of('/');
		const std::string dir  = (slash == std::string::npos) ? std::string()  : full.substr(0, slash);
		const std::string file = (slash == std::string::npos) ? full           : full.substr(slash + 1);

		// 分身は自機と同じ新モデル（スキニング）。2026-10-19：自機を新モデルにしたのに分身が古い人型（walk.mesh）のままだった
		AnimatedObject3DInstance* clone = AddDynamicAnimated(dir, file, ppos);
		if (!clone) continue; // 生成失敗時は視覚なしで継続
		Gameplay::Of(clone).GetCollider().enabled = false; // 当たり判定なし
		// 自機と同じ姿勢・向き（クリップが複数あるモデルは既定のアニメが付かないので差す）
		clone->PlayAnimation(kJdClonePoseClip, 0.0f);
		clone->SetLoop(false);
		clone->SetAnimationTime(kAnimLastFrameTime);
		clone->SetRotate(player_->GetRotate());
		clone->SetScale(player_->GetScale());
		clone->SetMaterialColor(jdCloneColor_); // 半透明色（alpha<1 で半透明）
		clone->Update(0.0f); // 初フレームの CB を確定（原点描画バグ回避）
		AddHighlight(clone); // 分身はグレースケール対象外（色つきのまま）にする
		jdClones_[i] = clone;
	}

	// 中心のプレイヤーは描画しない（分身に分裂した見た目にする）。
	// ハイライトからも外す＝非描画プレイヤーのシルエットがグレースケールに残らないように。
	player_->SetVisible(false);
	RemoveHighlight(player_);
}

void StagePlayScene::ClearJustDodgeClones()
{
	for (auto* c : jdClones_) {
		if (!c) continue;
		RemoveHighlight(c);     // ハイライト一覧から外す（破棄前に）
		DestroyDynamicEntity(c);
	}
	jdClones_.clear();
	// 中心プレイヤーの描画を戻す
	if (player_) player_->SetVisible(true);
}

bool StagePlayScene::TrySelectCloneByAction(InputActionMap* actions, const Vector2& moveDelta)
{
	if (!actions) return false;

	// アクションボタン→分身方向：通常移動入力(WASD/スティック)とは別系統。
	// WASD は移動・追加回避の方向指定に温存する設計。
	CounterDir dir = CounterDir::None;
	if      (actions->IsTriggered(static_cast<int>(Action::MeleeStrong))) dir = CounterDir::Up;
	else if (actions->IsTriggered(static_cast<int>(Action::MeleeWeak)))   dir = CounterDir::Right;
	else if (actions->IsTriggered(static_cast<int>(Action::Dodge)))       dir = CounterDir::Down;
	else if (actions->IsTriggered(static_cast<int>(Action::Heal)))        dir = CounterDir::Left;

	if (dir == CounterDir::None) return false;

	TriggerCloneCounterAction(dir, moveDelta);
	return true;
}

void StagePlayScene::TriggerCloneCounterAction(CounterDir dir, const Vector2& moveDelta)
{
	if (dir == CounterDir::None) return;

	// 全分身を破棄（選択された分身も含む）。以降は実プレイヤーモデル（AnimatedObject3D）を表示する。
	for (auto* c : jdClones_) {
		if (!c) continue;
		RemoveHighlight(c);
		DestroyDynamicEntity(c);
	}
	jdClones_.clear();

	// プレイヤーを再表示（SpawnJustDodgeClones で非表示にしていた）。
	if (player_) {
		player_->SetVisible(true);
		AddHighlight(player_); // ジャスト回避演出のグレースケール除外対象に戻す
	}
	// 派生を選んだら、ジャスト回避になった元の回避のアニメの続きは出さない（選ばなければ続きをそのまま鳴らす）
	StopDodgeAnim();

	// ボス戦（地上文脈）の派生。
	if (phase_ == Phase::Boss) {
		jdChosen_            = dir;
		jdSelecting_         = false;
		jdMerging_           = false;
		jdActionPhase_       = JdActionPhase::None;
		jdActionPhaseTimer_  = 0.0f;
		jdMeleeCameraActive_ = false;

		switch (dir) {
			case CounterDir::Left: {
				// 回復（小回復・無制限）。HP 操作は座標系に依存しないので STG と同一。即終了。
				if (player_) Gameplay::Of(player_).GetHP().Heal(healSmallAmount_);
				playerAnim_.Trigger(PlayerAnimState::Heal);
				LogBuffer::GetInstance()->Add("JustDodge derive (Boss): Left(回復)", LogBuffer::Level::Info);
				justDodgeCounterActive_ = false;
				EndJustDodgeCounterAction();
			} break;
			case CounterDir::Down: {
				// 追加回避：地上ダッシュ＋無敵窓＋射撃禁止＋必殺技ゲージ。即終了。
				// ダッシュ初速は groundVelocity_ に入り、justDodgeActive_ 中は移動入力がゼロ化される
				// ため、以降の指数減衰でダッシュだけが乗って自然停止する（通常回避と同じ手触り）。
				dodgeActive_          = true;
				dodgeTimer_           = 0.0f;
				dodgeCooldownTimer_   = dodgeCooldown_;
				dodgeActionLockTimer_ = dodgeActionLock_;
				if (bossStage_) bossStage_->ApplyDashImpulse(moveDelta);
				TriggerDodgeAnim(moveDelta);   // 新しい回避として最初から
				shootLockoutTimer_ = (std::max)(shootLockoutTimer_, dodgeIFrameDuration_);
				specialGauge_ = (std::min)(specialGaugeMax_, specialGauge_ + dodgeSpecialGaugeGain_);
				LogBuffer::GetInstance()->Add("JustDodge derive (Boss): Down(追加回避)", LogBuffer::Level::Info);
				justDodgeCounterActive_ = false;
				EndJustDodgeCounterAction();
			} break;
			case CounterDir::Up:
			case CounterDir::Right: {
				// 近接（強=Up / 弱=Right）：ボスの背後へワープして攻撃し、戻らない。
				// 詰め寄り終点＝ボス中心から「ボス→プレイヤー方向の逆符号」に jdMeleeApproachDist_ 離した点
				// （STGは同符号＝手前に接近。符号を反転するだけで背後になる）。
				if (player_) {
					jdApproachWorldStart_ = player_->GetTranslate();
				}
				// 回避した攻撃の持ち主（腕・拳など）ではなくボスの本体を基準に、水平だけで決める
				// （手足の後ろや宙に出て、判定がボスに届かないことがあった）。高さは今の地面のまま。
				// 「背後」はボスの向きの後ろ（自機から見た反対側だと、突進がすれ違った後はボスの進む先になり、
				// ワープした所へ突っ込んできた）。ボスの向きが取れなければ自機から見た反対側
				jdApproachWorldGoal_ = jdApproachWorldStart_;
				{
					IImGuiEditable* center = CounterFocusEntity();
					if (const Vector3* tp = center ? center->GetEditableTranslate() : nullptr) {
						float dx = jdApproachWorldStart_.x - tp->x;
						float dz = jdApproachWorldStart_.z - tp->z;
						if (const Vector3* br = center->GetEditableRotate()) {
							dx = std::sin(br->y);
							dz = std::cos(br->y);
						}
						const float dl = std::sqrt(dx * dx + dz * dz);
						if (dl > kNearZero) {
							dx /= dl; dz /= dl;
							jdApproachWorldGoal_ = {
								tp->x - dx * jdMeleeApproachDist_,
								jdApproachWorldStart_.y,
								tp->z - dz * jdMeleeApproachDist_,
							};
						}
					}
				}
				// Approach はここでは「ワープ待ち」の意味で流用（jdBossWarpDelay_ 経過で
				// UpdateJustDodgeCounterAction が実際のワープ＋攻撃発生を行う）。
				jdActionPhase_       = JdActionPhase::Approach;
				jdActionPhaseTimer_  = 0.0f;
				jdMeleeCameraActive_ = true;
				if (bossStage_) bossStage_->StopGroundVelocity(); // 残留速度が派生後に持ち越されないように
				SetTimeScale(TimeGroup::World, 0.0f);
				justDodgeCounterActive_ = true; // UpdateJustDodgeCounterAction に進行を委ねる
				LogBuffer::GetInstance()->Add(
					std::string("JustDodge derive (Boss): ") + ((dir == CounterDir::Up) ? "Up(近接強)" : "Right(近接弱)"),
					LogBuffer::Level::Info);
			} break;
			default: {
				justDodgeCounterActive_ = false;
				EndJustDodgeCounterAction();
			} break;
		}
		return;
	}

	// 復帰用に「派生開始時の playerInputOffset_」を保存（近接派生の元位置復帰で使う）。
	jdReturnOffset_ = playerInputOffset_;

	// プレイヤーを選択された分身位置へテレポート（視覚的に分身→プレイヤーへ切替）。
	switch (dir) {
		case CounterDir::Up:    playerInputOffset_.y += jdCloneOffset_; break;
		case CounterDir::Right: playerInputOffset_.x += jdCloneOffset_; break;
		case CounterDir::Down:  playerInputOffset_.y -= jdCloneOffset_; break;
		case CounterDir::Left:  playerInputOffset_.x -= jdCloneOffset_; break;
		default: break;
	}

	jdChosen_               = dir;
	jdSelecting_            = false;
	jdMerging_              = false;
	justDodgeCounterActive_ = true; // 派生中は受付期限を延長＋世界スロー継続
	jdActionPhase_          = JdActionPhase::None;
	jdActionPhaseTimer_     = 0.0f;
	jdMeleeCameraActive_    = false;

	// 該当アクション発動
	switch (dir) {
		case CounterDir::Up:
		case CounterDir::Right: {
			// 近接（強=Up は Y1〜Y4 / 弱=Right は B1〜B4 の自動連撃）。
			// フロー：ワールド空間で詰め寄り(Approach) → 攻撃発生(Active=既存meleeパイプ) → 元位置へ復帰(Return)。
			// camera-local 平面（z 固定）では z 差で melee 判定が届かないので、ワールド座標で直接補間する。
			if (player_) {
				Vector3* pp = player_->GetEditableTranslate();
				jdApproachWorldStart_ = pp ? *pp : Vector3{0.0f, 0.0f, 0.0f}; // テレポート後の現位置
				// 派生終了で戻すワールド位置 = jdReturnOffset_ をワールドへ変換
				if (camera_) {
					jdReturnWorldPos_ = TransformCoordinate(
						{ playerLocalOffset_.x + jdReturnOffset_.x,
						  playerLocalOffset_.y + jdReturnOffset_.y,
						  playerLocalOffset_.z }, camera_->GetWorldMatrix());
				} else {
					jdReturnWorldPos_ = jdApproachWorldStart_;
				}
			}
			// 詰め寄り終点 = 敵から「プレイヤー方向」へ jdMeleeApproachDist_ 離した位置
			jdApproachWorldGoal_ = jdApproachWorldStart_;
			if (jdCounterTarget_) {
				if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) {
					Vector3 epos = *tp;
					Vector3 d{ jdApproachWorldStart_.x - epos.x,
					           jdApproachWorldStart_.y - epos.y,
					           jdApproachWorldStart_.z - epos.z };
					float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
					if (dl > kNearZero) {
						d.x /= dl; d.y /= dl; d.z /= dl;
						jdApproachWorldGoal_ = {
							epos.x + d.x * jdMeleeApproachDist_,
							epos.y + d.y * jdMeleeApproachDist_,
							epos.z + d.z * jdMeleeApproachDist_,
						};
					}
				}
			}

			jdActionPhase_       = JdActionPhase::Approach;
			jdActionPhaseTimer_  = 0.0f;
			jdMeleeCameraActive_ = true; // 近接派生中は専用カメラに切替

			// 近接派生中はワールド時間を完全停止（敵がずれて当たらない事故を防ぐ）。
			// Player グループは等倍のままなので、プレイヤーの詰め寄り/攻撃は動く。
			SetTimeScale(TimeGroup::World, 0.0f);
		} break;

		case CounterDir::Down: {
			// 追加回避：moveDelta(WASD/スティック)方向へダッシュ＋無敵窓開始＋許容枠拡張＋射撃禁止
			SoundManager::GetInstance()->Play2DSound("se_dodge");
			TriggerDodgeAnim(moveDelta);   // 新しい回避として最初から
			dodgeActive_          = true;
			dodgeTimer_           = 0.0f;
			dodgeCooldownTimer_   = dodgeCooldown_;
			dodgeActionLockTimer_ = dodgeActionLock_;
			const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
			if (mlen > kNearZero) {
				playerVelocity_.x += (moveDelta.x / mlen) * dodgeImpulse_.x;
				playerVelocity_.y += (moveDelta.y / mlen) * dodgeImpulse_.y;
			}
			// 許容枠を一時拡張。回避＋無敵が終わってから戻し補間を開始する（UpdateJustDodgeCounterAction）。
			jdDodgeMarginActive_ = true;
			jdDodgeReturning_    = false;
			jdDodgeMarginTimer_  = 0.0f;
			// 射撃禁止：ダッシュ＋追加無敵中ずっと（UpdateJustDodgeCounterAction で毎フレ上書き）
			shootLockoutTimer_ = (std::max)(shootLockoutTimer_, dodgeIFrameDuration_);
			// 必殺技ゲージ加算
			specialGauge_ = (std::min)(specialGaugeMax_, specialGauge_ + dodgeSpecialGaugeGain_);
			// Down のフェーズ：直接 Active 扱いで、終了は dodgeActive_ が落ちたら判定する
			jdActionPhase_      = JdActionPhase::Active;
			jdActionPhaseTimer_ = 0.0f;
		} break;

		case CounterDir::Left: {
			// 回復（小回復・無制限）：上限チェックなし
			if (player_) {
				Gameplay::Of(player_).GetHP().Heal(healSmallAmount_);
			}
			playerAnim_.Trigger(PlayerAnimState::Heal);
			// 即座に派生終了（演出はジャスト回避のフェードアウトに任せる）
			EndJustDodgeCounterAction();
		} break;

		default: break;
	}

	const char* name =
		dir == CounterDir::Up    ? "Up(近接強)" :
		dir == CounterDir::Right ? "Right(近接弱)" :
		dir == CounterDir::Down  ? "Down(追加回避)" : "Left(回復)";
	LogBuffer::GetInstance()->Add(std::string("JustDodge derive: ") + name, LogBuffer::Level::Info);
}

void StagePlayScene::EndJustDodgeCounterAction()
{
	// 近接派生でワールドを停止していた場合は元のスロー値へ戻す。
	// （フェードアウト処理側で最終的に 1.0 に戻る）
	if (jdMeleeCameraActive_) {
		SetTimeScale(TimeGroup::World, justDodgeSlowWorld_);
	}
	justDodgeCounterActive_ = false;
	jdActionPhase_          = JdActionPhase::None;
	jdActionPhaseTimer_     = 0.0f;
	jdMeleeCameraActive_    = false;
	jdBossMeleeCamActive_   = false;
	jdBossCamSwingTimer_    = 0.0f;
}

void StagePlayScene::UpdateJustDodgeCounterAction(float dt)
{
	PEPPER_SCOPE("StagePlayScene::UpdateJustDodgeCounterAction");
	if (!justDodgeCounterActive_) return;
	jdActionPhaseTimer_ += dt;
	if (phase_ == Phase::Boss && jdBossMeleeCamActive_) jdBossCamSwingTimer_ += dt;

	switch (jdChosen_) {
		case CounterDir::Up:
		case CounterDir::Right: {
			// Approach → Active（溜め攻撃 1 発の発生〜スキ待ち） → Return → 終了
			// ワールド位置の補間は描画直前の SetTranslate 上書きで行う（ここではフェーズ管理だけ）。
			// 4 段の自動連撃はテンポが悪いので、上＝Y4 の後・右＝B4 の後の溜め攻撃の最後の段階を溜めずにすぐ放つ

			if (jdActionPhase_ == JdActionPhase::Approach) {
				// ボスはここを「ワープ待ち」として使う（詰め寄りダッシュの移動補間はしない）。
				const float waitDur = (phase_ == Phase::Boss) ? jdBossWarpDelay_ : jdMeleeApproachDuration_;
				if (jdActionPhaseTimer_ >= waitDur) {
					if (phase_ == Phase::Boss && player_) {
						// ワープ実行：位置は瞬間移動、カメラはここから最短経路で寄せ始める
						// （直前のカメラ実位置をスイング開始状態として採取）。
						// カメラを寄せる中心は ApplyJustDodgeMeleeCamera と同じ（ボスの本体・自機の高さ）
						Vector3 targetPos{ 0.0f, 0.0f, 0.0f };
						if (IImGuiEditable* focus = CounterFocusEntity()) {
							if (Vector3* tp = focus->GetEditableTranslate()) targetPos = *tp;
						}
						targetPos.y = jdApproachWorldGoal_.y;
						if (camera_) {
							ToSphericalAroundPivot(camera_->GetTranslate(), targetPos,
								jdBossCamSwingStartYaw_, jdBossCamSwingStartPitch_, jdBossCamSwingStartRadius_);
						}
						jdBossCamSwingTimer_  = 0.0f;
						jdBossMeleeCamActive_ = true;
						player_->SetTranslate(jdApproachWorldGoal_);
					}
					meleeCombo_.StartChargeAttack(jdChosen_ == CounterDir::Up
						? MeleeComboPart::ChargeKind::AfterY4 : MeleeComboPart::ChargeKind::AfterB4,
						MeleeChargeSet::kLevelCount);
					jdActionPhase_      = JdActionPhase::Active;
					jdActionPhaseTimer_ = 0.0f;
				}
			} else if (jdActionPhase_ == JdActionPhase::Active) {
				if (!meleeCombo_.IsLocked()) {
					if (phase_ == Phase::Boss) {
						// 戻らない：攻撃した場所（ボス背後）にそのまま留まる。
						// 通常カメラ（BossStagePart::UpdateCamera）へ制御を戻す前に、乗っ取っていた
						// 近接カメラの最終角度を camYaw_/camPitch_ へ同期しておく（古い角度への瞬間戻り防止）。
						meleeCombo_.ResetCombo();
						if (bossStage_) bossStage_->SyncYawPitchFromCamera();
						EndJustDodgeCounterAction();
					} else {
						jdActionPhase_      = JdActionPhase::Return;
						jdActionPhaseTimer_ = 0.0f;
					}
				}
			} else if (jdActionPhase_ == JdActionPhase::Return) {
				// STG専用（Bossはこのフェーズに来ない＝戻らない設計）。
				if (jdActionPhaseTimer_ >= jdMeleeReturnDuration_) {
					playerInputOffset_ = jdReturnOffset_;
					playerVelocity_    = { 0.0f, 0.0f };
					meleeCombo_.ResetCombo(); // 連撃終了でコンボ状態リセット
					EndJustDodgeCounterAction();
				}
			}
		} break;

		case CounterDir::Down: {
			// 回避が落ちる（dodgeActive_=false）まで Active 継続。
			// ダッシュ＋追加無敵中は射撃禁止を上書きし続ける（既存shootLockoutTimer_を流用）。
			if (dodgeActive_) {
				shootLockoutTimer_ = (std::max)(shootLockoutTimer_, kDodgeShootLockoutSeconds);
			} else {
				// 回避＋追加無敵終了 → 戻り開始フラグだけ立てる。
				// 押し戻し＆終了判定はメイン Update 側のコライダー 8 角チェックで行う
				// （ここで中心 NDC ベースに終了すると collider 端がはみ出したまま次フレで固まる）。
				if (!jdDodgeReturning_) {
					jdDodgeReturning_        = true;
					jdDodgeMarginTimer_      = 0.0f;
					jdDodgeReturnFromOffset_ = playerInputOffset_;
				}
				jdDodgeMarginTimer_ += dt;
			}
		} break;

		default: break;
	}
}

void StagePlayScene::UpdateJustDodgeClones(InputActionMap* actions, const Vector2& moveDelta, float dt)
{
	PEPPER_SCOPE("StagePlayScene::UpdateJustDodgeClones");
	if (!jdSelecting_ && !jdMerging_) return;
	if (!player_ || !camera_) return;

	// spread: 0=中心, 1=各方向に最大展開。分裂は ease-out で広がり、集合は中心へ戻る。
	float spread;
	if (jdMerging_) {
		jdMergeTimer_ += dt;
		float mt = (jdMergeDuration_ > kTinyEpsilon) ? (jdMergeTimer_ / jdMergeDuration_) : 1.0f;
		if (mt > 1.0f) mt = 1.0f;
		spread = 1.0f - mt * mt * mt; // 1→0（中心へ吸い込まれる）
	} else {
		jdSpreadTimer_ += dt;
		float st = (jdSpreadDuration_ > kTinyEpsilon) ? (jdSpreadTimer_ / jdSpreadDuration_) : 1.0f;
		if (st > 1.0f) st = 1.0f;
		spread = 1.0f - (1.0f - st) * (1.0f - st) * (1.0f - st); // EaseOutCubic 0→1
	}

	// 分身位置 = 本体位置 ± 基底 × オフセット × spread（ダッシュ滑り中も本体に追従）。
	// STG：カメラ視点平面（right/up）でダイヤモンド配置。
	// ボス戦：地上（XZ射影）基底で地面と平行なリング配置＝奥=Up/手前=Down/右=Right/左=Left。
	//         ピッチを振っても傾かず床にめり込まない（ヨーのみ追従）。
	const Vector3 ppos = player_->GetTranslate();
	Vector3 axisH; // Up/Down に使う軸（STG=カメラ上, ボス=地上forward）
	Vector3 axisV; // Right/Left に使う軸（STG=カメラ右, ボス=地上right）
	if (phase_ == Phase::Boss && bossStage_) {
		Vector3 gfwd, gright;
		bossStage_->ComputeGroundBasis(gfwd, gright);
		axisH = gfwd;
		axisV = gright;
	} else {
		const Matrix4x4 rot = MakeCameraRotateMatrix(camera_->GetRotate());
		axisV = { rot.m[0][0], rot.m[0][1], rot.m[0][2] }; // カメラ右
		axisH = { rot.m[1][0], rot.m[1][1], rot.m[1][2] }; // カメラ上
	}
	const float o = jdCloneOffset_ * spread;
	const Vector3 dirOff[4] = {
		{  axisH.x * o,  axisH.y * o,  axisH.z * o }, // Up   （奥／カメラ上）
		{  axisV.x * o,  axisV.y * o,  axisV.z * o }, // Right（右）
		{ -axisH.x * o, -axisH.y * o, -axisH.z * o }, // Down （手前／カメラ下）
		{ -axisV.x * o, -axisV.y * o, -axisV.z * o }, // Left （左）
	};
	for (int i = 0; i < static_cast<int>(jdClones_.size()) && i < kJdCloneCount; ++i) {
		if (!jdClones_[i]) continue;
		if (Vector3* t = jdClones_[i]->GetEditableTranslate()) {
			t->x = ppos.x + dirOff[i].x;
			t->y = ppos.y + dirOff[i].y;
			t->z = ppos.z + dirOff[i].z;
		}
		// ImGui で色を編集中でもライブ反映するため毎フレーム適用（clones は AnimatedObject3DInstance）
		static_cast<AnimatedObject3DInstance*>(jdClones_[i])->SetMaterialColor(jdCloneColor_);
	}

	// 集合アニメ完了 → 分身を消してプレイヤーを戻す
	if (jdMerging_ && jdMergeTimer_ >= jdMergeDuration_) {
		ClearJustDodgeClones();
		jdMerging_ = false;
		return;
	}

	// 選択中のみアクションボタンで方向確定（MeleeStrong=Up / MeleeWeak=Right / Dodge=Down / Heal=Left）
	if (jdSelecting_) TrySelectCloneByAction(actions, moveDelta);
}

void StagePlayScene::UpdateDodge(InputActionMap* actions, const Vector2& moveDelta, float dt)
{
	PEPPER_SCOPE("StagePlayScene::UpdateDodge");
	// クールダウン・行動ロックを実時間で減衰
	if (dodgeCooldownTimer_ > 0.0f)   dodgeCooldownTimer_   -= dt;
	if (dodgeActionLockTimer_ > 0.0f) dodgeActionLockTimer_ -= dt;

	// 回避無敵の経過。dodgeIFrameDuration_ を超えたら無敵終了。
	if (dodgeActive_) {
		dodgeTimer_ += dt;
		if (dodgeTimer_ > dodgeIFrameDuration_) {
			dodgeActive_ = false;
		}
	}

	// 入力で回避開始（行動ロック中・CD中・ジャスト回避演出中は不可）
	if (actions && actions->IsTriggered(static_cast<int>(Action::Dodge))
		&& !IsActionLocked() && dodgeCooldownTimer_ <= 0.0f && !justDodgeActive_) {
		SoundManager::GetInstance()->Play2DSound("se_dodge");
		TriggerDodgeAnim(moveDelta);
		dodgeActive_          = true;
		dodgeTimer_           = 0.0f;
		dodgeCooldownTimer_   = dodgeCooldown_;
		dodgeActionLockTimer_ = dodgeActionLock_;

		// 移動入力方向へダッシュ（入力なしはその場）。
		// ボス戦は地上速度（BossStagePart::groundVelocity_）へダッシュ初速を入れる
		// ＝camera-local の playerVelocity_ は地上移動に反映されないため。
		// STG は playerVelocity_ に初速インパルスを足す（以降の移動クリップで画面外に出ない）。
		if (phase_ == Phase::Boss && bossStage_) {
			bossStage_->ApplyDashImpulse(moveDelta);
		} else {
			const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
			if (mlen > kNearZero) {
				playerVelocity_.x += (moveDelta.x / mlen) * dodgeImpulse_.x;
				playerVelocity_.y += (moveDelta.y / mlen) * dodgeImpulse_.y;
			}
		}
	}
}

void StagePlayScene::ResetDodgeState()
{
	dodgeActive_          = false;
	dodgeTimer_           = 0.0f;
	dodgeCooldownTimer_   = 0.0f;
	dodgeActionLockTimer_ = 0.0f;
	wasInvincible_        = false;
	playerInvincibilityTimer_ = -kInvincibilityResetTimer;

	// 分身まわりは状態に関わらず確実に掃除
	jdSelecting_       = false;
	jdMerging_         = false;
	jdMergeTimer_      = 0.0f;
	jdChosen_          = CounterDir::None;
	jdCounterTarget_   = nullptr;
	jdEffectIntensity_ = 0.0f;
	jdActionPhase_     = JdActionPhase::None;
	jdActionPhaseTimer_ = 0.0f;
	jdMeleeCameraActive_ = false;
	jdBossMeleeCamActive_ = false;
	jdBossCamSwingTimer_  = 0.0f;
	jdDodgeMarginActive_ = false;
	jdDodgeReturning_    = false;
	jdDodgeMarginTimer_  = 0.0f;
	ClearJustDodgeClones();

	if (justDodgeActive_) {
		justDodgeActive_        = false;
		justDodgeTimer_         = 0.0f;
		justDodgeFadeOutTimer_  = -1.0f;
		justDodgeCounterActive_ = false;
		justDodgeTarget_        = nullptr;
		ClearHighlights();
		SetTimeScale(TimeGroup::World, 1.0f);
		if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
			pe->maskedGrayscale->SetEnabled(false);
			pe->maskedGrayscale->SetIntensity(1.0f);
			pe->maskedGrayscale->UpdateConstantBuffer();
		}
	}
}

void StagePlayScene::UpdateHeal(InputActionMap* actions)
{
	PEPPER_SCOPE("StagePlayScene::UpdateHeal");
	if (!actions || !player_) return;
	// ジャスト回避演出中は通常Healを発動しない（左派生で消費される＝同フレ二重消費を防ぐ）
	if (justDodgeActive_) return;
	if (!actions->IsTriggered(static_cast<int>(Action::Heal))) return;

	// デフォルトHeal = 大回復・固定回数制（STG/ボスで別カウント）。ストック制は廃止。
	const bool isBoss = (phase_ == Phase::Boss);
	int&      used = isBoss ? healUsedBoss_ : healUsedStg_;
	const int cap  = isBoss ? healMaxBoss_  : healMaxStg_;
	if (used >= cap) return;

	Gameplay::Of(player_).GetHP().Heal(healAmount_);
	++used;
	playerAnim_.Trigger(PlayerAnimState::Heal);
}

void StagePlayScene::ComputeAimBasis(Vector3& right, Vector3& up, Vector3& forward) const
{
	// 前 = 自機 → firingTarget。
	// 近接派生中（Up/Right）は firingTarget ではなく対象敵を直接向く（melee 判定オフセットも敵方向に展開される）。
	Vector3 fwd{ 0.0f, 0.0f, 1.0f };
	if (player_) {
		const Vector3 ppos = player_->GetTranslate();
		Vector3 tgt = firingTarget_;
		if (jdMeleeCameraActive_ && jdCounterTarget_) {
			if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) tgt = *tp;
		}
		fwd = { tgt.x - ppos.x, tgt.y - ppos.y, tgt.z - ppos.z };
	}
	float flen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
	if (flen < kBasisEpsilon) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.y /= flen; fwd.z /= flen;

	// 右 = worldUp × 前
	const Vector3 worldUp{ 0.0f, 1.0f, 0.0f };
	Vector3 rgt{
		worldUp.y * fwd.z - worldUp.z * fwd.y,
		worldUp.z * fwd.x - worldUp.x * fwd.z,
		worldUp.x * fwd.y - worldUp.y * fwd.x,
	};
	float rlen = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
	if (rlen < kBasisEpsilon) { rgt = { 1.0f, 0.0f, 0.0f }; rlen = 1.0f; } // 前が真上/真下のときの保険
	rgt.x /= rlen; rgt.y /= rlen; rgt.z /= rlen;

	// 上 = 前 × 右
	up = {
		fwd.y * rgt.z - fwd.z * rgt.y,
		fwd.z * rgt.x - fwd.x * rgt.z,
		fwd.x * rgt.y - fwd.y * rgt.x,
	};
	forward = fwd;
	right = rgt;
}

void StagePlayScene::SpawnComboMelee(const std::string& prefab, float damageScale, float rehitInterval)
{
	if (!player_) return;
	SoundManager::GetInstance()->Play2DSound("se_melee");
	// 向きは技が始まった時に決めた物（OnComboAttackStarted）。2 回目以降の判定も同じ向きに出る
	const Vector3& right = meleeBasisRight_;
	const Vector3& up = meleeBasisUp_;
	const Vector3& forward = meleeBasisForward_;
	// 技の威力＝攻撃力 × damageScale（当たった時の本あて/持続あての倍率はプレハブ側で掛かる）
	const int baseAtk = ScaledPlayerAttack();
	const int atk = baseAtk > 0
		? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(baseAtk) * damageScale)))
		: 0;
	// プレイヤー原点は足元なので、判定を world-up に meleeOriginHeight_ 持ち上げて体の中心から出す。
	// サイズは STG=プレハブ基準(1.0)、ボス戦は当てやすすぎるので meleeBossSizeScale_ で縮小。
	const float meleeSize = (phase_ == Phase::Boss) ? meleeBossSizeScale_ : 1.0f;
	SpawnPlayerMelee(player_, right, up, forward, prefab, atk,
		{ 0.0f, meleeOriginHeight_, 0.0f }, meleeSize, rehitInterval);
}

void StagePlayScene::OnComboAttackStarted()
{
	MarkPlayerAttack();
	// STG は狙い（ジャスト回避の派生中は相手）へ向ける
	if (phase_ != Phase::Boss || !bossStage_) {
		ComputeAimBasis(meleeBasisRight_, meleeBasisUp_, meleeBasisForward_);
		return;
	}
	// ボス戦：ジャスト回避の派生中はボスへ、そうでなければスティックが倒れている方へ、すぐ向き直ってから技を出す
	// （どちらも無ければ今の向き）
	float yaw = bossFacingYaw_;
	const bool turn = justDodgeCounterActive_ ? CounterTargetYaw(yaw) : MoveInputToBossYaw(playerMoveInput_, yaw);
	if (turn) {
		bossFacingYaw_ = yaw;
		bossFacingInit_ = true;
		if (player_) player_->SetRotate({ 0.0f, bossFacingYaw_, 0.0f });
	}
	const float s = std::sin(bossFacingYaw_);
	const float c = std::cos(bossFacingYaw_);
	meleeBasisForward_ = { s, 0.0f, c };
	meleeBasisRight_ = { c, 0.0f, -s };
	meleeBasisUp_ = { 0.0f, 1.0f, 0.0f };
}

void StagePlayScene::PlayComboClip(const std::string& clip, float speed, float fade, bool loop)
{
	// 近接の優先度は PlayerAnim の common.json（被弾・必殺技は近接より上）
	playerAnim_.PlayMelee(clip, speed, fade, loop);
}

void StagePlayScene::RestoreComboPose()
{
	playerAnim_.EndMelee();
}

void StagePlayScene::UpdateJustDodgeEffect(float dt)
{
	PEPPER_SCOPE("StagePlayScene::UpdateJustDodgeEffect");
	if (!justDodgeActive_) { jdEffectIntensity_ = 0.0f; return; }

	justDodgeTimer_ += dt;

	const float fi = justDodgeFadeIn_;
	const float fo = justDodgeFadeOut_;

	// 受付期間中（または追加入力アクション進行中）はフェードアウトせず保持する。
	// → 追加入力なしなら受付期間(=justDodgeReceiptWindow_)でフェードアウトへ。
	//    追加入力があれば justDodgeCounterActive_ を立てている間は延長される（次フェーズの分身カウンター用）。
	const bool holding = (justDodgeTimer_ < justDodgeReceiptWindow_) || justDodgeCounterActive_;

	float intensity = 1.0f;

	if (!holding) {
		// 受付終了 → フェードアウト開始。開始時に World 時間を通常へ戻してゲームを再開する。
		if (justDodgeFadeOutTimer_ < 0.0f) {
			justDodgeFadeOutTimer_ = 0.0f;
			SetTimeScale(TimeGroup::World, 1.0f);
			// 無選択で受付終了 → 分身を中心へ集合させてから消す
			if (jdSelecting_) {
				jdSelecting_  = false;
				jdMerging_    = true;
				jdMergeTimer_ = 0.0f;
			}
		}
		justDodgeFadeOutTimer_ += dt;
		intensity = (fo > 0.0f) ? (std::max)(0.0f, 1.0f - justDodgeFadeOutTimer_ / fo) : 0.0f;

		if (justDodgeFadeOutTimer_ >= fo && !jdMerging_) {
			// 完全終了（集合アニメ中は待つ）
			justDodgeActive_       = false;
			justDodgeTimer_        = 0.0f;
			justDodgeFadeOutTimer_ = -1.0f;
			justDodgeTarget_       = nullptr;
			jdEffectIntensity_     = 0.0f;
			jdSelecting_           = false;
			jdChosen_              = CounterDir::None;
			ClearJustDodgeClones();
			ClearHighlights();
			SetTimeScale(TimeGroup::World, 1.0f); // 念のため確実に戻す
			if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
				pe->maskedGrayscale->SetEnabled(false);
				pe->maskedGrayscale->SetIntensity(1.0f);
				pe->maskedGrayscale->UpdateConstantBuffer();
			}
			return;
		}
	} else if (justDodgeTimer_ < fi && fi > 0.0f) {
		// フェードイン
		intensity = justDodgeTimer_ / fi;
	}

	// intensity 反映（カメラ引きブレンドにも流用）
	jdEffectIntensity_ = intensity;
	if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
		pe->maskedGrayscale->SetIntensity(intensity);
		pe->maskedGrayscale->UpdateConstantBuffer();
	}
}

void StagePlayScene::UpdatePrecisionAim(InputActionMap* actions, float dt)
{
	PEPPER_SCOPE("StagePlayScene::UpdatePrecisionAim");
	if (!actions) return;

	// LT / 右クリックのホールドでモード ON（トグルは将来追加）
	bool held = actions->IsPressed(static_cast<int>(Action::PrecisionAim));
#ifdef _DEBUG
	// Debug：マウスが Viewport 外で ImGui がマウスをキャプチャ中なら抑制（Fire と同じ扱い）
	{
		bool mouseOverViewport = false;
		if (auto* vp = ImGuiManager::GetInstance()->GetViewportWindow()) {
			mouseOverViewport = vp->IsHovered();
		}
		if (!mouseOverViewport && ImGui::GetIO().WantCaptureMouse) {
			held = false;
		}
	}
#endif
	const float target = held ? 1.0f : 0.0f;
	precisionHeld_ = held;

	// blend を target へ線形フェード
	if (precisionFadeSpeed_ <= 0.0f) {
		precisionBlend_ = target;
	} else {
		const float step = precisionFadeSpeed_ * dt;
		if (precisionBlend_ < target) precisionBlend_ = (std::min)(target, precisionBlend_ + step);
		else                          precisionBlend_ = (std::max)(target, precisionBlend_ - step);
	}

	// FOV ズームとカメラ寄せは ApplyPrecisionCamera（プレイヤー配置後）でまとめて行う

	// ----- エイム感度低下（右スティックのみ。マウスは絶対ワープなので FOV ズームで自然に精密化）-----
	if (reticle_) {
		const float scale = 1.0f + (precisionStickScale_ - 1.0f) * precisionBlend_;
		reticle_->SetStickSpeed(reticleBaseStickSpeed_ * scale);
	}

	// ----- 演出（PrecisionBlur：周辺ぼかし＋周辺減光を内蔵。intensity でフェード）-----
	if (auto* pe = Game::GetPostEffect(); pe && pe->precisionBlur) {
		const bool active = precisionBlend_ > kVisibleEpsilon;
		pe->precisionBlur->SetEnabled(active);
		if (active) {
			pe->precisionBlur->SetInnerRadius(precisionBlurInnerRadius_);
			pe->precisionBlur->SetFalloff(precisionBlurFalloff_);
			pe->precisionBlur->SetMaxBlurPx(precisionBlurMaxPx_);
			pe->precisionBlur->SetVignette(precisionVignette_);
			pe->precisionBlur->SetIntensity(precisionBlurIntensity_ * precisionBlend_);
			pe->precisionBlur->UpdateConstantBuffer();
		}
	}
}

void StagePlayScene::ApplyPrecisionCamera(const Vector3& playerWorldPos)
{
	if (!camera_) return;

	// 基準（レール）カメラの状態。ここに来る時点で camera_ はレール位置・通常 FovY
	const Vector3 baseEye = camera_->GetTranslate();
	const Matrix4x4 rot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 right   = { rot.m[0][0], rot.m[0][1], rot.m[0][2] };
	const Vector3 up      = { rot.m[1][0], rot.m[1][1], rot.m[1][2] };
	const Vector3 forward = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };

	// プレイヤー位置を基準にした肩越しオフセット位置（カメラローカル R/U/F）
	const Vector3 targetEye = {
		playerWorldPos.x + right.x * precisionCamOffset_.x + up.x * precisionCamOffset_.y + forward.x * precisionCamOffset_.z,
		playerWorldPos.y + right.y * precisionCamOffset_.x + up.y * precisionCamOffset_.y + forward.y * precisionCamOffset_.z,
		playerWorldPos.z + right.z * precisionCamOffset_.x + up.z * precisionCamOffset_.y + forward.z * precisionCamOffset_.z,
	};

	const float b = precisionBlend_;
	camera_->SetTranslate({
		baseEye.x + (targetEye.x - baseEye.x) * b,
		baseEye.y + (targetEye.y - baseEye.y) * b,
		baseEye.z + (targetEye.z - baseEye.z) * b,
	});
	// FOV ズーム併用（向き＝GetRotate は変えない＝レール注視点方向を維持）
	camera_->SetFovY(baseFovY_ + (precisionFovY_ - baseFovY_) * b);
	camera_->Update();
}

void StagePlayScene::OnImGuiTuning() {
#ifdef _DEBUG
	// 調整の画面はボス戦・大河・自機などを全部触るので、開いたら後回しの初期化を済ませる
	CompleteDeferredInit();
	bool changed = false;
	ImGui::TextDisabled("Auto-saved to: %s", kStagePlayTuningPath);

	if (ImGui::CollapsingHeader("Player")) {
		ImGui::DragFloat3("Local Offset", &playerLocalOffset_.x, 0.05f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Move Speed (X/Y)", &playerMoveSpeed_.x, 0.05f, 0.0f, 50.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Clip Margin (X/Y)", &playerClipMargin_.x, 0.01f, -1.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Smooth Time (s)", &playerSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Attack Move Scale", &attackMoveSpeedScale_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Move State Delay (s)", &moveStateDelay_, 0.05f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Glide Hit Lean (deg)", &moveStateLeanDeg_, 0.5f, 0.0f, 90.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("Move State: %s", (timeSinceAttack_ >= moveStateDelay_) ? "Move" : "Attack");
		ImGui::SeparatorText("Look (material)");
		ImGui::SliderFloat("Environment (IBL) Coefficient##look", &playerLook_.environmentCoefficient, 0.0f, 2.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Checkbox("Override Metallic / Roughness##look", &playerLook_.overrideMaterial);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::TextDisabled("切ると .mat の値（体は金属度 0.976・粗さ 0.4）。切り替えは再起動で戻る");
		ImGui::SliderFloat("Metallic##look", &playerLook_.metallic, 0.0f, 1.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SliderFloat("Roughness##look", &playerLook_.roughness, 0.0f, 1.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SliderFloat("Base Glow (fill)##look", &playerLook_.baseGlow, 0.0f, 0.5f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		{ float lead = GetEnemyShotLeadRate(); if (ImGui::DragFloat("Enemy Shot Lead Rate", &lead, 0.01f, 0.0f, 1.0f, "%.2f")) SetEnemyShotLeadRate(lead); }
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("InputOffset: (%.2f, %.2f)  Vel: (%.2f, %.2f)",
			playerInputOffset_.x, playerInputOffset_.y,
			playerVelocity_.x, playerVelocity_.y);

		// シーン停止中（エディタ Pause）のみレティクル追従を止めてこの向きで固定（確認用）
		ImGui::DragFloat3("Paused Facing (rad)", &fixedPlayerFacing_.x, 0.01f);
	}

	// 自機のスラスター（カタパルトと同じポイント制。エフェクトは PlayerThruster_S/M/L）
	if (ImGui::CollapsingHeader("Player Thrusters")) {
		ImGui::Text("Move Input: (%.2f, %.2f)", playerMoveInput_.x, playerMoveInput_.y);
		playerThrusters_.OnImGui();
	}

	// 武器（ソケット追従）の設定は Hierarchy でプレイヤーを選択 → Inspector の "Weapon" セクションへ移設。
	// 編集後は Inspector の "Save as Prefab" で player.json に保存する。

	if (ImGui::CollapsingHeader("Melee")) {
		// 近接判定をプレイヤー足元原点から world-up に持ち上げる高さ（地上戦のめり込み対策）。
		ImGui::DragFloat("Origin Height (up)", &meleeOriginHeight_, 0.02f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		// ボス戦での判定＋見た目サイズ倍率（STG=1.0固定。ボスは当てやすすぎるので縮小）。
		ImGui::DragFloat("Boss Size Scale", &meleeBossSizeScale_, 0.01f, 0.05f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::TextDisabled("STG=1.0 固定 / Boss=この倍率（判定と見た目の両方）");
		// 技の表・溜め攻撃（Resources/Json/Tuning/MeleeCombo.json。Save は Part 側のボタン）
		if (ImGui::TreeNode("Combo")) {
			meleeCombo_.OnImGui();
			ImGui::TreePop();
		}
	}

	if (ImGui::CollapsingHeader("Stage Phase", ImGuiTreeNodeFlags_DefaultOpen)) {
		static const char* kPhaseNames[] = { "Rail", "Landing", "Boss", "StageStart" };
		ImGui::TextColored(ImVec4(kPhaseTextR, kPhaseTextGB, kPhaseTextGB, 1.0f), "Phase: %s", kPhaseNames[(int)phase_]);
		if (phase_ == Phase::Rail) {
			ImGui::Text("Stage: %.1f / %.1f s", railStage_ ? railStage_->GetStageSeconds() : 0.0f, seekMaxSec_);
		} else if (phase_ == Phase::Landing) {
			ImGui::Text("Landing: %.1f / %.1f s", landingTimer_, landingDuration_);
		} else if (phase_ == Phase::StageStart) {
			ImGui::Text("Stage Start (16_StageStart.md)");
		} else {
			ImGui::Text("Boss: %s", (bossStage_ && bossStage_->IsBossAlive()) ? "alive" : "down");
		}
		if (ImGui::DragFloat("Seek Max (s)", &seekMaxSec_, 1.0f, 5.0f, 1800.0f, "%.0f")) {}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::DragFloat("Landing Duration (s)", &landingDuration_, 0.5f, 0.0f, 120.0f, "%.1f")) {}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		ImGui::SeparatorText("ストリーミング（11 番 L9）");
		if (streaming_ && streaming_->OnImGui()) changed = true;

		ImGui::SeparatorText("クリップ面");
		ImGui::TextDisabled("Camera の既定は near=0.1 / far=100（=100m先が描画されない）。");
		if (ImGui::DragFloat("Near Clip", &cameraNearClip_, 0.05f, 0.01f, 10.0f, "%.2f")) {
			if (camera_) camera_->SetNearClip(cameraNearClip_);
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::DragFloat("Far Clip", &cameraFarClip_, 25.0f, 10.0f, 50000.0f, "%.0f")) {
			if (camera_) camera_->SetFarClip(cameraFarClip_);
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}
	if (stageEnv_) {
		// "Jump" は Rail タイムラインのシーク。Seek() 側で環境も即時適用される。
		stageEnv_->OnImGuiTuning(changed, [this](float sec) { Seek(sec); });
		stageEnv_->OnImGuiClouds();
		stageEnv_->OnImGuiRaymarchClouds();
	}
	if (railStage_) railStage_->OnImGuiTuning(changed); // 既存の Rail Camera / Wave Editor セクション
	if (bossStage_) bossStage_->OnImGuiTuning(changed); // ボス戦（アリーナ/移動/カメラ）調整
	if (ImGui::CollapsingHeader("Stage Clear (23 B)")) clearSeq_.OnImGui();   // 撃破の段取り・STAGE CLEAR
	if (bossWater_ && ImGui::CollapsingHeader("Boss Water")) bossWater_->OnImGui();
	if (gravityOrb_ && ImGui::CollapsingHeader("Boss GravityOrb（必殺技）")) gravityOrb_->OnImGui();

	if (ImGui::CollapsingHeader("Skybox")) {
		// 候補 Cubemap（手持ちの3枚）。新しい dds を足したらここに追記する。
		static const char* kCubemaps[] = {
			"Resources/Cubemaps/title_clear_sky.dds",
			"Resources/Cubemaps/rogland_clear_night_8k.dds",
			"Resources/Cubemaps/rogland_clear_night_4k.dds",
			"Resources/Cubemaps/passendorf_snow_8k.dds",
			"Resources/Cubemaps/TrueHDRI_YamagataField_181231_1137_L1000_Unclipped_sRGB.dds",
		};

		// --- 常時の着色（即反映）---
		if (ImGui::ColorEdit4("Tint (常時着色)", &skyboxTint_.x)) {
			if (skybox_) skybox_->SetColor(skyboxTint_);
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		// --- Cubemap 切替（即 / クロスフェード）---
		ImGui::SeparatorText("Cubemap 切替（テスト）");
		static int sel = 0;
		ImGui::Combo("Cubemap", &sel, kCubemaps, IM_ARRAYSIZE(kCubemaps));
		if (ImGui::Button("Apply (即差替)")) {
			if (skybox_) skybox_->SetCubemap(kCubemaps[sel]);
		}
		ImGui::SameLine();
		if (ImGui::Button("Blend (クロスフェード)")) {
			if (skybox_) skybox_->BlendTo(kCubemaps[sel], bossSkyboxBlendDuration_);
		}
		if (skybox_) {
			ImGui::Text("Now: %s%s", skybox_->GetCubemapFilePath().c_str(),
				skybox_->IsBlending() ? "  (blending...)" : "");
		}

		// --- 必殺技中の暗転 ---
		ImGui::SeparatorText("必殺技中の暗転");
		ImGui::ColorEdit4("Dark Color", &specialSkyboxDarkColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fade In (s)", &specialSkyboxFadeIn_, 0.01f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fade Out (s)", &specialSkyboxFadeOut_, 0.01f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		// --- ボス戦（フラグ未実装のためテスト用チェックで疑似トリガー）---
		ImGui::SeparatorText("ボス戦（テスト）");
		static int bossSel = 2;
		if (ImGui::Combo("Boss Cubemap", &bossSel, kCubemaps, IM_ARRAYSIZE(kCubemaps))) {
			bossSkyboxPath_ = kCubemaps[bossSel];
			changed = true;
		}
		ImGui::DragFloat("Blend Duration (s)", &bossSkyboxBlendDuration_, 0.05f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		// チェックの立ち上がり/下がりを Update 側が拾って BlendTo する
		ImGui::Checkbox("Boss Battle Active (test)", &bossBattleActive_);
		ImGui::TextDisabled("  ON で boss空へ / OFF で平常時の空へクロスフェード");
	}

	if (ImGui::CollapsingHeader("Camera Idle Sway")) {
		if (ImGui::Checkbox("Enabled##idleSway", &idleSwayEnabled_)) changed = true;
		ImGui::DragFloat3("Rot Amp (rad)", &idleSwayRotAmp_.x, 0.0005f, 0.0f, 0.1f, "%.4f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Pos Amp (m)", &idleSwayPosAmp_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Speed", &idleSwaySpeed_, 0.05f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}

	if (ImGui::CollapsingHeader("Shooting")) {
		ImGui::TextDisabled("弾パラメータ（速度/寿命/collider拡大/ホーミング/貫通）は\n弾プレハブの BulletParams で設定する。");
		ImGui::TextDisabled("連射間隔(Fire Rate)はプレイヤープレハブの ChargeParams で設定する。");
	}

	if (ImGui::CollapsingHeader("Precision Aim (精密射撃)")) {
		ImGui::TextDisabled("LT / 右クリック ホールドで発動。射撃間隔・チャージ・マルチロックは通常と同一。");
		ImGui::Text("Blend: %.2f  (BaseFovY: %.3f)", precisionBlend_, baseFovY_);
		ImGui::SeparatorText("カメラ / 操作");
		ImGui::DragFloat3("Camera Offset (R/U/F)", &precisionCamOffset_.x, 0.05f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Zoom FovY (rad)", &precisionFovY_, 0.005f, 0.05f, 1.5f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fade Speed (/s)", &precisionFadeSpeed_, 0.1f, 0.0f, 30.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Stick Sensitivity x", &precisionStickScale_, 0.01f, 0.05f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SeparatorText("演出（周辺ぼかし＋減光）");
		ImGui::DragFloat("Vignette", &precisionVignette_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blur Intensity", &precisionBlurIntensity_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blur Inner Radius", &precisionBlurInnerRadius_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blur Falloff", &precisionBlurFalloff_, 0.01f, 0.01f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blur Max (px)", &precisionBlurMaxPx_, 0.1f, 0.0f, 32.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}

	if (ImGui::CollapsingHeader("Aim / Lock-on")) {
		ImGui::DragFloat("Aim Plane Distance (m)", &aimPlaneDistance_, 1.0f, 5.0f, 500.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("カメラからの『狙いの面』距離。\n"
			                  "弾速・寿命とは独立。\n"
			                  "非ロックオン時、レティクルが指すこの面上の点へ弾が向かう。");
		}
		ImGui::DragFloat("Smooth Time (s)", &aimSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Assist Pixel Scale", &aimAssistPixelScale_, 0.05f, 0.5f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Reticle Overlap Rate", &aimReticleOverlapRate_, 0.05f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Aim Converge Px", &aimConvergePx_, 1.0f, 0.0f, 800.0f, "%.0f px");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		ImGui::DragFloat("Boss Facing Turn Smooth (s)", &bossFacingTurnSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ボス戦のプレイヤー旋回の指数減衰時定数（秒）。\n小さいほど機敏に移動方向/ボス方向を向く。");
		}
		ImGui::DragFloat("Boss Melee Warp Delay (s)", &jdBossWarpDelay_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ボス戦の近接派生（Up/Right）：トリガーからボス背後へワープするまでの待機秒数。");
		}
		ImGui::DragFloat("Boss Melee Cam Swing (s)", &jdBossCamSwingDuration_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ワープ直後、カメラが最短ヨーでボス周りを回り込んで最終フレーミングへ寄りきるまでの秒数。");
		}

		ImGui::Separator();
		ImGui::TextDisabled("Reticle Outer Particle");
		if (reticle_) {
			// Reticle のメンバを直接編集（中間変数を経由しない）
			ImGui::DragFloat("Outer Min Offset (px)", reticle_->GetLockOnMinPxOutsidePtr(), 1.0f, 0.0f, 512.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
			ImGui::DragFloat("Outer Max Offset (px)", reticle_->GetLockOnMaxPxOutsidePtr(), 1.0f, 0.0f, 1024.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("外側パーツの中心からの配置オフセット（内側レティクル半径連動）の最小/最大 (pixel)");
			}
			ImGui::DragFloat("Outer Min Size (px)", reticle_->GetOuterSizeMinPtr(), 1.0f, 0.0f, 512.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
			ImGui::DragFloat("Outer Max Size (px)", reticle_->GetOuterSizeMaxPtr(), 1.0f, 0.0f, 1024.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("外側パーツのスプライトサイズの最小/最大 (pixel)");
			}
		}
		ImGui::DragFloat("Charge Initial Offset (px)", &outerChargeStartRadius_, 1.0f, 0.0f, 1024.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("チャージ完了の瞬間、外側パーツが寄ってくる前の初期追加オフセット (pixel)。\nここから 0 へ減衰して内側へ収束する。");
		}
		ImGui::DragFloat("Outer Easing Duration (s)", &outerChargeEasingDuration_, 0.01f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("チャージ完了時に外側パーツの追加広がりが最大→0へ収束する時間 (秒)");
		}
	}

	if (ImGui::CollapsingHeader("Special (Shatterline) / Heal", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextUnformatted("必殺技（装備・ゲージ・クールタイム）");
		// 装備中種別（Storm Lock / Shatterline）。本来は2択装備制で発動ボタン共通。
		{
			const char* kindNames[] = { "StormLock", "Shatterline" };
			int kindIdx = static_cast<int>(equippedSpecial_);
			if (ImGui::Combo("Equipped Special", &kindIdx, kindNames, IM_ARRAYSIZE(kindNames))) {
				SetEquippedSpecial(static_cast<SpecialKind>(kindIdx));
				changed = true;
			}
			ImGui::Text("Active Gauge Max: %.1f", specialGaugeMax_);
		}
		ImGui::DragFloat("Gauge Max (Shatterline)", &specialGaugeMaxShatterline_, 1.0f, 1.0f, 1000.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) { SetEquippedSpecial(equippedSpecial_); changed = true; }
		ImGui::DragFloat("Cooldown",            &specialCooldown_,      0.5f, 0.0f, 120.0f, "%.1f sec");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (specialCooldownTimer_ > 0.0f) {
			ImGui::TextColored(ImVec4(1.0f, kCooldownTextG, kCooldownTextB, 1.0f), "Cooldown: %.1f sec 残り（発動不可）", specialCooldownTimer_);
		} else {
			ImGui::TextColored(ImVec4(kReadyTextR, 1.0f, kReadyTextB, 1.0f), "Cooldown: Ready");
		}
		ImGui::DragFloat("Dodge Gauge Gain",    &dodgeSpecialGaugeGain_,0.1f, 0.0f, 100.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::TextDisabled("近接の 1 ヒットのゲージ量は技ごと（Melee > Combo の Gauge Gain）");
		ImGui::DragFloat("Melee Late Rate",     &meleeLateGaugeRate_,   0.05f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ProgressBar(specialGauge_ / (std::max)(1.0f, specialGaugeMax_), ImVec2(-1.0f, 0.0f));

		// ----- Shatterline -----
		shatterline_.OnImGui(changed);

		// デバッグボタン
		if (ImGui::Button("Fill Gauge"))   specialGauge_ = specialGaugeMax_;
		ImGui::SameLine();
		if (ImGui::Button("Trigger Now") && !specialActive_) {
			specialGauge_ = specialGaugeMax_;
			TriggerSpecialMove();
		}
		ImGui::SameLine();
		if (ImGui::Button("Force End"))    {
			EndSpecialMove();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Cooldown")) specialCooldownTimer_ = 0.0f;

		// ゲージ UI 位置・サイズ
		ImGui::Separator();
		ImGui::TextUnformatted("Gauge Bar UI");
		ImGui::DragFloat("Bar Width",  &gaugeBarMaxWidth_, 1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Bar Height", &gaugeBarHeight_,   0.5f, 0.0f, 200.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Bar Pos X",  &gaugeBarPosX_,     1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Bar Pos Y",  &gaugeBarPosY_,     1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Bar BG Color",   &gaugeBarBgColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Bar FG Color",   &gaugeBarFgColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Bar Full Color", &gaugeBarFullColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		ImGui::Separator();
		ImGui::TextUnformatted("回復（デフォルト Heal = 大・固定回数 / 左派生 = 小・無制限）");
		ImGui::DragInt("Heal Amount (大・R/X)",   &healAmount_,      1.0f, 0, 999);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Heal Small (小・左派生)", &healSmallAmount_, 1.0f, 0, 999);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Max Heals (STG)",  &healMaxStg_,  1.0f, 0, 99);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Max Heals (Boss)", &healMaxBoss_, 1.0f, 0, 99);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("usedSTG=%d/%d  usedBoss=%d/%d  gauge=%.1f/%.1f",
			healUsedStg_, healMaxStg_, healUsedBoss_, healMaxBoss_,
			specialGauge_, specialGaugeMax_);
	}

	if (ImGui::CollapsingHeader("Damage / Invincibility")) {
		ImGui::DragFloat("Invincibility (s)", &playerInvincibilityDuration_, 0.05f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Shoot Lockout (s)", &shootLockoutDuration_, 0.05f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blink Frequency (Hz)", &damageBlinkFrequency_, 0.5f, 0.0f, 40.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Blink Alpha", &damageBlinkAlpha_, 0.02f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit3("Damage Blink Glow", &damageBlinkEmissive_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit3("Dodge Blink Glow", &dodgeBlinkEmissive_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Shake Intensity", &damageCameraShakeIntensity_, 0.02f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Shake Duration (s)", &damageCameraShakeDuration_, 0.02f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::Button("Test: Take 10 Damage") && player_) {
			OnPlayerTakeDamage(kTestDamage);
		}
		if (player_) {
			const HP& hp = Gameplay::Of(player_).GetHP();
			ImGui::Text("Player HP: %d / %d  (invuln=%.2f, lockout=%.2f)",
				hp.currentHP, hp.maxHP, playerInvincibilityTimer_, shootLockoutTimer_);
		}
	}

	if (ImGui::CollapsingHeader("HP Bar UI")) {
		if (ImGui::DragFloat("Bar Max Width", &hpBarMaxWidth_, 2.0f, 50.0f, 1000.0f, "%.0f")) {
			if (hpBarBackground_) hpBarBackground_->SetSize({ hpBarMaxWidth_ * hpBarTargetRatio_, hpBarHeight_ });
			if (hpBarForeground_) hpBarForeground_->SetSize({ hpBarMaxWidth_ * hpBarCurrentRatio_, hpBarHeight_ });
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::DragFloat("Bar Height", &hpBarHeight_, 1.0f, 4.0f, 80.0f, "%.0f")) {
			if (hpBarBackground_) hpBarBackground_->SetSize({ hpBarMaxWidth_ * hpBarTargetRatio_, hpBarHeight_ });
			if (hpBarForeground_) hpBarForeground_->SetSize({ hpBarMaxWidth_ * hpBarCurrentRatio_, hpBarHeight_ });
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Red Bar Lerp Speed", &hpBarLerpSpeed_, 0.02f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::DragFloat("Bar Pos X", &hpBarPosX_, 1.0f, 0.0f, 2000.0f, "%.0f")) {
			if (hpBarBackground_) hpBarBackground_->SetPosition({ hpBarPosX_, hpBarPosY_ });
			if (hpBarForeground_) hpBarForeground_->SetPosition({ hpBarPosX_, hpBarPosY_ });
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::DragFloat("Bar Pos Y", &hpBarPosY_, 1.0f, 0.0f, 2000.0f, "%.0f")) {
			if (hpBarBackground_) hpBarBackground_->SetPosition({ hpBarPosX_, hpBarPosY_ });
			if (hpBarForeground_) hpBarForeground_->SetPosition({ hpBarPosX_, hpBarPosY_ });
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}

	if (ImGui::CollapsingHeader("Loot (22)")) {
		GetGameSession()->GetLootLedger()->OnImGui();
	}

	if (ImGui::CollapsingHeader("Stage Clear Record (23)")) {
		GetGameSession()->GetStageClearRecord()->OnImGui();
	}

	if (ImGui::CollapsingHeader("Score UI")) {
		ImGui::Text("Score: %d", GetGameSession()->GetScoreManager()->GetScore());
		if (ImGui::Button("Reset Score")) GetGameSession()->GetScoreManager()->Reset();
		ImGui::SameLine();
		if (ImGui::Button("+1000")) GetGameSession()->GetScoreManager()->AddScore(kTestScoreAdd);

		ImGui::TextDisabled("位置は画面右端基準の (X, Y) オフセット");

		ImGui::TextUnformatted("Label (\"SCORE\")");
		ImGui::PushID("scoreLabel");
		ImGui::DragFloat("Scale",    &scoreLabelScale_,   0.02f, 0.1f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Offset X", &scoreLabelOffsetX_, 1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Offset Y", &scoreLabelOffsetY_, 1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Color",   &scoreLabelColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Outline Thickness", &scoreLabelOutlineThickness_, 0.1f, 0.0f, 10.0f, "%.1f px");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Outline Color", &scoreLabelOutlineColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::PopID();

		ImGui::TextUnformatted("Number");
		ImGui::PushID("scoreNumber");
		ImGui::DragFloat("Scale",    &scoreNumberScale_,   0.02f, 0.1f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Offset X", &scoreNumberOffsetX_, 1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Offset Y", &scoreNumberOffsetY_, 1.0f, 0.0f, 2000.0f, "%.0f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Color",   &scoreNumberColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Outline Thickness", &scoreNumberOutlineThickness_, 0.1f, 0.0f, 10.0f, "%.1f px");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Outline Color", &scoreNumberOutlineColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::PopID();
	}

	// ----- LightningRuntime テスト（Phase2B動作確認用、必殺技実装後に削除予定） -----
	if (ImGui::CollapsingHeader("Lightning Runtime Test")) {
		ImGui::TextDisabled("動的雷ランタイム（毎寿命でランダム再生成）。");
		ImGui::Text("State: %s", (lightningTest_ && lightningTest_->IsActive()) ? "Active" : "Inactive");
		if (ImGui::Button("Spawn (camera forward 30u)")) {
			lightningTest_ = std::make_unique<LightningRuntime>();
			PrimitiveGenerator::LightningBoltParams lp{};
			lp.appearance.startWidth = kTestBoltWidth;
			lp.appearance.endWidth   = kTestBoltWidth;
			lp.appearance.planeCount = kTestBoltPlaneCount;
			lp.appearance.fadeStartLength = kTestBoltFadeLength;
			lp.appearance.fadeEndLength   = kTestBoltFadeLength;
			lp.appearance.startColor = { kTestBoltColorR, kTestBoltColorG, 1.0f, 1.0f };
			lp.appearance.endColor   = { kTestBoltColorR, kTestBoltColorG, 1.0f, 1.0f };
			lp.generations = kTestBoltGenerations;
			lp.maxOffsetRatio = kTestBoltMaxOffsetRatio;
			lp.branchProbability = kTestBoltBranchProbability;
			lp.branchLengthScale = kTestBoltBranchLengthScale;
			lightningTest_->Initialize(lp, "Resources/Textures/white1x1.dds", PrimitivePipeline::kBlendModeAdd);
			// 始終点
			if (camera_) {
				Vector3 camPos = camera_->GetTranslate();
				Vector3 fwd    = camera_->GetForward();
				Vector3 endP = { camPos.x + fwd.x * kTestBoltDistance, camPos.y + fwd.y * kTestBoltDistance, camPos.z + fwd.z * kTestBoltDistance };
				lightningTest_->SetEndpoints(camPos, endP);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			if (lightningTest_) lightningTest_->Stop();
		}
		ImGui::TextDisabled("※ 始終点はSpawn時のカメラに固定。動的追従の正式版はStorm Lock実装時に。");
	}

	// 末尾で一括保存（変更があれば即書き出し）
	if (stageStart_ && ImGui::CollapsingHeader("Stage Start (16 F3 / 18-3 IN)")) {
		stageStart_->OnImGui();
		if (ImGui::Button("Play From Descent (Phase)")) {
			stageStart_->BeginDescent(stageStart_->MakeDefaultHandoff());
			phase_ = prevPhase_ = Phase::StageStart;
		}
		ImGui::SameLine();
		if (ImGui::Button("Save Stage Start")) changed = true;
		ImGui::TextDisabled("コクピットの灯りは Hub.json（HUB の Hub Tuning で保存）");
	}
	if (cloudTopIntro_ && ImGui::CollapsingHeader("Cloud Top Intro (16 F6)")) {
		// レールを t=0 に戻し、HUB を通らずに来たときと同じ雲の上から流す
		if (ImGui::Button("Play From Cloud Top")) {
			Seek(0.0f);
			if (stageStart_) {
				stageStart_->BeginFromCloudTop();
				phase_ = prevPhase_ = Phase::StageStart;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Save Cloud Top")) changed = true;
		cloudTopIntro_->OnImGui();
		if (playerWings_ && ImGui::TreeNode("Player Wings")) {
			playerWings_->OnImGui();
			ImGui::TreePop();
		}
	}
	if (playerDeath_ && ImGui::CollapsingHeader("Player Death (19 G1)")) {
		// 調整用：HP は減らさず、終わってもゲームオーバーへ行かない（自機を元に戻す）。種類はいまのフェーズで決まる
		if (ImGui::Button("Preview Death")) BeginPlayerDeath(true);
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) playerDeath_->Cancel();
		ImGui::SameLine();
		if (ImGui::Button("Save Player Death")) changed = true;
		playerDeath_->OnImGui();
	}
	// 賭ける画面（19 番 §4.7）ができるまで、ここで賭け分と難易度を決めて始める（§4.8）
	if (ImGui::CollapsingHeader("Ether State (19 G6)")) GetGameSession()->GetEtherState()->OnImGui();
	if (ImGui::CollapsingHeader("Difficulty Profile (19 5)")) GetGameSession()->GetDifficultyProfile()->OnImGui();
	if (ImGui::CollapsingHeader("Flight Unit (20 3)") && flightUnit_) flightUnit_->OnImGui();
	if (ImGui::CollapsingHeader("Player Beam") && playerBeam_) playerBeam_->OnImGui();
	if (ImGui::CollapsingHeader("Main Weapon (20 1)")) {
		ImGui::Checkbox("Preview Blade (show without melee)##MainWeapon", &weaponBladePreview_);
		if (GetGameSession()->GetLoadoutState()->OnImGuiMainWeapon()) RebuildMainWeaponModel();
	}
	if (ImGui::CollapsingHeader("Shield (20 2)") && GetGameSession()->GetLoadoutState()->OnImGuiShield()) {
		RebuildShieldModel();
	}
	// 自機のアニメ（25 番）：今の状態・JSON の読み直し・試し鳴らし
	if (ImGui::CollapsingHeader("Player Anim (25)")) {
		playerAnim_.OnImGui();
	}

	if (changed) SaveTuningToJson();
#endif
}

void StagePlayScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();
	// 画面の水滴の値（川・雲の縦穴の 2 組）。ResetEffects で既定に戻るので、ここで読み直す
	Game::GetPostEffect()->screenDroplet->LoadSettings("Resources/Json/Tuning/ScreenDroplet.json");

	// スコアをリセット（再ロード時の累積防止）
	GetGameSession()->GetScoreManager()->Reset();
	// 手に入れたアイテムとエーテルの集計も（リザルトが読む。22_ItemDrop.md）
	GetGameSession()->GetLootLedger()->Reset();
	lootLog_.Clear();
	// クリアタイムと撃破の瞬間のキャプチャ（リザルトが読む。23_StageClear.md §2・§3）。
	// ボス戦からのリトライでもここを通るので、タイムはその回の分だけになる
	GetGameSession()->GetStageClearRecord()->ResetForNewRun();
	clearSeq_.Reset();

	// ゲームオーバー画面の「ボス戦からリトライ」要求とスナップショットを読み出す。
	// このアタリで再取得されるまではチェックポイントを無効化しておく
	// （ボス到達前に力尽きた新しいアタリへ前回の値が漏れないように）。
	const bool bossRetryRequested = GetGameSession()->GetBossRetryState()->ConsumeBossRetryRequest();
	const bool haveBossCheckpoint = GetGameSession()->GetBossRetryState()->HasCheckpoint();
	const int  ckPlayerHP         = GetGameSession()->GetBossRetryState()->GetCheckpointPlayerHP();
	const float ckSpecialGauge    = GetGameSession()->GetBossRetryState()->GetCheckpointSpecialGauge();
	GetGameSession()->GetBossRetryState()->ClearCheckpoint();

	// RailStagePart を先に construct（LoadTuningFromJson が内部で railStage_->LoadFromJson を呼ぶため）。
	railStage_ = std::make_unique<RailStagePart>();
	// StageEnvironment も同様（LoadTuningFromJson で root["sections"] を読む）。
	// Skybox の紐付けは実体が出来てから Initialize() で行う。
	stageEnv_ = std::make_unique<StageEnvironment>();
	// スタート演出も同様（LoadTuningFromJson で root["stageStart"] を読む）
	stageStart_ = std::make_unique<StageStartPart>();
	cloudTopIntro_ = std::make_unique<CloudTopIntro>();
	playerWings_ = std::make_unique<PlayerWingUnit>();
	playerDeath_ = std::make_unique<PlayerDeathPart>();
	flightUnit_ = std::make_unique<FlightUnitPart>();
	// 自機のビーム（チャージ射撃・ファンネル大）。調整値は Resources/Json/Tuning/PlayerBeam.json
	playerBeam_ = std::make_unique<PlayerBeamPart>();
	playerBeam_->Initialize(this);
	// 近接コンボ（24 番）。技の表は Resources/Json/Tuning/MeleeCombo.json
	meleeCombo_.Initialize(this);
	playerAnim_.Initialize(player_);
	// ストリーミングの時刻表（LoadTuningFromJson で root["streaming"] を読む。配置を読む前に要る＝どれを後回しにするか）
	streaming_ = std::make_unique<StageStreaming>();
	streaming_->Initialize(this, "STAGEPLAY");

	// 調整値の読み込み（プレイヤー生成 / RailCamera 設定より前に行う）
	// 区間 StagePlay/* は差し替えの重さの内訳（SceneManager が profile.log の scene_phase 行に出す）
	{
		LOAD_PHASE("StagePlay/Tuning");
		LoadTuningFromJson();
	}

	// HUB から渡された値は必ずここで取って捨てる（リトライなどで古い値を拾わないように）
	StageStartHandoff handoff;
	const bool fromHub = StageStartHandoff::Take(handoff);
	// HUB の降下の続き（③IN）から始めるときは、降下に要らない物の初期化を降下中のフレームへ回す（16 番 F7 C'）
	const bool deferRest = fromHub && !handoff.skipToCloudTop && !bossRetryRequested
		&& FindCommandLineValue(L"--bench-hold").empty();
	deferredInit_.clear();
	deferredInitNext_ = 0;
	deferredInitActive_ = false;
	deferredInitFrames_ = 0;
	deferredInitWallMs_ = 0.0;

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -kInitialCameraBackZ });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	// Camera の既定は near=0.1 / far=100。100m 先までしか描画されないので明示的に広げる。
	camera_->SetNearClip(cameraNearClip_);
	camera_->SetFarClip(cameraFarClip_);
	GetObject3DManager()->SetDefaultCamera(camera_.get());
	baseFovY_ = camera_->GetFovY();   // 精密射撃モードのズーム基準（通常時 FovY）

	GetSkyboxManager()->SetDefaultCamera(camera_.get());

	// Skybox 生成（平常時の空。LoadTuningFromJson 後なので保存値が反映される）
	LoadProfiler::GetInstance()->BeginPhase("StagePlay/SkyEnv");
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(GetSkyboxManager(), GetDxCore(), defaultSkyboxPath_);
	skybox_->SetColor(skyboxTint_);  // 保存済みの常時着色を適用
	GetObject3DManager()->SetEnvironmentTexture(defaultSkyboxPath_);

	// 道中の環境セクション。Cubemap が切り替わったら環境マップも追従させる。
	stageEnv_->Initialize(skybox_.get(), GetObject3DManager(), [this](const std::string& path) {
		if (GetObject3DManager()) GetObject3DManager()->SetEnvironmentTexture(path);
	});
	stageEnv_->InitializeClouds(GetDxCore());
	stageEnv_->Reset(0.0f);   // ステージ先頭のセクションを即時適用（セクション0件なら無効果）
	LoadProfiler::GetInstance()->EndPhase("StagePlay/SkyEnv");

	// STG（Rail）専用ロジック一式の初期化（レールカメラ用スプライン・向きキー・Wave定義ロード）。
	// LoadTuningFromJson() で読み込み済みの speed/rotKeys を使ってセットアップする。
	{
		LOAD_PHASE("StagePlay/RailStage");
		railStage_->Initialize(this, camera_.get());
	}

	// 雲海のレイマーチ。降下の縦穴はシーン読み込みでレールが本物に差し替わった後に RebuildCloudShaft で拾い直す
	{
		LOAD_PHASE("StagePlay/RaymarchClouds");
		// 雲の RT は描画解像度ではなく UI の仮想解像度（1600×900）× 倍率で作る。
		// 描画解像度を上げても雲の重さは据え置き（合成は UV で引くので RT の大きさが違っても合う）
		stageEnv_->InitializeRaymarchClouds(GetDxCore(), GetSRVManager(),
			WindowsApplication::kUIWidth, WindowsApplication::kUIHeight, {});
		RebuildCloudShaft();
		// --no-raymarch-clouds：雲海なしで起動（run_cloud_bench.py で雲あり/なしを比べる用）
		if (std::wstring(::GetCommandLineW()).find(L"--no-raymarch-clouds") != std::wstring::npos) {
			CloudRaymarcher* raymarcher = stageEnv_->GetCloudRaymarcher();
			CloudRaymarcher::Params params = raymarcher->GetParams();
			params.enabled = false;
			raymarcher->SetParams(params);
		}
	}

	// スタート演出（カタパルト。施設はワールドの離れた所に置く）。コクピットの灯りは HUB と同じ値（Hub.json）
	{
		LOAD_PHASE("StagePlay/StageStart");
		stageStart_->Initialize(this, camera_.get(), GetObject3DManager(), GetDxCore(), GetSkinningComputeManager(), GetSRVManager());
	}
	{
		constexpr const char* kHubTuningPath = "Resources/Json/Tuning/Hub.json";
		if (std::filesystem::exists(kHubTuningPath)) {
			const auto hub = JsonParser::ParseFile(kHubTuningPath);
			if (hub.success) stageStart_->LoadCockpitTuning(hub.value["cockpitLighting"]);
		}
	}

	phase_ = Phase::Rail;
	prevPhase_ = Phase::Rail;
	landingTimer_ = 0.0f;
	paused_ = false;
	SoundManager::GetInstance()->Play2DSoundLooped("bgm_stage", kBgmVolume);

	// ここから下は降下に要らない物。並びは今までの Initialize と同じ（後の段は前の段の結果を使う）
	// 三人称で映すカタパルトの機体（一人称の降下では映らない。止まってから V で三人称にできる）
	deferredInit_.push_back({ "StageStart/Mech", [this]() {
		stageStart_->InitializeMech(GetObject3DManager(), GetSkinningComputeManager(), GetDxCore(), GetSRVManager());
		return true;
	} });
	// 必殺技（Shatterline は崩壊の破片レンダラ＝キャプチャ反転の 3D 片も作る）
	deferredInit_.push_back({ "StagePlay/SpecialMoves", [this]() {
		stormLock_.Initialize(this, camera_.get());
		shatterline_.Initialize(this, camera_.get());
		shatterline_.InitializeRenderer(GetDxCore(), PrimitivePipeline::GetInstance()->GetSRVManager());
		return true;
	} });
	// Boss（ボス戦）専用ロジックの初期化。突入までは何もスポーンしない（Enter で生成）。
	deferredInit_.push_back({ "StagePlay/BossStage", [this]() {
		bossStage_ = std::make_unique<BossStagePart>();
		bossStage_->Initialize(this, camera_.get());
		return true;
	} });
	// ボス戦の床。PSO 等はここで作っておき、ボス戦突入時にカクつかせない
	deferredInit_.push_back({ "StagePlay/BossWater", [this]() {
		bossWater_ = std::make_unique<BossArenaWater>();
		bossWater_->Initialize(GetDxCore(), GetSRVManager(), GetObject3DManager());
		return true;
	} });
	// ボスの必殺技 GravityOrb：岩を読んでボロノイで割り、描画を用意する（ボス戦の途中で割らない）
	deferredInit_.push_back({ "StagePlay/BossGravityOrb", [this]() {
		gravityOrb_ = std::make_unique<BossGravityOrb>();
		gravityOrb_->Initialize(GetDxCore(), GetSRVManager(), GetObject3DManager(), this);
		return true;
	} });
	// 大河の水面。PSO 等はここで作り、川のデータは雲の中（loadStartSec）で別スレッドから読む
	deferredInit_.push_back({ "StagePlay/RiverWater", [this]() {
		riverWater_ = std::make_unique<RiverWater>();
		riverWater_->Initialize(GetDxCore(), GetSRVManager(), GetObject3DManager(), stageEnv_->GetCloudLayer());
		return true;
	} });
	// 自機の羽根（雲の上で展開し、レールの間は付けたまま。ボス戦では収納）
	deferredInit_.push_back({ "StagePlay/PlayerWings", [this]() {
		playerWings_->Initialize(GetObject3DManager(), GetSkinningComputeManager(), GetDxCore(), GetSRVManager());
		return true;
	} });
	// 死亡演出の昇る糸のメッシュ
	deferredInit_.push_back({ "StagePlay/PlayerDeath", [this]() {
		playerDeath_->Initialize(this);
		return true;
	} });
	// 飛行ユニット（ファンネル・バリアの形とクールタイムのバー。20_Loadout.md §3）
	deferredInit_.push_back({ "StagePlay/FlightUnit", [this]() {
		flightUnit_->Initialize(this, camera_.get(), GetSpriteManager(), GetObject3DManager(), GetDxCore());
		return true;
	} });
	deferredInit_.push_back({ "StagePlay/UI", [this]() {
		if (GetSpriteManager()) {
			pauseDimSprite_ = std::make_unique<SpriteInstance>();
			pauseDimSprite_->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "PauseDim");
			pauseDimSprite_->SetPosition({ 0.0f, 0.0f });
			pauseDimSprite_->SetSize({ static_cast<float>(WindowsApplication::kUIWidth),
				static_cast<float>(WindowsApplication::kUIHeight) });
			pauseDimSprite_->SetColor({ 0.0f, 0.0f, 0.0f, kPauseDimAlpha });
			pauseDimSprite_->Update();
		}
		CreateBossHpBarUI();
		return true;
	} });
	// レティクル（UI と合わせると 1 フレームで 13 ms かかっていたので分ける）
	deferredInit_.push_back({ "StagePlay/Reticle", [this]() {
		reticle_ = std::make_unique<Reticle>();
		reticle_->Initialize(GetSpriteManager());
		// JSON から読み込んだ外側オフセット範囲・サイズ範囲を Reticle に初期反映
		reticle_->SetLockOnSizeClampOutside(reticleOuterMinPx_, reticleOuterMaxPx_);
		reticle_->SetOuterSizeClamp(reticleOuterSizeMinPx_, reticleOuterSizeMaxPx_);
		reticleBaseStickSpeed_ = reticle_->GetStickSpeed();   // 精密射撃モードの感度低下の基準
		return true;
	} });
	// 配置（StagePlay.json）：読む → 数個ずつ作る → 自機と武器 → レールと雲の縦穴 → HP の UI（後回しのときは別々のフレーム）
	deferredInit_.push_back({ "StagePlay/SceneJsonRead", [this]() {
		deferredSceneLoaded_ = false;
		std::string autoLoadPath = "Resources/Json/Scenes/StagePlay.json";
		// --scene-json <パス>：配置を差し替えて起動（板の雲を N 枚に増やした計測用シーンなど）。パスは ASCII 前提
		if (const std::wstring w = FindCommandLineValue(L"--scene-json"); !w.empty()) {
			autoLoadPath.clear();
			for (wchar_t c : w) autoLoadPath.push_back(static_cast<char>(c));
		}
		if (std::filesystem::exists(autoLoadPath)) {
			deferredSceneLoaded_ = BeginSceneLoadFromJson(autoLoadPath);
		}
		return true;
	} });
	deferredInit_.push_back({ "StagePlay/SceneJsonApply", [this]() {
		constexpr double kSceneApplyBudgetMs = 4.0;
		return ApplySceneLoadStep(kSceneApplyBudgetMs);
	} });
	deferredInit_.push_back({ "StagePlay/SceneJsonPlayer", [this]() {
		// 配置を読んだときは OnAfterSceneLoad と同じ（自機を貼り直す）。読めなかったときは既定の自機のプレハブ
		if (deferredSceneLoaded_) RelinkPlayerAfterSceneLoad();
		if (!player_) {
			player_ = dynamic_cast<AnimatedObject3DInstance*>(InstantiatePrefab("player", { 0.0f, 0.0f, 0.0f }));
			if (player_) player_->SetName("Player");
		}

		// 武器（ボーンソケット追従）：構成は player の WeaponParams コンポーネント（プレハブ由来）から読む。
		// プレハブに weapon ブロックが無ければ既定値をコンポーネントへ seed し、Inspector で編集→
		// プレイヤーの "Save as Prefab" で player.json に永続化できるようにする（リロードで初期化されない）。
		if (player_) {
			WeaponParams wp = Gameplay::Of(player_).GetWeaponParams();
			if (wp.modelFile.empty()) {
				wp.enabled         = true;
				wp.bone            = "mixamorig:RightHand";
				// 仮：手から突き出す細長い刃（playerBullet=0.5角の立方体を scale.z=6 で約3units の刃に）。
				wp.offsetScale     = { kTempBladeThickness, kTempBladeThickness, kTempBladeLength };
				wp.offsetRotate    = { 0.0f, 0.0f, 0.0f };
				wp.offsetTranslate = { 0.0f, 0.0f, kTempBladeOffsetZ };
				wp.modelDir        = "Resources/Models/Player/Bullet";
				wp.modelFile       = "playerBullet.mesh";
				Gameplay::Of(player_).SetWeaponParams(wp);
			}
			RebuildMainWeaponModel();
			RebuildShieldModel();
		}
		return true;
	} });
	deferredInit_.push_back({ "StagePlay/SceneJsonRail", [this]() {
		if (deferredSceneLoaded_) RebindRailAfterSceneLoad();
		return true;
	} });
	deferredInit_.push_back({ "StagePlay/SceneJsonUI", [this]() {
		// HP・ゲージの UI（配置を読んだときは OnAfterSceneLoad と同じ。読めなかったときも作る）
		RebuildUiAfterSceneLoad();
		return true;
	} });
	// Wave で出る敵（と撃つ弾）のモデルを先に読む。初めて出た時の読み込みで止まらないように
	// （11 秒の最初の敵で 46ms＝14 番 §4.7）。1 フレームに 1 プレハブずつ
	deferredInit_.push_back({ "StagePlay/WavePrefabs", [this, names = std::vector<std::string>{}, next = size_t{ 0 }, collected = false]() mutable {
		if (!collected) {
			if (railStage_) names = railStage_->CollectWavePrefabs();
			collected = true;
		}
		if (next < names.size()) PreloadPrefabModels({ names[next++] });
		return next >= names.size();
	} });
	// 「ボス戦からリトライ」：Rail/Landing を飛ばしてボス戦開始時点のHP・必殺技ゲージへ復元する。
	// prevPhase_ は Phase::Rail のまま（上で設定済み）なので、Update() 初回のフェーズ遷移エッジ検出で
	// bossStage_->Enter() が自動的に走る。（後回しにするのは HUB から来たときだけなのでここは必ず Initialize の中）
	if (bossRetryRequested && haveBossCheckpoint) {
		deferredInit_.push_back({ "StagePlay/BossRetry", [this, ckPlayerHP, ckSpecialGauge]() {
			if (!player_) return true;
			phase_ = Phase::Boss;
			HP& hp = Gameplay::Of(player_).GetHP();
			hp.currentHP = std::clamp(ckPlayerHP, 0, hp.maxHP);
			specialGauge_ = std::clamp(ckSpecialGauge, 0.0f, specialGaugeMax_);
			return true;
		} });
	}
	if (!deferRest) CompleteDeferredInit();

	// スタート演出。HUB から来たら③IN の降下から、HUB を通らずに来たら（リトライ・デバッグの直接開始）雲の上から
	// （2026-10-18 ユーザー決定）。ボス戦からのリトライはスタート演出なし
	{
		LOAD_PHASE("StagePlay/StageStartBegin");
		// 射出口の向こうに見せる雲の上＝レールの t=0 のカメラ位置（雲の上の場面 S4 と同じ景色）
		if (railStage_) stageStart_->SetSkyAnchor(railStage_->SampleCameraPathAtSec(0.0f));
		if (phase_ != Phase::Boss) {
			if (fromHub && handoff.skipToCloudTop) {
				// HUB の降下中にスキップした：雲の上から（白フェードが開く）
				stageStart_->BeginFromCloudTop();
			}
			else if (fromHub) {
				stageStart_->BeginDescent(handoff);
			}
#ifdef _DEBUG
			else if (stageStart_->GetDebugStart() == StageStartPart::DebugStart::Descent) {
				stageStart_->BeginDescent(stageStart_->MakeDefaultHandoff());
			}
			else if (stageStart_->GetDebugStart() == StageStartPart::DebugStart::Inspection) {
				stageStart_->BeginInspection(stageStart_->MakeDefaultHandoff());
			}
#endif
			else {
				stageStart_->BeginFromCloudTop();
			}
			phase_ = Phase::StageStart;
			prevPhase_ = Phase::StageStart;
		}
	}

	// 自機の姿勢（ボス戦からのリトライは Stand）
	if (deferRest) {
		// 配置（SceneJson）がレールを本物に差し替えるので、射出口の向こうの雲の上の位置も取り直す
		deferredInit_.push_back({ "StagePlay/PlayerPose", [this]() {
			if (railStage_ && stageStart_) stageStart_->SetSkyAnchor(railStage_->SampleCameraPathAtSec(0.0f));
			ApplyPlayerPose();
			return true;
		} });
		deferredInitActive_ = true;
		deferredInitSkipFrame_ = true;
	}
	else {
		ApplyPlayerPose();
	}

	// --bench-hold <秒>：その秒へシークしてレールを止める（同じ画面のまま GPU 時間を測る用）
	if (const std::wstring w = FindCommandLineValue(L"--bench-hold"); !w.empty()) {
		benchHoldSec_ = static_cast<float>(std::wcstod(w.c_str(), nullptr));
		Seek(benchHoldSec_);
	}
}

void StagePlayScene::RunDeferredInitStep() {
	const DeferredInitStep& step = deferredInit_[deferredInitNext_];
	const double start = LoadProfiler::NowMs();
	const size_t phaseBegin = LoadProfiler::GetInstance()->GetPhases().size();
#ifdef _DEBUG
	// 降下中のフレームで読んだ物も先読みのマニフェストに足す（Initialize の外なので SceneManager の記録に入らない）
	if (deferredInitActive_) ScenePreloader::GetInstance()->BeginRecording();
#endif
	bool stepDone = true;
	{
		LOAD_PHASE(step.name);
		stepDone = step.run();
	}
	if (stepDone) ++deferredInitNext_;
#ifdef _DEBUG
	if (deferredInitActive_) ScenePreloader::GetInstance()->EndRecording("STAGEPLAY");
#endif
	const double ms = LoadProfiler::NowMs() - start;
	if (deferredInitActive_) {
		deferredInitWallMs_ += ms;
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=deferred_step scene=STAGEPLAY name={} frame={} wall_ms={:.2f}", step.name, deferredInitFrames_, ms));
		// 段の中で閉じた区間（SceneJson/* など。段そのものの区間は上の行と同じなので除く）
		const auto& phases = LoadProfiler::GetInstance()->GetPhases();
		for (size_t i = phaseBegin; i < phases.size(); ++i) {
			if (phases[i].name == step.name) continue;
			SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
				std::format("LOAD kind=deferred_phase scene=STAGEPLAY step={} name={} wall_ms={:.2f}", step.name, phases[i].name, phases[i].wallMs));
		}
	}
}

void StagePlayScene::UpdateDeferredInit(double budgetMs) {
	PEPPER_SCOPE("StagePlayScene::UpdateDeferredInit");
	const double start = LoadProfiler::NowMs();
	do {
		RunDeferredInitStep();
	} while (HasDeferredInit() && LoadProfiler::NowMs() - start < budgetMs);
	++deferredInitFrames_;
	if (!HasDeferredInit() && deferredInitActive_) {
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=deferred_init scene=STAGEPLAY frames={} wall_ms={:.2f}", deferredInitFrames_, deferredInitWallMs_));
		deferredInitActive_ = false;
	}
}

void StagePlayScene::CompleteDeferredInit() {
	if (!HasDeferredInit()) return;
	while (HasDeferredInit()) RunDeferredInitStep();
	if (deferredInitActive_) {
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=deferred_init scene=STAGEPLAY frames={} wall_ms={:.2f} completed_early=1", deferredInitFrames_, deferredInitWallMs_));
		deferredInitActive_ = false;
	}
}

void StagePlayScene::Finalize() {
	StopAllPlayerBeams();
	// 画面の水滴は PostEffect（シーンをまたいで残る）にあるので消しておく
	if (auto* pe = Game::GetPostEffect(); pe && pe->screenDroplet) pe->screenDroplet->Clear();
	// 必殺技関連の GPU リソースを破棄する前に GPU 完了を待つ。
	// （ウィンドウ閉じ等でシーンが落ちる際、command list がまだ参照中の resource を
	//   解放すると D3D12 OBJECT_DELETED_WHILE_STILL_IN_USE になる）
	if (GetDxCore()) {
		GetDxCore()->WaitForGpu();
	}
	// スタート演出のポイントライト・共有のマテリアル（コクピット・液晶）を返す
	if (stageStart_) stageStart_->Finalize();
	// Object3DManager はシーンをまたいで生きているので、フォグを必ず切って返す
	// （切らないと DemoScene 等に StagePlay の霧が残る）。
	if (GetObject3DManager()) GetObject3DManager()->DisableFog();
	// 雲も同じ理由で外す（CloudLayer は stageEnv_ と一緒に消える）
	if (stageEnv_) stageEnv_->Finalize();
	if (playerDeath_) playerDeath_->Finalize();
	// EffectManager は全シーン共通で、Update するのはステージ等の一部のシーンだけ。残すと次のシーン
	// （ゲームオーバー等）で止まったまま描かれ、ボスの攻撃の歪み（Distortion）が画面に掛かり続ける
	if (auto* em = EffectManager::GetInstance()) em->StopAll();
	SoundManager::GetInstance()->Stop2DSound("bgm_stage");
	SoundManager::GetInstance()->Stop2DSound("bgm_boss");
	stormLock_.Finalize();
	shatterline_.Finalize();
	if (gravityOrb_) {
		gravityOrb_->Finalize();
		gravityOrb_.reset();
	}
}

bool StagePlayScene::IsPlayerAttacking(InputActionMap* actions) const {
	// 最後に撃つ／近接を出してから moveStateDelay_ 秒以内と、近接の発生〜後隙中を「攻撃中」とみなす。
	// チャージは射撃ボタンを離している間に進むので数えない（チャージしながら移動状態で動ける）
	(void)actions;
	return timeSinceAttack_ < moveStateDelay_ || meleeCombo_.IsLocked();
}

void StagePlayScene::ApplyCameraIdleSway(float dt) {
	// World 停止中（ポーズ / Aim オーサリング等）は揺らさない。オーサリング値に揺れが混ざるのを防ぐ
	if (!camera_ || !idleSwayEnabled_ || dt <= 0.0f) return;
	idleSwayTime_ += dt * idleSwaySpeed_;
	const float t = idleSwayTime_;

	// 軸ごとに周期と位相をずらした sin を重ね、規則的に見えないうねりにする
	const Vector3 rot = camera_->GetRotate();
	camera_->SetRotate({
		rot.x + idleSwayRotAmp_.x * std::sin(t * kIdleSwayFreqPitch),
		rot.y + idleSwayRotAmp_.y * std::sin(t * kIdleSwayFreqYaw + kIdleSwayPhaseYaw),
		rot.z + idleSwayRotAmp_.z * std::sin(t * kIdleSwayFreqRoll + kIdleSwayPhaseRoll),
	});

	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const float lift = idleSwayPosAmp_ * std::sin(t * kIdleSwayFreqLift + kIdleSwayPhaseLift);
	const Vector3 pos = camera_->GetTranslate();
	camera_->SetTranslate({
		pos.x + camRot.m[1][0] * lift,
		pos.y + camRot.m[1][1] * lift,
		pos.z + camRot.m[1][2] * lift,
	});
}

namespace {
	// 発狂ライン（BossBrain::rageHpRatio と合わせる）
	constexpr float kBossRageMarkRatio = 0.5f;
}

void StagePlayScene::CreateBossHpBarUI() {
	if (!GetSpriteManager()) return;
	auto make = [&](const char* name, const Vector4& color) {
		auto s = std::make_unique<SpriteInstance>();
		s->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", name);
		s->SetColor(color);
		return s;
	};
	bossHpBarBack_     = make("BossHpBarBack",  { kBossHpBackRG, kBossHpBackRG, kBossHpBackB, kBossHpBackAlpha });
	bossHpBarDelay_    = make("BossHpBarDelay", { 1.0f, kBossHpDelayG, kBossHpDelayB, 1.0f });
	bossHpBarFill_     = make("BossHpBarFill",  { kBossHpFillR, kBossHpFillG, kBossHpFillB, 1.0f });
	bossHpBarRageMark_ = make("BossHpBarRage",  { 1.0f, 1.0f, 1.0f, kBossHpRageMarkAlpha });
}

void StagePlayScene::UpdateBossHpBarUI(float dt) {
	bossHpBarVisible_ = false;
	if (phase_ != Phase::Boss || !bossStage_ || !bossHpBarFill_) return;
	IImGuiEditable* boss = bossStage_->GetBossEntity();
	if (!boss) return;
	const HP& hp = Gameplay::Of(boss).GetHP();
	if (!hp.enabled || hp.maxHP <= 0) return;
	bossHpBarVisible_ = true;

	bossHpRatio_ = (std::clamp)(static_cast<float>(hp.currentHP) / static_cast<float>(hp.maxHP), 0.0f, 1.0f);
	// 遅延ゲージは減った分だけ遅れて追う（回復方向は即時）
	if (bossHpDelayRatio_ < bossHpRatio_) bossHpDelayRatio_ = bossHpRatio_;
	else bossHpDelayRatio_ = (std::max)(bossHpRatio_, bossHpDelayRatio_ - bossHpDelaySpeed_ * dt);

	const float w = bossHpBarWidth_;
	const float h = bossHpBarHeight_;
	const float left = (static_cast<float>(WindowsApplication::kUIWidth) - w) * 0.5f;
	const float top = bossHpBarPosY_;
	constexpr float kFrame = 3.0f;

	bossHpBarBack_->SetPosition({ left - kFrame, top - kFrame });
	bossHpBarBack_->SetSize({ w + kFrame * 2.0f, h + kFrame * 2.0f });
	bossHpBarDelay_->SetPosition({ left, top });
	bossHpBarDelay_->SetSize({ w * bossHpDelayRatio_, h });
	bossHpBarFill_->SetPosition({ left, top });
	bossHpBarFill_->SetSize({ w * bossHpRatio_, h });
	// 発狂後はゲージ色を変えて「本気モード」を示す
	const bool rage = bossHpRatio_ <= kBossRageMarkRatio;
	bossHpBarFill_->SetColor(rage ? Vector4{ kBossHpRageFillR, kBossHpRageFillG, kBossHpRageFillB, 1.0f } : Vector4{ kBossHpFillR, kBossHpFillG, kBossHpFillB, 1.0f });
	bossHpBarRageMark_->SetPosition({ left + w * kBossRageMarkRatio - 1.0f, top - kFrame });
	bossHpBarRageMark_->SetSize({ kBossHpRageMarkWidth, h + kFrame * 2.0f });

	bossHpBarBack_->Update();
	bossHpBarDelay_->Update();
	bossHpBarFill_->Update();
	bossHpBarRageMark_->Update();
}

void StagePlayScene::UpdateStageClearCapture() {
	// 撃破のフレームに頼んだ 1 枚（PostEffect::RequestStillCapture）は、その Draw で撮れている。
	// RT は PostEffect が持ち、シーンをまたいで残る。名前を付けて登録し直して、リザルトへ「有る」と渡す
	auto* pe = Game::GetPostEffect();
	if (!pe || !pe->ConsumeStillCaptureDone()) return;
	RenderTexture* rt = pe->GetStillCaptureRT();
	if (!rt) return;
	constexpr const char* kCaptureName = "StageClearCapture";
	TextureManager::GetInstance()->RegisterExternalTexture(kCaptureName, rt->GetResource(), rt->GetSRVIndex());
	GetGameSession()->GetStageClearRecord()->SetCapture(kCaptureName, rt->GetSRVIndex(), rt->GetWidth(), rt->GetHeight());
}

void StagePlayScene::DrawBossHpBarUI() {
	if (!bossHpBarVisible_ || phase_ != Phase::Boss) return;
	bossHpBarBack_->Draw();
	bossHpBarDelay_->Draw();
	bossHpBarFill_->Draw();
	bossHpBarRageMark_->Draw();

	// ラベルは下のスコア表示と同じ Flush でまとめて描く
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;
	const float left = (static_cast<float>(WindowsApplication::kUIWidth) - bossHpBarWidth_) * 0.5f;
	tr->DrawText("BOSS", { left, bossHpBarPosY_ - kBossLabelOffsetY }, kBossLabelScale, { 1.0f, 1.0f, 1.0f, 1.0f }, kTextOutline, { 0.0f, 0.0f, 0.0f, 1.0f });
}

void StagePlayScene::OpenPauseMenu() {
	CompleteDeferredInit();   // ポーズメニューの暗幕・レティクルなどを使う
	paused_ = true;
	SetPauseView(PauseView::Main);
	if (pauseDimSprite_) pauseDimSprite_->Update();
}

void StagePlayScene::ClosePauseMenu() {
	paused_ = false;
	pauseView_ = PauseView::Main;
	controlGuide_.Close();
}

void StagePlayScene::SetPauseView(PauseView view) {
	pauseView_ = view;
	if (view == PauseView::Main) {
		pauseMenu_.SetItems({ "再開", "操作ガイド", "リトライ", "ステージセレクトに戻る", "タイトルに戻る" });
	} else {
		// 誤操作で戻ってしまわないよう「いいえ」を既定にする
		pauseMenu_.SetItems({ "いいえ", "はい" });
	}
}

void StagePlayScene::UpdatePauseMenu(InputActionMap* actions) {
	pauseMenu_.SetPosition({ static_cast<float>(GetDxCore()->GetUIWidth()) * 0.5f,
		static_cast<float>(GetDxCore()->GetUIHeight()) * kPauseMenuHeightRatio });
	const UIPointer pointer = UIPointer::FromInput(GetInput());
	// 操作ガイドを開いている間はガイドだけが入力を読む（閉じたらカーソルは「操作ガイド」のまま）
	if (controlGuide_.IsOpen()) {
		controlGuide_.Update(actions, &pointer);
		return;
	}
	const VerticalMenu::Result result = pauseMenu_.Update(actions, &pointer);
	const int index = pauseMenu_.GetSelectedIndex();

	if (pauseView_ == PauseView::Main) {
		if (result == VerticalMenu::Result::Canceled) {
			ClosePauseMenu();
			return;
		}
		if (result != VerticalMenu::Result::Confirmed) return;
		switch (index) {
		case 0: // 再開
			ClosePauseMenu();
			break;
		case 1: // 操作ガイド
			controlGuide_.Open();
			break;
		case kPauseItemRetry: // リトライ
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY");
			break;
		case kPauseItemStageSelect: // ステージセレクトに戻る（確認を挟む。開く側はステージ選択の起動＝18 番 ①の IN）
			SetPauseView(PauseView::ConfirmStageSelect);
			break;
		case kPauseItemTitle: // タイトルに戻る（確認を挟む）
			SetPauseView(PauseView::ConfirmTitle);
			break;
		}
		return;
	}

	// ConfirmStageSelect / ConfirmTitle
	const bool toTitle = (pauseView_ == PauseView::ConfirmTitle);
	if (result == VerticalMenu::Result::Canceled
		|| (result == VerticalMenu::Result::Confirmed && index == 0)) {
		SetPauseView(PauseView::Main);
		pauseMenu_.SetSelectedIndex(toTitle ? kPauseItemTitle : kPauseItemStageSelect);
		return;
	}
	if (result == VerticalMenu::Result::Confirmed && index == 1) {
		SceneManager::GetInstance()->ChangeScene(toTitle ? "TITLE" : "STAGESELECT");
	}
}

bool StagePlayScene::UpdateAttract() {
	AttractMode* attract = AttractMode::GetInstance();
	if (!attract->IsEnabled()) {
		attractPausing_ = false;
		return false;
	}
	if (SceneManager::GetInstance()->IsTransitioning() || gameOverTriggered_) return false;

	const float dt = GetDxCore()->GetDeltaTime();
	if (!attractActed_) {
		if (paused_) return false;   // 人が開いたポーズの間は数えない
		// スタート演出の間は数えない（機体確認は StageStartPart が自動で進める。数えるとカタパルトの上で HP 0・ポーズになる）
		// 雲の上の回り込み（操作を受け付ける前）も数えない
		if (phase_ == Phase::StageStart || IsCloudTopIntroActive()) return false;
		attractTime_ += dt;
		if (attractTime_ < attract->GetSettings().stageActionTime) return false;
		attractActed_ = true;
		if (attract->GetSettings().dieInStage && player_) {
			// 少し遊んだら死ぬ（残りの HP 分を被弾と同じ処理で受ける）→ 死亡演出 → ゲームオーバー（19 番 §4.7 のデモ。2026-10-27 ユーザー）
			OnPlayerTakeDamage((std::max)(Gameplay::Of(player_).GetHP().currentHP, 1));
			return false;
		}
		// 少し遊んだらポーズ →「ステージセレクトに戻る」→「はい」で一周の終わり（dieInStage が false のとき）
		OpenPauseMenu();
		attractPausing_ = true;
		attractMenu_.Reset();
		return true;
	}

	// デモモードが開いたポーズ：「ステージセレクトに戻る」→「はい」をカーソルが動くのを見せながら選ぶ
	if (!attractPausing_ || !paused_) return false;
	const bool confirmView = (pauseView_ == PauseView::ConfirmStageSelect);
	const int target = confirmView ? 1 : kPauseItemStageSelect;   // 確認は「はい」、メインは「ステージセレクトに戻る」
	switch (attractMenu_.Advance(dt, attract->GetSettings().menuWait, attract->GetSettings().menuConfirmDelay)) {
	case AttractMenuTimer::Step::MoveCursor:
		if (pauseMenu_.GetSelectedIndex() != target) {
			pauseMenu_.SetSelectedIndex(target);
			SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
		}
		return true;
	case AttractMenuTimer::Step::Confirm:
		pauseMenu_.SetSelectedIndex(target);
		SoundManager::GetInstance()->Play2DSound("se_ui_decide");
		attractMenu_.Reset();
		if (confirmView) {
			attractPausing_ = false;
			// 次の周は視点を入れ替える（一人称 ↔ 三人称）
			attract->ToggleThirdPersonLap();
			SceneManager::GetInstance()->ChangeScene("STAGESELECT");
		} else {
			SetPauseView(PauseView::ConfirmStageSelect);
		}
		return true;
	default:
		return false;   // 待っている間は人の入力も読む
	}
}

void StagePlayScene::DrawPauseMenu() {
	if (pauseDimSprite_) pauseDimSprite_->Draw();

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(GetDxCore()->GetUIWidth());
	const float screenH = static_cast<float>(GetDxCore()->GetUIHeight());

	if (controlGuide_.IsOpen()) {
		controlGuide_.Draw(screenW, screenH);
		return;
	}
	const Vector4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
	const Vector4 black{ 0.0f, 0.0f, 0.0f, 1.0f };

	const char* heading = "PAUSE";
	if (pauseView_ == PauseView::ConfirmStageSelect) heading = "ステージセレクトに戻りますか？";
	if (pauseView_ == PauseView::ConfirmTitle) heading = "タイトルに戻りますか？";
	const float headingScale = (pauseView_ == PauseView::Main) ? kPauseHeadingScale : kConfirmHeadingScale;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	tr->DrawText(heading, { (screenW - headingW) * 0.5f, screenH * kPauseHeadingHeightRatio }, headingScale, white, kPauseHeadingOutline, black);

	if (pauseView_ != PauseView::Main) {
		const char* note = "現在の進行状況は失われます";
		const float noteScale = 0.9f;
		const float noteW = tr->MeasureWidth(note, noteScale);
		tr->DrawText(note, { (screenW - noteW) * 0.5f, screenH * kPauseHeadingHeightRatio + kConfirmNoteOffsetY }, noteScale,
			{ kConfirmNoteGray, kConfirmNoteGray, kConfirmNoteGray, 1.0f }, kTextOutline, black);
	}

	pauseMenu_.SetPosition({ screenW * 0.5f, screenH * kPauseMenuHeightRatio });
	pauseMenu_.Draw();
}

void StagePlayScene::Update() {
	// 画面が覆われている間は止める（開いている間は見えているので動かす）。ただし最初の1回だけは通す。
	// Initialize の時点ではレールカメラ・カメラ行列・Skybox・物体の行列が一度も書かれておらず、
	// 開いていく画面に初期値の絵が映るため
	if (SceneManager::GetInstance()->IsTransitionCovering() && firstUpdateDone_) {
		return;
	}
	firstUpdateDone_ = true;

	auto* actions = GetInput()->GetActionMap();
	if (!actions) return;

	// デモモード（展示用の自動進行）
	if (UpdateAttract()) return;

	// ポーズ（開閉したフレームとポーズ中はゲームを進めない）
	if (HandlePauseInput(actions)) return;

#ifdef _DEBUG
	if (HandleDebugShortcuts()) return;
#endif

	// 後回しにした初期化：③IN の降下の間だけ少しずつ進め、それ以外の段階へ進んだら残りを全部済ませる
	AdvanceDeferredInit();

	// ----- スタート演出（③IN〜雲の上）。演出中はレール・自機の操作・HUD を動かさない -----
	// 終わったフレームはそのまま下のレールへ進む（1 フレームでもカタパルトのカメラのまま描かないように）
	// スキップの白フェードは演出が終わった後（レール）まで続くので、段階に関係なく進める
	if (stageStart_) stageStart_->UpdateOverlay((std::min)(GetScaledDeltaTime(TimeGroup::UI), 1.0f / kMinDeltaTimeFps));
	if (phase_ == Phase::StageStart && UpdateStageStart()) {
		return;
	}
	// クリアタイム（23 番 §2.1）：レールに出てからボス撃破まで。実時間（スローで伸ばさない）。
	// ポーズ中とスタート演出中は上で return するので足されない。撃破で MarkCleared した後は足さない
	GetGameSession()->GetStageClearRecord()->AddPlayTime(GetScaledDeltaTime(TimeGroup::UI));
	// 雲の上の回り込みの間は操作を受け付けない（16 番 §1。レールは t=0 から進める）。バインドの無い入力マップ＝何も押されていない
	static InputActionMap kNoInput;
	// 着陸（谷の出口 → 闘技場）と、撃破 → STAGE CLEAR の間も同じ
	if (IsCloudTopIntroActive() || IsPlayerDying() || phase_ == Phase::Landing
		|| (phase_ == Phase::Boss && clearSeq_.IsActive())) actions = &kNoInput;

	UpdateStagePhase(actions);

	// フェーズ遷移エッジ検出（自動遷移・F4 手動の両方をここで一元処理）
	if (phase_ != prevPhase_) OnStagePhaseChanged();

	UpdateBossBattle();

	// 拾った物の表示（実時間。スローでも普通の速さで出入りする）
	lootLog_.Update(GetScaledDeltaTime(TimeGroup::UI), *GetGameSession()->GetLootLedger());

	// 闘技場の水（足場の床・海）。World 時間で進める（スロー中は波も遅い）
	if (bossWater_ && bossStage_ && IsBossArenaVisible()) {
		bossWater_->SetArena(bossStage_->GetArenaCenter(), bossStage_->GetGroundY(),
			bossStage_->GetWaterRadius(), bossStage_->GetSeaY());
		bossWater_->Update(GetScaledDeltaTime(TimeGroup::World));
	}

	UpdateBaseCamera(actions);
	UpdatePlayerMovement(actions);

	UpdateStageEnvironment();

	// Skybox は Update 時点のカメラ VP を定数バッファに焼き込むので、必殺技/精密/ジャスト回避カメラの
	// 回り込み（UpdatePlayerMovement の中）が済んだ後で更新する（前に焼くと背景だけ回り込み前の VP で固定される）
	UpdateSkyboxAfterCamera();

	// Scene の動的エンティティ群（プレハブ生成物含む）の Update
	UpdateDynamicObjects();
	UpdateDynamicAnimated(GetScaledDeltaTime());
	// 判定の姿勢は、このフレームに描く自機の行列と同じ translate / rotate で作る（照準で回すのはこの後＝次のフレームの描画）
	UpdatePlayerColliderPose();
	UpdateDynamicPrimitives();
	UpdateDynamicSprites();

	UpdatePlayerAttachments();

#ifdef _DEBUG

	DrawDynamicSplinesDebug();

#endif // _DEBUG

	// 全シーン共通の EffectManager + GPUParticle を更新。
	// 実 delta（unscaled）を渡す。各エフェクト成分は EffectDef の TimeGroup（既定 World）で内部スケールされる。
	// 必殺技の全停止中でも進めたい演出は TimeGroup=Effect にしておけば動く（Effect グループ既定 1.0）。
	{ PEPPER_SCOPE("StagePlay::UpdateGlobalEffects"); UpdateGlobalEffects(camera_.get(), GetDxCore()->GetDeltaTime()); }

	// ジャスト回避演出のタイマ更新。受付期間/フェードは実時間（UI グループ）で計測する
	// （World をスローにしても受付 3 秒は実時間で一定にしたいため）。
	UpdateJustDodgeEffect(GetScaledDeltaTime(TimeGroup::UI));

	// HP 回復（ジャスト回避ストックを消費）
	UpdateHeal(actions);

	UpdateReticle();

	UpdateAimTarget();

	// ゲーム時間が止まっている時はゲームロジック（射撃／弾の進行）をスキップ。
	// TimeScale=0（"Pause" ボタン等）でシーンに敵を配置するときに弾が湧き続けないようにする。
	const float worldDt = GetScaledDeltaTime(TimeGroup::World);
	const bool gameFrozen = worldDt <= kFrozenDeltaTime;

	UpdateShooting(actions, gameFrozen);

	UpdateCombatAndSpecial(actions, worldDt, gameFrozen);

	// 自機のアニメ：このフレームの行動（射撃・回避・被弾・必殺技…）を受けて鳴らすクリップを決める。
	// アニメの時間は自機のアニメと同じ自機の時間（Player）で進める（1 回きりの長さを合わせる）
	SyncPlayerAnim(GetScaledDeltaTime(TimeGroup::Player));

	// Wave発火・敵コントローラ/移動更新・キル記録（STG専用。Landing/Boss突入後は発火しない）。
	if (phase_ == Phase::Rail && railStage_) {
		// 雲の上の回り込みの間も、敵の出現位置はレールのカメラで決める
		if (cloudTopIntro_) cloudTopIntro_->UseRailPose(camera_.get());
		railStage_->UpdateWaveAndEnemies(worldDt);
		if (cloudTopIntro_) cloudTopIntro_->UseIntroPose(camera_.get());
	}

	// LightningRuntime テスト：Active なら毎フレ Update。始終点はテストパネル側で更新済み
	if (lightningTest_ && lightningTest_->IsActive()) {
		lightningTest_->Update(camera_.get(), GetDxCore()->GetDeltaTime());
	}

	WriteFrameStateLog();
}

// Pause アクションでポーズを開閉する。開閉したフレームとポーズ中は true（ゲームを進めない）
bool StagePlayScene::HandlePauseInput(InputActionMap* actions) {
	// Pause アクションでポーズトグル。トグルしたフレームはメニュー入力を読まない
	// （同じキーが MenuCancel 等に割り当てられていても二重に反応させないため）
	if (actions->IsTriggered(static_cast<int>(Action::Pause))) {
		if (paused_ && controlGuide_.IsOpen()) {
			// 操作ガイドからはポーズの項目へ戻る（ゲームには戻さない）
			controlGuide_.Close();
			SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
		}
		else if (paused_) ClosePauseMenu();
		else         OpenPauseMenu();
		return true;
	}
	if (paused_) {
		UpdatePauseMenu(actions);
		return true;
	}
	return false;
}

// 後回しにした初期化：③IN の降下の間だけ 1 フレーム数 ms ずつ進め、それ以外の段階へ進んだら残りを全部済ませる
void StagePlayScene::AdvanceDeferredInit() {
	if (!HasDeferredInit()) return;
	constexpr double kDeferredInitBudgetMs = 4.0;
	if (deferredInitSkipFrame_) {
		// 差し替えのフレームは Initialize の分だけで重いので、段は次のフレームから
		deferredInitSkipFrame_ = false;
	} else if (phase_ == Phase::StageStart && stageStart_ && stageStart_->GetState() == StageStartPart::State::Descent) {
		UpdateDeferredInit(kDeferredInitBudgetMs);
	} else {
		CompleteDeferredInit();
	}
}

#ifdef _DEBUG
// DEBUG 専用ショートカット（F2=リザルト / F3=HUB / F4=段階送り）。シーンを切り替えたら true
bool StagePlayScene::HandleDebugShortcuts() {
	// DEBUG専用ショートカット（StagePlayScene完成時に削除）
	auto* kb = GetInput()->GetKeyboard();
	if (kb->TriggerKey(DIK_F2)) {
		SceneManager::GetInstance()->ChangeScene("RESULT");
		return true;
	}
	if (kb->TriggerKey(DIK_F3)) {
		SceneManager::GetInstance()->ChangeScene("HUB");
		return true;
	}
	if (kb->TriggerKey(DIK_F4)) {
		switch (phase_) {
		case Phase::Rail:    phase_ = Phase::Landing; break;
		case Phase::Landing: phase_ = Phase::Boss;    break;
		case Phase::Boss:    phase_ = Phase::Rail;    break;
		case Phase::StageStart:
			if (stageStart_) stageStart_->Skip();
			phase_ = Phase::Rail;
			prevPhase_ = Phase::Rail;
			break;
		}
	}
	return false;
}
#endif

// ステージ進行ステートマシン（Rail → Landing → Boss）。遷移の条件だけを見る（入った時の処理は OnStagePhaseChanged）
void StagePlayScene::UpdateStagePhase(InputActionMap* actions) {
	// ----- ステージ進行ステートマシン（Rail → Landing → Boss）-----
	if (phase_ == Phase::Rail && railStage_) {
		// SeekMax 到達で Landing へ自動遷移（このフレームはレール位置を直前ポーズで凍結）
		// --bench-hold 中はレールを進めない（dt=0 なら位置も時刻も止まる）。
		// 死亡演出の間も止める（19 番 G2。次のウェーブも来ない。ワールドに残る粒が後ろへ流れて見えない）
		const float railDt = (benchHoldSec_ >= 0.0f || IsPlayerDying()) ? 0.0f : GetScaledDeltaTime();
		if (railStage_->UpdateCamera(actions, railDt, seekMaxSec_)) {
			phase_ = Phase::Landing;
			landingTimer_ = 0.0f;
		} else {
			// レールカメラが毎フレーム姿勢を上書きした直後に揺れを足す（積算しない）
			ApplyCameraIdleSway(GetScaledDeltaTime(TimeGroup::World));
		}
	} else if (phase_ == Phase::Landing) {
		// 着陸：谷の出口から海の上の闘技場へ飛んで降りる（カメラと自機の位置は BossStagePart が書く）。
		// 降り終わったら Boss へ。BossStagePart が無ければ従来どおり滞在秒で進める
		landingTimer_ += GetScaledDeltaTime();
		bool landed = landingTimer_ >= landingDuration_;
		if (bossStage_) {
			if (!bossStage_->IsLandingActive() && !bossStage_->HasLandingTouchedDown()) bossStage_->BeginLanding();
			landed = bossStage_->UpdateLanding(GetScaledDeltaTime());
			if (bossStage_->ConsumeLandingTouchdown()) {
				// 足場に着いた：羽根をしまって立つ・足元の水に波
				if (playerWings_) playerWings_->Stow();
				ApplyPlayerPose();
				playerAnim_.Trigger(PlayerAnimState::LandingTouchdown);
				if (bossWater_) bossWater_->EmitSplash(bossStage_->GetPlayerStartPos());
			}
		}
		if (bossPreloadReady_ && landed) {
			phase_ = Phase::Boss;
		}
	} else if (phase_ == Phase::Boss) {
		// ボス戦本体の tick は下のフェーズ遷移検出の後で行う（Enter を先に走らせるため）。
	}
}

// 段階が変わったフレームに 1 回：前の段階の片付けと、次の段階に入る処理
void StagePlayScene::OnStagePhaseChanged() {
	// 飛行ユニットの技はフェーズをまたがない（スローは戻し、ファンネル・バリアは消す。クールタイムも 0）
	if (flightUnit_) flightUnit_->Reset();
	StopAllPlayerBeams();
	// レールを抜けたら、まだ飛んでいる落とし物は拾ったことにする（以降は動かさないため）
	if (prevPhase_ == Phase::Rail && railStage_) railStage_->CollectAllLoot();
	// 着陸の始まり：谷に残った敵を片付ける（降りている間に撃たれない・敵の参照を残さない。Boss 突入と同じ）
	if (phase_ == Phase::Landing) {
		ClearBossRuntimeState();
		nearestEnemy_    = nullptr;
		lockedEnemy_     = nullptr;
		jdCounterTarget_ = nullptr;
	}
	if (phase_ == Phase::Boss) {
		bossBattleActive_ = true;            // 既存スカイボックスクロスフェードを自動発火
		bossFacingInit_   = false;           // ボス戦開始時に向きを取り直す（初回フレームで再サンプル）
		// Rail側の残留敵を掃除してからボスへ突入する。SeekMax到達/F4/Seek直後の自動遷移では
		// Rail の Enemy が生き残ったまま（enemyControllers_ も含め）Boss へ持ち越されており、
		// ①EnemyControllerがボス戦中も動き続ける、②nearestEnemy_/lockedEnemy_ がその残留敵を
		// 指したままジャスト回避対象(jdCounterTarget_)に渡り、変な位置を向いて回転→敵が破棄
		// された時にダングリング参照でクラッシュ、という不具合の根本原因だった。
		ClearBossRuntimeState();
		// 上の掃除で破棄される実体を指している可能性がある生ポインタも合わせてクリア
		// （Enemy タグはボス戦中一度も再更新されないため、放置すると即ダングリングのまま残り続ける）。
		nearestEnemy_    = nullptr;
		lockedEnemy_     = nullptr;
		jdCounterTarget_ = nullptr;
		if (gravityOrb_) gravityOrb_->Abort(); // リトライで入り直したとき、前の必殺技の岩・暗さを残さない
		if (bossStage_) bossStage_->Enter(); // 地面・ボス・AI をスポーン（1回）
		// 地上のボス戦は直立・羽根は収納（先から消える）
		ApplyPlayerPose();
		if (playerWings_) playerWings_->Stow();
		SoundManager::GetInstance()->Stop2DSound("bgm_stage");
		SoundManager::GetInstance()->Play2DSoundLooped("bgm_boss", kBgmVolume);
		bossHpRatio_ = 1.0f;
		bossHpDelayRatio_ = 1.0f;
		// ボス戦開始時点のHP・必殺技ゲージをスナップショット（「ボス戦からリトライ」用）。
		// F4デバッグショートカットでの手動突入やリトライ再突入時も同様に保存し直される。
		if (player_) {
			GetGameSession()->GetBossRetryState()->SaveCheckpoint(
				Gameplay::Of(player_).GetHP().currentHP, specialGauge_);
		}
	} else if (prevPhase_ == Phase::Boss) {
		bossBattleActive_ = false;           // 空を平常時へ戻す
		if (gravityOrb_) gravityOrb_->Abort(); // 必殺技の岩・暗さ・閃光を消す（ボスと一緒にステートも消える）
		if (bossStage_) bossStage_->Reset(); // ボス・地面・コントローラを破棄
		if (clearSeq_.IsActive()) SetTimeScale(TimeGroup::World, 1.0f);
		clearSeq_.Reset();
		ApplyPlayerPose();
	}
	prevPhase_ = phase_;
}

// ボス戦の毎フレーム（ボス AI・HP バー・撃破 → STAGE CLEAR の段取り）
void StagePlayScene::UpdateBossBattle() {
	// ボス戦 tick（ボスAI更新＋撃破掃除）。Enter 済みのこのフレームから駆動される。
	if (phase_ != Phase::Boss || !bossStage_) return;
	// 自機の死亡演出の間、ボスは今の攻撃を終えたら次の行動を選ばない（19 番 G3。プレビューが終われば戻る）
	bossStage_->SetBossStandDown(IsPlayerDying());
	bossStage_->Update(GetScaledDeltaTime(TimeGroup::World));
	// 必殺技 GravityOrb（段取りはボスの攻撃ステートが始める。終わった後も消えていく破片と閃光のために回す）
	if (gravityOrb_) gravityOrb_->Update(GetScaledDeltaTime(TimeGroup::World));
	UpdateBossHpBarUI(GetScaledDeltaTime(TimeGroup::UI));

	// 撃破の後の段取り（23 番 §4 B）。時間は実時間。撃破のフレームの Begin より前に進める（撮るフレームは時刻 0 のまま）
	UpdateStageClearCapture();
	if (clearSeq_.IsActive()) {
		clearSeq_.Update(GetScaledDeltaTime(TimeGroup::UI));
		if (clearSeq_.ConsumeCollapse()) bossStage_->CollapseDefeatedBoss();   // 手足が落ち、本体の大きな爆発
		Vector3 burstPos;
		std::string burstFx;
		while (clearSeq_.PopBurst(burstPos, burstFx)) {
			if (auto* em = EffectManager::GetInstance()) em->Play(burstFx, burstPos);
			SoundManager::GetInstance()->Play2DSound("se_explode", kExplodeSeVolume);
		}
		SetTimeScale(TimeGroup::World, clearSeq_.GetWorldTimeScale());
		if (clearSeq_.ConsumeFinished()) {
			SetTimeScale(TimeGroup::World, 1.0f);
			SceneManager::GetInstance()->ChangeScene("RESULT");
		}
	}

	// ボス撃破検出：スコア加点・撃破数カウント → 撃破の段取り（ヒットストップ → … → STAGE CLEAR → リザルト）。
	// 残った敵の弾は消す（クリアの間は被弾もしない）
	if (bossStage_->ConsumeBossDefeated()) {
		GetGameSession()->GetStageClearRecord()->MarkCleared();   // クリアタイムを止める（23 番 §2）
		GetGameSession()->GetScoreManager()->AddScore(bossStage_->GetLastBossScoreValue());
		GetGameSession()->GetScoreManager()->AddKill();
		GetGameSession()->GetLootLedger()->AddBossReward();   // ボスの報酬（右下に出て、リザルトの合計に入る）
		KillAllEnemyBullets();
		// 飛行ユニットのスロー・揺れは持ち越さない（世界の時間はこの段取りが持つ）
		if (flightUnit_) flightUnit_->Reset();
		StopAllPlayerBeams();
		if (camera_) camera_->StopShake();
		Vector3 bossCenter = bossStage_->GetBossStartPos();
		bossStage_->GetDefeatedBossCenter(bossCenter);
		clearSeq_.Begin(bossCenter, bossStage_->GetGroundY(), *camera_, GetGameSession()->GetStageClearRecord()->GetClearTime());
		playerAnim_.Trigger(PlayerAnimState::Victory);
		SetTimeScale(TimeGroup::World, clearSeq_.GetWorldTimeScale());   // ヒットストップ
		// このフレームは HUD を描かないので、ポストエフェクトの後の絵をそのまま撮る（リザルトの背景）
		if (clearSeq_.ConsumeCaptureRequest()) {
			if (auto* pe = Game::GetPostEffect()) pe->RequestStillCapture();
		}
	}
}

// 自機を置く前の基準カメラ（精密射撃の FOV・カメラ行列・デバッグカメラ）
void StagePlayScene::UpdateBaseCamera(InputActionMap* actions) {
	// 精密射撃モード（FOV ズーム・感度・演出）。FOV を camera_->Update() の前に反映する
	UpdatePrecisionAim(actions, GetScaledDeltaTime());

	// FovY を基準値に戻してから Update する。プレイヤー移動のクリップ判定は基準カメラの
	// VP で行う必要があるため（前フレームのズーム済み FovY が残ると画面端で投影が拡大し、移動が固まる）。
	// 実際のズーム描画は後段の ApplyPrecisionCamera() が改めて反映する。
	camera_->SetFovY(baseFovY_);
	camera_->Update();
	UpdateDebugCameraIfActive();
}

// 自機の移動（ボス戦は地上、それ以外はカメラ空間のオフセット）と、自機を置いた後で上書きするカメラ
void StagePlayScene::UpdatePlayerMovement(InputActionMap* actions) {
	if (!player_) return;
	// プレイヤーの移動・入力は Player グループ（既定 1.0）で動かす＝ジャスト回避のワールドスロー中も等倍。
	const float dt = GetScaledDeltaTime(TimeGroup::Player);

	Vector2 moveDelta = ReadMoveInput();
	// 必殺技発動中・雲の上の回り込み・死亡演出の間は入力ロック（移動・慣性をゼロ化）
	if (specialActive_ || IsCloudTopIntroActive() || IsPlayerDying()) {
		moveDelta = { 0.0f, 0.0f };
		playerVelocity_ = { 0.0f, 0.0f };
	}
	playerMoveInput_ = moveDelta;   // スラスターの強弱（ボス戦も同じ。2026-10-20 ユーザー：ボス戦を作るときに合わせて直す）

	if (phase_ == Phase::Boss && bossStage_) {
		// ボス戦：地上ワールド移動＋フリールック/ロックオン/ターゲットの3モードカメラ
		UpdateBossPlayerMovement(actions, moveDelta, dt);
	} else if (phase_ == Phase::Landing && bossStage_) {
		// 着陸中は自機の位置を BossStagePart::UpdateLanding が書く（カメラ基準の配置はしない）
	} else {
		UpdateRailPlayerMovement(actions, moveDelta, dt);
	}
}

// 移動入力（WASD / 左スティック）を -1..1 で
Vector2 StagePlayScene::ReadMoveInput() const {
	Vector2 moveDelta{ 0.0f, 0.0f };
	auto* kb = GetInput()->GetKeyboard();
	if (kb->PushKey(DIK_A)) moveDelta.x -= 1.0f;
	if (kb->PushKey(DIK_D)) moveDelta.x += 1.0f;
	if (kb->PushKey(DIK_W)) moveDelta.y += 1.0f;
	if (kb->PushKey(DIK_S)) moveDelta.y -= 1.0f;
	if (auto* pad = GetInput()->GetController(); pad && pad->IsConnected()) {
		const auto& ls = pad->GetLeftStick();
		moveDelta.x += ls.x * ls.magnitude;
		moveDelta.y += ls.y * ls.magnitude;
	}
	moveDelta.x = std::clamp(moveDelta.x, -1.0f, 1.0f);
	moveDelta.y = std::clamp(moveDelta.y, -1.0f, 1.0f);
	return moveDelta;
}

// ----- ボス戦の自機（地上） -----

// 地上の移動・回避・分身カウンター → 向き → カメラ
void StagePlayScene::UpdateBossPlayerMovement(InputActionMap* actions, const Vector2& moveDelta, float dt) {
	// ジャスト回避のタイミング窓/無敵は UpdateDodge に委ねる（弾に対するジャスト回避検証のため）。
	// ※追加回避（Down派生）のダッシュは bossStage_->ApplyDashImpulse 経由で groundVelocity_ に入る
	//   （TriggerCloneCounterAction 側で呼ぶ。通常回避 Dodge の playerVelocity_ とは別経路）。
	UpdateDodge(actions, moveDelta, GetScaledDeltaTime(TimeGroup::World));
	// ジャスト回避スロー受付中は地上の自由移動を禁止（残存速度は慣性で減速し停止）。
	// STG と同方針。近接派生中（jdMeleeCameraActive_）はワープ/攻撃で位置を直接管理するため
	// 地上移動そのものを止める（UpdatePlayerGroundMovement を呼ばない）。
	// 近接の技の攻撃中の移動（24 番 §8）：free＝攻撃中でも遅くしない（倍率は技の speed）／
	// forward・back＝スティックに関係なく、向いている方へ技の速さで進む・下がる（向きも変えない）。
	// 近接の発生〜スキ・溜めの間で、技に移動の区間が無い所は止まる（スティックで滑らない）
	const MeleeMotion meleeMotion = meleeCombo_.GetCurrentMotion();
	const bool meleeFree = meleeMotion.mode == MeleeMotionMode::Free;
	const bool meleeForced = meleeMotion.mode == MeleeMotionMode::Forward || meleeMotion.mode == MeleeMotionMode::Back;
	const bool meleeRooted = meleeCombo_.IsLocked() && !meleeFree && !meleeForced;
	const float attackScale = meleeFree ? meleeMotion.speed : (IsPlayerAttacking(actions) ? attackMoveSpeedScale_ : 1.0f);
	const float moveScale = attackScale
		* (flightUnit_ ? flightUnit_->GetMoveSpeedScale() : 1.0f)
		* ShieldMoveSpeedScale()
		* meleeCombo_.GetBuffMoveScale();
	Vector2 groundMove = (justDodgeActive_ || meleeRooted) ? Vector2{ 0.0f, 0.0f } : Vector2{ moveDelta.x * moveScale, moveDelta.y * moveScale };
	if (meleeForced && !justDodgeActive_ && bossStage_->GetPlayerMoveSpeed() > kTinyEpsilon) {
		// 向いている方（後ろへ下がる時は反対）を、地上の移動の入力（カメラから見た右・奥）に直す。
		// 入力の大きさ＝技の速さ ÷ いつもの最大の速さ
		const float sign = (meleeMotion.mode == MeleeMotionMode::Back) ? -1.0f : 1.0f;
		const float dirX = std::sin(bossFacingYaw_) * sign;
		const float dirZ = std::cos(bossFacingYaw_) * sign;
		Vector3 gfwd, gright;
		bossStage_->ComputeGroundBasis(gfwd, gright);
		const float amount = meleeMotion.speed / bossStage_->GetPlayerMoveSpeed();
		groundMove = { (dirX * gright.x + dirZ * gright.z) * amount, (dirX * gfwd.x + dirZ * gfwd.z) * amount };
	}
	if (!jdMeleeCameraActive_) {
		bossStage_->UpdatePlayerGroundMovement(player_, dt, groundMove);
	}
	// 自機のアニメの走りの向き（PlayerAnimPart が待機・走り 4 方向を選ぶ）
	playerAnimGroundMove_ = jdMeleeCameraActive_ ? Vector2{ 0.0f, 0.0f } : GroundMoveToPlayerLocal(groundMove);
	// 分身カウンター派生の進行（近接のワープ/攻撃発生、地上文脈向け）。ワープで player_ の
	// ワールド座標を直接書き換えるので、以降のカメラ・向き計算より前に呼ぶ。
	UpdateJustDodgeCounterAction(GetScaledDeltaTime(TimeGroup::UI));
	// 必殺技 GravityOrb の岩（浮き上がり・落下中も含む）に重ならないよう横へ押し出す。移動・ワープの後に 1 回
	if (gravityOrb_) {
		Vector3 pushed = player_->GetTranslate();
		gravityOrb_->ResolvePlayerCollision(pushed);
		player_->SetTranslate(pushed);
	}
	// 大技のスーパーアーマー中は吹き飛ばないので、ボスの体にめり込まないよう水平に押し出す
	if (meleeCombo_.HasSuperArmor()) PushPlayerOutOfBoss();
	const Vector3 bWorldPos = player_->GetTranslate();

	// 近接の技の間は始まった時の向きのまま（自動で進む・下がる間も、スティックで向きが回ると進む向きがずれる）。
	// 向きを変えられるのは free の区間（Y2・Y4 の回転）だけ
	const bool meleeFacingLocked = meleeCombo_.IsLocked() && !meleeFree;
	// 撃ってから moveStateDelay_ 秒以内と精密射撃の間は、弾の飛ぶ向き（カメラの向き）を向く（2026-11-28 ユーザー：
	// カメラ側を向いたまま撃つと弾だけ前へ出ていた）。近接の技で向きを固定している間はそちらが先
	const bool aimFacing = !meleeFacingLocked && (precisionHeld_ || timeSinceShot_ < moveStateDelay_);
	UpdateBossPlayerFacing(meleeFacingLocked ? Vector2{ 0.0f, 0.0f } : moveDelta, bWorldPos, dt, aimFacing);

	// 分身プレビュー等の本体追従（ジャスト回避演出用）。
	UpdateJustDodgeClones(actions, moveDelta, GetScaledDeltaTime(TimeGroup::UI));

	UpdateBossCamera(actions, bWorldPos, dt);
}

// 自機の向き（yaw のみ）を更新。実際の SetRotate は後段の照準（UpdateAimTarget）で適用する。
void StagePlayScene::UpdateBossPlayerFacing(const Vector2& moveDelta, const Vector3& playerPos, float dt, bool aimFacing) {
	// ・ジャスト回避スロー中：攻撃元（ボス）方向へ向く。
	// ・射撃中・精密射撃中（aimFacing）：カメラの向き（地面に沿った前＝弾の飛ぶ向き）を向く。
	// ・通常時：移動入力方向を向く（入力が無ければ直前の向きを保持）。
	float targetYaw = bossFacingYaw_;
	bool  hasTarget  = false;
	if (justDodgeCounterActive_) {
		// ジャスト回避の派生中：ボスの方へすぐ向く（技を出す時にも向き直る＝OnComboAttackStarted）
		if (CounterTargetYaw(targetYaw)) { bossFacingYaw_ = targetYaw; bossFacingInit_ = true; }
		return;
	}
	if (justDodgeActive_ && jdCounterTarget_) {
		if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) {
			const float dx = tp->x - playerPos.x;
			const float dz = tp->z - playerPos.z;
			if (dx * dx + dz * dz > kSquaredEpsilon) { targetYaw = std::atan2(dx, dz); hasTarget = true; }
		}
	} else if (aimFacing && bossStage_) {
		Vector3 gfwd, gright;
		bossStage_->ComputeGroundBasis(gfwd, gright);
		targetYaw = std::atan2(gfwd.x, gfwd.z);
		hasTarget = true;
	} else {
		hasTarget = MoveInputToBossYaw(moveDelta, targetYaw);
	}
	if (!bossFacingInit_) { bossFacingYaw_ = targetYaw; bossFacingInit_ = true; }
	else if (hasTarget) {
		float d = targetYaw - bossFacingYaw_;
		while (d >  kPi) d -= kTwoPi;
		while (d < -kPi) d += kTwoPi;
		const float a = (bossFacingTurnSmoothTime_ > kTinyEpsilon)
			? (1.0f - std::exp(-dt / bossFacingTurnSmoothTime_)) : 1.0f;
		bossFacingYaw_ += d * a;
	}
}

void StagePlayScene::PushPlayerOutOfBoss() {
	if (!player_ || !bossStage_) return;
	IImGuiEditable* boss = bossStage_->GetBossEntity();
	const Vector3* bp = boss ? boss->GetEditableTranslate() : nullptr;
	if (!bp) return;
	// ボスの体（球の判定）と自機の判定の半径を足した円の外へ、水平だけで押し出す
	const Collider& bc = Gameplay::Of(boss).GetCollider();
	const float minDist = bc.radius + Gameplay::Of(player_).GetCollider().radius;
	Vector3 pos = player_->GetTranslate();
	const float dx = pos.x - (bp->x + bc.offset.x);
	const float dz = pos.z - (bp->z + bc.offset.z);
	const float d2 = dx * dx + dz * dz;
	if (d2 >= minDist * minDist) return;
	float nx = 0.0f, nz = 0.0f;
	if (d2 > kSquaredEpsilon) {
		const float d = std::sqrt(d2);
		nx = dx / d;
		nz = dz / d;
	} else {
		// 真ん中に重なった時は自機の後ろへ
		nx = -std::sin(bossFacingYaw_);
		nz = -std::cos(bossFacingYaw_);
	}
	pos.x = bp->x + bc.offset.x + nx * minDist;
	pos.z = bp->z + bc.offset.z + nz * minDist;
	player_->SetTranslate(pos);
}

IImGuiEditable* StagePlayScene::CounterFocusEntity() const {
	// ボス戦はボスの本体（回避した攻撃の持ち主は腕・拳・突進の判定などで、本体と位置がずれる）
	if (phase_ == Phase::Boss && bossStage_ && bossStage_->GetBossEntity()) return bossStage_->GetBossEntity();
	return jdCounterTarget_;
}

bool StagePlayScene::CounterTargetYaw(float& outYaw) const {
	if (!player_) return false;
	IImGuiEditable* target = CounterFocusEntity();
	const Vector3* tp = target ? target->GetEditableTranslate() : nullptr;
	if (!tp) return false;
	const Vector3 pp = player_->GetTranslate();
	const float dx = tp->x - pp.x;
	const float dz = tp->z - pp.z;
	if (dx * dx + dz * dz <= kSquaredEpsilon) return false;
	outYaw = std::atan2(dx, dz);
	return true;
}

bool StagePlayScene::MoveInputToBossYaw(const Vector2& moveDelta, float& outYaw) const {
	if (!bossStage_) return false;
	const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
	if (mlen <= kNearZero) return false;
	Vector3 gfwd, gright;
	bossStage_->ComputeGroundBasis(gfwd, gright);
	const float wx = gright.x * moveDelta.x + gfwd.x * moveDelta.y;
	const float wz = gright.z * moveDelta.x + gfwd.z * moveDelta.y;
	if (wx * wx + wz * wz <= kSquaredEpsilon) return false;
	outYaw = std::atan2(wx, wz);
	return true;
}

// ボス戦のカメラ（右スティック・マウスの look、L1 の短押し/長押し）と、その上に重ねる演出カメラ
void StagePlayScene::UpdateBossCamera(InputActionMap* actions, const Vector3& playerPos, float dt) {
	// --- カメラ look 入力（右スティック + マウス相対）。reticle は中央固定なので発射＝カメラ前方 ---
	float camStickX = 0.0f, camStickY = 0.0f;
	if (auto* pad = GetInput()->GetController(); pad && pad->IsConnected()) {
		const auto& rs = pad->GetRightStick();
		camStickX = rs.x * rs.magnitude;
		camStickY = rs.y * rs.magnitude;
	}
	float camMouseDx = 0.0f, camMouseDy = 0.0f;
	bool  mouseActive = false;
#ifdef _DEBUG
	if (auto* vp = ImGuiManager::GetInstance()->GetViewportWindow(); vp && vp->IsHovered()) mouseActive = true;
#else
	mouseActive = true; // Release はスワップチェーン直描画＝常にゲーム画面
#endif
	if (mouseActive && !GetUseDebugCamera()) {
		if (auto* m = GetInput()->GetMouse()) {
			camMouseDx = static_cast<float>(m->GetDeltaX());
			camMouseDy = static_cast<float>(m->GetDeltaY());
		}
	}

	// --- L1 / マウス中ボタン：短押し=ターゲットスナップ / 長押し=Free⇔LockOn トグル ---
	bool camBtn = actions->IsPressed(static_cast<int>(Action::LockCamButton));
	if (auto* m = GetInput()->GetMouse(); m && m->IsButtonPressed(MouseInput::Button::Middle)) camBtn = true;
	// 死亡演出の間はカメラを動かさない（右スティック・マウス・ボタンは actions を通らないのでここで切る。19 番 G3）
	if (IsPlayerDying() || clearSeq_.IsActive()) {
		camStickX = camStickY = camMouseDx = camMouseDy = 0.0f;
		camBtn = false;
	}
	const float uiDt = GetScaledDeltaTime(TimeGroup::UI);
	if (camBtn) {
		if (bossCamBtnHold_ < 0.0f) { bossCamBtnHold_ = 0.0f; bossCamBtnLongFired_ = false; }
		else                        { bossCamBtnHold_ += uiDt; }
		if (!bossCamBtnLongFired_ && bossCamBtnHold_ >= bossCamLongPressSec_) {
			bossStage_->ToggleCamMode();     // 長押し確定
			bossCamBtnLongFired_ = true;
		}
	} else {
		if (bossCamBtnHold_ >= 0.0f && !bossCamBtnLongFired_) {
			bossStage_->RequestTargetSnap(); // 短押し確定（離した瞬間）
		}
		bossCamBtnHold_ = -1.0f;
		bossCamBtnLongFired_ = false;
	}

	if (!GetUseDebugCamera()) {
		bossStage_->UpdateCamera(dt, camStickX, camStickY, camMouseDx, camMouseDy); // ベースカメラ
		ApplyPrecisionCamera(playerPos);  // 以降は各 state が立っている時だけ上書き
		ApplyJustDodgeCamera(playerPos);
		ApplyJustDodgeMeleeCamera(playerPos);
		ApplySpecialCamera(playerPos);
		// 死亡演出（19 番 G3）：ボスのカメラの位置から自機の斜め前・低めへ回り込み、ほどけ始めから見上げる
		if (IsPlayerDying()) {
			playerDeath_->ApplyCamera(*camera_);
			camera_->Update();
		}
		// 撃破の段取り：その場 → ボスの斜め前へ寄る → 崩壊で少し引く（いちばん最後に上書きする）
		if (clearSeq_.IsActive()) clearSeq_.ApplyCamera(*camera_);
	}
}

// ----- シューティングの自機（カメラ空間のオフセット） -----

// 慣性付きの移動 → 回避 → 画面端・床で止める → ワールドに置く → 演出カメラ
void StagePlayScene::UpdateRailPlayerMovement(InputActionMap* actions, const Vector2& moveDelta, float dt) {
	// 分身選択中も WASD/スティックは通常移動として有効（方向選択はアクションボタン側で受ける）。
	// 下=追加回避を選んだ瞬間の WASD を回避方向に使うため、ここでは zero 化しない。

	// 目標速度（入力 × 最大速度）に対して指数減衰で接近させる＝慣性
	// 飛行ユニットの常時の効果（機動系は移動が速くなる）。
	// 近接の技の自由移動（24 番 §8）の間は攻撃中でも遅くしない。前後の自動移動は STG では使わない（奥へ進めないので振りだけ）。
	// それ以外の近接の発生〜スキ・溜めの間は止まる（スティックで滑らない）
	const MeleeMotion meleeMotion = meleeCombo_.GetCurrentMotion();
	const bool meleeFree = meleeMotion.mode == MeleeMotionMode::Free;
	const bool meleeRooted = meleeCombo_.IsLocked() && !meleeFree;
	const float moveScale = (meleeFree ? meleeMotion.speed
		: (IsPlayerAttacking(actions) ? attackMoveSpeedScale_ : 1.0f))
		* (flightUnit_ ? flightUnit_->GetMoveSpeedScale() : 1.0f)
		* ShieldMoveSpeedScale()
		* meleeCombo_.GetBuffMoveScale();
	Vector2 targetVel = meleeRooted ? Vector2{ 0.0f, 0.0f } : Vector2{
		moveDelta.x * playerMoveSpeed_.x * moveScale,
		moveDelta.y * playerMoveSpeed_.y * moveScale,
	};
	// ジャスト回避スローの受付中は自由移動を禁止する（残存速度は慣性で減速し停止）。
	// 追加回避（下派生）のダッシュは playerVelocity_ へのインパルス、近接派生（上/右）は
	// worldPos 直接上書きで別経路のため、ここでの targetVel ゼロ化は影響しない。
	if (justDodgeActive_) {
		targetVel = { 0.0f, 0.0f };
	}
	float alpha = (playerSmoothTime_ > kTinyEpsilon)
		? (1.0f - std::exp(-dt / playerSmoothTime_))
		: 1.0f;
	playerVelocity_.x += (targetVel.x - playerVelocity_.x) * alpha;
	playerVelocity_.y += (targetVel.y - playerVelocity_.y) * alpha;

	// 回避（ダッシュ＋無敵窓）。playerVelocity_ にインパルスを足すので、以降のクリップ判定で
	// 画面外には出ない。iframe や CD は World dt（スロー中は遅く減る＝iframe が体感で延びる）。
	// 追加回避（Down派生）でジャスト回避スロー中に発動した場合、無敵がそのままスロー分延びる挙動。
	UpdateDodge(actions, moveDelta, GetScaledDeltaTime(TimeGroup::World));

	// 分身カウンター派生のアクション進行（近接の詰め寄り/戻り、追加回避の戻し管理など）。
	// playerInputOffset_ を直接書き換えるので、クリップ判定の手前で呼ぶ。
	UpdateJustDodgeCounterAction(GetScaledDeltaTime(TimeGroup::UI));

	MoveRailPlayerInsideScreen(dt);
	PushRailPlayerBackAfterDodge(dt);

	const Vector3 worldPos = ComputeRailPlayerWorldPos();
	player_->SetTranslate(worldPos);
	// 回転は照準ロジックで後段にてセット（camera 同期は廃止）

	// 分身の本体追従＋分裂アニメ＋方向選択（ジャスト回避中のみ動作）。
	// actions を渡すとアクションボタン (Melee*/Dodge/Heal) で方向確定する。
	UpdateJustDodgeClones(actions, moveDelta, GetScaledDeltaTime(TimeGroup::UI));

	ApplyRailOverrideCameras(worldPos);
}

// 速度で動かした候補のオフセットを、コライダーが画面端（+ マージン）と水面の床を越えない軸だけ採用する。
// 拒否した軸は速度もゼロに（壁にぶつかった扱い）
void StagePlayScene::MoveRailPlayerInsideScreen(float dt) {
	const Vector2 candidate = {
		playerInputOffset_.x + playerVelocity_.x * dt,
		playerInputOffset_.y + playerVelocity_.y * dt,
	};
	const Matrix4x4& camWorld = camera_->GetWorldMatrix();
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	// プレイヤーの回転＝カメラ回転前提でコライダーの軸を構築
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 axes[3] = {
		{ camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] },
		{ camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] },
		{ camRot.m[2][0], camRot.m[2][1], camRot.m[2][2] },
	};
	const Collider& col = Gameplay::Of(player_).GetCollider();
	const Vector3 he = ColliderHalfExtents(col);
	auto placeAt = [&](float ox, float oy) {
		return TransformCoordinate({ playerLocalOffset_.x + ox, playerLocalOffset_.y + oy, playerLocalOffset_.z }, camWorld);
	};

	// 許容枠。追加回避（Down）派生中は一時拡張し、戻し中(jdDodgeReturning_)は時間で線形に縮める。
	// ただし「プレイヤー現在位置を含む最小マージン」を下回らないようにする（戻し中に範囲外で固まる事故を防ぐ）。
	Vector2 effMargin = playerClipMargin_;
	if (jdMeleeCameraActive_) {
		// 近接派生中はクリップ制限を実質無効化（Approach で対象敵手前まで自由に移動できるように）
		effMargin = { kMeleeClipMarginFree, kMeleeClipMarginFree };
	} else if (jdDodgeMarginActive_) {
		if (jdDodgeReturning_ && jdDodgeReturnDuration_ > kTinyEpsilon) {
			const float t = (std::min)(jdDodgeMarginTimer_ / jdDodgeReturnDuration_, 1.0f);
			effMargin.x = jdDodgeExpandedMargin_.x + (playerClipMargin_.x - jdDodgeExpandedMargin_.x) * t;
			effMargin.y = jdDodgeExpandedMargin_.y + (playerClipMargin_.y - jdDodgeExpandedMargin_.y) * t;
		} else {
			effMargin = jdDodgeExpandedMargin_;
		}
		const Vector3 curWorld = placeAt(playerInputOffset_.x, playerInputOffset_.y);
		const float cwx = curWorld.x * vp.m[0][0] + curWorld.y * vp.m[1][0] + curWorld.z * vp.m[2][0] + vp.m[3][0];
		const float cwy = curWorld.x * vp.m[0][1] + curWorld.y * vp.m[1][1] + curWorld.z * vp.m[2][1] + vp.m[3][1];
		const float cww = curWorld.x * vp.m[0][3] + curWorld.y * vp.m[1][3] + curWorld.z * vp.m[2][3] + vp.m[3][3];
		if (cww > kTinyEpsilon) {
			const float needX = (std::max)(0.0f, std::fabs(cwx / cww) - 1.0f) + kClipMarginSlack;
			const float needY = (std::max)(0.0f, std::fabs(cwy / cww) - 1.0f) + kClipMarginSlack;
			effMargin.x = (std::max)(effMargin.x, needX);
			effMargin.y = (std::max)(effMargin.y, needY);
		}
	}

	// 候補位置でコライダーの 8 つの角が許容枠に収まるか
	auto isInsideClip = [&](float ox, float oy) {
		const Vector3 p = placeAt(ox, oy);
		const Vector3 center{ p.x + col.offset.x, p.y + col.offset.y, p.z + col.offset.z };
		float maxX = 0.0f, maxY = 0.0f;
		if (!ColliderMaxAbsNdc(center, axes, he, vp, maxX, maxY)) return false;
		return maxX <= 1.0f + effMargin.x && maxY <= 1.0f + effMargin.y;
	};

	// 水面の床（大河の上だけ。窓の外は -FLT_MAX）。コライダーのワールド Y 方向の下端で比べる。
	const float playerFloorY = railStage_ ? railStage_->GetPlayerFloorY() : -FLT_MAX;
	auto colliderBottomY = [&](float ox, float oy) {
		const Vector3 p = placeAt(ox, oy);
		const float extentY = std::fabs(camRot.m[0][1]) * he.x
		                    + std::fabs(camRot.m[1][1]) * he.y
		                    + std::fabs(camRot.m[2][1]) * he.z;
		return p.y + col.offset.y - extentY;
	};

	// 軸ごとに採否判定（X 単独・Y 単独で押し戻し）
	Vector2 next = playerInputOffset_;
	bool blockedX = false;
	bool blockedY = false;
	if (isInsideClip(candidate.x, playerInputOffset_.y)) {
		next.x = candidate.x;
	} else {
		playerVelocity_.x = 0.0f;
		blockedX = true;
	}
	// 床より下へ動く候補は不可（上へ動くのは常に可＝床の下に入っても抜け出せる）
	const bool sinksBelowFloor = candidate.y < playerInputOffset_.y
		&& colliderBottomY(next.x, candidate.y) < playerFloorY;
	if (isInsideClip(next.x, candidate.y) && !sinksBelowFloor) {
		next.y = candidate.y;
	} else {
		playerVelocity_.y = 0.0f;
		blockedY = true;
	}
	playerInputOffset_ = next;

	// カメラが降りて自機が床の下に入ったら、床まで持ち上げる（カメラの上方向で戻す）
	if (playerFloorY > -FLT_MAX) {
		const float deficit = playerFloorY - colliderBottomY(playerInputOffset_.x, playerInputOffset_.y);
		const float upY = camRot.m[1][1];
		if (deficit > 0.0f && upY > kMinCameraUpY) {
			playerInputOffset_.y += deficit / upY;
			if (playerVelocity_.y < 0.0f) playerVelocity_.y = 0.0f;
		}
	}

	// MOVE_BLOCKED: 非ブロック→ブロックの立ち上がりエッジでのみ出力（SUNDAY のスタック判定材料）
	if (blockedX && !prevMoveBlockedX_) {
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
			"MOVE_BLOCKED axis=X x=" + std::to_string(playerInputOffset_.x) + " y=" + std::to_string(playerInputOffset_.y));
	}
	if (blockedY && !prevMoveBlockedY_) {
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
			"MOVE_BLOCKED axis=Y x=" + std::to_string(playerInputOffset_.x) + " y=" + std::to_string(playerInputOffset_.y));
	}
	prevMoveBlockedX_ = blockedX;
	prevMoveBlockedY_ = blockedY;
}

// 追加回避の戻り：コライダー 8 角の最大 NDC で軸ごとに判定し、通常マージンを超えている軸だけ内側へ押し戻す
// （ダッシュで取った別軸位置は維持）。全体が枠内に戻ったら派生を終える
void StagePlayScene::PushRailPlayerBackAfterDodge(float dt) {
	if (!jdDodgeMarginActive_ || !jdDodgeReturning_) return;
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	const Vector3 worldPos = TransformCoordinate(
		{ playerLocalOffset_.x + playerInputOffset_.x,
		  playerLocalOffset_.y + playerInputOffset_.y, playerLocalOffset_.z }, camera_->GetWorldMatrix());
	const Matrix4x4 rot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 axes[3] = {
		{ rot.m[0][0], rot.m[0][1], rot.m[0][2] },
		{ rot.m[1][0], rot.m[1][1], rot.m[1][2] },
		{ rot.m[2][0], rot.m[2][1], rot.m[2][2] },
	};
	const Collider& col = Gameplay::Of(player_).GetCollider();
	const Vector3 center{ worldPos.x + col.offset.x, worldPos.y + col.offset.y, worldPos.z + col.offset.z };
	float maxAbsX = 0.0f, maxAbsY = 0.0f;
	if (!ColliderMaxAbsNdc(center, axes, ColliderHalfExtents(col), vp, maxAbsX, maxAbsY)) return;

	// 通常マージンより内側のセーフティ位置まで押し戻す（境界ピッタリで次フレ固定回避）
	const float xTgt = 1.0f + playerClipMargin_.x - kClipMarginSlack;
	const float yTgt = 1.0f + playerClipMargin_.y - kClipMarginSlack;
	const bool outX = maxAbsX > xTgt;
	const bool outY = maxAbsY > yTgt;
	if (outX || outY) {
		const float pushSpeed = 30.0f;
		if (outX) playerInputOffset_.x += ((playerInputOffset_.x > 0.0f) ? -1.0f : 1.0f) * pushSpeed * dt;
		if (outY) playerInputOffset_.y += ((playerInputOffset_.y > 0.0f) ? -1.0f : 1.0f) * pushSpeed * dt;
		playerVelocity_ = { 0.0f, 0.0f };
	} else {
		// コライダー全体がセーフティ枠内 → 派生終了（次フレの通常クリップでも確実に内側）
		jdDodgeMarginActive_ = false;
		jdDodgeReturning_    = false;
		EndJustDodgeCounterAction();
	}
}

// オフセットからワールド位置を出す。近接派生中は敵の手前まで詰め寄る位置で上書きし、
// 体の中心まわりに回ったように足元の位置をずらす
Vector3 StagePlayScene::ComputeRailPlayerWorldPos() {
	const Matrix4x4& camWorld = camera_->GetWorldMatrix();

	Vector3 worldPos = TransformCoordinate(
		{ playerLocalOffset_.x + playerInputOffset_.x,
		  playerLocalOffset_.y + playerInputOffset_.y,
		  playerLocalOffset_.z },
		camWorld);

	// 近接派生中はワールド位置を直接上書き（Approach/Active/Return）。
	// camera-local 平面では z が固定で melee 判定が遠方の敵に届かないため、
	// ワールド空間で敵手前まで詰め寄って攻撃→元位置へ戻す。
	if (jdMeleeCameraActive_) {
		auto easeOutCubic = [](float t) {
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
		};
		if (jdActionPhase_ == JdActionPhase::Approach) {
			float t = (jdMeleeApproachDuration_ > kTinyEpsilon)
				? (jdActionPhaseTimer_ / jdMeleeApproachDuration_) : 1.0f;
			const float e = easeOutCubic(t);
			worldPos = {
				jdApproachWorldStart_.x + (jdApproachWorldGoal_.x - jdApproachWorldStart_.x) * e,
				jdApproachWorldStart_.y + (jdApproachWorldGoal_.y - jdApproachWorldStart_.y) * e,
				jdApproachWorldStart_.z + (jdApproachWorldGoal_.z - jdApproachWorldStart_.z) * e,
			};
		} else if (jdActionPhase_ == JdActionPhase::Active) {
			worldPos = jdApproachWorldGoal_;
		} else if (jdActionPhase_ == JdActionPhase::Return) {
			float t = (jdMeleeReturnDuration_ > kTinyEpsilon)
				? (jdActionPhaseTimer_ / jdMeleeReturnDuration_) : 1.0f;
			const float e = easeOutCubic(t);
			worldPos = {
				jdApproachWorldGoal_.x + (jdReturnWorldPos_.x - jdApproachWorldGoal_.x) * e,
				jdApproachWorldGoal_.y + (jdReturnWorldPos_.y - jdApproachWorldGoal_.y) * e,
				jdApproachWorldGoal_.z + (jdReturnWorldPos_.z - jdApproachWorldGoal_.z) * e,
			};
		}
		playerVelocity_ = { 0.0f, 0.0f };
	}

	// 原点（足元）まわりに回すと体が振れるので、体の中心まわりに回ったように位置をずらす。
	// 自機の行列は照準で回す前（UpdateDynamicAnimated）に作られるので、ずらしもその時点の回転で掛ける
	{
		const Vector3& off = Gameplay::Of(player_).GetCollider().offset;
		const Matrix4x4 m = MakeCameraRotateMatrix(player_->GetRotate());
		const Vector3 rotated{
			off.x * m.m[0][0] + off.y * m.m[1][0] + off.z * m.m[2][0],
			off.x * m.m[0][1] + off.y * m.m[1][1] + off.z * m.m[2][1],
			off.x * m.m[0][2] + off.y * m.m[1][2] + off.z * m.m[2][2] };
		worldPos = { worldPos.x + off.x - rotated.x, worldPos.y + off.y - rotated.y, worldPos.z + off.z - rotated.z };
	}
	return worldPos;
}

// 自機を置いた後に上書きするカメラ（精密射撃・ジャスト回避・必殺技・雲の上・死亡演出）
void StagePlayScene::ApplyRailOverrideCameras(const Vector3& worldPos) {
	// 精密射撃：プレイヤー配置（基準カメラ）後に表示カメラを肩越し位置へ寄せる。
	// プレイヤーは基準カメラのまま配置済みなので移動範囲はズーム前と同じ。
	if (!GetUseDebugCamera()) {
		ApplyPrecisionCamera(worldPos);
		// ジャスト回避の引き（精密カメラの後＝上書きして優先）
		ApplyJustDodgeCamera(worldPos);
		// 近接派生中はさらに上書き（プレイヤー左斜め後ろからのカメラに切替）
		ApplyJustDodgeMeleeCamera(worldPos);
		// 必殺技発動中の引き（最優先）
		ApplySpecialCamera(worldPos);
	}
	// 雲の上の場面：レールのカメラ（自機はこの前に置いた）を、自機の右上前方から回り込むカメラで上書きする（F6）
	if (cloudTopIntro_ && cloudTopIntro_->IsActive()) {
		cloudTopIntro_->Apply(camera_.get(), worldPos, (std::min)(GetScaledDeltaTime(), 1.0f / kMinDeltaTimeFps));
	}
	// 死亡演出（19 番 G2）：レールの姿勢から自機へ寄り、昇る糸を追って見上げる。自機はカメラ基準で置くので、置いた後で上書きする
	// （先に動かすと自機もカメラについて動き、寄れない）
	if (IsPlayerDying()) {
		playerDeath_->ApplyCamera(*camera_);
		camera_->Update();
	}
}

// 道中の環境（空・ライト・フォグのセクション、大河、画面の水滴、区間ごとのストリーミング）
void StagePlayScene::UpdateStageEnvironment() {
	// 道中の環境セクション（空/平行光源/フォグ）を経過秒で駆動する。
	// Skybox の演出トリガー（下）より前に呼ぶことで、必殺技の暗転がこのフレームの
	// セクション着色を上書きできる（暗転優先）。必殺技中は着色だけスキップする。
	if (stageEnv_ && phase_ == Phase::Rail && railStage_) {
		stageEnv_->Update(railStage_->GetStageSeconds(), !specialActive_);
	}
	// 大河の水面：時刻でロードを始め、波を進める（波は World 時間＝スロー中は流れも遅い）。
	// 自機が水面すれすれを飛ぶと後ろに航跡が立つ（13 番 §8）
	if (riverWater_ && phase_ == Phase::Rail && railStage_) {
		const Vector3 playerPosition = player_ ? player_->GetTranslate() : Vector3{};
		riverWater_->Update(railStage_->GetStageSeconds(), GetScaledDeltaTime(TimeGroup::World),
			player_ ? &playerPosition : nullptr);
	}
	// 画面の水滴：川（水面すれすれ＝重力）と雲の縦穴（放射）の濡れ具合の大きい方を、1 か所でフィルタへ渡す。
	// 着陸・ボス戦は 0（乾いて消える）（13 番 §9.9）
	if (auto* pe = Game::GetPostEffect(); pe && pe->screenDroplet) {
		const bool rail = phase_ == Phase::Rail && railStage_;
		const float river = (rail && riverWater_) ? riverWater_->GetDropletTarget() : 0.0f;
		const float cloud = (rail && stageEnv_ && camera_) ? stageEnv_->GetCloudDropletTarget(camera_->GetTranslate()) : 0.0f;
		pe->screenDroplet->Update(GetScaledDeltaTime(TimeGroup::World), (std::max)(river, cloud),
			cloud > river ? ScreenDropletEffect::Mode::Radial : ScreenDropletEffect::Mode::Gravity);
	}
	// 区間ごとのストリーミング：時刻で裏で読み、決めた秒に作る。着陸・ボス戦（ボス戦からのリトライを含む）は全部作る
	if (streaming_) {
		if (phase_ == Phase::Rail && railStage_) {
			streaming_->Update(railStage_->GetStageSeconds());
		} else if (phase_ == Phase::Landing || phase_ == Phase::Boss) {
			streaming_->CreateAll();
		}
	}
}

// Skybox の演出（必殺技の暗転・ボス戦の空）と更新。カメラが確定した後に呼ぶ
void StagePlayScene::UpdateSkyboxAfterCamera() {
	// カメラが確定した後に Skybox を更新する（VP 焼き込みのため、回り込みを背景にも反映）。
	if (!skybox_) return;
	// --- Skybox 演出トリガー（フラグの立ち上がり/立ち下がりを見て呼ぶ）---
	// 必殺技中は空を暗転、終了で常時の着色へ戻す。
	if (specialActive_ && !prevSpecialActive_) {
		skybox_->FadeColor(specialSkyboxDarkColor_, specialSkyboxFadeIn_); // 暗転
	} else if (!specialActive_ && prevSpecialActive_) {
		// 復帰先はセクションが有効ならその着色。無ければ従来どおり常時着色。
		const Vector4& restore =
			(stageEnv_ && stageEnv_->HasSections()) ? stageEnv_->GetCurrentTint() : skyboxTint_;
		skybox_->FadeColor(restore, specialSkyboxFadeOut_);                // 復帰
	}
	prevSpecialActive_ = specialActive_;

	// ボス戦突入で空をクロスフェード（離脱で平常時の空へ戻す）。
	if (bossBattleActive_ && !prevBossBattleActive_) {
		skybox_->BlendTo(bossSkyboxPath_, bossSkyboxBlendDuration_);
	} else if (!bossBattleActive_ && prevBossBattleActive_) {
		skybox_->BlendTo(defaultSkyboxPath_, bossSkyboxBlendDuration_);
	}
	prevBossBattleActive_ = bossBattleActive_;

	// クロスフェード/着色補間はヒットストップの影響を受けない実時間で進める
	if (stageEnv_) stageEnv_->UpdateClouds(GetScaledDeltaTime(TimeGroup::UI), camera_->GetTranslate());
	skybox_->Update(GetScaledDeltaTime(TimeGroup::UI));
}

// 自機のスケルトンが更新された後に、骨に付ける物（武器・盾・飛行ユニット・羽根・スラスター）と死亡演出を進める
void StagePlayScene::UpdatePlayerAttachments() {
	hasWeaponMuzzle_ = false;
	// 武器をプレイヤーの手ボーンへ追従させる。
	// UpdateDynamicAnimated でプレイヤーのスケルトンが更新済みなのでここで行列を引く。
	// grip（握りの微調整）× hand（ボーンのワールド）が武器の最終ワールド行列。
	if (weapon_ && player_ && Gameplay::Of(player_).GetWeaponParams().enabled) {
		// ソケットの追従先/オフセットは player の WeaponParams（Inspector 編集）から毎フレ同期。
		const WeaponParams& wp = Gameplay::Of(player_).GetWeaponParams();
		const MainWeaponDef* mw = GetGameSession()->GetLoadoutState()->GetMainWeapon();
		const bool gripFromWeapon = mw && !mw->modelFile.empty();
		weaponSocket_.target           = player_;  // プレイヤー再生成（シーン再構築）に毎フレ追従
		weaponSocket_.jointName        = wp.bone;
		weaponSocket_.offset.translate = gripFromWeapon ? mw->gripTranslate : wp.offsetTranslate;
		weaponSocket_.offset.rotate    = gripFromWeapon ? mw->gripRotate : wp.offsetRotate;
		weaponSocket_.offset.scale     = gripFromWeapon ? mw->gripScale : wp.offsetScale;
		const Matrix4x4 world = weaponSocket_.World();
		weapon_->SetWorldMatrixOverride(world);
		// ギズモ/Inspector でも追従を確認できるよう、平行移動成分を transform_ にも反映する
		// （描画は override 側を使うので見た目には影響しない。デバッグ可視化用）
		weapon_->SetTranslate(weaponSocket_.Position());
		weapon_->Update();
		// 銃口（武器のモデルのローカル → ワールド）。撃つ処理はこの後のフレームで使う
		if (gripFromWeapon) {
			weaponMuzzle_ = TransformMatrix(mw->muzzleOffset, world);
			hasWeaponMuzzle_ = true;
#ifdef _DEBUG
			DebugDraw::Cross(weaponMuzzle_, kMuzzleDebugCrossSize, { 0.2f, 1.0f, 0.3f, 1.0f });
#endif
		}
		// ビームの刃：近接の発生〜後隙とコンボの受付の間だけ伸びる（刃の位置・向きは武器のモデルのローカル）
		if (weaponBlade_ && mw && mw->blade && gripFromWeapon) {
			const Matrix4x4 local = MakeAffineMatrix(Transform{ mw->bladeScale, mw->bladeRotate, mw->bladeOffset });
			// Y+B の強化中は刃を赤く（形・透けは武器のまま、色だけ替える）
			if (meleeCombo_.GetBuffLevel() > 0) {
				BeamBladeAppearance look = mw->bladeLook;
				look.coreColor = meleeCombo_.GetBuffBladeCoreColor();
				look.edgeColor = meleeCombo_.GetBuffBladeEdgeColor();
				weaponBlade_->SetAppearance(look);
			} else {
				weaponBlade_->SetAppearance(mw->bladeLook);
			}
			weaponBlade_->SetWorld(Multiply(local, world));
			weaponBlade_->SetActive(weaponBladePreview_ || meleeCombo_.IsEngaged());
			weaponBlade_->Update(GetScaledDeltaTime(TimeGroup::Player));
		}
	}
	// 盾を左手ボーンへ追従させる（20 番 §2。握りは付けている盾の grip）
	if (shield_ && player_) {
		if (const ShieldDef* sd = GetGameSession()->GetLoadoutState()->GetShield(); sd && !sd->modelFile.empty()) {
			shieldSocket_.target           = player_;
			shieldSocket_.jointName        = sd->bone;
			shieldSocket_.offset.translate = sd->gripTranslate;
			shieldSocket_.offset.rotate    = sd->gripRotate;
			shieldSocket_.offset.scale     = sd->gripScale;
			shield_->SetWorldMatrixOverride(shieldSocket_.World());
			shield_->SetTranslate(shieldSocket_.Position());
			shield_->Update();
			// ビームシールド：発生装置の位置（盾のローカル）を手の行列に重ねる
			if (beamShield_ && sd->beam) {
				Matrix4x4 emitter = MakeIdentity4x4();
				emitter.m[3][0] = sd->beamOffset.x;
				emitter.m[3][1] = sd->beamOffset.y;
				emitter.m[3][2] = sd->beamOffset.z;
				beamShield_->SetAppearance(sd->beamLook);
				beamShield_->SetWorld(Multiply(emitter, shieldSocket_.World()));
				beamShield_->Update(GetScaledDeltaTime(TimeGroup::UI));
			}
		}
	}
	// 飛行ユニットのバックパック（Spine2）と、それに付ける物（攻撃系のファンネル。20 番 §3.1）
	if (flightUnit_ && flightUnit_->IsInitialized() && player_) flightUnit_->UpdateModels(player_);

	// 自機の照らし方（IBL を切って平行光源で）と点滅の発光
	ApplyPlayerLook();

	// 自機の羽根を Spine2 に付ける（スケルトンが更新された後）。デバッグで演出を飛ばしてレールへ来たとき（F4・シーク）は開いた形で付ける
	// （死亡演出で隠した羽根は出し直さない）
	// バックパックを背負う飛行ユニット（攻撃系など）を付けている間は羽根を出さない（展開・焼け落ちも含めて隠したまま）
	if (playerWings_ && flightUnit_ && flightUnit_->UsesBackpack()) {
		if (playerWings_->GetState() != PlayerWingUnit::State::Hidden) playerWings_->Hide();
	} else if (playerWings_) {
		if (phase_ == Phase::Rail && !IsCloudTopIntroActive() && !IsPlayerDying()
			&& playerWings_->GetState() == PlayerWingUnit::State::Hidden) {
			playerWings_->ShowDeployed();
		}
		playerWings_->SetBlinkGlow(playerBlinkGlow_);
		playerWings_->Update(player_, GetScaledDeltaTime());
	}

	// 自機のスラスター（スケルトンが更新された後）。自機を描かない間（スタート演出・ジャスト回避の分身）は止める
	// ボス戦の死亡演出の間は咳き込んで止まる（19 番 G3。PlayerDeathPart の予定表）
	if (player_ && player_->GetVisible()) {
		MechThrusters::Sizes deathSizes;
		if (playerDeath_ && playerDeath_->GetThrusterSizes(deathSizes)) {
			playerThrusters_.Update(player_, deathSizes);
		} else {
			playerThrusters_.Update(player_, playerThrusters_.FromMoveAxis(playerMoveInput_.x, playerMoveInput_.y));
		}
	} else {
		playerThrusters_.StopAll();
	}

	// 死亡演出（スケルトンが更新された後。関節の位置からほどける高さと糸の根元を決める）。
	// ジャスト回避のワールドスローに引きずられないよう Player グループで進める
	if (playerDeath_) playerDeath_->Update(GetScaledDeltaTime(TimeGroup::Player));
}

// レティクルの位置（ボス戦・雲の上は中央固定、それ以外はマウス/スティック）
void StagePlayScene::UpdateReticle() {
	// ボス戦は reticle を画面中央固定（右スティック/マウスはカメラ操作へ回している）。
	// これで発射方向＝カメラ forward になり、狙いはカメラを向けて中央に捉える方式になる。
	// ResetToCenter で位置を中央にした上で Update(入力なし) を呼ぶ＝スプライトの描画位置/サイズ/色を反映。
	// （Update を呼ばないと position_ は中央でもスプライト側が STG 最終位置のまま残る）
	if (reticle_ && (phase_ == Phase::Boss || IsCloudTopIntroActive())) {
		// 雲の上の回り込みの間も中央（操作を受け付けない。終わったら中央から動かせる）
		reticle_->ResetToCenter();
		reticle_->Update(false, Vector2{ 0.0f, 0.0f }, false, nullptr, GetScaledDeltaTime(TimeGroup::Player));
	} else
	// レティクル：Viewport 内でマウスが動いたときだけワープ（ImGui 操作と衝突させない）
	if (reticle_) {
		bool mouseHovered = false;
		bool mouseMoved   = false;
		Vector2 mouseClient{ 0.0f, 0.0f };
#ifdef _DEBUG
		auto* vp = ImGuiManager::GetInstance()->GetViewportWindow();
		if (vp && vp->IsHovered()) {
			ImVec2 ip = vp->GetImageScreenPos();
			ImVec2 is = vp->GetImageScreenSize();
			ImVec2 ms = ImGui::GetMousePos();
			if (is.x > 0.0f && is.y > 0.0f) {
				mouseHovered = true;
				mouseClient.x = (ms.x - ip.x) * (static_cast<float>(WindowsApplication::kUIWidth) / is.x);
				mouseClient.y = (ms.y - ip.y) * (static_cast<float>(WindowsApplication::kUIHeight) / is.y);
				ImVec2 d = ImGui::GetIO().MouseDelta;
				mouseMoved = (d.x != 0.0f) || (d.y != 0.0f);
			}
		}
#else
		// Release ビルド：クライアント座標から黒帯を引いて内部解像度へ換算（枠の外でもレティクル側で端に止まる）
		auto* m = GetInput()->GetMouse();
		if (m) {
			m->GetUIPosition(mouseClient.x, mouseClient.y);
			mouseHovered = true;
			mouseMoved = (m->GetDeltaX() != 0) || (m->GetDeltaY() != 0);
		}
#endif
		// レティクルはプレイヤー操作なので Player グループ dt で更新する。
		// World スロー（ジャスト回避／Shatterline のチャージ等）の影響を受けず一定速度で動かすため。
		reticle_->Update(mouseHovered, mouseClient, mouseMoved,
			GetInput()->GetController(), GetScaledDeltaTime(TimeGroup::Player));
	}
}

// 照準（狙う敵・発射先・自機の向き・ロックオン表示）
void StagePlayScene::UpdateAimTarget() {
	// ----- 照準ターゲット計算（プレイヤー回転 + 発射方向の元） -----
	if (!player_ || !reticle_ || !camera_) return;
	const Vector2 rp = reticle_->GetPosition();
	const float ndcX = (rp.x / float(WindowsApplication::kUIWidth)) * 2.0f - 1.0f;
	const float ndcY = 1.0f - (rp.y / float(WindowsApplication::kUIHeight)) * 2.0f;
	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	const Vector3 farWorld = TransformCoordinate(Vector3{ ndcX, ndcY, 1.0f }, invVP);
	const Vector3 camPos = camera_->GetTranslate();
	Vector3 rayDir{ farWorld.x - camPos.x, farWorld.y - camPos.y, farWorld.z - camPos.z };
	const float rayLen = std::sqrt(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
	if (rayLen > kSquaredEpsilon) {
		rayDir.x /= rayLen; rayDir.y /= rayLen; rayDir.z /= rayLen;
	}

	// 敵レイヒット判定（Enemy タグ、Sphere collider のみ）
	bool hitEnemy = false;
	// bestT は「最も近い候補までの距離」。aimPlaneDistance_ は target 用なので使わない。
	float bestT = (std::numeric_limits<float>::max)();
	float hitEnemyRadius = 0.0f;
	// 軽ホーミング用：レティクル中心から最近の敵を画面距離で追跡
	IImGuiEditable* nearestLocal = nullptr;
	float nearestPx = (std::numeric_limits<float>::max)();
	float nearestDist = 0.0f; // nearestLocal までのカメラ距離（非ロック射撃の収束深度）
	// ロックオン対象（強ホーミング用）
	IImGuiEditable* lockedLocal = nullptr;
	Vector3 desiredTarget{
		camPos.x + rayDir.x * aimPlaneDistance_,
		camPos.y + rayDir.y * aimPlaneDistance_,
		camPos.z + rayDir.z * aimPlaneDistance_,
	};

	// 敵中心をワールド→スクリーン pixel に投影
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	auto projectToPixel = [&](const Vector3& w, Vector2& outPx, float& outClipW) -> bool {
		const float wx = w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0];
		const float wy = w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1];
		const float ww = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
		if (ww <= kTinyEpsilon) return false; // カメラ後方
		const float ndcX = wx / ww;
		const float ndcY = wy / ww;
		outPx.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(WindowsApplication::kUIWidth);
		outPx.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(WindowsApplication::kUIHeight);
		outClipW = ww;
		return true;
	};

	const Matrix4x4& projMat = camera_->GetProjectionMatrix();
	const float invTanHalfFovY = projMat.m[1][1];
	const float clientH = static_cast<float>(WindowsApplication::kUIHeight);

	auto checkEnemy = [&](IImGuiEditable* e, const Vector3& pos) {
		if (!e) return;
		const EntityTag etag = Gameplay::Of(e).GetTag();
		const bool isEnemy = (etag == EntityTag::Enemy);
		const bool isBoss  = (etag == EntityTag::Boss);
		// ボスはレティクルの視覚フィードバック（色/サイズ/近接レンジ）だけ対象にする。
		// 照準吸着(desiredTarget)やホーミング(locked/nearest)は付けない＝手動狙いのまま。
		if (!isEnemy && !isBoss) return;
		const auto& col = Gameplay::Of(e).GetCollider();
		if (!col.enabled || col.shape != ColliderShape::Sphere) return;
		const Vector3 center{ pos.x + col.offset.x, pos.y + col.offset.y, pos.z + col.offset.z };

		// 敵中心をスクリーン投影
		Vector2 enemyPx{};
		float clipW = 0.0f;
		if (!projectToPixel(center, enemyPx, clipW)) return;

		// カメラから敵中心までの距離
		const Vector3 toEnemy{ center.x - camPos.x, center.y - camPos.y, center.z - camPos.z };
		const float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y + toEnemy.z * toEnemy.z);
		if (dist < kTinyEpsilon) return;

		// 敵の見かけ半径(px)
		const float pixelRadius = col.radius * clientH * invTanHalfFovY * 0.5f / dist;

		// レティクルとの pixel 距離
		const float dx = enemyPx.x - rp.x;
		const float dy = enemyPx.y - rp.y;
		const float dPx = std::sqrt(dx * dx + dy * dy);

		// 許容範囲：見かけ半径 × アシスト倍率 ＋ レティクル自身の半径 × 重なり率。
		// レティクル中心だけで判定すると、遠くの小さな敵は輪が重なっていてもロックされず、
		// 視差で弾が敵の横を抜けてしまうため、レティクルの大きさも許容に含める。
		const float reticleRadiusPx = reticle_->GetSize() * 0.5f * aimReticleOverlapRate_;
		const float assistThreshold = pixelRadius * aimAssistPixelScale_ + reticleRadiusPx;

		// 画面上最近の敵を更新（軽ホーミング先候補）。ボスは対象外（手動狙い）。
		if (isEnemy && dPx < nearestPx) {
			nearestPx = dPx;
			nearestLocal = e;
			nearestDist = dist;
		}

		if (dPx <= assistThreshold && dist < bestT) {
			bestT = dist;
			hitEnemyRadius = col.radius;
			hitEnemy = true;
			if (isEnemy) {
				// ロックオン時は敵中心へ吸い込み（パルテナ式）＋強ホーミング。ボスには付けない。
				desiredTarget = center;
				lockedLocal = e;
			} else {
				// ボス：近接レンジ判定用にターゲット中心だけ渡す（射撃方向は下で camera-forward に戻す）。
				desiredTarget = center;
			}
		}
	};

	for (auto& p : GetDynamicPrimitives()) {
		if (p) checkEnemy(p.get(), *p->GetEditableTranslate());
	}
	for (auto& a : GetDynamicAnimated()) {
		if (a) checkEnemy(a.get(), a->GetTranslate());
	}
	for (auto& o : GetDynamicObjects()) {
		if (o) checkEnemy(o.get(), *o->GetEditableTranslate());
	}

	// 弾発射用は Lerp 前の即時 target（ロックオン直後でも遅れずに敵へ向かう）
	firingTarget_ = desiredTarget;
	// 非ロック時は aim plane（遠方）へ撃つため、自機とカメラの位置差による視差で手前の敵の横を抜ける。
	// レティクル付近に敵がいれば、レティクルの ray 上でその敵と同じ深さの点を狙って収束させる。
	if (!lockedLocal && nearestLocal && nearestPx <= aimConvergePx_ && nearestDist > kNearZero) {
		firingTarget_ = {
			camPos.x + rayDir.x * nearestDist,
			camPos.y + rayDir.y * nearestDist,
			camPos.z + rayDir.z * nearestDist,
		};
	}
	// ボス戦は照準吸着なし＝発射方向は常にカメラ前方（中央 reticle の ray）。
	// checkEnemy がボス中心へ desiredTarget を寄せても、射撃は手動狙いのままにする。
	if (phase_ == Phase::Boss) {
		firingTarget_ = {
			camPos.x + rayDir.x * aimPlaneDistance_,
			camPos.y + rayDir.y * aimPlaneDistance_,
			camPos.z + rayDir.z * aimPlaneDistance_,
		};
	}

	UpdatePlayerFacing(desiredTarget);
	UpdateReticleLockDisplay(hitEnemy, bestT, hitEnemyRadius, desiredTarget);

	// 弾発射時のホーミング選択用にメンバへ反映
	lockedEnemy_  = lockedLocal;
	nearestEnemy_ = nearestLocal;
}

// 自機の向き：狙う点をなめらかに追い、段階・演出ごとの決まった向きを優先する
void StagePlayScene::UpdatePlayerFacing(const Vector3& desiredTarget) {
	// プレイヤー回転用は Lerp でぬるっと追従
	if (!aimInitialized_) {
		aimTarget_ = desiredTarget;
		aimInitialized_ = true;
	} else {
		const float dtP = GetScaledDeltaTime(TimeGroup::Player);
		const float alpha = (aimSmoothTime_ > kTinyEpsilon)
			? (1.0f - std::exp(-dtP / aimSmoothTime_))
			: 1.0f;
		aimTarget_.x += (desiredTarget.x - aimTarget_.x) * alpha;
		aimTarget_.y += (desiredTarget.y - aimTarget_.y) * alpha;
		aimTarget_.z += (desiredTarget.z - aimTarget_.z) * alpha;
	}

	// プレイヤー回転を player → aimTarget 方向に。
	// ただし近接派生中（Up/Right）はレティクルではなく対象敵を直接向く。
	// 向きは足元でなく体の中心（判定の姿勢の中心＝このフレームに描く体の中心）から測る
	const Vector3 bodyPos = Gameplay::Of(player_).HasColliderPose()
		? Gameplay::Of(player_).GetColliderPoseCenter() : GameScene::GetShotAimPoint(player_);
	Vector3 aimTgt = aimTarget_;
	if (jdMeleeCameraActive_ && jdCounterTarget_) {
		if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) aimTgt = *tp;
	}
	const Vector3 toAim{ aimTgt.x - bodyPos.x, aimTgt.y - bodyPos.y, aimTgt.z - bodyPos.z };
	const float horiz = std::sqrt(toAim.x * toAim.x + toAim.z * toAim.z);
	if (GetSceneTimeScale() == 0.0f && !jdMeleeCameraActive_) {
		// シーン停止中（エディタ Pause）はレティクル追従を止めて固定アングルで構える。
		// ジャスト回避の近接派生も World を 0 にするので除く（固定の向きになり、ボスに背を向けていた）。
		// 武器ソケット等の確認・編集をしやすくする。位置（クリップ追従）はそのまま。
		player_->SetRotate(fixedPlayerFacing_);
	} else if (specialActive_) {
		// 必殺技中はレティクル追従を止め、正面（+Z）でどっしり構える。
		// （きょろきょろ防止＋発射時に向きが崩れるのを防ぐ。大技を構えて撃つ演出）
		player_->SetRotate(specialFirmFacing_);
	} else if (IsPlayerDying()) {
		// 死亡演出の間は向きを止める（カメラが寄って見上げると、画面基準のレティクルの向きがつられて動くため）
	} else if (IsCloudTopIntroActive()) {
		// 雲の上の回り込みの間はレールの前を向く（カメラが回り込むので、レティクルの向きは使わない）。
		// 照準の追従は回り込みが終わったフレームにレールのカメラから取り直す
		const Vector3& railRotate = cloudTopIntro_->GetRailRotate();
		player_->SetRotate({ railRotate.x, railRotate.y, 0.0f });
		aimInitialized_ = false;
	} else if (phase_ == Phase::Landing && bossStage_) {
		// 着陸中は進む向き → 降りる前にボスの方へ（pitch は 0）
		player_->SetRotate({ 0.0f, bossStage_->GetLandingPlayerYaw(), 0.0f });
		aimInitialized_ = false;
	} else if (phase_ == Phase::Boss) {
		// ボス戦：レティクル追従ではなく移動入力方向／ジャスト回避中は攻撃元方向を向く。
		// yaw は移動ブランチで bossFacingYaw_ に更新済み（pitch/roll は 0＝地上を走る通常キャラ）。
		player_->SetRotate({ 0.0f, bossFacingYaw_, 0.0f });
	} else if (horiz > kTinyEpsilon || std::abs(toAim.y) > kTinyEpsilon) {
		ApplyRailAimRotation(toAim);
	}
}

// ロックオン中の見た目：敵を囲む大きさと、弱近接 1 段目が届くか（届くなら近接用の絵）
void StagePlayScene::UpdateReticleLockDisplay(bool hitEnemy, float distToEnemy, float enemyRadius, const Vector3& enemyCenter) {
	// ロックオン時：敵の見かけサイズ（スクリーンpixel半径）を計算してレティクルへ
	if (hitEnemy) {
		// 敵中心までのカメラからの距離（ray の t をそのまま使える）
		// projection の m[1][1] = 1 / tan(fovY/2) なので、
		// pixelRadius = radius * clientHeight * m[1][1] / (2 * dist)
		const Matrix4x4& proj = camera_->GetProjectionMatrix();
		const float invTanHalfFovY = proj.m[1][1];
		const float pixelRadius = (distToEnemy > kTinyEpsilon)
			? (enemyRadius * static_cast<float>(WindowsApplication::kUIHeight)
				* invTanHalfFovY * 0.5f / distToEnemy)
			: 0.0f;
		// レティクルは外周マージン付きで少し大きめに（敵を囲む見た目に）
		constexpr float kReticleMargin = 1.25f;
		reticle_->SetLockOnTargetSize(pixelRadius * 2.0f * kReticleMargin);
	}
	// ----- 近接レンジ表示：弱近接1段目(melee_w1)の判定球が、ロック中の敵に届くか -----
	// レティクルが敵に重なっている時(hitEnemy)だけ判定。届くなら近接用テクスチャに切替。
	bool meleeInRange = false;
	if (hitEnemy) {
		const std::string mprefab = meleeCombo_.GetReachPrefab();
		float meleeRadius = 0.0f;
		Vector3 meleeOff{ 0.0f, 0.0f, 0.0f };
		if (const PrefabDef* md = PrefabManager::GetInstance()->Find(mprefab)) {
			meleeRadius = md->colliderRadius;        // Sphere collider 前提
			if (md->hasMelee) meleeOff = md->meleeOffset;
		}
		if (meleeRadius > 0.0f) {
			// aim 基底（前/右/上）= SpawnPlayerMelee と共通
			Vector3 rgt, up, fwd;
			ComputeAimBasis(rgt, up, fwd);
			const Vector3 ppos = player_->GetTranslate();
			// 判定球中心 = 自機 + 基底×offset
			const Vector3 hitCenter{
				ppos.x + rgt.x * meleeOff.x + up.x * meleeOff.y + fwd.x * meleeOff.z,
				ppos.y + rgt.y * meleeOff.x + up.y * meleeOff.y + fwd.y * meleeOff.z,
				ppos.z + rgt.z * meleeOff.x + up.z * meleeOff.y + fwd.z * meleeOff.z,
			};
			// ロック中の敵の中心と半径
			const float ddx = hitCenter.x - enemyCenter.x;
			const float ddy = hitCenter.y - enemyCenter.y;
			const float ddz = hitCenter.z - enemyCenter.z;
			const float d = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
			meleeInRange = (d <= meleeRadius + enemyRadius);
		}
	}
	reticle_->SetMeleeInRange(meleeInRange);

	reticle_->SetLockOn(hitEnemy);
}

// 射撃とチャージ
void StagePlayScene::UpdateShooting(InputActionMap* actions, bool gameFrozen) {
	// ----- 射撃・チャージシステム -----
	// 必殺技発動中は射撃・チャージを完全に止める（Shatterline のチャージ中は World がスロー＝
	// gameFrozen にならないため、specialActive_ を明示的に除外しないと点の決定 Fire で弾が出てしまう）。
	// 死亡演出の間も止める（入力なしでもチャージは「押していない時間」で進んでしまうため）
	if (gameFrozen || specialActive_ || IsPlayerDying()) return;
	const float dtP = GetScaledDeltaTime(TimeGroup::Player);
	fireTimer_ -= dtP;
	if (fireTimer_ < 0.0f) fireTimer_ = 0.0f;
	timeSinceAttack_ += dtP;
	timeSinceShot_ += dtP;

	// チャージ時間：メイン武器（20 番 §1）→ 無ければプレイヤープレハブ（有効時のみ）
	if (const MainWeaponDef* mw = GetGameSession()->GetLoadoutState()->GetMainWeapon()) {
		chargeStage1Time_ = mw->chargeStage1Time;
		chargeStage2Time_ = mw->chargeStage2Time;
	} else if (player_) {
		const ChargeParams& chp = Gameplay::Of(player_).GetChargeParams();
		if (chp.enabled) {
			chargeStage1Time_ = chp.stage1Time;
			chargeStage2Time_ = chp.stage2Time;
		}
	}

	// charge_hold（保持ループ）エフェクトをプレイヤー位置に毎フレーム追従させる
	if (chargeHoldEffectHandle_ != kInvalidEffectHandle && player_) {
		EffectManager::GetInstance()->SetPosition(chargeHoldEffectHandle_, player_->GetTranslate());
	}

	bool firePressed = actions->IsPressed(static_cast<int>(Action::Fire));

	// Fire を押していない時間でチャージ進行
	if (!firePressed && player_) UpdateChargeLevel(dtP);
#ifdef _DEBUG
	// Debug ビルドでは、マウスが Viewport 上に無く、かつ別の ImGui ウィンドウ
	// （Save ボタン等）がマウスをキャプチャ中の時は Fire を抑制する。
	// Viewport ホバー中はそのまま通す（Viewport も ImGui ウィンドウだから WantCaptureMouse は常に true）
	{
		bool mouseOverViewport = false;
		if (auto* vp = ImGuiManager::GetInstance()->GetViewportWindow()) {
			mouseOverViewport = vp->IsHovered();
		}
		if (!mouseOverViewport && ImGui::GetIO().WantCaptureMouse) {
			firePressed = false;
		}
	}
#endif
	// RT+Y+B の大技の見分け：チャージが 1 段階以上ある時に RT を押したら、同時押しとみなす間だけ射撃を待つ。
	// その間に近接が大技の溜めに入ったら撃たない（ConsumeGunCharge で待ちを消す）。待ち切ったら、離していても 1 発撃つ
	bool fireAfterWait = false;
	if (fireChordTimer_ > 0.0f) {
		if (meleeCombo_.IsLocked()) {
			fireChordTimer_ = 0.0f;
		} else {
			fireChordTimer_ -= dtP;
			fireAfterWait = (fireChordTimer_ <= 0.0f);
		}
		firePressed = fireAfterWait;
	} else if (firePressed && actions->IsTriggered(static_cast<int>(Action::Fire)) && playerChargeLevel_ >= 1.0f
		&& fireTimer_ <= 0.0f && !meleeCombo_.IsLocked()) {
		fireChordTimer_ = meleeCombo_.GetChordWindow();
		firePressed = false;
	}
	if (fireAfterWait) fireChordTimer_ = 0.0f;

	// Fire 押下で発射（チャージ状態でも即発射、または連射）
	if (firePressed && fireTimer_ <= 0.0f && shootLockoutTimer_ <= 0.0f
		&& !meleeCombo_.IsLocked() && player_) {
		FirePlayerShot();
	}
}

// 射撃ボタンを離している間にチャージを進める（1 段目＝外側のレティクルとエフェクト、2 段目＝差し替え）
void StagePlayScene::UpdateChargeLevel(float dtP) {
	if (playerChargeLevel_ < 0.0f) {
		// チャージ開始
		playerChargeLevel_ = 0.0f;
		chargeTimer_ = 0.0f;
		chargeStage1Triggered_ = false;
		chargeStage2Triggered_ = false;
	}
	chargeTimer_ += dtP;
	if (chargeTimer_ > chargeStage2Time_) chargeTimer_ = chargeStage2Time_;

	// 1段階目発動（3秒経過）→ 外側レティクル登場 + charge_start エフェクト
	if (!chargeStage1Triggered_ && chargeTimer_ >= chargeStage1Time_) {
		chargeStage1Triggered_ = true;
		playerChargeLevel_ = 1.0f;
		if (reticle_) {
			reticle_->StartChargeAnimation(outerChargeStartRadius_, outerChargeEndRadius_, outerChargeEasingDuration_);
			reticle_->SetOuterRotationSpeed(outerRotationSpeed_);
		}
		// プレイヤープレハブのエフェクトスロットから取得（charge_start=瞬間 / charge_hold=保持ループ）
		auto* effectMgr = EffectManager::GetInstance();
		if (effectMgr) {
			const std::string startEff = Gameplay::Of(player_).FindEffect("charge_start");
			const std::string holdEff  = Gameplay::Of(player_).FindEffect("charge_hold");
			if (!startEff.empty()) chargeStartEffectHandle_ = effectMgr->Play(startEff, player_->GetTranslate());
			if (!holdEff.empty())  chargeHoldEffectHandle_  = effectMgr->Play(holdEff, player_->GetTranslate());
		}
	}

	// 2段階目発動（6秒経過）→ 別エフェクト（charge_start2 / charge_hold2 スロット）
	if (!chargeStage2Triggered_ && chargeTimer_ >= chargeStage2Time_) {
		chargeStage2Triggered_ = true;
		playerChargeLevel_ = kChargeLevel2;
		auto* effectMgr = EffectManager::GetInstance();
		if (effectMgr) {
			const std::string startEff2 = Gameplay::Of(player_).FindEffect("charge_start2");
			const std::string holdEff2  = Gameplay::Of(player_).FindEffect("charge_hold2");
			if (!startEff2.empty()) effectMgr->Play(startEff2, player_->GetTranslate());
			// charge_hold を charge_hold2 に差し替え
			if (chargeHoldEffectHandle_ != kInvalidEffectHandle) {
				effectMgr->Stop(chargeHoldEffectHandle_);
				chargeHoldEffectHandle_ = kInvalidEffectHandle;
			}
			if (!holdEff2.empty()) chargeHoldEffectHandle_ = effectMgr->Play(holdEff2, player_->GetTranslate());
		}
	}
}

// 1 発撃つ（チャージ段階で弾を選び、メイン武器・精密射撃の補正を掛ける）。撃ったらチャージを戻す
void StagePlayScene::FirePlayerShot() {
	const Vector3 origin = PlayerShotOrigin();
	// 発射方向は Lerp 前の即時 target を使う（ロックオン直後の弾が遅れて飛ぶのを防ぐ）
	Vector3 dir{ firingTarget_.x - origin.x, firingTarget_.y - origin.y, firingTarget_.z - origin.z };
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len > kSquaredEpsilon) {
		dir.x /= len; dir.y /= len; dir.z /= len;
		// ロックオン中の敵を優先、外なら画面上最近の敵に軽ホーミング
		IImGuiEditable* homeTarget = lockedEnemy_ ? lockedEnemy_ : nearestEnemy_;

		// チャージレベルで弾プレハブを選択（normal / charge1 / charge2）
		std::string bulletPrefab;
		if (playerChargeLevel_ >= kChargeLevel2)      bulletPrefab = Gameplay::Of(player_).FindBulletPrefab("charge2");
		else if (playerChargeLevel_ >= 1.0f) bulletPrefab = Gameplay::Of(player_).FindBulletPrefab("charge1");
		else                                  bulletPrefab = Gameplay::Of(player_).FindBulletPrefab("normal");
		if (bulletPrefab.empty()) bulletPrefab = "TemporaryPlayerBullet"; // フォールバック

		// ロック中（レティクルが敵に重なっている）なら強ホーミング、最近敵のみなら軽ホーミング
		const bool isLocked = (lockedEnemy_ != nullptr);

		// 弾プレハブの BulletParams から速度・寿命・collider拡大・ホーミングを読む
		// bullet の指定が無ければ PrefabDef の既定値
		const PrefabDef* bdef = PrefabManager::GetInstance()->Find(bulletPrefab);
		const PrefabDef& bulletDef = (bdef && bdef->hasBullet) ? *bdef : PrefabDef::Defaults();
		float speed          = bulletDef.bulletSpeed;
		float lifetime       = bulletDef.bulletLifetime;
		float colliderGrowth = bulletDef.bulletColliderGrowth;
		float homing         = isLocked ? bulletDef.bulletStrongHomingStrength : bulletDef.bulletHomingStrength;

		// メイン武器の弾の倍率（通常弾／チャージ弾。精密射撃の加算より前に掛ける）
		const MainWeaponDef* mainWeapon = GetGameSession()->GetLoadoutState()->GetMainWeapon();
		float sizeScale = 1.0f;
		if (mainWeapon) {
			const MainWeaponShot& shot = mainWeapon->ShotFor(playerChargeLevel_);
			speed          *= shot.speedScale;
			homing         *= shot.homingScale;
			colliderGrowth *= shot.sizeScale;
			sizeScale       = shot.sizeScale;
		}

		// 精密射撃モードの加算（precisionBlend_ で補間）。
		// 弾速は全弾に加算、ホーミング加算はロック中（強ホーミング）の弾だけに効かせる。
		if (player_) {
			const PrecisionParams& pp = Gameplay::Of(player_).GetPrecisionParams();
			if (pp.enabled && precisionBlend_ > 0.0f) {
				speed += pp.speedAdd * precisionBlend_;
				if (isLocked) homing += pp.homingAdd * precisionBlend_;
			}
		}

		const float homeStrength = homeTarget ? homing : 0.0f;
		const int atk = ScaledPlayerAttack();
		if (playerChargeLevel_ >= 1.0f && playerBeam_) {
			// チャージ射撃はビーム：撃っている間はレティクルへ付いていき、通る所の敵すべてに当たり続ける。
			// 判定の太さはメイン武器の弾の大きさに合わせる（見た目の太さはエフェクト側）
			const PlayerBeamPart::Kind kind = (playerChargeLevel_ >= kChargeLevel2)
				? PlayerBeamPart::Kind::Charge2 : PlayerBeamPart::Kind::Charge1;
			if (chargeBeamId_ != PlayerBeamPart::kInvalidBeam) playerBeam_->Stop(chargeBeamId_);
			chargeBeamId_ = playerBeam_->Fire(kind, origin, dir, atk, sizeScale);
		} else {
			// 弾は aim plane に到達した時点で消滅させ、面より奥への乱射を防ぐ
			SpawnPlayerBullet(origin, dir, speed, lifetime,
				colliderGrowth, homeTarget, homeStrength,
				aimPlaneDistance_, bulletPrefab, atk, sizeScale);
		}
		SoundManager::GetInstance()->Play2DSound(playerChargeLevel_ >= 1.0f ? "se_charge_shot" : "se_shot");
		playerAnim_.Trigger(playerChargeLevel_ >= 1.0f ? PlayerAnimState::ChargeRelease : PlayerAnimState::Shoot);
		// 攻撃系の飛行ユニットは、展開していない間は自機の射撃に合わせて支援射撃する
		if (flightUnit_) flightUnit_->NotifyPlayerShot(true);

		// 連射間隔はメイン武器 → 無ければプレイヤープレハブの ChargeParams.fireRate
		const float fr = mainWeapon ? mainWeapon->fireRate : Gameplay::Of(player_).GetChargeParams().fireRate;
		fireTimer_ = (fr > 0.0f) ? fr : kDefaultFireInterval;
		MarkPlayerAttack();
		timeSinceShot_ = 0.0f;

		// チャージ状態だった場合はリセット
		ResetGunCharge();
	}
}

void StagePlayScene::ResetGunCharge() {
	if (playerChargeLevel_ < 0.0f) return;
	playerChargeLevel_ = -1.0f;
	chargeTimer_ = 0.0f;
	chargeStage1Triggered_ = false;
	chargeStage2Triggered_ = false;
	if (reticle_) {
		reticle_->EndChargeAnimation();
	}
	auto* effectMgr = EffectManager::GetInstance();
	if (effectMgr && chargeHoldEffectHandle_ != kInvalidEffectHandle) {
		effectMgr->Stop(chargeHoldEffectHandle_);
		chargeHoldEffectHandle_ = kInvalidEffectHandle;
	}
}

int StagePlayScene::ConsumeGunCharge() {
	const int level = (playerChargeLevel_ >= kChargeLevel2) ? 2 : (playerChargeLevel_ >= 1.0f ? 1 : 0);
	ResetGunCharge();
	// 大技の溜めの間、RT の射撃を待っていた分は撃たない
	fireChordTimer_ = 0.0f;
	return level;
}

// 近接・弾・飛行ユニット・被弾・必殺技（射撃より後。弾はこのフレームに撃った物も進める）
void StagePlayScene::UpdateCombatAndSpecial(InputActionMap* actions, float worldDt, bool gameFrozen) {
	// ----- 近接攻撃（B/Y の 4 段コンボ / Y+B の溜め。MeleeComboPart）-----
	// 分身カウンターの近接派生中は World=0 のため上の gameFrozen ブロックに入れない。
	// MeleeComboPart は発生・スキ・溜めのタイマーと判定の出待ちを進めるので、
	// 派生中も Player dt で必ず呼ぶ必要がある。
	if (!gameFrozen || jdMeleeCameraActive_) {
		const float dtP = GetScaledDeltaTime(TimeGroup::Player);
		// 分身カウンターの選択中・派生中は通常の近接入力を受け付けない（派生が確定したフレームの二重発動を防ぐ）
		meleeCombo_.Update(actions, dtP, !(justDodgeCounterActive_ || jdSelecting_));
	}

	// 弾の進行と寿命処理（World 時間軸で動かす）
	if (!gameFrozen) {
		// 機動系の RB のスロー中は、自機の弾だけ Player の時間で進める（ジャスト回避のスローは今までどおり World）
		const bool flightSlow = flightUnit_ && flightUnit_->IsTimeSlowActive();
		UpdateBullets(worldDt, flightSlow ? GetScaledDeltaTime(TimeGroup::Player) : -1.0f);
		// 自機のビームも自機の弾と同じ時間で進める
		UpdatePlayerBeams(flightSlow ? GetScaledDeltaTime(TimeGroup::Player) : worldDt);
	}
	// 近接判定の追従・寿命処理。
	// 近接派生中はワールド完全停止だが、判定の寿命と当たり処理は進める必要があるので Player dt で動かす。
	{
		const float meleeDt = jdMeleeCameraActive_
			? GetScaledDeltaTime(TimeGroup::Player) : worldDt;
		if (!gameFrozen || jdMeleeCameraActive_) {
			// 多段の判定（大技の回転斬り）のもう一度当たれるまでの時間は実時間で数える（ヒットストップでも進む）
			UpdateMelees(meleeDt, GetScaledDeltaTime(TimeGroup::UI));
		}
	}
	// 飛行ユニット（RB の技・クールタイム。実時間で進む）
	UpdateFlightUnit(actions);

	// 必殺技発動中は World が止まっていても必殺技自身の進行と UI 更新は走らせる
	if (!gameFrozen || specialActive_) {

		// プレイヤー被弾処理・UI更新
		UpdatePlayerDamageAndUI(worldDt);

		// 必殺技：発動入力検出 + 発動中タイマー進行
		UpdateSpecialMove(actions, worldDt);
		// ゲージ UI のスプライトサイズ更新
		UpdateSpecialGaugeUI();

#ifdef USE_IMGUI
		// Shatterline の割り方（Voronoiセル）と割れ順のプレビュー。発動中も Idle でも確認できる。
		shatterline_.DrawDebug();
#endif

		// 無敵フラグから HP.enabled を同期（CollisionManager の auto-damage を止める）
		// scene->Update 完了後に CollisionManager::Update が走るため、ここで反映すれば次の判定に間に合う
		RefreshPlayerHpInvincibility();

		// 必殺技の遅延削除キュー（GPU が使い終わるまで数フレーム持つ）とバリアの球の CB
		stormLock_.UpdateTrash(GetDxCore()->GetDeltaTime());
		shatterline_.UpdateTrash();

	}
}

// state.log：SUNDAY のハング検知・スタック判定の基準（フレーム末尾の位置・HP・シーン）
void StagePlayScene::WriteFrameStateLog() {
	// state.log: SUNDAY のハング検知（更新が止まる）・スタック判定（座標が動かない）の基準。
	// フレーム末尾でプレイヤーの最終位置・HP・シーンを毎フレーム記録する。
	if (!player_) return;
	const Vector3 p = player_->GetTranslate();
	const HP& hp = Gameplay::Of(player_).GetHP();
	SessionLogger::GetInstance()->Write(SessionLogger::Category::State, SessionLogger::Level::Trace,
		"frame=" + std::to_string(stateFrame_)
		+ " x=" + std::to_string(p.x) + " y=" + std::to_string(p.y) + " z=" + std::to_string(p.z)
		+ " hp=" + std::to_string(hp.currentHP) + "/" + std::to_string(hp.maxHP)
		+ " scene=" + SceneManager::GetInstance()->GetCurrentSceneName());
	++stateFrame_;
}

void StagePlayScene::Draw() {
	if (phase_ == Phase::StageStart && stageStart_ && stageStart_->IsActive()) {
		DrawStageStart();
		return;
	}
	const bool drawBossWater = (bossWater_ && IsBossArenaVisible());
	// 波のシミュレーション（コンピュート）はシーン RT をバインドする前に回す
	if (drawBossWater) bossWater_->DispatchSimulation();
	// 大河の水面の反射 RT（映す物を鏡像で描く）。シーン RT を貼る前に描き、下で貼り直す
	if (riverWater_ && phase_ == Phase::Rail && camera_) {
		riverWater_->DispatchSimulation(); // 航跡の波（コンピュート）
		riverWater_->RenderReflection(*camera_, GetDynamicObjects(), GetDynamicAnimated());
	}

	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto* commandList = GetDxCore()->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = GetDxCore()->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	GetSkyboxManager()->DrawSetting();
	if (skybox_) {
		skybox_->Draw(GetDxCore());
	}

	// 通常 DSV に戻す（以降のオブジェクト描画用）
	auto normalDsv = GetDxCore()->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	// 3Dオブジェクトの共通描画設定（Object3D / Animated 描画前に必要）
	GetObject3DManager()->DrawSetting();

	// rootParameter[3]=DirectionalLight / [5]=PointLight / [6]=SpotLight を bind
	// （Object3DInstance::Draw はライト系を bind しないため、ここで一括設定しないと GBV #935 が出る）
	LightManager::GetInstance()->BindLights(commandList);

	// Scene の動的エンティティ描画
	DrawDynamicObjects();
	// 武器（ソケット追従）：静的モデルなので Object3D PSO のこの位置で描画する
	if (weapon_ && player_ && player_->GetVisible() && Gameplay::Of(player_).GetWeaponParams().enabled) {
		weapon_->Draw(GetDxCore());
	}
	if (shield_ && player_ && player_->GetVisible()) {
		if (const ShieldDef* sd = GetGameSession()->GetLoadoutState()->GetShield(); sd && !sd->modelFile.empty()) shield_->Draw(GetDxCore());
	}
	// 飛行ユニットのバックパックとファンネル（不透明）
	if (flightUnit_ && flightUnit_->IsInitialized() && player_ && player_->GetVisible()) flightUnit_->DrawModels(GetDxCore());
	DrawDynamicAnimated();
	// 自機の羽根（Spine2 に付けたスキニングのモデル）
	if (playerWings_) playerWings_->Draw(GetDxCore());

	// ボス戦の床（石畳＋水面）。不透明物の後・Primitive（半透明の弾など）の前。
	// 水面は深度を書くので、後から描く物は水面の上に出る。Primitive は自前でルートシグネチャを貼り直す
	if (drawBossWater && skybox_) {
		bossWater_->Draw(*camera_, skybox_->GetCubemapFilePath());
	}
	// ボスの必殺技 GravityOrb の岩と破片（不透明。地面の下から浮き上がる所は床の深度で隠れる）
	if (gravityOrb_ && phase_ == Phase::Boss && camera_) {
		gravityOrb_->Draw(*camera_);
	}
	// 大河の水面。下の地形（川底）を乗算済み α で透かすので、地形を含む不透明物の後に描く
	if (riverWater_ && phase_ == Phase::Rail && skybox_) {
		riverWater_->Draw(*camera_, skybox_->GetCubemapFilePath());
	}

	// 雲海のレイマーチ。不透明物・水面の深度で止め、半透明（Primitive）の前に重ねる。水面には映さない
	if (stageEnv_ && phase_ == Phase::Rail && camera_) {
		stageEnv_->DrawRaymarchClouds(*camera_, Game::GetPostEffect()->GetSceneRenderTarget());
	}

	DrawDynamicPrimitives();

	// 死亡演出の昇る糸（加算・半透明。PrimitivePipeline に切り替える）
	if (playerDeath_ && camera_) playerDeath_->Draw(camera_.get());

	// 飛行ユニットのファンネル・バリア（加算・半透明）
	if (flightUnit_ && flightUnit_->IsInitialized()) flightUnit_->Draw3D();

	// LightningRuntime テスト描画
	if (lightningTest_ && lightningTest_->IsActive()) {
		lightningTest_->Draw();
	}

	// 必殺技 Storm Lock（バリアの球・ロックオン・電撃・雷）
	stormLock_.Draw();

	// 必殺技 Shatterline：崩壊の元絵のキャプチャと断裂線。飛び散る破片は PostEffect 後（DrawAfterPostEffect）に描く
	// ＝崩壊リビールの再反転による二重反転を回避する。
	shatterline_.DrawScene(commandList);


	// 盾のビーム（乗算済み α の半透明。20 番 §2。ルートシグネチャを替えるので、前の物の描画状態を壊さないようここで）
	if (IsShieldBeamVisible() && camera_) beamShield_->Draw(*camera_);
	// メイン武器のビームの刃（同じく乗算済み α の半透明）
	if (IsWeaponBladeVisible() && camera_) weaponBlade_->Draw(*camera_);
	// 全シーン共通の GPUParticle + Effect Editor 描画
	DrawGlobalEffects();

	DrawSceneCameraGizmo();
	DrawDynamicAnimatedSkeletonDebug();

	// スプライト
	// 撃破の段取りの間は HUD を描かない（映画のような画面。最初のフレームはこのまま撮ってリザルトの背景にする）
	const bool hideHud = clearSeq_.IsHudHidden();
	GetSpriteManager()->DrawSetting();
	if (!hideHud) {
		DrawDynamicSprites();
		// 飛行ユニットのクールタイムのバー（必殺技ゲージの下）
		if (flightUnit_ && flightUnit_->IsInitialized() && !IsCloudTopIntroActive()) flightUnit_->DrawUI();
		if (reticle_ && !IsCloudTopIntroActive() && phase_ != Phase::Landing) reticle_->Draw();

		DrawBossHpBarUI();
	}

	// 撃破の段取りの白いフラッシュと STAGE CLEAR（文字は下のスコア表示と同じ Flush でまとめて描く）
	if (clearSeq_.IsActive()) {
		clearSeq_.DrawOverlay(static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()));
	}
	// ボスの必殺技 GravityOrb の閃光（放つ瞬間・着地・爆発で画面を白く覆う）
	if (gravityOrb_ && phase_ == Phase::Boss) {
		gravityOrb_->DrawOverlay(static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()));
	}

	// ポーズメニュー（文字は下のスコア表示と同じ Flush でまとめて描く）
	if (paused_) DrawPauseMenu();

	// スコア表示（右上、控えめサイズ）。拾った物の表示（右下）も同じ Flush で描く
	{
		TextRenderer* tr = TextRenderer::GetInstance();
		if (tr->IsInitialized()) {
			// 操作ガイドの下地（右下まで広い）に重ならないよう、ガイドを開いている間は出さない
			if (!controlGuide_.IsOpen() && !hideHud) {
				lootLog_.Draw(static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()));
			}
			// 上段「SCORE」ラベル + 下段に数値。ラベルと数値は独立に位置・サイズ・色を設定可。
			// 位置は画面右上 (screenW, 0) から (offsetX, offsetY) のオフセット（右端基準）。
			int rawScore = GetGameSession()->GetScoreManager()->GetScore();
			if (rawScore < 0) rawScore = 0;
			if (rawScore > kMaxDisplayScore) rawScore = kMaxDisplayScore; // 5 桁で頭打ち
			char numBuf[16];
			std::snprintf(numBuf, sizeof(numBuf), "%d", rawScore);

			const char* labelStr = "SCORE";
			const float screenW = static_cast<float>(GetDxCore()->GetUIWidth());

			const float labelW = tr->MeasureWidth(labelStr, scoreLabelScale_);
			const float numW   = tr->MeasureWidth(numBuf, scoreNumberScale_);

			if (!hideHud) {
				tr->DrawText(labelStr,
					{ screenW - scoreLabelOffsetX_ - labelW, scoreLabelOffsetY_ },
					scoreLabelScale_, scoreLabelColor_,
					scoreLabelOutlineThickness_, scoreLabelOutlineColor_);
				tr->DrawText(numBuf,
					{ screenW - scoreNumberOffsetX_ - numW, scoreNumberOffsetY_ },
					scoreNumberScale_, scoreNumberColor_,
					scoreNumberOutlineThickness_, scoreNumberOutlineColor_);
			}
			tr->Flush();
		}
	}

	// スタート演出をスキップした白フェードの開き（レールの最初の数フレーム）
	if (TextRenderer* tr = TextRenderer::GetInstance(); tr->IsInitialized() && stageStart_) {
		stageStart_->DrawScreenOverlay(static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()));
		tr->Flush();
	}

	// DebugDraw（スプライン可視化など）の出力
	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();

	// Effect Editor のプレビューは Game::Draw がシーンに関係なく描く
}

bool StagePlayScene::UpdateStageStart() {
	// 同期ロードの直後の 1 フレームは dt が大きい（ロードの間 HUB の最後の 1 枚が止まっていた）ので、降下が飛ばないよう切り詰める
	const float dt = (std::min)(GetScaledDeltaTime(TimeGroup::UI), 1.0f / kMinDeltaTimeFps);
	const Vector2 screen = { static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()) };
	stageStart_->Update(GetInput(), dt, screen);
	if (!stageStart_->IsActive()) {
		// 終わった（RestoreStageEnvironment 済み）。このフレームからレール
		phase_ = Phase::Rail;
		prevPhase_ = Phase::Rail;
		return false;
	}
	camera_->UpdateShake(dt);
	camera_->Update();
	UpdateDebugCameraIfActive();
	// 施設・コクピット・機体などの行列は、このフレームのカメラができてから作る
	// コクピットの六角形のディゾルブはシェーダの SV_Position（描画解像度のピクセル）で並べるので、描画解像度を渡す
	stageStart_->LateUpdate(dt, { static_cast<float>(GetDxCore()->GetRenderWidth()), static_cast<float>(GetDxCore()->GetRenderHeight()) });
	// スラスターの噴射（EffectManager）。演出の間もエフェクトを進める
	UpdateGlobalEffects(camera_.get(), GetDxCore()->GetDeltaTime());
	if (skybox_) skybox_->Update(dt);
	// 射出口の向こうの雲（流れ・遠景の雲の視差）は雲の上の仮の目の位置で進める
	if (stageEnv_ && stageStart_->DrawsCloudsThroughExit()) {
		const Vector3 e = camera_->GetTranslate();
		const Vector3 o = stageStart_->GetSkyViewOffset();
		stageEnv_->UpdateClouds(dt, { e.x + o.x, e.y + o.y, e.z + o.z });
	}
	UpdateDynamicObjects();
	UpdateDynamicAnimated(dt);
	return true;
}

void StagePlayScene::DrawStageStart() {
	auto* commandList = GetDxCore()->GetCommandList();
	// 表示板の中身を RenderTexture へ（RTV とビューポートを切り替えるので、シーンの RT を描き始める前に）
	stageStart_->RenderBoardTexture(commandList);
	{
		// シーン RT は描画解像度
		const float w = static_cast<float>(GetDxCore()->GetRenderWidth());
		const float h = static_cast<float>(GetDxCore()->GetRenderHeight());
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, w, h, 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissor);
	}
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = GetDxCore()->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);
	GetSkyboxManager()->DrawSetting();
	if (skybox_) skybox_->Draw(GetDxCore());

	auto normalDsv = GetDxCore()->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);
	GetObject3DManager()->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	// 施設とコクピット（一人称）／演出専用の機体（三人称）。ステージの物は離れた所にあり閉じた施設の中からは
	// 見えないので描かない。ゲームの自機は SetPlayerHidden で隠している
	stageStart_->Draw(GetDxCore());
	// 射出口の向こうの雲海：今のカメラを位置だけ雲の上へずらして描く。向き・画角は同じなので
	// トンネルの壁の深度で雲が隠れ、射出口の穴からだけ見える（レイマーチは不透明物の深度で止まる）
	if (stageEnv_ && camera_ && stageStart_->DrawsCloudsThroughExit()) {
		Camera skyCamera = *camera_;
		const Vector3 e = camera_->GetTranslate();
		const Vector3 o = stageStart_->GetSkyViewOffset();
		skyCamera.SetTranslate({ e.x + o.x, e.y + o.y, e.z + o.z });
		skyCamera.Update();
		stageEnv_->DrawRaymarchClouds(skyCamera, Game::GetPostEffect()->GetSceneRenderTarget());
	}
	DrawGlobalEffects();

	GetSpriteManager()->DrawSetting();
	// 一人称で射出口に近づくまぶしさ（ポーズメニューより下に）
	if (TextRenderer* tr = TextRenderer::GetInstance(); tr->IsInitialized()) {
		stageStart_->DrawScreenOverlay(static_cast<float>(GetDxCore()->GetUIWidth()), static_cast<float>(GetDxCore()->GetUIHeight()));
		tr->Flush();
	}
	if (paused_) DrawPauseMenu();
	if (TextRenderer* tr = TextRenderer::GetInstance(); tr->IsInitialized()) tr->Flush();

	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();
}

bool StagePlayScene::HasBloomSources() const {
	return (phase_ == Phase::StageStart && stageStart_ && stageStart_->HasBloom())
		|| (playerWings_ && playerWings_->HasBloom())
		|| (playerDeath_ && playerDeath_->HasBloom());
}

void StagePlayScene::DrawBloomSources() {
	if (stageStart_) stageStart_->DrawBloom(GetDxCore());
	// 羽根のディゾルブの縁（雲の上の展開・ボス戦の収納）
	if (playerWings_) playerWings_->DrawBloom(GetDxCore());
	// 死亡演出：自機がほどける縁・昇る糸
	if (playerDeath_) playerDeath_->DrawBloom(GetDxCore());
	// 盾のビーム
	if (IsShieldBeamVisible() && camera_) beamShield_->DrawBloomPass(*camera_);
	// メイン武器のビームの刃
	if (IsWeaponBladeVisible() && camera_) weaponBlade_->DrawBloomPass(*camera_);
}

void StagePlayScene::BurnPlayerWings(float seconds) {
	if (playerWings_) playerWings_->Burn(seconds);
}

void StagePlayScene::CollectWingBurnPoints(std::vector<Vector3>& out) {
	if (playerWings_) {
		playerWings_->CollectJointPositions(out);
	} else {
		out.clear();
	}
}

void StagePlayScene::FinishPlayerDeath() {
	SceneManager::GetInstance()->ChangeScene("GAMEOVER");
}

void StagePlayScene::RestorePlayerPose() {
	playerAnim_.SetSuspended(false);
	ApplyPlayerPose();
}

void StagePlayScene::BeginPlayerDeath(bool preview) {
	if (!player_ || !playerDeath_) return;
	StopAllPlayerBeams();
	// 被弾・回避の点滅を止めて素の見た目から光らせる
	playerInvincibilityTimer_ = 0.0f;
	playerBlinkGlow_ = { 0.0f, 0.0f, 0.0f };
	player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	// 羽根があるのはレールの間だけ（着地の遷移で収納する）
	const PlayerDeathPart::Kind kind = phase_ == Phase::Rail ? PlayerDeathPart::Kind::Rail : PlayerDeathPart::Kind::Boss;
	// 死亡演出の間は自機のアニメを PlayerDeathPart に任せる（ボス戦は Kneel を向こうが鳴らす。STG は railDeath を鳴らしてから）
	if (kind == PlayerDeathPart::Kind::Rail) playerAnim_.PlayAndSuspend(PlayerAnimState::RailDeath);
	else playerAnim_.SetSuspended(true);
	playerDeath_->Begin(kind, player_, preview);
}

void StagePlayScene::SetPlayerHidden(bool hidden) {
	if (player_) player_->SetVisible(!hidden);
}

void StagePlayScene::RestoreStageEnvironment() {
	// 降下中に LSHIFT でスキップすると、後回しの初期化が残ったままここ（演出の終わり）へ来る
	CompleteDeferredInit();
	SetPlayerHidden(false);
	if (camera_) {
		camera_->SetNearClip(cameraNearClip_);
		camera_->SetFovY(baseFovY_);
	}
	// IBL は空の cubemap に、フォグ・平行光源・空は今のステージの時刻のセクションへ
	if (skybox_ && GetObject3DManager()) GetObject3DManager()->SetEnvironmentTexture(skybox_->GetCubemapFilePath());
	if (stageEnv_) stageEnv_->Reset(railStage_ ? railStage_->GetStageSeconds() : 0.0f);
}

void StagePlayScene::BeginCloudTop() {
	// 雲の上（S4、F6）：レールは t=0 から進める。自機は飛ぶ姿勢で、羽根はたたんだ形から付け根から現れて開く。
	// カメラは自機の右上前方からレールのカメラへ回り込み、その間は操作を受け付けない
	CompleteDeferredInit();   // 自機・羽根・レティクルを使う
	ApplyPlayerPose();
	if (cloudTopIntro_) {
		cloudTopIntro_->Begin();
		if (playerWings_) playerWings_->Deploy(cloudTopIntro_->GetWingDeployDelay());
	}
	aimInitialized_ = false;
	if (reticle_) reticle_->ResetToCenter();
}

void StagePlayScene::ApplyPlayerLook() {
	PEPPER_SCOPE("StagePlayScene::ApplyPlayerLook");
	if (!player_) return;
	player_->SetEnvironmentCoefficient(playerLook_.environmentCoefficient);
	if (playerLook_.overrideMaterial) player_->SetMetallicRoughness(playerLook_.metallic, playerLook_.roughness);
	const float g = playerLook_.baseGlow;
	// 死亡演出のエーテルの光も下地に足す
	const Vector3 death = playerDeath_ ? playerDeath_->GetBodyGlow() : Vector3{ 0.0f, 0.0f, 0.0f };
	player_->SetEmissive({ g + playerBlinkGlow_.x + death.x, g + playerBlinkGlow_.y + death.y, g + playerBlinkGlow_.z + death.z });
}

bool StagePlayScene::IsCloudTopIntroActive() const {
	return cloudTopIntro_ && cloudTopIntro_->IsActive();
}

bool StagePlayScene::IsBossArenaVisible() const {
	// 谷の出口（168 秒〜）で海が見え始める。闘技場のモデル（streamGroup "BossArena"）は 160 秒に作る
	constexpr float kShowSec = 150.0f;
	if (phase_ == Phase::Landing || phase_ == Phase::Boss) return true;
	return phase_ == Phase::Rail && railStage_ && railStage_->GetStageSeconds() >= kShowSec;
}

void StagePlayScene::ApplyPlayerPose() {
	if (!player_) return;
	// どのクリップを鳴らすかは PlayerAnimPart（Resources/Json/Tuning/PlayerAnim/*.json、25 番）。
	// STG は railStand（Launch の最後のコマで止める＝スタート演出の機体と同じ姿勢のまま渡す）、
	// ボス戦と着陸で足場に着いた後は bossIdle。フェーズが替わったので 1 回きり・押している間はやめる
	// 自機のアニメは自機の時間（Player）で進める：近接の派生でワールドを止めても技のアニメは動く
	player_->SetTimeGroup(TimeGroup::Player);
	playerAnim_.SetPlayer(player_);
	playerAnim_.SetMode(CurrentPlayerAnimMode());
	playerAnim_.ClearActions();
	playerAnim_.Replay();
}

PlayerAnimMode StagePlayScene::CurrentPlayerAnimMode() const {
	const bool ground = phase_ == Phase::Boss
		|| (phase_ == Phase::Landing && bossStage_ && bossStage_->HasLandingTouchedDown());
	return ground ? PlayerAnimMode::Ground : PlayerAnimMode::Rail;
}

void StagePlayScene::SyncPlayerAnim(float dt) {
	if (!player_) return;
	const PlayerAnimMode mode = CurrentPlayerAnimMode();
	playerAnim_.SetMode(mode);
	// チャージ 1 段目が溜まった後：STG は滑空（8 番 §2.2.1）。地上のチャージの構え（chargeHold）は鳴らさない
	// （2026-11-28 ユーザー：チャージは撃たずにいると 3 秒で溜まるので、構えが移動のダッシュ・スキーを隠し続けていた）
	const bool charged = chargeStage1Triggered_;
	playerAnim_.SetRailGlide(mode == PlayerAnimMode::Rail && charged);
	playerAnim_.SetGroundMove(mode == PlayerAnimMode::Ground ? playerAnimGroundMove_ : Vector2{ 0.0f, 0.0f });
	playerAnim_.SetHeld(PlayerAnimState::PrecisionAim, precisionHeld_);
	// 必殺技：Storm Lock は発動中ずっと、Shatterline は段（構え → 振り抜き → 硬直）ごと
	playerAnim_.SetHeld(PlayerAnimState::StormLock, specialActive_ && equippedSpecial_ == SpecialKind::StormLock);
	const ShatterlinePart::PoseStage stage = (specialActive_ && equippedSpecial_ == SpecialKind::Shatterline)
		? shatterline_.GetPoseStage() : ShatterlinePart::PoseStage::None;
	playerAnim_.SetHeld(PlayerAnimState::ShatterlineCharge, stage == ShatterlinePart::PoseStage::Charge);
	playerAnim_.SetHeld(PlayerAnimState::ShatterlineSlash, stage == ShatterlinePart::PoseStage::Slash);
	playerAnim_.SetHeld(PlayerAnimState::ShatterlineRecover, stage == ShatterlinePart::PoseStage::Recover);
	playerAnim_.Update(dt);
}

Vector2 StagePlayScene::GroundMoveToPlayerLocal(const Vector2& moveDelta) const {
	if (!bossStage_) return { 0.0f, 0.0f };
	Vector3 gfwd, gright;
	bossStage_->ComputeGroundBasis(gfwd, gright);
	const float wx = gright.x * moveDelta.x + gfwd.x * moveDelta.y;
	const float wz = gright.z * moveDelta.x + gfwd.z * moveDelta.y;
	// 自機の向き（yaw = atan2(x, z)）の前 (sin, cos) と右 (cos, -sin) へ写す
	const float s = std::sin(bossFacingYaw_);
	const float c = std::cos(bossFacingYaw_);
	return { wx * c - wz * s, wx * s + wz * c };
}

void StagePlayScene::TriggerDodgeAnim(const Vector2& moveDelta) {
	const bool ground = CurrentPlayerAnimMode() == PlayerAnimMode::Ground;
	const Vector2 dir = ground ? GroundMoveToPlayerLocal(moveDelta) : moveDelta;
	PlayerAnimState state = ground ? PlayerAnimState::GroundDodgeNeutral : PlayerAnimState::RailDodgeNeutral;
	if (std::sqrt(dir.x * dir.x + dir.y * dir.y) > kNearZero) {
		if (std::fabs(dir.x) >= std::fabs(dir.y)) {
			if (ground) state = dir.x < 0.0f ? PlayerAnimState::GroundDodgeLeft : PlayerAnimState::GroundDodgeRight;
			else        state = dir.x < 0.0f ? PlayerAnimState::RailDodgeLeft : PlayerAnimState::RailDodgeRight;
		} else {
			if (ground) state = dir.y < 0.0f ? PlayerAnimState::GroundDodgeBack : PlayerAnimState::GroundDodgeForward;
			else        state = dir.y < 0.0f ? PlayerAnimState::RailDodgeDown : PlayerAnimState::RailDodgeUp;
		}
	}
	playerAnim_.Trigger(state);
}

void StagePlayScene::StopDodgeAnim() {
	// 回避の状態は RailDodgeNeutral〜GroundDodgeRight に並んでいる
	for (int s = static_cast<int>(PlayerAnimState::RailDodgeNeutral); s <= static_cast<int>(PlayerAnimState::GroundDodgeRight); ++s) {
		playerAnim_.Stop(static_cast<PlayerAnimState>(s));
	}
}

void StagePlayScene::Seek(float seconds) {
	CompleteDeferredInit();
	if (cloudTopIntro_) cloudTopIntro_->Cancel();
	if (playerDeath_) playerDeath_->Cancel();
	// Boss/Landing 中に Rail タイムラインを Seek したら Rail へ戻す（ボス・地面・コントローラを掃除）。
	if (phase_ != Phase::Rail) {
		if (stageStart_) stageStart_->Skip();
		if (bossStage_) bossStage_->Reset();
		bossBattleActive_ = false;
		phase_       = Phase::Rail;
		prevPhase_   = Phase::Rail;
		landingTimer_ = 0.0f;
	}
	Scene::Seek(seconds);
	if (railStage_) railStage_->Seek(seconds);
	// 環境セクションはクロスフェードを挟まず即時適用する（巻き戻しで色を確認するため）。
	if (stageEnv_) stageEnv_->Reset(seconds);
	// 大河の水面のロード窓より後へ飛んだら、その場で読み終える
	if (riverWater_) riverWater_->Seek(seconds);
	// ストリーミング：作る時刻より後へ飛んだらその場で作る（前へ戻っても捨てない。破棄は L9 の後半）
	if (streaming_) streaming_->Seek(seconds);
}

// IRailStageHost::GetPlayer() の実装。AnimatedObject3DInstance の完全型が必要なため .cpp で定義。
IImGuiEditable* StagePlayScene::GetPlayer() const {
	return player_;
}

// IRailStageHost::RegisterEnemyController() の実装。unique_ptr<EnemyController> の値渡しには
// 完全型（デストラクタ）が必要なため .cpp で定義。
void StagePlayScene::RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) {
	GameScene::RegisterEnemyController(std::move(ctrl));
}

void StagePlayScene::RebuildCloudShaft() {
	if (!railStage_ || !stageEnv_ || !stageEnv_->GetCloudRaymarcher()) return;
	// レールの降下区間を 16 点で拾う（レールを直せば穴も付いてくる）
	std::vector<Vector3> shaftPath;
	for (int i = 0; i < kCloudShaftSampleCount; ++i) {
		const float sec = kCloudShaftStartSec + (kCloudShaftEndSec - kCloudShaftStartSec) * static_cast<float>(i) / kCloudShaftSampleLastIndex;
		shaftPath.push_back(railStage_->SampleCameraPathAtSec(sec));
	}
	stageEnv_->GetCloudRaymarcher()->SetShaftPath(shaftPath);
	// 雲の流れの向き＝自機の最初の進行方向（レール t=0 から少し先への向き）。雲はこの逆へ流れる
	const Vector3 a = railStage_->SampleCameraPathAtSec(0.0f);
	const Vector3 b = railStage_->SampleCameraPathAtSec(kCloudFlowDirectionSampleSec);
	stageEnv_->SetCloudFlowDirection({ b.x - a.x, b.z - a.z });
}

// IRailStageHost::EnsureCameraPathSpline() の実装。
// レールカメラの走行スプラインをシーンの持ち物にすることで、シーン JSON に載せて
// エンジン内エディタ / Blender の両方から編集できるようにする。
SplineCurveActor* StagePlayScene::EnsureCameraPathSpline(const std::vector<Vector3>& defaultPoints) {
	// 既にシーンにあるならそれを使う（JSON からロードされたものが最優先）
	for (const auto& sp : GetDynamicSplineList()) {
		if (sp && Gameplay::Of(sp).GetTag() == EntityTag::CameraPathSpline) {
			return sp.get();
		}
	}

	// 無ければ既定値を種にして作る。こうしておくと初回起動で Hierarchy に現れ、
	// Save Scene すれば JSON 化されて以後は完全にデータとして扱える（自動移行）。
	SplineCurveActor* added = AddDynamicSplineActor(EntityTag::CameraPathSpline);
	added->SetName("CameraPath");
	added->MutablePoints() = defaultPoints;
	return added;
}

// IRailStageHost::ClearWaveRuntimeState() の実装。RailStagePart::Seek() から host_ 経由で呼ばれる。
// 現在生きている敵・弾・スプライン追従敵・敵コントローラをすべて掃除する
// （enemyControllers_ は持っている敵のポインタがダングリングになるので必ずクリアする）。
void StagePlayScene::ClearWaveRuntimeState() {
	ClearCombatRuntime();
	meleeCombo_.Reset();
	// 敵・ボスと、自機/敵の弾・近接判定を遅延破棄する。
	// 非アニメ Object3D の敵（ScreenHover/Static 型プレハブ例: HoverEnemy=kind:Object3D）も含める。
	// これを漏らすと Seek のたびに固定敵が消えず、再スポーンで分身する。
	DestroyDynamicEntitiesIf([](IImGuiEditable* e) {
		const EntityTag t = Gameplay::Of(e).GetTag();
		return t == EntityTag::Enemy || t == EntityTag::Boss
			|| t == EntityTag::PlayerBullet || t == EntityTag::EnemyAttack
			|| t == EntityTag::PlayerMelee;
	});
}

// IBossStageHost::ClearBossRuntimeState() の実装。BossStagePart::Reset() から host_ 経由で呼ばれる。
// 敵/弾/敵コントローラ掃除は Wave 用と共通。加えてボス戦専用の Terrain（地面）Primitive も掃除する。
void StagePlayScene::ClearBossRuntimeState() {
	ClearWaveRuntimeState();
	// プリミティブに限る。シーン JSON の地形（CanyonTerrain など Object3D の Terrain）まで消すと、
	// Debug の自動保存で配置から消えてしまう（2026-11-24 に谷の地形が消えた）
	DestroyDynamicEntitiesIf([](IImGuiEditable* e) {
		return dynamic_cast<PrimitiveInstance*>(e) != nullptr && Gameplay::Of(e).GetTag() == EntityTag::Terrain;
	});
}

// IRailStageHost::TriggerRetreatForWaveEntry() の実装。EnemyController の完全型が必要なため .cpp で定義。
void StagePlayScene::TriggerRetreatForWaveEntry(int waveEntryIndex) {
	if (EnemyController* ctrl = FindEnemyControllerByWave(waveEntryIndex)) ctrl->TriggerRetreat();
}

Camera* StagePlayScene::GetCamera() {
	return camera_.get();
}

bool StagePlayScene::OnViewportPrefabDrop(const std::string& prefabName, float relX, float relY) {
	return railStage_ ? railStage_->OnViewportPrefabDrop(prefabName, relX, relY) : false;
}

float StagePlayScene::GetCameraProgressT() const {
	return railStage_ ? railStage_->GetCameraProgressT() : -1.0f;
}

float StagePlayScene::GetSeekMaxSeconds() const {
	return seekMaxSec_;
}

// =====================================================================
// =====================================================================
// シーン保存 / 読込
//   本体は GameScene に集約済み（JSON の形は SceneSerializer が持つ）。
//   ここには StagePlay 固有の差分だけを置く。
// =====================================================================

std::string StagePlayScene::GetSceneJsonName() const {
	return "StagePlayScene";
}

bool StagePlayScene::ShouldSkipOnSave(const IImGuiEditable* entity, EntityTag tag) const {
	// プレイヤーは常にプレハブから復元する（collider 等のプレハブ属性を確実に反映するため）
	if (tag == EntityTag::Player) return true;
	// 弾・攻撃判定は一時オブジェクト
	if (tag == EntityTag::PlayerBullet || tag == EntityTag::EnemyAttack) return true;
	// 敵・ボスは Wave JSON からスポーンするランタイム実体。**シーン JSON には絶対に保存しない**
	// （プレイ中に自動保存が走ると、湧いた敵がマップオブジェクトとして焼き込まれてしまうため）。
	if (tag == EntityTag::Enemy || tag == EntityTag::Boss) return true;
	// 谷の障害物が崩れた部品も同じくランタイム実体
	if (railStage_ && railStage_->IsObstacleDebris(entity)) return true;
	// 敵が落とした物も
	if (railStage_ && railStage_->IsLootDrop(entity)) return true;
	// ランタイム UI（HP バー / 必殺ゲージ）は毎回建て直すので保存しない
	if (entity == hpBarBackground_ || entity == hpBarForeground_ ||
		entity == gaugeBarBackground_ || entity == gaugeBarForeground_) return true;
	return false;
}

bool StagePlayScene::ShouldSkipOnLoad(const SceneEntityDesc& desc) const {
	// 過去の汚れた save 対策：一時オブジェクト・ランタイム敵が混ざっていても復元しない
	if (desc.tag == EntityTag::PlayerBullet || desc.tag == EntityTag::EnemyAttack) return true;
	if (desc.tag == EntityTag::Enemy || desc.tag == EntityTag::Boss) return true;
	if (desc.kind == SceneEntityDesc::Kind::Sprite &&
		(desc.name == "HPBarBackground" || desc.name == "HPBarForeground" ||
		 desc.name == "SpecialGaugeBackground" || desc.name == "SpecialGaugeForeground")) {
		return true;
	}
	return false;
}

bool StagePlayScene::ShouldStreamOnLoad(const SceneEntityDesc& desc) const {
	// 時刻表にあるグループだけ後で作る。知らないグループ（まだ時刻の決まっていない "Canyon" など）はすぐ作る
	return streaming_ && streaming_->HasGroup(desc.streamGroup);
}

void StagePlayScene::OnBeforeSceneLoad() {
	// 全エンティティが消えるので、それを指す参照とゲーム状態を落とす
	player_ = nullptr;
	// 敵のコントローラは持っている敵のポインタがダングリングになるので必ず捨てる（Seek リセットと同様）
	ClearCombatRuntime();
	ResetDodgeState();
}

void StagePlayScene::OnAfterSceneLoad() {
	RelinkPlayerAfterSceneLoad();
	RebindRailAfterSceneLoad();
	RebuildUiAfterSceneLoad();
}

void StagePlayScene::RelinkPlayerAfterSceneLoad() {
	// 復元されたエンティティから player_ を貼り直す
	for (const auto& a : GetDynamicAnimated()) {
		if (a && Gameplay::Of(a).GetTag() == EntityTag::Player) {
			player_ = a.get();
			break;
		}
	}

	// Player はセーブに含めないため、存在しなければプレハブから再生成する
	if (!player_) {
		player_ = dynamic_cast<AnimatedObject3DInstance*>(InstantiatePrefab("player", { 0.0f, 0.0f, 0.0f }));
		if (player_) player_->SetName("Player");
	}
	// 新モデルはクリップが複数あり既定のアニメが付かない（T ポーズになる）ので姿勢を差す
	ApplyPlayerPose();
}

void StagePlayScene::RebindRailAfterSceneLoad() {
	// dynamicSplines_ を作り直したので、レールカメラの走行スプラインを取り直す。
	// これを忘れると解放済みのスプラインを指したまま走る（Auto Reload で毎回踏む）。
	if (railStage_) railStage_->RebindCameraPath();
	// 雲海の縦穴もレールから拾い直す（ここより前はレールが既定の仮の形で、穴が降下ルートとずれる）
	RebuildCloudShaft();
}

void StagePlayScene::RebuildUiAfterSceneLoad() {
	// シーンの動的スプライトを全削除したので UI スプライトを建て直す（生ポインタの dangling 回避）
	InitializeHPBarUI();
	InitializeSpecialGaugeUI();
	if (player_) {
		Gameplay::Of(player_).GetHP().enabled = true;
	}
}

void StagePlayScene::InitializeHPBarUI() {
	hpBarBackground_ = nullptr;
	hpBarForeground_ = nullptr;

	if (!GetSpriteManager()) return;

	const Vector2 hpBarPos{ hpBarPosX_, hpBarPosY_ };

	// 赤ゲージ背景（補間で遅延追従）
	auto bgSprite = std::make_unique<SpriteInstance>();
	bgSprite->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "HPBarBackground");
	bgSprite->SetPosition(hpBarPos);
	bgSprite->SetSize({ hpBarMaxWidth_, hpBarHeight_ });
	bgSprite->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
	hpBarBackground_ = AdoptDynamicSprite(std::move(bgSprite));

	// 緑ゲージ前景（現在HP に即時追従）
	auto fgSprite = std::make_unique<SpriteInstance>();
	fgSprite->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "HPBarForeground");
	fgSprite->SetPosition(hpBarPos);
	fgSprite->SetSize({ hpBarMaxWidth_, hpBarHeight_ });
	fgSprite->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f });
	hpBarForeground_ = AdoptDynamicSprite(std::move(fgSprite));

	hpBarCurrentRatio_ = 1.0f;
	hpBarTargetRatio_ = 1.0f;

	// プレイヤー HP を有効化（プレハブで enabled=false の場合に備えて）
	if (player_) {
		Gameplay::Of(player_).GetHP().enabled = true;
	}
}

void StagePlayScene::UpdatePlayerDamageAndUI(float deltaTime) {
	PEPPER_SCOPE("StagePlayScene::UpdatePlayerDamageAndUI");
	if (!player_) return;

	// シェイクオフセットの計算を進める
	if (camera_) {
		camera_->UpdateShake(deltaTime);
	}

	// ----- 無敵状態の集約（無敵の発生源で点滅色を出し分ける）-----
	if (playerInvincibilityTimer_ > 0.0f) playerInvincibilityTimer_ -= deltaTime;
	if (shootLockoutTimer_ > 0.0f)        shootLockoutTimer_ -= deltaTime;

	const bool justInv    = justDodgeActive_;                                    // ジャスト回避中：無敵・点滅なし
	const bool dodgeInv   = dodgeActive_ && dodgeTimer_ <= dodgeIFrameDuration_; // 回避無敵：水色点滅
	const bool dmgInv     = playerInvincibilityTimer_ > 0.0f;                    // 被弾無敵：赤/白点滅
	const bool specialInv = specialActive_ && specialInvincible_;                // 必殺技発動中（後隙除く）：完全無敵
	const bool invincible = justInv || dodgeInv || dmgInv || specialInv;

	// 色のコマは発光を足して色を付ける（色を掛ける作りだと、青い自機の新モデルに赤を掛けてもほぼ黒になり、
	// 被弾が白のコマ＝自機の青だけに見えた。2026-10-19 実機）。半透明は今まで通り色の α
	const float blinkFreq = damageBlinkFrequency_;
	const Vector3 noGlow = { 0.0f, 0.0f, 0.0f };
	if (IsPlayerDying()) {
		// 死亡演出の間は点滅させない（エーテルの光は ApplyPlayerLook が足す）
		playerBlinkGlow_ = noGlow;
	} else if (justInv) {
		// ジャスト回避中は無敵だが点滅させない（ソリッド表示）
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		playerBlinkGlow_ = noGlow;
	} else if (dodgeInv) {
		// 回避（〜ジャスト窓〜回避無敵）：水色／素の点滅
		float phase = std::fmod(dodgeTimer_ * blinkFreq, 1.0f);
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, damageBlinkAlpha_ });
		playerBlinkGlow_ = phase < 0.5f ? noGlow : dodgeBlinkEmissive_;
	} else if (dmgInv) {
		// 被弾無敵：赤／素の点滅
		float phase = std::fmod(playerInvincibilityTimer_ * blinkFreq, 1.0f);
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, damageBlinkAlpha_ });
		playerBlinkGlow_ = phase < 0.5f ? noGlow : damageBlinkEmissive_;
	} else if (wasInvincible_) {
		// 無敵終了直後に通常色へ戻す（1度だけ）
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		playerBlinkGlow_ = noGlow;
	}
	wasInvincible_ = invincible;

	// ----- 敵攻撃との接触判定（敵弾＋突進）-----
	// ジャスト回避中は完全スルー。それ以外は、回避ジャスト窓内の接触＝ジャスト回避成立、
	// 各種無敵中＝ダメージ無効、いずれでもなければ通常被弾。死亡演出の間は当たらない。
	const bool stageClearing = (phase_ == Phase::Boss && clearSeq_.IsActive());
	if (!justInv && !IsPlayerDying() && !stageClearing) {
		const Vector3 playerPos = player_->GetTranslate();

		// 敵弾の当たり判定はプレハブのカプセル。中心・軸は UpdatePlayerColliderPose が決めたもの（デバッグ描画と同じ）
		if (!Gameplay::Of(player_).HasColliderPose()) UpdatePlayerColliderPose();
		const Collider& playerCol = Gameplay::Of(player_).GetCollider();
		const Vector3 bodyCenter = Gameplay::Of(player_).GetColliderPoseCenter();
		const Vector3* capsuleAxes = Gameplay::Of(player_).GetColliderPoseAxes();

		bool            hitFound = false;
		int             incomingDamage = 0;
		PrimitiveInstance* hitBullet = nullptr; // 当たった敵弾
		IImGuiEditable* attacker = nullptr; // ジャスト演出のハイライト対象
		bool            heavyHit = false;   // 近接系の判定ボリューム（ボス戦のノックバックを大きくする）
		Vector3         hitSourcePos = playerPos; // ノックバックの押し出し元

		// 敵弾（最初に当たった 1 発だけを見る）
		ForEachEnemyBullet([&](PrimitiveInstance* bullet) {
			if (hitFound) return;
			const Vector3* bp = bullet->GetEditableTranslate();
			if (!bp) return;
			const float bulletR = Gameplay::Of(bullet).GetCollider().radius;
			if (CollisionGeometry::TestSphereCapsule(*bp, bulletR, bodyCenter, capsuleAxes,
				playerCol.capsuleHeight, playerCol.capsuleRadius)) {
				incomingDamage = Gameplay::Of(bullet).GetDamageDealer().damage;
				if (incomingDamage <= 0) incomingDamage = kDefaultContactDamage;
				hitBullet = bullet;
				// 弾の発射元は不明なので画面上最近の敵を演出対象にする。ただしボスは Enemy タグ
				// ではないため nearestEnemy_ が更新されず、STG時代の破棄済みポインタが残ったまま
				// になりうる（ダングリング参照でクラッシュする）。ボス戦はボス本体を直接使う。
				attacker = (phase_ == Phase::Boss && bossStage_) ? bossStage_->GetBossEntity() : nearestEnemy_;
				hitSourcePos = *bp;
				hitFound = true;
			}
		});

		// 突進など「攻撃接触中」の敵（弾が当たっていない場合のみ。ただの移動接触は IsContactDamageActive()=false で除外）
		// 形どおり（球・カプセル・OBB）に判定する。谷の障害物は細長いカプセル／梁なので球の距離では合わない
		if (!hitFound) {
			const Collider& pc = Gameplay::Of(player_).GetCollider();
			CollisionGeometry::WorldData pw;
			const bool playerShape = pc.enabled && CollisionManager::GetWorldData(player_, pc, pw);
			ForEachEnemyController([&](const EnemyController& ctrl) {
				if (!playerShape || hitFound) return;
				if (!ctrl.IsContactDamageActive() || !ctrl.GetEntity()) return;
				IImGuiEditable* e = ctrl.GetEntity();
				const Vector3* ep = e->GetEditableTranslate();
				if (!ep) return;
				const Collider& ec = Gameplay::Of(e).GetCollider();
				CollisionGeometry::WorldData ew;
				if (!CollisionGeometry::TryGetWorldData(e, ec, ew)) return;
				if (CollisionGeometry::TestPair(pc, pw, ec, ew)) {
					incomingDamage = Gameplay::Of(e).GetDamageDealer().damage;
					if (incomingDamage <= 0) incomingDamage = kDefaultContactDamage;
					attacker = e;
					hitSourcePos = *ep;
					hitFound = true;
				}
			});
		}

		// 弾以外の敵攻撃判定（ボスの近接/突進の BossMeleeHit 等）。OBB もあるので CollisionManager と同じ判定を使う。
		// 以前は CollisionManager の自動ダメージ（毎フレーム・無敵無視）だけで処理されていて即死の原因だった。
		if (!hitFound) {
			const Collider& pc = Gameplay::Of(player_).GetCollider();
			CollisionGeometry::WorldData pw;
			if (pc.enabled && CollisionManager::GetWorldData(player_, pc, pw)) {
				for (auto& p : GetDynamicPrimitives()) {
					if (!p || Gameplay::Of(p.get()).GetTag() != EntityTag::EnemyAttack) continue;
					if (IsBullet(p.get())) continue;
					const Collider& vc = Gameplay::Of(p.get()).GetCollider();
					if (!vc.enabled) continue;
					CollisionGeometry::WorldData vw;
					if (!CollisionGeometry::TryGetWorldData(p.get(), vc, vw)) continue;
					if (!CollisionGeometry::TestPair(pc, pw, vc, vw)) continue;

					incomingDamage = Gameplay::Of(p.get()).GetDamageDealer().damage;
					if (incomingDamage <= 0) incomingDamage = kDefaultContactDamage;
					// 判定ボリュームは攻撃後すぐ破棄されるので、演出対象・押し出し元はボス本体にする
					IImGuiEditable* boss = (phase_ == Phase::Boss && bossStage_) ? bossStage_->GetBossEntity() : nullptr;
					attacker = boss;
					const Vector3* src = boss ? boss->GetEditableTranslate() : p->GetEditableTranslate();
					if (src) hitSourcePos = *src;
					heavyHit = true;
					hitFound = true;
					break;
				}
			}
		}

		// 防御系のバリア（RB）が張られている間は、バリアが受ける（ジャスト回避もダメージも起きない。20 番 §3.2）
		// 回避中でも先にバリアが受ける（バリアの間はジャスト回避が起きない）。被弾無敵・必殺技の無敵の間は減らさない
		if (hitFound && !dmgInv && !specialInv && flightUnit_ &&
			flightUnit_->TryAbsorbHit(DifficultyProfile::ScaleInt(incomingDamage, GetGameSession()->GetDifficultyProfile()->GetDamageScale()))) {
			if (hitBullet) KillBullet(hitBullet);
			hitFound = false;
		}

		// 近接の技の前方ガード（24 番 §8。Y1 の盾を構えた突進）：正面から守る角度の内側で受けた攻撃は通さない（弾は消す）
		float guardAngleDeg = 0.0f;
		if (hitFound && meleeCombo_.IsGuarding(guardAngleDeg)) {
			Vector3 front{};
			if (phase_ == Phase::Boss) {
				front = { std::sin(bossFacingYaw_), 0.0f, std::cos(bossFacingYaw_) };
			} else {
				Vector3 aimRight, aimUp;
				ComputeAimBasis(aimRight, aimUp, front);
			}
			Vector3 toSource{ hitSourcePos.x - playerPos.x, hitSourcePos.y - playerPos.y, hitSourcePos.z - playerPos.z };
			// ボス戦は水平だけで見る（地上の攻撃は高さがまちまち）
			if (phase_ == Phase::Boss) toSource.y = 0.0f;
			const float srcLen = std::sqrt(toSource.x * toSource.x + toSource.y * toSource.y + toSource.z * toSource.z);
			const float frontLen = std::sqrt(front.x * front.x + front.y * front.y + front.z * front.z);
			// 攻撃元が自機と重なっている（向きが出ない）時は正面から来たものとして守る
			const bool inFront = (srcLen <= kTinyEpsilon || frontLen <= kTinyEpsilon)
				|| ((toSource.x * front.x + toSource.y * front.y + toSource.z * front.z) / (srcLen * frontLen)
					>= std::cos(DegToRad(guardAngleDeg)));
			if (inFront) {
				if (hitBullet) KillBullet(hitBullet);
				if (IsShieldBeamVisible()) beamShield_->Flash(1.0f);
				hitFound = false;
			}
		}

		if (hitFound) {
			const float justWindow = dodgeJustWindow_ + (flightUnit_ ? flightUnit_->GetJustDodgeWindowBonus() : 0.0f)
				+ meleeCombo_.GetBuffJustWindowAdd();
			if (dodgeActive_ && dodgeTimer_ <= justWindow) {
				// ジャスト回避成立（スロー＋演出＋回復ストック＋スコア）
				TriggerJustDodge(attacker);
				dodgeActive_ = false; // 回避無敵を消費し、以降はジャスト無敵へ移行
				if (hitBullet) KillBullet(hitBullet);
			} else if (dodgeInv || dmgInv || specialInv) {
				// 無敵中：ダメージ無効（弾はそのまま通過＝既存の被弾無敵と同様）
			} else {
				// 通常被弾（敵の攻撃のダメージに純度の倍率。19 番 §5）。防御系の常時の軽減もここで掛ける
				int damage = DifficultyProfile::ScaleInt(incomingDamage, GetGameSession()->GetDifficultyProfile()->GetDamageScale());
				if (flightUnit_) damage = flightUnit_->ModifyIncomingDamage(damage);
				if (const ShieldDef* shield = GetGameSession()->GetLoadoutState()->GetShield()) damage = shield->ReduceDamage(damage);
				// ビームシールドは受けた瞬間だけ強く光る
				if (IsShieldBeamVisible()) beamShield_->Flash(1.0f);
				OnPlayerTakeDamage(damage);
				// ボス戦：攻撃元から離れる向きへノックバック（弾=小 / 近接=大）。大技のスーパーアーマー中は吹き飛ばない
				if (phase_ == Phase::Boss && bossStage_ && !meleeCombo_.HasSuperArmor()) {
					bossStage_->ApplyKnockback(hitSourcePos, heavyHit);
				}
				// 被弾エフェクト：攻撃側（敵弾）の "hit" ＋ プレイヤーの "hurt" を再生
				IImGuiEditable* atkPrefab = hitBullet
					? static_cast<IImGuiEditable*>(hitBullet)
					: attacker;
				PlayHitEffects(atkPrefab, player_, player_->GetTranslate());
				if (hitBullet) KillBullet(hitBullet);
			}
		}
	}

	UpdateHPBarUI();

	// HP=0 で死亡演出（19_GameOverAndDifficulty.md §1）→ 終わったらゲームオーバー画面へ（FinishPlayerDeath。リトライ方法はGameOverSceneで選択）
	if (Gameplay::Of(player_).GetHP().IsDead() && !gameOverTriggered_) {
		gameOverTriggered_ = true;
		SessionLogger::GetInstance()->Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
			"GAMEOVER reason=player_dead x=" + std::to_string(playerInputOffset_.x) + " y=" + std::to_string(playerInputOffset_.y));
		// 調整用の演出（preview）中に死んだら、本番の演出で始め直す
		if (playerDeath_) {
			BeginPlayerDeath(false);
		} else {
			SceneManager::GetInstance()->ChangeScene("GAMEOVER");
		}
	}
}

void StagePlayScene::OnPlayerTakeDamage(int damageAmount) {
	if (!player_) return;

	Gameplay::Of(player_).GetHP().TakeDamage(damageAmount);
	SoundManager::GetInstance()->Play2DSound("se_player_damage");
	// 大技のスーパーアーマー中はひるまない（被弾のアニメで技のクリップを止めない。ダメージはそのまま）
	if (!meleeCombo_.HasSuperArmor()) {
		playerAnim_.Trigger(CurrentPlayerAnimMode() == PlayerAnimMode::Ground ? PlayerAnimState::GroundHit : PlayerAnimState::RailHit);
	}

	playerInvincibilityTimer_ = playerInvincibilityDuration_;
	shootLockoutTimer_ = shootLockoutDuration_;

	if (camera_) {
		camera_->Shake(damageCameraShakeIntensity_, damageCameraShakeDuration_);
	}
}

// ----- ボスの必殺技 GravityOrb（IBossGravityOrbHost）-----

BossGravityOrb* StagePlayScene::GetBossGravityOrb() {
	return (phase_ == Phase::Boss) ? gravityOrb_.get() : nullptr;
}

IImGuiEditable* StagePlayScene::GetGravityOrbPlayer() const {
	return player_;
}

float StagePlayScene::GetGravityOrbGroundY() const {
	return bossStage_ ? bossStage_->GetGroundY() : 0.0f;
}

void StagePlayScene::OnGravityOrbPlayerHit(float damageRatio) {
	// 死亡演出の間は追い打ちしない。回避の無敵では避けられない（盾の岩に隠れる技なので）
	if (!player_ || IsPlayerDying()) return;
	const HP& hp = Gameplay::Of(player_).GetHP();
	if (!hp.enabled || hp.maxHP <= 0) return;
	const int damage = (std::max)(1, static_cast<int>(std::ceil(static_cast<float>(hp.maxHP) * damageRatio)));
	OnPlayerTakeDamage(damage);
}

void StagePlayScene::ShakeGravityOrbCamera(float intensity, float duration) {
	if (camera_) camera_->Shake(intensity, duration);
}

void StagePlayScene::SetGravityOrbSkyDark(bool dark) {
	if (dark == gravityOrbSkyDark_) return;
	gravityOrbSkyDark_ = dark;
	if (!skybox_) return;
	if (dark) {
		skybox_->FadeColor(specialSkyboxDarkColor_, specialSkyboxFadeIn_);
	} else {
		const Vector4& restore = (stageEnv_ && stageEnv_->HasSections()) ? stageEnv_->GetCurrentTint() : skyboxTint_;
		skybox_->FadeColor(restore, specialSkyboxFadeOut_);
	}
}

void StagePlayScene::UpdateHPBarUI() {
	if (!player_ || !hpBarForeground_ || !hpBarBackground_) return;

	const HP& hp = Gameplay::Of(player_).GetHP();
	if (hp.maxHP <= 0) return;

	// 現在 HP の比率（緑ゲージはこれに即時追従、赤ゲージは追従目標）
	float currentRatio = static_cast<float>(hp.currentHP) / static_cast<float>(hp.maxHP);
	if (currentRatio < 0.0f) currentRatio = 0.0f;
	if (currentRatio > 1.0f) currentRatio = 1.0f;
	hpBarCurrentRatio_ = currentRatio;
	hpBarTargetRatio_ = currentRatio;
	hpBarForeground_->SetSize({ hpBarMaxWidth_ * currentRatio, hpBarHeight_ });

	// 赤ゲージは緑ゲージへ線形に追従（双方向、必ず追いつく）
	float dt = GetScaledDeltaTime();
	float currentRedRatio = hpBarBackground_->GetSize().x / hpBarMaxWidth_;
	float diff = hpBarTargetRatio_ - currentRedRatio;
	float step = hpBarLerpSpeed_ * dt;
	float absDiff = (diff < 0.0f) ? -diff : diff;
	if (absDiff <= step) {
		currentRedRatio = hpBarTargetRatio_;
	} else if (diff < 0.0f) {
		currentRedRatio -= step;
	} else {
		currentRedRatio += step;
	}
	if (currentRedRatio < 0.0f) currentRedRatio = 0.0f;
	if (currentRedRatio > 1.0f) currentRedRatio = 1.0f;
	hpBarBackground_->SetSize({ hpBarMaxWidth_ * currentRedRatio, hpBarHeight_ });
}

void StagePlayScene::InitializeSpecialGaugeUI() {
	gaugeBarBackground_ = nullptr;
	gaugeBarForeground_ = nullptr;
	if (!GetSpriteManager()) return;

	const Vector2 pos{ gaugeBarPosX_, gaugeBarPosY_ };

	// 背景（暗色フル幅）
	auto bg = std::make_unique<SpriteInstance>();
	bg->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "SpecialGaugeBackground");
	bg->SetPosition(pos);
	bg->SetSize({ gaugeBarMaxWidth_, gaugeBarHeight_ });
	bg->SetColor(gaugeBarBgColor_);
	gaugeBarBackground_ = AdoptDynamicSprite(std::move(bg));

	// 前景（現在値）
	auto fg = std::make_unique<SpriteInstance>();
	fg->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "SpecialGaugeForeground");
	fg->SetPosition(pos);
	fg->SetSize({ 0.0f, gaugeBarHeight_ });
	fg->SetColor(gaugeBarFgColor_);
	gaugeBarForeground_ = AdoptDynamicSprite(std::move(fg));
}

void StagePlayScene::OnPlayerMeleeHit([[maybe_unused]] IImGuiEditable* target, const std::string& prefabName, bool clean) {
	if (!player_ || specialActive_) return;
	(void)prefabName;
	float gain = meleeCombo_.GetCurrentGaugeGain();
	if (!clean) gain *= meleeLateGaugeRate_;
	specialGauge_ = (std::min)(specialGaugeMax_, specialGauge_ + gain);
}

void StagePlayScene::UpdateSpecialGaugeUI() {
	PEPPER_SCOPE("StagePlayScene::UpdateSpecialGaugeUI");
	if (!gaugeBarBackground_ || !gaugeBarForeground_) return;

	// 位置（ImGuiでチューニングされた値を毎フレ反映）
	gaugeBarBackground_->SetPosition({ gaugeBarPosX_, gaugeBarPosY_ });
	gaugeBarForeground_->SetPosition({ gaugeBarPosX_, gaugeBarPosY_ });
	gaugeBarBackground_->SetSize({ gaugeBarMaxWidth_, gaugeBarHeight_ });
	gaugeBarBackground_->SetColor(gaugeBarBgColor_);

	// 比率
	float ratio = (specialGaugeMax_ > kGaugeMaxEpsilon) ? specialGauge_ / specialGaugeMax_ : 0.0f;
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	gaugeBarForeground_->SetSize({ gaugeBarMaxWidth_ * ratio, gaugeBarHeight_ });

	// MAX のとき色を切替（発動可能サイン）。発動中は前景非表示（消費済み演出）
	if (specialActive_) {
		gaugeBarForeground_->SetSize({ 0.0f, gaugeBarHeight_ });
	} else if (specialGauge_ >= specialGaugeMax_) {
		gaugeBarForeground_->SetColor(gaugeBarFullColor_);
	} else {
		gaugeBarForeground_->SetColor(gaugeBarFgColor_);
	}
}

void StagePlayScene::UpdateSpecialMove(InputActionMap* actions, float dt) {
	PEPPER_SCOPE("StagePlayScene::UpdateSpecialMove");
	if (!actions) return;

	if (specialActive_) {
		// 発動中：総タイマーを進め、装備中の必殺技の段取りを進める（終わったら EndSpecialMove でクールタイムへ）
		specialTimer_ += dt;

		// Shatterline は World がスロー/停止するため実時間で進める
		if (equippedSpecial_ == SpecialKind::Shatterline) {
			const bool firePressed = actions->IsTriggered(static_cast<int>(Action::Fire));
			if (shatterline_.Update(firePressed, GetDxCore()->GetDeltaTime())) EndSpecialMove();
			return;
		}

		// Storm Lock：Barrier / Fire / End は World の dt、Lockon は実時間で進む
		if (stormLock_.Update(dt, GetDxCore()->GetDeltaTime())) EndSpecialMove();
		return;
	}

	// 非発動時：クールタイムを消化（両必殺技共通）
	if (specialCooldownTimer_ > 0.0f) {
		specialCooldownTimer_ = (std::max)(0.0f, specialCooldownTimer_ - dt);
	}

	// 全能モード（タイトルで F9）：ゲージは常に満タン・クールタイム無し
	const bool showcase = AttractMode::GetInstance()->IsShowcase();
	if (showcase) {
		specialGauge_ = specialGaugeMax_;
		specialCooldownTimer_ = 0.0f;
	}

	// 発動待ち：行動可能チェック（ゲージ・クールタイムは TryTriggerSpecial 側で種別ごとに判定）
	if (IsActionLocked()) return;
	if (justDodgeActive_) return;

	// 種別ごとの専用キーで両方を別々に出せるよう分離する（本来は装備制で共通ボタン）。Debug と全能モードだけ。
	// Shatterline = G キー / 左スティック押し込み(L3)。Storm Lock = 既存 Ultimate(F/R3)。
#ifdef _DEBUG
	const bool separateKeys = true;
#else
	const bool separateKeys = showcase;
#endif
	if (separateKeys) {
		auto* kb  = GetInput()->GetKeyboard();
		auto* pad = GetInput()->GetController();
		const bool shatterlineKey =
			(kb && kb->TriggerKey(DIK_G)) ||
			(pad && pad->IsConnected() && pad->IsButtonTriggered(XINPUT_GAMEPAD_LEFT_THUMB));
		if (shatterlineKey) {
			TryTriggerSpecial(SpecialKind::Shatterline);
			return;
		}
	}

	// 通常：Ultimate(F/R3) で装備中の必殺技を発動。全能モードは G で Shatterline を撃つと装備が替わるので、F は Storm Lock に固定する
	if (actions->IsTriggered(static_cast<int>(Action::Ultimate))) {
		TryTriggerSpecial(showcase ? SpecialKind::StormLock : equippedSpecial_);
	}
}

void StagePlayScene::SetEquippedSpecial(SpecialKind k) {
	equippedSpecial_ = k;
	specialGaugeMax_ = SpecialGaugeMaxFor(k);
	// 新上限を超えていたらクランプ（上限の低い種別へ切替えたとき）
	specialGauge_ = (std::min)(specialGauge_, specialGaugeMax_);
}

void StagePlayScene::TryTriggerSpecial(SpecialKind k) {
	// 全能モードはゲージ・クールタイムを見ない（装備中と違う種別は上限が違うので、満タンでも足りないことがある）
	if (!AttractMode::GetInstance()->IsShowcase()) {
		if (specialCooldownTimer_ > 0.0f) return;               // クールタイム中は発動不可
		if (specialGauge_ < SpecialGaugeMaxFor(k)) return;      // 種別ごとの上限に満たない
	}
	TriggerSpecialMove(k);
}

void StagePlayScene::TriggerSpecialMove() {
	// 装備中種別を発動（ImGui ボタン等からの無条件発動用）
	TriggerSpecialMove(equippedSpecial_);
}

void StagePlayScene::TriggerSpecialMove(SpecialKind k) {
	// 発動した種別を装備中に同期（デバッグキーで別種別を撃った場合に合わせる）
	SetEquippedSpecial(k);

	specialActive_      = true;
	SoundManager::GetInstance()->Play2DSound("se_special");
	specialTimer_       = 0.0f;
	specialGauge_       = 0.0f;
	specialInvincible_  = true;  // 既定で無敵ON（Shatterline の後隙でのみ OFF）
	// HP.enabled の同期は RefreshPlayerHpInvincibility に一元化

	// 被弾シェイク中に発動すると World 停止で UpdateShake が進まず揺れが固まるため、即クリア
	if (camera_) camera_->StopShake();

	// 両方の必殺技で共通：自機を画面中央へ寄せる。入力は specialActive_ で全フェーズロックされる
	// （今の入力オフセット・速度は EndSpecialMove で戻す用に取っておく）
	specialPlayerInputOffsetSaved_ = playerInputOffset_;
	specialPlayerVelocitySaved_    = playerVelocity_;
	playerInputOffset_ = { 0.0f, 0.0f };
	playerVelocity_    = { 0.0f, 0.0f };

	if (k == SpecialKind::StormLock) {
		// Barrier へ
		stormLock_.Begin();
	} else {
		// Shatterline：Charge へ（専用の段取り）
		shatterline_.Begin();
	}
}

void StagePlayScene::DrawAfterPostEffect([[maybe_unused]] ID3D12GraphicsCommandList* commandList) {
	// PostEffect（崩壊リビールの色反転）適用後の最終 RT に重ねて描く。
	// シーン描画パス内（PostEffect 前）で描くと殻側に飛んだ破片が再反転で元色に戻る＝二重反転になるため。
	shatterline_.DrawShards();
}

bool StagePlayScene::IsSpecialTargetAlive(IImGuiEditable* e) const {
	if (!e) return false;
	for (auto& up : GetDynamicPrimitives()) if (up.get() == e) return true;
	for (auto& up : GetDynamicObjects()) if (up.get() == e) return true;
	for (auto& up : GetDynamicAnimated())   if (up.get() == e) return true;
	return false;
}

// ----- ISpecialMoveHost 実装 -----

Vector2 StagePlayScene::GetReticleScreenPos() const {
	if (reticle_) return reticle_->GetPosition();
	return { static_cast<float>(WindowsApplication::kUIWidth) * 0.5f, static_cast<float>(WindowsApplication::kUIHeight) * 0.5f };
}

void StagePlayScene::CollectEnemyBullets(std::vector<IImGuiEditable*>& out) {
	ForEachEnemyBullet([&](PrimitiveInstance* bullet) { out.push_back(bullet); });
}

void StagePlayScene::CollectEnemies(std::vector<IImGuiEditable*>& out) {
	auto consider = [&](IImGuiEditable* e) {
		if (!e) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag == EntityTag::Enemy || tag == EntityTag::Boss) out.push_back(e);
	};
	for (auto& up : GetDynamicPrimitives()) consider(up.get());
	for (auto& up : GetDynamicObjects()) consider(up.get());
	for (auto& up : GetDynamicAnimated())   consider(up.get());
}

void StagePlayScene::DestroyEnemyBullet(IImGuiEditable* bullet) {
	// 収集した後に消えた弾かもしれないが、KillBullet はポインタの比較だけなので安全
	KillBullet(bullet);
}

void StagePlayScene::DestroyEnemyWithScore(IImGuiEditable* e) {
	GetGameSession()->GetScoreManager()->AddScore(Gameplay::Of(e).GetScoreValue());
	GetGameSession()->GetScoreManager()->AddKill();
	DestroyDynamicEntity(e);
}

void StagePlayScene::CollectScreenTargets(std::vector<std::pair<IImGuiEditable*, bool>>& out) {
	if (!camera_) return;
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	auto isVisibleInClip = [&](const Vector3& w) {
		Vector4 clip = {
			w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0],
			w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1],
			w.x * vp.m[0][2] + w.y * vp.m[1][2] + w.z * vp.m[2][2] + vp.m[3][2],
			w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3],
		};
		if (clip.w <= 0.0f) return false;
		const float ndx = clip.x / clip.w;
		const float ndy = clip.y / clip.w;
		return std::abs(ndx) <= 1.0f && std::abs(ndy) <= 1.0f;
	};

	// 敵弾（EnemyAttack）
	ForEachEnemyBullet([&](PrimitiveInstance* bullet) {
		const Vector3* p = bullet->GetEditableTranslate();
		if (p && isVisibleInClip(*p)) out.emplace_back(static_cast<IImGuiEditable*>(bullet), true);
	});
	// 敵本体（Enemy / Boss）
	std::unordered_set<IImGuiEditable*> seen;
	auto pushEnemy = [&](IImGuiEditable* e) {
		if (!e || !seen.insert(e).second) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Vector3* p = e->GetEditableTranslate();
		if (!p || !isVisibleInClip(*p)) return;
		out.emplace_back(e, false);
	};
	for (auto& up : GetDynamicPrimitives()) pushEnemy(up.get());
	for (auto& up : GetDynamicObjects()) pushEnemy(up.get());
	for (auto& up : GetDynamicAnimated())   pushEnemy(up.get());
}

//====================
// 飛行ユニット（20_Loadout.md §3）
//====================

void StagePlayScene::UpdateFlightUnit(InputActionMap* actions) {
	if (!flightUnit_ || !flightUnit_->IsInitialized() || !player_) return;
	const bool inPlay = (phase_ == Phase::Rail || phase_ == Phase::Boss);
	// ジャスト回避・必殺技・死亡演出が時間を握っている間は技を出せない（機動系のスローはそこで終わる）
	const bool timeTaken = justDodgeActive_ || specialActive_ || IsPlayerDying();
	const bool canAct = inPlay && !timeTaken && !IsActionLocked();
	const bool triggered = actions && actions->IsTriggered(static_cast<int>(Action::FlightSkill));
	// World が止まっている間（エディタの一時停止・ヒットストップ・必殺技の止め）は技・支援射撃・クールタイムも止める。
	// 飛行ユニットは UI の時間で進むので、止めないと止まった World に支援射撃の弾が積もる
	if (GetTimeScale(TimeGroup::World) > 0.0f) {
		flightUnit_->Update(GetScaledDeltaTime(TimeGroup::UI), triggered, canAct, timeTaken);
	}

	// 防御系の HP 上限の補正。付け替えても残り HP の割合は保つ
	HP& hp = Gameplay::Of(player_).GetHP();
	if (playerBaseMaxHP_ < 0) playerBaseMaxHP_ = hp.maxHP;
	// 盾の加算 → 防御系の倍率の順（20 番 §2）
	const ShieldDef* shield = GetGameSession()->GetLoadoutState()->GetShield();
	const int baseMax = playerBaseMaxHP_ + (shield ? shield->maxHpAdd : 0);
	const int wantMax = (std::max)(1, static_cast<int>(std::lround(static_cast<float>(baseMax) * flightUnit_->GetMaxHpScale())));
	if (hp.maxHP != wantMax && hp.maxHP > 0) {
		const float ratio = static_cast<float>(hp.currentHP) / static_cast<float>(hp.maxHP);
		hp.maxHP = wantMax;
		hp.currentHP = (std::min)(wantMax, static_cast<int>(std::lround(ratio * static_cast<float>(wantMax))));
	}
}

void StagePlayScene::UpdatePlayerColliderPose() {
	if (!player_) return;
	const Vector3 t = player_->GetTranslate();
	const Vector3& off = Gameplay::Of(player_).GetCollider().offset;
	// 自機の回転（Object3D と同じ Rx·Ry·Rz）。行が右・上・前
	const Matrix4x4 m = MakeCameraRotateMatrix(player_->GetRotate());
	const Vector3 right{ m.m[0][0], m.m[0][1], m.m[0][2] };
	const Vector3 up{ m.m[1][0], m.m[1][1], m.m[1][2] };
	const Vector3 fwd{ m.m[2][0], m.m[2][1], m.m[2][2] };
	const Vector3 center{
		t.x + off.x * right.x + off.y * up.x + off.z * fwd.x,
		t.y + off.x * right.y + off.y * up.y + off.z * fwd.y,
		t.z + off.x * right.z + off.y * up.z + off.z * fwd.z };

	// STG の滑空中（チャージ 1 段目が溜まった後。8 番 §2.2.1）は、カプセルの軸（上）を前へ moveStateLeanDeg_ だけ倒す
	// （頭が前・足が後ろ。滑空のクリップ RailGlide の体の角度と同じ）。立ち姿勢（RailStand）は起こしたまま。
	// 条件は SyncPlayerAnim の railGlide と同じ
	const bool leaning = (phase_ == Phase::Rail) && chargeStage1Triggered_;
	const float a = leaning ? DegToRad(moveStateLeanDeg_) : 0.0f;
	const float c = std::cos(a), s = std::sin(a);
	const Vector3 axes[3] = {
		right,
		{ up.x * c + fwd.x * s, up.y * c + fwd.y * s, up.z * c + fwd.z * s },
		{ fwd.x * c - up.x * s, fwd.y * c - up.y * s, fwd.z * c - up.z * s } };
	Gameplay::Of(player_).SetColliderPose(center, axes);
}

void StagePlayScene::ApplyRailAimRotation(const Vector3& toAim) {
	// 上の基準を世界の上でなくカメラの上にする。雲の中の降下（ほぼ真下を向く）でも、照準が少し動いただけで
	// ヨーが大きく回ることがない。水平飛行ではカメラの上≒世界の上なので今までと同じ向きになる
	const float len = std::sqrt(toAim.x * toAim.x + toAim.y * toAim.y + toAim.z * toAim.z);
	if (len < kBasisEpsilon || !camera_) return;
	const Vector3 f{ toAim.x / len, toAim.y / len, toAim.z / len };
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 refUp{ camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };
	Vector3 r{ refUp.y * f.z - refUp.z * f.y, refUp.z * f.x - refUp.x * f.z, refUp.x * f.y - refUp.y * f.x };
	float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
	if (rl < kTinyEpsilon) {
		r = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
		rl = 1.0f;
	}
	r = { r.x / rl, r.y / rl, r.z / rl };
	const Vector3 u{ f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x };

	// 行（右・上・前）＝ Rx·Ry·Rz から角度を取り出す（Object3D の回転順。MakeRotateMatrix とは逆順なので自前で解く）
	const float sinYaw = (std::max)(-1.0f, (std::min)(1.0f, -r.z));
	const float yaw = std::asin(sinYaw);
	float pitch = 0.0f, roll = 0.0f;
	if (std::cos(yaw) > kTinyEpsilon) {
		pitch = std::atan2(u.z, f.z);
		roll  = std::atan2(r.y, r.x);
	} else {
		// ヨー ±90°（真横を向く）はピッチとロールが重なるので、ロールを 0 に固定して解く
		pitch = (sinYaw > 0.0f) ? std::atan2(u.x, u.y) : std::atan2(-u.x, u.y);
	}
	player_->SetRotate({ pitch, yaw, roll });
}

int StagePlayScene::ScaledPlayerAttack() const {
	if (!player_ || !Gameplay::Of(player_).HasAttackPower()) return 0;
	const MainWeaponDef* mw = GetGameSession()->GetLoadoutState()->GetMainWeapon();
	const int atk = Gameplay::Of(player_).GetAttackPower() + (mw ? mw->attackBonus : 0);
	// 飛行ユニットの倍率と、Y+B の強化の倍率（射撃・近接・支援とも）
	const float scale = (flightUnit_ ? flightUnit_->GetAttackScale() : 1.0f) * meleeCombo_.GetBuffAttackScale();
	return (atk > 0) ? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(atk) * scale))) : 0;
}

void StagePlayScene::RebuildMainWeaponModel() {
	if (!player_) return;
	const WeaponParams& wp = Gameplay::Of(player_).GetWeaponParams();
	const MainWeaponDef* mw = GetGameSession()->GetLoadoutState()->GetMainWeapon();
	const bool fromWeapon = mw && !mw->modelFile.empty();
	const std::string& dir = fromWeapon ? mw->modelDir : wp.modelDir;
	const std::string& file = fromWeapon ? mw->modelFile : wp.modelFile;
	if (!weapon_) {
		weapon_ = std::make_unique<Object3DInstance>();
		weapon_->Initialize(GetObject3DManager(), GetDxCore(), dir, file, "Weapon");
	} else {
		// 作り直すと前のフレームの定数バッファを GPU が使っている間に消してしまうので、モデルだけ差し替える
		ModelManager::GetInstance()->LoadModel(dir, file);
		weapon_->SetModel(file);
	}
	weapon_->SetCamera(camera_.get());
	if (fromWeapon && mw->blade && !weaponBlade_) {
		weaponBlade_ = std::make_unique<BeamBladeRenderer>();
		weaponBlade_->Initialize(GetDxCore());
	}
	// 付け替えたら前の武器の刃は出さない
	if (weaponBlade_) weaponBlade_->SnapExtend(0.0f);
}

bool StagePlayScene::IsWeaponBladeVisible() const {
	if (!weaponBlade_ || !weaponBlade_->IsVisible() || !weapon_ || !player_ || !player_->GetVisible()) return false;
	if (!Gameplay::Of(player_).GetWeaponParams().enabled) return false;
	const MainWeaponDef* mw = GetGameSession()->GetLoadoutState()->GetMainWeapon();
	return mw && mw->blade && !mw->modelFile.empty();
}

void StagePlayScene::RebuildShieldModel() {
	if (!player_) return;
	const ShieldDef* sd = GetGameSession()->GetLoadoutState()->GetShield();
	if (!sd || sd->modelFile.empty()) return; // モデルの無い盾は描かない（shield_ は残して使い回す）
	if (!shield_) {
		shield_ = std::make_unique<Object3DInstance>();
		shield_->Initialize(GetObject3DManager(), GetDxCore(), sd->modelDir, sd->modelFile, "Shield");
	} else {
		// 武器と同じく、作り直さずモデルだけ差し替える
		ModelManager::GetInstance()->LoadModel(sd->modelDir, sd->modelFile);
		shield_->SetModel(sd->modelFile);
	}
	shield_->SetCamera(camera_.get());
	if (sd->beam && !beamShield_) {
		beamShield_ = std::make_unique<BeamShieldRenderer>();
		beamShield_->Initialize(GetDxCore());
	}
}

bool StagePlayScene::IsShieldBeamVisible() const {
	if (!beamShield_ || !shield_ || !player_ || !player_->GetVisible()) return false;
	const ShieldDef* sd = GetGameSession()->GetLoadoutState()->GetShield();
	return sd && sd->beam && !sd->modelFile.empty();
}

float StagePlayScene::ShieldMoveSpeedScale() const {
	const ShieldDef* sd = GetGameSession()->GetLoadoutState()->GetShield();
	return sd ? sd->moveSpeedScale : 1.0f;
}

void StagePlayScene::CollectOnScreenEnemies(std::vector<IImGuiEditable*>& out) {
	std::vector<std::pair<IImGuiEditable*, bool>> targets;
	CollectScreenTargets(targets);
	for (const auto& [e, isBullet] : targets) {
		if (!isBullet) out.push_back(e);
	}
}

void StagePlayScene::FireSupportBullet(const Vector3& from, const Vector3& dir, IImGuiEditable* homingTarget, float damageScale) {
	if (!player_) return;
	// 自機の通常弾と同じプレハブ・同じ弾の値（攻撃力だけ倍率を掛ける）
	std::string bulletPrefab = Gameplay::Of(player_).FindBulletPrefab("normal");
	if (bulletPrefab.empty()) bulletPrefab = "TemporaryPlayerBullet";
	// bullet の指定が無ければ PrefabDef の既定値
	const PrefabDef* bdef = PrefabManager::GetInstance()->Find(bulletPrefab);
	const PrefabDef& bulletDef = (bdef && bdef->hasBullet) ? *bdef : PrefabDef::Defaults();
	const float speed          = bulletDef.bulletSpeed;
	const float lifetime       = bulletDef.bulletLifetime;
	const float colliderGrowth = bulletDef.bulletColliderGrowth;
	const float homing         = homingTarget ? bulletDef.bulletStrongHomingStrength : 0.0f;
	const int atk = ScaledPlayerAttack();
	const int scaledAtk = atk > 0 ? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(atk) * damageScale))) : 0;
	// 狙う敵がいれば狙いの面で消さない（ファンネルは面より奥の敵も撃つ）
	SpawnPlayerBullet(from, dir, speed, lifetime, colliderGrowth, homingTarget, homing,
		homingTarget ? 0.0f : aimPlaneDistance_, bulletPrefab, scaledAtk);
}

uint32_t StagePlayScene::FireSupportBeam(const Vector3& from, const Vector3& dir, float damageScale) {
	if (!player_ || !playerBeam_) return PlayerBeamPart::kInvalidBeam;
	const int atk = ScaledPlayerAttack();
	const int scaledAtk = atk > 0 ? (std::max)(1, static_cast<int>(std::lround(static_cast<float>(atk) * damageScale))) : 0;
	return playerBeam_->Fire(PlayerBeamPart::Kind::FunnelLarge, from, dir, scaledAtk);
}

void StagePlayScene::SetSupportBeamPose(uint32_t id, const Vector3& from, const Vector3& dir) {
	if (playerBeam_) playerBeam_->SetPose(id, from, dir);
}

bool StagePlayScene::IsSupportBeamActive(uint32_t id) const {
	return playerBeam_ && playerBeam_->IsActive(id);
}

void StagePlayScene::UpdatePlayerBeams(float dt) {
	if (!playerBeam_) return;
	// チャージビームは自機からレティクルへ付いていく
	if (chargeBeamId_ != PlayerBeamPart::kInvalidBeam) {
		if (!player_ || !playerBeam_->IsActive(chargeBeamId_)) {
			chargeBeamId_ = PlayerBeamPart::kInvalidBeam;
		} else {
			const Vector3 origin = PlayerShotOrigin();
			Vector3 dir{ firingTarget_.x - origin.x, firingTarget_.y - origin.y, firingTarget_.z - origin.z };
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len > kSquaredEpsilon) {
				dir = { dir.x / len, dir.y / len, dir.z / len };
				playerBeam_->SetPose(chargeBeamId_, origin, dir);
			}
		}
	}
	playerBeam_->Update(dt);
}

Vector3 StagePlayScene::PlayerShotOrigin() const {
	if (hasWeaponMuzzle_) return weaponMuzzle_;
	return player_ ? player_->GetTranslate() : Vector3{ 0.0f, 0.0f, 0.0f };
}

void StagePlayScene::StopAllPlayerBeams() {
	if (playerBeam_) playerBeam_->StopAll();
	chargeBeamId_ = PlayerBeamPart::kInvalidBeam;
}

int StagePlayScene::AbsorbEnemyBulletsInSphere(const Vector3& center, float radius) {
	int total = 0;
	const float r2 = radius * radius;
	std::vector<PrimitiveInstance*> absorbed;
	ForEachEnemyBullet([&](PrimitiveInstance* bullet) {
		const Vector3* bp = bullet->GetEditableTranslate();
		if (!bp) return;
		const float dx = bp->x - center.x, dy = bp->y - center.y, dz = bp->z - center.z;
		if (dx * dx + dy * dy + dz * dz > r2) return;
		int dmg = Gameplay::Of(bullet).GetDamageDealer().damage;
		if (dmg <= 0) dmg = kDefaultContactDamage;
		total += dmg;
		absorbed.push_back(bullet);
	});
	for (PrimitiveInstance* bullet : absorbed) KillBullet(bullet);
	return total;
}

Vector3 StagePlayScene::SpecialPlayerCenter() const {
	Vector3 base = player_ ? player_->GetTranslate() : Vector3{ 0.0f, 0.0f, 0.0f };
	if (!player_) return base;
	float off = specialPlayerCenterOffset_;
	if (off <= 0.0f) off = Gameplay::Of(player_).GetCollider().offset.y; // プレハブの足元→カプセル中心
	if (off == 0.0f) return base;
	// カメラ平面ベースの演出（楕円射影/放射）と整合するよう up はカメラ up を使う
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camUp = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };
	return { base.x + camUp.x * off, base.y + camUp.y * off, base.z + camUp.z * off };
}

void StagePlayScene::GetSpecialBodyEllipse(float& horizontal, float& vertical) const {
	horizontal = specialBodyEllipseH_;
	vertical   = specialBodyEllipseV_;
	if ((horizontal <= 0.0f || vertical <= 0.0f) && player_) {
		const Collider& col = Gameplay::Of(player_).GetCollider();
		const float cr = col.capsuleRadius;
		const float ch = col.capsuleHeight;
		if (horizontal <= 0.0f) horizontal = (cr > kMinEllipseRadius ? cr : kFallbackEllipseHorizontal);
		if (vertical   <= 0.0f) vertical   = (ch > kMinEllipseRadius ? ch * 0.5f + cr : horizontal * kFallbackEllipseAspect);
	}
	if (horizontal < kMinEllipseRadius) horizontal = kFallbackEllipseHorizontal;
	if (vertical   < kMinEllipseRadius) vertical   = 1.0f;
}

Vector3 StagePlayScene::SpecialBoltStart(const Vector3& playerPos, const Vector3& dir) const {
	// 自機の体を囲む楕円（画面に投影したカプセル）の表面の点を返す（Storm Lock のチャージ電撃と同じ考え方）。
	// dir は正規化済みの「始点 → 対象」方向。
	float ellipseA = 0.0f, ellipseB = 0.0f;
	GetSpecialBodyEllipse(ellipseA, ellipseB);

	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
	const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };
	// dir をカメラ平面に投影した成分から角度を求める
	const float cx = dir.x * camRight.x + dir.y * camRight.y + dir.z * camRight.z;
	const float cy = dir.x * camUp.x    + dir.y * camUp.y    + dir.z * camUp.z;
	const float ct = std::cos(std::atan2(cy, cx));
	const float st = std::sin(std::atan2(cy, cx));
	const float denom = std::sqrt((ellipseB * ct) * (ellipseB * ct) + (ellipseA * st) * (ellipseA * st));
	const float startR = (denom > kBasisEpsilon) ? (ellipseA * ellipseB) / denom : ellipseA;
	return { playerPos.x + dir.x * startR, playerPos.y + dir.y * startR, playerPos.z + dir.z * startR };
}

void StagePlayScene::EndSpecialMove() {
	specialActive_ = false;
	specialTimer_  = 0.0f;

	// クールタイム開始（両必殺技共通）。Shatterline は Recover(後隙)明けにここへ来る＝後隙明け開始。
	specialCooldownTimer_ = specialCooldown_;

	// 両方の必殺技の段取り・演出を戻す（描画物は遅延削除へ）＋無敵フラグを既定(true)へ戻す
	stormLock_.Reset();
	shatterline_.Reset();
	specialInvincible_ = true;

	// ワールド時間を通常に戻す
	SetTimeScale(TimeGroup::World, 1.0f);

	// プレイヤー位置を「画面内クリップ範囲」に収めるため、入力オフセットを 0 に戻す
	playerInputOffset_ = { 0.0f, 0.0f };
	playerVelocity_    = { 0.0f, 0.0f };
}

void StagePlayScene::ApplySpecialCamera(const Vector3& playerWorldPos) {
	(void)playerWorldPos;
	if (!camera_ || !specialActive_) return;

	// Shatterline は専用のシネマティック演出（フェーズ別の目標姿勢を補間）に分岐。
	if (equippedSpecial_ == SpecialKind::Shatterline) {
		shatterline_.ApplyCamera(GetDxCore() ? GetDxCore()->GetDeltaTime() : 0.0f);
		return;
	}

	// Storm Lock：今のカメラから後ろ＋上へ引く（全フェーズ）
	stormLock_.ApplyCamera();
}

void StagePlayScene::RefreshPlayerHpInvincibility() {
	if (!player_) return;

	// 無敵の発生源を全て集約。どれか1つでも有効なら HP.enabled=false にして
	// CollisionManager::Update の自動ダメージ経路も止める。
	const bool justInv    = justDodgeActive_;
	const bool dodgeInv   = dodgeActive_ && dodgeTimer_ <= dodgeIFrameDuration_;
	const bool dmgInv     = playerInvincibilityTimer_ > 0.0f;
	const bool specialInv = specialActive_ && specialInvincible_;  // 後隙(Recover)では false ＝被弾可能
	const bool invincible = justInv || dodgeInv || dmgInv || specialInv;

	Gameplay::Of(player_).GetHP().enabled = !invincible;
}
