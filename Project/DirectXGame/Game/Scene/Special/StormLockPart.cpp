#include "StormLockPart.h"
#include "ISpecialMoveHost.h"

#include "Camera.h"
#include "Components/Gameplay.h"
#include "Components/EntityTag.h"
#include "Components/SphereCollider.h"
#include "Effect/EffectManager.h"
#include "Effect/EffectPrimitiveRenderer.h"
#include "Effect/LightningRuntime.h"
#include "Game.h"
#include "IImGuiEditable.h"
#include "Json/JsonValue.h"
#include "MathUtility.h"
#include "Particle/GPUParticleManager.h"
#include "Primitive/LineRenderer.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Primitive/PrimitivePipeline.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace {
	// ロックオンの線の数
	constexpr int kLockonLineCount = 3;
	// Vector3 の JSON 配列に要る要素の数
	constexpr int kVector3ComponentCount = 3;
	// Vector4（色）の JSON 配列に要る要素の数
	constexpr int kVector4ComponentCount = 4;
	// Vector2 の JSON 配列に要る要素の数
	constexpr int kVector2ComponentCount = 2;
	// バリアのパーティクルを出す間隔がこれ以下なら既定の間隔を使う [秒]
	constexpr float kMinEmitInterval = 1e-4f;
	// バリアのパーティクルを出す既定の間隔 [秒]
	constexpr float kFallbackEmitInterval = 0.02f;
	// 1 フレームに出すバリアのパーティクルの回数の上限（出し過ぎない安全弁）
	constexpr int kMaxBurstsPerFrame = 8;
	// ワイヤ球の円 1 周の分割数の最小
	constexpr int kMinWireSegments = 3;
	// ロックオンの線の角度の型の数（交互に A/B）
	constexpr int kLockonPatternCount = 2;
	// これより小さい当たりの半径は使わず既定の半径にする [m]
	constexpr float kMinTargetRadius = 0.01f;
	// 当たりの半径が分からないときの既定の半径 [m]
	constexpr float kFallbackTargetRadius = 0.5f;
	// ロックオンのリングの分割数
	constexpr int kLockonRingDivisions = 48;
	// 線の内側の端が動き始める距離（敵の半径に対する倍率）
	constexpr float kLineStartRadiusRatio = 3.0f;
	// リングが縮み切ったときの半径（敵の半径に対する倍率）
	constexpr float kRingEndRadiusRatio = 0.3f;
	// 溜めの雷の太さ [m]
	constexpr float kChargeBoltWidth = 0.05f;
	// 雷の板の枚数
	constexpr int kBoltPlaneCount = 3;
	// 雷の端を薄くする長さ [m]
	constexpr float kBoltFadeLength = 0.05f;
	// 溜めの雷の枝分かれの世代数
	constexpr int kChargeBoltGenerations = 5;
	// 溜めの雷のジグザグの大きさ（長さに対する割合）
	constexpr float kChargeBoltMaxOffsetRatio = 0.2f;
	// 溜めの雷が枝分かれする確率
	constexpr float kChargeBoltBranchProbability = 0.2f;
	// 溜めの雷の枝の長さの倍率
	constexpr float kChargeBoltBranchLengthScale = 0.25f;
	// 楕円の半径の式の分母がこれ以下なら長い方の半径を使う
	constexpr float kMinEllipseDenom = 1e-5f;
	// 放つ雷の枝分かれの世代数
	constexpr int kFireBoltGenerations = 6;
	// 放つ雷の枝の長さの倍率
	constexpr float kFireBoltBranchLengthScale = 0.3f;
	// 向きの長さがこれ以下なら上向きにする
	constexpr float kMinDirectionLength = 1e-4f;
	// 雷が伸びる長さの下限 [秒]（これ以下ならすぐ伸び切る）
	constexpr float kMinGrowSeconds = 1e-4f;
	// 雷の伸びの最小（0 長の空メッシュを避け、最低限見える長さ）
	constexpr float kMinGrowRatio = 0.12f;
}

StormLockPart::StormLockPart() = default;
StormLockPart::~StormLockPart() = default;

void StormLockPart::Initialize(ISpecialMoveHost* host, Camera* camera) {
	host_ = host;
	camera_ = camera;
}

void StormLockPart::Finalize() {
	if (barrierEffectHandle_ != kInvalidEffectHandle) {
		if (auto* em = EffectManager::GetInstance()) em->Stop(barrierEffectHandle_);
		barrierEffectHandle_ = kInvalidEffectHandle;
	}
	trash_.clear();
	barrierVis_.reset();
	lockonTargets_.clear();
	chargeBolts_.clear();
	fireBolts_.clear();
	fireQueue_.clear();
	fireProcessed_.clear();
}

void StormLockPart::Begin() {
	finished_ = false;
	EnterBarrierPhase();
}

bool StormLockPart::Update(float worldDt, float realDt) {
	realDt_ = realDt;
	phaseTimer_ += worldDt;

	// バリア球の追従＋当たり判定と光の翼は全フェーズ共通（Fire 中もカメラ前進にズレず追従させる）
	UpdateBarrier();
	UpdateWings();

	switch (phase_) {
	case Phase::Barrier:
		UpdateBarrierPhase(worldDt);
		break;
	case Phase::Lockon:
		// Lockon は World が止まっているので実時間で進める
		UpdateLockonPhase(realDt);
		break;
	case Phase::Fire:
		// Fire は World 再開済み。World の dt で進めて敵の移動と同期させる
		UpdateFirePhase(worldDt);
		break;
	case Phase::End:
		UpdateEndPhase(worldDt);
		break;
	default:
		break;
	}
	return finished_;
}

void StormLockPart::ApplyCamera() {
	if (!camera_) return;
	// 今のカメラから forward 逆＋up へ引く（全フェーズ）。全て 0 ならカメラに触れない（FOV の浮動小数ドリフトを避ける）
	if (camPullback_ == 0.0f && camUpAdd_ == 0.0f && camFovAdd_ == 0.0f) return;

	const Vector3 eye = camera_->GetTranslate();
	const Matrix4x4 rot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 forward = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };
	const Vector3 up      = { rot.m[1][0], rot.m[1][1], rot.m[1][2] };
	camera_->SetTranslate({
		eye.x - forward.x * camPullback_ + up.x * camUpAdd_,
		eye.y - forward.y * camPullback_ + up.y * camUpAdd_,
		eye.z - forward.z * camPullback_ + up.z * camUpAdd_,
	});
	camera_->SetFovY(camera_->GetFovY() + camFovAdd_);
	camera_->Update();
}

