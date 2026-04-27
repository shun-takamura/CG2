// Distortion.PS.hlsl
// ディストーションRTに描画された歪みマップを読み取り、シーンのUVをオフセットする

#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);        // シーンテクスチャ
Texture2D<float4> gDistortionMap : register(t1);   // ディストーションRT
SamplerState gSampler : register(s0);

cbuffer DistortionParams : register(b0)
{
    float strength;     // 歪みの強さ倍率
    float3 _padding;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float2 uv = input.texcoord;
    
    // ディストーションRTからオフセット情報を読み取る
    // R,G: UVオフセット方向（0.5が基準＝オフセットなし）
    // A: 強度（0で影響なし、1で最大）
    float4 distortionSample = gDistortionMap.Sample(gSampler, uv);
    
    // 0.5基準からオフセットに変換（-0.5 ~ +0.5）
    float2 offset = (distortionSample.rg - 0.5f) * 2.0f;
    
    // 強度を乗算
    float intensity = distortionSample.a;
    offset *= intensity * strength;
    
    // オフセットしたUVでシーンをサンプリング（クランプ）
    float2 distortedUV = clamp(uv + offset, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    output.color = gTexture.Sample(gSampler, distortedUV);
    
    return output;
}
