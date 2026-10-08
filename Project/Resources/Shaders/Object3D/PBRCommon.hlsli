// PBR（Cook-Torrance, メタリック/ラフネス方式）の共通部分。Object3dPBR.PS と Terrain.PS が include する。
// ライト・カメラの CB（b1〜b4）、環境マップ（t1）と通常サンプラ（s0）、BRDF の式、全ライトと IBL の寄与。
// Material（b0）はシェーダごとに宣言が違うので、ここには置かない。

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

struct Camera
{
    float3 worldPosition;
};

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

TextureCube<float4> gEnvironmentTexture : register(t1);  // IBL 用スカイボックス
SamplerState gSampler : register(s0);

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

// roughness を考慮したフレネル（IBL の環境光用）。粗い面ほどグレージング反射を弱める
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float oneMinusR = 1.0f - roughness;
    float3 r = max(float3(oneMinusR, oneMinusR, oneMinusR), F0);
    return F0 + (r - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
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

// 平行光源（影あり）・点光源・スポットライト（影なし）の直接光の合計。影は平行光源の直接光のみに掛ける
float3 PBRDirectLighting(float3 N, float3 V, float3 worldPosition,
                         float3 albedo, float metallic, float roughness, float shadow)
{
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    {
        float3 L = normalize(-gDirectionalLight.direction);
        float3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
    }

    for (uint i = 0; i < gPointLightGroup.activeCount; ++i)
    {
        PointLight pl = gPointLightGroup.lights[i];
        float dist = length(pl.position - worldPosition);
        float attenuation = pow(saturate(-dist / pl.radius + 1.0f), pl.decay);
        float3 L = normalize(pl.position - worldPosition);
        float3 radiance = pl.color.rgb * pl.intensity * attenuation;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness);
    }

    for (uint j = 0; j < gSpotLightGroup.activeCount; ++j)
    {
        SpotLight sl = gSpotLightGroup.lights[j];
        float3 dirOnSurface = normalize(worldPosition - sl.position);
        float dist = length(sl.position - worldPosition);
        float attenuation = pow(saturate(-dist / sl.distance + 1.0f), sl.decay);
        float cosAngle = dot(dirOnSurface, sl.direction);
        float falloff = saturate((cosAngle - sl.cosAngle) / (sl.cosFalloffStart - sl.cosAngle));
        float3 L = -dirOnSurface;
        float3 radiance = sl.color.rgb * sl.intensity * attenuation * falloff;
        Lo += PBRLight(N, V, L, radiance, albedo, metallic, roughness);
    }
    return Lo;
}

// IBL（簡易：Skybox キューブマップを光源化）。影では暗くしない。
// 雲の映り込みなどを足す呼び出し側のために、反射ベクトル R と環境光のフレネル Fr も返す
float3 PBRAmbient(float3 N, float3 V, float3 albedo, float metallic, float roughness,
                  out float3 R, out float3 Fr)
{
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float NdotV = saturate(dot(N, V));
    Fr = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kdIBL = (1.0f - Fr) * (1.0f - metallic); // 金属は拡散しない

    // mip 数を取得（prefiltered 環境マップの代用に roughness で mip を選ぶ）
    uint envW, envH, envMips;
    gEnvironmentTexture.GetDimensions(0, envW, envH, envMips);
    float maxMip = max((float) envMips - 1.0f, 0.0f);

    // 拡散 IBL：最も粗い mip（≒平均＝irradiance 近似）を N 方向でサンプル
    float3 irradiance = gEnvironmentTexture.SampleLevel(gSampler, N, maxMip).rgb;
    float3 diffuseIBL = kdIBL * albedo * irradiance;

    // 鏡面 IBL：反射ベクトルを roughness に応じた mip でサンプル（粗い面ほどボケた反射）
    R = reflect(-V, N);
    float3 prefiltered = gEnvironmentTexture.SampleLevel(gSampler, R, roughness * maxMip).rgb;
    float3 specularIBL = prefiltered * Fr;

    return diffuseIBL + specularIBL;
}
