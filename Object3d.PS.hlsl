#include "Object3d.hlsli"

//ConstantBuffer<Material> gMaterial : register(b0);

//cbuffer gDirectionalLight : register(b1)
//{
//    float4 color;
//    float3 direction;
//    float intensity;
//};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    int lightingType;
};

cbuffer gTransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
};

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
}

Texture2D<float4> gTexture : register(t0); // SRVはt
SamplerState gSumpler : register(s0); // Sumplerはs

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSumpler, transformedUV.xy);

    if (gMaterial.enableLighting != 0){
        float cos = 1.0f; // 初期値（None用）

        float3 normal = normalize(input.normal);
        float3 lightDir = normalize(-gDirectionalLight.direction);

        if (gDirectionalLight.lightingType == 1)
        {
            // Lambert
            cos = saturate(dot(normal, lightDir));
        }
        else if (gDirectionalLight.lightingType == 2)
        {
            // Half-Lambert
            float NdotL = dot(normal, lightDir);
            cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        }
        else
        {
            // lightingType == 0 (None)
            cos = 1.0f;
        }

        //output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        output.color.a = gMaterial.color.a * textureColor.a;

    }else{
        output.color = gMaterial.color * textureColor;
    }

    if (textureColor.a == 0.0f){
        discard;
    }
    
    if (output.color.a == 0.0f){
        discard;
    }
    
    return output;
}