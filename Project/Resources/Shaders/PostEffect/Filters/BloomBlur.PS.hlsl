// BloomBlur.PS.hlsl
// 1 方向の 9 タップガウスぼかし。隣り合うタップをバイリニアの中間点でまとめて 5 回のフェッチで済ませる。
// 横 → 縦の 2 パスで 2 次元のぼかしになる。

#include "Bloom.hlsli"

Texture2D<float4> gSource : register(t0);

// 9 タップ（σ≒2）の重みを 2 タップずつまとめた値
static const float kOffsets[3] = { 0.0f, 1.3846153846f, 3.2307692308f };
static const float kWeights[3] = { 0.2270270270f, 0.3162162162f, 0.0702702703f };

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float2 step = direction * texelSize;
    float3 c = gSource.Sample(gSampler, input.texcoord).rgb * kWeights[0];
    [unroll]
    for (int i = 1; i < 3; ++i)
    {
        c += gSource.Sample(gSampler, input.texcoord + step * kOffsets[i]).rgb * kWeights[i];
        c += gSource.Sample(gSampler, input.texcoord - step * kOffsets[i]).rgb * kWeights[i];
    }
    output.color = float4(c, 1.0f);
    return output;
}
