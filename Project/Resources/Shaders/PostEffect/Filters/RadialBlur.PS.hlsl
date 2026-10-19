// RadialBlur.PS.hlsl
// 中心から放射状にぼかすエフェクト
// outline 用 root signature（color t0 + depth t1 + cbuffer b0 + linear s0 + point s1）。
// depthThreshold > 0 のとき、深度がそれより手前（目に近い）の画素はぼかさず元の色のまま、
// ぼかしの平均にも手前の画素を混ぜない（一人称のコクピットの内装にかけないため。16_StageStart.md F5）

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerState gSamplerPoint : register(s1);

cbuffer RadialBlurParams : register(b0)
{
    float2 center;         // ぼかし中心 (UV空間, 0..1)
    float blurWidth;       // 1サンプルごとのオフセット倍率
    int numSamples;        // サンプル数
    float depthThreshold;  // 0 で無効。深度（0..1、手前が 0）がこれ未満の画素はぼかさない
    float3 _padding;
};

// ループ回数の上限（コンパイル時固定）
static const int kMaxSamples = 32;

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    output.color.a = 1.0f;

    const bool masked = depthThreshold > 0.0f;
    // 手前（マスクの内側）の画素はそのまま
    if (masked && gDepthTexture.Sample(gSamplerPoint, input.texcoord) < depthThreshold)
    {
        output.color.rgb = gTexture.Sample(gSampler, input.texcoord).rgb;
        return output;
    }

    // 中心から見た方向ベクトル（正規化はせず距離成分も込みでサンプル）
    float2 direction = input.texcoord - center;

    float3 outputColor = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;

    int samples = clamp(numSamples, 1, kMaxSamples);

    for (int i = 0; i < kMaxSamples; ++i)
    {
        if (i >= samples)
        {
            break;
        }
        float2 texcoord = input.texcoord + direction * blurWidth * (float) i;
        // 手前の画素はぼかしの平均に混ぜない（コクピットの色が外ににじまないように）
        if (masked && gDepthTexture.Sample(gSamplerPoint, texcoord) < depthThreshold)
        {
            continue;
        }
        outputColor += gTexture.Sample(gSampler, texcoord).rgb;
        weight += 1.0f;
    }

    output.color.rgb = weight > 0.0f ? outputColor * rcp(weight) : gTexture.Sample(gSampler, input.texcoord).rgb;
    return output;
}
