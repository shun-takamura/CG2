#include "StagePlayScene.h"

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
#include "Spline/RailCameraController.h"
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
#include "LogBuffer.h"
#include "Components/SphereCollider.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "UI/Reticle.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyCommandFactory.h"
#include "Enemy/EnemyContext.h"
#include "Score/ScoreManager.h"
#include "TextRenderer.h"
#include "FontAtlas.h"
#include "Effect/EffectManager.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"

#ifdef _DEBUG
#include "ViewportWindow.h"
#include "MouseInput.h"
#else
#include "MouseInput.h"
#endif
#include "MathUtility.h"
#include "WindowsApplication.h"
#include <algorithm>
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#include "ImGuiManager.h"
#include "IImGuiEditable.h"
#endif

namespace {
	constexpr const char* kStagePlayTuningPath = "Resources/Json/Tuning/StagePlay.json";
}

#ifdef _DEBUG
#include "KeyboardInput.h"
#include "ImGuiManager.h"
#include "Effect/EffectEditorWindow.h"
#endif

StagePlayScene::StagePlayScene() = default;
StagePlayScene::~StagePlayScene() = default;

void StagePlayScene::LoadTuningFromJson() {
	if (!std::filesystem::exists(kStagePlayTuningPath)) return;
	auto result = JsonParser::ParseFile(kStagePlayTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;

	const JsonValue& off = root["player"]["localOffset"];
	if (off.IsArray() && off.Size() >= 3) {
		playerLocalOffset_ = {
			static_cast<float>(off[0].AsDouble(playerLocalOffset_.x)),
			static_cast<float>(off[1].AsDouble(playerLocalOffset_.y)),
			static_cast<float>(off[2].AsDouble(playerLocalOffset_.z)),
		};
	}
	railCameraSpeed_ = static_cast<float>(
		root["camera"]["speed"].AsDouble(railCameraSpeed_));
	playerSmoothTime_ = static_cast<float>(
		root["player"]["smoothTime"].AsDouble(playerSmoothTime_));

	const JsonValue& spd = root["player"]["moveSpeed"];
	if (spd.IsArray() && spd.Size() >= 2) {
		playerMoveSpeed_.x = static_cast<float>(spd[0].AsDouble(playerMoveSpeed_.x));
		playerMoveSpeed_.y = static_cast<float>(spd[1].AsDouble(playerMoveSpeed_.y));
	}
	const JsonValue& mar = root["player"]["clipMargin"];
	if (mar.IsArray() && mar.Size() >= 2) {
		playerClipMargin_.x = static_cast<float>(mar[0].AsDouble(playerClipMargin_.x));
		playerClipMargin_.y = static_cast<float>(mar[1].AsDouble(playerClipMargin_.y));
	}


	// ----- aim -----
	const JsonValue& aim = root["aim"];
	if (aim.IsObject()) {
		aimPlaneDistance_     = static_cast<float>(aim["planeDistance"].AsDouble(aimPlaneDistance_));
		aimSmoothTime_        = static_cast<float>(aim["smoothTime"].AsDouble(aimSmoothTime_));
		aimAssistPixelScale_  = static_cast<float>(aim["assistPixelScale"].AsDouble(aimAssistPixelScale_));
		reticleOuterMinPx_    = static_cast<float>(aim["reticleOuterMinPx"].AsDouble(reticleOuterMinPx_));
		reticleOuterMaxPx_    = static_cast<float>(aim["reticleOuterMaxPx"].AsDouble(reticleOuterMaxPx_));
		reticleOuterSizeMinPx_ = static_cast<float>(aim["reticleOuterSizeMinPx"].AsDouble(reticleOuterSizeMinPx_));
		reticleOuterSizeMaxPx_ = static_cast<float>(aim["reticleOuterSizeMaxPx"].AsDouble(reticleOuterSizeMaxPx_));
	}

	// ----- damage / invincibility -----
	const JsonValue& dmg = root["damage"];
	if (dmg.IsObject()) {
		playerInvincibilityDuration_  = static_cast<float>(dmg["invincibilityDuration"].AsDouble(playerInvincibilityDuration_));
		shootLockoutDuration_         = static_cast<float>(dmg["shootLockoutDuration"].AsDouble(shootLockoutDuration_));
		damageBlinkFrequency_         = static_cast<float>(dmg["blinkFrequency"].AsDouble(damageBlinkFrequency_));
		damageBlinkAlpha_             = static_cast<float>(dmg["blinkAlpha"].AsDouble(damageBlinkAlpha_));
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
		{
			const JsonValue& em = jd["dodgeExpandedMargin"];
			if (em.IsArray() && em.Size() >= 2) {
				jdDodgeExpandedMargin_.x = static_cast<float>(em[0].AsDouble(jdDodgeExpandedMargin_.x));
				jdDodgeExpandedMargin_.y = static_cast<float>(em[1].AsDouble(jdDodgeExpandedMargin_.y));
			}
			const JsonValue& mc = jd["meleeCamOffset"];
			if (mc.IsArray() && mc.Size() >= 3) {
				jdMeleeCameraOffset_ = {
					static_cast<float>(mc[0].AsDouble(jdMeleeCameraOffset_.x)),
					static_cast<float>(mc[1].AsDouble(jdMeleeCameraOffset_.y)),
					static_cast<float>(mc[2].AsDouble(jdMeleeCameraOffset_.z)),
				};
			}
			const JsonValue& ml = jd["meleeCamLookOffset"];
			if (ml.IsArray() && ml.Size() >= 3) {
				jdMeleeCameraLookOffset_ = {
					static_cast<float>(ml[0].AsDouble(jdMeleeCameraLookOffset_.x)),
					static_cast<float>(ml[1].AsDouble(jdMeleeCameraLookOffset_.y)),
					static_cast<float>(ml[2].AsDouble(jdMeleeCameraLookOffset_.z)),
				};
			}
		}
		const JsonValue& cpArr = jd["clonePaths"];
		if (cpArr.IsArray()) {
			for (size_t i = 0; i < cpArr.Size() && i < 4; ++i) {
				jdClonePath_[i] = cpArr[i].AsString(jdClonePath_[i]);
			}
		}
		const JsonValue& cc = jd["cloneColor"];
		if (cc.IsArray() && cc.Size() >= 4) {
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
		specialGaugeMax_       = static_cast<float>(sg["max"].AsDouble(specialGaugeMax_));
		dodgeSpecialGaugeGain_ = static_cast<float>(sg["dodgeGain"].AsDouble(dodgeSpecialGaugeGain_));
		specialDuration_       = static_cast<float>(sg["duration"].AsDouble(specialDuration_));
		// Phase 1
		specialBarrierRadius_   = static_cast<float>(sg["barrierRadius"].AsDouble(specialBarrierRadius_));
		specialBarrierDuration_ = static_cast<float>(sg["barrierDuration"].AsDouble(specialBarrierDuration_));
		specialBarrierFillOn_   = sg["barrierFill"].AsBool(specialBarrierFillOn_);
		specialPlayerCenterOffset_ = static_cast<float>(sg["centerOffset"].AsDouble(specialPlayerCenterOffset_));
		// バリアのワイヤーフレーム球
		const JsonValue& wf = sg["barrierWire"];
		if (wf.IsObject()) {
			specialBarrierWireframeOn_   = wf["on"].AsBool(specialBarrierWireframeOn_);
			const JsonValue& ss = wf["spinSpeed"];
			if (ss.IsArray() && ss.Size() >= 3) {
				specialBarrierWireSpinSpeed_.x = static_cast<float>(ss[0].AsDouble(specialBarrierWireSpinSpeed_.x));
				specialBarrierWireSpinSpeed_.y = static_cast<float>(ss[1].AsDouble(specialBarrierWireSpinSpeed_.y));
				specialBarrierWireSpinSpeed_.z = static_cast<float>(ss[2].AsDouble(specialBarrierWireSpinSpeed_.z));
			} else if (ss.IsNumber()) {
				// 旧フォーマット（単一値=Y軸のみ）との互換
				specialBarrierWireSpinSpeed_.y = static_cast<float>(ss.AsDouble(specialBarrierWireSpinSpeed_.y));
			}
			specialBarrierWireMeridians_ = static_cast<int>(wf["meridians"].AsInt(static_cast<int64_t>(specialBarrierWireMeridians_)));
			specialBarrierWireParallels_ = static_cast<int>(wf["parallels"].AsInt(static_cast<int64_t>(specialBarrierWireParallels_)));
			specialBarrierWireSegments_  = static_cast<int>(wf["segments"].AsInt(static_cast<int64_t>(specialBarrierWireSegments_)));
			auto readV4w = [](const JsonValue& a, Vector4& v) {
				if (a.IsArray() && a.Size() >= 4) {
					v.x = static_cast<float>(a[0].AsDouble(v.x));
					v.y = static_cast<float>(a[1].AsDouble(v.y));
					v.z = static_cast<float>(a[2].AsDouble(v.z));
					v.w = static_cast<float>(a[3].AsDouble(v.w));
				}
			};
			readV4w(wf["gold"], specialBarrierWireColorGold_);
			readV4w(wf["pink"], specialBarrierWireColorPink_);
		}
		// バリアパーティクル
		const JsonValue& bp = sg["barrierParticle"];
		if (bp.IsObject()) {
			specialBarrierParticleOn_   = bp["on"].AsBool(specialBarrierParticleOn_);
			specialBarrierEmitInterval_ = static_cast<float>(bp["interval"].AsDouble(specialBarrierEmitInterval_));
			specialBarrierEmitCount_    = static_cast<int>(bp["count"].AsInt(static_cast<int64_t>(specialBarrierEmitCount_)));
			specialBarrierParticleLife_ = static_cast<float>(bp["life"].AsDouble(specialBarrierParticleLife_));
			specialBarrierParticleRadiusScale_ = static_cast<float>(bp["radiusScale"].AsDouble(specialBarrierParticleRadiusScale_));
			auto readV2 = [](const JsonValue& a, Vector2& v) {
				if (a.IsArray() && a.Size() >= 2) {
					v.x = static_cast<float>(a[0].AsDouble(v.x));
					v.y = static_cast<float>(a[1].AsDouble(v.y));
				}
			};
			auto readV4 = [](const JsonValue& a, Vector4& v) {
				if (a.IsArray() && a.Size() >= 4) {
					v.x = static_cast<float>(a[0].AsDouble(v.x));
					v.y = static_cast<float>(a[1].AsDouble(v.y));
					v.z = static_cast<float>(a[2].AsDouble(v.z));
					v.w = static_cast<float>(a[3].AsDouble(v.w));
				}
			};
			readV2(bp["scaleMin"], specialBarrierParticleScaleMin_);
			readV2(bp["scaleMax"], specialBarrierParticleScaleMax_);
			readV4(bp["color0"], specialBarrierParticleColor0_);
			readV4(bp["color1"], specialBarrierParticleColor1_);
		}
		// 光の翼（X字パーティクル）
		const JsonValue& wg = sg["wing"];
		if (wg.IsObject()) {
			specialWingOn_          = wg["on"].AsBool(specialWingOn_);
			specialWingArmCount_    = static_cast<int>(wg["armCount"].AsInt(static_cast<int64_t>(specialWingArmCount_)));
			specialWingAngleOffset_ = static_cast<float>(wg["angleOffset"].AsDouble(specialWingAngleOffset_));
			specialWingSpeed_       = static_cast<float>(wg["speed"].AsDouble(specialWingSpeed_));
			specialWingLife_        = static_cast<float>(wg["life"].AsDouble(specialWingLife_));
			specialWingBurstCount_  = static_cast<int>(wg["burstCount"].AsInt(static_cast<int64_t>(specialWingBurstCount_)));
			specialWingJitter_      = static_cast<float>(wg["jitter"].AsDouble(specialWingJitter_));
			specialWingEmitRadius_  = static_cast<float>(wg["emitRadius"].AsDouble(specialWingEmitRadius_));
			auto readV2w = [](const JsonValue& a, Vector2& v) {
				if (a.IsArray() && a.Size() >= 2) {
					v.x = static_cast<float>(a[0].AsDouble(v.x));
					v.y = static_cast<float>(a[1].AsDouble(v.y));
				}
			};
			auto readV4w2 = [](const JsonValue& a, Vector4& v) {
				if (a.IsArray() && a.Size() >= 4) {
					v.x = static_cast<float>(a[0].AsDouble(v.x));
					v.y = static_cast<float>(a[1].AsDouble(v.y));
					v.z = static_cast<float>(a[2].AsDouble(v.z));
					v.w = static_cast<float>(a[3].AsDouble(v.w));
				}
			};
			readV2w(wg["scaleMin"], specialWingScaleMin_);
			readV2w(wg["scaleMax"], specialWingScaleMax_);
			readV4w2(wg["colorInner"], specialWingColorInner_);
			readV4w2(wg["colorOuter"], specialWingColorOuter_);
		}
		const JsonValue& bc = sg["barrierColor"];
		if (bc.IsArray() && bc.Size() >= 4) {
			specialBarrierColor_ = {
				static_cast<float>(bc[0].AsDouble(specialBarrierColor_.x)),
				static_cast<float>(bc[1].AsDouble(specialBarrierColor_.y)),
				static_cast<float>(bc[2].AsDouble(specialBarrierColor_.z)),
				static_cast<float>(bc[3].AsDouble(specialBarrierColor_.w))
			};
		}
		specialCamPullback_ = static_cast<float>(sg["camPullback"].AsDouble(specialCamPullback_));
		specialCamFovAdd_   = static_cast<float>(sg["camFovAdd"].AsDouble(specialCamFovAdd_));
		specialCamUpAdd_    = static_cast<float>(sg["camUpAdd"].AsDouble(specialCamUpAdd_));
		// Phase 2 チャージ
		specialChargeBoltCount_     = static_cast<int>(sg["chargeBoltCount"].AsInt(static_cast<int64_t>(specialChargeBoltCount_)));
		specialChargeRegenInterval_ = static_cast<float>(sg["chargeRegenInterval"].AsDouble(specialChargeRegenInterval_));
		specialChargeStartRadiusH_  = static_cast<float>(sg["chargeStartRadiusH"].AsDouble(specialChargeStartRadiusH_));
		specialChargeStartRadiusV_  = static_cast<float>(sg["chargeStartRadiusV"].AsDouble(specialChargeStartRadiusV_));
		specialChargeMinLength_     = static_cast<float>(sg["chargeMinLength"].AsDouble(specialChargeMinLength_));
		// Phase 3 Fire
		specialFireSimultaneous_   = static_cast<int>(sg["fireSimultaneous"].AsInt(static_cast<int64_t>(specialFireSimultaneous_)));
		specialFireMinHold_        = static_cast<float>(sg["fireMinHold"].AsDouble(specialFireMinHold_));
		specialFireMaxHold_        = static_cast<float>(sg["fireMaxHold"].AsDouble(specialFireMaxHold_));
		specialFireTickInterval_   = static_cast<float>(sg["fireTickInterval"].AsDouble(specialFireTickInterval_));
		specialFireTickDamage_     = static_cast<int>(sg["fireTickDamage"].AsInt(static_cast<int64_t>(specialFireTickDamage_)));
		specialFireLaunchInterval_ = static_cast<float>(sg["fireLaunchInterval"].AsDouble(specialFireLaunchInterval_));
		specialFireGrowTime_       = static_cast<float>(sg["fireGrowTime"].AsDouble(specialFireGrowTime_));
		specialFireMaxOffsetRatio_ = static_cast<float>(sg["fireMaxOffsetRatio"].AsDouble(specialFireMaxOffsetRatio_));
		specialFireBranchProb_     = static_cast<float>(sg["fireBranchProb"].AsDouble(specialFireBranchProb_));
		specialFireStartWidth_     = static_cast<float>(sg["fireStartWidth"].AsDouble(specialFireStartWidth_));
		specialFireEndWidth_       = static_cast<float>(sg["fireEndWidth"].AsDouble(specialFireEndWidth_));
		specialFireBoltLifetime_   = static_cast<float>(sg["fireBoltLifetime"].AsDouble(specialFireBoltLifetime_));
		specialEndDuration_        = static_cast<float>(sg["endDuration"].AsDouble(specialEndDuration_));
		const JsonValue& fc = sg["fireColor"];
		if (fc.IsArray() && fc.Size() >= 4) {
			specialFireColor_ = {
				static_cast<float>(fc[0].AsDouble(specialFireColor_.x)),
				static_cast<float>(fc[1].AsDouble(specialFireColor_.y)),
				static_cast<float>(fc[2].AsDouble(specialFireColor_.z)),
				static_cast<float>(fc[3].AsDouble(specialFireColor_.w))
			};
		}
		// ゲージ UI
		const JsonValue& bar = sg["bar"];
		if (bar.IsObject()) {
			gaugeBarMaxWidth_ = static_cast<float>(bar["maxWidth"].AsDouble(gaugeBarMaxWidth_));
			gaugeBarHeight_   = static_cast<float>(bar["height"].AsDouble(gaugeBarHeight_));
			gaugeBarPosX_     = static_cast<float>(bar["posX"].AsDouble(gaugeBarPosX_));
			gaugeBarPosY_     = static_cast<float>(bar["posY"].AsDouble(gaugeBarPosY_));
			const JsonValue& bgC = bar["bgColor"];
			if (bgC.IsArray() && bgC.Size() >= 4) {
				gaugeBarBgColor_ = {
					static_cast<float>(bgC[0].AsDouble(gaugeBarBgColor_.x)),
					static_cast<float>(bgC[1].AsDouble(gaugeBarBgColor_.y)),
					static_cast<float>(bgC[2].AsDouble(gaugeBarBgColor_.z)),
					static_cast<float>(bgC[3].AsDouble(gaugeBarBgColor_.w))
				};
			}
			const JsonValue& fgC = bar["fgColor"];
			if (fgC.IsArray() && fgC.Size() >= 4) {
				gaugeBarFgColor_ = {
					static_cast<float>(fgC[0].AsDouble(gaugeBarFgColor_.x)),
					static_cast<float>(fgC[1].AsDouble(gaugeBarFgColor_.y)),
					static_cast<float>(fgC[2].AsDouble(gaugeBarFgColor_.z)),
					static_cast<float>(fgC[3].AsDouble(gaugeBarFgColor_.w))
				};
			}
			const JsonValue& fuC = bar["fullColor"];
			if (fuC.IsArray() && fuC.Size() >= 4) {
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
		if (arr.IsArray() && arr.Size() >= 4) {
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
		if (co.IsArray() && co.Size() >= 3) {
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
	root["player"] = std::move(playerObj);

	JsonValue camObj = JsonValue::MakeObject();
	camObj["speed"] = static_cast<double>(railCameraSpeed_);
	root["camera"] = std::move(camObj);

	JsonValue aimObj = JsonValue::MakeObject();
	aimObj["planeDistance"]      = static_cast<double>(aimPlaneDistance_);
	aimObj["smoothTime"]         = static_cast<double>(aimSmoothTime_);
	aimObj["assistPixelScale"]   = static_cast<double>(aimAssistPixelScale_);
	aimObj["reticleOuterMinPx"]  = static_cast<double>(reticle_ ? reticle_->GetLockOnMinPxOutside() : reticleOuterMinPx_);
	aimObj["reticleOuterMaxPx"]  = static_cast<double>(reticle_ ? reticle_->GetLockOnMaxPxOutside() : reticleOuterMaxPx_);
	aimObj["reticleOuterSizeMinPx"] = static_cast<double>(reticle_ ? reticle_->GetOuterSizeMin() : reticleOuterSizeMinPx_);
	aimObj["reticleOuterSizeMaxPx"] = static_cast<double>(reticle_ ? reticle_->GetOuterSizeMax() : reticleOuterSizeMaxPx_);
	root["aim"] = std::move(aimObj);

	JsonValue dmgObj = JsonValue::MakeObject();
	dmgObj["invincibilityDuration"] = static_cast<double>(playerInvincibilityDuration_);
	dmgObj["shootLockoutDuration"]  = static_cast<double>(shootLockoutDuration_);
	dmgObj["blinkFrequency"]        = static_cast<double>(damageBlinkFrequency_);
	dmgObj["blinkAlpha"]            = static_cast<double>(damageBlinkAlpha_);
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
		for (int i = 0; i < 4; ++i) cpArr.Push(JsonValue(jdClonePath_[i]));
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
	sgObj["max"]       = static_cast<double>(specialGaugeMax_);
	sgObj["dodgeGain"] = static_cast<double>(dodgeSpecialGaugeGain_);
	sgObj["duration"]  = static_cast<double>(specialDuration_);
	sgObj["barrierRadius"]   = static_cast<double>(specialBarrierRadius_);
	sgObj["barrierDuration"] = static_cast<double>(specialBarrierDuration_);
	sgObj["barrierFill"]     = JsonValue(specialBarrierFillOn_);
	sgObj["centerOffset"]    = static_cast<double>(specialPlayerCenterOffset_);
	{
		JsonValue wfObj = JsonValue::MakeObject();
		wfObj["on"]        = JsonValue(specialBarrierWireframeOn_);
		{
			JsonValue ss = JsonValue::MakeArray();
			ss.Push(JsonValue(static_cast<double>(specialBarrierWireSpinSpeed_.x)));
			ss.Push(JsonValue(static_cast<double>(specialBarrierWireSpinSpeed_.y)));
			ss.Push(JsonValue(static_cast<double>(specialBarrierWireSpinSpeed_.z)));
			wfObj["spinSpeed"] = std::move(ss);
		}
		wfObj["meridians"] = static_cast<int64_t>(specialBarrierWireMeridians_);
		wfObj["parallels"] = static_cast<int64_t>(specialBarrierWireParallels_);
		wfObj["segments"]  = static_cast<int64_t>(specialBarrierWireSegments_);
		auto pushV4w = [](const Vector4& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			arr.Push(JsonValue(static_cast<double>(v.z)));
			arr.Push(JsonValue(static_cast<double>(v.w)));
			return arr;
		};
		wfObj["gold"] = pushV4w(specialBarrierWireColorGold_);
		wfObj["pink"] = pushV4w(specialBarrierWireColorPink_);
		sgObj["barrierWire"] = std::move(wfObj);
	}
	{
		JsonValue bpObj = JsonValue::MakeObject();
		bpObj["on"]          = JsonValue(specialBarrierParticleOn_);
		bpObj["interval"]    = static_cast<double>(specialBarrierEmitInterval_);
		bpObj["count"]       = static_cast<int64_t>(specialBarrierEmitCount_);
		bpObj["life"]        = static_cast<double>(specialBarrierParticleLife_);
		bpObj["radiusScale"] = static_cast<double>(specialBarrierParticleRadiusScale_);
		auto pushV2 = [](const Vector2& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			return arr;
		};
		auto pushV4b = [](const Vector4& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			arr.Push(JsonValue(static_cast<double>(v.z)));
			arr.Push(JsonValue(static_cast<double>(v.w)));
			return arr;
		};
		bpObj["scaleMin"] = pushV2(specialBarrierParticleScaleMin_);
		bpObj["scaleMax"] = pushV2(specialBarrierParticleScaleMax_);
		bpObj["color0"]   = pushV4b(specialBarrierParticleColor0_);
		bpObj["color1"]   = pushV4b(specialBarrierParticleColor1_);
		sgObj["barrierParticle"] = std::move(bpObj);
	}
	{
		JsonValue wgObj = JsonValue::MakeObject();
		wgObj["on"]          = JsonValue(specialWingOn_);
		wgObj["armCount"]    = static_cast<int64_t>(specialWingArmCount_);
		wgObj["angleOffset"] = static_cast<double>(specialWingAngleOffset_);
		wgObj["speed"]       = static_cast<double>(specialWingSpeed_);
		wgObj["life"]        = static_cast<double>(specialWingLife_);
		wgObj["burstCount"]  = static_cast<int64_t>(specialWingBurstCount_);
		wgObj["jitter"]      = static_cast<double>(specialWingJitter_);
		wgObj["emitRadius"]  = static_cast<double>(specialWingEmitRadius_);
		auto pushV2w = [](const Vector2& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			return arr;
		};
		auto pushV4w2 = [](const Vector4& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			arr.Push(JsonValue(static_cast<double>(v.z)));
			arr.Push(JsonValue(static_cast<double>(v.w)));
			return arr;
		};
		wgObj["scaleMin"]   = pushV2w(specialWingScaleMin_);
		wgObj["scaleMax"]   = pushV2w(specialWingScaleMax_);
		wgObj["colorInner"] = pushV4w2(specialWingColorInner_);
		wgObj["colorOuter"] = pushV4w2(specialWingColorOuter_);
		sgObj["wing"] = std::move(wgObj);
	}
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(specialBarrierColor_.x)));
		arr.Push(JsonValue(static_cast<double>(specialBarrierColor_.y)));
		arr.Push(JsonValue(static_cast<double>(specialBarrierColor_.z)));
		arr.Push(JsonValue(static_cast<double>(specialBarrierColor_.w)));
		sgObj["barrierColor"] = std::move(arr);
	}
	sgObj["camPullback"] = static_cast<double>(specialCamPullback_);
	sgObj["camFovAdd"]   = static_cast<double>(specialCamFovAdd_);
	sgObj["camUpAdd"]    = static_cast<double>(specialCamUpAdd_);
	// Phase 2 チャージ
	sgObj["chargeBoltCount"]     = static_cast<int64_t>(specialChargeBoltCount_);
	sgObj["chargeRegenInterval"] = static_cast<double>(specialChargeRegenInterval_);
	sgObj["chargeStartRadiusH"]  = static_cast<double>(specialChargeStartRadiusH_);
	sgObj["chargeStartRadiusV"]  = static_cast<double>(specialChargeStartRadiusV_);
	sgObj["chargeMinLength"]     = static_cast<double>(specialChargeMinLength_);
	// Phase 3 Fire
	sgObj["fireSimultaneous"]   = static_cast<int64_t>(specialFireSimultaneous_);
	sgObj["fireMinHold"]        = static_cast<double>(specialFireMinHold_);
	sgObj["fireMaxHold"]        = static_cast<double>(specialFireMaxHold_);
	sgObj["fireTickInterval"]   = static_cast<double>(specialFireTickInterval_);
	sgObj["fireTickDamage"]     = static_cast<int64_t>(specialFireTickDamage_);
	sgObj["fireLaunchInterval"] = static_cast<double>(specialFireLaunchInterval_);
	sgObj["fireGrowTime"]       = static_cast<double>(specialFireGrowTime_);
	sgObj["fireMaxOffsetRatio"] = static_cast<double>(specialFireMaxOffsetRatio_);
	sgObj["fireBranchProb"]     = static_cast<double>(specialFireBranchProb_);
	sgObj["fireStartWidth"]     = static_cast<double>(specialFireStartWidth_);
	sgObj["fireEndWidth"]       = static_cast<double>(specialFireEndWidth_);
	sgObj["fireBoltLifetime"]   = static_cast<double>(specialFireBoltLifetime_);
	sgObj["endDuration"]        = static_cast<double>(specialEndDuration_);
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(specialFireColor_.x)));
		arr.Push(JsonValue(static_cast<double>(specialFireColor_.y)));
		arr.Push(JsonValue(static_cast<double>(specialFireColor_.z)));
		arr.Push(JsonValue(static_cast<double>(specialFireColor_.w)));
		sgObj["fireColor"] = std::move(arr);
	}
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

	std::filesystem::path p(kStagePlayTuningPath);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
	}
	JsonWriter::WriteFile(kStagePlayTuningPath, root, { true, 2 });
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
	// World だけスロー（Player/UI は等速）。終了は UpdateJustDodgeEffect が元に戻す。
	SetTimeScale(TimeGroup::World, justDodgeSlowWorld_);
	// （ストック制廃止：左派生＝小回復・無制限、デフォルトHeal＝大回復・固定回数制に再設計）
	// スコア加点
	ScoreManager::GetInstance()->AddScore(justDodgeScore_);

	// 分身カウンター：4方向に分身プレビューを出し、方向入力での確定待ちに入る。
	// （上=近接強 / 右=近接弱 / 下=追加回避 / 左=回復。各アクションは Phase2 で実装）
	jdCounterTarget_        = attacker;
	jdChosen_               = CounterDir::None;
	justDodgeCounterActive_ = false; // 派生が確定するまでは false（無入力なら受付期限で自然終了）
	jdSelecting_            = true;
	jdMerging_              = false;
	SpawnJustDodgeClones();
}

