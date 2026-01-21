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
    float radius;
    float decay;
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

cbuffer SpotLightBuffer : register(b4)
{
    SpotLight gSpotLight;
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
        // 逆二乗の法則による減衰係数の計算を行う
        float distance = length(gPointLight.position - input.worldPosition);
        float factor = pow(saturate(-distance / gPointLight.radius + 1.0), gPointLight.decay);
        
        // PointLight
        float3 pointLightDirection = normalize( input.worldPosition - gPointLight.position);
        
        // PointLight用の拡散反射係数 (法線とライト方向の内積)
        float pointLightCos = saturate(dot(normal, -pointLightDirection));
       
        // PointLightの拡散反射
        float3 diffusePointLight = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * pointLightCos * gPointLight.intensity*factor;
        
        // PointLightの鏡面反射
        float3 pointLightHalfVector = normalize(-pointLightDirection + toEye);
        float pointLightNDotH = dot(normal, pointLightHalfVector);
        float pointLightSpecularPow = pow(saturate(pointLightNDotH), gMaterial.shininess);
        float3 specularPointLight = gPointLight.color.rgb * gPointLight.intensity * pointLightSpecularPow*factor;
        
        // SpotLight
        // 入射光の方向を計算
        float3 spotLightDirectionOnSurface = normalize(input.worldPosition - gSpotLight.position);
        
        // 距離による減衰
        float spotDistance = length(gSpotLight.position - input.worldPosition);
        float spotAttenuationFactor = pow(saturate(-spotDistance / gSpotLight.distance + 1.0), gSpotLight.decay);
        
        // Falloffの計算
        float spotCosAngle = dot(spotLightDirectionOnSurface, gSpotLight.direction);
        float spotFalloffFactor = saturate((spotCosAngle - gSpotLight.cosAngle) / (gSpotLight.cosFalloffStart - gSpotLight.cosAngle));
        
        // SpotLightの拡散反射
        float spotLightCos = saturate(dot(normal, -spotLightDirectionOnSurface));
        float3 diffuseSpotLight = gMaterial.color.rgb * textureColor.rgb * gSpotLight.color.rgb 
                                  * spotLightCos * gSpotLight.intensity 
                                  * spotAttenuationFactor * spotFalloffFactor;
        
        // SpotLightの鏡面反射
        float3 spotLightHalfVector = normalize(-spotLightDirectionOnSurface + toEye);
        float spotLightNDotH = dot(normal, spotLightHalfVector);
        float spotLightSpecularPow = pow(saturate(spotLightNDotH), gMaterial.shininess);
        float3 specularSpotLight = gSpotLight.color.rgb * gSpotLight.intensity * spotLightSpecularPow
                                   * spotAttenuationFactor * spotFalloffFactor;
        
        // 最終出力
        // 最終出力
        output.color.rgb = diffuseDirectionalLight + specularDirectionalLight
                         + diffusePointLight + specularPointLight
                         + diffuseSpotLight + specularSpotLight;
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
