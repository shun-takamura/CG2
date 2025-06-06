#include "Object3d.hlsli"

//ConstantBuffer<Material> gMaterial : register(b0);

struct TransformationMatrix{
    float4x4 WVP;
    float4x4 World;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct Material
{
    float4 color;
    int enableLighting;
};

struct PixelShaderOutput{
    float4 color : SV_TARGET0;
};

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

Texture2D<float4> gTexture : register(t0); // SRVはt
SamplerState gSumpler : register(s0); // Sumplerはs

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

PixelShaderOutput main(VertexShaderOutput input){
    
    float4 textureColor = gTexture.Sample(gSumpler, input.texcoord);
    
    PixelShaderOutput output;
    output.color = gMaterial.color * textureColor;
    return output;
}