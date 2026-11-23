#include "HubEtherBet.h"
#include "Score/GameSession.h"

#include "GameOver/EtherVesselRoom.h"
#include "GameOver/EtherVessel.h"
#include "GameOver/EtherPillarFlow.h"
#include "Score/EtherState.h"
#include "Camera.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "UIPointer.h"
#include "InputAction.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Config/GameActions.h"
#include "SoundManager.h"
#include "AttractMode.h"
#include "MathUtility.h"
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// smoothstep（3u² − 2u³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3u² − 2u³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 桁区切りのカンマを入れる桁数
	constexpr int kThousandsGroupDigits = 3;
	// 純度 1 あたりの 0.1 刻みの数
	constexpr float kTenthsPerUnit = 10.0f;
	// 払える上限を 0.1 刻みに切り捨てるときの誤差の逃がし
	constexpr float kTenthsRoundEpsilon = 1e-3f;
	// 賭けを決めたときの液面の揺れの強さ
	constexpr float kBetSplashStrength = 0.006f;
	// 時間の長さの下限 [秒]（これ以下ならすぐ終わったものとして扱う）
	constexpr float kMinSeconds = 1e-3f;
	// 純度を 1 刻み動かす間隔の下限 [秒]
	constexpr float kMinStepInterval = 0.005f;
	// 表示の純度が目標とこれより近ければ目標に合わせる
	constexpr float kValueSnapEpsilon = 1e-3f;
	// ◀ ▶ の当たり判定の大きさ（矢印の大きさに対する割合）
	constexpr float kArrowHitSizeRatio = 2.2f;
	// ◀ ▶ の当たり判定の大きさの最小（帯の高さに対する割合）
	constexpr float kArrowHitBandRatio = 0.8f;
	// 中央の当たり判定の幅から引く分（矢印の当たり判定の大きさに対する割合）
	constexpr float kCenterHitTrimRatio = 1.2f;
	// 暗い帯を重ねるごとに高さを伸ばす割合
	constexpr float kBandLayerGrowth = 0.25f;
	// 外側に重ねる暗い帯の濃さ
	constexpr float kBandOuterLayerAlpha = 0.35f;
	// 暗い帯の緑
	constexpr float kBandGreen = 0.01f;
	// 暗い帯の青
	constexpr float kBandBlue = 0.03f;
	// しずくの下の円の半径（しずくの高さに対する割合）
	constexpr float kDropletRadiusRatio = 0.36f;
	// しずくを描く横線の最小の数
	constexpr int kMinDropletRows = 4;
	// しずくの上のすぼまりの太さ（円の半径に対する割合）
	constexpr float kDropletConeRatio = 0.92f;
	// しずくの上のすぼまり方の指数
	constexpr float kDropletConeExponent = 1.3f;
	// この半幅以下の横線は描かない [px]
	constexpr float kMinDropletHalfWidth = 0.05f;
	// ◀ ▶ を描く縦線の最小の数
	constexpr int kMinArrowColumns = 4;
	// 押していない ◀ ▶ の明るさ
	constexpr float kArrowIdleBrightness = 0.7f;
	// ◀ ▶ の根元の半分の高さ（矢印の大きさに対する割合）
	constexpr float kArrowHalfHeightRatio = 0.6f;
	// 所持エーテルの枠の斜めの幅（枠の高さに対する割合）
	constexpr float kHandPanelSlantRatio = 0.45f;
	// 所持エーテルの枠の横線をすき間なく重ねる量 [px]
	constexpr float kHandPanelRowOverlap = 0.5f;
	// 所持エーテルの枠の暗い塗りの不透明度
	constexpr float kHandPanelAlpha = 0.7f;
	// 所持エーテルの枠の下の金の線の太さ [px]
	constexpr float kHandPanelUnderlineThickness = 2.0f;
	// 所持・消費の数字の右の余白 [px]
	constexpr float kHandTextRightPadding = 24.0f;
	// 所持の数字としずくの印の間 [px]
	constexpr float kHandDropletGap = 26.0f;
	// 所持のしずくの印の高さ（文字の高さに対する割合）
	constexpr float kHandDropletHeightRatio = 0.7f;
	// 数字の文字の縁取りの太さ
	constexpr float kTextOutlineWidth = 2.0f;
	// 決定したときに消費分が出てくる長さ [秒]
	constexpr float kCostAppearSeconds = 0.4f;
	// 消費分の文字の大きさ（所持の文字に対する割合）
	constexpr float kCostScaleRatio = 0.8f;
	// 消費分を所持の枠の下からどれだけ下に置くか [px]
	constexpr float kCostOffsetY = 12.0f;
	// 消費分が出てくるときに滑り込む距離 [px]
	constexpr float kCostSlideY = 10.0f;
	// 消費分・操作の案内の縁取りの不透明度
	constexpr float kOutlineAlpha = 0.8f;
	// 帯のしずくの印の高さ（文字の高さに対する割合）
	constexpr float kBandDropletHeightRatio = 0.65f;
	// 帯のしずくの印と数字の間 [px]
	constexpr float kBandDropletGap = 14.0f;
	// しずくの印が占める幅（しずくの高さに対する割合）
	constexpr float kDropletWidthRatio = 0.75f;
	// 操作の案内を帯の下端からどれだけ下に置くか [px]
	constexpr float kHintOffsetY = 18.0f;
	// 操作の案内の文字の赤
	constexpr float kHintColorRed = 0.62f;
	// 操作の案内の文字の緑
	constexpr float kHintColorGreen = 0.64f;
	// 操作の案内の文字の青
	constexpr float kHintColorBlue = 0.68f;

	// 器の間の見た目（背景・水面・器）とカメラの既定はゲームオーバー画面と同じ値を読む
	constexpr const char* kGameOverTuningPath = "Resources/Json/Tuning/GameOver.json";
	// TextRenderer のフォントの高さ（Framework::Initialize の MPLUS1p 32px）
	constexpr float kFontPixelHeight = 32.0f;
	// 所持エーテルの表示の上限（今は無限の扱い。2026-10-27 ユーザーのスケッチ）
	constexpr int kHandDisplayMax = 99999;
	const Vector4 kEtherUiColor{ 0.55f, 0.85f, 1.0f, 1.0f };

	/// <summary>0〜1 に切ってから smoothstep をかける</summary>
	/// <param name="u">入力</param>
	/// <returns>0〜1 のなめらかな値</returns>
	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * u);
	}

	/// <summary>JSON のキーの値を float として読む（無ければ今の値のまま）</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="value">読み込み先</param>
	void ReadFloat(const JsonValue& node, const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	}
	/// <summary>float の値を JSON のキーへ書く</summary>
	/// <param name="node">書き込み先の JSON オブジェクト</param>
	/// <param name="key">キー</param>
	/// <param name="value">書く値</param>
	void WriteFloat(JsonValue& node, const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	}

	/// <summary>整数を 3 桁ごとにカンマを入れた文字列にする（例：11,000）</summary>
	/// <param name="value">整数</param>
	/// <returns>カンマ入りの文字列</returns>
	std::string FormatThousands(int value) {
		std::string digits = std::to_string(value < 0 ? -value : value);
		std::string out;
		for (size_t i = 0; i < digits.size(); ++i) {
			if (i > 0 && (digits.size() - i) % kThousandsGroupDigits == 0) out += ',';
			out += digits[i];
		}
		return value < 0 ? "-" + out : out;
	}
}

