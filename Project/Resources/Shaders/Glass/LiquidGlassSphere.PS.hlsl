// LiquidGlassSphere.PS.hlsl
// 液体の入ったガラスの球（LiquidGlassSphere.cpp）。乗算済み α で重ねる。
//   ガラス：フレネル（Schlick）で IBL の映り込みと平行光源のハイライト、縁の明るさ、奥側の内面のかすかな縁
//   液体  ：メッシュを持たない。視線と球（解析解）・液面（平面）の交わりから「液体の中を通る長さ」を求め、
//           長さで発光と不透明度を決める。視線が中心の近くを通るほど白に寄せる（芯）。
//           液面は RippleSimulation の高さマップの傾きで揺らし、ガラスに触れる所に光る線を引く
// gBloomPass = 1 のときは発光 RT 用に光る分だけを出す（加算で描く）。
#include "LiquidGlassSphere.hlsli"

cbuffer GlassParams : register(b0)
{
    float3 gCenter;
    float  gRadius;
    float3 gCameraPosition;
    float  gSurfaceOffset;       // 液面の中心からの距離（液面の法線の向き）。-R=空 / +R=満杯
    float3 gSurfaceNormal;       // 液面の法線（ワールド・上向き）。液体は dot(p - C, n) <= offset の側
    float  gTime;

    // ----- ガラス -----
    float3 gGlassTint;
    float  gFresnelF0;
    float3 gRimColor;
    float  gRimPower;
    float  gReflectionIntensity;
    float  gSpecularIntensity;
    float  gSpecularPower;
    float  gGlassAlpha;
    float  gBackRimIntensity;
    float  gLensIor;
    int    gBloomPass;
    float  gRimBloom;

    // ----- 液体（エーテル） -----
    float3 gLiquidColor;
    float  gLiquidGlow;
    float  gLiquidDensity;
    float  gLiquidAbsorption;
    float  gCoreWhite;
    float  gCorePower;
    float3 gSurfaceColor;
    float  gSurfaceIntensity;
    float  gMeniscusWidth;
    float  gMeniscusIntensity;
    float  gBloomScale;
    float  gRippleNormalScale;

    // ----- 波紋 -----
    float  gRippleUvScale;       // 中心からの距離 [m] → UV
    float  gRippleTexel;
    int    gRippleEnabled;
    float  gPadding0;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    int lightingType;
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
};