void StagePlayScene::ApplyJustDodgeCamera(const Vector3& playerWorldPos)
{
	(void)playerWorldPos; // Phase3 の端寄せ構図補正で使用予定
	if (!camera_) return;
	const float b = jdEffectIntensity_;
	if (b <= 0.001f) return;

	// 精密カメラ適用後の現在 eye を基準に、forward 逆方向へ引く（後退＝視野が広がる）。
	// clip 判定は引き前の通常カメラで済んでいるので、本体の可動範囲は変わらない（端固まり防止）。
	const Vector3 eye = camera_->GetTranslate();
	const Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
	const Vector3 forward = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };
	camera_->SetTranslate({
		eye.x - forward.x * jdCamPullback_ * b,
		eye.y - forward.y * jdCamPullback_ * b,
		eye.z - forward.z * jdCamPullback_ * b,
	});
	camera_->SetFovY(camera_->GetFovY() + jdCamFovAdd_ * b);
	camera_->Update();
}

void StagePlayScene::ApplyJustDodgeMeleeCamera(const Vector3& playerWorldPos)
{
	if (!camera_ || !jdMeleeCameraActive_) return;
	// プレイヤー→対象敵を forward に取って、その基底（right/up/forward）で
	// jdMeleeCameraOffset_ ぶんプレイヤーから離れた位置にカメラを置く。
	// 注視点は (player+target)/2 + jdMeleeCameraLookOffset_。
	Vector3 target = playerWorldPos;
	if (jdCounterTarget_) {
		if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) target = *tp;
	}

	Vector3 fwd{ target.x - playerWorldPos.x, target.y - playerWorldPos.y, target.z - playerWorldPos.z };
	float flen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
	if (flen < 1e-4f) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.y /= flen; fwd.z /= flen;

	const Vector3 worldUp{ 0.0f, 1.0f, 0.0f };
	Vector3 rgt{
		worldUp.y * fwd.z - worldUp.z * fwd.y,
		worldUp.z * fwd.x - worldUp.x * fwd.z,
		worldUp.x * fwd.y - worldUp.y * fwd.x,
	};
	float rlen = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
	if (rlen < 1e-4f) { rgt = { 1.0f, 0.0f, 0.0f }; rlen = 1.0f; }
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
	if (dlen < 1e-4f) return;
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
	jdClones_.assign(4, nullptr); // index: 0=Up,1=Right,2=Down,3=Left
	for (int i = 0; i < 4; ++i) {
		// パスを「dir/file」に分割（AddDynamicObject の引数仕様）。
		// ローダ側で dir + "/" + filename を行うため、dir 末尾の '/' は付けない（二重スラッシュ防止）。
		const std::string& full = jdClonePath_[i];
		const auto slash = full.find_last_of('/');
		const std::string dir  = (slash == std::string::npos) ? std::string()  : full.substr(0, slash);
		const std::string file = (slash == std::string::npos) ? full           : full.substr(slash + 1);

		const size_t prev = object3DInstances_.size();
		AddDynamicObject(dir, file, ppos);
		if (object3DInstances_.size() != prev + 1) continue; // 生成失敗時は視覚なしで継続
		Object3DInstance* clone = object3DInstances_.back().get();
		if (!clone) continue;
		clone->GetCollider().enabled = false; // 当たり判定なし
		clone->SetMaterialColor(jdCloneColor_); // 半透明色（alpha<1 で半透明）
		clone->Update(); // 初フレームの CB を確定（原点描画バグ回避）
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
			// 近接（強=Up / 弱1段目=Right）。
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
					if (dl > 1e-3f) {
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

			meleeComboIndex_ = (dir == CounterDir::Up) ? 0 : 1;
		} break;

		case CounterDir::Down: {
			// 追加回避：moveDelta(WASD/スティック)方向へダッシュ＋無敵窓開始＋許容枠拡張＋射撃禁止
			dodgeActive_          = true;
			dodgeTimer_           = 0.0f;
			dodgeCooldownTimer_   = dodgeCooldown_;
			dodgeActionLockTimer_ = dodgeActionLock_;
			const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
			if (mlen > 1e-3f) {
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
				player_->GetHP().Heal(healSmallAmount_);
			}
			// 即座に派生終了（演出はジャスト回避のフェードアウトに任せる）
			EndJustDodgeCounterAction();
		} break;

		default: break;
	}

	const char* name =
		dir == CounterDir::Up    ? "Up(近接強)" :
		dir == CounterDir::Right ? "Right(近接弱)" :
		dir == CounterDir::Down  ? "Down(追加回避)" : "Left(回復)";
	LogBuffer::Instance().Add(std::string("JustDodge derive: ") + name, LogBuffer::Level::Info);
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
}