HubEtherBet::HubEtherBet() = default;
HubEtherBet::~HubEtherBet() = default;

void HubEtherBet::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	SpriteManager* spriteManager) {
	dxCore_ = dxCore;
	room_ = std::make_unique<EtherVesselRoom>();
	room_->Initialize(dxCore, srvManager, object3DManager, spriteManager);
	if (std::filesystem::exists(kGameOverTuningPath)) {
		const auto result = JsonParser::ParseFile(kGameOverTuningPath);
		if (result.success) {
			room_->LoadTuning(result.value);
			// カメラはゲームオーバー画面の引いた位置（Hub.json の etherBet.camera があればそちらが勝つ）
			cameraPos_ = ReadVector3(result.value["camera"]["farPos"], cameraPos_);
			cameraRotDeg_ = ReadVector3(result.value["camera"]["farRotDeg"], cameraRotDeg_);
		}
	}
}

void HubEtherBet::Finalize() {
	if (room_) room_->Finalize();
}

Camera* HubEtherBet::GetCamera() const {
	return room_ ? room_->GetCamera() : nullptr;
}

void HubEtherBet::Begin() {
	room_->Activate();
	room_->SetCameraPose(cameraPos_, cameraRotDeg_);

	EtherState* ether = gameSession_->GetEtherState();
	valueTenths_ = static_cast<int>(std::lround(ether->GetDifficulty() * kTenthsPerUnit));
	const int maxTenths = static_cast<int>(std::floor(ether->GetMaxAffordableDifficulty() * kTenthsPerUnit + kTenthsRoundEpsilon));
	const int minTenths = static_cast<int>(std::lround(ether->GetDifficultyMin() * kTenthsPerUnit));
	valueTenths_ = std::clamp(valueTenths_, minTenths, (std::max)(maxTenths, minTenths));
	shownValue_ = GetValue();

	EtherVessel* vessel = room_->GetVessel();
	vessel->StopRelease();
	vessel->SetGlowBoost(1.0f);
	vessel->SetDifficulty01(ether->DifficultyTo01(shownValue_), true);
	vessel->SetFill(ether->BetToFill(ether->CostOf(shownValue_)), true);
	if (EtherPillarFlow* flow = vessel->GetPillarFlow()) {
		flow->StopAscend();
		flow->StopDescend();
	}

	step_ = Step::Adjust;
	stepTime_ = 0.0f;
	uiAlpha_ = 0.0f;
	heldDirection_ = 0;
	holdTime_ = 0.0f;
	repeatTimer_ = 0.0f;
	flowTimer_ = 0.0f;
	flowDirection_ = 0;
	hoverArrow_ = 0;
	attractTimer_ = 0.0f;
	attractStepTimer_ = 0.0f;
	confirmedCost_ = 0.0f;
}

