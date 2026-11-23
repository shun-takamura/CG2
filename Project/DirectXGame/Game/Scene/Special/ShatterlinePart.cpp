#include "ShatterlinePart.h"
#include "ISpecialMoveHost.h"

#include "Camera.h"
#include "Components/Gameplay.h"
#include "Components/EntityTag.h"
#include "Components/SphereCollider.h"
#include "DirectXCore.h"
#include "Effect/EffectManager.h"
#include "Effect/EffectPrimitiveRenderer.h"
#include "Effect/ScreenShardRenderer.h"
#include "Game.h"
#include "IImGuiEditable.h"
#include "Json/JsonValue.h"
#include "MathUtility.h"
#include "PostEffect.h"
#include "Primitive/LineRenderer.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Primitive/PrimitivePipeline.h"
#include "WindowsApplication.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <unordered_set>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 長さがこれ以下のベクトルは向きを決めない
	constexpr float kMinNormalizeLength = 1e-6f;
	// Vector3 の JSON 配列に要る要素の数
	constexpr int kVector3ComponentCount = 3;
	// Vector4（色）の JSON 配列に要る要素の数
	constexpr int kVector4ComponentCount = 4;
	// rad を度にする倍率
	constexpr float kRadToDegFactor = 57.29578f;
	// 画面の線をワールドへ戻すときの NDC の深さ（中間。見た目に影響なし）
	constexpr float kDebugNdcDepth = 0.5f;
	// 中央支点のリングを描く半径の下限 [px]
	constexpr float kMinPivotRadiusPx = 0.5f;
	// 中央支点のリングの分割数
	constexpr int kPivotRingSegments = 16;
	// この w 以下はカメラの後ろとみなす
	constexpr float kMinClipW = 1e-4f;
	// 画面の高さが分からないときの裂け目の幅の倍率（px あたり）
	constexpr float kFallbackLineWidthScale = 0.01f;
	// カメラ前方の面との交点の式の分母の下限
	constexpr float kMinRayDenom = 1e-4f;
	// 画面の線の端点を対角の長さの何倍まで伸ばすか（カメラを引いても画面端を超える）
	constexpr float kLineExtendRatio = 1.5f;
	// 長さ・距離の下限（これより短ければ何もしない）
	constexpr float kMinLength = 1e-4f;
	// 裂け目の板の枚数
	constexpr int kRiftPlaneCount = 3;
	// 裂け目の長さ方向の分割数
	constexpr int kRiftLengthSegments = 8;
	// smoothstep（3u² − 2u³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3u² − 2u³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 時間の長さの下限 [秒]（これ以下ならすぐ終わったものとして扱う）
	constexpr float kMinSeconds = 1e-4f;
	// 画面の大きさが分からないときの縦横比の横
	constexpr float kFallbackAspectWidth = 16.0f;
	// 画面の大きさが分からないときの縦横比の縦
	constexpr float kFallbackAspectHeight = 9.0f;
	// 長さがこれ以下なら向き・割合を出さない
	constexpr float kMinVectorLength = 1e-5f;
	// 割れが画面の端まで届く距離に足す余裕（倍率）
	constexpr float kBreakDistMargin = 1.02f;
	// 三角形 1 つの頂点の数
	constexpr int kVertsPerTriangle = 3;
	// 破片の寿命の下限 [秒]
	constexpr float kMinBreakLifeSeconds = 1e-3f;
	// Voronoi のセルの数の上限
	constexpr int kMaxCellCount = 2000;
	// 半平面で切ったときに増える頂点の数の上限
	constexpr int kClipExtraVerts = 2;
	// 画面の長方形の角の数
	constexpr int kRectCornerCount = 4;
	// 2 つの種の距離の 2 乗がこれより小さければ切らない
	constexpr float kMinSeedDistanceSq = 1e-12f;
	// 面積がこれより小さい多角形は頂点の平均を重心にする
	constexpr float kMinPolygonArea = 1e-9f;
	// 多角形の重心の式（Σ / 3A）の面積に掛ける数
	constexpr float kCentroidAreaFactor = 3.0f;
	// デバッグ表示で既に割れたセルの色
	constexpr Vector4 kBrokenDebugColor = { 1.0f, 0.25f, 0.2f, 1.0f };
	// デバッグ表示のまだ割れていないセルの緑の基本
	constexpr float kDebugColorGreenBase = 0.4f;
	// デバッグ表示のまだ割れていないセルの緑に割れる順に足す量
	constexpr float kDebugColorGreenRange = 0.4f;
	// デバッグ表示のまだ割れていないセルの青の基本
	constexpr float kDebugColorBlueBase = 0.2f;
	// デバッグ表示のまだ割れていないセルの青に割れる順に足す量
	constexpr float kDebugColorBlueRange = 0.6f;

	/// <summary>ローカル +Z をワールドの方向 d へ向けるオイラー角を求める（カメラの look 逆算と同じ規約）</summary>
	/// <param name="d">向けたい方向（長さは問わない。ほぼ 0 なら +Z とみなす）</param>
	/// <returns>オイラー角（pitch, yaw, roll）[ラジアン]。roll は常に 0</returns>
	Vector3 DirToEuler(const Vector3& d) {
		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		const Vector3 n = (len > kMinNormalizeLength) ? Vector3{ d.x / len, d.y / len, d.z / len } : Vector3{ 0.0f, 0.0f, 1.0f };
		const float yaw   = std::atan2(n.x, n.z);
		const float pitch = -std::asin(std::clamp(n.y, -1.0f, 1.0f));
		return { pitch, yaw, 0.0f };
	}
}

ShatterlinePart::ShatterlinePart() = default;
ShatterlinePart::~ShatterlinePart() = default;

void ShatterlinePart::Initialize(ISpecialMoveHost* host, Camera* camera) {
	host_ = host;
	camera_ = camera;
}

void ShatterlinePart::InitializeRenderer(DirectXCore* dxCore, SRVManager* srvManager) {
	shards_ = std::make_unique<ScreenShardRenderer>();
	shards_->Initialize(dxCore, srvManager, kShardCap_);
}

void ShatterlinePart::Finalize() {
	if (shards_) { shards_->Finalize(); shards_.reset(); }
	if (auto* em = EffectManager::GetInstance()) {
		if (beamHandle_ != kInvalidEffectHandle) em->Stop(beamHandle_);
		if (chargeHandle_ != kInvalidEffectHandle) em->Stop(chargeHandle_);
	}
	beamHandle_ = kInvalidEffectHandle;
	chargeHandle_ = kInvalidEffectHandle;
	rift_.reset();
	trash_.clear();
}

void ShatterlinePart::Reset() {
	phase_         = Phase::Idle;
	phaseTimer_    = 0.0f;
	aimConfirmed_  = false;
	aimAngle_      = 0.0f;
	cutWorldValid_ = false;
	camActive_     = false;  // カメラ演出を解除（以後はレール/通常追従に戻る）
	camInit_       = false;
	// 色反転（PostEffect）を確実に OFF（後隙で世界が反転したまま残らないように）
	if (auto* pe = Game::GetPostEffect(); pe && pe->colorInvert) {
		pe->colorInvert->SetEnabled(false);
	}
	// 発射ビーム／チャージ演出を止め、断裂線は遅延削除へ
	if (auto* em = EffectManager::GetInstance()) {
		if (beamHandle_ != kInvalidEffectHandle) { em->Stop(beamHandle_); beamHandle_ = kInvalidEffectHandle; }
		if (chargeHandle_ != kInvalidEffectHandle) { em->Stop(chargeHandle_); chargeHandle_ = kInvalidEffectHandle; }
	}
	TrashVisual(rift_);
	cellMeshUploaded_ = false; // 次の崩壊でセル形状を再アップロード
	pendingEnemies_.clear();
	pendingBullets_.clear();
	killsDone_ = false;
}

void ShatterlinePart::UpdateTrash() {
	for (auto& e : trash_) {
		if (e.framesLeft > 0) --e.framesLeft;
	}
	trash_.erase(std::remove_if(trash_.begin(), trash_.end(),
		[](const TrashEntry& e) { return e.framesLeft <= 0; }), trash_.end());
}

void ShatterlinePart::DrawScene(ID3D12GraphicsCommandList* commandList) {
	// 崩壊の破片用：シーンを1回だけキャプチャ（反転世界の元絵）。
	// カメラが戻り切り、かつ「断裂線が画面に走り切ってから」撮る＝走る様子を見せてから殻化して割る。
	// 早く撮ると断裂線が走る前に殻が覆って線が見えなくなる。
	if (phase_ == Phase::Collapse && camArrived_ && riftTimer_ >= riftRevealTime_ && !captureDone_) {
		if (auto* pe = Game::GetPostEffect()) pe->CaptureScene(commandList);
		captureDone_ = true;
		collapseRevealTimer_ = revealStartDelay_; // 殻が出た瞬間から割れ進行を 0 開始
		TrashVisual(rift_);                       // 断裂線は役目を終えたので片付ける（殻の下に残さない）
	}
	// 断裂線。発射ビーム／衝撃波は EffectManager が描く
	if (rift_) rift_->Draw();
}

void ShatterlinePart::DrawDebug() {
	if (cellDebugDraw_) DrawCellBordersDebug();
}

