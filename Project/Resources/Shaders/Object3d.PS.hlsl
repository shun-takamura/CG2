#include "Object3d.hlsli"

// 最大ライト数（C++側と合わせる）
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 8

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
    float radius;
    float decay;
    float2 padding;
};

struct PointLightGroup
{
    PointLight lights[MAX_POINT_LIGHTS];
    uint activeCount;
    float3 padding;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float padding;
};

struct SpotLightGroup
{
    SpotLight lights[MAX_SPOT_LIGHTS];
    uint activeCount;
    float3 padding;
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
    PointLightGroup gPointLightGroup;
}

cbuffer SpotLightBuffer : register(b4)
{
    SpotLightGroup gSpotLightGroup;
}

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (gMaterial.enableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        
        // 最終的な色を蓄積する変数
        float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
        float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

        // ===== DirectionalLight =====
        {
            float3 lightDir = normalize(-gDirectionalLight.direction);
            
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
            
            float3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            
            float3 halfVector = normalize(-gDirectionalLight.direction + toEye);
            float NDotH = dot(normal, halfVector);
            float specularPow = pow(saturate(NDotH), gMaterial.shininess);
            float3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow;
            
            totalDiffuse += diffuse;
            totalSpecular += specular;
        }

        // ===== PointLights (複数対応) =====
        for (uint i = 0; i < gPointLightGroup.activeCount; ++i)
        {
            PointLight pl = gPointLightGroup.lights[i];
            
            float distance = length(pl.position - input.worldPosition);
            float factor = pow(saturate(-distance / pl.radius + 1.0f), pl.decay);
            
            float3 pointLightDirection = normalize(input.worldPosition - pl.position);
            float pointLightCos = saturate(dot(normal, -pointLightDirection));
            
            float3 diffuse = gMaterial.color.rgb * textureColor.rgb * pl.color.rgb * pointLightCos * pl.intensity * factor;
            
            float3 halfVector = normalize(-pointLightDirection + toEye);
            float NDotH = dot(normal, halfVector);
            float specularPow = pow(saturate(NDotH), gMaterial.shininess);
            float3 specular = pl.color.rgb * pl.intensity * specularPow * factor;
            
            totalDiffuse += diffuse;
            totalSpecular += specular;
        }

        // ===== SpotLights (複数対応) =====
        for (uint j = 0; j < gSpotLightGroup.activeCount; ++j)
        {
            SpotLight sl = gSpotLightGroup.lights[j];
            
            float3 spotLightDirectionOnSurface = normalize(input.worldPosition - sl.position);
            
            float spotDistance = length(sl.position - input.worldPosition);
            float spotAttenuationFactor = pow(saturate(-spotDistance / sl.distance + 1.0f), sl.decay);
            
            float spotCosAngle = dot(spotLightDirectionOnSurface, sl.direction);
            float spotFalloffFactor = saturate((spotCosAngle - sl.cosAngle) / (sl.cosFalloffStart - sl.cosAngle));
            
            float spotLightCos = saturate(dot(normal, -spotLightDirectionOnSurface));
            float3 diffuse = gMaterial.color.rgb * textureColor.rgb * sl.color.rgb 
                           * spotLightCos * sl.intensity 
                           * spotAttenuationFactor * spotFalloffFactor;
            
            float3 halfVector = normalize(-spotLightDirectionOnSurface + toEye);
            float NDotH = dot(normal, halfVector);
            float specularPow = pow(saturate(NDotH), gMaterial.shininess);
            float3 specular = sl.color.rgb * sl.intensity * specularPow
                            * spotAttenuationFactor * spotFalloffFactor;
            
            totalDiffuse += diffuse;
            totalSpecular += specular;
        }

        // 最終出力
        output.color.rgb = totalDiffuse + totalSpecular;
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