bool HubEtherBet::Step10(int tenths) {
	EtherState* ether = gameSession_->GetEtherState();
	const int minTenths = static_cast<int>(std::lround(ether->GetDifficultyMin() * kTenthsPerUnit));
	const int maxTenths = (std::max)(static_cast<int>(std::floor(ether->GetMaxAffordableDifficulty() * kTenthsPerUnit + kTenthsRoundEpsilon)), minTenths);
	const int next = std::clamp(valueTenths_ + tenths, minTenths, maxTenths);
	if (next == valueTenths_) return false;
	const int direction = next > valueTenths_ ? 1 : -1;
	valueTenths_ = next;
	SoundManager::GetInstance()->Play2DSound("se_ui_cursor");

	// 上げる＝泉から支柱を昇って器へ、下げる＝泉へ下りる（§4.5）
	flowTimer_ = 0.0f;
	if (direction != flowDirection_) {
		flowDirection_ = direction;
		if (EtherPillarFlow* flow = room_->GetVessel()->GetPillarFlow()) {
			if (direction > 0) {
				flow->StopDescend();
				flow->BeginAscend();
			} else {
				flow->StopAscend();
				flow->BeginDescend();
			}
		}
	}
	return true;
}

void HubEtherBet::Confirm() {
	EtherState* ether = gameSession_->GetEtherState();
	ether->PlaceBet(GetValue());
	confirmedCost_ = ether->GetBet();
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
	room_->GetVessel()->Splash(kBetSplashStrength);
	room_->EmitCursorRing();
	step_ = Step::Confirming;
	stepTime_ = 0.0f;
	heldDirection_ = 0;
}

