#include "InkTransition.h"
#include "TransitionTuning.h"
#include "TransitionManager.h"
#include "SceneManager.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include "PostEffect.h"
#include "Game.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const wchar_t* kPixelShaderPath = L"Resources/Shaders/Transition/InkTransition.PS.hlsl";
	// 雲のノイズを流用（R = 形の fBm / G = 低周波の fBm。上下左右がつながる）
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";
	// ScreenWaterSurface の t0 / t1 の後ろに足すテクスチャ：t2 = ノイズ
	constexpr UINT kRootNoise = ScreenWaterSurface::kRootExtraTextures;
}

void InkTransition::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	water_.Initialize(dxCore_, SceneManager::GetInstance()->GetSRVManager(), screenWidth_, screenHeight_);
	rootSignature_ = water_.CreateRootSignature(1);
	pipelineState_ = water_.CreatePipelineState(rootSignature_.Get(), kPixelShaderPath, false);
	TextureManager::GetInstance()->LoadTexture(kNoisePath);

	paramsResource_ = dxCore_->CreateBufferResource((sizeof(ParamsForGPU) + 255) & ~255);
	paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
	*paramsData_ = ParamsForGPU{};
}

void InkTransition::Finalize() {
	water_.Finalize();
	paramsResource_.Reset();
	paramsData_ = nullptr;
}

void InkTransition::OnStartCover() {
	coverage_ = 0.0f;
	fade_ = 1.0f;
	impulseFired_ = false;
	water_.Reset();
}

void InkTransition::UpdateVisual() {
	if (!isTransitioning_) return;

	// 着水の瞬間に一度だけへこませる
	if (state_ == TransitionState::FadeIn && !impulseFired_ && currentTime_ >= settings_.dropTime) {
		water_.AddImpulse(settings_.dropCenter, settings_.impulseRadius, settings_.impulseStrength);
		impulseFired_ = true;
	}

	// 全面黒の間は見えないので止めておく
	if (state_ != TransitionState::Hold) {
		water_.Update(GetStepDeltaTime());
	}
}

bool InkTransition::UpdateCover(float /*dt*/) {
	// currentTime_ は基底が進める
	const float spreadSec = currentTime_ - settings_.dropTime;
	coverage_ = std::clamp(spreadSec / (std::max)(settings_.spreadTime, 0.01f), 0.0f, 1.0f);
	return coverage_ >= 1.0f;
}

bool InkTransition::UpdateReveal(float /*dt*/) {
	fade_ = std::clamp(1.0f - currentTime_ / (std::max)(settings_.revealTime, 0.01f), 0.0f, 1.0f);
	return fade_ <= 0.0f;
}

void InkTransition::SetFullyCovered() {
	coverage_ = 1.0f;
	fade_ = 1.0f;
}

float InkTransition::CalcInkRadius() const {
	if (state_ == TransitionState::FadeIn && currentTime_ < settings_.dropTime) {
		return -1.0f;  // 着水前
	}
	// 雫の点から一番遠い角まで（画面の幅を 1 とした長さ）。縁のぼかしの分だけ先まで広げて端を残さない
	const float aspect = water_.GetAspect();
	const Vector2& c = settings_.dropCenter;
	const float dx = (std::max)(c.x, 1.0f - c.x);
	const float dy = (std::max)(c.y, 1.0f - c.y) / aspect;
	// 筋で縮む所・薄い縁の分も見込む（足りない分は最後にシェーダが全面黒へ寄せる）
	const float shapeMargin = 1.0f + 0.5f * settings_.tendrilAmount + settings_.edgeWidth;
	const float fullRadius = std::sqrt(dx * dx + dy * dy) * shapeMargin + settings_.edgeSoftness;

	// 最初に速く広がって後で粘る（墨が水に落ちて勢いが落ちていく感じ）
	const float eased = 1.0f - std::pow(1.0f - coverage_, (std::max)(settings_.spreadEase, 1.0f));
	return eased * fullRadius;
}

InkTransition::ParamsForGPU InkTransition::WriteParams() {
	static_assert(sizeof(ParamsForGPU) == 144, "InkTransition.PS.hlsl の InkParams と大きさを合わせる");
	const Settings& s = settings_;
	ParamsForGPU p{};
	p.water = water_.GetConstants();
	p.dropCenter = s.dropCenter;
	p.inkRadius = CalcInkRadius();
	p.edgeSoftness = (std::max)(s.edgeSoftness, 0.0001f);
	// Hold は全面黒（取り込んだ画面を読まない）。開く側は全面の墨を fade で薄める
	if (state_ == TransitionState::Hold) {
		p.coverage = 1.0f;
	} else if (state_ == TransitionState::FadeOut) {
		p.coverage = 0.0f;
		p.inkRadius = 100.0f;  // 全面が墨の芯（筋の隙間を出さない）。濃さは fade で薄める
	} else {
		p.coverage = coverage_;
	}
	p.fade = fade_;
	p.time = (state_ == TransitionState::FadeIn) ? currentTime_ : s.dropTime + s.spreadTime;
	p.dropPhase = (state_ == TransitionState::FadeIn && currentTime_ < s.dropTime)
		? currentTime_ / (std::max)(s.dropTime, 0.001f) : -1.0f;
	p.dropStartRadius = s.dropStartRadius;
	p.dropEndRadius = s.dropEndRadius;
	p.warpAmount = s.warpAmount;
	p.warpScale = s.warpScale;
	p.tendrilAmount = s.tendrilAmount;
	p.tendrilFreq = s.tendrilFreq;
	p.detailScale = s.detailScale;
	p.wispAmount = s.wispAmount;
	p.edgeWidth = (std::max)(s.edgeWidth, 0.001f);
	p.flowSpeed = s.flowSpeed;
	p.shadowStrength = s.shadowStrength;
	p.thinColor = s.thinColor;
	p.toneScale = s.toneScale;
	p.toneMin = std::clamp(s.toneMin, 0.0f, 1.0f);
	p.toneDepth = s.toneDepth;
	p.toneDarkStart = std::clamp(s.toneDarkStart, 0.0f, 0.89f);
	*paramsData_ = p;
	return p;
}

