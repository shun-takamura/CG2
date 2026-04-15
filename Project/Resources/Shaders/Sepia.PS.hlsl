// Sepia.PS.hlsl
// セピア調専用のピクセルシェーダー

#include "PostProcess.hlsli"

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// Sepia専用の定数バッファ
cbuffer SepiaParams : register(b0)
{
    float intensity; // offset: 0
    float sepiaColorR; // offset: 4
    float sepiaColorG; // offset: 8
    float sepiaColorB; // offset: 12
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 元の色をサンプリング
    float4 color = gTexture.Sample(gSampler, input.texcoord);

    // BT.709方式でグレースケール値を計算
    float grayscaleValue = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));

    // セピア色を適用
    float3 sepiaColor = float3(sepiaColorR, sepiaColorG, sepiaColorB);
    float3 sepia = grayscaleValue * sepiaColor;

    // 元の色とセピア色をブレンド
    output.color.rgb = lerp(color.rgb, sepia, intensity);
    output.color.a = color.a;

    return output;
}
