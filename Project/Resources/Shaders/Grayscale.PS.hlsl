// Grayscale.PS.hlsl
// グレースケール専用のピクセルシェーダー（BT.709方式）
// intensityで元の色とのブレンド具合を調整可能

#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// Grayscale専用の定数バッファ
cbuffer GrayscaleParams : register(b0)
{
    float intensity;  // 0.0 = 元の色, 1.0 = 完全な白黒
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 元の色をサンプリング
    float4 color = gTexture.Sample(gSampler, input.texcoord);

    // BT.709方式でグレースケール値を計算
    float value = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    float3 grayscale = float3(value, value, value);

    // 元の色とグレースケールをブレンド
    output.color.rgb = lerp(color.rgb, grayscale, intensity);
    output.color.a = color.a;

    return output;
}
