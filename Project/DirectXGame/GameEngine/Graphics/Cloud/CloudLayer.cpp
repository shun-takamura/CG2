#include "CloudLayer.h"

#include "DirectXCore.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr float kTwoPi = 6.28318531f;

	float Wrap01(float v) {
		return v - std::floor(v);
	}
}

CloudLayer::CloudLayer() = default;
CloudLayer::~CloudLayer() = default;

void CloudLayer::Initialize(DirectXCore* dxCore, const std::string& noiseTexturePath)
{
	dxCore_ = dxCore;
	noiseTexturePath_ = noiseTexturePath;
	TextureManager::GetInstance()->LoadTexture(noiseTexturePath_);

	constantsResource_ = dxCore_->CreateBufferResource((sizeof(ConstantsForGPU) + 255) & ~255);
	constantsResource_->Map(0, nullptr, reinterpret_cast<void**>(&constantsData_));
	Update(0.0f, { 0.0f, 0.0f, 0.0f });
}

void CloudLayer::SetSunDirection(const Vector3& towardSun)
{
	sunDirection_ = Normalize(towardSun);
}

void CloudLayer::Update(float deltaTime, const Vector3& eyePosition)
{
	time_ += deltaTime;

	// 風 [m/s] を UV の速度に直して積む。UV は 1 で1周なので折り返しても見た目は変わらない
	const auto scroll = [deltaTime](Vector2& offset, const Vector2& wind, float scale) {
		offset.x = Wrap01(offset.x - wind.x / scale * deltaTime);
		offset.y = Wrap01(offset.y - wind.y / scale * deltaTime);
	};
	const float shapeScale = (std::max)(params_.shapeScale, 1.0f);
	const float detailScale = (std::max)(params_.detailScale, 1.0f);
	const float maskScale = (std::max)(params_.maskScale, 1.0f);
	const float evolveScale = (std::max)(params_.evolveScale, 1.0f);
	scroll(shapeOffset_, params_.shapeWind, shapeScale);
	scroll(detailOffset_, params_.detailWind, detailScale);
	scroll(maskOffset_, params_.maskWind, maskScale);
	scroll(evolveOffset_, params_.evolveWind, evolveScale);

	const float swingA = std::sin(kTwoPi * time_ / (std::max)(params_.swingPeriodA, 0.1f));
	const float swingB = std::sin(kTwoPi * time_ / (std::max)(params_.swingPeriodB, 0.1f));
	const float threshold = params_.coverage + params_.swingAmplitude * (0.6f * swingA + 0.4f * swingB);

	// 一様分布2枚を w で混ぜると標準偏差が sqrt((1-w)^2 + w^2) 倍に縮むので、その分広げ直す
	const float w = std::clamp(params_.detailWeight, 0.0f, 1.0f);
	const float detailContrast = 1.0f / std::sqrt((1.0f - w) * (1.0f - w) + w * w);

	// 太陽側へ lightStep [m] ずらす量を、大きい形の UV に直す。太陽が真上なら水平成分が無いのでずらさない
	Vector2 lightOffset{ 0.0f, 0.0f };
	const float sunXZ = std::sqrt(sunDirection_.x * sunDirection_.x + sunDirection_.z * sunDirection_.z);
	if (sunXZ > 1e-4f) {
		const float k = params_.lightStep / shapeScale / sunXZ;
		lightOffset = { sunDirection_.x * k, sunDirection_.z * k };
	}

	ConstantsForGPU c{};
	c.eyePosition = eyePosition;
	c.enabled = params_.enabled ? 1 : 0;
	c.sunDirection = sunDirection_;
	c.height = params_.height;
	c.shapeOffset = shapeOffset_;
	c.detailOffset = detailOffset_;
	c.maskOffset = maskOffset_;
	c.evolveOffset = evolveOffset_;
	c.shapeTiling = 1.0f / shapeScale;
	c.detailTiling = 1.0f / detailScale;
	c.maskTiling = 1.0f / maskScale;
	c.evolveTiling = 1.0f / evolveScale;
	c.detailWeight = w;
	c.detailContrast = detailContrast;
	c.maskStrength = params_.maskStrength;
	c.evolveAmount = params_.evolveAmount;
	c.threshold = threshold;
	c.sharpness = params_.sharpness;
	c.opacity = params_.opacity;
	c.fadeStart = params_.fadeStart;
	c.fadeEnd = (std::max)(params_.fadeEnd, params_.fadeStart + 1.0f);
	c.hazeStart = params_.hazeStart;
	c.hazeEnd = (std::max)(params_.hazeEnd, params_.hazeStart + 1.0f);
	c.hazeMax = params_.hazeMax;
	c.lightOffset = lightOffset;
	c.lightContrast = params_.lightContrast;
	c.sunPower = params_.sunPower;
	c.litColor = params_.litColor;
	c.shadowDepth = params_.shadowDepth;
	c.shadowColor = params_.shadowColor;
	c.sunGlow = params_.sunGlow;
	c.edgeColor = params_.edgeColor;
	*constantsData_ = c;
}

