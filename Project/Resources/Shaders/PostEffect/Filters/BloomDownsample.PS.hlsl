// BloomDownsample.PS.hlsl
// 半分のサイズへ縮小。入力テクセルの角 4 点をバイリニアで拾うので、実質 4x4 テクセルの平均になり
// 縮小時のちらつき（細い光が段ごとに出たり消えたり）を抑えられる。

#include "Bloom.hlsli"

Texture2D<float4> gSource : register(t0);

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float2 o = texelSize;
    float3 c = gSource.Sample(gSampler, input.texcoord + float2(-o.x, -o.y)).rgb
             + gSource.Sample(gSampler, input.texcoord + float2( o.x, -o.y)).rgb
             + gSource.Sample(gSampler, input.texcoord + float2(-o.x,  o.y)).rgb
             + gSource.Sample(gSampler, input.texcoord + float2( o.x,  o.y)).rgb;
    output.color = float4(c * 0.25f, 1.0f);
    return output;
}