void StagePlayScene::UpdateJustDodgeCounterAction(float dt)
{
	if (!justDodgeCounterActive_) return;
	jdActionPhaseTimer_ += dt;

	switch (jdChosen_) {
		case CounterDir::Up:
		case CounterDir::Right: {
			// Approach → Active（meleeパイプ＋melee発生待ち） → (弱は自動連撃で w1→w4) → Return → 終了
			// ワールド位置の補間は描画直前の SetTranslate 上書きで行う（ここではフェーズ管理だけ）。

			// 近接スロット名から発生予約をまとめる lambda。stage は弱の場合 1..4、強の場合は無視。
			auto spawnMeleeStage = [&](int stage) {
				std::string slot;
				if (jdChosen_ == CounterDir::Up) {
					slot = "melee_strong";
				} else {
					switch (stage) {
						case 1: slot = "melee_w1"; break;
						case 2: slot = "melee_w2"; break;
						case 3: slot = "melee_w3"; break;
						case 4: slot = "melee_w4"; break;
						default: slot = "melee_w1"; break;
					}
				}
				std::string prefab = player_ ? player_->FindBulletPrefab(slot) : std::string();
				if (prefab.empty()) prefab = "TemporaryPlayerMelee";
				float startup = 0.05f, active = 0.20f, recovery = 0.15f, comboWindow = 0.40f;
				if (const PrefabDef* mdef = PrefabManager::GetInstance()->Find(prefab); mdef && mdef->hasMelee) {
					startup     = mdef->meleeStartup;
					active      = mdef->meleeActiveDuration;
					recovery    = mdef->meleeRecovery;
					comboWindow = mdef->meleeComboWindow;
				}
				meleePendingPrefab_   = prefab;
				meleeStartupTimer_    = startup;
				meleePending_         = true;
				meleeActionLockTimer_ = startup + active + recovery;
				meleeComboTimer_      = startup + active + recovery + comboWindow;
				meleeComboIndex_      = (jdChosen_ == CounterDir::Up) ? 0 : stage;
				if (meleeStartupTimer_ <= 0.0f) {
					meleeStartupTimer_ = 0.0f;
					meleePending_      = false;
					SpawnPendingMelee();
				}
			};

			if (jdActionPhase_ == JdActionPhase::Approach) {
				if (jdActionPhaseTimer_ >= jdMeleeApproachDuration_) {
					jdAutoComboStage_   = 1;
					spawnMeleeStage(jdAutoComboStage_);
					jdActionPhase_      = JdActionPhase::Active;
					jdActionPhaseTimer_ = 0.0f;
				}
			} else if (jdActionPhase_ == JdActionPhase::Active) {
				if (meleeActionLockTimer_ <= 0.0f) {
					// 弱（Right）は kMeleeWeakComboMax_ 段まで自動連撃
					const bool isWeak = (jdChosen_ == CounterDir::Right);
					if (isWeak && jdAutoComboStage_ < kMeleeWeakComboMax_) {
						++jdAutoComboStage_;
						spawnMeleeStage(jdAutoComboStage_);
						jdActionPhaseTimer_ = 0.0f; // Active 継続
					} else {
						jdActionPhase_      = JdActionPhase::Return;
						jdActionPhaseTimer_ = 0.0f;
					}
				}
			} else if (jdActionPhase_ == JdActionPhase::Return) {
				if (jdActionPhaseTimer_ >= jdMeleeReturnDuration_) {
					playerInputOffset_ = jdReturnOffset_;
					playerVelocity_    = { 0.0f, 0.0f };
					meleeComboIndex_   = 0; // 連撃終了でコンボ状態リセット
					EndJustDodgeCounterAction();
				}
			}
		} break;

		case CounterDir::Down: {
			// 回避が落ちる（dodgeActive_=false）まで Active 継続。
			// ダッシュ＋追加無敵中は射撃禁止を上書きし続ける（既存shootLockoutTimer_を流用）。
			if (dodgeActive_) {
				shootLockoutTimer_ = (std::max)(shootLockoutTimer_, 0.05f);
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
	if (!jdSelecting_ && !jdMerging_) return;
	if (!player_ || !camera_) return;

	// spread: 0=中心, 1=各方向に最大展開。分裂は ease-out で広がり、集合は中心へ戻る。
	float spread;
	if (jdMerging_) {
		jdMergeTimer_ += dt;
		float mt = (jdMergeDuration_ > 1e-4f) ? (jdMergeTimer_ / jdMergeDuration_) : 1.0f;
		if (mt > 1.0f) mt = 1.0f;
		spread = 1.0f - mt * mt * mt; // 1→0（中心へ吸い込まれる）
	} else {
		jdSpreadTimer_ += dt;
		float st = (jdSpreadDuration_ > 1e-4f) ? (jdSpreadTimer_ / jdSpreadDuration_) : 1.0f;
		if (st > 1.0f) st = 1.0f;
		spread = 1.0f - (1.0f - st) * (1.0f - st) * (1.0f - st); // EaseOutCubic 0→1
	}

	// 分身位置 = 本体位置 ± カメラ右/上 × オフセット × spread（ダッシュ滑り中も本体に追従）
	const Vector3 ppos = player_->GetTranslate();
	const Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
	const Vector3 right = { rot.m[0][0], rot.m[0][1], rot.m[0][2] };
	const Vector3 up    = { rot.m[1][0], rot.m[1][1], rot.m[1][2] };
	const float o = jdCloneOffset_ * spread;
	const Vector3 dirOff[4] = {
		{  up.x * o,    up.y * o,    up.z * o    }, // Up
		{  right.x * o, right.y * o, right.z * o }, // Right
		{ -up.x * o,   -up.y * o,   -up.z * o    }, // Down
		{ -right.x * o,-right.y * o,-right.z * o }, // Left
	};
	for (int i = 0; i < static_cast<int>(jdClones_.size()) && i < 4; ++i) {
		if (!jdClones_[i]) continue;
		if (Vector3* t = jdClones_[i]->GetEditableTranslate()) {
			t->x = ppos.x + dirOff[i].x;
			t->y = ppos.y + dirOff[i].y;
			t->z = ppos.z + dirOff[i].z;
		}
		// ImGui で色を編集中でもライブ反映するため毎フレーム適用（clones は Object3DInstance）
		static_cast<Object3DInstance*>(jdClones_[i])->SetMaterialColor(jdCloneColor_);
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
		dodgeActive_          = true;
		dodgeTimer_           = 0.0f;
		dodgeCooldownTimer_   = dodgeCooldown_;
		dodgeActionLockTimer_ = dodgeActionLock_;

		// 移動入力方向へダッシュ（入力なしはその場）。playerVelocity_ に初速インパルスを足すだけなので
		// 以降の移動クリップ（isInsideClip）がそのまま効いて画面外には出ない。
		const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
		if (mlen > 1e-3f) {
			playerVelocity_.x += (moveDelta.x / mlen) * dodgeImpulse_.x;
			playerVelocity_.y += (moveDelta.y / mlen) * dodgeImpulse_.y;
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
	playerInvincibilityTimer_ = -2.0f;

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
	if (!actions || !player_) return;
	// ジャスト回避演出中は通常Healを発動しない（左派生で消費される＝同フレ二重消費を防ぐ）
	if (justDodgeActive_) return;
	if (!actions->IsTriggered(static_cast<int>(Action::Heal))) return;

	// デフォルトHeal = 大回復・固定回数制（STG/ボスで別カウント）。ストック制は廃止。
	const bool isBoss = (phase_ == Phase::Boss);
	int&      used = isBoss ? healUsedBoss_ : healUsedStg_;
	const int cap  = isBoss ? healMaxBoss_  : healMaxStg_;
	if (used >= cap) return;

	player_->GetHP().Heal(healAmount_);
	++used;
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
	if (flen < 1e-5f) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.y /= flen; fwd.z /= flen;

	// 右 = worldUp × 前
	const Vector3 worldUp{ 0.0f, 1.0f, 0.0f };
	Vector3 rgt{
		worldUp.y * fwd.z - worldUp.z * fwd.y,
		worldUp.z * fwd.x - worldUp.x * fwd.z,
		worldUp.x * fwd.y - worldUp.y * fwd.x,
	};
	float rlen = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
	if (rlen < 1e-5f) { rgt = { 1.0f, 0.0f, 0.0f }; rlen = 1.0f; } // 前が真上/真下のときの保険
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

void StagePlayScene::SpawnPendingMelee()
{
	if (!player_) return;
	Vector3 right, up, forward;
	ComputeAimBasis(right, up, forward);
	const int atk = player_->HasAttackPower() ? player_->GetAttackPower() : 0;
	SpawnPlayerMelee(player_, right, up, forward, meleePendingPrefab_, atk);
}

void StagePlayScene::UpdateMeleeCombo(InputActionMap* actions, float dt)
{
	if (!player_) return;

	// タイマー進行
	if (meleeActionLockTimer_ > 0.0f) {
		meleeActionLockTimer_ -= dt;
		if (meleeActionLockTimer_ < 0.0f) meleeActionLockTimer_ = 0.0f;
	}
	if (meleeComboIndex_ > 0) {
		meleeComboTimer_ -= dt;
		if (meleeComboTimer_ <= 0.0f) {
			meleeComboTimer_ = 0.0f;
			meleeComboIndex_ = 0; // 受付猶予超過でコンボリセット
		}
	}

	// 発生待ち：startup 経過で判定を spawn
	if (meleePending_) {
		meleeStartupTimer_ -= dt;
		if (meleeStartupTimer_ <= 0.0f) {
			meleeStartupTimer_ = 0.0f;
			meleePending_ = false;
			SpawnPendingMelee();
		}
	}

	if (!actions) return;

	// 行動ロック中（発生〜後隙）は新規入力を一切受け付けない
	if (meleeActionLockTimer_ > 0.0f) return;
	// 分身カウンター派生中は通常の近接入力を受け付けない
	// （MeleeStrong/MeleeWeak で派生が確定したフレームに通常近接が二重発動するのを防止）。
	if (justDodgeCounterActive_ || jdSelecting_) return;

	const bool strongTrig = actions->IsTriggered(static_cast<int>(Action::MeleeStrong));
	const bool weakTrig   = actions->IsTriggered(static_cast<int>(Action::MeleeWeak));
	if (!strongTrig && !weakTrig) return;

	// 攻撃スロットを決定（強=単発でコンボリセット / 弱=1→2→3→4→1 と段送り）
	std::string slot;
	if (strongTrig) {
		slot = "melee_strong";
		meleeComboIndex_ = 0;
	} else {
		const int next = (meleeComboIndex_ % kMeleeWeakComboMax_) + 1;
		slot = "melee_w" + std::to_string(next);
		meleeComboIndex_ = next;
	}

	std::string prefab = player_->FindBulletPrefab(slot);
	if (prefab.empty()) prefab = "TemporaryPlayerMelee"; // フォールバック

	// 発生/持続/後隙/受付猶予をプレハブの MeleeParams から
	float startup = 0.05f, active = 0.20f, recovery = 0.15f, comboWindow = 0.40f;
	if (const PrefabDef* mdef = PrefabManager::GetInstance()->Find(prefab); mdef && mdef->hasMelee) {
		startup     = mdef->meleeStartup;
		active      = mdef->meleeActiveDuration;
		recovery    = mdef->meleeRecovery;
		comboWindow = mdef->meleeComboWindow;
	}

	// 攻撃を予約：startup 後に判定発生。発生〜後隙まで全行動ロック。
	meleePendingPrefab_ = prefab;
	meleeStartupTimer_  = startup;
	meleePending_       = true;
	meleeActionLockTimer_ = startup + active + recovery;
	// 後隙終了後 comboWindow 秒だけ次段を受け付ける（その間に入力が無ければコンボリセット）
	meleeComboTimer_      = startup + active + recovery + comboWindow;

	// startup が 0 なら同フレームで発生させる（1フレーム遅延を避ける）
	if (meleeStartupTimer_ <= 0.0f) {
		meleeStartupTimer_ = 0.0f;
		meleePending_ = false;
		SpawnPendingMelee();
	}
}

void StagePlayScene::UpdateJustDodgeEffect(float dt)
{
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
	if (!actions) return;

	// LT / 右クリックのホールドでモード ON（トグルは将来追加）
	bool held = actions->IsPressed(static_cast<int>(Action::PrecisionAim));
#ifdef _DEBUG
	// Debug：マウスが Viewport 外で ImGui がマウスをキャプチャ中なら抑制（Fire と同じ扱い）
	{
		bool mouseOverViewport = false;
		if (auto* vp = ImGuiManager::Instance().GetViewportWindow()) {
			mouseOverViewport = vp->IsHovered();
		}
		if (!mouseOverViewport && ImGui::GetIO().WantCaptureMouse) {
			held = false;
		}
	}
#endif
	const float target = held ? 1.0f : 0.0f;

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
		const bool active = precisionBlend_ > 0.001f;
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
	const Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
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
		ImGui::Text("InputOffset: (%.2f, %.2f)  Vel: (%.2f, %.2f)",
			playerInputOffset_.x, playerInputOffset_.y,
			playerVelocity_.x, playerVelocity_.y);
	}

	if (ImGui::CollapsingHeader("Rail Camera")) {
		ImGui::DragFloat("Speed (t/sec)", &railCameraSpeed_, 0.005f, 0.0f, 5.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			changed = true;
			if (railCamera_) railCamera_->SetSpeed(railCameraSpeed_);
		}
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

	if (ImGui::CollapsingHeader("Dodge / Just Dodge / Heal")) {
		ImGui::Separator();
		ImGui::TextUnformatted("回避（Dodge）");
		ImGui::DragFloat("Just Window (s)", &dodgeJustWindow_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("回避入力後この秒数以内に被弾接触するとジャスト成立。遠近感対策で広めから調整");
		ImGui::DragFloat("I-Frame Duration (s)", &dodgeIFrameDuration_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("回避入力後この秒数まで被弾無効（Just Window を内包）。これを過ぎると被弾");
		ImGui::DragFloat("Cooldown (s)", &dodgeCooldown_, 0.02f, 0.0f, 3.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Action Lock (s)", &dodgeActionLock_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Dash Impulse (X/Y)", &dodgeImpulse_.x, 0.5f, 0.0f, 100.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("runtime: active=%d t=%.2f cd=%.2f lock=%.2f",
			dodgeActive_ ? 1 : 0, dodgeTimer_, dodgeCooldownTimer_, dodgeActionLockTimer_);

		ImGui::Separator();
		ImGui::TextUnformatted("ジャスト回避スロー（受付期限モデル）");
		ImGui::DragFloat("Slow World Scale", &justDodgeSlowWorld_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Receipt Window (s)", &justDodgeReceiptWindow_, 0.05f, 0.1f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("追加入力の受付期間。追加入力なしならこの秒数で演出終了（実時間）");
		ImGui::DragFloat("Fade In (s)", &justDodgeFadeIn_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fade Out (s)", &justDodgeFadeOut_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Just Dodge Score", &justDodgeScore_, 5.0f, 0, 100000);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		IImGuiEditable* sel = ImGuiManager::Instance().GetSelected();
		ImGui::Text("Highlight Target (Inspector 選択): %s", sel ? sel->GetName().c_str() : "(none)");
		if (ImGui::Button("Test: Trigger Just Dodge")) {
			TriggerJustDodge(sel);
		}
		ImGui::SameLine();
		if (ImGui::Button("Play Effect Only")) {
			PlayJustDodgeEffect(sel, justDodgeReceiptWindow_);
		}
		if (justDodgeActive_) {
			ImGui::Text("Active: %.2f s (counter=%d, fadeOut=%.2f)",
				justDodgeTimer_, justDodgeCounterActive_ ? 1 : 0, justDodgeFadeOutTimer_);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("分身カウンター（Phase1: カメラ引き＋プレビュー＋方向選択）");
		ImGui::DragFloat("Clone Offset", &jdCloneOffset_, 0.1f, 0.0f, 30.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam Pullback", &jdCamPullback_, 0.2f, 0.0f, 60.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam FovY Add", &jdCamFovAdd_, 0.005f, 0.0f, 0.5f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Select Threshold", &jdSelectThreshold_, 0.02f, 0.05f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Spread Duration", &jdSpreadDuration_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Merge Duration", &jdMergeDuration_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Clone Color", &jdCloneColor_.x); // alpha<1 で半透明
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		// 各方向の分身モデルパス（あとで派生モーション初期ポーズの.meshに差し替え可能）
		{
			const char* labels[4] = { "Path Up", "Path Right", "Path Down", "Path Left" };
			for (int i = 0; i < 4; ++i) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), "%s", jdClonePath_[i].c_str());
				if (ImGui::InputText(labels[i], buf, sizeof(buf))) jdClonePath_[i] = buf;
				if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
			}
		}
		ImGui::Text("selecting=%d chosen=%d clones=%d intensity=%.2f",
			jdSelecting_ ? 1 : 0, static_cast<int>(jdChosen_),
			static_cast<int>(jdClones_.size()), jdEffectIntensity_);

		ImGui::Separator();
		ImGui::TextUnformatted("分身カウンター派生（Phase2: 詰め寄り / 戻り / 専用カメラ）");
		ImGui::DragFloat("Melee Approach Duration (s)", &jdMeleeApproachDuration_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Melee Return Duration (s)",   &jdMeleeReturnDuration_,   0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Melee Approach Dist",         &jdMeleeApproachDist_,     0.1f,  0.0f, 30.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat3("Melee Cam Offset (R/U/F)",   &jdMeleeCameraOffset_.x,   0.05f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("プレイヤー基底（右/上/前=対象敵方向）でのカメラ位置オフセット。\n左斜め後ろ＝右-（左）, 上+, 前-（後ろ）");
		ImGui::DragFloat3("Melee Cam Look Offset",      &jdMeleeCameraLookOffset_.x, 0.05f);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("プレイヤーと敵の中点を基準にした注視点オフセット（基底空間）");
		ImGui::DragFloat2("Dodge Expanded Margin",      &jdDodgeExpandedMargin_.x, 0.01f, 0.0f, 3.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Dodge Return Duration (s)",   &jdDodgeReturnDuration_,   0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("phase=%d  jdReturnOffset=(%.2f, %.2f)  dodgeReturning=%d",
			static_cast<int>(jdActionPhase_), jdReturnOffset_.x, jdReturnOffset_.y, jdDodgeReturning_ ? 1 : 0);

		ImGui::Separator();
		ImGui::TextUnformatted("必殺技（傲慢サンダー）");
		ImGui::DragFloat("Special Gauge Max",   &specialGaugeMax_,      1.0f, 1.0f, 1000.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Dodge Gauge Gain",    &dodgeSpecialGaugeGain_,0.1f, 0.0f, 100.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Active Duration",     &specialDuration_,      0.05f, 0.1f, 30.0f, "%.2f sec");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ProgressBar(specialGauge_ / (std::max)(1.0f, specialGaugeMax_), ImVec2(-1.0f, 0.0f));
		const char* phaseName = "Idle";
		switch (specialPhase_) {
		case SpecialPhase::Barrier: phaseName = "Barrier"; break;
		case SpecialPhase::Lockon:  phaseName = "Lockon";  break;
		case SpecialPhase::Fire:    phaseName = "Fire";    break;
		case SpecialPhase::End:     phaseName = "End";     break;
		default: break;
		}
		ImGui::Text("Phase: %s  timer=%.2f  phaseTimer=%.2f",
			phaseName, specialTimer_, specialPhaseTimer_);

		// Phase 1（バリア）
		ImGui::SeparatorText("Phase 1 (Barrier)");
		ImGui::DragFloat("Barrier Radius",   &specialBarrierRadius_,   0.05f, 0.1f, 20.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Barrier Duration", &specialBarrierDuration_, 0.05f, 0.1f, 10.0f, "%.2f sec");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Checkbox("Barrier Fill (sphere)", &specialBarrierFillOn_);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("塗りつぶし半透明球の表示。OFF でワイヤ球のみ");
		ImGui::ColorEdit4("Barrier Color",   &specialBarrierColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Center Offset Up (0=auto)", &specialPlayerCenterOffset_, 0.02f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("バリア球・電撃の中心を up 方向へ持ち上げる量。\n0 で player Collider.offset.y（足元→体の中心）を自動採用");
		ImGui::Checkbox("Wireframe Sphere", &specialBarrierWireframeOn_);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat3("  Wire Spin Speed XYZ (rad/s)", &specialBarrierWireSpinSpeed_.x, 0.05f, -10.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("  Wire Meridians", &specialBarrierWireMeridians_, 0.2f, 1, 32);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("  Wire Parallels", &specialBarrierWireParallels_, 0.2f, 1, 32);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("  Wire Segments", &specialBarrierWireSegments_, 0.5f, 3, 96);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("  Wire Gold (meridian)", &specialBarrierWireColorGold_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("  Wire Pink (parallel)", &specialBarrierWireColorPink_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Checkbox("Barrier Particles", &specialBarrierParticleOn_);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("  Emit Interval (s)", &specialBarrierEmitInterval_, 0.002f, 0.001f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("  Emit Count / burst", &specialBarrierEmitCount_, 0.5f, 1, 200);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("  Particle Life (s)", &specialBarrierParticleLife_, 0.02f, 0.05f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("  Radius Scale", &specialBarrierParticleRadiusScale_, 0.02f, 0.1f, 3.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("発生半径 = Barrier Radius × これ");
		ImGui::DragFloat2("  Scale Min", &specialBarrierParticleScaleMin_.x, 0.005f, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("  Scale Max", &specialBarrierParticleScaleMax_.x, 0.005f, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("  Particle Color (start)", &specialBarrierParticleColor0_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("  Particle Color (end)",   &specialBarrierParticleColor1_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		// 光の翼（X字方向に小パーティクルを外向き噴出）
		ImGui::SeparatorText("光の翼 (Wings)");
		ImGui::Checkbox("Wings On", &specialWingOn_);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Wing Arms (本数)", &specialWingArmCount_, 0.2f, 1, 16);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Wing Angle Offset (rad)", &specialWingAngleOffset_, 0.02f, -3.14f, 3.14f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("配置の基準角。π/4≒0.79・本数4 で X 字");
		ImGui::DragFloat("Wing Speed (速度→長さ)", &specialWingSpeed_, 0.1f, 0.0f, 40.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("外向き初速。実質の長さ ≒ Speed × Life");
		ImGui::DragFloat("Wing Life (s)", &specialWingLife_, 0.02f, 0.05f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Wing Burst Count (量)", &specialWingBurstCount_, 0.2f, 1, 64);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Wing Jitter (ゆらぎ)", &specialWingJitter_, 0.02f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Wing Emit Radius", &specialWingEmitRadius_, 0.01f, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Wing Scale Min", &specialWingScaleMin_.x, 0.002f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Wing Scale Max", &specialWingScaleMax_.x, 0.002f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Wing Color (発生=金)", &specialWingColorInner_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Wing Color (末=ピンク)", &specialWingColorOuter_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		// Phase 2（チャージ電撃）
		ImGui::SeparatorText("Phase 2 Charge Lightning");
		ImGui::DragInt("Charge Bolt Count", &specialChargeBoltCount_, 0.1f, 1, 16);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Charge Regen Interval", &specialChargeRegenInterval_, 0.005f, 0.01f, 1.0f, "%.3f sec");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Charge Start H (0=auto)", &specialChargeStartRadiusH_, 0.02f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Charge Start V (0=auto)", &specialChargeStartRadiusV_, 0.02f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::TextDisabled("(0 のとき player Collider.capsule から自動算出)");
		ImGui::DragFloat("Charge Min Length", &specialChargeMinLength_, 0.02f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		// Phase 3（Fire = サンダー発射）
		ImGui::SeparatorText("Phase 3 (Fire)");
		ImGui::DragInt("Fire Simultaneous", &specialFireSimultaneous_, 0.1f, 1, 16);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Min Hold (s)", &specialFireMinHold_, 0.01f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Max Hold (s)", &specialFireMaxHold_, 0.01f, 0.1f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Tick Interval (s)", &specialFireTickInterval_, 0.005f, 0.01f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragInt("Fire Tick Damage", &specialFireTickDamage_, 0.2f, 0, 999);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Launch Interval (s)", &specialFireLaunchInterval_, 0.005f, 0.0f, 2.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::ColorEdit4("Fire Color", &specialFireColor_.x);
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Grow Time (s)", &specialFireGrowTime_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("発射直後に始点→敵へ伸びる時間。0で即全長。大きいほど飛んでいく感");
		ImGui::DragFloat("Fire Zigzag (maxOffset)", &specialFireMaxOffsetRatio_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("ジグザグの暴れ幅。小さいほど直線的＝飛んでる感、大きいほど放電感");
		ImGui::DragFloat("Fire Branch Prob", &specialFireBranchProb_, 0.01f, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("枝分かれ確率。放電のパチパチ感");
		ImGui::DragFloat("Fire Start Width", &specialFireStartWidth_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire End Width", &specialFireEndWidth_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Fire Bolt Lifetime (s)", &specialFireBoltLifetime_, 0.005f, 0.01f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("1メッシュの寿命。小さいほど高速にパチパチ再生成");
		ImGui::DragFloat("End Duration (s)", &specialEndDuration_, 0.05f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::Text("fire bolts=%d  queue=%d", static_cast<int>(specialFireBolts_.size()), static_cast<int>(specialFireQueue_.size()));

		// カメラ引き
		ImGui::SeparatorText("Camera Pullback");
		ImGui::DragFloat("Cam Pullback (forward)", &specialCamPullback_, 0.1f, 0.0f, 60.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam FovY Add",           &specialCamFovAdd_,   0.005f, 0.0f, 1.0f, "%.3f rad");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam Up Add",             &specialCamUpAdd_,    0.05f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
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
		ImGui::DragFloat("Shake Intensity", &damageCameraShakeIntensity_, 0.02f, 0.0f, 5.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Shake Duration (s)", &damageCameraShakeDuration_, 0.02f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (ImGui::Button("Test: Take 10 Damage") && player_) {
			OnPlayerTakeDamage(10);
		}
		if (player_) {
			const HP& hp = player_->GetHP();
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

	if (ImGui::CollapsingHeader("Score UI")) {
		ImGui::Text("Score: %d", ScoreManager::GetInstance()->GetScore());
		if (ImGui::Button("Reset Score")) ScoreManager::GetInstance()->Reset();
		ImGui::SameLine();
		if (ImGui::Button("+1000")) ScoreManager::GetInstance()->AddScore(1000);

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
			lp.appearance.startWidth = 0.4f;
			lp.appearance.endWidth   = 0.4f;
			lp.appearance.planeCount = 3;
			lp.appearance.fadeStartLength = 0.5f;
			lp.appearance.fadeEndLength   = 0.5f;
			lp.appearance.startColor = { 0.6f, 0.8f, 1.0f, 1.0f };
			lp.appearance.endColor   = { 0.6f, 0.8f, 1.0f, 1.0f };
			lp.generations = 5;
			lp.maxOffsetRatio = 0.15f;
			lp.branchProbability = 0.15f;
			lp.branchLengthScale = 0.25f;
			lightningTest_->Initialize(lp, "Resources/Textures/white1x1.dds", 2 /*Add*/);
			// 始終点
			if (camera_) {
				Vector3 camPos = camera_->GetTranslate();
				Vector3 fwd    = camera_->GetForward();
				Vector3 endP = { camPos.x + fwd.x * 30.0f, camPos.y + fwd.y * 30.0f, camPos.z + fwd.z * 30.0f };
				lightningTest_->SetEndpoints(camPos, endP);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			if (lightningTest_) lightningTest_->Stop();
		}
		ImGui::TextDisabled("※ 始終点はSpawn時のカメラに固定。動的追従の正式版は傲慢サンダー実装時に。");
	}

	// 末尾で一括保存（変更があれば即書き出し）
	if (changed) SaveTuningToJson();
#endif
}

void StagePlayScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	// スコアをリセット（再ロード時の累積防止）
	ScoreManager::GetInstance()->Reset();

	// 調整値の読み込み（プレイヤー生成 / RailCamera 設定より前に行う）
	LoadTuningFromJson();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -20.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
	baseFovY_ = camera_->GetFovY();   // 精密射撃モードのズーム基準（通常時 FovY）
	skyboxManager_->SetDefaultCamera(camera_.get());

	// Skybox 生成（DemoScene と同じ Cubemap を使用）
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, "Resources/Cubemaps/rogland_clear_night_4k.dds");
	object3DManager_->SetEnvironmentTexture("Resources/Cubemaps/rogland_clear_night_4k.dds");

	// レールカメラ用スプライン（位置）
	cameraPath_ = std::make_unique<SplineCurveActor>();
	cameraPath_->SetName("CameraPath");
	cameraPath_->SetTag(EntityTag::CameraPathSpline);
	cameraPath_->MutablePoints() = {
		{   0.0f, 5.0f,   0.0f },
		{  10.0f, 5.0f,  20.0f },
		{  20.0f, 8.0f,  40.0f },
		{  30.0f, 5.0f,  60.0f },
		{  40.0f, 5.0f,  80.0f },
	};

	// レールカメラ用スプライン（注視点）— cameraPath を +Z に平行移動して前進視点にする
	lookAtPath_ = std::make_unique<SplineCurveActor>();
	lookAtPath_->SetName("LookAtPath");
	lookAtPath_->SetTag(EntityTag::CameraLookAtSpline);
	{
		auto& src = cameraPath_->GetPoints();
		std::vector<Vector3> dst;
		dst.reserve(src.size());
		constexpr float kForwardOffset = 10.0f;
		for (const auto& p : src) {
			dst.push_back({ p.x, p.y, p.z + kForwardOffset });
		}
		lookAtPath_->MutablePoints() = std::move(dst);
	}

	// レールカメラコントローラ
	railCamera_ = std::make_unique<RailCameraController>();
	railCamera_->Initialize(camera_.get());
	railCamera_->SetCameraPath(cameraPath_.get());
	railCamera_->SetLookAtPath(lookAtPath_.get());
	railCamera_->SetSpeed(railCameraSpeed_);
	railCamera_->SetLoop(true);

	phase_ = Phase::Rail;
	paused_ = false;

	// レティクル
	reticle_ = std::make_unique<Reticle>();
	reticle_->Initialize(spriteManager_);
	// JSON から読み込んだ外側オフセット範囲・サイズ範囲を Reticle に初期反映
	reticle_->SetLockOnSizeClampOutside(reticleOuterMinPx_, reticleOuterMaxPx_);
	reticle_->SetOuterSizeClamp(reticleOuterSizeMinPx_, reticleOuterSizeMaxPx_);
	reticleBaseStickSpeed_ = reticle_->GetStickSpeed();   // 精密射撃モードの感度低下の基準

	// 前回保存したシーン配置があれば自動ロード（先にやって、Player が含まれていれば再生成しない）
	{
		const std::string kAutoLoadPath = "Resources/Json/Scenes/StagePlay.json";
		if (std::filesystem::exists(kAutoLoadPath)) {
			LoadSceneFromJson(kAutoLoadPath);
		}
	}

	// auto-load で Player が復元されていなければデフォルトのプレイヤープレハブを生成
	if (!player_) {
		InstantiatePrefab("player", { 0.0f, 0.0f, 0.0f });
		if (!dynamicAnimated_.empty()) {
			player_ = dynamicAnimated_.back().get();
			player_->SetName("Player");
		}
	}

	// ウェーブ定義ロード（存在しなければ空のまま=何も湧かない）
	{
		const std::string wavePath = "Resources/Json/Waves/stage1.json";
		if (std::filesystem::exists(wavePath)) {
			if (WaveDefIO::LoadFromFile(wavePath, currentWave_)) {
				spawnFired_.assign(currentWave_.entries.size(), false);
				retreatFired_.assign(currentWave_.entries.size(), false);
				killAtT_.assign(currentWave_.entries.size(), -1.0f);
			}
		}
	}

	// HP バー UI 初期化
	InitializeHPBarUI();
	// 必殺技ゲージ UI 初期化
	InitializeSpecialGaugeUI();
}

void StagePlayScene::Finalize() {
	// 必殺技関連の GPU リソースを破棄する前に GPU 完了を待つ。
	// （ウィンドウ閉じ等でシーンが落ちる際、command list がまだ参照中の resource を
	//   解放すると D3D12 OBJECT_DELETED_WHILE_STILL_IN_USE になる）
	if (dxCore_) {
		dxCore_->WaitForGpu();
	}
	specialTrash_.clear();
	specialBarrierVis_.reset();
	specialLockonTargets_.clear();
	specialChargeBolts_.clear();
	specialFireBolts_.clear();
	specialFireQueue_.clear();
	specialFireProcessed_.clear();
}

void StagePlayScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	// Pause アクションでポーズトグル（メニュー実装は後で）
	if (actions->IsTriggered(static_cast<int>(Action::Pause))) {
		paused_ = !paused_;
	}
	if (paused_) {
		return;
	}

#ifdef _DEBUG
	// DEBUG専用ショートカット（StagePlayScene完成時に削除）
	auto* kb = input_->GetKeyboard();
	if (kb->TriggerKey(DIK_F2)) {
		SceneManager::GetInstance()->ChangeScene("RESULT", TransitionType::Fade);
		return;
	}
	if (kb->TriggerKey(DIK_F3)) {
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
		return;
	}
	if (kb->TriggerKey(DIK_F4)) {
		switch (phase_) {
		case Phase::Rail:    phase_ = Phase::Landing; break;
		case Phase::Landing: phase_ = Phase::Boss;    break;
		case Phase::Boss:    phase_ = Phase::Rail;    break;
		}
	}

	// スプライン可視化（Hierarchy の Debug 表示 ON 時のみ描かれる）
	if (cameraPath_) cameraPath_->DrawDebug();
	if (lookAtPath_) lookAtPath_->DrawDebug();
#endif

	// レール走行
	if (phase_ == Phase::Rail && railCamera_) {
		railCamera_->Update(GetScaledDeltaTime());
	}

	// 精密射撃モード（FOV ズーム・感度・演出）。FOV を camera_->Update() の前に反映する
	UpdatePrecisionAim(actions, GetScaledDeltaTime());

	// FovY を基準値に戻してから Update する。プレイヤー移動のクリップ判定は基準カメラの
	// VP で行う必要があるため（前フレームのズーム済み FovY が残ると画面端で投影が拡大し、移動が固まる）。
	// 実際のズーム描画は後段の ApplyPrecisionCamera() が改めて反映する。
	camera_->SetFovY(baseFovY_);
	camera_->Update();
	UpdateDebugCameraIfActive();

	if (skybox_) {
		skybox_->Update();
	}

	// プレイヤー：入力でカメラ空間オフセットを動かしつつ、コライダーが画面端を越えないようクリップ
	if (player_) {
		// プレイヤーの移動・入力は Player グループ（既定 1.0）で動かす＝ジャスト回避のワールドスロー中も等倍。
		const float dt = GetScaledDeltaTime(TimeGroup::Player);

		// ----- 入力で moveDelta (-1..1) を作る -----
		Vector2 moveDelta{ 0.0f, 0.0f };
		auto* kb = input_->GetKeyboard();
		if (kb->PuhsKey(DIK_A)) moveDelta.x -= 1.0f;
		if (kb->PuhsKey(DIK_D)) moveDelta.x += 1.0f;
		if (kb->PuhsKey(DIK_W)) moveDelta.y += 1.0f;
		if (kb->PuhsKey(DIK_S)) moveDelta.y -= 1.0f;
		if (auto* pad = input_->GetController(); pad && pad->IsConnected()) {
			const auto& ls = pad->GetLeftStick();
			moveDelta.x += ls.x * ls.magnitude;
			moveDelta.y += ls.y * ls.magnitude;
		}
		moveDelta.x = std::clamp(moveDelta.x, -1.0f, 1.0f);
		moveDelta.y = std::clamp(moveDelta.y, -1.0f, 1.0f);

		// 必殺技発動中は入力ロック（移動・慣性をゼロ化）
		if (specialActive_) {
			moveDelta = { 0.0f, 0.0f };
			playerVelocity_ = { 0.0f, 0.0f };
		}

		// 分身選択中も WASD/スティックは通常移動として有効（方向選択はアクションボタン側で受ける）。
		// 下=追加回避を選んだ瞬間の WASD を回避方向に使うため、ここでは zero 化しない。

		// 目標速度（入力 × 最大速度）に対して指数減衰で接近させる＝慣性
		Vector2 targetVel = {
			moveDelta.x * playerMoveSpeed_.x,
			moveDelta.y * playerMoveSpeed_.y,
		};
		float alpha = (playerSmoothTime_ > 1e-4f)
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

		// 候補オフセット（クリップで反映可否を判定する）
		Vector2 candidate = {
			playerInputOffset_.x + playerVelocity_.x * dt,
			playerInputOffset_.y + playerVelocity_.y * dt,
		};

		const Matrix4x4& camWorld = camera_->GetWorldMatrix();
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();

		// 候補位置でコライダーが画面端 + margin を超えないかチェック。超える軸は据え置き。
		auto isInsideClip = [&](float ox, float oy) {
			Vector3 worldPos = TransformCoordinate(
				{ playerLocalOffset_.x + ox, playerLocalOffset_.y + oy, playerLocalOffset_.z },
				camWorld);

			// プレイヤーの回転＝カメラ回転前提でコライダーの軸を構築
			Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
			Vector3 axes[3] = {
				{ rot.m[0][0], rot.m[0][1], rot.m[0][2] },
				{ rot.m[1][0], rot.m[1][1], rot.m[1][2] },
				{ rot.m[2][0], rot.m[2][1], rot.m[2][2] },
			};

			const Collider& col = player_->GetCollider();
			Vector3 center = {
				worldPos.x + col.offset.x,
				worldPos.y + col.offset.y,
				worldPos.z + col.offset.z,
			};

			// 形状に応じた「外殻ローカル AABB の半幅」
			Vector3 he{ 0.0f, 0.0f, 0.0f };
			switch (col.shape) {
			case ColliderShape::Sphere:
				he = { col.radius, col.radius, col.radius };
				break;
			case ColliderShape::OBB:
				he = col.halfExtents;
				break;
			case ColliderShape::Capsule:
				he = { col.capsuleRadius, 0.5f * col.capsuleHeight + col.capsuleRadius, col.capsuleRadius };
				break;
			}

			// 8 角を World → Clip 投影して X/Y が許容範囲を超えるか。
			// 追加回避（Down）派生中は許容枠を一時拡張。戻し中(jdDodgeReturning_)は時間で線形に縮める。
			// ただし「プレイヤー現在位置を含む最小マージン」を下回らないようにする（戻し中に固定回避）。
			Vector2 effMargin = playerClipMargin_;
			// 近接派生中はクリップ制限を実質無効化（Approach で対象敵手前まで自由に移動できるように）。
			if (jdMeleeCameraActive_) {
				effMargin = { 10.0f, 10.0f };
			} else
			if (jdDodgeMarginActive_) {
				if (jdDodgeReturning_ && jdDodgeReturnDuration_ > 1e-4f) {
					float t = jdDodgeMarginTimer_ / jdDodgeReturnDuration_;
					if (t > 1.0f) t = 1.0f;
					effMargin.x = jdDodgeExpandedMargin_.x + (playerClipMargin_.x - jdDodgeExpandedMargin_.x) * t;
					effMargin.y = jdDodgeExpandedMargin_.y + (playerClipMargin_.y - jdDodgeExpandedMargin_.y) * t;
				} else {
					effMargin = jdDodgeExpandedMargin_;
				}
				// プレイヤー現在位置(中心)のNDCを取り、それを含む最小 margin と比較して大きい方を採用。
				// これで戻し中にマージンが縮みすぎて player が範囲外で固まる事故を防ぐ（押し戻しで内側に来たら自然に縮む）。
				Vector3 curWorld = TransformCoordinate(
					{ playerLocalOffset_.x + playerInputOffset_.x,
					  playerLocalOffset_.y + playerInputOffset_.y,
					  playerLocalOffset_.z }, camWorld);
				float cwx = curWorld.x * vp.m[0][0] + curWorld.y * vp.m[1][0] + curWorld.z * vp.m[2][0] + vp.m[3][0];
				float cwy = curWorld.x * vp.m[0][1] + curWorld.y * vp.m[1][1] + curWorld.z * vp.m[2][1] + vp.m[3][1];
				float cww = curWorld.x * vp.m[0][3] + curWorld.y * vp.m[1][3] + curWorld.z * vp.m[2][3] + vp.m[3][3];
				if (cww > 1e-4f) {
					float ndcX = cwx / cww;
					float ndcY = cwy / cww;
					float needX = (std::max)(0.0f, std::fabs(ndcX) - 1.0f) + 0.05f;
					float needY = (std::max)(0.0f, std::fabs(ndcY) - 1.0f) + 0.05f;
					if (needX > effMargin.x) effMargin.x = needX;
					if (needY > effMargin.y) effMargin.y = needY;
				}
			}
			const float xLim = 1.0f + effMargin.x;
			const float yLim = 1.0f + effMargin.y;
			for (int i = 0; i < 8; ++i) {
				float sx = (i & 1) ? +1.0f : -1.0f;
				float sy = (i & 2) ? +1.0f : -1.0f;
				float sz = (i & 4) ? +1.0f : -1.0f;
				Vector3 local = {
					center.x + axes[0].x * he.x * sx + axes[1].x * he.y * sy + axes[2].x * he.z * sz,
					center.y + axes[0].y * he.x * sx + axes[1].y * he.y * sy + axes[2].y * he.z * sz,
					center.z + axes[0].z * he.x * sx + axes[1].z * he.y * sy + axes[2].z * he.z * sz,
				};
				// w 除算で NDC へ
				float wx = local.x * vp.m[0][0] + local.y * vp.m[1][0] + local.z * vp.m[2][0] + vp.m[3][0];
				float wy = local.x * vp.m[0][1] + local.y * vp.m[1][1] + local.z * vp.m[2][1] + vp.m[3][1];
				float ww = local.x * vp.m[0][3] + local.y * vp.m[1][3] + local.z * vp.m[2][3] + vp.m[3][3];
				if (ww <= 1e-4f) return false;
				float ndcX = wx / ww;
				float ndcY = wy / ww;
				if (ndcX < -xLim || ndcX > xLim) return false;
				if (ndcY < -yLim || ndcY > yLim) return false;
			}
			return true;
		};

		// 軸ごとに採否判定（X 単独・Y 単独で押し戻し）。拒否された軸は速度もゼロに（壁にぶつかった扱い）。
		Vector2 next = playerInputOffset_;
		if (isInsideClip(candidate.x, playerInputOffset_.y)) {
			next.x = candidate.x;
		} else {
			playerVelocity_.x = 0.0f;
		}
		if (isInsideClip(next.x, candidate.y)) {
			next.y = candidate.y;
		} else {
			playerVelocity_.y = 0.0f;
		}
		playerInputOffset_ = next;

		// 追加回避の戻り：コライダー 8 角の最大 NDC で軸ごとに判定。
		// 通常マージンを超えてる軸だけ内側へ押し戻す（ダッシュで取った別軸位置は維持）。
		if (jdDodgeMarginActive_ && jdDodgeReturning_) {
			// 8 角の NDC を全部取って、軸ごとの max(|ndc|) を求める
			Vector3 worldPosN = TransformCoordinate(
				{ playerLocalOffset_.x + playerInputOffset_.x,
				  playerLocalOffset_.y + playerInputOffset_.y, playerLocalOffset_.z }, camWorld);
			Matrix4x4 rotN = MakeRotateMatrix(camera_->GetRotate());
			Vector3 axesN[3] = {
				{ rotN.m[0][0], rotN.m[0][1], rotN.m[0][2] },
				{ rotN.m[1][0], rotN.m[1][1], rotN.m[1][2] },
				{ rotN.m[2][0], rotN.m[2][1], rotN.m[2][2] },
			};
			const Collider& colN = player_->GetCollider();
			Vector3 centerN = { worldPosN.x + colN.offset.x, worldPosN.y + colN.offset.y, worldPosN.z + colN.offset.z };
			Vector3 heN{ 0.0f, 0.0f, 0.0f };
			switch (colN.shape) {
			case ColliderShape::Sphere:  heN = { colN.radius, colN.radius, colN.radius }; break;
			case ColliderShape::OBB:     heN = colN.halfExtents; break;
			case ColliderShape::Capsule: heN = { colN.capsuleRadius, 0.5f * colN.capsuleHeight + colN.capsuleRadius, colN.capsuleRadius }; break;
			}
			float maxAbsX = 0.0f, maxAbsY = 0.0f;
			bool valid = true;
			for (int i = 0; i < 8; ++i) {
				float sx = (i & 1) ? +1.0f : -1.0f;
				float sy = (i & 2) ? +1.0f : -1.0f;
				float sz = (i & 4) ? +1.0f : -1.0f;
				Vector3 local = {
					centerN.x + axesN[0].x * heN.x * sx + axesN[1].x * heN.y * sy + axesN[2].x * heN.z * sz,
					centerN.y + axesN[0].y * heN.x * sx + axesN[1].y * heN.y * sy + axesN[2].y * heN.z * sz,
					centerN.z + axesN[0].z * heN.x * sx + axesN[1].z * heN.y * sy + axesN[2].z * heN.z * sz,
				};
				float wx = local.x * vp.m[0][0] + local.y * vp.m[1][0] + local.z * vp.m[2][0] + vp.m[3][0];
				float wy = local.x * vp.m[0][1] + local.y * vp.m[1][1] + local.z * vp.m[2][1] + vp.m[3][1];
				float ww = local.x * vp.m[0][3] + local.y * vp.m[1][3] + local.z * vp.m[2][3] + vp.m[3][3];
				if (ww <= 1e-4f) { valid = false; break; }
				float ndcX = wx / ww;
				float ndcY = wy / ww;
				if (std::fabs(ndcX) > maxAbsX) maxAbsX = std::fabs(ndcX);
				if (std::fabs(ndcY) > maxAbsY) maxAbsY = std::fabs(ndcY);
			}
			if (valid) {
				// 通常マージンより内側のセーフティ位置まで押し戻す（境界ピッタリで次フレ固定回避）
				const float xTgt = 1.0f + playerClipMargin_.x - 0.05f;
				const float yTgt = 1.0f + playerClipMargin_.y - 0.05f;
				bool outX = maxAbsX > xTgt;
				bool outY = maxAbsY > yTgt;
				if (outX || outY) {
					const float pushSpeed = 30.0f;
					if (outX) {
						float sign = (playerInputOffset_.x > 0.0f) ? -1.0f : 1.0f;
						playerInputOffset_.x += sign * pushSpeed * dt;
					}
					if (outY) {
						float sign = (playerInputOffset_.y > 0.0f) ? -1.0f : 1.0f;
						playerInputOffset_.y += sign * pushSpeed * dt;
					}
					playerVelocity_ = { 0.0f, 0.0f };
				} else {
					// コライダー全体がセーフティ枠内 → 派生終了（次フレの通常クリップでも確実に内側）
					jdDodgeMarginActive_ = false;
					jdDodgeReturning_    = false;
					EndJustDodgeCounterAction();
				}
			}
		}

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
				float t = (jdMeleeApproachDuration_ > 1e-4f)
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
				float t = (jdMeleeReturnDuration_ > 1e-4f)
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

		player_->SetTranslate(worldPos);
		// 回転は照準ロジックで後段にてセット（camera 同期は廃止）

		// 分身の本体追従＋分裂アニメ＋方向選択（ジャスト回避中のみ動作）。
		// actions を渡すとアクションボタン (Melee*/Dodge/Heal) で方向確定する。
		UpdateJustDodgeClones(actions, moveDelta, GetScaledDeltaTime(TimeGroup::UI));

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
	}

	// BaseScene の動的エンティティ群（プレハブ生成物含む）の Update
	UpdateDynamicObjects();
	UpdateDynamicAnimated(GetScaledDeltaTime());
	UpdateDynamicPrimitives();
	UpdateDynamicSprites();

#ifdef _DEBUG

	DrawDynamicSplinesDebug();

#endif // _DEBUG

	// 全シーン共通の EffectManager + GPUParticle を更新
	UpdateGlobalEffects(camera_.get(), GetScaledDeltaTime());

	// ジャスト回避演出のタイマ更新。受付期間/フェードは実時間（UI グループ）で計測する
	// （World をスローにしても受付 3 秒は実時間で一定にしたいため）。
	UpdateJustDodgeEffect(GetScaledDeltaTime(TimeGroup::UI));

	// HP 回復（ジャスト回避ストックを消費）
	UpdateHeal(actions);

	// レティクル：Viewport 内でマウスが動いたときだけワープ（ImGui 操作と衝突させない）
	if (reticle_) {
		bool mouseHovered = false;
		bool mouseMoved   = false;
		Vector2 mouseClient{ 0.0f, 0.0f };
#ifdef _DEBUG
		auto* vp = ImGuiManager::Instance().GetViewportWindow();
		if (vp && vp->IsHovered()) {
			ImVec2 ip = vp->GetImageScreenPos();
			ImVec2 is = vp->GetImageScreenSize();
			ImVec2 ms = ImGui::GetMousePos();
			if (is.x > 0.0f && is.y > 0.0f) {
				mouseHovered = true;
				mouseClient.x = (ms.x - ip.x) * (static_cast<float>(WindowsApplication::kClientWidth) / is.x);
				mouseClient.y = (ms.y - ip.y) * (static_cast<float>(WindowsApplication::kClientHeight) / is.y);
				ImVec2 d = ImGui::GetIO().MouseDelta;
				mouseMoved = (d.x != 0.0f) || (d.y != 0.0f);
			}
		}
#else
		// Release ビルドはスワップチェーン直書き＝マウス座標がそのままクライアント
		auto* m = input_->GetMouse();
		if (m) {
			mouseClient.x = static_cast<float>(m->GetClientX());
			mouseClient.y = static_cast<float>(m->GetClientY());
			mouseHovered = true;
			mouseMoved = (m->GetDeltaX() != 0) || (m->GetDeltaY() != 0);
		}
#endif
		reticle_->Update(mouseHovered, mouseClient, mouseMoved,
			input_->GetController(), GetScaledDeltaTime());
	}

	// ----- 照準ターゲット計算（プレイヤー回転 + 発射方向の元） -----
	if (player_ && reticle_ && camera_) {
		const Vector2 rp = reticle_->GetPosition();
		const float ndcX = (rp.x / float(WindowsApplication::kClientWidth)) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (rp.y / float(WindowsApplication::kClientHeight)) * 2.0f;
		const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
		const Vector3 farWorld = TransformCoordinate(Vector3{ ndcX, ndcY, 1.0f }, invVP);
		const Vector3 camPos = camera_->GetTranslate();
		Vector3 rayDir{ farWorld.x - camPos.x, farWorld.y - camPos.y, farWorld.z - camPos.z };
		const float rayLen = std::sqrt(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
		if (rayLen > 1e-6f) {
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
			if (ww <= 1e-4f) return false; // カメラ後方
			const float ndcX = wx / ww;
			const float ndcY = wy / ww;
			outPx.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(WindowsApplication::kClientWidth);
			outPx.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(WindowsApplication::kClientHeight);
			outClipW = ww;
			return true;
		};

		const Matrix4x4& projMat = camera_->GetProjectionMatrix();
		const float invTanHalfFovY = projMat.m[1][1];
		const float clientH = static_cast<float>(WindowsApplication::kClientHeight);

		auto checkEnemy = [&](IImGuiEditable* e, const Vector3& pos) {
			if (!e || e->GetTag() != EntityTag::Enemy) return;
			const auto& col = e->GetCollider();
			if (!col.enabled || col.shape != ColliderShape::Sphere) return;
			const Vector3 center{ pos.x + col.offset.x, pos.y + col.offset.y, pos.z + col.offset.z };

			// 敵中心をスクリーン投影
			Vector2 enemyPx{};
			float clipW = 0.0f;
			if (!projectToPixel(center, enemyPx, clipW)) return;

			// カメラから敵中心までの距離
			const Vector3 toEnemy{ center.x - camPos.x, center.y - camPos.y, center.z - camPos.z };
			const float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y + toEnemy.z * toEnemy.z);
			if (dist < 1e-4f) return;

			// 敵の見かけ半径(px)
			const float pixelRadius = col.radius * clientH * invTanHalfFovY * 0.5f / dist;

			// レティクルとの pixel 距離
			const float dx = enemyPx.x - rp.x;
			const float dy = enemyPx.y - rp.y;
			const float dPx = std::sqrt(dx * dx + dy * dy);

			// 許容範囲：見かけ半径 × アシスト倍率
			const float assistThreshold = pixelRadius * aimAssistPixelScale_;

			// 画面上最近の敵を更新（軽ホーミング先候補）
			if (dPx < nearestPx) {
				nearestPx = dPx;
				nearestLocal = e;
			}

			if (dPx <= assistThreshold && dist < bestT) {
				bestT = dist;
				// ロックオン時は敵中心へ吸い込み（パルテナ式）
				desiredTarget = center;
				hitEnemyRadius = col.radius;
				hitEnemy = true;
				lockedLocal = e;
			}
		};

		for (auto& p : dynamicPrimitives_) {
			if (p) checkEnemy(p.get(), *p->GetEditableTranslate());
		}
		for (auto& a : dynamicAnimated_) {
			if (a) checkEnemy(a.get(), a->GetTranslate());
		}
		for (auto& o : object3DInstances_) {
			if (o) checkEnemy(o.get(), *o->GetEditableTranslate());
		}

		// 弾発射用は Lerp 前の即時 target（ロックオン直後でも遅れずに敵へ向かう）
		firingTarget_ = desiredTarget;

		// プレイヤー回転用は Lerp でぬるっと追従
		if (!aimInitialized_) {
			aimTarget_ = desiredTarget;
			aimInitialized_ = true;
		} else {
			const float dtP = GetScaledDeltaTime(TimeGroup::Player);
			const float alpha = (aimSmoothTime_ > 1e-4f)
				? (1.0f - std::exp(-dtP / aimSmoothTime_))
				: 1.0f;
			aimTarget_.x += (desiredTarget.x - aimTarget_.x) * alpha;
			aimTarget_.y += (desiredTarget.y - aimTarget_.y) * alpha;
			aimTarget_.z += (desiredTarget.z - aimTarget_.z) * alpha;
		}

		// プレイヤー回転を player → aimTarget 方向に。
		// ただし近接派生中（Up/Right）はレティクルではなく対象敵を直接向く。
		const Vector3 playerPos = player_->GetTranslate();
		Vector3 aimTgt = aimTarget_;
		if (jdMeleeCameraActive_ && jdCounterTarget_) {
			if (Vector3* tp = jdCounterTarget_->GetEditableTranslate()) aimTgt = *tp;
		}
		const Vector3 toAim{ aimTgt.x - playerPos.x, aimTgt.y - playerPos.y, aimTgt.z - playerPos.z };
		const float horiz = std::sqrt(toAim.x * toAim.x + toAim.z * toAim.z);
		if (horiz > 1e-4f || std::abs(toAim.y) > 1e-4f) {
			const float yaw = std::atan2(toAim.x, toAim.z);
			const float pitch = -std::atan2(toAim.y, horiz);
			player_->SetRotate({ pitch, yaw, 0.0f });
		}

		// ロックオン時：敵の見かけサイズ（スクリーンpixel半径）を計算してレティクルへ
		if (hitEnemy && reticle_) {
			// 敵中心までのカメラからの距離（ray の t をそのまま使える）
			const float distToEnemy = bestT;
			// projection の m[1][1] = 1 / tan(fovY/2) なので、
			// pixelRadius = radius * clientHeight * m[1][1] / (2 * dist)
			const Matrix4x4& proj = camera_->GetProjectionMatrix();
			const float invTanHalfFovY = proj.m[1][1];
			const float pixelRadius = (distToEnemy > 1e-4f)
				? (hitEnemyRadius * static_cast<float>(WindowsApplication::kClientHeight)
					* invTanHalfFovY * 0.5f / distToEnemy)
				: 0.0f;
			// レティクルは外周マージン付きで少し大きめに（敵を囲む見た目に）
			constexpr float kReticleMargin = 1.25f;
			reticle_->SetLockOnTargetSize(pixelRadius * 2.0f * kReticleMargin);
		}
		// ----- 近接レンジ表示：弱近接1段目(melee_w1)の判定球が、ロック中の敵に届くか -----
		// レティクルが敵に重なっている時(hitEnemy)だけ判定。届くなら近接用テクスチャに切替。
		bool meleeInRange = false;
		if (hitEnemy && player_) {
			std::string mprefab = player_->FindBulletPrefab("melee_w1");
			if (mprefab.empty()) mprefab = "TemporaryPlayerMelee";
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
				// ロック中の敵中心 = desiredTarget、敵半径 = hitEnemyRadius（既に取得済み）
				const float ddx = hitCenter.x - desiredTarget.x;
				const float ddy = hitCenter.y - desiredTarget.y;
				const float ddz = hitCenter.z - desiredTarget.z;
				const float d = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
				meleeInRange = (d <= meleeRadius + hitEnemyRadius);
			}
		}
		reticle_->SetMeleeInRange(meleeInRange);

		reticle_->SetLockOn(hitEnemy);

		// 弾発射時のホーミング選択用にメンバへ反映
		lockedEnemy_  = lockedLocal;
		nearestEnemy_ = nearestLocal;
	}

	// ゲーム時間が止まっている時はゲームロジック（射撃／弾の進行）をスキップ。
	// TimeScale=0（"Pause" ボタン等）でシーンに敵を配置するときに弾が湧き続けないようにする。
	const float worldDt = GetScaledDeltaTime(TimeGroup::World);
	const bool gameFrozen = worldDt <= 0.0001f;

	// ----- 射撃・チャージシステム -----
	if (!gameFrozen) {
		const float dtP = GetScaledDeltaTime(TimeGroup::Player);
		fireTimer_ -= dtP;
		if (fireTimer_ < 0.0f) fireTimer_ = 0.0f;

		// プレイヤープレハブからチャージ時間を反映（有効時のみ）
		if (player_) {
			const ChargeParams& chp = player_->GetChargeParams();
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
		if (!firePressed && player_) {
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
					const std::string startEff = player_->FindEffect("charge_start");
					const std::string holdEff  = player_->FindEffect("charge_hold");
					if (!startEff.empty()) chargeStartEffectHandle_ = effectMgr->Play(startEff, player_->GetTranslate());
					if (!holdEff.empty())  chargeHoldEffectHandle_  = effectMgr->Play(holdEff, player_->GetTranslate());
				}
			}

			// 2段階目発動（6秒経過）→ 別エフェクト（charge_start2 / charge_hold2 スロット）
			if (!chargeStage2Triggered_ && chargeTimer_ >= chargeStage2Time_) {
				chargeStage2Triggered_ = true;
				playerChargeLevel_ = 2.0f;
				auto* effectMgr = EffectManager::GetInstance();
				if (effectMgr) {
					const std::string startEff2 = player_->FindEffect("charge_start2");
					const std::string holdEff2  = player_->FindEffect("charge_hold2");
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
#ifdef _DEBUG
		// Debug ビルドでは、マウスが Viewport 上に無く、かつ別の ImGui ウィンドウ
		// （Save ボタン等）がマウスをキャプチャ中の時は Fire を抑制する。
		// Viewport ホバー中はそのまま通す（Viewport も ImGui ウィンドウだから WantCaptureMouse は常に true）
		{
			bool mouseOverViewport = false;
			if (auto* vp = ImGuiManager::Instance().GetViewportWindow()) {
				mouseOverViewport = vp->IsHovered();
			}
			if (!mouseOverViewport && ImGui::GetIO().WantCaptureMouse) {
				firePressed = false;
			}
		}
#endif
		// Fire 押下で発射（チャージ状態でも即発射、または連射）
		if (firePressed && fireTimer_ <= 0.0f && shootLockoutTimer_ <= 0.0f
			&& meleeActionLockTimer_ <= 0.0f && player_) {
			const Vector3 origin = player_->GetTranslate();
			// 発射方向は Lerp 前の即時 target を使う（ロックオン直後の弾が遅れて飛ぶのを防ぐ）
			Vector3 dir{ firingTarget_.x - origin.x, firingTarget_.y - origin.y, firingTarget_.z - origin.z };
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len > 1e-6f) {
				dir.x /= len; dir.y /= len; dir.z /= len;
				// ロックオン中の敵を優先、外なら画面上最近の敵に軽ホーミング
				IImGuiEditable* homeTarget = lockedEnemy_ ? lockedEnemy_ : nearestEnemy_;

				// チャージレベルで弾プレハブを選択（normal / charge1 / charge2）
				std::string bulletPrefab;
				if (playerChargeLevel_ >= 2.0f)      bulletPrefab = player_->FindBulletPrefab("charge2");
				else if (playerChargeLevel_ >= 1.0f) bulletPrefab = player_->FindBulletPrefab("charge1");
				else                                  bulletPrefab = player_->FindBulletPrefab("normal");
				if (bulletPrefab.empty()) bulletPrefab = "TemporaryPlayerBullet"; // フォールバック

				// ロック中（レティクルが敵に重なっている）なら強ホーミング、最近敵のみなら軽ホーミング
				const bool isLocked = (lockedEnemy_ != nullptr);

				// 弾プレハブの BulletParams から速度・寿命・collider拡大・ホーミングを読む
				float speed = 80.0f, lifetime = 2.0f, colliderGrowth = 0.0f, homing = 0.0f;
				if (const PrefabDef* bdef = PrefabManager::GetInstance()->Find(bulletPrefab);
					bdef && bdef->hasBullet) {
					speed          = bdef->bulletSpeed;
					lifetime       = bdef->bulletLifetime;
					colliderGrowth = bdef->bulletColliderGrowth;
					homing         = isLocked ? bdef->bulletStrongHomingStrength : bdef->bulletHomingStrength;
				}

				// 精密射撃モードの加算（precisionBlend_ で補間）。
				// 弾速は全弾に加算、ホーミング加算はロック中（強ホーミング）の弾だけに効かせる。
				if (player_) {
					const PrecisionParams& pp = player_->GetPrecisionParams();
					if (pp.enabled && precisionBlend_ > 0.0f) {
						speed += pp.speedAdd * precisionBlend_;
						if (isLocked) homing += pp.homingAdd * precisionBlend_;
					}
				}

				const float homeStrength = homeTarget ? homing : 0.0f;
				// 弾は aim plane に到達した時点で消滅させ、面より奥への乱射を防ぐ
				const int atk = (player_ && player_->HasAttackPower()) ? player_->GetAttackPower() : 0;
				SpawnPlayerBullet(origin, dir, speed, lifetime,
					colliderGrowth, homeTarget, homeStrength,
					aimPlaneDistance_, bulletPrefab, atk);

				// 連射間隔はプレイヤープレハブの ChargeParams.fireRate から
				const float fr = player_->GetChargeParams().fireRate;
				fireTimer_ = (fr > 0.0f) ? fr : 0.12f;

				// チャージ状態だった場合はリセット
				if (playerChargeLevel_ >= 0.0f) {
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
			}
		}

	}

	// ----- 近接攻撃（弱4段コンボ / 強単発）-----
	// 分身カウンターの近接派生中は World=0 のため上の gameFrozen ブロックに入れない。
	// MeleeCombo は startup/recovery タイマーや SpawnPendingMelee を駆動するので、
	// 派生中も Player dt で必ず呼ぶ必要がある。
	if (!gameFrozen || jdMeleeCameraActive_) {
		const float dtP = GetScaledDeltaTime(TimeGroup::Player);
		UpdateMeleeCombo(actions, dtP);
	}

	// 弾の進行と寿命処理（World 時間軸で動かす）
	if (!gameFrozen) {
		UpdateBullets(worldDt);
	}
	// 近接判定の追従・寿命処理。
	// 近接派生中はワールド完全停止だが、判定の寿命と当たり処理は進める必要があるので Player dt で動かす。
	{
		const float meleeDt = jdMeleeCameraActive_
			? GetScaledDeltaTime(TimeGroup::Player) : worldDt;
		if (!gameFrozen || jdMeleeCameraActive_) {
			UpdateMelees(meleeDt);
		}
	}
	// 必殺技発動中は World が止まっていても必殺技自身の進行と UI 更新は走らせる
	if (!gameFrozen || specialActive_) {

		// プレイヤー被弾処理・UI更新
		UpdatePlayerDamageAndUI(worldDt);

		// 必殺技：発動入力検出 + 発動中タイマー進行
		UpdateSpecialMove(actions, worldDt);
		// ゲージ UI のスプライトサイズ更新
		UpdateSpecialGaugeUI();

		// 無敵フラグから HP.enabled を同期（CollisionManager の auto-damage を止める）
		// scene->Update 完了後に CollisionManager::Update が走るため、ここで反映すれば次の判定に間に合う
		RefreshPlayerHpInvincibility();

		// バリア可視化プリミティブの CB 更新（位置は UpdateSpecialBarrier で毎フレ反映済み）
		if (specialBarrierVis_) {
			specialBarrierVis_->Update(camera_.get(), dxCore_->GetDeltaTime());
		}

		// 遅延削除キュー：framesLeft を減らし、0 になったら破棄
		for (auto& e : specialTrash_) {
			if (e.framesLeft > 0) --e.framesLeft;
		}
		specialTrash_.erase(
			std::remove_if(specialTrash_.begin(), specialTrash_.end(),
				[](const SpecialTrashEntry& e) { return e.framesLeft <= 0; }),
			specialTrash_.end());

		// SweepDeadEntities の前に、HP がゼロになった敵のスポーンエントリに kill t を記録
		// （SweepDeadEntities が DestroyDynamicEntity 経由で movingEnemies_ の entity を null 化する前に行う）
		{
			const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
			for (auto& m : movingEnemies_) {
				if (!m.entity) continue;
				if (m.waveEntryIndex < 0) continue;
				if (static_cast<size_t>(m.waveEntryIndex) >= killAtT_.size()) continue;
				if (killAtT_[m.waveEntryIndex] >= 0.0f) continue; // 既記録
				if (m.entity->GetHP().IsDead()) {
					killAtT_[m.waveEntryIndex] = currentT;
					// 撃破で加点（プレハブ側で設定された scoreValue を使う）
					ScoreManager::GetInstance()->AddScore(m.entity->GetScoreValue());
				}
			}
		}

		// HP がゼロになった敵などを破棄キューへ
		SweepDeadEntities();
	}

	// スポーン：カメラ進行度 t でエントリをトリガー
	if (!gameFrozen) {
		const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
			const WaveEntry& we = currentWave_.entries[i];

			// スポーントリガー
			if (i < spawnFired_.size() && !spawnFired_[i] && currentT >= we.triggerT) {
				IImGuiEditable* spawned = nullptr;
				if (!we.splineId.empty()) {
					SplineCurveActor* sp = FindDynamicSplineByName(we.splineId);
					if (sp) {
						// Rusher は終端で止まる（removeAtEnd=false）
						const bool removeAtEnd = (we.enemyType != "Rusher");
						// traverse_t [カメラt] → スプライン速度 [enemy_t/sec] へ変換
						// enemy_speed = railCameraSpeed_ / traverse_t
						const float enemySpeed = (we.traverseT > 1e-5f)
							? (railCameraSpeed_ / we.traverseT) : 0.0f;
						spawned = SpawnEnemyOnSpline(we.prefab, sp, enemySpeed,
							removeAtEnd, 0.0f, static_cast<int>(i));
					} else {
						LogBuffer::Instance().Add(
							std::string("Wave: spline not found: ") + we.splineId,
							LogBuffer::Level::Warning);
					}
				}

				// EnemyController を生成してコマンドを設定
				if (spawned) {
					auto ctrl = std::make_unique<EnemyController>();
					ctrl->entity_           = spawned;
					ctrl->waveEntryIndex_   = static_cast<int>(i);
					ctrl->billboardToPlayer_ = (we.enemyType != "Carrier");
					ctrl->triggerT_         = we.triggerT;
					ctrl->shootIntervalT_   = we.shootIntervalT;
					ctrl->spawnIntervalSec_ = we.spawnIntervalSec;
					ctrl->spawnLimit_       = we.spawnLimit;
					// 子敵は明示指定があればそれを、なければ自身のプレハブ／スプラインにフォールバック
					ctrl->childPrefab_      = we.childPrefab.empty()    ? we.prefab   : we.childPrefab;
					ctrl->childSplineId_    = we.childSplineId.empty()  ? we.splineId : we.childSplineId;
					ctrl->Init(EnemyCommandFactory::Create(we));

					// MovingEnemy にコントローラを紐付け
					for (auto& m : movingEnemies_) {
						if (m.entity == spawned) {
							m.controller       = ctrl.get();
							m.billboardToPlayer = ctrl->billboardToPlayer_;
							break;
						}
					}
					enemyControllers_.push_back(std::move(ctrl));
				}
				spawnFired_[i] = true;
			}

			// 退避トリガー
			if (i < retreatFired_.size() && i < spawnFired_.size()
				&& spawnFired_[i] && !retreatFired_[i]
				&& we.retreatT >= 0.0f && currentT >= we.retreatT) {
				// 対応するコントローラに退避を指示
				for (auto& ctrl : enemyControllers_) {
					if (ctrl && ctrl->waveEntryIndex_ == static_cast<int>(i)) {
						ctrl->TriggerRetreat();
						break;
					}
				}
				retreatFired_[i] = true;
			}
		}
	}

	// 敵コントローラ更新（自由移動・ビルボード・退避完了処理）
	if (!gameFrozen) {
		const float cameraT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		UpdateEnemyControllers(worldDt, player_, cameraT);
	}

	// スプライン追従敵の進行
	if (!gameFrozen) {
		UpdateMovingEnemies(worldDt);
	}

	// LightningRuntime テスト：Active なら毎フレ Update。始終点はテストパネル側で更新済み
	if (lightningTest_ && lightningTest_->IsActive()) {
		lightningTest_->Update(camera_.get(), dxCore_->GetDeltaTime());
	}
}

void StagePlayScene::Draw() {
	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto* commandList = dxCore_->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	if (skybox_) {
		skybox_->Draw(dxCore_);
	}

	// 通常 DSV に戻す（以降のオブジェクト描画用）
	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	// 3Dオブジェクトの共通描画設定（Object3D / Animated 描画前に必要）
	object3DManager_->DrawSetting();

	// rootParameter[3]=DirectionalLight / [5]=PointLight / [6]=SpotLight を bind
	// （Object3DInstance::Draw はライト系を bind しないため、ここで一括設定しないと GBV #935 が出る）
	LightManager::GetInstance()->BindLights(commandList);

	// BaseScene の動的エンティティ描画
	DrawDynamicObjects();
	DrawDynamicAnimated();
	DrawDynamicPrimitives();

	// LightningRuntime テスト描画
	if (lightningTest_ && lightningTest_->IsActive()) {
		lightningTest_->Draw();
	}

	// 必殺技バリア可視化（半透明球）。既定では非表示（ワイヤ球のみ）。
	if (specialBarrierFillOn_ && specialBarrierVis_) {
		specialBarrierVis_->Draw();
	}

	// 必殺技 Phase 2 ロックオン可視化（線・リング・キューブ）
	for (auto& t : specialLockonTargets_) {
		if (!t.visualsCreated) continue;
		for (int i = 0; i < 3; ++i) if (t.lines[i]) t.lines[i]->Draw();
		if (t.ring) t.ring->Draw();
		if (t.cube) t.cube->Draw();
	}

	// 必殺技 Phase 2 チャージ電撃（複数本）
	for (auto& b : specialChargeBolts_) {
		if (b.rt && b.rt->IsActive()) b.rt->Draw();
	}

	// 必殺技 Phase 3 サンダー（発射中の雷）
	for (auto& fb : specialFireBolts_) {
		if (fb.rt && fb.rt->IsActive()) fb.rt->Draw();
	}

	// 光の翼は GPU パーティクル群（special_wing_*）として DrawGlobalEffects が自動描画

	// 全シーン共通の GPUParticle + Effect Editor 描画
	DrawGlobalEffects();

	DrawSceneCameraGizmo();
	DrawDynamicAnimatedSkeletonDebug();

	// スプライト
	spriteManager_->DrawSetting();
	DrawDynamicSprites();
	if (reticle_) reticle_->Draw();

	// スコア表示（右上、控えめサイズ）
	{
		TextRenderer* tr = TextRenderer::GetInstance();
		if (tr->IsInitialized()) {
			// 上段「SCORE」ラベル + 下段に数値。ラベルと数値は独立に位置・サイズ・色を設定可。
			// 位置は画面右上 (screenW, 0) から (offsetX, offsetY) のオフセット（右端基準）。
			int rawScore = ScoreManager::GetInstance()->GetScore();
			if (rawScore < 0) rawScore = 0;
			if (rawScore > 99999) rawScore = 99999; // 5 桁で頭打ち
			char numBuf[16];
			std::snprintf(numBuf, sizeof(numBuf), "%d", rawScore);

			const char* labelStr = "SCORE";
			const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());

			const float labelW = tr->MeasureWidth(labelStr, scoreLabelScale_);
			const float numW   = tr->MeasureWidth(numBuf, scoreNumberScale_);

			tr->DrawText(labelStr,
				{ screenW - scoreLabelOffsetX_ - labelW, scoreLabelOffsetY_ },
				scoreLabelScale_, scoreLabelColor_,
				scoreLabelOutlineThickness_, scoreLabelOutlineColor_);
			tr->DrawText(numBuf,
				{ screenW - scoreNumberOffsetX_ - numW, scoreNumberOffsetY_ },
				scoreNumberScale_, scoreNumberColor_,
				scoreNumberOutlineThickness_, scoreNumberOutlineColor_);
			tr->Flush();
		}
	}

	// DebugDraw（スプライン可視化など）の出力
	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();

#ifdef _DEBUG
	// Effect Editor プレビュー RT への描画。
	// これを呼ばないと EffectEditor が開かれた瞬間に RT のバリアが整わず
	// SRV 読み出しと衝突して D3D12 GPU 検証エラーになる。
	if (auto* edit = ImGuiManager::Instance().GetEffectEditorWindow()) {
		edit->Render();

		// Scene RT に戻して以降の描画が漏れないようにする
		auto rtv = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
		auto dsv = dxCore_->GetDsvHandle();
		commandList->OMSetRenderTargets(1, &rtv, false, &dsv);
		D3D12_VIEWPORT vp{};
		vp.Width = static_cast<float>(WindowsApplication::kClientWidth);
		vp.Height = static_cast<float>(WindowsApplication::kClientHeight);
		vp.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &vp);
		D3D12_RECT sc{ 0, 0,
			static_cast<LONG>(WindowsApplication::kClientWidth),
			static_cast<LONG>(WindowsApplication::kClientHeight) };
		commandList->RSSetScissorRects(1, &sc);
	}
#endif
}

void StagePlayScene::Seek(float seconds) {
	BaseScene::Seek(seconds);

	// RailCamera の進行度を経過秒から再構築する（speed と loop を尊重）
	if (railCamera_) {
		float t = seconds * railCameraSpeed_;
		// loop = true 前提で 0..1 へ正規化
		t -= std::floor(t);
		railCamera_->SetProgress(t);
		// Seek 結果を即カメラに反映（dt=0 で Update）
		railCamera_->Update(0.0f);
		camera_->Update();
	}

	// ----- ゲーム状態を Seek 先に合わせてリセット -----
	// 現在生きている敵・弾・スプライン追従敵・敵コントローラをすべて掃除
	// （enemyControllers_ は entity_ がダングリングになるので必ずクリアする）
	enemyControllers_.clear();
	pendingEnemyControllers_.clear();
	movingEnemies_.clear();
	bullets_.clear();
	melees_.clear();
	meleeComboIndex_ = 0;
	meleeComboTimer_ = 0.0f;
	meleeStartupTimer_ = 0.0f;
	meleeActionLockTimer_ = 0.0f;
	meleePending_ = false;
	ResetDodgeState();
	for (auto& p : dynamicPrimitives_) {
		if (!p) continue;
		const EntityTag t = p->GetTag();
		if (t == EntityTag::Enemy || t == EntityTag::Boss
			|| t == EntityTag::PlayerBullet || t == EntityTag::EnemyAttack
			|| t == EntityTag::PlayerMelee) {
			deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(p.release()));
		}
	}
	dynamicPrimitives_.erase(
		std::remove_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
			[](const std::unique_ptr<PrimitiveInstance>& p) { return !p; }),
		dynamicPrimitives_.end());

	for (auto& a : dynamicAnimated_) {
		if (!a) continue;
		const EntityTag t = a->GetTag();
		if (t == EntityTag::Enemy || t == EntityTag::Boss) {
			deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(a.release()));
		}
	}
	dynamicAnimated_.erase(
		std::remove_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
			[](const std::unique_ptr<AnimatedObject3DInstance>& a) { return !a; }),
		dynamicAnimated_.end());

	// スポーン/退避フラグ / kill t を Seek 先に合わせて再構築
	const float seekT = railCamera_ ? railCamera_->GetProgress() : 0.0f;

	if (spawnFired_.size() != currentWave_.entries.size())
		spawnFired_.assign(currentWave_.entries.size(), false);
	if (retreatFired_.size() != currentWave_.entries.size())
		retreatFired_.assign(currentWave_.entries.size(), false);
	if (killAtT_.size() != currentWave_.entries.size())
		killAtT_.assign(currentWave_.entries.size(), -1.0f);

	// seek 先より後の kill は未発生扱いに戻す
	for (size_t i = 0; i < killAtT_.size(); ++i) {
		if (killAtT_[i] > seekT) killAtT_[i] = -1.0f;
	}

	// スコアは Seek では再構築しない（開発時のみ Seek を使う想定。
	// 厳密にやるなら killAtT_ ごとに wave→prefab→scoreValue を引く必要があり実装コスト高）
	ScoreManager::GetInstance()->Reset();

	// フラグを seekT から再構築
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		spawnFired_[i]  = (seekT >= we.triggerT);
		retreatFired_[i] = (we.retreatT >= 0.0f && seekT >= we.retreatT);
	}

	// 生存中の敵をスプライン上の正しい位置で復元
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		if (!spawnFired_[i]) continue;
		if (retreatFired_[i]) continue;
		if (killAtT_[i] >= 0.0f) continue;
		if (we.splineId.empty()) continue;

		// camera t 進行量 / traverse_t = スプライン上の t
		if (we.traverseT < 1e-5f) continue;
		const float tOnSpline = std::clamp((seekT - we.triggerT) / we.traverseT, 0.0f, 1.0f);
		if (tOnSpline >= 1.0f) continue;

		SplineCurveActor* sp = FindDynamicSplineByName(we.splineId);
		if (!sp) continue;
		const bool removeAtEnd = (we.enemyType != "Rusher");
		const float enemySpeed = railCameraSpeed_ / we.traverseT;
		IImGuiEditable* spawned = SpawnEnemyOnSpline(
			we.prefab, sp, enemySpeed, removeAtEnd, tOnSpline, static_cast<int>(i));
		if (!spawned) continue;

		// Seek 復元された敵にも EnemyController を作って AI を再開させる
		auto ctrl = std::make_unique<EnemyController>();
		ctrl->entity_           = spawned;
		ctrl->waveEntryIndex_   = static_cast<int>(i);
		ctrl->billboardToPlayer_ = (we.enemyType != "Carrier");
		ctrl->triggerT_         = we.triggerT;
		ctrl->shootIntervalT_   = we.shootIntervalT;
		ctrl->spawnIntervalSec_ = we.spawnIntervalSec;
		ctrl->spawnLimit_       = we.spawnLimit;
		ctrl->childPrefab_      = we.childPrefab.empty()   ? we.prefab   : we.childPrefab;
		ctrl->childSplineId_    = we.childSplineId.empty() ? we.splineId : we.childSplineId;
		ctrl->Init(EnemyCommandFactory::Create(we));

		for (auto& m : movingEnemies_) {
			if (m.entity == spawned) {
				m.controller       = ctrl.get();
				m.billboardToPlayer = ctrl->billboardToPlayer_;
				break;
			}
		}
		enemyControllers_.push_back(std::move(ctrl));
	}
}

Camera* StagePlayScene::GetCamera() {
	return camera_.get();
}

float StagePlayScene::GetCameraProgressT() const {
	return railCamera_ ? railCamera_->GetProgress() : -1.0f;
}

// =====================================================================
// シーン保存 / 読込（DemoScene の実装を流用）
// =====================================================================
namespace {
	JsonValue Vec3ToJson(const Vector3& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		return arr;
	}
	Vector3 JsonToVec3(const JsonValue& v, const Vector3& fallback = {}) {
		if (!v.IsArray() || v.Size() < 3) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z))
		};
	}
	JsonValue TransformToJson(const Vector3& s, const Vector3& r, const Vector3& t) {
		JsonValue obj = JsonValue::MakeObject();
		obj["scale"] = Vec3ToJson(s);
		obj["rotate"] = Vec3ToJson(r);
		obj["translate"] = Vec3ToJson(t);
		return obj;
	}
}

bool StagePlayScene::SaveSceneToJson(const std::string& filePath) {
	JsonValue root = JsonValue::MakeObject();
	root["scene"] = "StagePlayScene";

	JsonValue arr = JsonValue::MakeArray();

	for (const auto& o : object3DInstances_) {
		if (!o) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Object3D";
		e["name"] = o->GetName();
		e["tag"] = std::string(GetTagName(o->GetTag()));
		e["dir"] = o->GetDirectoryPath();
		e["file"] = o->GetModelFileName();
		e["transform"] = TransformToJson(o->GetScale(), o->GetRotate(), o->GetTranslate());
		arr.Push(std::move(e));
	}
	for (const auto& a : dynamicAnimated_) {
		if (!a) continue;
		// プレイヤーは常にプレハブから復元するので保存しない（collider 等のプレハブ属性を確実に反映するため）
		if (a->GetTag() == EntityTag::Player) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "AnimatedObject3D";
		e["name"] = a->GetName();
		e["tag"] = std::string(GetTagName(a->GetTag()));
		e["dir"] = a->GetDirectoryPath();
		e["file"] = a->GetModelFileName();
		e["transform"] = TransformToJson(a->GetScale(), a->GetRotate(), a->GetTranslate());
		arr.Push(std::move(e));
	}
	for (const auto& p : dynamicPrimitives_) {
		if (!p) continue;
		// 弾は一時オブジェクトなのでシーン保存に含めない
		const EntityTag t = p->GetTag();
		if (t == EntityTag::PlayerBullet || t == EntityTag::EnemyAttack) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Primitive";
		e["name"] = p->GetName();
		e["tag"] = std::string(GetTagName(t));
		e["primitiveType"] = static_cast<int64_t>(p->GetPrimitiveType());
		const Transform& tr = p->GetMesh().GetTransform();
		e["transform"] = TransformToJson(tr.scale, tr.rotate, tr.translate);
		if (!p->GetTextureFilePath().empty()) {
			e["texture"] = p->GetTextureFilePath();
		}
		arr.Push(std::move(e));
	}
	for (const auto& s : dynamicSprites_) {
		if (!s) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Sprite";
		e["name"] = s->GetName();
		e["tag"] = std::string(GetTagName(s->GetTag()));
		e["texture"] = s->GetTextureFilePath();
		const Vector2& pos = s->GetPosition();
		JsonValue p = JsonValue::MakeArray();
		p.Push(JsonValue(static_cast<double>(pos.x)));
		p.Push(JsonValue(static_cast<double>(pos.y)));
		e["pos"] = std::move(p);
		arr.Push(std::move(e));
	}
	for (const auto& sp : dynamicSplines_) {
		if (!sp) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Spline";
		e["name"] = sp->GetName();
		e["tag"] = std::string(GetTagName(sp->GetTag()));
		JsonValue ptsArr = JsonValue::MakeArray();
		for (const auto& pt : sp->GetPoints()) {
			ptsArr.Push(Vec3ToJson(pt));
		}
		e["points"] = std::move(ptsArr);
		arr.Push(std::move(e));
	}

	root["objects"] = std::move(arr);

	std::filesystem::path path(filePath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}

	bool ok = JsonWriter::WriteFile(filePath, root, { true, 2 });
	LogBuffer::Instance().Add(
		ok ? ("Scene saved: " + filePath)
		   : ("Scene save FAILED: " + filePath),
		ok ? LogBuffer::Level::Info : LogBuffer::Level::Error);
	return ok;
}

bool StagePlayScene::LoadSceneFromJson(const std::string& filePath) {
	auto result = JsonParser::ParseFile(filePath);
	if (!result.success) {
		LogBuffer::Instance().Add(
			"Scene load FAILED: " + filePath + " (" + result.errorMessage + ")",
			LogBuffer::Level::Error);
		return false;
	}

	// 既存の動的オブジェクトを全削除
	for (auto& o : object3DInstances_) {
		if (o) deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(o.release()));
	}
	object3DInstances_.clear();
	for (auto& a : dynamicAnimated_) {
		if (a) deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(a.release()));
	}
	dynamicAnimated_.clear();
	for (auto& m : dynamicAnimatedModels_) {
		if (m) deferredDeletes_.emplace_back(std::shared_ptr<AnimatedModelInstance>(m.release()));
	}
	dynamicAnimatedModels_.clear();
	for (auto& s : dynamicSprites_) {
		if (s) deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(s.release()));
	}
	dynamicSprites_.clear();
	for (auto& p : dynamicPrimitives_) {
		if (p) deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(p.release()));
	}
	dynamicPrimitives_.clear();
	for (auto& sp : dynamicSplines_) {
		if (sp) deferredDeletes_.emplace_back(std::shared_ptr<SplineCurveActor>(sp.release()));
	}
	dynamicSplines_.clear();

	// LoadScene で全エンティティが消えたので参照ポインタもリセット
	player_ = nullptr;
	movingEnemies_.clear();
	bullets_.clear();
	melees_.clear();
	ResetDodgeState();

	const JsonValue& objs = result.value["objects"];
	if (!objs.IsArray()) return true;

	for (size_t i = 0; i < objs.Size(); ++i) {
		const JsonValue& e = objs[i];
		std::string type = e["type"].AsString();
		std::string name = e["name"].AsString();
		EntityTag tag = TagFromName(e["tag"].AsString());
		Vector3 sc = JsonToVec3(e["transform"]["scale"], { 1,1,1 });
		Vector3 ro = JsonToVec3(e["transform"]["rotate"], { 0,0,0 });
		Vector3 tr = JsonToVec3(e["transform"]["translate"], { 0,0,0 });

		if (type == "Object3D") {
			AddDynamicObject(e["dir"].AsString(), e["file"].AsString(), tr);
			if (!object3DInstances_.empty()) {
				auto& back = object3DInstances_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
			}
		} else if (type == "AnimatedObject3D") {
			AddDynamicAnimated(e["dir"].AsString(), e["file"].AsString(), tr);
			if (!dynamicAnimated_.empty()) {
				auto& back = dynamicAnimated_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
				if (tag == EntityTag::Player) player_ = back.get();
			}
		} else if (type == "Primitive") {
			// 弾は一時オブジェクトなのでロードでも無視（過去の汚れた save 対策）
			if (tag == EntityTag::PlayerBullet || tag == EntityTag::EnemyAttack) {
				continue;
			}
			int primType = static_cast<int>(e["primitiveType"].AsInt());
			AddDynamicPrimitive(primType, tr);
			if (!dynamicPrimitives_.empty()) {
				auto& back = dynamicPrimitives_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
				std::string tex = e["texture"].AsString();
				if (!tex.empty()) back->SetTexture(tex);
			}
		} else if (type == "Sprite") {
			float cx = static_cast<float>(e["pos"][0].AsDouble(0.0));
			float cy = static_cast<float>(e["pos"][1].AsDouble(0.0));
			AddDynamicSprite(e["texture"].AsString(), cx, cy);
			if (!dynamicSprites_.empty()) {
				auto& back = dynamicSprites_.back();
				back->SetName(name);
				back->SetTag(tag);
			}
		} else if (type == "Spline") {
			AddDynamicSpline(static_cast<int>(tag));
			if (!dynamicSplines_.empty()) {
				auto& back = dynamicSplines_.back();
				back->SetName(name);
				back->MutablePoints().clear();
				const JsonValue& pts = e["points"];
				if (pts.IsArray()) {
					for (size_t pi = 0; pi < pts.Size(); ++pi) {
						back->AddPoint(JsonToVec3(pts[pi]));
					}
				}
			}
		}
	}

	LogBuffer::Instance().Add("Scene loaded: " + filePath, LogBuffer::Level::Info);
	return true;
}

void StagePlayScene::InitializeHPBarUI() {
	hpBarBackground_ = nullptr;
	hpBarForeground_ = nullptr;

	if (!spriteManager_) return;

	const Vector2 hpBarPos{ hpBarPosX_, hpBarPosY_ };

	// 赤ゲージ背景（補間で遅延追従）
	auto bgSprite = std::make_unique<SpriteInstance>();
	bgSprite->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "HPBarBackground");
	bgSprite->SetPosition(hpBarPos);
	bgSprite->SetSize({ hpBarMaxWidth_, hpBarHeight_ });
	bgSprite->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
	hpBarBackground_ = bgSprite.get();
	dynamicSprites_.push_back(std::move(bgSprite));

	// 緑ゲージ前景（現在HP に即時追従）
	auto fgSprite = std::make_unique<SpriteInstance>();
	fgSprite->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "HPBarForeground");
	fgSprite->SetPosition(hpBarPos);
	fgSprite->SetSize({ hpBarMaxWidth_, hpBarHeight_ });
	fgSprite->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f });
	hpBarForeground_ = fgSprite.get();
	dynamicSprites_.push_back(std::move(fgSprite));

	hpBarCurrentRatio_ = 1.0f;
	hpBarTargetRatio_ = 1.0f;

	// プレイヤー HP を有効化（プレハブで enabled=false の場合に備えて）
	if (player_) {
		player_->GetHP().enabled = true;
	}
}

void StagePlayScene::UpdatePlayerDamageAndUI(float deltaTime) {
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
	const bool specialInv = specialActive_;                                      // 必殺技発動中：完全無敵
	const bool invincible = justInv || dodgeInv || dmgInv || specialInv;

	const float blinkFreq = damageBlinkFrequency_;
	if (justInv) {
		// ジャスト回避中は無敵だが点滅させない（ソリッド表示）
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	} else if (dodgeInv) {
		// 回避（〜ジャスト窓〜回避無敵）：水色／白の点滅
		float phase = std::fmod(dodgeTimer_ * blinkFreq, 1.0f);
		if (phase < 0.5f) player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, damageBlinkAlpha_ });
		else              player_->SetMaterialColor({ 0.4f, 0.8f, 1.0f, damageBlinkAlpha_ });
	} else if (dmgInv) {
		// 被弾無敵：赤／白の点滅
		float phase = std::fmod(playerInvincibilityTimer_ * blinkFreq, 1.0f);
		if (phase < 0.5f) player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, damageBlinkAlpha_ });
		else              player_->SetMaterialColor({ 1.0f, 0.3f, 0.3f, damageBlinkAlpha_ });
	} else if (wasInvincible_) {
		// 無敵終了直後に通常色へ戻す（1度だけ）
		player_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	wasInvincible_ = invincible;

	// ----- 敵攻撃との接触判定（敵弾＋突進）-----
	// ジャスト回避中は完全スルー。それ以外は、回避ジャスト窓内の接触＝ジャスト回避成立、
	// 各種無敵中＝ダメージ無効、いずれでもなければ通常被弾。
	if (!justInv) {
		const Vector3 playerPos = player_->GetTranslate();
		const float playerR = player_->GetCollider().radius;

		bool            hitFound = false;
		int             incomingDamage = 0;
		int             hitBulletIndex = -1;
		IImGuiEditable* attacker = nullptr; // ジャスト演出のハイライト対象

		// 敵弾
		for (size_t i = 0; i < bullets_.size(); ++i) {
			auto& b = bullets_[i];
			if (!b.primitive) continue;
			if (b.primitive->GetTag() != EntityTag::EnemyAttack) continue;
			const Vector3* bp = b.primitive->GetEditableTranslate();
			if (!bp) continue;
			const float bulletR = b.primitive->GetCollider().radius;
			float dx = playerPos.x - bp->x, dy = playerPos.y - bp->y, dz = playerPos.z - bp->z;
			float sumR = playerR + bulletR;
			if (dx * dx + dy * dy + dz * dz < sumR * sumR) {
				incomingDamage = b.primitive->GetDamageDealer().damage;
				if (incomingDamage <= 0) incomingDamage = 10;
				hitBulletIndex = static_cast<int>(i);
				attacker = nearestEnemy_; // 弾の発射元は不明なので画面上最近の敵を演出対象に
				hitFound = true;
				break;
			}
		}

		// 突進など「攻撃接触中」の敵（弾が当たっていない場合のみ。ただの移動接触は contactDamageActive_=false で除外）
		if (!hitFound) {
			for (auto& ctrl : enemyControllers_) {
				if (!ctrl || !ctrl->contactDamageActive_ || !ctrl->entity_) continue;
				IImGuiEditable* e = ctrl->entity_;
				const Vector3* ep = e->GetEditableTranslate();
				if (!ep) continue;
				const float er = e->GetCollider().radius;
				float dx = playerPos.x - ep->x, dy = playerPos.y - ep->y, dz = playerPos.z - ep->z;
				float sumR = playerR + er;
				if (dx * dx + dy * dy + dz * dz < sumR * sumR) {
					incomingDamage = e->GetDamageDealer().damage;
					if (incomingDamage <= 0) incomingDamage = 10;
					attacker = e;
					hitFound = true;
					break;
				}
			}
		}

		if (hitFound) {
			if (dodgeActive_ && dodgeTimer_ <= dodgeJustWindow_) {
				// ジャスト回避成立（スロー＋演出＋回復ストック＋スコア）
				TriggerJustDodge(attacker);
				dodgeActive_ = false; // 回避無敵を消費し、以降はジャスト無敵へ移行
				if (hitBulletIndex >= 0) bullets_[hitBulletIndex].remainingLifetime = -1.0f;
			} else if (dodgeInv || dmgInv || specialInv) {
				// 無敵中：ダメージ無効（弾はそのまま通過＝既存の被弾無敵と同様）
			} else {
				// 通常被弾
				OnPlayerTakeDamage(incomingDamage);
				if (hitBulletIndex >= 0) bullets_[hitBulletIndex].remainingLifetime = -1.0f;
			}
		}
	}

	UpdateHPBarUI();

	// HP=0 でゲームオーバー（リトライ：シーン再ロード）
	if (player_->GetHP().IsDead() && !gameOverTriggered_) {
		gameOverTriggered_ = true;
		SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);
	}
}

void StagePlayScene::OnPlayerTakeDamage(int damageAmount) {
	if (!player_) return;

	player_->GetHP().TakeDamage(damageAmount);

	playerInvincibilityTimer_ = playerInvincibilityDuration_;
	shootLockoutTimer_ = shootLockoutDuration_;

	if (camera_) {
		camera_->Shake(damageCameraShakeIntensity_, damageCameraShakeDuration_);
	}
}

void StagePlayScene::UpdateHPBarUI() {
	if (!player_ || !hpBarForeground_ || !hpBarBackground_) return;

	const HP& hp = player_->GetHP();
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
	if (!spriteManager_) return;

	const Vector2 pos{ gaugeBarPosX_, gaugeBarPosY_ };

	// 背景（暗色フル幅）
	auto bg = std::make_unique<SpriteInstance>();
	bg->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "SpecialGaugeBackground");
	bg->SetPosition(pos);
	bg->SetSize({ gaugeBarMaxWidth_, gaugeBarHeight_ });
	bg->SetColor(gaugeBarBgColor_);
	gaugeBarBackground_ = bg.get();
	dynamicSprites_.push_back(std::move(bg));

	// 前景（現在値）
	auto fg = std::make_unique<SpriteInstance>();
	fg->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "SpecialGaugeForeground");
	fg->SetPosition(pos);
	fg->SetSize({ 0.0f, gaugeBarHeight_ });
	fg->SetColor(gaugeBarFgColor_);
	gaugeBarForeground_ = fg.get();
	dynamicSprites_.push_back(std::move(fg));
}

void StagePlayScene::UpdateSpecialGaugeUI() {
	if (!gaugeBarBackground_ || !gaugeBarForeground_) return;

	// 位置（ImGuiでチューニングされた値を毎フレ反映）
	gaugeBarBackground_->SetPosition({ gaugeBarPosX_, gaugeBarPosY_ });
	gaugeBarForeground_->SetPosition({ gaugeBarPosX_, gaugeBarPosY_ });
	gaugeBarBackground_->SetSize({ gaugeBarMaxWidth_, gaugeBarHeight_ });
	gaugeBarBackground_->SetColor(gaugeBarBgColor_);

	// 比率
	float ratio = (specialGaugeMax_ > 0.0001f) ? specialGauge_ / specialGaugeMax_ : 0.0f;
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
	if (!actions) return;

	if (specialActive_) {
		// 発動中：総タイマーとフェーズ内タイマーを進める
		specialTimer_      += dt;
		specialPhaseTimer_ += dt;

		// バリア球の追従＋当たり判定は全フェーズ共通（Fire 中もカメラ前進にズレず追従させる）
		UpdateSpecialBarrier();
		// 光の翼（放射状ストリーク）も全フェーズ共通で流す
		UpdateSpecialWings();

		switch (specialPhase_) {
		case SpecialPhase::Barrier:
			UpdateSpecialPhaseBarrier(dt);
			break;
		case SpecialPhase::Lockon:
			// Lockon フェーズは実時間で進行（World は止まっているため）
			UpdateSpecialPhaseLockon(dxCore_->GetDeltaTime());
			break;
		case SpecialPhase::Fire:
			// Fire は World 再開済み。dt（worldDt）で進行させ敵移動と同期させる
			UpdateSpecialPhaseFire(dt);
			break;
		case SpecialPhase::End:
			UpdateSpecialPhaseEnd(dt);
			break;
		default: break;
		}
		return;
	}

	// 発動待ち：ゲージMAX & 行動可能 & Ultimate トリガで発動
	if (specialGauge_ < specialGaugeMax_) return;
	if (IsActionLocked()) return;
	if (justDodgeActive_) return;

	if (actions->IsTriggered(static_cast<int>(Action::Ultimate))) {
		TriggerSpecialMove();
	}
}

void StagePlayScene::TriggerSpecialMove() {
	specialActive_      = true;
	specialTimer_       = 0.0f;
	specialPhaseTimer_  = 0.0f;
	specialGauge_       = 0.0f;
	// HP.enabled の同期は RefreshPlayerHpInvincibility に一元化

	// Phase 1: バリア展開へ
	EnterSpecialPhaseBarrier();
}

void StagePlayScene::EnterSpecialPhaseBarrier() {
	specialPhase_      = SpecialPhase::Barrier;
	specialPhaseTimer_ = 0.0f;

	// 現在のプレイヤー入力オフセット・速度を保存（終了時に滑らかに戻す用途は EndSpecialMove で）
	specialPlayerInputOffsetSaved_ = playerInputOffset_;
	specialPlayerVelocitySaved_    = playerVelocity_;
	// 強制中はゼロ固定（入力ロックされるので外部からの変動はない想定）
	playerInputOffset_ = { 0.0f, 0.0f };
	playerVelocity_    = { 0.0f, 0.0f };

	// バリア可視化プリミティブ（半透明青の球、Blend=Normal、両面、深度書き込みなし）
	specialBarrierVis_ = std::make_unique<EffectPrimitiveRenderer>();
	specialBarrierVis_->Initialize(2 /*Sphere*/, "Resources/Textures/white1x1.dds");
	specialBarrierVis_->SetBlendMode(PrimitivePipeline::kBlendModeNormal);
	specialBarrierVis_->SetDepthWrite(false);
	specialBarrierVis_->SetCullBackface(false);
	specialBarrierVis_->SetColor(specialBarrierColor_);
	const float diameter = specialBarrierRadius_ * 2.0f; // CreateSphere は radius=0.5 既定なので
	specialBarrierVis_->SetScale({ diameter, diameter, diameter });

	// バリアのパーティクル群を用意（球状エミッタ）。時間停止中も動くよう TimeGroup=Player。
	if (auto* gpu = Game::GetGPUParticleManager()) {
		if (!gpu->HasGroup("special_barrier")) {
			gpu->CreateGroup("special_barrier", "Resources/Textures/circle.dds");
		}
		gpu->SetGroupBillboardMode("special_barrier", BillboardMode::Full);
		gpu->SetGroupTimeGroup("special_barrier", TimeGroup::Player);
		specialBarrierGroupReady_ = true;
	}
	specialBarrierEmitAccum_ = 0.0f;
	specialBarrierWireSpin_  = { 0.0f, 0.0f, 0.0f };
}

void StagePlayScene::UpdateSpecialBarrier() {
	if (!player_) return;

	// バリア球の中心 = プレイヤー体の中心（足元原点 + offset）。毎フレ追従。
	// Fire 中はレールカメラが進行してプレイヤーのワールド位置も前進するため、
	// 全フェーズ共通でここで追従させないと球と当たり判定が置いて行かれる。
	const Vector3 playerPos = SpecialPlayerCenter();
	if (specialBarrierVis_) {
		specialBarrierVis_->SetTranslate(playerPos);
	}

	// バリア当たり判定：球内に侵入した EnemyAttack 弾を消す（距離判定）
	const float r = specialBarrierRadius_;
	const float r2 = r * r;
	for (auto& b : bullets_) {
		if (!b.primitive) continue;
		if (b.primitive->GetTag() != EntityTag::EnemyAttack) continue;
		const Vector3* bp = b.primitive->GetEditableTranslate();
		if (!bp) continue;
		float dx = bp->x - playerPos.x;
		float dy = bp->y - playerPos.y;
		float dz = bp->z - playerPos.z;
		if (dx*dx + dy*dy + dz*dz <= r2) {
			b.remainingLifetime = -1.0f; // 消滅
			// 波紋エフェクトはエディター機能準備後に追加
		}
	}

	// 実時間 dt（時間停止中もバリア演出は等速で進める）
	const float rdt = dxCore_ ? dxCore_->GetDeltaTime() : (1.0f / 60.0f);

	// 回転ワイヤーフレーム球（バリアの正確な範囲を可視化）。X/Y/Z 3軸で回す。
	specialBarrierWireSpin_.x += specialBarrierWireSpinSpeed_.x * rdt;
	specialBarrierWireSpin_.y += specialBarrierWireSpinSpeed_.y * rdt;
	specialBarrierWireSpin_.z += specialBarrierWireSpinSpeed_.z * rdt;
	if (specialBarrierWireframeOn_) {
		DrawSpecialBarrierWireframe(playerPos);
	}

	// バリアのパーティクル：球面内に連続バースト（実時間で進めるので時間停止中もシマー）
	if (specialBarrierParticleOn_ && specialBarrierGroupReady_) {
		if (auto* gpu = Game::GetGPUParticleManager()) {
			const float interval = (specialBarrierEmitInterval_ > 1e-4f) ? specialBarrierEmitInterval_ : 0.02f;
			const float emitRadius = specialBarrierRadius_ * specialBarrierParticleRadiusScale_;
			const uint32_t count = static_cast<uint32_t>((std::max)(1, specialBarrierEmitCount_));
			gpu->SetEmitterTranslate("special_barrier", playerPos);
			specialBarrierEmitAccum_ += rdt;
			int guard = 0; // 1フレームに出し過ぎない安全弁
			while (specialBarrierEmitAccum_ >= interval && guard < 8) {
				specialBarrierEmitAccum_ -= interval;
				++guard;
				gpu->BurstEmit("special_barrier", playerPos, count, emitRadius,
					1 /*Fixed*/, specialBarrierParticleColor0_, specialBarrierParticleColor1_,
					specialBarrierParticleScaleMin_, specialBarrierParticleScaleMax_, true,
					specialBarrierParticleLife_);
			}
		}
	}
}

void StagePlayScene::DrawSpecialBarrierWireframe(const Vector3& center) {
	auto* lr = LineRenderer::GetInstance();
	if (!lr) return;

	const float kPi = 3.14159265358979323846f;
	const float R   = specialBarrierRadius_;
	const int meridians = (std::max)(1, specialBarrierWireMeridians_);
	const int parallels = (std::max)(1, specialBarrierWireParallels_);
	const int seg       = (std::max)(3, specialBarrierWireSegments_);

	// 3軸回転行列。ローカル球面点に適用してから center に足す。
	const Matrix4x4 rot = MakeRotateMatrix(specialBarrierWireSpin_);
	auto toWorld = [&](float lx, float ly, float lz) -> Vector3 {
		return {
			center.x + (lx * rot.m[0][0] + ly * rot.m[1][0] + lz * rot.m[2][0]),
			center.y + (lx * rot.m[0][1] + ly * rot.m[1][1] + lz * rot.m[2][1]),
			center.z + (lx * rot.m[0][2] + ly * rot.m[1][2] + lz * rot.m[2][2]),
		};
	};

	// 経線（Y軸を通る大円）。色=金。
	for (int m = 0; m < meridians; ++m) {
		const float phi = static_cast<float>(m) / meridians * kPi;
		const float cphi = std::cos(phi), sphi = std::sin(phi);
		Vector3 prev{};
		for (int i = 0; i <= seg; ++i) {
			const float t = static_cast<float>(i) / seg * 2.0f * kPi;
			const float st = std::sin(t), ct = std::cos(t);
			const Vector3 p = toWorld(R * (st * cphi), R * ct, R * (st * sphi));
			if (i > 0) lr->AddLine(prev, p, specialBarrierWireColorGold_);
			prev = p;
		}
	}

	// 緯線（高さ固定の水平円）。色=ピンク。
	for (int k = 1; k <= parallels; ++k) {
		const float lat = static_cast<float>(k) / (parallels + 1) * kPi; // (0,π) の内側
		const float y = std::cos(lat);
		const float rxz = std::sin(lat);
		Vector3 prev{};
		for (int i = 0; i <= seg; ++i) {
			const float a = static_cast<float>(i) / seg * 2.0f * kPi;
			const Vector3 p = toWorld(R * (rxz * std::cos(a)), R * y, R * (rxz * std::sin(a)));
			if (i > 0) lr->AddLine(prev, p, specialBarrierWireColorPink_);
			prev = p;
		}
	}
}

void StagePlayScene::UpdateSpecialWings() {
	if (!specialWingOn_) return;
	auto* gpu = Game::GetGPUParticleManager();
	if (!gpu) return;

	const int arms = (std::max)(1, specialWingArmCount_);
	const float kPi = 3.14159265358979323846f;
	const Vector3 center = SpecialPlayerCenter();
	// X字方向はカメラ平面（画面）基準。camRight/camUp で画面内の角度を作る。
	const Matrix4x4 camRot = MakeRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
	const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };

	for (int i = 0; i < arms; ++i) {
		const std::string name = "special_wing_" + std::to_string(i);
		if (!gpu->HasGroup(name)) {
			gpu->CreateGroup(name, "Resources/Textures/circle.dds");
			gpu->SetGroupBillboardMode(name, BillboardMode::Full);
			gpu->SetGroupTimeGroup(name, TimeGroup::Player);
		}
		// アーム方向（画面内で均等配置。offset=π/4・arms=4 で X 字）
		const float a = specialWingAngleOffset_ + static_cast<float>(i) / arms * 2.0f * kPi;
		const Vector3 dir = {
			std::cos(a) * camRight.x + std::sin(a) * camUp.x,
			std::cos(a) * camRight.y + std::sin(a) * camUp.y,
			std::cos(a) * camRight.z + std::sin(a) * camUp.z,
		};
		const Vector3 vel = { dir.x * specialWingSpeed_, dir.y * specialWingSpeed_, dir.z * specialWingSpeed_ };
		gpu->SetEmitterVelocity(name, vel, specialWingJitter_, 1); // 外向き初速（方向固定）
		gpu->BurstEmit(name, center,
			static_cast<uint32_t>((std::max)(1, specialWingBurstCount_)), specialWingEmitRadius_,
			1 /*Fixed: 金→ピンク*/, specialWingColorInner_, specialWingColorOuter_,
			specialWingScaleMin_, specialWingScaleMax_, true, specialWingLife_);
	}
}

void StagePlayScene::UpdateSpecialPhaseBarrier(float worldDt) {
	(void)worldDt;
	// 追従・当たり判定は UpdateSpecialBarrier（全フェーズ共通）で実施。
	// ここは Phase 1 のフェーズ遷移判定のみ。
	// 3 秒経過で Phase 2（Lockon）へ遷移
	if (specialPhaseTimer_ >= specialBarrierDuration_) {
		EnterSpecialPhaseLockon();
	}
}

void StagePlayScene::EnterSpecialPhaseLockon() {
	specialPhase_ = SpecialPhase::Lockon;
	specialPhaseTimer_ = 0.0f;
	specialPhase2Timer_ = 0.0f;
	specialAllLockonComplete_ = false;
	specialChargeStartTime_ = 0.0f;

	// ワールド時間停止（A案：既存のジャスト回避と同じ仕組み）
	SetTimeScale(TimeGroup::World, 0.0f);

	// ロックオン対象列挙
	specialLockonTargets_.clear();
	EnumerateLockonTargets();
}

void StagePlayScene::EnumerateLockonTargets() {
	if (!camera_ || !player_) return;

	const Vector3 playerPos = player_->GetTranslate();
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();

	auto isVisibleInClip = [&](const Vector3& worldPos) {
		Vector4 clip = {
			worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] + worldPos.z * vp.m[2][0] + vp.m[3][0],
			worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] + worldPos.z * vp.m[2][1] + vp.m[3][1],
			worldPos.x * vp.m[0][2] + worldPos.y * vp.m[1][2] + worldPos.z * vp.m[2][2] + vp.m[3][2],
			worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] + worldPos.z * vp.m[2][3] + vp.m[3][3],
		};
		// カメラの前方にあるか（clip.w > 0）と XY 画面内にあるかだけで判定。
		// 奥行きは far plane (デフォ 100) を超えると NDC Z > 1 になるが、レールSTG では
		// 弾や敵が far plane 近くを飛ぶことが多いので、奥行きフィルタは敢えてかけない。
		if (clip.w <= 0.0f) return false;
		const float ndx = clip.x / clip.w;
		const float ndy = clip.y / clip.w;
		const float margin = 0.0f;
		return std::abs(ndx) <= 1.0f + margin && std::abs(ndy) <= 1.0f + margin;
	};

	auto sqDistToPlayer = [&](const Vector3& p) {
		float dx = p.x - playerPos.x, dy = p.y - playerPos.y, dz = p.z - playerPos.z;
		return dx * dx + dy * dy + dz * dz;
	};

	struct Candidate {
		IImGuiEditable* entity;
		int bulletIndex;
		float dist2;
		float radius;
	};
	std::vector<Candidate> cands;

	// 敵弾（EnemyAttack）
	for (size_t i = 0; i < bullets_.size(); ++i) {
		auto& b = bullets_[i];
		if (!b.primitive) continue;
		if (b.primitive->GetTag() != EntityTag::EnemyAttack) continue;
		const Vector3* p = b.primitive->GetEditableTranslate();
		if (!p) continue;
		if (!isVisibleInClip(*p)) continue;
		Candidate c;
		c.entity = b.primitive;
		c.bulletIndex = static_cast<int>(i);
		c.dist2 = sqDistToPlayer(*p);
		c.radius = b.primitive->GetCollider().radius;
		cands.push_back(c);
	}

	// 敵本体：dynamicPrimitives_ / object3DInstances_ / dynamicAnimated_ の中から
	// Enemy / Boss タグを持つものを直接拾う。HP の有無は問わない。
	// （EnemyController や movingEnemies_ に登録されていない敵もカバーできるよう、
	//   entity の格納コンテナを直接スキャンするのがもっとも漏れがない）
	std::unordered_set<IImGuiEditable*> enemySeen;
	auto pushEnemy = [&](IImGuiEditable* e) {
		if (!e) return;
		if (!enemySeen.insert(e).second) return; // 重複
		const EntityTag tag = e->GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Vector3* p = e->GetEditableTranslate();
		if (!p) return;
		if (!isVisibleInClip(*p)) return;
		Candidate c;
		c.entity = e;
		c.bulletIndex = -1;
		c.dist2 = sqDistToPlayer(*p);
		c.radius = e->GetCollider().radius;
		cands.push_back(c);
	};
	for (auto& up : dynamicPrimitives_)  { pushEnemy(up.get()); }
	for (auto& up : object3DInstances_)  { pushEnemy(up.get()); }
	for (auto& up : dynamicAnimated_)    { pushEnemy(up.get()); }

	// プレイヤーから近い順にソート
	std::sort(cands.begin(), cands.end(),
		[](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });

	specialLockonTargets_.reserve(cands.size());
	for (size_t i = 0; i < cands.size(); ++i) {
		SpecialLockonTarget t;
		t.entity = cands[i].entity;
		t.bulletIndex = cands[i].bulletIndex;
		t.patternIndex = static_cast<int>(i % 2); // 交互に A/B
		t.startTime = static_cast<float>(i) * specialLockonInterval_;
		t.radius = (cands[i].radius > 0.01f) ? cands[i].radius : 0.5f;
		specialLockonTargets_.push_back(std::move(t));
	}
}