void InkTransition::Draw() {
	if (!isTransitioning_) return;

	RenderTexture* sceneTarget = Game::GetPostEffect()->GetSceneRenderTarget();
	if (!sceneTarget) return;

	const ParamsForGPU params = WriteParams();
	auto* cmd = dxCore_->GetCommandList();

	// このフレームのシーンをコピーして読む。全面黒の間はコピーしない
	water_.BeginDraw(cmd, sceneTarget, params.coverage < 1.0f);

	cmd->SetGraphicsRootSignature(rootSignature_.Get());
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->SetGraphicsRootConstantBufferView(ScreenWaterSurface::kRootParams, paramsResource_->GetGPUVirtualAddress());
	water_.BindTextures(cmd);
	cmd->SetGraphicsRootDescriptorTable(kRootNoise, TextureManager::GetInstance()->GetSrvHandleGPU(kNoisePath));
	cmd->DrawInstanced(3, 1, 0, 0);

	water_.EndDraw(cmd, sceneTarget);
}

void InkTransition::OnImGui() {
#ifdef _DEBUG
	Settings& s = settings_;
	ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)  coverage %.2f", static_cast<int>(state_), coverage_);
	ImGui::SliderFloat2("Drop Center (UV)", &s.dropCenter.x, 0.0f, 1.0f);
	ImGui::DragFloat("Drop Time [s]", &s.dropTime, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Spread Time [s]", &s.spreadTime, 0.01f, 0.05f, 5.0f);
	ImGui::DragFloat("Spread Ease", &s.spreadEase, 0.05f, 1.0f, 8.0f);
	ImGui::DragFloat("Edge Softness", &s.edgeSoftness, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Reveal Time [s]", &s.revealTime, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Hold Duration [s]", &holdDuration_, 0.01f, 0.0f, 3.0f);
	ImGui::SeparatorText("Ripple");
	water_.OnImGui();
	ImGui::DragFloat("Impulse Radius", &s.impulseRadius, 0.001f, 0.001f, 0.3f);
	ImGui::DragFloat("Impulse Strength", &s.impulseStrength, 0.001f, 0.0f, 1.0f);
	ImGui::SeparatorText("Ink Shape");
	ImGui::DragFloat("Warp Amount", &s.warpAmount, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Warp Scale", &s.warpScale, 0.05f, 0.1f, 20.0f);
	ImGui::DragFloat("Tendril Amount", &s.tendrilAmount, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Tendril Freq", &s.tendrilFreq, 0.05f, 0.1f, 10.0f);
	ImGui::DragFloat("Detail Scale", &s.detailScale, 0.1f, 0.5f, 40.0f);
	ImGui::DragFloat("Wisp Amount", &s.wispAmount, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Edge Width", &s.edgeWidth, 0.005f, 0.001f, 1.0f);
	ImGui::DragFloat("Flow Speed", &s.flowSpeed, 0.005f, 0.0f, 1.0f);
	ImGui::ColorEdit3("Thin Color", &s.thinColor.x);
	ImGui::SeparatorText("Ink Tone");
	ImGui::DragFloat("Tone Scale", &s.toneScale, 0.05f, 0.1f, 20.0f);
	ImGui::SliderFloat("Tone Min", &s.toneMin, 0.0f, 1.0f);
	ImGui::DragFloat("Tone Depth", &s.toneDepth, 0.01f, 0.0f, 3.0f);
	ImGui::SliderFloat("Tone Dark Start", &s.toneDarkStart, 0.0f, 0.89f);
	ImGui::SeparatorText("Drop");
	ImGui::DragFloat("Drop Start Radius", &s.dropStartRadius, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Drop End Radius", &s.dropEndRadius, 0.001f, 0.0f, 0.2f);
	ImGui::DragFloat("Shadow Strength", &s.shadowStrength, 0.01f, 0.0f, 1.0f);
	// シーンを切り替えずに覆う→隠したまま→開く（黒が薄れる簡易版）を流す
	if (ImGui::Button("Preview (no scene change)")) {
		TransitionManager::GetInstance()->StartTransition(TransitionType::Ink, "", "", nullptr);
	}
#endif
}

void InkTransition::LoadTuning(const JsonValue& json) {
	TransitionTuning::Load(json, settings_);
	TransitionTuning::Load(json["water"], water_.GetSettings());
	holdDuration_ = static_cast<float>(json["holdDuration"].AsDouble(holdDuration_));
}

void InkTransition::SaveTuning(JsonValue& json) const {
	json = TransitionTuning::Save(settings_);
	json["water"] = TransitionTuning::Save(water_.GetSettings());
	json["holdDuration"] = JsonValue(static_cast<double>(holdDuration_));
}