D3D12_GPU_VIRTUAL_ADDRESS CloudLayer::GetConstantBufferAddress() const
{
	return constantsResource_->GetGPUVirtualAddress();
}

D3D12_GPU_DESCRIPTOR_HANDLE CloudLayer::GetNoiseSrvHandle() const
{
	return TextureManager::GetInstance()->GetSrvHandleGPU(noiseTexturePath_);
}

Microsoft::WRL::ComPtr<ID3D12Resource> CloudLayer::CreateDisabledConstantBuffer(DirectXCore* dxCore)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCore->CreateBufferResource((sizeof(ConstantsForGPU) + 255) & ~255);
	ConstantsForGPU* data = nullptr;
	resource->Map(0, nullptr, reinterpret_cast<void**>(&data));
	*data = ConstantsForGPU{};
	resource->Unmap(0, nullptr);
	return resource;
}

const char* CloudLayer::GetFallbackTexturePath()
{
	return "Resources/Textures/white1x1.dds";
}

void CloudLayer::OnImGui()
{
#ifdef _DEBUG
	Params& p = params_;
	ImGui::Checkbox("Cloud Enabled", &p.enabled);
	ImGui::DragFloat("Cloud Height", &p.height, 5.0f, 10.0f, 5000.0f);
	ImGui::SeparatorText("Shape (large + detail)");
	ImGui::DragFloat("Shape Scale [m]", &p.shapeScale, 10.0f, 10.0f, 50000.0f);
	ImGui::DragFloat2("Shape Wind [m/s]", &p.shapeWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::DragFloat("Detail Scale [m]", &p.detailScale, 5.0f, 10.0f, 50000.0f);
	ImGui::DragFloat2("Detail Wind [m/s]", &p.detailWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::SliderFloat("Detail Weight", &p.detailWeight, 0.0f, 1.0f);
	ImGui::SeparatorText("Size variation (mask)");
	ImGui::DragFloat("Mask Scale [m]", &p.maskScale, 10.0f, 10.0f, 100000.0f);
	ImGui::DragFloat2("Mask Wind [m/s]", &p.maskWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::SliderFloat("Mask Strength", &p.maskStrength, 0.0f, 1.0f);
	ImGui::SeparatorText("Dissolve (evolve)");
	ImGui::DragFloat("Evolve Scale [m]", &p.evolveScale, 10.0f, 10.0f, 100000.0f);
	ImGui::DragFloat2("Evolve Wind [m/s]", &p.evolveWind.x, 0.5f, -500.0f, 500.0f);
	ImGui::SliderFloat("Evolve Amount", &p.evolveAmount, 0.0f, 1.0f);
	ImGui::SeparatorText("Coverage");
	ImGui::SliderFloat("Coverage (threshold)", &p.coverage, 0.0f, 1.0f);
	ImGui::DragFloat("Sharpness", &p.sharpness, 0.05f, 0.1f, 50.0f);
	ImGui::DragFloat("Swing Amplitude", &p.swingAmplitude, 0.005f, 0.0f, 0.5f);
	ImGui::DragFloat("Swing Period A [s]", &p.swingPeriodA, 0.5f, 0.1f, 600.0f);
	ImGui::DragFloat("Swing Period B [s]", &p.swingPeriodB, 0.5f, 0.1f, 600.0f);
	ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f);
	ImGui::SeparatorText("Distance");
	ImGui::DragFloat("Haze Start [m]", &p.hazeStart, 50.0f, 0.0f, 100000.0f);
	ImGui::DragFloat("Haze End [m]", &p.hazeEnd, 50.0f, 0.0f, 100000.0f);
	ImGui::SliderFloat("Haze Max", &p.hazeMax, 0.0f, 1.0f);
	ImGui::DragFloat("Fade Start [m]", &p.fadeStart, 50.0f, 0.0f, 200000.0f);
	ImGui::DragFloat("Fade End [m]", &p.fadeEnd, 50.0f, 0.0f, 200000.0f);
	ImGui::SeparatorText("Color / Light");
	ImGui::DragFloat("Light Step [m]", &p.lightStep, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Light Contrast", &p.lightContrast, 0.05f, 0.0f, 50.0f);
	ImGui::ColorEdit3("Lit Color", &p.litColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::ColorEdit3("Shadow Color", &p.shadowColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::DragFloat("Shadow Depth", &p.shadowDepth, 0.01f, 0.0f, 4.0f);
	ImGui::ColorEdit3("Edge Color", &p.edgeColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::DragFloat("Sun Power", &p.sunPower, 0.1f, 1.0f, 128.0f);
	ImGui::DragFloat("Sun Glow", &p.sunGlow, 0.01f, 0.0f, 4.0f);
#endif
}