void StagePlayScene::SpawnLockonVisuals(SpecialLockonTarget& t) {
	if (t.visualsCreated) return;

	// 3本の線（薄い Plane、Full Billboard、Blend=Add で発光感）
	for (int i = 0; i < 3; ++i) {
		t.lines[i] = std::make_unique<EffectPrimitiveRenderer>();
		t.lines[i]->Initialize(0 /*Plane*/, "Resources/Textures/white1x1.dds");
		t.lines[i]->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
		t.lines[i]->SetBillboardMode(BillboardMode::Full);
		t.lines[i]->SetDepthWrite(false);
		t.lines[i]->SetCullBackface(false);
		t.lines[i]->SetColor(specialLockonColor_);
		t.lines[i]->SetScale({ 0.0f, 0.0f, 1.0f }); // 初期は非表示
	}

	// リング（Y軸ビルボードはNG＝Fullで正面）
	{
		PrimitiveGenerator::RingParams rp{};
		rp.outerRadius = 1.0f;
		rp.innerRadius = 1.0f - specialLockonRingThickness_;
		rp.divisions = 48;
		rp.innerColor = specialLockonColor_;
		rp.outerColor = specialLockonColor_;
		t.ring = std::make_unique<EffectPrimitiveRenderer>();
		t.ring->Initialize(3 /*Ring*/, "Resources/Textures/white1x1.dds", rp);
		t.ring->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
		t.ring->SetBillboardMode(BillboardMode::Full);
		t.ring->SetDepthWrite(false);
		t.ring->SetCullBackface(false);
		t.ring->SetColor(specialLockonColor_);
		t.ring->SetScale({ 0.0f, 0.0f, 0.0f });
	}

	// キューブ（半透明オレンジ、Blend=Normal、両面）
	{
		t.cube = std::make_unique<EffectPrimitiveRenderer>();
		t.cube->Initialize(1 /*Box*/, "Resources/Textures/white1x1.dds");
		t.cube->SetBlendMode(PrimitivePipeline::kBlendModeNormal);
		t.cube->SetDepthWrite(false);
		t.cube->SetCullBackface(false);
		t.cube->SetColor(specialCubeColor_);
		t.cube->SetScale({ 0.0f, 0.0f, 0.0f });
	}

	t.visualsCreated = true;
}

