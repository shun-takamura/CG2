#include "Skybox.hlsli"

struct Material
{
    float4 color;
    float blendT;
    float3 padding;
};

ConstantBuffer<Material> gMaterial : register(b0);

TextureCube<float4> gTexture0 : register(t0);
TextureCube<float4> gTexture1 : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    // 2枚のCubemapを blendT で補間（シームレスなクロスフェード）
    float4 c0 = gTexture0.Sample(gSampler, input.texcoord);
    float4 c1 = gTexture1.Sample(gSampler, input.texcoord);
    float4 textureColor = lerp(c0, c1, gMaterial.blendT);
    output.color = textureColor * gMaterial.color;
    return output;
}