void ShatterlinePart::LoadTuning(const JsonValue& sg) {
	chargeDuration_   = static_cast<float>(sg["shatterlineCharge"].AsDouble(chargeDuration_));
	slashDuration_    = static_cast<float>(sg["shatterlineSlash"].AsDouble(slashDuration_));
	collapseDuration_ = static_cast<float>(sg["shatterlineCollapse"].AsDouble(collapseDuration_));
	recoverDuration_  = static_cast<float>(sg["shatterlineRecover"].AsDouble(recoverDuration_));
	chargeTimeScale_  = static_cast<float>(sg["shatterlineChargeTimeScale"].AsDouble(chargeTimeScale_));
	lineWidthPx_      = static_cast<float>(sg["shatterlineLineWidth"].AsDouble(lineWidthPx_));
	bossDamage_       = static_cast<int>(sg["shatterlineBossDamage"].AsInt(static_cast<int64_t>(bossDamage_)));
	pivotRadiusPx_    = static_cast<float>(sg["shatterlinePivotRadius"].AsDouble(pivotRadiusPx_));
	camPullback_      = static_cast<float>(sg["shatterlineCamPullback"].AsDouble(camPullback_));
	camUpAdd_         = static_cast<float>(sg["shatterlineCamUpAdd"].AsDouble(camUpAdd_));
	camFovAdd_        = static_cast<float>(sg["shatterlineCamFovAdd"].AsDouble(camFovAdd_));
	camEaseTime_      = static_cast<float>(sg["shatterlineCamEaseTime"].AsDouble(camEaseTime_));
	camFireSide_      = static_cast<float>(sg["shatterlineCamFireSide"].AsDouble(camFireSide_));
	camFireFront_     = static_cast<float>(sg["shatterlineCamFireFront"].AsDouble(camFireFront_));
	camFireDown_      = static_cast<float>(sg["shatterlineCamFireDown"].AsDouble(camFireDown_));
	camFireLookUp_    = static_cast<float>(sg["shatterlineCamFireLookUp"].AsDouble(camFireLookUp_));
	camFireFovAdd_    = static_cast<float>(sg["shatterlineCamFireFovAdd"].AsDouble(camFireFovAdd_));
	camCollapsePullback_ = static_cast<float>(sg["shatterlineCamCollapsePullback"].AsDouble(camCollapsePullback_));
	camCollapseUpAdd_    = static_cast<float>(sg["shatterlineCamCollapseUpAdd"].AsDouble(camCollapseUpAdd_));
	cutDepth_         = static_cast<float>(sg["shatterlineCutDepth"].AsDouble(cutDepth_));
	// Step5-B: 発射ビーム(EffectDef化) / 衝撃波 / 断裂線
	if (sg["shatterlineBeamEffect"].IsString()) beamEffectName_ = sg["shatterlineBeamEffect"].AsString();
	if (sg["shatterlineChargeEffect"].IsString()) chargeEffectName_ = sg["shatterlineChargeEffect"].AsString();
	riftWidthScale_ = static_cast<float>(sg["shatterlineRiftWidthScale"].AsDouble(riftWidthScale_));
	riftRevealTime_ = static_cast<float>(sg["shatterlineRiftRevealTime"].AsDouble(riftRevealTime_));
	revealIntensity_    = static_cast<float>(sg["shatterlineRevealIntensity"].AsDouble(revealIntensity_));
	revealStartDelay_   = static_cast<float>(sg["shatterlineRevealStartDelay"].AsDouble(revealStartDelay_));
	collapseEndAt_      = static_cast<float>(sg["shatterlineCollapseEndAt"].AsDouble(collapseEndAt_));
	invertDelay_        = static_cast<float>(sg["shatterlineInvertDelay"].AsDouble(invertDelay_));
	fragAlpha_       = static_cast<float>(sg["shatterlineFragAlpha"].AsDouble(fragAlpha_));
	fragSatBoost_    = static_cast<float>(sg["shatterlineFragSatBoost"].AsDouble(fragSatBoost_));
	fragSpin_       = static_cast<float>(sg["shatterlineFragSpin"].AsDouble(fragSpin_));
	fragMinScale_   = static_cast<float>(sg["shatterlineFragMinScale"].AsDouble(fragMinScale_));
	fractureSeed_     = static_cast<uint32_t>(sg["shatterlineFractureSeed"].AsInt(static_cast<int64_t>(fractureSeed_)));
	fractureSeedLock_ = sg["shatterlineFractureSeedLock"].AsBool(fractureSeedLock_);
	cellCount_        = static_cast<int>(sg["shatterlineCellCount"].AsInt(static_cast<int64_t>(cellCount_)));
	breakFreeze_      = static_cast<float>(sg["shatterlineBreakFreeze"].AsDouble(breakFreeze_));
	breakPopSpeed_    = static_cast<float>(sg["shatterlineBreakPopSpeed"].AsDouble(breakPopSpeed_));
	breakSpread_      = static_cast<float>(sg["shatterlineBreakSpread"].AsDouble(breakSpread_));
	breakGravity_     = static_cast<float>(sg["shatterlineBreakGravity"].AsDouble(breakGravity_));
	breakLifeMin_     = static_cast<float>(sg["shatterlineBreakLifeMin"].AsDouble(breakLifeMin_));
	breakLifeMax_     = static_cast<float>(sg["shatterlineBreakLifeMax"].AsDouble(breakLifeMax_));
	{
		const JsonValue& bd = sg["shatterlineBeamDir"];
		if (bd.IsArray() && bd.Size() >= kVector3ComponentCount) {
			beamDir_ = { static_cast<float>(bd[0].AsDouble(beamDir_.x)),
			                      static_cast<float>(bd[1].AsDouble(beamDir_.y)),
			                      static_cast<float>(bd[2].AsDouble(beamDir_.z)) };
		}
		auto loadColor = [&](const char* key, Vector4& c) {
			const JsonValue& v = sg[key];
			if (v.IsArray() && v.Size() >= kVector4ComponentCount) {
				c = { static_cast<float>(v[0].AsDouble(c.x)), static_cast<float>(v[1].AsDouble(c.y)),
				      static_cast<float>(v[2].AsDouble(c.z)), static_cast<float>(v[3].AsDouble(c.w)) };
			}
		};
		loadColor("shatterlineRiftColor",  riftColor_);
	}
}

void ShatterlinePart::SaveTuning(JsonValue& sgObj) const {
	sgObj["shatterlineCharge"]         = static_cast<double>(chargeDuration_);
	sgObj["shatterlineSlash"]          = static_cast<double>(slashDuration_);
	sgObj["shatterlineCollapse"]       = static_cast<double>(collapseDuration_);
	sgObj["shatterlineRecover"]        = static_cast<double>(recoverDuration_);
	sgObj["shatterlineChargeTimeScale"] = static_cast<double>(chargeTimeScale_);
	sgObj["shatterlineLineWidth"]       = static_cast<double>(lineWidthPx_);
	sgObj["shatterlineBossDamage"]      = static_cast<int64_t>(bossDamage_);
	sgObj["shatterlinePivotRadius"]     = static_cast<double>(pivotRadiusPx_);
	sgObj["shatterlineCamPullback"]     = static_cast<double>(camPullback_);
	sgObj["shatterlineCamUpAdd"]        = static_cast<double>(camUpAdd_);
	sgObj["shatterlineCamFovAdd"]       = static_cast<double>(camFovAdd_);
	sgObj["shatterlineCamEaseTime"]     = static_cast<double>(camEaseTime_);
	sgObj["shatterlineCamFireSide"]     = static_cast<double>(camFireSide_);
	sgObj["shatterlineCamFireFront"]    = static_cast<double>(camFireFront_);
	sgObj["shatterlineCamFireDown"]     = static_cast<double>(camFireDown_);
	sgObj["shatterlineCamFireLookUp"]   = static_cast<double>(camFireLookUp_);
	sgObj["shatterlineCamFireFovAdd"]   = static_cast<double>(camFireFovAdd_);
	sgObj["shatterlineCamCollapsePullback"] = static_cast<double>(camCollapsePullback_);
	sgObj["shatterlineCamCollapseUpAdd"]    = static_cast<double>(camCollapseUpAdd_);
	sgObj["shatterlineCutDepth"]        = static_cast<double>(cutDepth_);
	sgObj["shatterlineBeamEffect"]  = beamEffectName_;
	sgObj["shatterlineChargeEffect"] = chargeEffectName_;
	sgObj["shatterlineRiftWidthScale"] = static_cast<double>(riftWidthScale_);
	sgObj["shatterlineRiftRevealTime"] = static_cast<double>(riftRevealTime_);
	sgObj["shatterlineRevealIntensity"]    = static_cast<double>(revealIntensity_);
	sgObj["shatterlineRevealStartDelay"]   = static_cast<double>(revealStartDelay_);
	sgObj["shatterlineCollapseEndAt"]      = static_cast<double>(collapseEndAt_);
	sgObj["shatterlineInvertDelay"]        = static_cast<double>(invertDelay_);
	sgObj["shatterlineFragAlpha"]       = static_cast<double>(fragAlpha_);
	sgObj["shatterlineFragSatBoost"]    = static_cast<double>(fragSatBoost_);
	sgObj["shatterlineFragSpin"]       = static_cast<double>(fragSpin_);
	sgObj["shatterlineFragMinScale"]   = static_cast<double>(fragMinScale_);
	sgObj["shatterlineFractureSeed"]     = static_cast<int64_t>(fractureSeed_);
	sgObj["shatterlineFractureSeedLock"] = JsonValue(fractureSeedLock_);
	sgObj["shatterlineCellCount"]        = static_cast<int64_t>(cellCount_);
	sgObj["shatterlineBreakFreeze"]      = static_cast<double>(breakFreeze_);
	sgObj["shatterlineBreakPopSpeed"]    = static_cast<double>(breakPopSpeed_);
	sgObj["shatterlineBreakSpread"]      = static_cast<double>(breakSpread_);
	sgObj["shatterlineBreakGravity"]     = static_cast<double>(breakGravity_);
	sgObj["shatterlineBreakLifeMin"]     = static_cast<double>(breakLifeMin_);
	sgObj["shatterlineBreakLifeMax"]     = static_cast<double>(breakLifeMax_);
	{
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(beamDir_.x)));
		arr.Push(JsonValue(static_cast<double>(beamDir_.y)));
		arr.Push(JsonValue(static_cast<double>(beamDir_.z)));
		sgObj["shatterlineBeamDir"] = std::move(arr);
	}
	auto saveColor = [&](const char* key, const Vector4& c) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(c.x)));
		arr.Push(JsonValue(static_cast<double>(c.y)));
		arr.Push(JsonValue(static_cast<double>(c.z)));
		arr.Push(JsonValue(static_cast<double>(c.w)));
		sgObj[key] = std::move(arr);
	};
	saveColor("shatterlineRiftColor",  riftColor_);
}

