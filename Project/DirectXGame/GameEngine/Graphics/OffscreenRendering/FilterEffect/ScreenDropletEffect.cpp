#include "ScreenDropletEffect.h"
#include "DirectXCore.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
    // これより薄くなったら水滴が見えないので、0 に落としてパスを飛ばす
    constexpr float kVisibleWetness = 1e-3f;

    // JSON のキー（モードごとの節の名前と、Settings の各値）
    constexpr const char* kModeKeys[] = { "gravity", "radial" };

    struct FieldEntry {
        const char* key;
        float ScreenDropletEffect::Settings::* member;
    };
    constexpr FieldEntry kFields[] = {
        { "density", &ScreenDropletEffect::Settings::density },
        { "sizeMin", &ScreenDropletEffect::Settings::sizeMin },
        { "sizeMax", &ScreenDropletEffect::Settings::sizeMax },
        { "dryScale", &ScreenDropletEffect::Settings::dryScale },
        { "lifetime", &ScreenDropletEffect::Settings::lifetime },
        { "refraction", &ScreenDropletEffect::Settings::refraction },
        { "dripChance", &ScreenDropletEffect::Settings::dripChance },
        { "dripSpeed", &ScreenDropletEffect::Settings::dripSpeed },
        { "dripColumns", &ScreenDropletEffect::Settings::dripColumns },
        { "trailLength", &ScreenDropletEffect::Settings::trailLength },
        { "dripRadius", &ScreenDropletEffect::Settings::dripRadius },
        { "dripAccel", &ScreenDropletEffect::Settings::dripAccel },
        { "dripStepRate", &ScreenDropletEffect::Settings::dripStepRate },
        { "dripGrow", &ScreenDropletEffect::Settings::dripGrow },
        { "topSizeScale", &ScreenDropletEffect::Settings::topSizeScale },
        { "dripGrowTime", &ScreenDropletEffect::Settings::dripGrowTime },
        { "trailBeadSpacing", &ScreenDropletEffect::Settings::trailBeadSpacing },
        { "coverageTopDry", &ScreenDropletEffect::Settings::coverageTopDry },
        { "coverageTopWet", &ScreenDropletEffect::Settings::coverageTopWet },
        { "coverageFade", &ScreenDropletEffect::Settings::coverageFade },
        { "highlight", &ScreenDropletEffect::Settings::highlight },
        { "rimDarkness", &ScreenDropletEffect::Settings::rimDarkness },
        { "flowSpeed", &ScreenDropletEffect::Settings::flowSpeed },
        { "sizeSpeed", &ScreenDropletEffect::Settings::sizeSpeed },
        { "wetTime", &ScreenDropletEffect::Settings::wetTime },
        { "dryTime", &ScreenDropletEffect::Settings::dryTime },
        { "sizeDryTime", &ScreenDropletEffect::Settings::sizeDryTime },
    };
}

ScreenDropletEffect::Settings ScreenDropletEffect::DefaultSettings(Mode mode) {
    Settings s;
    if (mode == Mode::Radial) {
        // 雲の縦穴：霧雨のような小さい水滴がたくさん付き、合体しながら外へ流れる
        s.density = 22.0f;
        s.dryScale = 0.8f;
        s.lifetime = 3.2f;   // 入れ替わりが多いと炭酸のように見えて、合体・垂れが見づらい
        s.dripChance = 0.8f;
        s.dripSpeed = 0.18f;
        s.dripColumns = 22.0f;
        s.trailLength = 0.25f;
        s.dripRadius = 0.012f;
        s.dripGrow = 1.2f;
        s.dripGrowTime = 0.6f;
        s.trailBeadSpacing = 0.018f;
        s.topSizeScale = 0.6f;
        s.coverageTopDry = 0.85f;
        s.coverageTopWet = 0.55f;
        s.flowSpeed = 0.25f; // 中心からの距離が 4 秒で約 2.7 倍（0.5 だと速すぎて入れ替わりが多かった）
        s.sizeSpeed = 1.6f; // 大きい水滴ほど速く外へ滑り、途中の水滴と合体して垂れる
    }
    return s;
}

void ScreenDropletEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* /*copyRootSignature*/,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
    IDxcBlob* psBlob = dxCore->LoadShaderBlob(
        L"Resources/Shaders/PostEffect/Filters/ScreenDroplet.PS.hlsl",
        L"ps_6_0"
    );
    assert(psBlob);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
    psoDesc.pRootSignature = effectRootSignature;
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    ResetParams();
    CreateConstantBuffer(dxCore, sizeof(ParamsCB));
    UpdateConstantBuffer();
}

