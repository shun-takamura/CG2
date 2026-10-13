// WriteEmissive.PS.hlsl
// 発光パス用：ブルームの発光 RT（float）に「光る分」だけを書く（Object3DInstance::DrawBloomPass）。
// 光る分 = マテリアルの発光（emissive。w=1 ならテクスチャを掛ける）＋ディゾルブの発光帯。どちらも bloomScale 倍する。
// 深度はメイン描画で書いたものに LessEqual でテストする（隠れている所は光らない）。
#include "Object3d.hlsli"

// C++ の Material.h と同じ並び（このシェーダはディゾルブと発光しか読まない）
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
    float cloudReflection;
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
    int useParallax;
    float parallaxDepth;
    float parallaxMinLayers;
    float parallaxMaxLayers;
    float4 terrainTile;
    float4 terrainRoughness;
    float4 terrainParams;
    float4 emissive;
};

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

// ルート定数：x = 発光 RT に書くときの倍率（メイン描画の発光より強くして光の広がりを出す）
cbuffer BloomCB : register(b1)
{
    float gBloomScale;
    float3 gBloomPadding;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

#include "Dissolve.hlsli"

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float edge = ApplyDissolve(input.worldPosition, input.position.xy);
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    // emissive.w = 1 ならテクスチャの色と透明度を掛ける（文字を描いた板の、文字だけを光らせる）
    float3 emissive = gMaterial.emissive.rgb * lerp(float3(1.0f, 1.0f, 1.0f), textureColor.rgb * textureColor.a, gMaterial.emissive.w);
    float3 glow = emissive + gMaterial.dissolveEdgeColor * edge;
    return float4(glow * gBloomScale, 1.0f);
}