HubEtherBet::Result HubEtherBet::Update(float deltaTime, InputManager* input, bool inputEnabled, bool attract) {
	Result result = Result::None;
	InputActionMap* actions = input ? input->GetActionMap() : nullptr;
	KeyboardInput* keyboard = input ? input->GetKeyboard() : nullptr;
	const UIPointer pointerValue = UIPointer::FromInput(input);
	const UIPointer* pointer = &pointerValue;
	stepTime_ += deltaTime;
	if (inputEnabled || step_ != Step::Adjust) {
		uiAlpha_ = uiFadeSeconds_ > kMinSeconds ? (std::min)(uiAlpha_ + deltaTime / uiFadeSeconds_, 1.0f) : 1.0f;
	}

	switch (step_) {
	case Step::Adjust: {
		if (!inputEnabled || !actions) {
			heldDirection_ = 0;
			break;
		}
		// デモモード：少し見せてから、人が長押ししたときのように純度を 9.0 まで上げ（液面が上がり、泉から光が昇る）、着いたら少し待って決める
		if (attract) {
			const AttractMode::Settings& attractSettings = AttractMode::GetInstance()->GetSettings();
			EtherState* ether = gameSession_->GetEtherState();
			const float targetPurity = (std::min)(attractSettings.betTargetPurity, ether->GetMaxAffordableDifficulty());
			const int targetTenths = static_cast<int>(std::lround(targetPurity * kTenthsPerUnit));
			attractTimer_ += deltaTime;
			if (valueTenths_ != targetTenths) {
				if (attractTimer_ >= attractSettings.betRaiseDelay) {
					const int direction = targetTenths > valueTenths_ ? 1 : -1;
					heldDirection_ = direction;   // ◀ ▶ を押しているように光らせる
					attractStepTimer_ -= deltaTime;
					while (attractStepTimer_ <= 0.0f && valueTenths_ != targetTenths) {
						attractStepTimer_ += (std::max)(attractSettings.betStepInterval, kMinStepInterval);
						if (!Step10(direction)) break;
					}
				}
				if (valueTenths_ == targetTenths) {
					heldDirection_ = 0;
					attractTimer_ = attractSettings.betRaiseDelay;   // ここから betConfirmWait を数える
				}
			} else {
				heldDirection_ = 0;
				if (attractTimer_ >= attractSettings.betRaiseDelay + attractSettings.betConfirmWait) Confirm();
			}
			break;
		}
		if (actions->IsTriggered(static_cast<int>(Action::MenuCancel))) {
			SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
			result = Result::Canceled;
			break;
		}

		// マウスが ◀ ▶ の上にあるか（中央のクリックは決定）
		hoverArrow_ = 0;
		bool clickedCenter = false;
		if (pointer->valid) {
			Rect left, center, right;
			LayoutBand(left, center, right);
			if (left.Contains(pointer->position)) hoverArrow_ = -1;
			if (right.Contains(pointer->position)) hoverArrow_ = 1;
			if (pointer->clicked && hoverArrow_ == 0 && center.Contains(pointer->position)) clickedCenter = true;
		}

		// 左右・A/D・◀ ▶ の長押し：押した瞬間に 0.1、押し続けると repeatDelay 後から連続で（だんだん速く）。
		// A/D はキー設定（MenuLeft/Right）を変えずにここだけで読む（ほかのメニューの操作は変えない。2026-10-27 ユーザー）
		const bool keyA = keyboard && keyboard->PushKey(DIK_A);
		const bool keyD = keyboard && keyboard->PushKey(DIK_D);
		const bool mouseLeft = pointer->valid && pointer->pressed && hoverArrow_ < 0;
		const bool mouseRight = pointer->valid && pointer->pressed && hoverArrow_ > 0;
		const bool leftPressed = actions->IsPressed(static_cast<int>(Action::MenuLeft)) || keyA || mouseLeft;
		const bool rightPressed = actions->IsPressed(static_cast<int>(Action::MenuRight)) || keyD || mouseRight;
		const int direction = leftPressed == rightPressed ? 0 : (rightPressed ? 1 : -1);
		const bool leftTriggered = actions->IsTriggered(static_cast<int>(Action::MenuLeft))
			|| (keyboard && keyboard->TriggerKey(DIK_A)) || (pointer->clicked && hoverArrow_ < 0);
		const bool rightTriggered = actions->IsTriggered(static_cast<int>(Action::MenuRight))
			|| (keyboard && keyboard->TriggerKey(DIK_D)) || (pointer->clicked && hoverArrow_ > 0);
		if ((leftTriggered && direction < 0) || (rightTriggered && direction > 0)) {
			if (Step10(direction)) room_->EmitCursorRing();
			heldDirection_ = direction;
			holdTime_ = 0.0f;
			repeatTimer_ = repeatDelay_;
		} else if (direction != 0 && direction == heldDirection_) {
			holdTime_ += deltaTime;
			repeatTimer_ -= deltaTime;
			while (repeatTimer_ <= 0.0f) {
				const float u = repeatAccelSeconds_ > kMinSeconds
					? std::clamp((holdTime_ - repeatDelay_) / repeatAccelSeconds_, 0.0f, 1.0f) : 1.0f;
				repeatTimer_ += (std::max)(repeatIntervalStart_ + (repeatIntervalEnd_ - repeatIntervalStart_) * u, kMinStepInterval);
				if (!Step10(direction)) {
					repeatTimer_ = repeatDelay_;
					break;
				}
			}
		} else {
			heldDirection_ = 0;
		}

		if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm)) || clickedCenter) Confirm();
		break;
	}
	case Step::Confirming: {
		// 球の光が強まって（賭けたエーテルで機体を強くする）、少し待ってから戻る
		const float t = confirmSeconds_ > kMinSeconds ? std::clamp(stepTime_ / confirmSeconds_, 0.0f, 1.0f) : 1.0f;
		room_->GetVessel()->SetGlowBoost(1.0f + (confirmGlowBoost_ - 1.0f) * SmoothStep01(t * 2.0f));
		if (t >= 1.0f) {
			step_ = Step::Finished;
			result = Result::Confirmed;
		}
		break;
	}
	case Step::Finished:
		break;
	}

	// 値が止まってしばらくしたら支柱の光を止める
	flowTimer_ += deltaTime;
	if (flowDirection_ != 0 && flowTimer_ >= flowHoldSeconds_) {
		flowDirection_ = 0;
		if (EtherPillarFlow* flow = room_->GetVessel()->GetPillarFlow()) {
			flow->StopAscend();
			flow->StopDescend();
		}
	}

	UpdateVessel(deltaTime);
	room_->SetCameraPose(cameraPos_, cameraRotDeg_);
	room_->Update(deltaTime);
	return result;
}

