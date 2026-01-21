#include "Object3d.hlsli"

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    int lightingType;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
};

cbuffer gTransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
};

struct Camera
{
    float3 worldPosition;
};

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding;
    float4x4 uvTransform;
    float shininess;
    float3 padding2;
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

cbuffer CameraBuffer : register(b2)
{
    Camera gCamera;
}

cbuffer PointLightBuffer : register(b3)
{
    PointLight gPointLight;
}

Texture2D<float4> gTexture : register(t0); // SRVはt
SamplerState gSumpler : register(s0); // Sumplerはs


PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSumpler, transformedUV.xy);

    if (gMaterial.enableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        
        // DirectionalLight
        float3 lightDir = normalize(-gDirectionalLight.direction);
       
        // 拡散反射
        float cos = 1.0f;
        
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
        
        float3 diffuseDirectionalLight = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        
        // 鏡面反射
        float3 halfVector = normalize(-gDirectionalLight.direction + toEye);
        float NDotH = dot(normal, halfVector);
        float specularPow = pow(saturate(NDotH), gMaterial.shininess);
        
        // 鏡面反射色
        float3 specularDirectionalLight = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow;
        
        // PointLight
        float3 pointLightDirection = normalize( input.worldPosition - gPointLight.position);
        
        // PointLight用の拡散反射係数 (法線とライト方向の内積)
        float pointLightCos = saturate(dot(normal, -pointLightDirection));
       
        // PointLightの拡散反射
        float3 diffusePointLight = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * pointLightCos * gPointLight.intensity;
        
        // PointLightの鏡面反射
        float3 pointLightHalfVector = normalize(-pointLightDirection + toEye);
        float pointLightNDotH = dot(normal, pointLightHalfVector);
        float pointLightSpecularPow = pow(saturate(pointLightNDotH), gMaterial.shininess);
        float3 specularPointLight = gPointLight.color.rgb * gPointLight.intensity * pointLightSpecularPow;
        
        // 最終出力
        output.color.rgb = diffuseDirectionalLight + specularDirectionalLight + diffusePointLight + specularPointLight;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }

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
