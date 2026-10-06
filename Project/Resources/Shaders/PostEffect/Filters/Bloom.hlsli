// Bloom.hlsli
// BloomEffect の縮小/ぼかし/合成パス共通の定義。
// パラメータはルート定数（b0）。BloomEffect::Params と並びを一致させること。

#include "../Common/PostProcess.hlsli"

cbuffer BloomParams : register(b0)
{
    float2 texelSize;    // 入力テクスチャの 1 テクセル（UV）
    float2 direction;    // ぼかし方向（横=(1,0) / 縦=(0,1)）
    float4 levelWeights; // 合成時の各段の重み（1/2, 1/4, 1/8, 1/16）
    float3 tint;
    float intensity;
};

SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