void HubEtherBet::UpdateVessel(float deltaTime) {
	// 針と光は値へなめらかに寄せる（毎回カチッと動かすと、押し続けたときに行き過ぎが重なって暴れる）。
	// 液面は器の側の追従（fillFollow）で遅れて上がり下がりする
	EtherState* ether = gameSession_->GetEtherState();
	const float target = GetValue();
	shownValue_ += (target - shownValue_) * (1.0f - std::exp(-followSpeed_ * deltaTime));
	if (std::fabs(target - shownValue_) < kValueSnapEpsilon) shownValue_ = target;
	EtherVessel* vessel = room_->GetVessel();
	vessel->SetDifficulty01(ether->DifficultyTo01(shownValue_), true);
	vessel->SetFill(ether->BetToFill(ether->CostOf(target)), false);
}

void HubEtherBet::LayoutBand(Rect& left, Rect& center, Rect& right) const {
	const float w = static_cast<float>(dxCore_->GetUIWidth());
	const float h = static_cast<float>(dxCore_->GetUIHeight());
	const float cy = h * bandYRatio_;
	const float hit = (std::max)(arrowSize_ * kArrowHitSizeRatio, bandHeight_ * kArrowHitBandRatio);
	left = { w * 0.5f - arrowSpread_ - hit * 0.5f, cy - hit * 0.5f, hit, hit };
	right = { w * 0.5f + arrowSpread_ - hit * 0.5f, cy - hit * 0.5f, hit, hit };
	const float centerWidth = arrowSpread_ * 2.0f - hit * kCenterHitTrimRatio;
	center = { w * 0.5f - centerWidth * 0.5f, cy - bandHeight_ * 0.5f, centerWidth, bandHeight_ };
}

void HubEtherBet::Draw() {
	room_->Draw();
	DrawUi();
}

void HubEtherBet::DrawBloom() {
	room_->DrawBloom();
}

void HubEtherBet::DrawBand(float centerY, float height, float alpha) const {
	// 見やすいように後ろに暗い帯（ゲームオーバー画面の選択肢と同じ作り。左右の端と上下はぼかす）
	TextRenderer* tr = TextRenderer::GetInstance();
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	constexpr int kColumns = 32;
	constexpr int kRows = 3;
	const float columnW = screenW / static_cast<float>(kColumns);
	for (int c = 0; c < kColumns; ++c) {
		const float u = (static_cast<float>(c) + 0.5f) / static_cast<float>(kColumns);
		const float edge = SmoothStep01((std::min)(u, 1.0f - u) / (std::max)(bandEdgeFade_, kMinSeconds));
		for (int r = 0; r < kRows; ++r) {
			const float bh = height * (1.0f + static_cast<float>(r) * kBandLayerGrowth);
			const float k = r == 0 ? 1.0f : kBandOuterLayerAlpha;
			tr->DrawRect({ columnW * static_cast<float>(c), centerY - bh * 0.5f }, { columnW + 1.0f, bh },
				{ 0.0f, kBandGreen, kBandBlue, bandAlpha_ * edge * k * alpha });
		}
	}
}

void HubEtherBet::DrawDroplet(const Vector2& center, float height, float alpha) const {
	// しずくの印（絵が無いので 1px の横線を重ねる）：下が円、上へすぼまって尖る
	TextRenderer* tr = TextRenderer::GetInstance();
	const float radius = height * kDropletRadiusRatio;
	const float top = center.y - height * 0.5f;
	const float circleY = center.y + height * 0.5f - radius;
	const int rows = (std::max)(static_cast<int>(height), kMinDropletRows);
	const Vector4 color{ kEtherUiColor.x, kEtherUiColor.y, kEtherUiColor.z, alpha };
	for (int i = 0; i < rows; ++i) {
		const float y = top + static_cast<float>(i) + 0.5f;
		float half = 0.0f;
		const float dy = y - circleY;
		if (std::fabs(dy) < radius) half = std::sqrt(radius * radius - dy * dy);
		if (y < circleY) {
			const float cone = radius * kDropletConeRatio * std::pow(std::clamp((y - top) / (circleY - top), 0.0f, 1.0f), kDropletConeExponent);
			half = (std::max)(half, cone);
		}
		if (half <= kMinDropletHalfWidth) continue;
		tr->DrawRect({ center.x - half, y - 0.5f }, { half * 2.0f, 1.0f }, color);
	}
}

