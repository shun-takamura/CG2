#include "Sprite.hlsli"

struct SpriteMaterial
{
    float4 color;
    float4x4 uvTransform;
};

cbuffer MaterialBuffer : register(b0)
{
    SpriteMaterial gMaterial;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV変換
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // マテリアル色とテクスチャ色を乗算
    output.color = gMaterial.color * textureColor;

    // テクスチャαまたは最終αが0ならdiscard
    if (textureColor.a == 0.0f)
    {
        discard;
    }
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
    
}