TextureCube<float4> gEnvironmentTexture : register(t0);
Texture2D<float2> gRippleHeight : register(t1);
SamplerState gSampler : register(s0);
SamplerState gClampSampler : register(s1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float Schlick(float f0, float cosTheta)
{
    const float m = 1.0f - saturate(cosTheta);
    const float m2 = m * m;
    return f0 + (1.0f - f0) * m2 * m2 * m;
}

// 液面の点の傾き（高さマップの中心差分）。範囲外・無効なら 0
float2 RippleGradient(float3 worldPosition)
{
    if (gRippleEnabled == 0)
    {
        return float2(0.0f, 0.0f);
    }
    const float2 uv = (worldPosition.xz - gCenter.xz) * gRippleUvScale + 0.5f;
    if (any(uv <= 0.0f) || any(uv >= 1.0f))
    {
        return float2(0.0f, 0.0f);
    }
    const float2 du = float2(gRippleTexel, 0.0f);
    const float2 dv = float2(0.0f, gRippleTexel);
    const float hx = gRippleHeight.SampleLevel(gClampSampler, uv + du, 0).r - gRippleHeight.SampleLevel(gClampSampler, uv - du, 0).r;
    const float hz = gRippleHeight.SampleLevel(gClampSampler, uv + dv, 0).r - gRippleHeight.SampleLevel(gClampSampler, uv - dv, 0).r;
    // 波源の深さは [m]。UV の差をワールドの長さに直して傾き（m あたり）にする
    return float2(hx, hz) / (2.0f * gRippleTexel) * gRippleUvScale;
}

// ガラスに液面が触れる所の光る線（s = 液面からの符号付き距離）
float Meniscus(float s)
{
    const float w = max(gMeniscusWidth, 1e-4f);
    const float k = saturate(1.0f - abs(s) / w);
    return k * k;
}

PixelShaderOutput main(GlassVertexOutput input)
{
    PixelShaderOutput output;

    const float3 rd = normalize(input.worldPosition - gCameraPosition);

    // ----- 視線と球（解析解）。多角形の面ではなく真球の上で塗る -----
    const float3 oc = gCameraPosition - gCenter;
    const float b = dot(oc, rd);
    const float c = dot(oc, oc) - gRadius * gRadius;
    const float disc = b * b - c;
    if (disc <= 0.0f)
    {
        discard;
    }
    const float sq = sqrt(disc);
    const float tEnter = -b - sq;
    const float tExit = -b + sq;
    // 奥側の面のピクセルは描かない（巻き順に頼らない。手前の面から奥まで計算する）
    const float tPixel = dot(input.worldPosition - gCameraPosition, rd);
    if (abs(tPixel - tExit) < abs(tPixel - tEnter))
    {
        discard;
    }

    const float3 entry = gCameraPosition + rd * max(tEnter, 0.0f);
    const float3 N = normalize(entry - gCenter);
    const float NdotV = saturate(dot(N, -rd));

    // ----- ガラスの表面 -----
    const float F = Schlick(gFresnelF0, NdotV);
    const float3 R = reflect(rd, N);
    const float3 env = gEnvironmentTexture.Sample(gSampler, R).rgb * gReflectionIntensity * gGlassTint;
    const float3 L = normalize(-gDirectionalLight.direction);
    const float3 lightColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
    const float3 specular = lightColor * pow(saturate(dot(R, L)), gSpecularPower) * gSpecularIntensity;
    const float rim = pow(1.0f - NdotV, gRimPower);

    // ----- 球の中（屈折で少し曲げてから液体を通す） -----
    float3 rdIn = refract(rd, N, 1.0f / max(gLensIor, 1.0f));
    if (dot(rdIn, rdIn) < 1e-6f)
    {
        rdIn = rd;
    }
    rdIn = normalize(rdIn);
    const float tFar = max(-2.0f * dot(entry - gCenter, rdIn), 0.0f); // 入った点から反対側の内面まで
    const float3 back = entry + rdIn * tFar;
    const float3 Nb = normalize(back - gCenter);
    const float backRim = pow(1.0f - saturate(dot(Nb, rdIn)), gRimPower) * gBackRimIntensity;

    // ----- 液体の中を通る区間 [ta, tb] -----
    const float3 n = gSurfaceNormal;
    const float s0 = dot(entry - gCenter, n) - gSurfaceOffset; // 入った点の液面からの距離（負＝液体の中）
    const float ds = dot(rdIn, n);
    float ta = 0.0f;
    float tb = tFar;
    float tPlane = -1.0f;
    if (abs(ds) > 1e-5f)
    {
        tPlane = -s0 / ds;
        if (ds > 0.0f)
        {
            tb = min(tb, tPlane); // 上へ向かう：液面より手前だけが液体
        }
        else
        {
            ta = max(ta, tPlane); // 下へ向かう：液面より先だけが液体
        }
    }
    else if (s0 > 0.0f)
    {
        tb = ta;
    }
    const float len = max(tb - ta, 0.0f);

    // 発光：中を通る長さで飽和させる。視線が中心に近いほど白に寄せる（芯）
    float3 emit = float3(0.0f, 0.0f, 0.0f);
    float liquidAlpha = 0.0f;
    if (len > 0.0f)
    {
        const float tc = clamp(dot(gCenter - entry, rdIn), ta, tb);
        const float dist = length(entry + rdIn * tc - gCenter) / gRadius;
        const float core = pow(saturate(1.0f - dist), gCorePower) * gCoreWhite;
        const float3 body = lerp(gLiquidColor, float3(1.0f, 1.0f, 1.0f), saturate(core));
        emit = body * gLiquidGlow * (1.0f - exp(-gLiquidDensity * len));
        liquidAlpha = 1.0f - exp(-gLiquidAbsorption * len);
    }

    // ----- 液面（球の中で視線が液面を横切るとき） -----
    float3 surface = float3(0.0f, 0.0f, 0.0f);
    if (tPlane > 0.0f && tPlane < tFar)
    {
        const float3 S = entry + rdIn * tPlane;
        const float2 grad = RippleGradient(S) * gRippleNormalScale;
        const float3 nS = normalize(n - float3(grad.x, 0.0f, grad.y));
        const float cosS = abs(dot(nS, rdIn));
        const float3 RS = reflect(rdIn, ds < 0.0f ? nS : -nS);
        const float fresnelS = Schlick(0.02f, cosS);
        const float3 sheen = gEnvironmentTexture.Sample(gSampler, RS).rgb * gReflectionIntensity * fresnelS
            + lightColor * pow(saturate(dot(RS, L)), gSpecularPower * 0.5f) * gSpecularIntensity;
        // 浅い角度ほど明るい液面の色。波の傾きで明るさがゆらぐ
        const float wave = saturate(1.0f + dot(grad, float2(0.7f, 0.7f)));
        const float3 glow = gSurfaceColor * gSurfaceIntensity * gLiquidGlow * (1.0f - 0.5f * cosS) * wave;
        // 下から見上げるときは液体の中を通ってから届く
        const float visibility = ds < 0.0f ? 1.0f : exp(-gLiquidAbsorption * max(tPlane - ta, 0.0f));
        surface = (glow + sheen) * visibility;
    }

    // ----- ガラスに液面が触れる線（手前の面と奥の面） -----
    const float3 meniscusColor = lerp(gLiquidColor, float3(1.0f, 1.0f, 1.0f), 0.5f) * gMeniscusIntensity * gLiquidGlow;
    const float meniscusFront = Meniscus(s0);
    const float meniscusBack = Meniscus(dot(back - gCenter, n) - gSurfaceOffset) * (1.0f - liquidAlpha) * 0.6f;
    const float3 meniscus = meniscusColor * (meniscusFront + meniscusBack);

    // ----- 合成（乗算済み α）。ガラスの奥＝液体と奥の内面、手前＝ガラスの表面 -----
    const float3 inner = emit + surface + gRimColor * backRim * (1.0f - liquidAlpha);

    if (gBloomPass != 0)
    {
        const float3 glowOnly = (1.0f - F) * (emit + surface * 0.5f) + meniscus + gRimColor * rim * gRimBloom;
        output.color = float4(glowOnly * gBloomScale, 0.0f);
        return output;
    }

    const float3 rgb = env * F + specular + gRimColor * rim + meniscus + (1.0f - F) * inner;
    const float alpha = saturate(gGlassAlpha + F + (1.0f - F) * liquidAlpha);
    output.color = float4(rgb, alpha);
    return output;
}