void HubEtherBet::DrawArrow(const Vector2& center, float size, bool right, float alpha, bool highlighted) const {
	// ◀ ▶（文字のフォントに無いかもしれないので四角を重ねて三角に）
	TextRenderer* tr = TextRenderer::GetInstance();
	const int columns = (std::max)(static_cast<int>(size), kMinArrowColumns);
	const float k = highlighted ? 1.0f : kArrowIdleBrightness;
	const Vector4 color{ outlineGold_[0] * k, outlineGold_[1] * k, outlineGold_[2] * k, alpha };
	for (int i = 0; i < columns; ++i) {
		// 先（尖った方）から根元へ
		const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(columns);
		const float half = size * kArrowHalfHeightRatio * u;
		const float x = right ? center.x + size * 0.5f - static_cast<float>(i) - 1.0f : center.x - size * 0.5f + static_cast<float>(i);
		tr->DrawRect({ x, center.y - half }, { 1.0f, half * 2.0f }, color);
	}
}

void HubEtherBet::DrawHandPanel(float alpha) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || alpha <= 0.0f) return;
	const float a = alpha;
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const Vector4 gold{ outlineGold_[0], outlineGold_[1], outlineGold_[2], a };
	const Vector4 white{ 1.0f, 1.0f, 1.0f, a };
	const EtherState* ether = gameSession_->GetEtherState();

	const float right = screenW - handPanelMargin_;
	const float top = handPanelMargin_;
	const float slant = handPanelHeight_ * kHandPanelSlantRatio;
	constexpr int kRows = 16;
	const float rowH = handPanelHeight_ / static_cast<float>(kRows);
	for (int r = 0; r < kRows; ++r) {
		const float inset = slant * (static_cast<float>(r) + 0.5f) / static_cast<float>(kRows);
		tr->DrawRect({ right - handPanelWidth_ + inset, top + rowH * static_cast<float>(r) }, { handPanelWidth_ - inset, rowH + kHandPanelRowOverlap },
			{ 0.0f, kBandGreen, kBandBlue, kHandPanelAlpha * a });
	}
	tr->DrawRect({ right - handPanelWidth_ + slant, top + handPanelHeight_ }, { handPanelWidth_ - slant, kHandPanelUnderlineThickness }, gold);
	const int hand = ether->IsInfiniteHand() ? kHandDisplayMax
		: (std::min)(static_cast<int>(std::lround(ether->GetHand())), kHandDisplayMax);
	const std::string handText = FormatThousands(hand);
	const float textH = kFontPixelHeight * handScale_;
	const float textW = tr->MeasureWidth(handText, handScale_);
	const float textX = right - kHandTextRightPadding - textW;
	const float textY = top + (handPanelHeight_ - textH) * 0.5f;
	DrawDroplet({ textX - kHandDropletGap, top + handPanelHeight_ * 0.5f }, textH * kHandDropletHeightRatio, a);
	tr->DrawText(handText, { textX, textY }, handScale_, white, kTextOutlineWidth, gold);
}