void StormLockPart::Reset() {
	phase_             = Phase::Idle;
	phaseTimer_        = 0.0f;
	lockonTimer_       = 0.0f;
	allLockonComplete_ = false;
	chargeStartTime_   = 0.0f;
	finished_          = false;

	// バリア本体エフェクトを停止
	if (barrierEffectHandle_ != kInvalidEffectHandle) {
		if (auto* em = EffectManager::GetInstance()) em->Stop(barrierEffectHandle_);
		barrierEffectHandle_ = kInvalidEffectHandle;
	}
	// バリア・ロックオン可視化・電撃は遅延削除へ（GPU が前フレームの commandList で参照中の可能性あり）
	TrashRenderer(barrierVis_);
	for (auto& b : chargeBolts_) TrashLightning(b.rt);
	chargeBolts_.clear();
	for (auto& fb : fireBolts_) TrashLightning(fb.rt);
	fireBolts_.clear();
	fireQueue_.clear();
	fireProcessed_.clear();
	fireLaunchTimer_ = 0.0f;
	TrashLockonVisuals();
	// 光の翼パーティクルは発生を止めれば寿命で自然消滅（追加処理不要）
}

void StormLockPart::UpdateTrash(float realDt) {
	// バリア可視化プリミティブの CB 更新（位置は UpdateBarrier で毎フレ反映済み）
	if (barrierVis_) barrierVis_->Update(camera_, realDt);

	for (auto& e : trash_) {
		if (e.framesLeft > 0) --e.framesLeft;
	}
	trash_.erase(std::remove_if(trash_.begin(), trash_.end(),
		[](const TrashEntry& e) { return e.framesLeft <= 0; }), trash_.end());
}

void StormLockPart::Draw() {
	// バリア可視化（半透明球）。既定では非表示（ワイヤ球のみ）
	if (barrierFillOn_ && barrierVis_) barrierVis_->Draw();
	// ロックオン可視化（線・リング・キューブ）
	for (auto& t : lockonTargets_) {
		if (!t.visualsCreated) continue;
		for (int i = 0; i < kLockonLineCount; ++i) if (t.lines[i]) t.lines[i]->Draw();
		if (t.ring) t.ring->Draw();
		if (t.cube) t.cube->Draw();
	}
	// チャージ電撃（複数本）
	for (auto& b : chargeBolts_) {
		if (b.rt && b.rt->IsActive()) b.rt->Draw();
	}
	// 発射の雷
	for (auto& fb : fireBolts_) {
		if (fb.rt && fb.rt->IsActive()) fb.rt->Draw();
	}
	// 光の翼は GPU パーティクル群（special_wing_*）として DrawGlobalEffects が自動描画
}

void StormLockPart::CollectScreenTargets(std::vector<std::pair<IImGuiEditable*, bool>>& out) {
	if (!camera_) return;
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	// カメラの前方にあるか（clip.w > 0）と XY が画面内かだけで判定。
	// レールSTG では弾や敵が far plane 近くを飛ぶことが多いので、奥行きフィルタは敢えてかけない
	auto isVisibleInClip = [&](const Vector3& w) {
		const float cx = w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0];
		const float cy = w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1];
		const float cw = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
		if (cw <= 0.0f) return false;
		return std::abs(cx / cw) <= 1.0f && std::abs(cy / cw) <= 1.0f;
	};
	auto push = [&](IImGuiEditable* e, bool isBullet) {
		const Vector3* p = e ? e->GetEditableTranslate() : nullptr;
		if (p && isVisibleInClip(*p)) out.emplace_back(e, isBullet);
	};
	std::vector<IImGuiEditable*> list;
	host_->CollectEnemyBullets(list);
	for (IImGuiEditable* e : list) push(e, true);
	list.clear();
	host_->CollectEnemies(list);
	for (IImGuiEditable* e : list) push(e, false);
}

void StormLockPart::TrashRenderer(std::unique_ptr<EffectPrimitiveRenderer>& r) {
	if (!r) return;
	TrashEntry e;
	e.renderer = std::move(r);
	trash_.push_back(std::move(e));
}

void StormLockPart::TrashLightning(std::unique_ptr<LightningRuntime>& rt) {
	if (!rt) return;
	TrashEntry e;
	e.lightning = std::move(rt);
	trash_.push_back(std::move(e));
}

void StormLockPart::TrashLockonVisuals() {
	for (auto& t : lockonTargets_) {
		for (int i = 0; i < kLockonLineCount; ++i) TrashRenderer(t.lines[i]);
		TrashRenderer(t.ring);
		TrashRenderer(t.cube);
	}
	lockonTargets_.clear();
}

