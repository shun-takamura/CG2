// ColorInvert.PS.hlsl
#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer ColorInvertParams : register(b0)
{
    float intensity; // 0.0 = 元の色, 1.0 = 完全反転
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 color = gTexture.Sample(gSampler, input.texcoord);
    float3 inverted = 1.0f - color.rgb;

    output.color.rgb = lerp(color.rgb, inverted, intensity);
    output.color.a = color.a;

    return output;
}
