#include "Primitive.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);

// 3つのサンプラー（material.samplerMode で選択）
//   s0: WRAP U  / WRAP V
//   s1: WRAP U  / CLAMP V  （Ring/Cylinderの中心アーティファクト回避）
//   s2: CLAMP U / CLAMP V
SamplerState gSamplerWrap        : register(s0);
SamplerState gSamplerWrapU_ClampV: register(s1);
SamplerState gSamplerClamp       : register(s2);

struct PixelOutput
{
    float4 color : SV_TARGET0;
};

PixelOutput main(VertexOutput input)
{
    PixelOutput output;

    // UV変換を適用
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // サンプラー選択（uniform 分岐なのでGPUコストは小さい）
    float4 texColor;
    if (gMaterial.samplerMode == 1)
    {
        texColor = gTexture.Sample(gSamplerWrapU_ClampV, transformedUV.xy);
    }
    else if (gMaterial.samplerMode == 2)
    {
        texColor = gTexture.Sample(gSamplerClamp, transformedUV.xy);
    }
    else
    {
        texColor = gTexture.Sample(gSamplerWrap, transformedUV.xy);
    }

    // 最終色 = テクスチャ × 頂点カラー × マテリアル色
    output.color = texColor * input.color * gMaterial.color;

    // alphaReferenceを使ったdiscard
    if (output.color.a <= gMaterial.alphaReference)
    {
        discard;
    }

    return output;
}