void StormLockPart::LoadTuning(const JsonValue& sg) {
	// Phase 1
	barrierRadius_   = static_cast<float>(sg["stormLockBarrierRadius"].AsDouble(barrierRadius_));
	barrierDuration_ = static_cast<float>(sg["stormLockBarrierDuration"].AsDouble(barrierDuration_));
	barrierFillOn_   = sg["stormLockBarrierFill"].AsBool(barrierFillOn_);
	if (sg["stormLockBarrierEffect"].IsString()) barrierEffectName_ = sg["stormLockBarrierEffect"].AsString();
	// バリアのワイヤーフレーム球
	const JsonValue& wf = sg["stormLockBarrierWire"];
	if (wf.IsObject()) {
		barrierWireframeOn_   = wf["on"].AsBool(barrierWireframeOn_);
		const JsonValue& ss = wf["spinSpeed"];
		if (ss.IsArray() && ss.Size() >= kVector3ComponentCount) {
			barrierWireSpinSpeed_.x = static_cast<float>(ss[0].AsDouble(barrierWireSpinSpeed_.x));
			barrierWireSpinSpeed_.y = static_cast<float>(ss[1].AsDouble(barrierWireSpinSpeed_.y));
			barrierWireSpinSpeed_.z = static_cast<float>(ss[2].AsDouble(barrierWireSpinSpeed_.z));
		} else if (ss.IsNumber()) {
			// 旧フォーマット（単一値=Y軸のみ）との互換
			barrierWireSpinSpeed_.y = static_cast<float>(ss.AsDouble(barrierWireSpinSpeed_.y));
		}
		barrierWireMeridians_ = static_cast<int>(wf["meridians"].AsInt(static_cast<int64_t>(barrierWireMeridians_)));
		barrierWireParallels_ = static_cast<int>(wf["parallels"].AsInt(static_cast<int64_t>(barrierWireParallels_)));
		barrierWireSegments_  = static_cast<int>(wf["segments"].AsInt(static_cast<int64_t>(barrierWireSegments_)));
		auto readV4w = [](const JsonValue& a, Vector4& v) {
			if (a.IsArray() && a.Size() >= kVector4ComponentCount) {
				v.x = static_cast<float>(a[0].AsDouble(v.x));
				v.y = static_cast<float>(a[1].AsDouble(v.y));
				v.z = static_cast<float>(a[2].AsDouble(v.z));
				v.w = static_cast<float>(a[3].AsDouble(v.w));
			}
		};
		readV4w(wf["gold"], barrierWireColorGold_);
		readV4w(wf["pink"], barrierWireColorPink_);
	}
	// バリアパーティクル
	const JsonValue& bp = sg["stormLockBarrierParticle"];
	if (bp.IsObject()) {
		barrierParticleOn_   = bp["on"].AsBool(barrierParticleOn_);
		barrierEmitInterval_ = static_cast<float>(bp["interval"].AsDouble(barrierEmitInterval_));
		barrierEmitCount_    = static_cast<int>(bp["count"].AsInt(static_cast<int64_t>(barrierEmitCount_)));
		barrierParticleLife_ = static_cast<float>(bp["life"].AsDouble(barrierParticleLife_));
		barrierParticleRadiusScale_ = static_cast<float>(bp["radiusScale"].AsDouble(barrierParticleRadiusScale_));
		auto readV2 = [](const JsonValue& a, Vector2& v) {
			if (a.IsArray() && a.Size() >= kVector2ComponentCount) {
				v.x = static_cast<float>(a[0].AsDouble(v.x));
				v.y = static_cast<float>(a[1].AsDouble(v.y));
			}
		};
		auto readV4 = [](const JsonValue& a, Vector4& v) {
			if (a.IsArray() && a.Size() >= kVector4ComponentCount) {
				v.x = static_cast<float>(a[0].AsDouble(v.x));
				v.y = static_cast<float>(a[1].AsDouble(v.y));
				v.z = static_cast<float>(a[2].AsDouble(v.z));
				v.w = static_cast<float>(a[3].AsDouble(v.w));
			}
		};
		readV2(bp["scaleMin"], barrierParticleScaleMin_);
		readV2(bp["scaleMax"], barrierParticleScaleMax_);
		readV4(bp["color0"], barrierParticleColor0_);
		readV4(bp["color1"], barrierParticleColor1_);
	}
	// 光の翼（X字パーティクル）
	const JsonValue& wg = sg["stormLockWing"];
	if (wg.IsObject()) {
		wingOn_          = wg["on"].AsBool(wingOn_);
		wingArmCount_    = static_cast<int>(wg["armCount"].AsInt(static_cast<int64_t>(wingArmCount_)));
		wingAngleOffset_ = static_cast<float>(wg["angleOffset"].AsDouble(wingAngleOffset_));
		wingSpeed_       = static_cast<float>(wg["speed"].AsDouble(wingSpeed_));
		wingLife_        = static_cast<float>(wg["life"].AsDouble(wingLife_));
		wingBurstCount_  = static_cast<int>(wg["burstCount"].AsInt(static_cast<int64_t>(wingBurstCount_)));
		wingJitter_      = static_cast<float>(wg["jitter"].AsDouble(wingJitter_));
		wingEmitRadius_  = static_cast<float>(wg["emitRadius"].AsDouble(wingEmitRadius_));
		auto readV2w = [](const JsonValue& a, Vector2& v) {
			if (a.IsArray() && a.Size() >= kVector2ComponentCount) {
				v.x = static_cast<float>(a[0].AsDouble(v.x));
				v.y = static_cast<float>(a[1].AsDouble(v.y));
			}
		};
		auto readV4w2 = [](const JsonValue& a, Vector4& v) {
			if (a.IsArray() && a.Size() >= kVector4ComponentCount) {
				v.x = static_cast<float>(a[0].AsDouble(v.x));
				v.y = static_cast<float>(a[1].AsDouble(v.y));
				v.z = static_cast<float>(a[2].AsDouble(v.z));
				v.w = static_cast<float>(a[3].AsDouble(v.w));
			}
		};
		readV2w(wg["scaleMin"], wingScaleMin_);
		readV2w(wg["scaleMax"], wingScaleMax_);
		readV4w2(wg["colorInner"], wingColorInner_);
		readV4w2(wg["colorOuter"], wingColorOuter_);
	}
	const JsonValue& bc = sg["stormLockBarrierColor"];
	if (bc.IsArray() && bc.Size() >= kVector4ComponentCount) {
		barrierColor_ = {
			static_cast<float>(bc[0].AsDouble(barrierColor_.x)),
			static_cast<float>(bc[1].AsDouble(barrierColor_.y)),
			static_cast<float>(bc[2].AsDouble(barrierColor_.z)),
			static_cast<float>(bc[3].AsDouble(barrierColor_.w))
		};
	}
	camPullback_ = static_cast<float>(sg["stormLockCamPullback"].AsDouble(camPullback_));
	camFovAdd_   = static_cast<float>(sg["stormLockCamFovAdd"].AsDouble(camFovAdd_));
	camUpAdd_    = static_cast<float>(sg["stormLockCamUpAdd"].AsDouble(camUpAdd_));
	// Phase 2 チャージ
	chargeBoltCount_     = static_cast<int>(sg["stormLockChargeBoltCount"].AsInt(static_cast<int64_t>(chargeBoltCount_)));
	chargeRegenInterval_ = static_cast<float>(sg["stormLockChargeRegenInterval"].AsDouble(chargeRegenInterval_));
	chargeMinLength_     = static_cast<float>(sg["stormLockChargeMinLength"].AsDouble(chargeMinLength_));
	// Phase 3 Fire
	fireSimultaneous_   = static_cast<int>(sg["stormLockFireSimultaneous"].AsInt(static_cast<int64_t>(fireSimultaneous_)));
	fireMinHold_        = static_cast<float>(sg["stormLockFireMinHold"].AsDouble(fireMinHold_));
	fireMaxHold_        = static_cast<float>(sg["stormLockFireMaxHold"].AsDouble(fireMaxHold_));
	fireTickInterval_   = static_cast<float>(sg["stormLockFireTickInterval"].AsDouble(fireTickInterval_));
	fireTickDamage_     = static_cast<int>(sg["stormLockFireTickDamage"].AsInt(static_cast<int64_t>(fireTickDamage_)));
	fireLaunchInterval_ = static_cast<float>(sg["stormLockFireLaunchInterval"].AsDouble(fireLaunchInterval_));
	fireGrowTime_       = static_cast<float>(sg["stormLockFireGrowTime"].AsDouble(fireGrowTime_));
	fireMaxOffsetRatio_ = static_cast<float>(sg["stormLockFireMaxOffsetRatio"].AsDouble(fireMaxOffsetRatio_));
	fireBranchProb_     = static_cast<float>(sg["stormLockFireBranchProb"].AsDouble(fireBranchProb_));
	fireStartWidth_     = static_cast<float>(sg["stormLockFireStartWidth"].AsDouble(fireStartWidth_));
	fireEndWidth_       = static_cast<float>(sg["stormLockFireEndWidth"].AsDouble(fireEndWidth_));
	fireBoltLifetime_   = static_cast<float>(sg["stormLockFireBoltLifetime"].AsDouble(fireBoltLifetime_));
	endDuration_        = static_cast<float>(sg["stormLockEndDuration"].AsDouble(endDuration_));
	const JsonValue& fc = sg["stormLockFireColor"];
	if (fc.IsArray() && fc.Size() >= kVector4ComponentCount) {
		fireColor_ = {
			static_cast<float>(fc[0].AsDouble(fireColor_.x)),
			static_cast<float>(fc[1].AsDouble(fireColor_.y)),
			static_cast<float>(fc[2].AsDouble(fireColor_.z)),
			static_cast<float>(fc[3].AsDouble(fireColor_.w))
		};
	}
}

