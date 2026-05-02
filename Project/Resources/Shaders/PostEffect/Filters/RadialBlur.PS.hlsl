// RadialBlur.PS.hlsl
// 中心から放射状にぼかすエフェクト

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer RadialBlurParams : register(b0)
{
    float2 center;     // ぼかし中心 (UV空間, 0..1)
    float blurWidth;   // 1サンプルごとのオフセット倍率
    int numSamples;    // サンプル数
};

// ループ回数の上限（コンパイル時固定）
static const int kMaxSamples = 32;

PixelShaderOutput main(VertexShaderOutput input)
{
    // 中心から見た方向ベクトル（正規化はせず距離成分も込みでサンプル）
    float2 direction = input.texcoord - center;

    float3 outputColor = float3(0.0f, 0.0f, 0.0f);

    int samples = clamp(numSamples, 1, kMaxSamples);

    for (int i = 0; i < kMaxSamples; ++i)
    {
        if (i >= samples)
        {
            break;
        }
        float2 texcoord = input.texcoord + direction * blurWidth * (float) i;
        outputColor += gTexture.Sample(gSampler, texcoord).rgb;
    }

    outputColor *= rcp((float) samples);

    PixelShaderOutput output;
    output.color.rgb = outputColor;
    output.color.a = 1.0f;
    return output;
}
