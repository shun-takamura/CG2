#include "Object3d.hlsli"

// PBR（Cook-Torrance, メタリック/ラフネス方式）。環境マップ反射は入れない（IBL はフェーズ3）。
// 影は平行光源の直接光のみに掛ける（Object3d.PS と同じ取り決め）。

// 最大ライト数（C++側と合わせる）
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 8

static const float PI = 3.14159265f;

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
    float environmentCoefficient;
    int useEnvironmentMap;
    float metallic;
    float roughness;
    int shadingModel;
    float2 padding2;
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

// ===== Shadow (CSM + PCSS) =====
#include "Shadow.hlsli"

// ===== Cook-Torrance BRDF =====

// 法線分布関数 GGX（ハイライトの広がり）
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-6f);
}

// 幾何減衰（Smith, 直接光用 k）
float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(N, V)), roughness)
         * GeometrySchlickGGX(saturate(dot(N, L)), roughness);
}

// フレネル（Schlick 近似）
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// 1ライト分の寄与（radiance = 光色 * 強度 * 減衰）
float3 PBRLight(float3 N, float3 V, float3 L, float3 radiance,
                float3 albedo, float metallic, float roughness)
{
    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

    float3 numerator = D * G * F;
    float denom = 4.0f * saturate(dot(N, V)) * saturate(dot(N, L)) + 1e-4f;
    float3 specular = numerator / denom;

    // 金属は拡散しない。kd は反射しなかった分の拡散寄与
    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kd * albedo / PI;

    float NdotL = saturate(dot(N, L));
    return (diffuse + specular) * radiance * NdotL;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);

    float3 albedo = gMaterial.color.rgb * textureColor.rgb;
    float metallic = saturate(gMaterial.metallic);
    float roughness = clamp(gMaterial.roughness, 0.04f, 1.0f);

    // 平行光源の影係数（平行光源の直接光のみに掛ける）
    float shadow = CalcShadowFactor(input.worldPosition, N, input.position.xy);

    // デバッグ：影係数をそのままグレースケール表示
    if (gShadowDebug > 0.5f)
    {
        output.color = float4(shadow, shadow, shadow, 1.0f);
        return output;
    }

    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // ===== DirectionalLight（影あり）=====
    {
        float3 L = normalize(-gDirectionalLight.direction);
        float3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
    }

    // ===== PointLights（影なし）=====
    for (uint i = 0; i < gPointLightGroup.activeCount; ++i)
    {
        PointLight pl = gPointLightGroup.lights[i];
        float dist = length(pl.position - input.worldPosition);
        float attenuation = pow(saturate(-dist / pl.radius + 1.0f), pl.decay);
        float3 L = normalize(pl.position - input.worldPosition);
        float3 radiance = pl.color.rgb * pl.intensity * attenuation;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness);
    }

    // ===== SpotLights（影なし）=====
    for (uint j = 0; j < gSpotLightGroup.activeCount; ++j)
    {
        SpotLight sl = gSpotLightGroup.lights[j];
        float3 dirOnSurface = normalize(input.worldPosition - sl.position);
        float dist = length(sl.position - input.worldPosition);
        float attenuation = pow(saturate(-dist / sl.distance + 1.0f), sl.decay);
        float cosAngle = dot(dirOnSurface, sl.direction);
        float falloff = saturate((cosAngle - sl.cosAngle) / (sl.cosFalloffStart - sl.cosAngle));
        float3 L = -dirOnSurface;
        float3 radiance = sl.color.rgb * sl.intensity * attenuation * falloff;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness);
    }

    // 仮の環境光（IBL はフェーズ3で置換）。影では暗くしない。
    float3 ambient = albedo * 0.03f;

    output.color.rgb = Lo + ambient;
    output.color.a = gMaterial.color.a * textureColor.a;

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