void StormLockPart::SaveTuning(JsonValue& sgObj) const {
	sgObj["stormLockBarrierRadius"]   = static_cast<double>(barrierRadius_);
	sgObj["stormLockBarrierDuration"] = static_cast<double>(barrierDuration_);
	sgObj["stormLockBarrierFill"]     = JsonValue(barrierFillOn_);
	sgObj["stormLockBarrierEffect"]   = barrierEffectName_;
	{
		JsonValue wfObj = JsonValue::MakeObject();
		wfObj["on"]        = JsonValue(barrierWireframeOn_);
		{
			JsonValue ss = JsonValue::MakeArray();
			ss.Push(JsonValue(static_cast<double>(barrierWireSpinSpeed_.x)));
			ss.Push(JsonValue(static_cast<double>(barrierWireSpinSpeed_.y)));
			ss.Push(JsonValue(static_cast<double>(barrierWireSpinSpeed_.z)));
			wfObj["spinSpeed"] = std::move(ss);
		}
		wfObj["meridians"] = static_cast<int64_t>(barrierWireMeridians_);
		wfObj["parallels"] = static_cast<int64_t>(barrierWireParallels_);
		wfObj["segments"]  = static_cast<int64_t>(barrierWireSegments_);
		auto pushV4w = [](const Vector4& v) {
			JsonValue arr = JsonValue::MakeArray();
			arr.Push(JsonValue(static_cast<double>(v.x)));
			arr.Push(JsonValue(static_cast<double>(v.y)));
			arr.Push(JsonValue(static_cast<double>(v.z)));
			arr.Push(JsonValue(static_cast<double>(v.w)));
			return arr;
		};
		wfObj["gold"] = pushV4w(barrierWireColorGold_);
		wfObj["pink"] = pushV4w(barrierWireColorPink_);
		sgObj["stormLockBarrierWire"] = std::move(wfObj);
	}
	{
		JsonValue bpObj = JsonValue::MakeObject();
		bpObj["on"]          = JsonValue(barrierParticleOn_);
		bpObj["interval"]    = static_cast<double>(barrierEmitInterval_);
		bpObj["count"]       = static_cast<int64_t>(barrierEmitCount_);
		bpObj["life"]        = static_cast<double>(barrierParticleLife_);
		bpObj["radiusScale"] = static_cast<double>(barrierParticleRadiusScale_);
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
		bpObj["scaleMin"] = pushV2(barrierParticleScaleMin_);
		bpObj["scaleMax"] = pushV2(barrierParticleScaleMax_);
		bpObj["color0"]   = pushV4b(barrierParticleColor0_);
		bpObj["color1"]   = pushV4b(barrierParticleColor1_);
		sgObj["stormLockBarrierParticle"] = std::move(bpObj);
	}
	{
		JsonValue wgObj = JsonValue::MakeObject();
		wgObj["on"]          = JsonValue(wingOn_);
		wgObj["armCount"]    = static_cast<int64_t>(wingArmCount_);
		wgObj["angleOffset"] = static_cast<double>(wingAngleOffset_);
		wgObj["speed"]       = static_cast<double>(wingSpeed_);
		wgObj["life"]        = static_cast<double>(wingLife_);
		wgObj["burstCount"]  = static_cast<int64_t>(wingBurstCount_);
		wgObj["jitter"]      = static_cast<double>(wingJitter_);
		wgObj["emitRadius"]  = static_cast<double>(wingEmitRadius_);
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
		wgObj["scaleMin"]   = pushV2w(wingScaleMin_);
		wgObj["scaleMax"]   = pushV2w(wingScaleMax_);
		wgObj["colorInner"] = pushV4w2(wingColorInner_);
		wgObj["colorOuter"] = pushV4w2(wingColorOuter_);
		sgObj["stormLockWing"] = std::move(wgObj);
	}
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(barrierColor_.x)));
		arr.Push(JsonValue(static_cast<double>(barrierColor_.y)));
		arr.Push(JsonValue(static_cast<double>(barrierColor_.z)));
		arr.Push(JsonValue(static_cast<double>(barrierColor_.w)));
		sgObj["stormLockBarrierColor"] = std::move(arr);
	}
	sgObj["stormLockCamPullback"] = static_cast<double>(camPullback_);
	sgObj["stormLockCamFovAdd"]   = static_cast<double>(camFovAdd_);
	sgObj["stormLockCamUpAdd"]    = static_cast<double>(camUpAdd_);
	// Phase 2 チャージ
	sgObj["stormLockChargeBoltCount"]     = static_cast<int64_t>(chargeBoltCount_);
	sgObj["stormLockChargeRegenInterval"] = static_cast<double>(chargeRegenInterval_);
	sgObj["stormLockChargeMinLength"]     = static_cast<double>(chargeMinLength_);
	// Phase 3 Fire
	sgObj["stormLockFireSimultaneous"]   = static_cast<int64_t>(fireSimultaneous_);
	sgObj["stormLockFireMinHold"]        = static_cast<double>(fireMinHold_);
	sgObj["stormLockFireMaxHold"]        = static_cast<double>(fireMaxHold_);
	sgObj["stormLockFireTickInterval"]   = static_cast<double>(fireTickInterval_);
	sgObj["stormLockFireTickDamage"]     = static_cast<int64_t>(fireTickDamage_);
	sgObj["stormLockFireLaunchInterval"] = static_cast<double>(fireLaunchInterval_);
	sgObj["stormLockFireGrowTime"]       = static_cast<double>(fireGrowTime_);
	sgObj["stormLockFireMaxOffsetRatio"] = static_cast<double>(fireMaxOffsetRatio_);
	sgObj["stormLockFireBranchProb"]     = static_cast<double>(fireBranchProb_);
	sgObj["stormLockFireStartWidth"]     = static_cast<double>(fireStartWidth_);
	sgObj["stormLockFireEndWidth"]       = static_cast<double>(fireEndWidth_);
	sgObj["stormLockFireBoltLifetime"]   = static_cast<double>(fireBoltLifetime_);
	sgObj["stormLockEndDuration"]        = static_cast<double>(endDuration_);
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(fireColor_.x)));
		arr.Push(JsonValue(static_cast<double>(fireColor_.y)));
		arr.Push(JsonValue(static_cast<double>(fireColor_.z)));
		arr.Push(JsonValue(static_cast<double>(fireColor_.w)));
		sgObj["stormLockFireColor"] = std::move(arr);
	}
}

// ===== 段取り（Barrier → Lockon → Fire → End） =====================================