void StagePlayScene::UpdateLockonTargetVisuals(SpecialLockonTarget& t, float localTime) {
	// ターゲットのワールド位置を取得（敵 or 弾が消えていたら 0 で抜ける）
	if (!t.entity) return;
	const Vector3* tp = t.entity->GetEditableTranslate();
	if (!tp) return;
	const Vector3 enemyPos = *tp;
	const float r = t.radius;

	// 線：t∈[0, lineTravel+lineHold] = [0, 0.3] で表示。
	//  outer 端は固定（radius*5 と radius*3 の中間など）、inner 端が radius*3 → 0 に移動。
	//  ここでは仕様通り「内側の点が radius*3 から 0 へ」、線の長さは constant（= radius*2）。
	const float lineLen = r * 2.0f;
	const float lineLife = specialLockonLineTravelTime_ + specialLockonLineHoldTime_; // 0.3
	const bool  lineVisible = (localTime >= 0.0f && localTime < lineLife);

	// 3 つの角度（パターンA: 90°/210°/330°、パターンB: 270°/30°/150°）
	const float kPi = 3.14159265358979323846f;
	const float baseAngles[3] = { 0.5f * kPi, 7.0f / 6.0f * kPi, 11.0f / 6.0f * kPi };
	const float patternOffset = (t.patternIndex == 1) ? kPi : 0.0f; // パターンBは180°回転

	// カメラ基底（線・リングの向き計算用）
	const Matrix4x4 camRot = MakeRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
	const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };

	float innerDist;
	if (localTime <= specialLockonLineTravelTime_) {
		const float u = localTime / specialLockonLineTravelTime_;
		innerDist = r * 3.0f * (1.0f - u);
	} else {
		innerDist = 0.0f;
	}
	const float outerDist = innerDist + lineLen;
	const float midDist   = (innerDist + outerDist) * 0.5f;

	for (int i = 0; i < 3; ++i) {
		if (!t.lines[i]) continue;
		if (!lineVisible) {
			t.lines[i]->SetScale({ 0.0f, 0.0f, 1.0f });
			continue;
		}
		const float angle = baseAngles[i] + patternOffset;
		const Vector3 dir = {
			std::cos(angle) * camRight.x + std::sin(angle) * camUp.x,
			std::cos(angle) * camRight.y + std::sin(angle) * camUp.y,
			std::cos(angle) * camRight.z + std::sin(angle) * camUp.z,
		};
		const Vector3 midPos = {
			enemyPos.x + dir.x * midDist,
			enemyPos.y + dir.y * midDist,
			enemyPos.z + dir.z * midDist,
		};
		t.lines[i]->SetTranslate(midPos);
		t.lines[i]->SetRotate({ 0.0f, 0.0f, angle - 0.5f * kPi });
		t.lines[i]->SetScale({ specialLockonLineWidth_, lineLen, 1.0f });
		Vector4 col = specialLockonColor_;
		// 0.2〜0.3 秒は到達後の余韻でαを薄くしてフェード
		if (localTime > specialLockonLineTravelTime_) {
			const float fade = 1.0f - (localTime - specialLockonLineTravelTime_) / specialLockonLineHoldTime_;
			col.w = (std::max)(0.0f, fade) * specialLockonColor_.w;
		}
		t.lines[i]->SetColor(col);
	}

	// リング：t∈[0.2, 0.6] で表示。サイズは ジャスト(r) → r*0.3 に収縮（0.2〜0.5）、0.5〜0.6 でフェードアウト
	const float ringAppear  = specialLockonRingAppearTime_;            // 0.2
	const float ringComplete= specialLockonDuration_;                  // 0.5
	const float ringFadeEnd = specialLockonDuration_ + specialLockonRingFadeoutTime_; // 0.6
	if (t.ring) {
		if (localTime < ringAppear || localTime > ringFadeEnd) {
			t.ring->SetScale({ 0.0f, 0.0f, 0.0f });
		} else {
			float ringRadius;
			if (localTime <= ringComplete) {
				const float u = (localTime - ringAppear) / (ringComplete - ringAppear);
				ringRadius = r * (1.0f - u) + r * 0.3f * u;
			} else {
				ringRadius = r * 0.3f;
			}
			t.ring->SetTranslate(enemyPos);
			t.ring->SetScale({ ringRadius, ringRadius, ringRadius });
			Vector4 col = specialLockonColor_;
			if (localTime > ringComplete) {
				const float fade = 1.0f - (localTime - ringComplete) / specialLockonRingFadeoutTime_;
				col.w = (std::max)(0.0f, fade) * specialLockonColor_.w;
			}
			t.ring->SetColor(col);
		}
	}

	// キューブ：ロック完了（0.5秒）以降ターゲットが消えるまで継続
	if (t.cube) {
		if (localTime < specialLockonDuration_) {
			t.cube->SetScale({ 0.0f, 0.0f, 0.0f });
		} else {
			const float side = r * 2.0f;
			t.cube->SetTranslate(enemyPos);
			t.cube->SetScale({ side, side, side });
			t.cube->SetColor(specialCubeColor_);
		}
	}

	// Update（CB書き込み）。dt=0 でも位置/scale/color は反映される
	const float realDt = dxCore_ ? dxCore_->GetDeltaTime() : 0.0f;
	for (int i = 0; i < 3; ++i) if (t.lines[i]) t.lines[i]->Update(camera_.get(), realDt);
	if (t.ring) t.ring->Update(camera_.get(), realDt);
	if (t.cube) t.cube->Update(camera_.get(), realDt);
}

