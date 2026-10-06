// BloomComposite.PS.hlsl
// シーン（t0）に、縮小＋ぼかし済みの各段（t1..t4）を重み付きで加算する。
// 小さい段ほど広く柔らかい光になるので、全段を足すと芯は鋭く裾は広いグローになる。

#include "Bloom.hlsli"

Texture2D<float4> gScene  : register(t0);
Texture2D<float4> gLevel0 : register(t1); // 1/2
Texture2D<float4> gLevel1 : register(t2); // 1/4
Texture2D<float4> gLevel2 : register(t3); // 1/8
Texture2D<float4> gLevel3 : register(t4); // 1/16

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 scene = gScene.Sample(gSampler, input.texcoord);

    float3 bloom = gLevel0.Sample(gSampler, input.texcoord).rgb * levelWeights.x
                 + gLevel1.Sample(gSampler, input.texcoord).rgb * levelWeights.y
                 + gLevel2.Sample(gSampler, input.texcoord).rgb * levelWeights.z
                 + gLevel3.Sample(gSampler, input.texcoord).rgb * levelWeights.w;

    output.color = float4(scene.rgb + bloom * tint * intensity, scene.a);
    return output;
}