void StormLockPart::EnterBarrierPhase() {
	phase_      = Phase::Barrier;
	phaseTimer_ = 0.0f;

	// バリア可視化プリミティブ（半透明青の球、Blend=Normal、両面、深度書き込みなし）
	barrierVis_ = std::make_unique<EffectPrimitiveRenderer>();
	barrierVis_->Initialize(EffectMeshType::Sphere, "Resources/Textures/white1x1.dds");
	barrierVis_->SetBlendMode(PrimitivePipeline::kBlendModeNormal);
	barrierVis_->SetDepthWrite(false);
	barrierVis_->SetCullBackface(false);
	barrierVis_->SetColor(barrierColor_);
	const float diameter = barrierRadius_ * 2.0f; // CreateSphere は radius=0.5 既定なので
	barrierVis_->SetScale({ diameter, diameter, diameter });

	// バリアのパーティクル群を用意（球状エミッタ）。時間停止中も動くよう TimeGroup=Player。
	if (auto* gpu = Game::GetGPUParticleManager()) {
		if (!gpu->HasGroup("special_barrier")) {
			gpu->CreateGroup("special_barrier", "Resources/Textures/circle.dds");
		}
		gpu->SetGroupBillboardMode("special_barrier", BillboardMode::Full);
		gpu->SetGroupTimeGroup("special_barrier", TimeGroup::Player);
		barrierGroupReady_ = true;
	}
	barrierEmitAccum_ = 0.0f;
	barrierWireSpin_  = { 0.0f, 0.0f, 0.0f };

	// バリア本体エフェクト（エディタ製 "Barrier" 等）を再生。登録済みならワイヤー球の代わりにこれを使う。
	barrierEffectHandle_ = kInvalidEffectHandle;
	if (auto* em = EffectManager::GetInstance()) {
		if (!barrierEffectName_.empty() && em->HasDef(barrierEffectName_)) {
			barrierEffectHandle_ = em->Play(barrierEffectName_, host_->GetSpecialPlayerCenter());
		}
	}
}

void StormLockPart::UpdateBarrierPhase(float worldDt) {
	(void)worldDt;
	// 追従・当たり判定は UpdateBarrier（全フェーズ共通）で実施。
	// ここは Phase 1 のフェーズ遷移判定のみ。
	// 3 秒経過で Phase 2（Lockon）へ遷移
	if (phaseTimer_ >= barrierDuration_) {
		EnterLockonPhase();
	}
}

void StormLockPart::UpdateBarrier() {
	if (!host_->GetPlayer()) return;

	// バリア球の中心 = プレイヤー体の中心（足元原点 + offset）。毎フレ追従。
	// Fire 中はレールカメラが進行してプレイヤーのワールド位置も前進するため、
	// 全フェーズ共通でここで追従させないと球と当たり判定が置いて行かれる。
	const Vector3 playerPos = host_->GetSpecialPlayerCenter();
	if (barrierVis_) {
		barrierVis_->SetTranslate(playerPos);
	}

	// バリア当たり判定：球内に侵入した敵弾を消す（距離判定）。吸った量は使わない
	host_->AbsorbEnemyBulletsInSphere(playerPos, barrierRadius_);

	// 実時間 dt（時間停止中もバリア演出は等速で進める）
	const float rdt = realDt_;

	// バリア本体エフェクト再生中：プレイヤー中心へ追従。ワイヤー球は出さない。
	const bool barrierEffectActive = (barrierEffectHandle_ != kInvalidEffectHandle);
	if (barrierEffectActive) {
		if (auto* em = EffectManager::GetInstance()) {
			em->SetPosition(barrierEffectHandle_, playerPos);
		}
	} else {
		// フォールバック：回転ワイヤーフレーム球（X/Y/Z 3軸で回す）
		barrierWireSpin_.x += barrierWireSpinSpeed_.x * rdt;
		barrierWireSpin_.y += barrierWireSpinSpeed_.y * rdt;
		barrierWireSpin_.z += barrierWireSpinSpeed_.z * rdt;
		if (barrierWireframeOn_) {
			DrawBarrierWireframe(playerPos);
		}
	}

	// バリアのパーティクル：球面内に連続バースト（実時間で進めるので時間停止中もシマー）
	if (barrierParticleOn_ && barrierGroupReady_) {
		if (auto* gpu = Game::GetGPUParticleManager()) {
			const float interval = (barrierEmitInterval_ > kMinEmitInterval) ? barrierEmitInterval_ : kFallbackEmitInterval;
			const float emitRadius = barrierRadius_ * barrierParticleRadiusScale_;
			const uint32_t count = static_cast<uint32_t>((std::max)(1, barrierEmitCount_));
			gpu->SetEmitterTranslate("special_barrier", playerPos);
			barrierEmitAccum_ += rdt;
			int guard = 0; // 1フレームに出し過ぎない安全弁
			while (barrierEmitAccum_ >= interval && guard < kMaxBurstsPerFrame) {
				barrierEmitAccum_ -= interval;
				++guard;
				gpu->BurstEmit("special_barrier", playerPos, count, emitRadius,
					GPUParticleManager::kColorModeFixed, barrierParticleColor0_, barrierParticleColor1_,
					barrierParticleScaleMin_, barrierParticleScaleMax_, true,
					barrierParticleLife_);
			}
		}
	}
}

void StormLockPart::DrawBarrierWireframe(const Vector3& center) {
	auto* lr = LineRenderer::GetInstance();
	if (!lr) return;

	const float R   = barrierRadius_;
	const int meridians = (std::max)(1, barrierWireMeridians_);
	const int parallels = (std::max)(1, barrierWireParallels_);
	const int seg       = (std::max)(kMinWireSegments, barrierWireSegments_);

	// 3軸回転行列。ローカル球面点に適用してから center に足す。
	const Matrix4x4 rot = MakeRotateMatrix(barrierWireSpin_);
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
			if (i > 0) lr->AddLine(prev, p, barrierWireColorGold_);
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
			if (i > 0) lr->AddLine(prev, p, barrierWireColorPink_);
			prev = p;
		}
	}
}

void StormLockPart::UpdateWings() {
	if (!wingOn_) return;
	auto* gpu = Game::GetGPUParticleManager();
	if (!gpu) return;

	const int arms = (std::max)(1, wingArmCount_);
	const Vector3 center = host_->GetSpecialPlayerCenter();
	// X字方向はカメラ平面（画面）基準。camRight/camUp で画面内の角度を作る。
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
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
		const float a = wingAngleOffset_ + static_cast<float>(i) / arms * 2.0f * kPi;
		const Vector3 dir = {
			std::cos(a) * camRight.x + std::sin(a) * camUp.x,
			std::cos(a) * camRight.y + std::sin(a) * camUp.y,
			std::cos(a) * camRight.z + std::sin(a) * camUp.z,
		};
		const Vector3 vel = { dir.x * wingSpeed_, dir.y * wingSpeed_, dir.z * wingSpeed_ };
		gpu->SetEmitterVelocity(name, vel, wingJitter_, GPUParticleManager::kVelocityDirectional); // 外向き初速
		// 色は固定のグラデーション（金→ピンク）
		gpu->BurstEmit(name, center,
			static_cast<uint32_t>((std::max)(1, wingBurstCount_)), wingEmitRadius_,
			GPUParticleManager::kColorModeFixed, wingColorInner_, wingColorOuter_,
			wingScaleMin_, wingScaleMax_, true, wingLife_);
	}
}

void StormLockPart::EnterLockonPhase() {
	phase_ = Phase::Lockon;
	phaseTimer_ = 0.0f;
	lockonTimer_ = 0.0f;
	allLockonComplete_ = false;
	chargeStartTime_ = 0.0f;

	// ワールド時間停止（A案：既存のジャスト回避と同じ仕組み）
	host_->SetTimeScale(TimeGroup::World, 0.0f);

	// ロックオン対象列挙
	lockonTargets_.clear();
	EnumerateLockonTargets();
}

