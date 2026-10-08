#include "Object3d.hlsli"

// PBR（Cook-Torrance, メタリック/ラフネス方式）。環境マップ反射は入れない（IBL はフェーズ3）。
// 影は平行光源の直接光のみに掛ける（Object3d.PS と同じ取り決め）。

cbuffer gTransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
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

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gNormalMap : register(t2);
Texture2D<float4> gHeightMap : register(t4);   // 視差のハイトマップ（R。1=高い）

// ライト・カメラ（b1〜b4）、環境マップ（t1）、s0、BRDF の式、全ライトと IBL の寄与
#include "PBRCommon.hlsli"

// ===== Shadow (CSM + PCSS) =====
#include "Shadow.hlsli"
#include "Fog.hlsli"
#include "Dissolve.hlsli"
#include "../Cloud/CloudSky.hlsli"  // b7 / t5 / s4。鏡面反射に遠景の雲を映す

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

    float3 Lo = PBRDirectLighting(N, V, input.worldPosition, albedo, metallic, roughness, shadow);

    // IBL（影では暗くしない）
    float3 R, Fr;
    float3 ambient = PBRAmbient(N, V, albedo, metallic, roughness, R, Fr) * gMaterial.environmentCoefficient;

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
