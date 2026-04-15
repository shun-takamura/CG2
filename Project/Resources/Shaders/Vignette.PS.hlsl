// Vignette.PS.hlsl
// ヴィネット効果専用のピクセルシェーダー（周辺減光）

#include "PostProcess.hlsli"

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


// Vignette専用の定数バッファ
cbuffer VignetteParams : register(b0)
{
    float intensity; // offset: 0
    float power; // offset: 4
    float scale; // offset: 8
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 元の色をサンプリング
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 中心からの距離を計算
    float2 correct = input.texcoord * (1.0f - input.texcoord.yx);

    // 中心が明るく、周辺が暗くなるグラデーション
    float vignette = correct.x * correct.y * scale;

    // カーブ調整
    vignette = pow(vignette, 1.0f / power);

    // 0〜1にクランプ
    vignette = saturate(vignette);

    // intensityでブレンド（0なら効果なし、1で最大）
    output.color.rgb *= lerp(1.0f, vignette, intensity);

    return output;
}