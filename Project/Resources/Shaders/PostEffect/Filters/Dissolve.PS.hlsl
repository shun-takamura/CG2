// Dissolve.PS.hlsl
// マスクテクスチャを利用したディゾルブエフェクト
// マスク値が threshold 以下のピクセルを discard し、
// 閾値付近の幅 edgeWidth の領域を edge とみなして指定色を加算する

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gMaskTexture : register(t1);
SamplerState gSampler : register(s0);

cbuffer DissolveParams : register(b0)
{
    float threshold;     // 閾値（0..1）
    float edgeWidth;     // エッジ範囲の幅
    float2 _pad;         // 16バイト境界揃え用
    float4 edgeColor;    // エッジ加算色 (rgb), 強度 (a)
    float4 fillColor;    // 抜き部分の塗り色 (rgb), 強度 (a)
};

PixelShaderOutput main(VertexShaderOutput input)
{
    // マスクは輝度として読む（rチャネルでよい）
    float mask = gMaskTexture.Sample(gSampler, input.texcoord).r;

    PixelShaderOutput output;

    if (mask <= threshold)
    {
        // 抜き部分はfillColorで塗る（discardだとswapchainに前フレームが残るため固定色で塗る）
        output.color.rgb = fillColor.rgb * fillColor.a;
        output.color.a = 1.0f;
        return output;
    }

    // edgeっぽさを smoothstep で算出（境界に近いほど 1 に近い）
    float edge = 1.0f - smoothstep(threshold, threshold + edgeWidth, mask);

    float4 baseColor = gTexture.Sample(gSampler, input.texcoord);

    output.color.rgb = baseColor.rgb + edge * edgeColor.rgb * edgeColor.a;
    output.color.a = 1.0f;
    return output;
}