void ShatterlinePart::OnImGui(bool& changed) {
#ifdef _DEBUG
	ImGui::SeparatorText("Shatterline (Charge→Slash→Collapse→Recover)");
	ImGui::DragFloat("Charge Duration",   &chargeDuration_,   0.05f, 0.05f, 10.0f, "%.2f sec");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Slash Duration",    &slashDuration_,    0.02f, 0.02f, 5.0f, "%.2f sec");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Collapse Duration", &collapseDuration_, 0.05f, 0.05f, 10.0f, "%.2f sec");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("崩壊全体の長さ。大きいほどゆっくり崩れる（剥がれ時間=Collapse Duration - Reveal Start Delay）");
	ImGui::DragFloat("Collapse End At (戻り割合で終了)", &collapseEndAt_, 0.01f, 0.1f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("画面がこの割合まで通常色に戻ったら崩壊終了→World再開（破片は生かしたまま）");
	ImGui::DragFloat("Recover Duration",  &recoverDuration_,  0.05f, 0.05f, 10.0f, "%.2f sec");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Charge TimeScale",  &chargeTimeScale_,  0.01f, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::TextDisabled("  チャージ演出（受付時間中）は EffectDef \"%s\"（エフェクトエディター）で作る", chargeEffectName_.c_str());
	ImGui::DragFloat("Line Half Width (px)", &lineWidthPx_, 1.0f, 1.0f, 400.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragInt("Boss Damage (fixed)", &bossDamage_, 1.0f, 0, 99999);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::ColorEdit4("Aim Preview Color", &aimPreviewColor_.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::ColorEdit4("Aim Confirm Color", &aimConfirmColor_.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Pivot Marker Radius (px)", &pivotRadiusPx_, 0.5f, 0.0f, 100.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::ColorEdit4("Pivot Marker Color", &pivotColor_.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::TextDisabled("Camera (Charge=狙う後方引き / Slash=発射ショット / Collapse=後方復帰)");
	ImGui::TextDisabled("※移動完了までは次フェーズへ進まない（手続き形式・秒数変更に強い）");
	ImGui::DragFloat("Cam Move Time (s)", &camEaseTime_, 0.01f, 0.01f, 2.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("CamMove: timer=%.2f arrived=%d", camMoveTimer_, camArrived_ ? 1 : 0);
	ImGui::Text("[Charge] 狙う後方引き");
	ImGui::DragFloat("Charge Pullback", &camPullback_, 0.1f, 0.0f, 20.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Charge Up Add", &camUpAdd_, 0.05f, 0.0f, 10.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Charge FovY Add", &camFovAdd_, 0.005f, 0.0f, 1.0f, "%.3f rad");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("[Slash] 発射ショット（左横やや前・あおり）");
	ImGui::DragFloat("Fire Side (左横)", &camFireSide_, 0.1f, 0.0f, 30.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fire Front (前方)", &camFireFront_, 0.1f, -30.0f, 30.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fire Down (あおり)", &camFireDown_, 0.1f, -20.0f, 20.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fire Look Up", &camFireLookUp_, 0.05f, -10.0f, 10.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fire FovY Add", &camFireFovAdd_, 0.005f, -0.5f, 1.0f, "%.3f rad");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("[Collapse] 後方復帰");
	ImGui::DragFloat("Collapse Pullback", &camCollapsePullback_, 0.1f, 0.0f, 30.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Collapse Up Add", &camCollapseUpAdd_, 0.05f, 0.0f, 10.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

	ImGui::TextDisabled("Step5-B: 発射ビーム / 衝撃波 / 断裂線（A案・仮ビーム）");
	ImGui::Text("[発射ビーム] 固定方向の一閃（狙い角度と無関係）");
	ImGui::DragFloat3("Beam Dir (world)", &beamDir_.x, 0.02f, -1.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::TextDisabled("  色/サイズ/先細り/伸長＋衝撃波(Ringコンポ追加)は EffectDef \"%s\"（エフェクトエディター）で編集", beamEffectName_.c_str());
	ImGui::Text("[断裂線] 仮ビーム（Collapse で右→左に走り込む。幅=判定帯に一致）");
	ImGui::DragFloat("Rift Width Scale (x判定帯)", &riftWidthScale_, 0.02f, 0.05f, 4.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Rift Reveal Time (s)", &riftRevealTime_, 0.01f, 0.01f, 3.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::ColorEdit4("Rift Color", &riftColor_.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("[崩壊リビール] 反転殻＝Slash全画面反転→Collapseで線から上下へ通常色が伝播");
	ImGui::TextDisabled("剥がれは [Start Delay, Collapse Duration] 区間で進む。長くするほどゆっくり戻る");
	ImGui::DragFloat("Invert Delay (発射→反転) (s)", &invertDelay_, 0.01f, 0.0f, 2.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("カメラ回り込み→ビーム発射の後、色反転が入るまでの遅れ");
	ImGui::DragFloat("Reveal Start Delay (s)", &revealStartDelay_, 0.01f, 0.0f, 5.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Reveal Intensity", &revealIntensity_, 0.01f, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("[殻・破片の見た目] 未割れ＝静止殻 / 割れ＝飛散（共通）");
	ImGui::DragFloat("Frag Min Scale (消えるサイズ比 0-1)", &fragMinScale_, 0.005f, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("破片サイズ(1=発生時)がこの比まで縮んだら完全に消す（αは下げない）。大きいほど早くパッと消える");
	ImGui::DragFloat("Frag Spin (rad/s)", &fragSpin_, 0.1f, 0.0f, 30.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Frag Alpha (不透明度)", &fragAlpha_, 0.01f, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Frag Sat Boost (彩度)", &fragSatBoost_, 0.05f, 0.0f, 4.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("拾った反転色の彩度を強調（1=そのまま、>1で色が濃く）");

	ImGui::SeparatorText("Fracture (割り方・シード) [F1]");
	ImGui::TextDisabled("画面を事前にVoronoiセル分割。seed固定で同じ割れ方を再現。");
	ImGui::Checkbox("Cell Debug Draw (境界線)", &cellDebugDraw_);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON でセル境界線を色分け表示（暖色=先に割れる/寒色=後）。発動中も Idle でも見える");
	if (ImGui::Checkbox("Seed Lock (固定)", &fractureSeedLock_)) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON=毎回このシードで割る（同じ割れ方）。OFF=発動ごとにランダム（使った値はSeedに残る）");
	{
		int seedI = static_cast<int>(fractureSeed_);
		if (ImGui::InputInt("Seed", &seedI)) { if (seedI < 0) seedI = 0; fractureSeed_ = static_cast<uint32_t>(seedI); }
		if (ImGui::IsItemDeactivatedAfterEdit()) { BuildCells(); changed = true; }
	}
	if (ImGui::Button("Randomize")) { fractureSeed_ = std::random_device{}(); BuildCells(); changed = true; }
	ImGui::SameLine();
	if (ImGui::Button("Regenerate (same seed)")) { BuildCells(); }
	ImGui::DragInt("Cell Count", &cellCount_, 1.0f, 1, 1000);
	if (ImGui::IsItemDeactivatedAfterEdit()) { BuildCells(); changed = true; }
	ImGui::DragFloat("Break Preview (revealT)", &cellPreviewRevealT_, 0.01f, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("境界の進み具合を手動スクラブ。この値まで届いた片が『割れた』表示（赤）になる＝割れ順の確認");
	ImGui::Text("Cells built: %d", static_cast<int>(cells_.size()));
	ImGui::Checkbox("Static Shards (F2: 割らず静止描画)", &cellStaticDraw_);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("崩壊中に全セルを静止描画＝baked UV のマッピング確認。Reveal Intensity を 0 にすると背景が通常色のままでズレが見やすい");
	ImGui::TextDisabled("[F3] 飛散（Static Shards OFF で有効）");
	ImGui::DragFloat("Break Freeze (s)", &breakFreeze_, 0.01f, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("割れた直後この秒数は静止＝『バリン』の溜め");
	ImGui::DragFloat("Break Pop Speed (視点へ)", &breakPopSpeed_, 0.1f, 0.0f, 50.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Break Spread (拡散)", &breakSpread_, 0.1f, 0.0f, 50.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Break Gravity (落下)", &breakGravity_, 0.1f, 0.0f, 50.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat2("Break Life Min/Max (s)", &breakLifeMin_, 0.01f, 0.05f, 5.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	{
		int broken = 0;
		for (const auto& rt : cellRuntime_) if (rt.broken && rt.age < rt.lifeDur) ++broken;
		ImGui::Text("Broken (flying): %d / %d", broken, static_cast<int>(cellRuntime_.size()));
	}

	{
		const char* dPhase = "Idle";
		switch (phase_) {
		case Phase::Charge:   dPhase = "Charge";   break;
		case Phase::Slash:    dPhase = "Slash";    break;
		case Phase::Collapse: dPhase = "Collapse"; break;
		case Phase::Recover:  dPhase = "Recover";  break;
		default: break;
		}
		ImGui::Text("Shatterline Phase: %s  phaseTimer=%.2f  invincible=%d",
			dPhase, phaseTimer_, host_->IsSpecialInvincible() ? 1 : 0);
		ImGui::Text("Aim: angle=%.1f deg  confirmed=%d",
			aimAngle_ * kRadToDegFactor, aimConfirmed_ ? 1 : 0);
	}

#else
	(void)changed;
#endif
}

// ===== 段取り（Charge → Slash → Collapse → Recover） ==================================

void ShatterlinePart::Begin() {
	// 自機を画面中央へ寄せる・入力を止めるのはシーン側（TriggerSpecialMove）の共通処理
	// 照準リセット：未確定・初期角度はレティクル方向（チャージ中にライブ追従）
	aimConfirmed_ = false;
	aimAngle_     = AngleFromReticle();

	// 切断線（ワールド焼き付け）をリセット
	cutWorldValid_ = false;

	// カメラ演出を有効化（補間状態は初回フレームで実カメラから初期化）
	camActive_ = true;
	camInit_   = false;

	// 前回の演出残骸があれば片付ける（発射ビーム／チャージ演出は EffectManager、断裂線は遅延削除）
	if (auto* em = EffectManager::GetInstance()) {
		if (beamHandle_ != kInvalidEffectHandle) { em->Stop(beamHandle_); beamHandle_ = kInvalidEffectHandle; }
		if (chargeHandle_ != kInvalidEffectHandle) { em->Stop(chargeHandle_); chargeHandle_ = kInvalidEffectHandle; }
	}
	TrashVisual(rift_);
	riftTimer_ = 0.0f;
	// 遅延キルの保留をリセット
	pendingEnemies_.clear();
	pendingBullets_.clear();
	killsDone_ = false;
	// 発射タイミング状態をリセット（カメラ回り込み後に発射→遅れて反転）
	fired_ = false;
	fireElapsed_ = 0.0f;
	collapseRevealTimer_ = 0.0f;

	EnterPhase(Phase::Charge);
}

void ShatterlinePart::EnterPhase(Phase p) {
	phase_      = p;
	phaseTimer_ = 0.0f;

	switch (p) {
	case Phase::Charge:
		// 収束：World 停止/スロー（chargeTimeScale_、既定0=停止で敵が固定）＋無敵
		host_->SetTimeScale(TimeGroup::World, chargeTimeScale_);
		host_->SetSpecialInvincible(true);
		// チャージ演出（受付時間中）をプレイヤー中央で再生。中身はエフェクトエディターで作る（loop=true）。
		// World 停止中でも進めたいので、Update は Charge の間だけ実時間で回す（UpdateGlobalEffects 側で分岐）。
		if (auto* em = EffectManager::GetInstance()) {
			if (chargeHandle_ != kInvalidEffectHandle) em->Stop(chargeHandle_);
			chargeHandle_ = kInvalidEffectHandle;
			if (!chargeEffectName_.empty() && em->HasDef(chargeEffectName_)) {
				chargeHandle_ = em->Play(chargeEffectName_, host_->GetSpecialPlayerCenter());
			}
		}
		break;
	case Phase::Slash:
		// 一閃：World 停止＋無敵。チャージ演出は受付終了なので停止。
		host_->SetTimeScale(TimeGroup::World, 0.0f);
		host_->SetSpecialInvincible(true);
		if (auto* em = EffectManager::GetInstance(); em && chargeHandle_ != kInvalidEffectHandle) {
			em->Stop(chargeHandle_);
			chargeHandle_ = kInvalidEffectHandle;
		}
		break;
	case Phase::Collapse:
		// 崩壊：World 停止のまま＋無敵。発射ビーム/衝撃波を片付け、断裂線を生成（右→左に走り込む）。
		host_->SetTimeScale(TimeGroup::World, 0.0f);
		host_->SetSpecialInvincible(true);
		// ApplySpecialCamera は UpdateSpecialMove より前に走るため、この遷移フレームでは
		// camArrived が Slash 到達のまま true で残っている。明示的に false に落として、
		// 後方アングルへ戻り切ってからキャプチャ＆セル形状 bake させる（横アングルで焼くと殻がズレる）。
		camArrived_ = false;
		if (auto* em = EffectManager::GetInstance(); em && beamHandle_ != kInvalidEffectHandle) {
			em->Stop(beamHandle_);
			beamHandle_ = kInvalidEffectHandle;
		}
		BuildRift();
		// F1: 事前分割セルを構築（種点はスクリーン空間固定＝seed で割れ方が決まる）。
		if (!fractureSeedLock_) fractureSeed_ = std::random_device{}();
		BuildCells();
		cellMeshUploaded_ = false; // この崩壊でセルのワールド形状を1回アップロードし直す
		captureDone_ = false;   // この崩壊のシーンキャプチャは Draw で1回だけ
		collapseRevealTimer_ = 0.0f; // カメラが狙うアングルへ戻り切ってから進める
		break;
	case Phase::Recover:
		// 復帰＋後隙：World 再開・無敵なし（入力は specialActive_ で継続ロック）。
		// 念のため：断裂線が引き切る前に Collapse が終わってもキルは保証（取りこぼし防止）。
		ExecuteKills();
		host_->SetTimeScale(TimeGroup::World, 1.0f);
		host_->SetSpecialInvincible(false);
		TrashVisual(rift_);
		break;
	default:
		break;
	}
}

bool ShatterlinePart::Update(bool firePressed, float realDt) {
	phaseTimer_ += realDt;
	bool finished = false;

	switch (phase_) {
	case Phase::Charge:
		// 照準：未確定ならレティクル方向へライブ追従（予測線）。Fire で確定（角度凍結）。
		// 早置きでもチャージは短縮しない（タイマーはそのまま進む）。
		if (!aimConfirmed_) {
			aimAngle_ = AngleFromReticle();
			if (firePressed) {
				aimConfirmed_ = true;
			}
		}
		// フェーズ遷移は「規定秒数を満たす」かつ「このフェーズのカメラ移動が完了済み」で進む（手続き形式）。
		// カメラ移動は決定的に完了するので、各フェーズ秒数を変えても演出が途中で打ち切られない。
		if (phaseTimer_ >= chargeDuration_ && camArrived_) {
			// 未配置なら横一閃（θ=0）で自動発射
			if (!aimConfirmed_) {
				aimAngle_     = 0.0f;
				aimConfirmed_ = true;
			}
			EnterPhase(Phase::Slash);
			// 線決定時（＝今まだ「狙う」カメラ）の視点でワールド切断線を焼き付け＆対象収集する。
			// こうすると Collapse で狙うアングルに戻したとき、崩壊が描いた線どおりの向きになる。
			ExecuteSlash();
		}
		break;
	case Phase::Slash:
		if (!fired_) {
			// カメラが発射ショット位置へ回り込み切った瞬間にビーム発射（回り込み→発射の順）
			if (camArrived_) {
				BuildFireVisuals();  // 発射ビーム＋衝撃波を生成
				fired_ = true;
				fireElapsed_ = 0.0f;
			}
		} else {
			// 発射の表示尺ぶん見せてから崩壊（後方復帰＋断裂線走り込み）へ
			fireElapsed_ += realDt;
			if (fireElapsed_ >= slashDuration_ && camArrived_) {
				EnterPhase(Phase::Collapse);
			}
		}
		break;
	case Phase::Collapse:
		// カメラが「狙うアングル」へ戻り切ってから崩壊タイマーを進める（復帰中は崩壊しない）。
		if (camArrived_) collapseRevealTimer_ += realDt;
		// 画面が collapseEndAt_ の割合まで通常色に戻ったら後隙(World再開)へ。
		// 破片はそのまま生かして後隙中にフェードさせる（World再開後も少し飛び続ける）。
		if (CollapseRevealT() >= collapseEndAt_) {
			EnterPhase(Phase::Recover);
		}
		break;
	case Phase::Recover:
		// 後隙明け → 終了（シーンが EndSpecialMove でクールタイムを始める）
		if (phaseTimer_ >= recoverDuration_ && camArrived_) {
			finished = true;
		}
		break;
	default:
		break;
	}

	// カメラ演出はフェーズ別の目標姿勢を ApplyCamera() が毎フレ補間する（ここでは扱わない）。

	// Step5-B 演出の更新（発射ビーム伸長／衝撃波／断裂線の走り込み）。
	UpdateVisuals(realDt);

	// 予測線（チャージ中の狙い）のみ LineRenderer で表示。一閃以降はビーム/断裂線ビジュアルが担当。
	if (phase_ == Phase::Charge) {
		DrawAimLine();
	}
	return finished;
}

float ShatterlinePart::AngleFromReticle() const {
	const float cx = static_cast<float>(WindowsApplication::kUIWidth)  * 0.5f;
	const float cy = static_cast<float>(WindowsApplication::kUIHeight) * 0.5f;
	const Vector2 rp = host_->GetReticleScreenPos();
	const float dx = rp.x - cx;
	const float dy = rp.y - cy;
	if (dx * dx + dy * dy < 1.0f) return aimAngle_; // 中央付近は前回値を維持（角度ジャンプ防止）
	return std::atan2(dy, dx);
}

void ShatterlinePart::DrawAimLine() {
	auto* lr = LineRenderer::GetInstance();
	if (!lr || !camera_) return;

	// 一閃以降（Slash/Collapse）は、焼き付けたワールド線を描く。カメラを引いても
	// 敵と同じ世界座標に残るので「描いた通り」に貫いて見える。
	if (phase_ != Phase::Charge) {
		if (cutWorldValid_) {
			lr->AddLine(cutWorldP1_, cutWorldP2_, aimConfirmColor_);
		}
		return;
	}

	const float w = static_cast<float>(WindowsApplication::kUIWidth);
	const float h = static_cast<float>(WindowsApplication::kUIHeight);
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;
	// スクリーン端まで延ばすため対角長を半径に取り、中央±方向の2端点を作る
	const float half = std::sqrt(w * w + h * h);
	const float dirX = std::cos(aimAngle_);
	const float dirY = std::sin(aimAngle_);
	const Vector2 p1{ cx - dirX * half, cy - dirY * half };
	const Vector2 p2{ cx + dirX * half, cy + dirY * half };

	// pixel → world（同一NDC深度でアンプロジェクト＝再投影でまっすぐな画面線になる）
	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	const float ndcZ = kDebugNdcDepth; // 中間深度（debug 可視化用、見た目に影響なし）
	auto pixelToWorld = [&](const Vector2& px) -> Vector3 {
		const float ndcX = (px.x / w) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (px.y / h) * 2.0f;
		return TransformCoordinate(Vector3{ ndcX, ndcY, ndcZ }, invVP);
	};

	const Vector4 color = aimConfirmed_ ? aimConfirmColor_ : aimPreviewColor_;
	lr->AddLine(pixelToWorld(p1), pixelToWorld(p2), color);

	// 中央支点マーカー（リング）。どこが回転中心かを明示する。
	if (pivotRadiusPx_ > kMinPivotRadiusPx) {
		const int seg = kPivotRingSegments;
		const float r = pivotRadiusPx_;
		Vector2 prev{};
		for (int i = 0; i <= seg; ++i) {
			const float a = (static_cast<float>(i) / seg) * kTwoPi;
			const Vector2 pt{ cx + std::cos(a) * r, cy + std::sin(a) * r };
			if (i > 0) lr->AddLine(pixelToWorld(prev), pixelToWorld(pt), pivotColor_);
			prev = pt;
		}
	}
}

void ShatterlinePart::ExecuteSlash() {
	if (!camera_) return;

	const float w = static_cast<float>(WindowsApplication::kUIWidth);
	const float h = static_cast<float>(WindowsApplication::kUIHeight);
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;
	const float dirX = std::cos(aimAngle_);
	const float dirY = std::sin(aimAngle_);

	// 点 P（pixel）の「中央支点を通る角度 θ の直線」への垂直距離。d は単位ベクトルなので外積の大きさ＝距離。
	auto distToLine = [&](const Vector2& px) -> float {
		const float ex = px.x - cx;
		const float ey = px.y - cy;
		return std::abs(ex * dirY - ey * dirX);
	};

	// ワールド→pixel 投影（ww>0 ＝カメラ前方。深度は無視＝物陰の敵もヒット）。outW にクリップ w(=前方距離) を返す。
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	auto projectToPixel = [&](const Vector3& world, Vector2& outPx, float& outW) -> bool {
		const float wx = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
		const float wy = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
		const float ww = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
		if (ww <= kMinClipW) return false; // カメラ後方
		const float ndcX = wx / ww;
		const float ndcY = wy / ww;
		if (std::abs(ndcX) > 1.0f || std::abs(ndcY) > 1.0f) return false; // 画面外（線は画面端まで）
		outPx.x = (ndcX * 0.5f + 0.5f) * w;
		outPx.y = (1.0f - (ndcY * 0.5f + 0.5f)) * h;
		outW = ww;
		return true;
	};

	// world 半径 → 画面 px 半径 の係数（敵投影サイズと同式）。前方距離 ww で割る。
	const Matrix4x4& projMat = camera_->GetProjectionMatrix();
	const float invTanHalfFovY = projMat.m[1][1];

	// コライダーの代表 world 半径（かすり判定用）。形状ごとに体の太さを取る。
	auto colliderWorldRadius = [](const Collider& c) -> float {
		switch (c.shape) {
		case ColliderShape::Sphere:  return c.radius;
		case ColliderShape::Capsule: return c.capsuleRadius;
		default:                     return (std::max)({ c.halfExtents.x, c.halfExtents.y, c.halfExtents.z });
		}
	};

	// 線上判定：コライダー中心を投影し「線への距離 <= 判定半幅 + 投影半径(かすり)」。深度無視で物陰貫通。
	auto onLine = [&](const Vector3& centerWorld, float worldRadius) -> bool {
		Vector2 px{};
		float ww = 0.0f;
		if (!projectToPixel(centerWorld, px, ww)) return false;
		float pxR = 0.0f;
		if (worldRadius > 0.0f && ww > kMinClipW) pxR = worldRadius * h * invTanHalfFovY * 0.5f / ww;
		return distToLine(px) <= (lineWidthPx_ + pxR);
	};

	// ワールド線焼き付け用：ヒットした敵のカメラ前方距離(depth)を平均する
	const Vector3 camPos = camera_->GetTranslate();
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 camFwd{ camRot.m[2][0], camRot.m[2][1], camRot.m[2][2] };
	float depthSum = 0.0f;
	int   depthCount = 0;
	auto accumDepth = [&](const Vector3& pos) {
		const float dz = (pos.x - camPos.x) * camFwd.x + (pos.y - camPos.y) * camFwd.y + (pos.z - camPos.z) * camFwd.z;
		if (dz > 0.0f) { depthSum += dz; ++depthCount; }
	};

	// ----- 判定は「収集」のみ。実際のキルは断裂線が引き切った瞬間（ExecuteKills）に一括実行する -----
	// World は Slash/Collapse 中ずっと停止なので、収集した対象は凍結＝位置不変で安全。
	pendingEnemies_.clear();
	pendingBullets_.clear();
	killsDone_ = false;

	// 敵弾（EnemyAttack）：コライダー中心で判定
	std::vector<IImGuiEditable*> candidates;
	host_->CollectEnemyBullets(candidates);
	for (IImGuiEditable* bullet : candidates) {
		const Vector3* p = bullet->GetEditableTranslate();
		if (!p) continue;
		const Collider& bc = Gameplay::Of(bullet).GetCollider();
		const Vector3 center{ p->x + bc.offset.x, p->y + bc.offset.y, p->z + bc.offset.z };
		if (onLine(center, colliderWorldRadius(bc))) pendingBullets_.push_back(bullet);
	}

	// 敵本体（Enemy / Boss）。コライダー中心＋投影半径で判定し、線上なら収集＋焼き付け奥行きに加算。
	std::unordered_set<IImGuiEditable*> seen;
	auto consider = [&](IImGuiEditable* e) {
		if (!e || !seen.insert(e).second) return;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag != EntityTag::Enemy && tag != EntityTag::Boss) return;
		const Vector3* p = e->GetEditableTranslate();
		if (!p) return;
		const Collider& col = Gameplay::Of(e).GetCollider();
		const Vector3 center{ p->x + col.offset.x, p->y + col.offset.y, p->z + col.offset.z };
		if (!onLine(center, colliderWorldRadius(col))) return;
		accumDepth(center);
		pendingEnemies_.push_back(e);
	};
	candidates.clear();
	host_->CollectEnemies(candidates);
	for (IImGuiEditable* e : candidates) consider(e);

	// ----- 切断線をワールド空間へ焼き付ける -----
	// 画面端まで延ばした2端点を、ヒット敵の平均奥行き（無ければ既定）でワールド化。
	// 以後カメラを引いても、この線は敵と同じ世界座標に残り続ける＝「描いた通り」に貫いて見える。
	const float depth = (depthCount > 0) ? (depthSum / depthCount) : cutDepth_;
	// 案1：判定帯（lineWidthPx_ 半幅px）を焼き付け深度で world 半幅へ換算して保持。
	// px→world は敵投影半径の式（pixel = world * clientH * 0.5 / (tan(fov/2) * depth)）の逆。
	{
		const float fovY    = camera_->GetFovY();
		const float tanHalf = std::tan(fovY * 0.5f);
		const float clientH = static_cast<float>(WindowsApplication::kUIHeight);
		riftBakedHalfWidth_ = (clientH > 1.0f)
			? (lineWidthPx_ * tanHalf * depth * 2.0f / clientH)
			: (lineWidthPx_ * kFallbackLineWidthScale);
	}
	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	auto pixelToWorldAtDepth = [&](const Vector2& px) -> Vector3 {
		const float ndcX = (px.x / w) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (px.y / h) * 2.0f;
		const Vector3 farW = TransformCoordinate(Vector3{ ndcX, ndcY, 1.0f }, invVP);
		Vector3 rd{ farW.x - camPos.x, farW.y - camPos.y, farW.z - camPos.z };
		const float rl = std::sqrt(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z);
		if (rl > kMinNormalizeLength) { rd.x /= rl; rd.y /= rl; rd.z /= rl; }
		float denom = rd.x * camFwd.x + rd.y * camFwd.y + rd.z * camFwd.z;
		if (denom < kMinRayDenom) denom = kMinRayDenom; // カメラ前方平面に乗せる
		const float t = depth / denom;
		return { camPos.x + rd.x * t, camPos.y + rd.y * t, camPos.z + rd.z * t };
	};
	// カメラを引いても画面端を超えるよう、対角長を多めに取って端点を作る
	const float halfLen = std::sqrt(w * w + h * h) * kLineExtendRatio;
	const Vector2 e1{ cx - dirX * halfLen, cy - dirY * halfLen };
	const Vector2 e2{ cx + dirX * halfLen, cy + dirY * halfLen };
	// 断裂線の走り込みを「右→左」に固定するため、画面右端を P1・左端を P2 に並べる
	// （dirX>=0 なら e2 が右側、そうでなければ e1 が右側）。
	const Vector2 rightPx = (dirX >= 0.0f) ? e2 : e1;
	const Vector2 leftPx  = (dirX >= 0.0f) ? e1 : e2;
	cutWorldP1_ = pixelToWorldAtDepth(rightPx); // 右端（断裂の走り始め）
	cutWorldP2_ = pixelToWorldAtDepth(leftPx);  // 左端
	cutWorldValid_ = true;
	// 発射ビームはここでは作らない。カメラが発射ショット位置へ回り込み切ってから BuildFireVisuals() で生成する。
}

void ShatterlinePart::TrashVisual(std::unique_ptr<EffectPrimitiveRenderer>& r) {
	if (!r) return;
	TrashEntry e;
	e.renderer = std::move(r);
	trash_.push_back(std::move(e));
}

void ShatterlinePart::BuildFireVisuals() {
	// 固定発射方向（ワールド）。プレイヤーは正面構えなのでこれと整合。狙い角度とは無関係。
	Vector3 d = beamDir_;
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	beamFireDir_ = (len > kMinNormalizeLength) ? Vector3{ d.x / len, d.y / len, d.z / len } : Vector3{ 0.0f, 0.0f, 1.0f };
	const Vector3 player  = host_->GetSpecialPlayerCenter();
	beamMuzzle_  = host_->GetSpecialMuzzle(player, beamFireDir_);
	const Vector3 euler   = DirToEuler(beamFireDir_);

	// 発射ビーム：EffectDef(ShatterlineBeam) を筒先で再生し、確定発射方向へ向ける。
	// 色/サイズ/先細り/フェード/伸長カーブは JSON＝エフェクトエディターで編集。伸長は scaleCurve が駆動。
	// World 停止中(Slash)でも伸びるよう、Update は Slash の間だけ実時間で回す（Update 内で分岐）。
	// 衝撃波リングはこの EffectDef に Ring コンポーネントを足してエディターで作る（直書きは廃止）。
	if (auto* em = EffectManager::GetInstance()) {
		if (beamHandle_ != kInvalidEffectHandle) em->Stop(beamHandle_);
		beamHandle_ = kInvalidEffectHandle;
		if (!beamEffectName_.empty() && em->HasDef(beamEffectName_)) {
			beamHandle_ = em->Play(beamEffectName_, beamMuzzle_);
			em->SetRotation(beamHandle_, euler);
		}
	}
}

void ShatterlinePart::BuildRift() {
	TrashVisual(rift_);
	if (!cutWorldValid_) return;
	const Vector3 d{ cutWorldP2_.x - cutWorldP1_.x,
	                 cutWorldP2_.y - cutWorldP1_.y,
	                 cutWorldP2_.z - cutWorldP1_.z };
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (len < kMinLength) return;

	// 案1：判定帯の世界幅（全幅 = 半幅×2）に倍率を掛けてビーム幅に使う。深度換算済みなので
	// 近くを斬っても遠くを斬っても画面上は判定帯と同じ太さに見える。
	const float fullWidth = 2.0f * riftBakedHalfWidth_ * riftWidthScale_;
	PrimitiveGenerator::BeamParams bp{};
	bp.startPos = { 0.0f, 0.0f, 0.0f };
	bp.endPos   = { 0.0f, 0.0f, len };
	bp.appearance.startWidth = fullWidth;
	bp.appearance.endWidth   = fullWidth;
	bp.appearance.planeCount = kRiftPlaneCount;
	bp.appearance.startColor = riftColor_;
	bp.appearance.endColor   = riftColor_;
	bp.appearance.fadeStartLength = 0.0f;
	bp.appearance.fadeEndLength   = 0.0f;
	bp.lengthSegments = kRiftLengthSegments;
	rift_ = std::make_unique<EffectPrimitiveRenderer>();
	rift_->Initialize(EffectMeshType::Beam, "Resources/Textures/white1x1.dds", {}, {}, {}, bp);
	rift_->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
	rift_->SetDepthWrite(false);
	rift_->SetCullBackface(false);
	rift_->SetTranslate(cutWorldP1_); // 右端から
	rift_->SetRotate(DirToEuler({ d.x / len, d.y / len, d.z / len }));
	rift_->SetScale({ 1.0f, 1.0f, 0.0f });     // 走り込み 0 から（右→左）
	riftTimer_ = 0.0f;
}

void ShatterlinePart::ExecuteKills() {
	if (killsDone_) return;
	killsDone_ = true;

	// 敵弾：線上に収集済みのものを消滅（まだ生きているものだけ）
	for (IImGuiEditable* prim : pendingBullets_) {
		host_->DestroyEnemyBullet(prim);
	}
	pendingBullets_.clear();

	// 敵本体：収集時点のポインタを、まだ生存しているものだけキル（凍結中なので基本は全員生存）。
	// 通常敵=即死（HP持ちは HP=0 で既存 kill-t/スコア/SweepDead 経路へ）。ボス=固定ダメージ。
	for (IImGuiEditable* e : pendingEnemies_) {
		if (!host_->IsEnemyAlive(e)) continue;
		const EntityTag tag = Gameplay::Of(e).GetTag();
		if (tag == EntityTag::Boss) {
			Gameplay::Of(e).GetHP().TakeDamage(bossDamage_);
		} else {
			HP& hp = Gameplay::Of(e).GetHP();
			if (hp.enabled) {
				hp.TakeDamage(hp.currentHP);
			} else {
				host_->DestroyEnemyWithScore(e);
			}
		}
	}
	pendingEnemies_.clear();
}

void ShatterlinePart::UpdateVisuals(float realDt) {
	if (!camera_) return;
	auto smooth = [](float u) { if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f; return u * u * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * u); };

	// 発射ビーム・衝撃波は EffectDef(ShatterlineBeam) 化済み。伸長/拡大・描画・更新は EffectManager が担う
	// （Slash 中は World 停止だが、Update 内で Slash の間だけ EffectManager を実時間で回して進める）。
	// 断裂線：Collapse 中に右→左へ走り込む。引き切った瞬間に敵を一括キル。
	if (rift_) {
		// カメラが狙うアングルへ戻り切ってから断裂線を走り込ませる（復帰中は止める）。
		if (phase_ == Phase::Collapse && camArrived_) {
			riftTimer_ += realDt;
			const float raw = (riftRevealTime_ > kMinSeconds) ? (riftTimer_ / riftRevealTime_) : 1.0f;
			rift_->SetScale({ 1.0f, 1.0f, smooth(raw) });
			if (raw >= 1.0f) ExecuteKills(); // 線が画面に入り切ったら全部同時に斬る
		}
		rift_->Update(camera_, realDt);
	}

	// Step6: 崩壊リビール＋色反転（PostEffect）の更新。境界はシェーダで軽くギザギザに。
	UpdateReveal();

	// F3: 事前分割セルを境界進行で順に割って飛散させる
	UpdateCells(realDt);
}

void ShatterlinePart::UpdateReveal() {
	auto* pe = Game::GetPostEffect();
	if (!pe || !pe->colorInvert) return;
	auto* rev = pe->colorInvert; // 全画面色反転は汎用 ColorInvert を流用

	// 反転シェーダを使うのは (a) Slash 後半の全画面反転、(b) Collapse でセル殻が描けるまでの全反転ブリッジ、のみ。
	// セルが準備でき次第（メッシュ＋キャプチャ完了）反転を切る＝背景は通常色に戻り、
	// 以後は「未割れセルの静止反転シャード＝殻」が反転を担い、割れた所から下の通常色が覗く（段階戻しはセルが担当）。
	const bool cellsReady = cellMeshUploaded_ && captureDone_;
	const bool slashInvert = (phase_ == Phase::Slash) &&
	                         fired_ && (fireElapsed_ >= invertDelay_);
	const bool collapseBridge = (phase_ == Phase::Collapse) && !cellsReady;
	const bool active = slashInvert || collapseBridge;
	if (!active || !camera_) {
		rev->SetEnabled(false);
		return;
	}

	rev->SetEnabled(true);
	rev->SetIntensity(revealIntensity_); // 1=完全反転
}

float ShatterlinePart::CollapseRevealT() const {
	const float span  = collapseDuration_ - revealStartDelay_;
	const float local = collapseRevealTimer_ - revealStartDelay_;
	if (local <= 0.0f) return 0.0f;
	const float raw = (span > kMinSeconds) ? (local / span) : 1.0f;
	const float u = std::clamp(raw, 0.0f, 1.0f);
	return u * u * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * u); // smoothstep
}

// F2: cells_（スクリーンUVの凸多角形）を、切断深度の前方平面へ射影してワールド三角形にし、
// 各頂点を「重心ローカル座標＋baked UV」にして renderer へアップロードする（崩壊中に1回）。
// 重心ワールド配置は cellCentroidWorld_ に保存し、F3 の飛散基準にも使う。
void ShatterlinePart::BuildCellMeshesAndUpload() {
	if (!shards_ || !camera_ || cells_.empty()) return;

	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	const Vector3 camPos = camera_->GetTranslate();
	const Matrix4x4 camRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 camFwd{ camRot.m[2][0], camRot.m[2][1], camRot.m[2][2] };
	const float depth = cutDepth_;
	auto uvToWorld = [&](const Vector2& uv) -> Vector3 {
		const float ndcX = uv.x * 2.0f - 1.0f;
		const float ndcY = 1.0f - uv.y * 2.0f;
		const Vector3 farW = TransformCoordinate(Vector3{ ndcX, ndcY, 1.0f }, invVP);
		Vector3 rd{ farW.x - camPos.x, farW.y - camPos.y, farW.z - camPos.z };
		const float rl = std::sqrt(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z);
		if (rl > kMinNormalizeLength) { rd.x /= rl; rd.y /= rl; rd.z /= rl; }
		float denom = rd.x * camFwd.x + rd.y * camFwd.y + rd.z * camFwd.z;
		if (denom < kMinRayDenom) denom = kMinRayDenom;
		const float t = depth / denom;
		return { camPos.x + rd.x * t, camPos.y + rd.y * t, camPos.z + rd.z * t };
	};

	std::vector<ScreenShardRenderer::CellMesh> meshes;
	meshes.reserve(cells_.size());
	cellCentroidWorld_.assign(cells_.size(), Vector3{ 0.0f, 0.0f, 0.0f });

	// 割れ順（F3）：切断線をスクリーンUV→aspect空間へ射影し、各セル重心の垂直距離 breakDist と
	// 全画面を覆う最大垂直距離 maxDist を確定する（F1 の割れ順プレビューと同じ式＝確認した順序と一致）。
	const float w = static_cast<float>(WindowsApplication::kUIWidth);
	const float h = static_cast<float>(WindowsApplication::kUIHeight);
	const float aspect = (h > 1.0f) ? (w / h) : (kFallbackAspectWidth / kFallbackAspectHeight);
	Vector2 cutA{ 0.0f, 0.5f }, cutB{ 1.0f, 0.5f };
	if (cutWorldValid_) {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		auto worldToUV = [&](const Vector3& wld, Vector2& uv) {
			const float wx = wld.x * vp.m[0][0] + wld.y * vp.m[1][0] + wld.z * vp.m[2][0] + vp.m[3][0];
			const float wy = wld.x * vp.m[0][1] + wld.y * vp.m[1][1] + wld.z * vp.m[2][1] + vp.m[3][1];
			float ww = wld.x * vp.m[0][3] + wld.y * vp.m[1][3] + wld.z * vp.m[2][3] + vp.m[3][3];
			if (ww <= kMinClipW) ww = kMinClipW;
			uv = { (wx / ww) * 0.5f + 0.5f, 1.0f - ((wy / ww) * 0.5f + 0.5f) };
		};
		worldToUV(cutWorldP1_, cutA);
		worldToUV(cutWorldP2_, cutB);
	}
	const Vector2 cutAa{ cutA.x * aspect, cutA.y };
	const Vector2 cutBa{ cutB.x * aspect, cutB.y };
	Vector2 cutDir{ cutBa.x - cutAa.x, cutBa.y - cutAa.y };
	const float cutLen = std::sqrt(cutDir.x * cutDir.x + cutDir.y * cutDir.y);
	if (cutLen > kMinVectorLength) { cutDir.x /= cutLen; cutDir.y /= cutLen; } else { cutDir = { 1.0f, 0.0f }; }
	const Vector2 cutPerp{ -cutDir.y, cutDir.x };
	auto perpDist = [&](float px, float py) { return std::abs((px - cutAa.x) * cutPerp.x + (py - cutAa.y) * cutPerp.y); };
	cellMaxBreakDist_ = (std::max)((std::max)(perpDist(0.0f, 0.0f), perpDist(aspect, 0.0f)),
	                                        (std::max)(perpDist(0.0f, 1.0f), perpDist(aspect, 1.0f))) * kBreakDistMargin;
	cellRuntime_.assign(cells_.size(), CellRuntime{});

	for (size_t ci = 0; ci < cells_.size(); ++ci) {
		const Cell& cell = cells_[ci];
		const size_t m = cell.polyUV.size();
		const Vector3 cW = uvToWorld(cell.centroidUV);
		cellCentroidWorld_[ci] = cW;
		cellRuntime_[ci].breakDist = perpDist(cell.centroidUV.x * aspect, cell.centroidUV.y);

		ScreenShardRenderer::CellMesh mesh;
		mesh.verts.reserve(m * kVertsPerTriangle);
		auto push = [&](const Vector3& w, const Vector2& uv) {
			ScreenShardRenderer::Vertex v;
			v.pos[0] = w.x - cW.x; v.pos[1] = w.y - cW.y; v.pos[2] = w.z - cW.z; // 重心ローカル
			v.uv[0] = uv.x; v.uv[1] = uv.y;                                       // baked UV
			mesh.verts.push_back(v);
		};
		for (size_t k = 0; k < m; ++k) {
			const Vector2& uv0 = cell.polyUV[k];
			const Vector2& uv1 = cell.polyUV[(k + 1) % m];
			// 扇状三角形 (重心, v[k], v[k+1])
			push(cW, cell.centroidUV);
			push(uvToWorld(uv0), uv0);
			push(uvToWorld(uv1), uv1);
		}
		meshes.push_back(std::move(mesh));
	}

	shards_->SetCells(meshes);
	cellMeshUploaded_ = true;
}

// F3: リビール境界（halfW = revealT × maxDist）が届いたセルから順に割って飛散させる。
// 割れた直後は freeze 秒だけ静止（バリン）→ 視点方向 pop ＋ ランダム拡散で飛び、重力で落ち、重心中心に回転、寿命でフェード。
// World 停止中でも realDt で進む（リビール進捗と同じ実時間駆動）。
void ShatterlinePart::UpdateCells(float realDt) {
	if (phase_ != Phase::Collapse) return;
	if (!camArrived_) return;
	// 断裂線が走り切ってキャプチャ＆殻が用意できてから割り始める（殻が出る前に割れないように）。
	if (!cellMeshUploaded_ || !captureDone_) return;
	if (cells_.empty() || cellRuntime_.size() != cells_.size()) return;

	const float revealT = CollapseRevealT();
	const float halfW = revealT * cellMaxBreakDist_;

	static thread_local std::mt19937 rng(
		static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
	auto randRange = [&](float a, float b) { return a + dist01(rng) * (std::max)(b - a, 0.0f); };
	auto randSym   = [&]() { return dist01(rng) * 2.0f - 1.0f; };

	const Vector3 camPos = camera_ ? camera_->GetTranslate() : Vector3{ 0.0f, 0.0f, 0.0f };
	const float spinMax = fragSpin_;

	// 重力＝各破片を「最も近い断裂線上の点」へ引く（線に吸い込まれる感）。断裂線 P1→P2 を線分として最近点を取る。
	const Vector3 lineP1 = cutWorldP1_;
	const Vector3 lineW{ cutWorldP2_.x - cutWorldP1_.x,
	                     cutWorldP2_.y - cutWorldP1_.y,
	                     cutWorldP2_.z - cutWorldP1_.z };
	const float lineWW = lineW.x * lineW.x + lineW.y * lineW.y + lineW.z * lineW.z;

	for (size_t ci = 0; ci < cellRuntime_.size(); ++ci) {
		CellRuntime& rt = cellRuntime_[ci];

		if (!rt.broken) {
			// 境界が重心へ届いたら割れる
			if (rt.breakDist <= halfW) {
				rt.broken  = true;
				rt.age     = 0.0f;
				rt.lifeDur = (std::max)(randRange(breakLifeMin_, breakLifeMax_), kMinBreakLifeSeconds);
				// 視点方向へ飛び出す（pop）＋ ランダム拡散
				const Vector3 cW = cellCentroidWorld_[ci];
				Vector3 toCam{ camPos.x - cW.x, camPos.y - cW.y, camPos.z - cW.z };
				const float tl = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
				if (tl > kMinVectorLength) { toCam.x /= tl; toCam.y /= tl; toCam.z /= tl; }
				Vector3 rnd{ randSym(), randSym(), randSym() };
				const float rl = std::sqrt(rnd.x * rnd.x + rnd.y * rnd.y + rnd.z * rnd.z);
				if (rl > kMinVectorLength) { rnd.x /= rl; rnd.y /= rl; rnd.z /= rl; }
				rt.vel = { toCam.x * breakPopSpeed_ + rnd.x * breakSpread_,
				           toCam.y * breakPopSpeed_ + rnd.y * breakSpread_,
				           toCam.z * breakPopSpeed_ + rnd.z * breakSpread_ };
				rt.spin = { randSym() * spinMax, randSym() * spinMax, randSym() * spinMax };
				rt.rot = { 0.0f, 0.0f, 0.0f };
				rt.offset = { 0.0f, 0.0f, 0.0f };
			}
			continue;
		}

		// 既に割れている
		if (rt.age >= rt.lifeDur) continue;
		rt.age += realDt;
		// サイズ(1=発生時→0=寿命末)が設定比より小さくなったら完全に消す（α は下げない）。
		if ((1.0f - rt.age / rt.lifeDur) <= fragMinScale_) { rt.age = rt.lifeDur; continue; }
		// freeze 中は静止（バリンの溜め）。それ以降に飛散＋回転＋重力。
		if (rt.age > breakFreeze_) {
			rt.offset.x += rt.vel.x * realDt;
			rt.offset.y += rt.vel.y * realDt;
			rt.offset.z += rt.vel.z * realDt;
			// 現在位置から最も近い断裂線上の点へ引く（線分 P1..P2 への最近点）
			const Vector3 cWcur = cellCentroidWorld_[ci];
			const Vector3 Q{ cWcur.x + rt.offset.x, cWcur.y + rt.offset.y, cWcur.z + rt.offset.z };
			float tt = (lineWW > kMinNormalizeLength)
				? ((Q.x - lineP1.x) * lineW.x + (Q.y - lineP1.y) * lineW.y + (Q.z - lineP1.z) * lineW.z) / lineWW
				: 0.0f;
			tt = std::clamp(tt, 0.0f, 1.0f);
			Vector3 gdir{ (lineP1.x + lineW.x * tt) - Q.x,
			              (lineP1.y + lineW.y * tt) - Q.y,
			              (lineP1.z + lineW.z * tt) - Q.z };
			const float gl = std::sqrt(gdir.x * gdir.x + gdir.y * gdir.y + gdir.z * gdir.z);
			if (gl > kMinVectorLength) { gdir.x /= gl; gdir.y /= gl; gdir.z /= gl; }
			rt.vel.x += gdir.x * breakGravity_ * realDt;
			rt.vel.y += gdir.y * breakGravity_ * realDt;
			rt.vel.z += gdir.z * breakGravity_ * realDt;
			rt.rot.x += rt.spin.x * realDt;
			rt.rot.y += rt.spin.y * realDt;
			rt.rot.z += rt.spin.z * realDt;
		}
	}
}

void ShatterlinePart::DrawShards() {
	if (!shards_ || !camera_) return;
	auto* pe = Game::GetPostEffect();
	if (!pe) return;
	// 崩壊中、シーンキャプチャ完了後のみ描く（capture が反転元絵。camera 到達後でもある）。
	if (phase_ != Phase::Collapse || !captureDone_) return;
	if (cells_.empty()) return;
	if (!cellMeshUploaded_) BuildCellMeshesAndUpload();

	std::vector<ScreenShardRenderer::DrawItem> items;

	if (cellStaticDraw_) {
		// F2: 全セルを静止描画（rest）＝baked UV のマッピング確認用（デバッグ）。
		items.reserve(cellCentroidWorld_.size());
		for (size_t ci = 0; ci < cellCentroidWorld_.size(); ++ci) {
			ScreenShardRenderer::DrawItem it;
			it.cellIndex = static_cast<uint32_t>(ci);
			Transform t{};
			t.scale = { 1.0f, 1.0f, 1.0f };
			t.rotate = { 0.0f, 0.0f, 0.0f };
			t.translate = cellCentroidWorld_[ci];
			it.world = MakeAffineMatrix(t);
			it.alpha = fragAlpha_;
			it.satBoost = fragSatBoost_;
			items.push_back(it);
		}
	} else {
		// F3: 未割れセル＝静止した反転シャードで“殻”を作り、割れたセルは飛散させる。
		// 飛び終えたセルは描かない＝その場所は下の通常色が残る（＝色がセル形で戻る）。
		items.reserve(cellRuntime_.size());
		for (size_t ci = 0; ci < cellRuntime_.size(); ++ci) {
			const CellRuntime& rt = cellRuntime_[ci];
			ScreenShardRenderer::DrawItem it;
			it.cellIndex = static_cast<uint32_t>(ci);
			it.satBoost = fragSatBoost_;
			Transform t{};
			t.scale = { 1.0f, 1.0f, 1.0f };
			if (!rt.broken) {
				// 殻：静止・不透明（反転シャードが画面を覆う＝反転して見える）
				t.rotate = { 0.0f, 0.0f, 0.0f };
				t.translate = cellCentroidWorld_[ci];
				it.world = MakeAffineMatrix(t);
				it.alpha = fragAlpha_;
			} else {
				if (rt.age >= rt.lifeDur) continue; // 飛び終わり＝描かない（通常色が残る）
				// α は一定（下げない）。サイズだけ縮める＝重心へ向かって小さくなる。
				const float lifeN = (rt.lifeDur > kMinSeconds) ? (rt.age / rt.lifeDur) : 1.0f;
				const float sc = 1.0f - lifeN;
				t.scale = { sc, sc, sc };
				t.rotate = rt.rot; // 重心ローカル頂点を回す＝重心中心の回転
				t.translate = { cellCentroidWorld_[ci].x + rt.offset.x,
				                cellCentroidWorld_[ci].y + rt.offset.y,
				                cellCentroidWorld_[ci].z + rt.offset.z };
				it.world = MakeAffineMatrix(t);
				it.alpha = fragAlpha_;
			}
			items.push_back(it);
		}
	}

	if (items.empty()) return;
	shards_->Draw(camera_->GetViewProjectionMatrix(), pe->GetCaptureSRVIndex(), items);
}


// F1: 事前分割セルの構築（手続き Voronoi）。
// seed から種点をアスペクト補正空間に撒き、各種点のボロノイ領域を「他種点との垂直二等分線で
// 半平面クリップ（Sutherland-Hodgman）」して凸多角形セルを得る。重心も計算し、スクリーンUVで格納する。
// 種点はスクリーン空間固定＝エイム角θに非依存。同じ seed なら同じ割れ方を再現する（O(N^2)・構築1回）。
void ShatterlinePart::BuildCells() {
	cells_.clear();
	const int n = std::clamp(cellCount_, 1, kMaxCellCount);
	const float w = static_cast<float>(WindowsApplication::kUIWidth);
	const float h = static_cast<float>(WindowsApplication::kUIHeight);
	const float aspect = (h > 1.0f) ? (w / h) : (kFallbackAspectWidth / kFallbackAspectHeight);

	// 種点（アスペクト補正空間 [0,aspect]×[0,1] ＝セルが正方形寄りになる）
	std::mt19937 rng(fractureSeed_);
	std::uniform_real_distribution<float> dx(0.0f, aspect);
	std::uniform_real_distribution<float> dy(0.0f, 1.0f);
	std::vector<Vector2> seeds(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) seeds[i] = { dx(rng), dy(rng) };

	// 画面矩形（初期ポリゴン）
	const Vector2 rect[4] = { {0.0f,0.0f}, {aspect,0.0f}, {aspect,1.0f}, {0.0f,1.0f} };

	// 半平面クリップ：種点 i 側（dot(p-mid, dir) <= 0、dir = seedJ - seedI）だけ残す
	auto clipHalfPlane = [](std::vector<Vector2>& poly, const Vector2& mid, const Vector2& dir) {
		std::vector<Vector2> out;
		out.reserve(poly.size() + kClipExtraVerts);
		const size_t m = poly.size();
		for (size_t k = 0; k < m; ++k) {
			const Vector2& a = poly[k];
			const Vector2& b = poly[(k + 1) % m];
			const float fa = (a.x - mid.x) * dir.x + (a.y - mid.y) * dir.y;
			const float fb = (b.x - mid.x) * dir.x + (b.y - mid.y) * dir.y;
			const bool inA = fa <= 0.0f;
			const bool inB = fb <= 0.0f;
			if (inA) out.push_back(a);
			if (inA != inB) {
				const float t = fa / (fa - fb);
				out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
			}
		}
		poly.swap(out);
	};

	for (int i = 0; i < n; ++i) {
		std::vector<Vector2> poly(rect, rect + kRectCornerCount);
		for (int j = 0; j < n && poly.size() >= kVertsPerTriangle; ++j) {
			if (j == i) continue;
			const Vector2 dir{ seeds[j].x - seeds[i].x, seeds[j].y - seeds[i].y };
			if (dir.x * dir.x + dir.y * dir.y < kMinSeedDistanceSq) continue;
			const Vector2 mid{ (seeds[i].x + seeds[j].x) * 0.5f, (seeds[i].y + seeds[j].y) * 0.5f };
			clipHalfPlane(poly, mid, dir);
		}
		if (poly.size() < kVertsPerTriangle) continue;

		// 面積重み付き重心（縮退時は頂点平均でフォールバック）
		float area = 0.0f;
		Vector2 c{ 0.0f, 0.0f };
		const size_t m = poly.size();
		for (size_t k = 0; k < m; ++k) {
			const Vector2& a = poly[k];
			const Vector2& b = poly[(k + 1) % m];
			const float cross = a.x * b.y - b.x * a.y;
			area += cross;
			c.x += (a.x + b.x) * cross;
			c.y += (a.y + b.y) * cross;
		}
		if (std::abs(area) < kMinPolygonArea) {
			c = { 0.0f, 0.0f };
			for (const auto& p : poly) { c.x += p.x; c.y += p.y; }
			c.x /= static_cast<float>(m); c.y /= static_cast<float>(m);
		} else {
			c.x /= (kCentroidAreaFactor * area); c.y /= (kCentroidAreaFactor * area);
		}

		Cell cell;
		cell.centroidUV = { c.x / aspect, c.y };
		cell.polyUV.reserve(m);
		for (const auto& p : poly) cell.polyUV.push_back({ p.x / aspect, p.y });
		cells_.push_back(std::move(cell));
	}
}

// F1: セル境界線を割れ順で色分けして LineRenderer へ積む（デバッグ確認用）。
// 切断線をスクリーンへ射影し、各セル重心の垂直距離を maxDist で正規化＝0(線際/先に割れる)〜1(端/後)。
// cellPreviewRevealT_ を超えた片は「割れた」表示（赤）にして割れ順をなぞれる。
void ShatterlinePart::DrawCellBordersDebug() {
	auto* lr = LineRenderer::GetInstance();
	if (!lr || !camera_) return;
	if (cells_.empty()) BuildCells();
	if (cells_.empty()) return;

	const float w = static_cast<float>(WindowsApplication::kUIWidth);
	const float h = static_cast<float>(WindowsApplication::kUIHeight);
	const float aspect = (h > 1.0f) ? (w / h) : (kFallbackAspectWidth / kFallbackAspectHeight);

	// 切断線（無効時は水平線 y=0.5）をスクリーンUVへ
	Vector2 A{ 0.0f, 0.5f }, B{ 1.0f, 0.5f };
	if (cutWorldValid_) {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		auto worldToUV = [&](const Vector3& wld, Vector2& uv) {
			const float wx = wld.x * vp.m[0][0] + wld.y * vp.m[1][0] + wld.z * vp.m[2][0] + vp.m[3][0];
			const float wy = wld.x * vp.m[0][1] + wld.y * vp.m[1][1] + wld.z * vp.m[2][1] + vp.m[3][1];
			float ww = wld.x * vp.m[0][3] + wld.y * vp.m[1][3] + wld.z * vp.m[2][3] + vp.m[3][3];
			if (ww <= kMinClipW) ww = kMinClipW;
			uv = { (wx / ww) * 0.5f + 0.5f, 1.0f - ((wy / ww) * 0.5f + 0.5f) };
		};
		worldToUV(cutWorldP1_, A);
		worldToUV(cutWorldP2_, B);
	}
	const Vector2 Aa{ A.x * aspect, A.y };
	const Vector2 Ba{ B.x * aspect, B.y };
	Vector2 ldir{ Ba.x - Aa.x, Ba.y - Aa.y };
	const float llen = std::sqrt(ldir.x * ldir.x + ldir.y * ldir.y);
	if (llen > kMinVectorLength) { ldir.x /= llen; ldir.y /= llen; } else { ldir = { 1.0f, 0.0f }; }
	const Vector2 perp{ -ldir.y, ldir.x };
	auto perpDist = [&](float px, float py) { return std::abs((px - Aa.x) * perp.x + (py - Aa.y) * perp.y); };
	const float maxDist = (std::max)((std::max)(perpDist(0.0f, 0.0f), perpDist(aspect, 0.0f)),
	                                 (std::max)(perpDist(0.0f, 1.0f), perpDist(aspect, 1.0f))) * kBreakDistMargin;

	// スクリーンUV → ワールド（DrawAimLine と同じ中間深度アンプロジェクト）
	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	auto uvToWorld = [&](const Vector2& uv) -> Vector3 {
		const float ndcX = uv.x * 2.0f - 1.0f;
		const float ndcY = 1.0f - uv.y * 2.0f;
		return TransformCoordinate(Vector3{ ndcX, ndcY, kDebugNdcDepth }, invVP);
	};

	const float scrub = std::clamp(cellPreviewRevealT_, 0.0f, 1.0f);
	for (const auto& cell : cells_) {
		const float norm = (maxDist > kMinVectorLength)
			? (perpDist(cell.centroidUV.x * aspect, cell.centroidUV.y) / maxDist) : 0.0f; // 0=線際(先), 1=端(後)
		Vector4 col;
		if (norm <= scrub) col = kBrokenDebugColor;                          // 既に割れた
		else                col = { 1.0f - norm, kDebugColorGreenBase + kDebugColorGreenRange * norm, kDebugColorBlueBase + kDebugColorBlueRange * norm, 1.0f }; // 暖色(先)→寒色(後)
		const size_t m = cell.polyUV.size();
		for (size_t k = 0; k < m; ++k) {
			lr->AddLine(uvToWorld(cell.polyUV[k]), uvToWorld(cell.polyUV[(k + 1) % m]), col);
		}
	}
}

void ShatterlinePart::ApplyCamera(float realDt) {
	if (!camera_ || !camActive_) return;

	// 基準姿勢＝この時点の camera_（レール/通常追従が毎フレ書き込んだ値）。
	// Charge〜Collapse は World 停止で基準が静止し、Recover で再開してレールが動き出す。
	const Vector3   baseEye = camera_->GetTranslate();
	const Matrix4x4 baseRot = MakeCameraRotateMatrix(camera_->GetRotate());
	const Vector3 fwd   = { baseRot.m[2][0], baseRot.m[2][1], baseRot.m[2][2] }; // 画面奥
	const Vector3 right = { baseRot.m[0][0], baseRot.m[0][1], baseRot.m[0][2] }; // 画面右
	const Vector3 up    = { baseRot.m[1][0], baseRot.m[1][1], baseRot.m[1][2] }; // 画面上
	const float   baseFov = camera_->GetFovY();
	const Vector3 player  = host_->GetSpecialPlayerCenter();

	// フェーズ別の目標姿勢（eye / 注視点 look / FovY）を決める
	Vector3 tgtEye  = baseEye;
	Vector3 tgtLook = player;
	float   tgtFov  = baseFov;
	switch (phase_) {
	case Phase::Charge:
		// 狙う：後方やや引き＋持ち上げ（発動直後にスッと引く）。WYSIWYG。
		tgtEye  = { baseEye.x - fwd.x * camPullback_ + up.x * camUpAdd_,
		            baseEye.y - fwd.y * camPullback_ + up.y * camUpAdd_,
		            baseEye.z - fwd.z * camPullback_ + up.z * camUpAdd_ };
		tgtLook = player;
		tgtFov  = baseFov + camFovAdd_;
		break;
	case Phase::Slash:
		// 発射ショット：プレイヤーの左横やや前・少し下げ（あおり）から本体を撮る。
		tgtEye = { player.x - right.x * camFireSide_ + fwd.x * camFireFront_ - up.x * camFireDown_,
		           player.y - right.y * camFireSide_ + fwd.y * camFireFront_ - up.y * camFireDown_,
		           player.z - right.z * camFireSide_ + fwd.z * camFireFront_ - up.z * camFireDown_ };
		tgtLook = { player.x + up.x * camFireLookUp_,
		            player.y + up.y * camFireLookUp_,
		            player.z + up.z * camFireLookUp_ };
		tgtFov  = baseFov + camFireFovAdd_;
		break;
	case Phase::Collapse:
		// 崩壊：線を決めたとき（Charge）の「狙うアングル」へ戻す。
		// ここへ戻り切ってから断裂線が走り込み崩壊する＝描いた線どおりの向きで崩れる。
		tgtEye  = { baseEye.x - fwd.x * camPullback_ + up.x * camUpAdd_,
		            baseEye.y - fwd.y * camPullback_ + up.y * camUpAdd_,
		            baseEye.z - fwd.z * camPullback_ + up.z * camUpAdd_ };
		tgtLook = player;
		tgtFov  = baseFov + camFovAdd_;
		break;
	case Phase::Recover:
	default:
		// 後隙：通常（レール基準）へ戻す。
		tgtEye  = baseEye;
		tgtLook = player;
		tgtFov  = baseFov;
		break;
	}

	// 初回は実カメラ姿勢から移動を開始（発動直後のスナップ防止）。移動の「開始姿勢」も確定。
	if (!camInit_) {
		camEyeCur_  = baseEye;
		camLookCur_ = player;
		camFovCur_  = baseFov;
		camFromEye_  = camEyeCur_;
		camFromLook_ = camLookCur_;
		camFromFov_  = camFovCur_;
		camMoveTimer_ = 0.0f;
		camArrived_   = false;
		camMovePhase_ = phase_;
		camInit_      = true;
	}

	// フェーズが変わったら、現在姿勢を開始姿勢に固定して新しい移動を開始（手続き形式）。
	if (camMovePhase_ != phase_) {
		camFromEye_  = camEyeCur_;
		camFromLook_ = camLookCur_;
		camFromFov_  = camFovCur_;
		camMoveTimer_ = 0.0f;
		camArrived_   = false;
		camMovePhase_ = phase_;
	}

	// 決定的なフェーズ間移動：camEaseTime_ 秒で必ず完了（smoothstep）。
	// 漸近イーズと違い「到達済み」が明確なので、フェーズ遷移をこれで門番できる。
	const float dur = (camEaseTime_ > kMinSeconds) ? camEaseTime_ : kMinSeconds;
	camMoveTimer_ += realDt;
	float u = camMoveTimer_ / dur;
	if (u >= 1.0f) { u = 1.0f; camArrived_ = true; }
	const float s = u * u * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * u); // smoothstep
	auto lerp3 = [](const Vector3& a, const Vector3& b, float t) -> Vector3 {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	};
	camEyeCur_  = lerp3(camFromEye_,  tgtEye,  s);
	camLookCur_ = lerp3(camFromLook_, tgtLook, s);
	camFovCur_  = camFromFov_ + (tgtFov - camFromFov_) * s;

	// 注視点 → yaw/pitch を逆算してカメラへ適用
	Vector3 dir{ camLookCur_.x - camEyeCur_.x,
	             camLookCur_.y - camEyeCur_.y,
	             camLookCur_.z - camEyeCur_.z };
	float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (dlen < kMinLength) return; // 退避：eye と look がほぼ同一なら触らない
	dir.x /= dlen; dir.y /= dlen; dir.z /= dlen;
	const float yaw   = std::atan2(dir.x, dir.z);
	const float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));

	camera_->SetTranslate(camEyeCur_);
	camera_->SetRotate({ pitch, yaw, 0.0f });
	camera_->SetFovY(camFovCur_);
	camera_->Update();
}
