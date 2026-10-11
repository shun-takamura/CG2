// 画面を上から見下ろした水面として扱う共通部品（ScreenWaterSurface と対）。
//   t0 = そのフレームのシーン RT のコピー / t1 = 波の高さ（RippleSimulation.CS、R = 今の高さ）/ s0 = クランプ・線形
// 使う側は定数バッファの先頭に ScreenWaterParams gWater を置く（ScreenWaterSurface::ConstantsForGPU と 1:1）。
// 長さは「画面の幅 = 1」で測る（縦横比で円がつぶれないように）。

#ifndef SCREEN_WATER_HLSLI
#define SCREEN_WATER_HLSLI

struct ScreenWaterParams
{
    float2 rippleCenter;   // 波の範囲の中心（画面の幅 = 1 の座標）
    float rippleSize;      // 波の範囲の一辺
    float refraction;      // 波の傾きで画面をずらす量
    float2 lightDirection; // 光の来る向き（画面 UV、正規化済み）
    float highlight;       // 斜面の明暗の強さ
    float aspect;          // 幅 / 高さ
};

Texture2D<float4> gScene : register(t0);
Texture2D<float2> gRippleHeight : register(t1);
SamplerState gLinearClampSampler : register(s0);

static const float kRippleTexel = 1.0f / 512.0f; // RippleSimulation::kResolution

// 画面 UV → 画面の幅 = 1 の座標
float2 ToWidthSpace(ScreenWaterParams water, float2 uv)
{
    return float2(uv.x, uv.y / water.aspect);
}

// 画面 UV → 波の高さマップの UV
float2 ToRippleUV(ScreenWaterParams water, float2 uv)
{
    return (ToWidthSpace(water, uv) - water.rippleCenter) / water.rippleSize + 0.5f;
}

// 波の高さの傾き（画面 UV の向き）。へこみが負なので、傾きは「低い方から高い方」
float2 RippleSlope(ScreenWaterParams water, float2 uv)
{
    const float2 r = ToRippleUV(water, uv);
    const float2 du = float2(kRippleTexel, 0.0f);
    const float2 dv = float2(0.0f, kRippleTexel);
    const float hx = gRippleHeight.SampleLevel(gLinearClampSampler, r + du, 0).r - gRippleHeight.SampleLevel(gLinearClampSampler, r - du, 0).r;
    const float hy = gRippleHeight.SampleLevel(gLinearClampSampler, r + dv, 0).r - gRippleHeight.SampleLevel(gLinearClampSampler, r - dv, 0).r;
    return float2(hx, hy);
}

// 屈折：波の斜面の分だけ下の画面がずれて見える
float2 RefractUV(ScreenWaterParams water, float2 uv, float2 slope)
{
    return uv + slope * water.refraction;
}

// 斜面の明暗：光の来る側を向いた斜面は明るく、反対側は暗く（白地でも黒地でも波が読めるように）
// 傾きは 1 テクセルあたり 0.005 前後（輪）〜 0.02（着水点）。30 倍で輪が 0.1 前後の明暗になる
float3 ApplyRippleHighlight(ScreenWaterParams water, float3 color, float2 slope)
{
    const float facing = dot(slope, -water.lightDirection) * 30.0f;
    return saturate(color + facing * water.highlight);
}

#endif // SCREEN_WATER_HLSLI
