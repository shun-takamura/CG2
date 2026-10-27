#include "HubEtherBet.h"

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
	// 器の間の見た目（背景・水面・器）とカメラの既定はゲームオーバー画面と同じ値を読む
	constexpr const char* kGameOverTuningPath = "Resources/Json/Tuning/GameOver.json";
	// TextRenderer のフォントの高さ（Framework::Initialize の MPLUS1p 32px）
	constexpr float kFontPixelHeight = 32.0f;
	// 所持エーテルの表示の上限（今は無限の扱い。2026-10-27 ユーザーのスケッチ）
	constexpr int kHandDisplayMax = 99999;
	const Vector4 kEtherUiColor{ 0.55f, 0.85f, 1.0f, 1.0f };

	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
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
	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}

	// 3 桁ごとにカンマ（例：11,000）
	std::string FormatThousands(int value) {
		std::string digits = std::to_string(value < 0 ? -value : value);
		std::string out;
		for (size_t i = 0; i < digits.size(); ++i) {
			if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
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
			ReadVector3(result.value["camera"]["farPos"], cameraPos_);
			ReadVector3(result.value["camera"]["farRotDeg"], cameraRotDeg_);
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

	EtherState* ether = EtherState::GetInstance();
	valueTenths_ = static_cast<int>(std::lround(ether->GetDifficulty() * 10.0f));
	const int maxTenths = static_cast<int>(std::floor(ether->GetMaxAffordableDifficulty() * 10.0f + 1e-3f));
	const int minTenths = static_cast<int>(std::lround(ether->GetDifficultyMin() * 10.0f));
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
	EtherState* ether = EtherState::GetInstance();
	const int minTenths = static_cast<int>(std::lround(ether->GetDifficultyMin() * 10.0f));
	const int maxTenths = (std::max)(static_cast<int>(std::floor(ether->GetMaxAffordableDifficulty() * 10.0f + 1e-3f)), minTenths);
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
	EtherState* ether = EtherState::GetInstance();
	ether->PlaceBet(GetValue());
	confirmedCost_ = ether->GetBet();
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
	room_->GetVessel()->Splash(0.006f);
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
		uiAlpha_ = uiFadeSeconds_ > 1e-3f ? (std::min)(uiAlpha_ + deltaTime / uiFadeSeconds_, 1.0f) : 1.0f;
	}

	switch (step_) {
	case Step::Adjust: {
		if (!inputEnabled || !actions) {
			heldDirection_ = 0;
			break;
		}
		// デモモード：少し見せてから、人が長押ししたときのように純度を 9.0 まで上げ（液面が上がり、泉から光が昇る）、着いたら少し待って決める
		if (attract) {
			const AttractMode* mode = AttractMode::GetInstance();
			EtherState* ether = EtherState::GetInstance();
			const float targetPurity = (std::min)(mode->betTargetPurity, ether->GetMaxAffordableDifficulty());
			const int targetTenths = static_cast<int>(std::lround(targetPurity * 10.0f));
			attractTimer_ += deltaTime;
			if (valueTenths_ != targetTenths) {
				if (attractTimer_ >= mode->betRaiseDelay) {
					const int direction = targetTenths > valueTenths_ ? 1 : -1;
					heldDirection_ = direction;   // ◀ ▶ を押しているように光らせる
					attractStepTimer_ -= deltaTime;
					while (attractStepTimer_ <= 0.0f && valueTenths_ != targetTenths) {
						attractStepTimer_ += (std::max)(mode->betStepInterval, 0.005f);
						if (!Step10(direction)) break;
					}
				}
				if (valueTenths_ == targetTenths) {
					heldDirection_ = 0;
					attractTimer_ = mode->betRaiseDelay;   // ここから betConfirmWait を数える
				}
			} else {
				heldDirection_ = 0;
				if (attractTimer_ >= mode->betRaiseDelay + mode->betConfirmWait) Confirm();
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
				const float u = repeatAccelSeconds_ > 1e-3f
					? std::clamp((holdTime_ - repeatDelay_) / repeatAccelSeconds_, 0.0f, 1.0f) : 1.0f;
				repeatTimer_ += (std::max)(repeatIntervalStart_ + (repeatIntervalEnd_ - repeatIntervalStart_) * u, 0.005f);
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
		const float t = confirmSeconds_ > 1e-3f ? std::clamp(stepTime_ / confirmSeconds_, 0.0f, 1.0f) : 1.0f;
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
	EtherState* ether = EtherState::GetInstance();
	const float target = GetValue();
	shownValue_ += (target - shownValue_) * (1.0f - std::exp(-followSpeed_ * deltaTime));
	if (std::fabs(target - shownValue_) < 1e-3f) shownValue_ = target;
	EtherVessel* vessel = room_->GetVessel();
	vessel->SetDifficulty01(ether->DifficultyTo01(shownValue_), true);
	vessel->SetFill(ether->BetToFill(ether->CostOf(target)), false);
}

void HubEtherBet::LayoutBand(Rect& left, Rect& center, Rect& right) const {
	const float w = static_cast<float>(dxCore_->GetUIWidth());
	const float h = static_cast<float>(dxCore_->GetUIHeight());
	const float cy = h * bandYRatio_;
	const float hit = (std::max)(arrowSize_ * 2.2f, bandHeight_ * 0.8f);
	left = { w * 0.5f - arrowSpread_ - hit * 0.5f, cy - hit * 0.5f, hit, hit };
	right = { w * 0.5f + arrowSpread_ - hit * 0.5f, cy - hit * 0.5f, hit, hit };
	const float centerWidth = arrowSpread_ * 2.0f - hit * 1.2f;
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
		const float edge = SmoothStep01((std::min)(u, 1.0f - u) / (std::max)(bandEdgeFade_, 1e-3f));
		for (int r = 0; r < kRows; ++r) {
			const float bh = height * (1.0f + static_cast<float>(r) * 0.25f);
			const float k = r == 0 ? 1.0f : 0.35f;
			tr->DrawRect({ columnW * static_cast<float>(c), centerY - bh * 0.5f }, { columnW + 1.0f, bh },
				{ 0.0f, 0.01f, 0.03f, bandAlpha_ * edge * k * alpha });
		}
	}
}

void HubEtherBet::DrawDroplet(const Vector2& center, float height, float alpha) const {
	// しずくの印（絵が無いので 1px の横線を重ねる）：下が円、上へすぼまって尖る
	TextRenderer* tr = TextRenderer::GetInstance();
	const float radius = height * 0.36f;
	const float top = center.y - height * 0.5f;
	const float circleY = center.y + height * 0.5f - radius;
	const int rows = (std::max)(static_cast<int>(height), 4);
	const Vector4 color{ kEtherUiColor.x, kEtherUiColor.y, kEtherUiColor.z, alpha };
	for (int i = 0; i < rows; ++i) {
		const float y = top + static_cast<float>(i) + 0.5f;
		float half = 0.0f;
		const float dy = y - circleY;
		if (std::fabs(dy) < radius) half = std::sqrt(radius * radius - dy * dy);
		if (y < circleY) {
			const float cone = radius * 0.92f * std::pow(std::clamp((y - top) / (circleY - top), 0.0f, 1.0f), 1.3f);
			half = (std::max)(half, cone);
		}
		if (half <= 0.05f) continue;
		tr->DrawRect({ center.x - half, y - 0.5f }, { half * 2.0f, 1.0f }, color);
	}
}

void HubEtherBet::DrawArrow(const Vector2& center, float size, bool right, float alpha, bool highlighted) const {
	// ◀ ▶（文字のフォントに無いかもしれないので四角を重ねて三角に）
	TextRenderer* tr = TextRenderer::GetInstance();
	const int columns = (std::max)(static_cast<int>(size), 4);
	const float k = highlighted ? 1.0f : 0.7f;
	const Vector4 color{ outlineGold_[0] * k, outlineGold_[1] * k, outlineGold_[2] * k, alpha };
	for (int i = 0; i < columns; ++i) {
		// 先（尖った方）から根元へ
		const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(columns);
		const float half = size * 0.6f * u;
		const float x = right ? center.x + size * 0.5f - static_cast<float>(i) - 1.0f : center.x - size * 0.5f + static_cast<float>(i);
		tr->DrawRect({ x, center.y - half }, { 1.0f, half * 2.0f }, color);
	}
}

void HubEtherBet::DrawUi() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized() || uiAlpha_ <= 0.0f) return;
	const float a = SmoothStep01(uiAlpha_);
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const float screenH = static_cast<float>(dxCore_->GetUIHeight());
	const Vector4 gold{ outlineGold_[0], outlineGold_[1], outlineGold_[2], a };
	const Vector4 white{ 1.0f, 1.0f, 1.0f, a };
	EtherState* ether = EtherState::GetInstance();

	// ----- 右上：所持エーテル（斜めの暗い枠。今は無限＝99,999） -----
	{
		const float right = screenW - handPanelMargin_;
		const float top = handPanelMargin_;
		const float slant = handPanelHeight_ * 0.45f;
		constexpr int kRows = 16;
		const float rowH = handPanelHeight_ / static_cast<float>(kRows);
		for (int r = 0; r < kRows; ++r) {
			const float inset = slant * (static_cast<float>(r) + 0.5f) / static_cast<float>(kRows);
			tr->DrawRect({ right - handPanelWidth_ + inset, top + rowH * static_cast<float>(r) }, { handPanelWidth_ - inset, rowH + 0.5f },
				{ 0.0f, 0.01f, 0.03f, 0.7f * a });
		}
		tr->DrawRect({ right - handPanelWidth_ + slant, top + handPanelHeight_ }, { handPanelWidth_ - slant, 2.0f }, gold);
		const int hand = ether->IsInfiniteHand() ? kHandDisplayMax
			: (std::min)(static_cast<int>(std::lround(ether->GetHand())), kHandDisplayMax);
		const std::string handText = FormatThousands(hand);
		const float textH = kFontPixelHeight * handScale_;
		const float textW = tr->MeasureWidth(handText, handScale_);
		const float textX = right - 24.0f - textW;
		const float textY = top + (handPanelHeight_ - textH) * 0.5f;
		DrawDroplet({ textX - 26.0f, top + handPanelHeight_ * 0.5f }, textH * 0.7f, a);
		tr->DrawText(handText, { textX, textY }, handScale_, white, 2.0f, gold);

		// 決定したら所持の下に消費分が出る（所持は無限なので数字は減らない）
		if (step_ != Step::Adjust) {
			const float k = SmoothStep01(stepTime_ / 0.4f);
			const std::string costText = "- " + FormatThousands(static_cast<int>(std::lround(confirmedCost_)));
			const float costScale = handScale_ * 0.8f;
			const float costW = tr->MeasureWidth(costText, costScale);
			tr->DrawText(costText, { right - 24.0f - costW, top + handPanelHeight_ + 12.0f + (1.0f - k) * 10.0f }, costScale,
				{ kEtherUiColor.x, kEtherUiColor.y, kEtherUiColor.z, a * k }, 2.0f, { 0.0f, 0.0f, 0.0f, a * k * 0.8f });
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
		const float dropH = textH * 0.65f;
		const float gap = 14.0f;
		const float totalW = dropH * 0.75f + gap + textW;
		const float startX = screenW * 0.5f - totalW * 0.5f;
		DrawDroplet({ startX + dropH * 0.36f, cy }, dropH, a);
		tr->DrawText(costText, { startX + dropH * 0.75f + gap, cy - textH * 0.5f }, costScale_, white, 2.0f, gold);
		if (step_ == Step::Adjust) {
			DrawArrow({ screenW * 0.5f - arrowSpread_, cy }, arrowSize_, false, a, hoverArrow_ < 0 || heldDirection_ < 0);
			DrawArrow({ screenW * 0.5f + arrowSpread_, cy }, arrowSize_, true, a, hoverArrow_ > 0 || heldDirection_ > 0);
			// 操作の案内（小さく）
			constexpr const char* kHint = "左右・A/D：純度を選ぶ　　決定：賭ける　　キャンセル：戻る";
			const float hintW = tr->MeasureWidth(kHint, hintScale_);
			tr->DrawText(kHint, { screenW * 0.5f - hintW * 0.5f, cy + bandHeight_ * 0.5f + 18.0f }, hintScale_,
				{ 0.62f, 0.64f, 0.68f, a }, 1.0f, { 0.0f, 0.0f, 0.0f, a * 0.8f });
		}
	}
	tr->Flush();
}

void HubEtherBet::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	const JsonValue& cam = node["camera"];
	ReadVector3(cam["pos"], cameraPos_);
	ReadVector3(cam["rotDeg"], cameraRotDeg_);
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
	EtherState* ether = EtherState::GetInstance();
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
