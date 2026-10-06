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
    int useNormalMap;
    float cloudReflection;   // 鏡面反射に映す遠景の雲の強さ（0=映さない）
    // ディゾルブ（Dissolve.hlsli。C++ の Material.h と同じ並び）
    int dissolveEnable;
    float dissolveProgress;
    float dissolveEdgeWidth;
    float dissolveNoiseScale;
    float dissolveHeightMin;
    float dissolveHeightMax;
    float dissolveNoiseWeight;
    float dissolvePadding;
    float3 dissolveEdgeColor;
    float dissolvePadding2;
    // 視差オクルージョン（POM）。この PS だけが宣言する
    int useParallax;
    float parallaxDepth;      // 凹凸の深さ（UV 1 あたり）
    float parallaxMinLayers;  // 正面から見たときのレイマーチの段数
    float parallaxMaxLayers;  // 浅い角度から見たときの段数
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
TextureCube<float4> gEnvironmentTexture : register(t1);  // IBL 用スカイボックス
Texture2D<float4> gNormalMap : register(t2);
Texture2D<float4> gHeightMap : register(t4);   // 視差のハイトマップ（R。1=高い）
SamplerState gSampler : register(s0);

// ===== Shadow (CSM + PCSS) =====
#include "Shadow.hlsli"
#include "Fog.hlsli"
#include "Dissolve.hlsli"
#include "../Cloud/CloudSky.hlsli"  // b7 / t5 / s4。鏡面反射に遠景の雲を映す

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

// 視差オクルージョン（POM）。接空間の視線でハイトマップの中をレイマーチし、表面に当たった所の UV を返す。
// 視線が浅いほど段数を増やす。最後は前後の段の間を線形補間して段の縞を消す。
// ループの中は SampleGrad（元の UV の微分）で引く＝分岐やループで mip 選択が乱れない
float2 ParallaxOcclusionUV(float2 uv, float3 viewTS)
{
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);
    const float layers = lerp(gMaterial.parallaxMaxLayers, gMaterial.parallaxMinLayers, saturate(viewTS.z));
    const float layerDepth = 1.0f / layers;
    // 1 段ごとの UV のずれ。真横に近い視線で発散しないよう z の下限を置く
    const float2 shift = viewTS.xy / max(viewTS.z, 0.1f) * gMaterial.parallaxDepth;
    const float2 deltaUV = shift / layers;

    float2 currentUV = uv;
    float currentDepth = 0.0f;
    float mapDepth = 1.0f - gHeightMap.SampleGrad(gSampler, currentUV, dx, dy).r;
    [loop]
    for (int i = 0; i < 64 && currentDepth < mapDepth; ++i)
    {
        currentUV -= deltaUV;
        mapDepth = 1.0f - gHeightMap.SampleGrad(gSampler, currentUV, dx, dy).r;
        currentDepth += layerDepth;
    }

    // 当たった段と 1 つ手前の段の間で、表面と交わる位置を補間する
    const float2 prevUV = currentUV + deltaUV;
    const float after = mapDepth - currentDepth;
    const float before = (1.0f - gHeightMap.SampleGrad(gSampler, prevUV, dx, dy).r) - (currentDepth - layerDepth);
    const float w = after / (after - before + 1e-5f);
    return lerp(currentUV, prevUV, saturate(w));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 消える部分はここで捨てる（以降の計算を省く）
    float dissolveEdge = ApplyDissolve(input.worldPosition);

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    float3 N = normalize(input.normal);

    // 視差：ずらした UV でベースカラーと法線マップを引く（シルエットは変わらない）
    if (gMaterial.useParallax != 0)
    {
        float3 T = normalize(input.tangent);
        float3 B = normalize(input.bitangent);
        float3 viewTS = normalize(float3(dot(V, T), dot(V, B), dot(V, N)));
        transformedUV.xy = ParallaxOcclusionUV(transformedUV.xy, viewTS);
    }
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 法線マップで N をピクセル単位に差し替え（TBN でタンジェント空間→ワールド）
    if (gMaterial.useNormalMap != 0)
    {
        float3 normalTS = gNormalMap.Sample(gSampler, transformedUV.xy).xyz * 2.0f - 1.0f;
        float3 T = normalize(input.tangent);
        float3 B = normalize(input.bitangent);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(normalTS, TBN));
    }

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

    // ===== IBL（簡易：Skybox キューブマップを光源化）。影では暗くしない =====
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float NdotV = saturate(dot(N, V));
    float3 Fr = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kdIBL = (1.0f - Fr) * (1.0f - metallic); // 金属は拡散しない

    // mip 数を取得（prefiltered 環境マップの代用に roughness で mip を選ぶ）
    uint envW, envH, envMips;
    gEnvironmentTexture.GetDimensions(0, envW, envH, envMips);
    float maxMip = max((float) envMips - 1.0f, 0.0f);

    // 拡散 IBL：最も粗い mip（≒平均＝irradiance 近似）を N 方向でサンプル
    float3 irradiance = gEnvironmentTexture.SampleLevel(gSampler, N, maxMip).rgb;
    float3 diffuseIBL = kdIBL * albedo * irradiance;

    // 鏡面 IBL：反射ベクトルを roughness に応じた mip でサンプル（粗い面ほどボケた反射）
    float3 R = reflect(-V, N);
    float3 prefiltered = gEnvironmentTexture.SampleLevel(gSampler, R, roughness * maxMip).rgb;
    float3 specularIBL = prefiltered * Fr;

    float3 ambient = (diffuseIBL + specularIBL) * gMaterial.environmentCoefficient;

    // 遠景の雲の映り込み。空の映り込み（environmentCoefficient）とは別の強さで足す
    // （青空は弱く・雲は強く映して、金属が空の青でくすむのを避けられるように）。
    // 雲はキューブマップに入っていないので反射ベクトルで直接引く。粗い面ほど映り込みを弱める
    if (gMaterial.cloudReflection > 0.0f)
    {
        float4 cloud = CloudSky(input.worldPosition, R);
        float sharpness = saturate(1.0f - roughness);
        ambient += cloud.rgb * cloud.a * Fr * (gMaterial.cloudReflection * sharpness);
    }

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

    // 距離フォグ（最後に乗せる＝ライティング/IBL/シャドウの結果すべてに効かせる）。
    // アルファは触らない（ApplyFog 内で rgb のみ扱う）。
    output.color.rgb = ApplyDissolveEdge(output.color.rgb, dissolveEdge);
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition, gCamera.worldPosition);

    return output;
}