void HubEtherBet::DrawUi() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || uiAlpha_ <= 0.0f) return;
	const float a = SmoothStep01(uiAlpha_);
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const float screenH = static_cast<float>(dxCore_->GetUIHeight());
	const Vector4 gold{ outlineGold_[0], outlineGold_[1], outlineGold_[2], a };
	const Vector4 white{ 1.0f, 1.0f, 1.0f, a };
	EtherState* ether = gameSession_->GetEtherState();

	// ----- 右上：所持エーテル（斜めの暗い枠。今は無限＝99,999） -----
	{
		DrawHandPanel(a);
		const float right = screenW - handPanelMargin_;
		const float top = handPanelMargin_;

		// 決定したら所持の下に消費分が出る（所持は無限なので数字は減らない）
		if (step_ != Step::Adjust) {
			const float k = SmoothStep01(stepTime_ / kCostAppearSeconds);
			const std::string costText = "- " + FormatThousands(static_cast<int>(std::lround(confirmedCost_)));
			const float costScale = handScale_ * kCostScaleRatio;
			const float costW = tr->MeasureWidth(costText, costScale);
			tr->DrawText(costText, { right - kHandTextRightPadding - costW, top + handPanelHeight_ + kCostOffsetY + (1.0f - k) * kCostSlideY }, costScale,
				{ kEtherUiColor.x, kEtherUiColor.y, kEtherUiColor.z, a * k }, kTextOutlineWidth, { 0.0f, 0.0f, 0.0f, a * k * kOutlineAlpha });
		}
	}

	// ----- 針の先の札：純度（なめらかに追う針と一緒に文字盤の弧を動く。数字は決めた値） -----
	room_->DrawPurityTag(GetValue(), a, tagScale_, tagOffset_, outlineGold_);

	// ----- 下の黒い帯：使用するエーテル（◀ ▶ で調整） -----
	{
		const float cy = screenH * bandYRatio_;
		DrawBand(cy, bandHeight_, a);
		const float cost = ether->CostOf(GetValue());
		const std::string costText = FormatThousands(static_cast<int>(std::lround(cost)));
		const float textH = kFontPixelHeight * costScale_;
		const float textW = tr->MeasureWidth(costText, costScale_);
		const float dropH = textH * kBandDropletHeightRatio;
		const float gap = kBandDropletGap;
		const float totalW = dropH * kDropletWidthRatio + gap + textW;
		const float startX = screenW * 0.5f - totalW * 0.5f;
		DrawDroplet({ startX + dropH * kDropletRadiusRatio, cy }, dropH, a);
		tr->DrawText(costText, { startX + dropH * kDropletWidthRatio + gap, cy - textH * 0.5f }, costScale_, white, kTextOutlineWidth, gold);
		if (step_ == Step::Adjust) {
			DrawArrow({ screenW * 0.5f - arrowSpread_, cy }, arrowSize_, false, a, hoverArrow_ < 0 || heldDirection_ < 0);
			DrawArrow({ screenW * 0.5f + arrowSpread_, cy }, arrowSize_, true, a, hoverArrow_ > 0 || heldDirection_ > 0);
			// 操作の案内（小さく）
			constexpr const char* kHint = "左右・A/D：純度を選ぶ　　決定：賭ける　　キャンセル：戻る";
			const float hintW = tr->MeasureWidth(kHint, hintScale_);
			tr->DrawText(kHint, { screenW * 0.5f - hintW * 0.5f, cy + bandHeight_ * 0.5f + kHintOffsetY }, hintScale_,
				{ kHintColorRed, kHintColorGreen, kHintColorBlue, a }, 1.0f, { 0.0f, 0.0f, 0.0f, a * kOutlineAlpha });
		}
	}
	tr->Flush();
}

void HubEtherBet::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	const JsonValue& cam = node["camera"];
	cameraPos_ = ReadVector3(cam["pos"], cameraPos_);
	cameraRotDeg_ = ReadVector3(cam["rotDeg"], cameraRotDeg_);
	ReadFloat(node, "uiFadeSeconds", uiFadeSeconds_);
	ReadFloat(node, "followSpeed", followSpeed_);
	ReadFloat(node, "repeatDelay", repeatDelay_);
	ReadFloat(node, "repeatIntervalStart", repeatIntervalStart_);
	ReadFloat(node, "repeatIntervalEnd", repeatIntervalEnd_);
	ReadFloat(node, "repeatAccelSeconds", repeatAccelSeconds_);
	ReadFloat(node, "flowHoldSeconds", flowHoldSeconds_);
	ReadFloat(node, "confirmSeconds", confirmSeconds_);
	ReadFloat(node, "confirmGlowBoost", confirmGlowBoost_);
	const JsonValue& ui = node["ui"];
	ReadFloat(ui, "bandYRatio", bandYRatio_);
	ReadFloat(ui, "bandHeight", bandHeight_);
	ReadFloat(ui, "bandAlpha", bandAlpha_);
	ReadFloat(ui, "bandEdgeFade", bandEdgeFade_);
	ReadFloat(ui, "costScale", costScale_);
	ReadFloat(ui, "arrowSpread", arrowSpread_);
	ReadFloat(ui, "arrowSize", arrowSize_);
	ReadFloat(ui, "hintScale", hintScale_);
	ReadFloat(ui, "handPanelWidth", handPanelWidth_);
	ReadFloat(ui, "handPanelHeight", handPanelHeight_);
	ReadFloat(ui, "handPanelMargin", handPanelMargin_);
	ReadFloat(ui, "handScale", handScale_);
	ReadFloat(ui, "tagScale", tagScale_);
	ReadFloat(ui, "tagOffset", tagOffset_);
}

