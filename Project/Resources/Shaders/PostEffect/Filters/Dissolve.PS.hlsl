// Dissolve.PS.hlsl
// マスク値が threshold 以下のピクセルを抜き、
// 閾値付近の幅 edgeWidth の領域を edge とみなして指定色を加算する
//
// マスク値は以下のいずれかから取得する
//  - useNoise == 0: マスクテクスチャ (t1) のRチャネル
//  - useNoise != 0: GPU側で生成した擬似乱数 (texcoord + time をseedにする)

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
    float useNoise;      // 0=テクスチャ, それ以外=GPUノイズ
    float noiseScale;    // ノイズタイル倍率
    float4 edgeColor;    // エッジ加算色 (rgb), 強度 (a)
    float4 fillColor;    // 抜き部分の塗り色 (rgb), 強度 (a)
    float noiseTime;     // ノイズSeedに加算する経過時間
    float3 _pad;         // 16バイト境界揃え用
};

// 2次元入力から擬似乱数(0..1)を返す
float rand2dTo1d(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

// 整数格子の乱数を滑らかに補間して塊状のノイズを作る (Value Noise)
float valueNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0f - 2.0f * f); // smoothstep補間

    float a = rand2dTo1d(i);
    float b = rand2dTo1d(i + float2(1.0f, 0.0f));
    float c = rand2dTo1d(i + float2(0.0f, 1.0f));
    float d = rand2dTo1d(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    float mask;
    if (useNoise > 0.5f)
    {
        // texcoordをスケールしtimeでSeedを変化させる
        float2 seed = input.texcoord * noiseScale + noiseTime;
        mask = valueNoise(seed);
    }
    else
    {
        // マスクは輝度として読む（rチャネルでよい）
        mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
    }

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