void StagePlayScene::UpdateSpecialPhaseLockon(float realDt) {
	specialPhase2Timer_ += realDt;

	// 各ターゲットの可視化生成・更新
	for (auto& t : specialLockonTargets_) {
		// ターゲットが消えていたらスキップ（敵が破壊済み or 弾消滅）
		if (!t.entity) continue;
		// 弾は remainingLifetime<0 で消滅予定だが Phase 2 では World=0 なので生きてる想定

		const float localTime = specialPhase2Timer_ - t.startTime;
		if (localTime < 0.0f) continue; // まだ開始してない

		if (!t.visualsCreated) {
			SpawnLockonVisuals(t);
		}
		UpdateLockonTargetVisuals(t, localTime);
	}

	// 全ターゲットのロック完了タイミング = 最後のターゲットの startTime + Duration
	if (!specialAllLockonComplete_) {
		bool justCompleted = false;
		if (specialLockonTargets_.empty()) {
			specialAllLockonComplete_ = true;
			specialChargeStartTime_ = specialPhase2Timer_;
			justCompleted = true;
		} else {
			const float lastStart = specialLockonTargets_.back().startTime;
			if (specialPhase2Timer_ >= lastStart + specialLockonDuration_) {
				specialAllLockonComplete_ = true;
				specialChargeStartTime_ = specialPhase2Timer_;
				justCompleted = true;
			}
		}
		// チャージ電撃の本数ぶん LightningRuntime を起動
		if (justCompleted) {
			specialChargeBolts_.clear();
			specialChargeBolts_.reserve(specialChargeBoltCount_);
			for (int i = 0; i < specialChargeBoltCount_; ++i) {
				SpecialChargeBolt b;
				b.rt = std::make_unique<LightningRuntime>();
				PrimitiveGenerator::LightningBoltParams lp{};
				lp.appearance.startWidth = 0.05f;
				lp.appearance.endWidth   = 0.05f;
				lp.appearance.planeCount = 3;
				lp.appearance.fadeStartLength = 0.05f;
				lp.appearance.fadeEndLength   = 0.05f;
				lp.appearance.startColor = specialLockonColor_;
				lp.appearance.endColor   = specialLockonColor_;
				lp.generations = 5;
				lp.maxOffsetRatio = 0.2f;
				lp.branchProbability = 0.2f;
				lp.branchLengthScale = 0.25f;
				b.rt->Initialize(lp, "Resources/Textures/white1x1.dds", 2 /*Add*/);
				b.rt->SetBoltLifetime(specialChargeRegenInterval_);
				b.rt->SetOverlapOffset(specialChargeRegenInterval_ * 0.5f);
				// 各本に再生成タイミングをずらして配置（同時に全部点滅しないように）
				b.nextRegenTime = specialPhase2Timer_
					+ static_cast<float>(i) * (specialChargeRegenInterval_ / specialChargeBoltCount_);
				specialChargeBolts_.push_back(std::move(b));
			}
		}
	}

	// チャージ進行（全ロック完了後）
	if (specialAllLockonComplete_) {
		const float chargeT = specialPhase2Timer_ - specialChargeStartTime_;
		const float u = (std::clamp)(chargeT / specialChargeDuration_, 0.0f, 1.0f);

		// プレイヤー位置と始終点半径
		// 始点はプレイヤーカプセル表面（スクリーン投影楕円）。
		// 終点はバリア表面方向（チャージ進行 u で成長、最小長で下限を確保）
		const Vector3 ppos = SpecialPlayerCenter();
		// カプセル投影の半軸（ImGui で 0 のままなら collider から自動算出）
		float ellipseA = specialChargeStartRadiusH_; // 水平
		float ellipseB = specialChargeStartRadiusV_; // 垂直
		if ((ellipseA <= 0.0f || ellipseB <= 0.0f) && player_) {
			const Collider& col = player_->GetCollider();
			const float cr = col.capsuleRadius;
			const float ch = col.capsuleHeight;
			if (ellipseA <= 0.0f) ellipseA = (cr > 0.01f ? cr : 0.5f);
			if (ellipseB <= 0.0f) ellipseB = (ch > 0.01f ? ch * 0.5f + cr : ellipseA * 1.8f);
		}
		if (ellipseA < 0.01f) ellipseA = 0.5f;
		if (ellipseB < 0.01f) ellipseB = 1.0f;

		// カメラ基底
		const Matrix4x4 camRot = MakeRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
		const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
		const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };

		// 乱数源
		static thread_local std::mt19937 rng(
			static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159265358979323846f);

		for (auto& b : specialChargeBolts_) {
			if (!b.rt) continue;

			// 再生成タイミングに到達したら角度をランダムに更新
			if (specialPhase2Timer_ >= b.nextRegenTime) {
				b.angleBase = distAngle(rng);
				b.nextRegenTime += specialChargeRegenInterval_;
				if (b.nextRegenTime < specialPhase2Timer_) {
					b.nextRegenTime = specialPhase2Timer_ + specialChargeRegenInterval_;
				}
			}

			// 放射方向（カメラ平面上のランダム角度）
			const float a = b.angleBase;
			const float ct = std::cos(a), st = std::sin(a);
			const Vector3 dir = {
				ct * camRight.x + st * camUp.x,
				ct * camRight.y + st * camUp.y,
				ct * camRight.z + st * camUp.z,
			};
			// 始点：スクリーン投影楕円 r(θ) = a*b / sqrt((b cosθ)^2 + (a sinθ)^2)
			const float denom = std::sqrt((ellipseB * ct) * (ellipseB * ct) + (ellipseA * st) * (ellipseA * st));
			const float startR = (denom > 1e-5f) ? (ellipseA * ellipseB) / denom : ellipseA;
			// 終点：チャージ進行に応じた半径（最小長下限）
			const float endR = (std::max)(startR + specialChargeMinLength_, specialBarrierRadius_ * u);

			Vector3 s = { ppos.x + dir.x * startR, ppos.y + dir.y * startR, ppos.z + dir.z * startR };
			Vector3 e = { ppos.x + dir.x * endR,   ppos.y + dir.y * endR,   ppos.z + dir.z * endR   };
			b.rt->SetEndpoints(s, e);
			b.rt->Update(camera_.get(), realDt);
		}

		// チャージ完了で Fire フェーズへ
		if (chargeT >= specialChargeDuration_) {
			EnterSpecialPhaseFire();
		}
	}
}