void HubEtherBet::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	JsonValue& cam = node["camera"];
	cam = JsonValue::MakeObject();
	cam["pos"] = MakeVector3(cameraPos_);
	cam["rotDeg"] = MakeVector3(cameraRotDeg_);
	WriteFloat(node, "uiFadeSeconds", uiFadeSeconds_);
	WriteFloat(node, "followSpeed", followSpeed_);
	WriteFloat(node, "repeatDelay", repeatDelay_);
	WriteFloat(node, "repeatIntervalStart", repeatIntervalStart_);
	WriteFloat(node, "repeatIntervalEnd", repeatIntervalEnd_);
	WriteFloat(node, "repeatAccelSeconds", repeatAccelSeconds_);
	WriteFloat(node, "flowHoldSeconds", flowHoldSeconds_);
	WriteFloat(node, "confirmSeconds", confirmSeconds_);
	WriteFloat(node, "confirmGlowBoost", confirmGlowBoost_);
	JsonValue& ui = node["ui"];
	ui = JsonValue::MakeObject();
	WriteFloat(ui, "bandYRatio", bandYRatio_);
	WriteFloat(ui, "bandHeight", bandHeight_);
	WriteFloat(ui, "bandAlpha", bandAlpha_);
	WriteFloat(ui, "bandEdgeFade", bandEdgeFade_);
	WriteFloat(ui, "costScale", costScale_);
	WriteFloat(ui, "arrowSpread", arrowSpread_);
	WriteFloat(ui, "arrowSize", arrowSize_);
	WriteFloat(ui, "hintScale", hintScale_);
	WriteFloat(ui, "handPanelWidth", handPanelWidth_);
	WriteFloat(ui, "handPanelHeight", handPanelHeight_);
	WriteFloat(ui, "handPanelMargin", handPanelMargin_);
	WriteFloat(ui, "handScale", handScale_);
	WriteFloat(ui, "tagScale", tagScale_);
	WriteFloat(ui, "tagOffset", tagOffset_);
}

void HubEtherBet::OnImGui() {
#ifdef _DEBUG
	static const char* kStepNames[] = { "Adjust", "Confirming", "Finished" };
	EtherState* ether = gameSession_->GetEtherState();
	ImGui::Text("Step %s  Purity %.1f (shown %.2f)  Cost %.0f", kStepNames[static_cast<int>(step_)], GetValue(), shownValue_,
		ether->CostOf(GetValue()));
	ImGui::DragFloat3("Camera Pos", &cameraPos_.x, 0.01f);
	ImGui::DragFloat3("Camera Rot [deg]", &cameraRotDeg_.x, 0.1f);
	ImGui::DragFloat("UI Fade [s]", &uiFadeSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Follow Speed [1/s]", &followSpeed_, 0.1f, 0.1f, 60.0f);
	ImGui::DragFloat("Repeat Delay [s]", &repeatDelay_, 0.01f, 0.05f, 2.0f);
	ImGui::DragFloat("Repeat Interval Start [s]", &repeatIntervalStart_, 0.005f, 0.005f, 1.0f);
	ImGui::DragFloat("Repeat Interval End [s]", &repeatIntervalEnd_, 0.005f, 0.005f, 1.0f);
	ImGui::DragFloat("Repeat Accel [s]", &repeatAccelSeconds_, 0.05f, 0.0f, 5.0f);
	ImGui::DragFloat("Flow Hold [s]", &flowHoldSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Confirm Seconds", &confirmSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Confirm Glow Boost", &confirmGlowBoost_, 0.05f, 1.0f, 10.0f);
	if (ImGui::TreeNode("UI##EtherBet")) {
		ImGui::SliderFloat("Band Y (ratio)", &bandYRatio_, 0.0f, 1.0f);
		ImGui::DragFloat("Band Height [px]", &bandHeight_, 1.0f, 0.0f, 400.0f);
		ImGui::SliderFloat("Band Alpha", &bandAlpha_, 0.0f, 1.0f);
		ImGui::SliderFloat("Band Edge Fade", &bandEdgeFade_, 0.01f, 0.5f);
		ImGui::DragFloat("Cost Scale", &costScale_, 0.01f, 0.2f, 5.0f);
		ImGui::DragFloat("Arrow Spread [px]", &arrowSpread_, 1.0f, 0.0f, 800.0f);
		ImGui::DragFloat("Arrow Size [px]", &arrowSize_, 0.5f, 4.0f, 120.0f);
		ImGui::DragFloat("Hint Scale", &hintScale_, 0.01f, 0.2f, 3.0f);
		ImGui::DragFloat("Hand Panel Width", &handPanelWidth_, 1.0f, 50.0f, 800.0f);
		ImGui::DragFloat("Hand Panel Height", &handPanelHeight_, 1.0f, 10.0f, 300.0f);
		ImGui::DragFloat("Hand Panel Margin", &handPanelMargin_, 1.0f, 0.0f, 300.0f);
		ImGui::DragFloat("Hand Scale", &handScale_, 0.01f, 0.2f, 5.0f);
		ImGui::DragFloat("Tag Scale", &tagScale_, 0.01f, 0.2f, 5.0f);
		ImGui::DragFloat("Tag Offset [px]", &tagOffset_, 0.5f, -200.0f, 300.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Ether State##EtherBet")) {
		ether->OnImGui();
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Room Water##EtherBet")) {
		room_->OnImGuiWater();
		ImGui::TreePop();
	}
#endif
}