void ScreenDropletEffect::Update(float deltaTime, float targetWetness, Mode mode) {
    if (previewWetness_ >= 0.0f) {
        targetWetness = previewWetness_;
        mode = static_cast<Mode>(editMode_);
    }
    // モードは乾ききってから切り替える（見えている水滴が跳ばないように、まず今のモードで乾かす）
    if (mode != mode_) {
        if (wetness_ <= 0.0f) {
            mode_ = mode;
        } else {
            targetWetness = 0.0f;
        }
    }
    targetWetness = std::clamp(targetWetness, 0.0f, 1.0f);
    deltaTime = (std::max)(deltaTime, 0.0f);
    const Settings& s = Current();

    // 濡れるのは速く、乾くのはゆっくり（指数で追いかける）
    const float tau = (targetWetness > wetness_) ? s.wetTime : s.dryTime;
    wetness_ += (targetWetness - wetness_) * (1.0f - std::exp(-deltaTime / (std::max)(tau, 1e-3f)));
    if (targetWetness <= 0.0f && wetness_ < kVisibleWetness) wetness_ = 0.0f;
    // 大きさに使う濡れ具合は、上がる時は同じく速く、下がる時はゆっくり（乾く間に水滴が縮んで見えないように）
    const float sizeTau = (targetWetness > sizeWetness_) ? s.wetTime : s.sizeDryTime;
    sizeWetness_ += (targetWetness - sizeWetness_) * (1.0f - std::exp(-deltaTime / (std::max)(sizeTau, 1e-3f)));
    if (wetness_ <= 0.0f) sizeWetness_ = 0.0f;

    time_ += deltaTime;
    enabled_ = wetness_ > 0.0f;
}

void ScreenDropletEffect::Clear() {
    wetness_ = 0.0f;
    sizeWetness_ = 0.0f;
    enabled_ = false;
}

void ScreenDropletEffect::LoadSettings(const std::string& path) {
    settingsPath_ = path;
    if (!std::filesystem::exists(path)) return;
    auto result = JsonParser::ParseFile(path);
    if (!result.success) return;
    for (int m = 0; m < static_cast<int>(Mode::Count); ++m) {
        const JsonValue& section = result.value[kModeKeys[m]];
        if (!section.IsObject()) continue;
        Settings& s = settings_[m];
        for (const FieldEntry& f : kFields) {
            s.*f.member = static_cast<float>(section[f.key].AsDouble(s.*f.member));
        }
    }
}

void ScreenDropletEffect::SaveSettings() const {
    if (settingsPath_.empty()) return;
    JsonValue root = JsonValue::MakeObject();
    for (int m = 0; m < static_cast<int>(Mode::Count); ++m) {
        JsonValue section = JsonValue::MakeObject();
        for (const FieldEntry& f : kFields) {
            section[f.key] = static_cast<double>(settings_[m].*f.member);
        }
        root[kModeKeys[m]] = std::move(section);
    }
    JsonWriter::WriteFile(settingsPath_, root, { true, 2 });
}

