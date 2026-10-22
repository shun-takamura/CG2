// 雲の時間方向の積み重ね（2026-10-23）。雲海のボリュームは歩き始めを毎フレーム揺らすので、1 フレームだけだとざらつく。
// 前のフレームの結果を「雲の代表の距離」で今の画面へ写し、今のフレームと gTemporalBlend で混ぜて平均する。
// 写した値は今のフレームのまわり 3x3 の範囲に収める（動いた物・抜けた所の残像を出さない）
//   t0 = 今のフレームの雲（乗算済み）/ t1 = 雲の代表の距離 / t2 = 前のフレームまでの結果

#define CLOUD_NO_TEXTURES
#include "CloudRaymarch.hlsli"
#include "../PostEffect/Common/PostProcess.hlsli"

Texture2D<float4> gCurrent : register(t0);
Texture2D<float> gCurrentDepth : register(t1);
Texture2D<float4> gHistory : register(t2);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    const int2 pixel = int2(input.position.xy);
    const float4 current = gCurrent.Load(int3(pixel, 0));
    if (gHistoryValid == 0)
    {
        return current;
    }

    // 今の画素の雲の位置 → 前のフレームの画面の位置
    const float2 uv = input.texcoord;
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), gInvViewProj);
    farPoint.xyz /= farPoint.w;
    const float3 dir = normalize(farPoint.xyz - gEye);
    const float distance = gCurrentDepth.Load(int3(pixel, 0));
    const float3 world = gEye + dir * min(distance, 100000.0f);
    const float4 prevClip = mul(float4(world, 1.0f), gPrevViewProj);
    if (prevClip.w <= 1e-4f)
    {
        return current;
    }
    const float2 prevNdc = prevClip.xy / prevClip.w;
    const float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
    if (any(prevUv < 0.0f) || any(prevUv > 1.0f))
    {
        return current;
    }
    float4 history = gHistory.SampleLevel(gLinearClampSampler, prevUv, 0);

    // まわり 3x3 の範囲に収める
    float4 lo = current;
    float4 hi = current;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float4 c = gCurrent.Load(int3(pixel + int2(x, y), 0));
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }
    history = clamp(history, lo, hi);
    return lerp(history, current, gTemporalBlend);
}
