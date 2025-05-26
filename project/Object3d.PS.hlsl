#include "Object3d.hlsli"

struct Material{
    float4 color;
};

//ConstantBuffer<Material> gMaterial : register(b0);

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

struct PixelShaderOutput{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0); // SRVはt
SamplerState gSumpler : register(s0); // Sumplerはs

PixelShaderOutput main(VertexShaderOutput input){
    
    float4 textureColor = gTexture.Sample(gSumpler, input.texcoord);
    
    PixelShaderOutput output;
    output.color = gMaterial.color * textureColor;
    return output;
}