void ScreenDropletEffect::UpdateConstantBuffer() {
    if (!constantBufferMappedPtr_) return;

    const Settings& s = Current();
    ParamsCB cb{};
    cb.time = time_;
    cb.wetness = wetness_;
    cb.density = (std::max)(s.density, 1.0f);
    cb.sizeMin = std::clamp(s.sizeMin, 0.0f, 0.35f);
    cb.sizeMax = std::clamp(s.sizeMax, cb.sizeMin, 0.35f);
    cb.dryScale = s.dryScale;
    cb.lifetime = s.lifetime;
    cb.refraction = s.refraction;
    cb.dripChance = s.dripChance;
    cb.dripSpeed = s.dripSpeed;
    cb.dripColumns = s.dripColumns;
    cb.trailLength = s.trailLength;
    cb.coverageTopDry = s.coverageTopDry;
    cb.coverageTopWet = s.coverageTopWet;
    cb.coverageFade = s.coverageFade;
    cb.highlight = s.highlight;
    cb.rimDarkness = s.rimDarkness;
    cb.dripRadius = s.dripRadius;
    cb.dripAccel = s.dripAccel;
    cb.dripStepRate = s.dripStepRate;
    cb.dripGrow = s.dripGrow;
    cb.topSizeScale = s.topSizeScale;
    cb.dripGrowTime = s.dripGrowTime;
    cb.trailBeadSpacing = s.trailBeadSpacing;
    cb.sizeWetness = sizeWetness_;
    cb.radial = (mode_ == Mode::Radial) ? 1.0f : 0.0f;
    cb.flowSpeed = (mode_ == Mode::Radial) ? s.flowSpeed : 0.0f;
    cb.sizeSpeed = std::clamp(s.sizeSpeed, 0.0f, 1.8f);
    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void ScreenDropletEffect::ShowImGui() {
#ifdef USE_IMGUI
    const char* modeNames[] = { "Gravity (river)", "Radial (cloud shaft)" };
    ImGui::Text("Mode: %s / Wetness: %.3f / Size Wetness: %.3f",
        modeNames[static_cast<int>(mode_)], wetness_, sizeWetness_);
    ImGui::Combo("Edit##ScreenDroplet", &editMode_, modeNames, IM_ARRAYSIZE(modeNames));
    bool preview = previewWetness_ >= 0.0f;
    if (ImGui::Checkbox("Preview (edit mode)##ScreenDroplet", &preview)) previewWetness_ = preview ? 1.0f : -1.0f;
    if (preview) ImGui::SliderFloat("Preview Wetness##ScreenDroplet", &previewWetness_, 0.0f, 1.0f);
    if (ImGui::Button("Save##ScreenDroplet")) SaveSettings();
    ImGui::SameLine();
    if (ImGui::Button("Reload##ScreenDroplet")) LoadSettings(settingsPath_);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", settingsPath_.c_str());

    Settings& s = settings_[editMode_];
    ImGui::DragFloat("Density##ScreenDroplet", &s.density, 0.1f, 1.0f, 60.0f);
    ImGui::SliderFloat("Size Min##ScreenDroplet", &s.sizeMin, 0.0f, 0.35f);
    ImGui::SliderFloat("Size Max##ScreenDroplet", &s.sizeMax, 0.0f, 0.35f);
    ImGui::SliderFloat("Dry Scale##ScreenDroplet", &s.dryScale, 0.0f, 1.0f);
    ImGui::SliderFloat("Top Size Scale##ScreenDroplet", &s.topSizeScale, 0.0f, 1.0f);
    ImGui::DragFloat("Lifetime [s]##ScreenDroplet", &s.lifetime, 0.05f, 0.1f, 30.0f);
    ImGui::DragFloat("Refraction##ScreenDroplet", &s.refraction, 0.01f, 0.0f, 5.0f);
    if (editMode_ == static_cast<int>(Mode::Radial)) {
        ImGui::DragFloat("Flow Speed [1/s]##ScreenDroplet", &s.flowSpeed, 0.01f, 0.0f, 5.0f);
    }
    ImGui::SliderFloat("Size Speed [cells]##ScreenDroplet", &s.sizeSpeed, 0.0f, 1.8f);
    ImGui::SliderFloat("Drip Chance##ScreenDroplet", &s.dripChance, 0.0f, 1.0f);
    ImGui::DragFloat("Drip Speed##ScreenDroplet", &s.dripSpeed, 0.005f, 0.0f, 3.0f);
    ImGui::DragFloat("Drip Columns##ScreenDroplet", &s.dripColumns, 0.1f, 1.0f, 60.0f);
    ImGui::DragFloat("Drip Radius##ScreenDroplet", &s.dripRadius, 0.001f, 0.001f, 0.2f, "%.3f");
    ImGui::DragFloat("Drip Accel##ScreenDroplet", &s.dripAccel, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Drip Step Rate##ScreenDroplet", &s.dripStepRate, 0.05f, 0.1f, 20.0f);
    ImGui::DragFloat("Drip Grow##ScreenDroplet", &s.dripGrow, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Drip Grow Time [s]##ScreenDroplet", &s.dripGrowTime, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Trail Bead Spacing##ScreenDroplet", &s.trailBeadSpacing, 0.001f, 0.002f, 0.2f, "%.3f");
    ImGui::DragFloat("Trail Length##ScreenDroplet", &s.trailLength, 0.005f, 0.0f, 1.0f);
    ImGui::SliderFloat("Coverage Top Dry##ScreenDroplet", &s.coverageTopDry, 0.0f, 1.0f);
    ImGui::SliderFloat("Coverage Top Wet##ScreenDroplet", &s.coverageTopWet, 0.0f, 1.0f);
    ImGui::SliderFloat("Coverage Fade##ScreenDroplet", &s.coverageFade, 0.0f, 1.0f);
    ImGui::SliderFloat("Highlight##ScreenDroplet", &s.highlight, 0.0f, 3.0f);
    ImGui::SliderFloat("Rim Darkness##ScreenDroplet", &s.rimDarkness, 0.0f, 1.0f);
    ImGui::DragFloat("Wet Time [s]##ScreenDroplet", &s.wetTime, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Dry Time [s]##ScreenDroplet", &s.dryTime, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Size Dry Time [s]##ScreenDroplet", &s.sizeDryTime, 0.05f, 0.0f, 30.0f);
#endif
}

void ScreenDropletEffect::ResetParams() {
    for (int m = 0; m < static_cast<int>(Mode::Count); ++m) {
        settings_[m] = DefaultSettings(static_cast<Mode>(m));
    }
    mode_ = Mode::Gravity;
    wetness_ = 0.0f;
    sizeWetness_ = 0.0f;
    previewWetness_ = -1.0f;
}