void StormLockPart::EnumerateLockonTargets() {
	IImGuiEditable* player = host_->GetPlayer();
	const Vector3* pp = player ? player->GetEditableTranslate() : nullptr;
	if (!camera_ || !pp) return;
	const Vector3 playerPos = *pp;

	struct Candidate {
		IImGuiEditable* entity;
		bool isBullet;
		float dist2;
		float radius;
	};
	std::vector<Candidate> cands;
	std::vector<std::pair<IImGuiEditable*, bool>> screen;
	CollectScreenTargets(screen);
	cands.reserve(screen.size());
	for (const auto& [e, isBullet] : screen) {
		const Vector3* p = e->GetEditableTranslate();
		const float dx = p->x - playerPos.x, dy = p->y - playerPos.y, dz = p->z - playerPos.z;
		cands.push_back({ e, isBullet, dx * dx + dy * dy + dz * dz, Gameplay::Of(e).GetCollider().radius });
	}

	// プレイヤーから近い順にソート
	std::sort(cands.begin(), cands.end(),
		[](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });

	lockonTargets_.reserve(cands.size());
	for (size_t i = 0; i < cands.size(); ++i) {
		LockonTarget t;
		t.entity = cands[i].entity;
		t.isBullet = cands[i].isBullet;
		t.patternIndex = static_cast<int>(i % kLockonPatternCount); // 交互に A/B
		t.startTime = static_cast<float>(i) * lockonInterval_;
		t.radius = (cands[i].radius > kMinTargetRadius) ? cands[i].radius : kFallbackTargetRadius;
		lockonTargets_.push_back(std::move(t));
	}
}

void StormLockPart::SpawnLockonVisuals(LockonTarget& t) {
	if (t.visualsCreated) return;

	// 3本の線（薄い Plane、Full Billboard、Blend=Add で発光感）
	for (int i = 0; i < kLockonLineCount; ++i) {
		t.lines[i] = std::make_unique<EffectPrimitiveRenderer>();
		t.lines[i]->Initialize(EffectMeshType::Plane, "Resources/Textures/white1x1.dds");
		t.lines[i]->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
		t.lines[i]->SetBillboardMode(BillboardMode::Full);
		t.lines[i]->SetDepthWrite(false);
		t.lines[i]->SetCullBackface(false);
		t.lines[i]->SetColor(lockonColor_);
		t.lines[i]->SetScale({ 0.0f, 0.0f, 1.0f }); // 初期は非表示
	}

	// リング（Y軸ビルボードはNG＝Fullで正面）
	{
		PrimitiveGenerator::RingParams rp{};
		rp.outerRadius = 1.0f;
		rp.innerRadius = 1.0f - lockonRingThickness_;
		rp.divisions = kLockonRingDivisions;
		rp.innerColor = lockonColor_;
		rp.outerColor = lockonColor_;
		t.ring = std::make_unique<EffectPrimitiveRenderer>();
		t.ring->Initialize(EffectMeshType::Ring, "Resources/Textures/white1x1.dds", rp);
		t.ring->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
		t.ring->SetBillboardMode(BillboardMode::Full);
		t.ring->SetDepthWrite(false);
		t.ring->SetCullBackface(false);
		t.ring->SetColor(lockonColor_);
		t.ring->SetScale({ 0.0f, 0.0f, 0.0f });
	}

	// キューブ（半透明オレンジ、Blend=Normal、両面）
	{
		t.cube = std::make_unique<EffectPrimitiveRenderer>();
		t.cube->Initialize(EffectMeshType::Box, "Resources/Textures/white1x1.dds");
		t.cube->SetBlendMode(PrimitivePipeline::kBlendModeNormal);
		t.cube->SetDepthWrite(false);
		t.cube->SetCullBackface(false);
		t.cube->SetColor(cubeColor_);
		t.cube->SetScale({ 0.0f, 0.0f, 0.0f });
	}

	t.visualsCreated = true;
}

void StormLockPart::UpdateLockonTargetVisuals(LockonTarget& t, float localTime) {
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
	const float lineLife = lockonLineTravelTime_ + lockonLineHoldTime_; // 0.3
	const bool  lineVisible = (localTime >= 0.0f && localTime < lineLife);

	// 3 つの角度（パターンA: 90°/210°/330°、パターンB: 270°/30°/150°）
	const float baseAngles[3] = { 0.5f * kPi, 7.0f / 6.0f * kPi, 11.0f / 6.0f * kPi };
	const float patternOffset = (t.patternIndex == 1) ? kPi : 0.0f; // パターンBは180°回転

	// カメラ基底（線・リングの向き計算用）
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
	const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
	const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };

	float innerDist;
	if (localTime <= lockonLineTravelTime_) {
		const float u = localTime / lockonLineTravelTime_;
		innerDist = r * kLineStartRadiusRatio * (1.0f - u);
	} else {
		innerDist = 0.0f;
	}
	const float outerDist = innerDist + lineLen;
	const float midDist   = (innerDist + outerDist) * 0.5f;

	for (int i = 0; i < kLockonLineCount; ++i) {
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
		t.lines[i]->SetScale({ lockonLineWidth_, lineLen, 1.0f });
		Vector4 col = lockonColor_;
		// 0.2〜0.3 秒は到達後の余韻でαを薄くしてフェード
		if (localTime > lockonLineTravelTime_) {
			const float fade = 1.0f - (localTime - lockonLineTravelTime_) / lockonLineHoldTime_;
			col.w = (std::max)(0.0f, fade) * lockonColor_.w;
		}
		t.lines[i]->SetColor(col);
	}

	// リング：t∈[0.2, 0.6] で表示。サイズは ジャスト(r) → r*0.3 に収縮（0.2〜0.5）、0.5〜0.6 でフェードアウト
	const float ringAppear  = lockonRingAppearTime_;            // 0.2
	const float ringComplete= lockonDuration_;                  // 0.5
	const float ringFadeEnd = lockonDuration_ + lockonRingFadeoutTime_; // 0.6
	if (t.ring) {
		if (localTime < ringAppear || localTime > ringFadeEnd) {
			t.ring->SetScale({ 0.0f, 0.0f, 0.0f });
		} else {
			float ringRadius;
			if (localTime <= ringComplete) {
				const float u = (localTime - ringAppear) / (ringComplete - ringAppear);
				ringRadius = r * (1.0f - u) + r * kRingEndRadiusRatio * u;
			} else {
				ringRadius = r * kRingEndRadiusRatio;
			}
			t.ring->SetTranslate(enemyPos);
			t.ring->SetScale({ ringRadius, ringRadius, ringRadius });
			Vector4 col = lockonColor_;
			if (localTime > ringComplete) {
				const float fade = 1.0f - (localTime - ringComplete) / lockonRingFadeoutTime_;
				col.w = (std::max)(0.0f, fade) * lockonColor_.w;
			}
			t.ring->SetColor(col);
		}
	}

	// キューブ：ロック完了（0.5秒）以降ターゲットが消えるまで継続
	if (t.cube) {
		if (localTime < lockonDuration_) {
			t.cube->SetScale({ 0.0f, 0.0f, 0.0f });
		} else {
			const float side = r * 2.0f;
			t.cube->SetTranslate(enemyPos);
			t.cube->SetScale({ side, side, side });
			t.cube->SetColor(cubeColor_);
		}
	}

	// Update（CB書き込み）。dt=0 でも位置/scale/color は反映される
	const float realDt = realDt_;
	for (int i = 0; i < kLockonLineCount; ++i) if (t.lines[i]) t.lines[i]->Update(camera_, realDt);
	if (t.ring) t.ring->Update(camera_, realDt);
	if (t.cube) t.cube->Update(camera_, realDt);
}

