#include "Primitive.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelOutput
{
    float4 color : SV_TARGET0;
};

PixelOutput main(VertexOutput input)
{
    PixelOutput output;

    // UV変換を適用
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // テクスチャサンプリング
    float4 texColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 最終色 = テクスチャ × 頂点カラー × マテリアル色
    output.color = texColor * input.color * gMaterial.color;

  // alphaReferenceを使ったdiscard
    if (output.color.a <= gMaterial.alphaReference)
    {
        discard;
    }

    return output;
}