void StagePlayScene::EnterSpecialPhaseFire() {
	specialPhase_      = SpecialPhase::Fire;
	specialPhaseTimer_ = 0.0f;

	// World 時間を再開（Lockon で 0 にしていた）
	SetTimeScale(TimeGroup::World, 1.0f);

	// 処理待ちキューをロック済みターゲットから構築（近い順は EnumerateLockonTargets でソート済み）
	specialFireQueue_.clear();
	specialFireProcessed_.clear();
	for (auto& t : specialLockonTargets_) {
		if (!t.entity) continue;
		if (!specialFireProcessed_.insert(t.entity).second) continue;
		SpecialFireQueueItem item;
		item.entity   = t.entity;
		item.isBullet = (t.bulletIndex >= 0);
		specialFireQueue_.push_back(item);
	}

	// Phase 2 の可視化（線・リング・キューブ）とチャージ電撃は役目を終えたので trash へ
	for (auto& b : specialChargeBolts_) {
		if (b.rt) {
			SpecialTrashEntry e;
			e.lightning = std::move(b.rt);
			specialTrash_.push_back(std::move(e));
		}
	}
	specialChargeBolts_.clear();
	for (auto& t : specialLockonTargets_) {
		for (int i = 0; i < 3; ++i) {
			if (t.lines[i]) {
				SpecialTrashEntry e;
				e.renderer = std::move(t.lines[i]);
				specialTrash_.push_back(std::move(e));
			}
		}
		if (t.ring) { SpecialTrashEntry e; e.renderer = std::move(t.ring); specialTrash_.push_back(std::move(e)); }
		if (t.cube) { SpecialTrashEntry e; e.renderer = std::move(t.cube); specialTrash_.push_back(std::move(e)); }
	}
	specialLockonTargets_.clear();

	specialFireBolts_.clear();
	specialFireLaunchTimer_ = 0.0f; // 1 本目は即発射
}