void StormLockPart::UpdateLockonPhase(float realDt) {
	lockonTimer_ += realDt;

	// 各ターゲットの可視化生成・更新
	for (auto& t : lockonTargets_) {
		// ターゲットが消えていたらスキップ（敵が破壊済み or 弾消滅）
		if (!t.entity) continue;
		// 弾は remainingLifetime<0 で消滅予定だが Phase 2 では World=0 なので生きてる想定

		const float localTime = lockonTimer_ - t.startTime;
		if (localTime < 0.0f) continue; // まだ開始してない

		if (!t.visualsCreated) {
			SpawnLockonVisuals(t);
		}
		UpdateLockonTargetVisuals(t, localTime);
	}

	// 全ターゲットのロック完了タイミング = 最後のターゲットの startTime + Duration
	if (!allLockonComplete_) {
		bool justCompleted = false;
		if (lockonTargets_.empty()) {
			allLockonComplete_ = true;
			chargeStartTime_ = lockonTimer_;
			justCompleted = true;
		} else {
			const float lastStart = lockonTargets_.back().startTime;
			if (lockonTimer_ >= lastStart + lockonDuration_) {
				allLockonComplete_ = true;
				chargeStartTime_ = lockonTimer_;
				justCompleted = true;
			}
		}
		// チャージ電撃の本数ぶん LightningRuntime を起動
		if (justCompleted) {
			chargeBolts_.clear();
			chargeBolts_.reserve(chargeBoltCount_);
			for (int i = 0; i < chargeBoltCount_; ++i) {
				ChargeBolt b;
				b.rt = std::make_unique<LightningRuntime>();
				PrimitiveGenerator::LightningBoltParams lp{};
				lp.appearance.startWidth = kChargeBoltWidth;
				lp.appearance.endWidth   = kChargeBoltWidth;
				lp.appearance.planeCount = kBoltPlaneCount;
				lp.appearance.fadeStartLength = kBoltFadeLength;
				lp.appearance.fadeEndLength   = kBoltFadeLength;
				lp.appearance.startColor = lockonColor_;
				lp.appearance.endColor   = lockonColor_;
				lp.generations = kChargeBoltGenerations;
				lp.maxOffsetRatio = kChargeBoltMaxOffsetRatio;
				lp.branchProbability = kChargeBoltBranchProbability;
				lp.branchLengthScale = kChargeBoltBranchLengthScale;
				b.rt->Initialize(lp, "Resources/Textures/white1x1.dds", PrimitivePipeline::kBlendModeAdd);
				b.rt->SetBoltLifetime(chargeRegenInterval_);
				b.rt->SetOverlapOffset(chargeRegenInterval_ * 0.5f);
				// 各本に再生成タイミングをずらして配置（同時に全部点滅しないように）
				b.nextRegenTime = lockonTimer_
					+ static_cast<float>(i) * (chargeRegenInterval_ / chargeBoltCount_);
				chargeBolts_.push_back(std::move(b));
			}
		}
	}

	// チャージ進行（全ロック完了後）
	if (allLockonComplete_) {
		const float chargeT = lockonTimer_ - chargeStartTime_;
		const float u = (std::clamp)(chargeT / chargeDuration_, 0.0f, 1.0f);

		// プレイヤー位置と始終点半径
		// 始点はプレイヤーカプセル表面（スクリーン投影楕円）。
		// 終点はバリア表面方向（チャージ進行 u で成長、最小長で下限を確保）
		const Vector3 ppos = host_->GetSpecialPlayerCenter();
		// 自機の体を囲む楕円の半軸（シーンの共通設定。0 なら collider から自動算出）
		float ellipseA = 0.0f, ellipseB = 0.0f;
		host_->GetSpecialBodyEllipse(ellipseA, ellipseB);

		// カメラ基底
		const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_ ? camera_->GetRotate() : Vector3{ 0.0f, 0.0f, 0.0f });
		const Vector3 camRight = { camRot.m[0][0], camRot.m[0][1], camRot.m[0][2] };
		const Vector3 camUp    = { camRot.m[1][0], camRot.m[1][1], camRot.m[1][2] };

		// 乱数源
		static thread_local std::mt19937 rng(
			static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * kPi);

		for (auto& b : chargeBolts_) {
			if (!b.rt) continue;

			// 再生成タイミングに到達したら角度をランダムに更新
			if (lockonTimer_ >= b.nextRegenTime) {
				b.angleBase = distAngle(rng);
				b.nextRegenTime += chargeRegenInterval_;
				if (b.nextRegenTime < lockonTimer_) {
					b.nextRegenTime = lockonTimer_ + chargeRegenInterval_;
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
			const float startR = (denom > kMinEllipseDenom) ? (ellipseA * ellipseB) / denom : ellipseA;
			// 終点：チャージ進行に応じた半径（最小長下限）
			const float endR = (std::max)(startR + chargeMinLength_, barrierRadius_ * u);

			Vector3 s = { ppos.x + dir.x * startR, ppos.y + dir.y * startR, ppos.z + dir.z * startR };
			Vector3 e = { ppos.x + dir.x * endR,   ppos.y + dir.y * endR,   ppos.z + dir.z * endR   };
			b.rt->SetEndpoints(s, e);
			b.rt->Update(camera_, realDt);
		}

		// チャージ完了で Fire フェーズへ
		if (chargeT >= chargeDuration_) {
			EnterFirePhase();
		}
	}
}

void StormLockPart::EnterFirePhase() {
	phase_      = Phase::Fire;
	phaseTimer_ = 0.0f;

	// World 時間を再開（Lockon で 0 にしていた）
	host_->SetTimeScale(TimeGroup::World, 1.0f);

	// 処理待ちキューをロック済みターゲットから構築（近い順は EnumerateLockonTargets でソート済み）
	fireQueue_.clear();
	fireProcessed_.clear();
	for (auto& t : lockonTargets_) {
		if (!t.entity) continue;
		if (!fireProcessed_.insert(t.entity).second) continue;
		FireQueueItem item;
		item.entity   = t.entity;
		item.isBullet = t.isBullet;
		fireQueue_.push_back(item);
	}

	// Phase 2 の可視化（線・リング・キューブ）とチャージ電撃は役目を終えたので trash へ
	for (auto& b : chargeBolts_) {
		TrashLightning(b.rt);
	}
	chargeBolts_.clear();
	TrashLockonVisuals();

	fireBolts_.clear();
	fireLaunchTimer_ = 0.0f; // 1 本目は即発射
}

void StormLockPart::UpdateFirePhase(float dt) {
	fireLaunchTimer_ -= dt;

	const Vector3 ppos = host_->GetSpecialPlayerCenter();

	// ----- 新規侵入の検出：画面内に新しく入った敵/敵弾を queue に追加 -----
	{
		std::vector<std::pair<IImGuiEditable*, bool>> screen;
		CollectScreenTargets(screen);
		// 現在アクティブな bolt のターゲット集合（重複防止）
		std::unordered_set<IImGuiEditable*> activeSet;
		for (auto& fb : fireBolts_) if (fb.entity) activeSet.insert(fb.entity);
		for (auto& s : screen) {
			IImGuiEditable* e = s.first;
			if (fireProcessed_.count(e)) continue; // 既処理（発射済み）
			if (activeSet.count(e)) continue;             // 発射中
			// queue 内の重複
			bool inQueue = false;
			for (auto& q : fireQueue_) { if (q.entity == e) { inQueue = true; break; } }
			if (inQueue) continue;
			FireQueueItem item; item.entity = e; item.isBullet = s.second;
			fireQueue_.push_back(item);
		}
	}

	// ----- 空き枠があり、発射間隔を満たしたら queue から 1 本発射 -----
	if (fireLaunchTimer_ <= 0.0f &&
		static_cast<int>(fireBolts_.size()) < fireSimultaneous_) {
		// 生存している先頭ターゲットを探す
		while (!fireQueue_.empty()) {
			FireQueueItem item = fireQueue_.front();
			fireQueue_.erase(fireQueue_.begin());
			// 弾は primitive ポインタ、敵は entity ポインタ。いずれも動的コンテナで生存確認できる
			if (!host_->IsEnemyAlive(item.entity)) continue; // 既に消滅 → スキップ
			fireProcessed_.insert(item.entity);

			FireBolt fb;
			fb.entity   = item.entity;
			fb.isBullet = item.isBullet;
			fb.elapsed  = 0.0f;
			fb.nextTickTime = 0.0f; // 開始直後に 1 ティック目
			fb.rt = std::make_unique<LightningRuntime>();
			PrimitiveGenerator::LightningBoltParams lp{};
			lp.appearance.startWidth = fireStartWidth_;
			lp.appearance.endWidth   = fireEndWidth_;
			lp.appearance.planeCount = kBoltPlaneCount;
			lp.appearance.fadeStartLength = kBoltFadeLength;
			lp.appearance.fadeEndLength   = kBoltFadeLength;
			lp.appearance.startColor = fireColor_;
			lp.appearance.endColor   = fireColor_;
			lp.generations = kFireBoltGenerations;
			lp.maxOffsetRatio = fireMaxOffsetRatio_;
			lp.branchProbability = fireBranchProb_;
			lp.branchLengthScale = kFireBoltBranchLengthScale;
			fb.rt->Initialize(lp, "Resources/Textures/white1x1.dds", PrimitivePipeline::kBlendModeAdd);
			fb.rt->SetBoltLifetime(fireBoltLifetime_);
			fb.rt->SetOverlapOffset(fireBoltLifetime_ * 0.5f);
			fireBolts_.push_back(std::move(fb));
			fireLaunchTimer_ = fireLaunchInterval_;
			break; // 1 フレームに発射するのは 1 本まで
		}
	}

	// ----- 各 bolt の更新（追従・ダメージティック・終了判定）-----
	for (auto& fb : fireBolts_) {
		if (fb.done) continue;
		if (!fb.rt) { fb.done = true; continue; }

		// ターゲット生存確認（World 再開中は sweep でポインタが無効化されうる）
		if (!host_->IsEnemyAlive(fb.entity)) { fb.done = true; continue; }
		const Vector3* tp = fb.entity->GetEditableTranslate();
		if (!tp) { fb.done = true; continue; }
		fb.lastTargetPos = *tp;

		fb.elapsed += dt;

		// サンダーの始点（プレイヤーカプセル表面）と終点（ターゲット現在位置）。
		// 発射直後は始点→ターゲットへ伸ばす（敵へ飛んでいく感）。grow 完了後は終点を追従。
		Vector3 dir = { fb.lastTargetPos.x - ppos.x, fb.lastTargetPos.y - ppos.y, fb.lastTargetPos.z - ppos.z };
		const float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (dlen > kMinDirectionLength) { dir.x /= dlen; dir.y /= dlen; dir.z /= dlen; }
		else { dir = { 0.0f, 1.0f, 0.0f }; }
		const Vector3 s = host_->GetSpecialMuzzle(ppos, dir);
		float gu = (fireGrowTime_ > kMinGrowSeconds)
			? (std::clamp)(fb.elapsed / fireGrowTime_, 0.0f, 1.0f)
			: 1.0f;
		if (gu < kMinGrowRatio) gu = kMinGrowRatio; // 0 長（空メッシュ）回避＆最低限の見える長さ確保
		const Vector3 renderEnd = {
			s.x + (fb.lastTargetPos.x - s.x) * gu,
			s.y + (fb.lastTargetPos.y - s.y) * gu,
			s.z + (fb.lastTargetPos.z - s.z) * gu,
		};
		fb.rt->SetEndpoints(s, renderEnd);
		fb.rt->Update(camera_, dt);

		// ダメージティック
		while (fb.elapsed >= fb.nextTickTime) {
			if (!fb.isBullet) {
				Gameplay::Of(fb.entity).GetHP().TakeDamage(fireTickDamage_);
			}
			fb.nextTickTime += fireTickInterval_;
		}

		// 終了判定
		if (fb.isBullet) {
			// 敵弾：最低拘束時間で強制消滅
			if (fb.elapsed >= fireMinHold_) {
				host_->DestroyEnemyBullet(fb.entity);
				fb.done = true;
			}
		} else {
			const bool dead = Gameplay::Of(fb.entity).GetHP().IsDead();
			if (dead && fb.elapsed >= fireMinHold_) {
				fb.done = true; // 撃破（scoreは SweepDeadEntities 前の加点経路で処理）
			} else if (fb.elapsed >= fireMaxHold_) {
				fb.done = true; // 生存でも上限で次へ（ボス等）
			}
		}
	}

	// ----- 終了した bolt を trash へ回収 -----
	for (auto& fb : fireBolts_) {
		if (fb.done) TrashLightning(fb.rt);
	}
	fireBolts_.erase(
		std::remove_if(fireBolts_.begin(), fireBolts_.end(),
			[](const FireBolt& fb) { return fb.done; }),
		fireBolts_.end());

	// ----- Phase 終了：処理待ち 0 かつ アクティブ雷 0 -----
	if (fireQueue_.empty() && fireBolts_.empty()) {
		EnterEndPhase();
	}
}

void StormLockPart::EnterEndPhase() {
	phase_      = Phase::End;
	phaseTimer_ = 0.0f;
}

void StormLockPart::UpdateEndPhase(float dt) {
	(void)dt;
	// Fire 終了から endDuration_ 秒後にバリアを消して通常へ戻す
	if (phaseTimer_ >= endDuration_) {
		finished_ = true;
	}
}