bool StagePlayScene::IsSpecialTargetAlive(IImGuiEditable* e) const {
	if (!e) return false;
	for (auto& up : dynamicPrimitives_) if (up.get() == e) return true;
	for (auto& up : object3DInstances_) if (up.get() == e) return true;
	for (auto& up : dynamicAnimated_)   if (up.get() == e) return true;
	return false;
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
	for (auto& b : bullets_) {
		if (!b.primitive) continue;
		if (b.primitive->GetTag() != EntityTag::EnemyAttack) continue;
		const Vector3* p = b.primitive->GetEditableTranslate();
		if (!p || !isVisibleInClip(*p)) continue;
		out.emplace_back(static_cast<IImGuiEditable*>(b.primitive), true);
	}
	// 敵本体（Enemy / Boss）
	std::unordered_set<IImGuiEditable*> seen;
	auto pushEnemy = [&](IImGuiEditable* e) {
		if (!e || !seen.insert(e).second) return;
		const EntityTag tag = e->GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Vector3* p = e->GetEditableTranslate();
		if (!p || !isVisibleInClip(*p)) return;
		out.emplace_back(e, false);
	};
	for (auto& up : dynamicPrimitives_) pushEnemy(up.get());
	for (auto& up : object3DInstances_) pushEnemy(up.get());
	for (auto& up : dynamicAnimated_)   pushEnemy(up.get());
}

Vector3 StagePlayScene::SpecialPlayerCenter() const {
	Vector3 base = player_ ? player_->GetTranslate() : Vector3{ 0.0f, 0.0f, 0.0f };
	if (!player_) return base;
	float off = specialPlayerCenterOffset_;
	if (off <= 0.0f) off = player_->GetCollider().offset.y; // プレハブの足元→カプセル中心
	if (off == 0.0f) return base;
	// カメラ平面ベースの演出（楕円射影/放射）と整合するよう up はカメラ up を使う
	const Matrix4x4 camRot = MakeRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camUp = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };
	return { base.x + camUp.x * off, base.y + camUp.y * off, base.z + camUp.z * off };
}

Vector3 StagePlayScene::SpecialBoltStart(const Vector3& playerPos, const Vector3& dir) const {
	// プレイヤーカプセルのスクリーン投影楕円表面の点を返す（チャージ電撃と同じ考え方）。
	// dir は正規化済みの「始点 → 対象」方向。
	float ellipseA = specialChargeStartRadiusH_;
	float ellipseB = specialChargeStartRadiusV_;
	if ((ellipseA <= 0.0f || ellipseB <= 0.0f) && player_) {
		const Collider& col = player_->GetCollider();
		const float cr = col.capsuleRadius;
		const float ch = col.capsuleHeight;
		if (ellipseA <= 0.0f) ellipseA = (cr > 0.01f ? cr : 0.5f);
		if (ellipseB <= 0.0f) ellipseB = (ch > 0.01f ? ch * 0.5f + cr : ellipseA * 1.8f);
	}
	if (ellipseA < 0.01f) ellipseA = 0.5f;
	if (ellipseB < 0.01f) ellipseB = 1.0f;

	const Matrix4x4 camRot = MakeRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
	const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };
	// dir をカメラ平面に投影した成分から角度を求める
	const float cx = dir.x * camRight.x + dir.y * camRight.y + dir.z * camRight.z;
	const float cy = dir.x * camUp.x    + dir.y * camUp.y    + dir.z * camUp.z;
	const float ct = std::cos(std::atan2(cy, cx));
	const float st = std::sin(std::atan2(cy, cx));
	const float denom = std::sqrt((ellipseB * ct) * (ellipseB * ct) + (ellipseA * st) * (ellipseA * st));
	const float startR = (denom > 1e-5f) ? (ellipseA * ellipseB) / denom : ellipseA;
	return { playerPos.x + dir.x * startR, playerPos.y + dir.y * startR, playerPos.z + dir.z * startR };
}

void StagePlayScene::UpdateSpecialPhaseFire(float dt) {
	specialFireLaunchTimer_ -= dt;

	const Vector3 ppos = SpecialPlayerCenter();

	// ----- 新規侵入の検出：画面内に新しく入った敵/敵弾を queue に追加 -----
	{
		std::vector<std::pair<IImGuiEditable*, bool>> screen;
		CollectScreenTargets(screen);
		// 現在アクティブな bolt のターゲット集合（重複防止）
		std::unordered_set<IImGuiEditable*> activeSet;
		for (auto& fb : specialFireBolts_) if (fb.entity) activeSet.insert(fb.entity);
		for (auto& s : screen) {
			IImGuiEditable* e = s.first;
			if (specialFireProcessed_.count(e)) continue; // 既処理（発射済み）
			if (activeSet.count(e)) continue;             // 発射中
			// queue 内の重複
			bool inQueue = false;
			for (auto& q : specialFireQueue_) { if (q.entity == e) { inQueue = true; break; } }
			if (inQueue) continue;
			SpecialFireQueueItem item; item.entity = e; item.isBullet = s.second;
			specialFireQueue_.push_back(item);
		}
	}

	// ----- 空き枠があり、発射間隔を満たしたら queue から 1 本発射 -----
	if (specialFireLaunchTimer_ <= 0.0f &&
		static_cast<int>(specialFireBolts_.size()) < specialFireSimultaneous_) {
		// 生存している先頭ターゲットを探す
		while (!specialFireQueue_.empty()) {
			SpecialFireQueueItem item = specialFireQueue_.front();
			specialFireQueue_.erase(specialFireQueue_.begin());
			// 弾は primitive ポインタ、敵は entity ポインタ。いずれも動的コンテナで生存確認できる
			if (!IsSpecialTargetAlive(item.entity)) continue; // 既に消滅 → スキップ
			specialFireProcessed_.insert(item.entity);

			SpecialFireBolt fb;
			fb.entity   = item.entity;
			fb.isBullet = item.isBullet;
			fb.elapsed  = 0.0f;
			fb.nextTickTime = 0.0f; // 開始直後に 1 ティック目
			fb.rt = std::make_unique<LightningRuntime>();
			PrimitiveGenerator::LightningBoltParams lp{};
			lp.appearance.startWidth = specialFireStartWidth_;
			lp.appearance.endWidth   = specialFireEndWidth_;
			lp.appearance.planeCount = 3;
			lp.appearance.fadeStartLength = 0.05f;
			lp.appearance.fadeEndLength   = 0.05f;
			lp.appearance.startColor = specialFireColor_;
			lp.appearance.endColor   = specialFireColor_;
			lp.generations = 6;
			lp.maxOffsetRatio = specialFireMaxOffsetRatio_;
			lp.branchProbability = specialFireBranchProb_;
			lp.branchLengthScale = 0.3f;
			fb.rt->Initialize(lp, "Resources/Textures/white1x1.dds", 2 /*Add*/);
			fb.rt->SetBoltLifetime(specialFireBoltLifetime_);
			fb.rt->SetOverlapOffset(specialFireBoltLifetime_ * 0.5f);
			specialFireBolts_.push_back(std::move(fb));
			specialFireLaunchTimer_ = specialFireLaunchInterval_;
			break; // 1 フレームに発射するのは 1 本まで
		}
	}

	// ----- 各 bolt の更新（追従・ダメージティック・終了判定）-----
	for (auto& fb : specialFireBolts_) {
		if (fb.done) continue;
		if (!fb.rt) { fb.done = true; continue; }

		// ターゲット生存確認（World 再開中は sweep でポインタが無効化されうる）
		if (!IsSpecialTargetAlive(fb.entity)) { fb.done = true; continue; }
		const Vector3* tp = fb.entity->GetEditableTranslate();
		if (!tp) { fb.done = true; continue; }
		fb.lastTargetPos = *tp;

		fb.elapsed += dt;

		// サンダーの始点（プレイヤーカプセル表面）と終点（ターゲット現在位置）。
		// 発射直後は始点→ターゲットへ伸ばす（敵へ飛んでいく感）。grow 完了後は終点を追従。
		Vector3 dir = { fb.lastTargetPos.x - ppos.x, fb.lastTargetPos.y - ppos.y, fb.lastTargetPos.z - ppos.z };
		const float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (dlen > 1e-4f) { dir.x /= dlen; dir.y /= dlen; dir.z /= dlen; }
		else { dir = { 0.0f, 1.0f, 0.0f }; }
		const Vector3 s = SpecialBoltStart(ppos, dir);
		float gu = (specialFireGrowTime_ > 1e-4f)
			? (std::clamp)(fb.elapsed / specialFireGrowTime_, 0.0f, 1.0f)
			: 1.0f;
		if (gu < 0.12f) gu = 0.12f; // 0 長（空メッシュ）回避＆最低限の見える長さ確保
		const Vector3 renderEnd = {
			s.x + (fb.lastTargetPos.x - s.x) * gu,
			s.y + (fb.lastTargetPos.y - s.y) * gu,
			s.z + (fb.lastTargetPos.z - s.z) * gu,
		};
		fb.rt->SetEndpoints(s, renderEnd);
		fb.rt->Update(camera_.get(), dt);

		// ダメージティック
		while (fb.elapsed >= fb.nextTickTime) {
			if (!fb.isBullet) {
				fb.entity->GetHP().TakeDamage(specialFireTickDamage_);
			}
			fb.nextTickTime += specialFireTickInterval_;
		}

		// 終了判定
		if (fb.isBullet) {
			// 敵弾：最低拘束時間で強制消滅
			if (fb.elapsed >= specialFireMinHold_) {
				for (auto& b : bullets_) {
					if (static_cast<IImGuiEditable*>(b.primitive) == fb.entity) { b.remainingLifetime = -1.0f; break; }
				}
				fb.done = true;
			}
		} else {
			const bool dead = fb.entity->GetHP().IsDead();
			if (dead && fb.elapsed >= specialFireMinHold_) {
				fb.done = true; // 撃破（scoreは SweepDeadEntities 前の加点経路で処理）
			} else if (fb.elapsed >= specialFireMaxHold_) {
				fb.done = true; // 生存でも上限で次へ（ボス等）
			}
		}
	}

	// ----- 終了した bolt を trash へ回収 -----
	for (auto& fb : specialFireBolts_) {
		if (fb.done && fb.rt) {
			SpecialTrashEntry e;
			e.lightning = std::move(fb.rt);
			specialTrash_.push_back(std::move(e));
		}
	}
	specialFireBolts_.erase(
		std::remove_if(specialFireBolts_.begin(), specialFireBolts_.end(),
			[](const SpecialFireBolt& fb) { return fb.done; }),
		specialFireBolts_.end());

	// ----- Phase 終了：処理待ち 0 かつ アクティブ雷 0 -----
	if (specialFireQueue_.empty() && specialFireBolts_.empty()) {
		EnterSpecialPhaseEnd();
	}
}

void StagePlayScene::EnterSpecialPhaseEnd() {
	specialPhase_      = SpecialPhase::End;
	specialPhaseTimer_ = 0.0f;
}

void StagePlayScene::UpdateSpecialPhaseEnd(float dt) {
	(void)dt;
	// Fire 終了から specialEndDuration_ 秒後にバリアを消して通常へ戻す
	if (specialPhaseTimer_ >= specialEndDuration_) {
		EndSpecialMove();
	}
}

void StagePlayScene::EndSpecialMove() {
	specialActive_     = false;
	specialPhase_      = SpecialPhase::Idle;
	specialPhaseTimer_ = 0.0f;
	specialTimer_      = 0.0f;
	specialPhase2Timer_       = 0.0f;
	specialAllLockonComplete_ = false;
	specialChargeStartTime_   = 0.0f;

	// ワールド時間を通常に戻す
	SetTimeScale(TimeGroup::World, 1.0f);

	// プレイヤー位置を「画面内クリップ範囲」に収めるため、入力オフセットを 0 に戻す
	playerInputOffset_ = { 0.0f, 0.0f };
	playerVelocity_    = { 0.0f, 0.0f };

	// バリア・ロックオン可視化・チャージ電撃を遅延削除キューへ移して、
	// 数フレーム後に破棄する（GPU が前フレームの commandList で参照中の可能性あり）
	if (specialBarrierVis_) {
		SpecialTrashEntry e;
		e.renderer = std::move(specialBarrierVis_);
		specialTrash_.push_back(std::move(e));
	}
	for (auto& b : specialChargeBolts_) {
		if (b.rt) {
			SpecialTrashEntry e;
			e.lightning = std::move(b.rt);
			specialTrash_.push_back(std::move(e));
		}
	}
	specialChargeBolts_.clear();
	for (auto& fb : specialFireBolts_) {
		if (fb.rt) {
			SpecialTrashEntry e;
			e.lightning = std::move(fb.rt);
			specialTrash_.push_back(std::move(e));
		}
	}
	specialFireBolts_.clear();
	specialFireQueue_.clear();
	specialFireProcessed_.clear();
	specialFireLaunchTimer_ = 0.0f;
	// 光の翼パーティクルは発生を止めれば寿命で自然消滅（追加処理不要）
	for (auto& t : specialLockonTargets_) {
		for (int i = 0; i < 3; ++i) {
			if (t.lines[i]) {
				SpecialTrashEntry e;
				e.renderer = std::move(t.lines[i]);
				specialTrash_.push_back(std::move(e));
			}
		}
		if (t.ring) {
			SpecialTrashEntry e;
			e.renderer = std::move(t.ring);
			specialTrash_.push_back(std::move(e));
		}
		if (t.cube) {
			SpecialTrashEntry e;
			e.renderer = std::move(t.cube);
			specialTrash_.push_back(std::move(e));
		}
	}
	specialLockonTargets_.clear();
}

void StagePlayScene::ApplySpecialCamera(const Vector3& playerWorldPos) {
	(void)playerWorldPos;
	if (!camera_ || !specialActive_) return;

	// 既存のカメラ位置を基準に forward 逆方向 + up 方向に引く（FovY も加算）
	const Vector3 eye = camera_->GetTranslate();
	const Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
	const Vector3 forward = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };
	const Vector3 up      = { rot.m[1][0], rot.m[1][1], rot.m[1][2] };

	camera_->SetTranslate({
		eye.x - forward.x * specialCamPullback_ + up.x * specialCamUpAdd_,
		eye.y - forward.y * specialCamPullback_ + up.y * specialCamUpAdd_,
		eye.z - forward.z * specialCamPullback_ + up.z * specialCamUpAdd_,
	});
	camera_->SetFovY(camera_->GetFovY() + specialCamFovAdd_);
	camera_->Update();
}

void StagePlayScene::RefreshPlayerHpInvincibility() {
	if (!player_) return;

	// 無敵の発生源を全て集約。どれか1つでも有効なら HP.enabled=false にして
	// CollisionManager::Update の自動ダメージ経路も止める。
	const bool justInv    = justDodgeActive_;
	const bool dodgeInv   = dodgeActive_ && dodgeTimer_ <= dodgeIFrameDuration_;
	const bool dmgInv     = playerInvincibilityTimer_ > 0.0f;
	const bool specialInv = specialActive_;
	const bool invincible = justInv || dodgeInv || dmgInv || specialInv;

	player_->GetHP().enabled = !invincible;
}
