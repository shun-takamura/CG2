#include "Object3d.hlsli"

// 地形（.mat v5 / shadingModel = 2）。設計書: Documents/Tasks/12_TerrainRendering.md §3
//   t0 = スプラットマップ（R = 草 / G = 土・砂 / B = 岩 / A = 濡れ。メッシュの UV で引く）
//   t2 = 地形全体の色合い（アルベドへの乗数 ÷ 2）。地形は法線マップを使わないので、法線マップの枠を流用している
//   t6 = 層の色の配列（A = 高さ）、t7 = 層の法線の配列。層の順番はスプラットの RGB と同じ
// 草と土はワールド XZ に投影して大小 2 スケールで引く。岩だけ三平面投影（崖が縦に引き伸びないように）。
// 法線は whiteout で幾何法線と合成する。XZ 投影なので接線 = ワールド X、従法線 = ワールド Z に固定でき、頂点の接線は使わない。
// ライティングは PBR（PBRCommon.hlsli）、影・フォグも Object3dPBR.PS と同じ。
// 層は異方性のサンプラ（s5）で引く。低空から地面を浅い角度で見るので、線形だと遠くがボケる。

cbuffer gTransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
};

// C++ の Material.h と同じ並び（地形の欄は末尾の float4 × 4）
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
    float4 terrainTile;        // xyz = 層ごとのタイル長 [m]、w = 高さブレンドの幅
    float4 terrainRoughness;   // xyz = 層ごとの粗さ、w = 岩の三平面投影の鋭さ
    float4 terrainParams;      // x = 低い周波数の色ムラの強さ、y = 濡れた所を暗くする強さ
    float4 terrainPadding;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

Texture2D<float4> gSplat : register(t0);
Texture2D<float4> gTint : register(t2);
Texture2DArray<float4> gLayerColor : register(t6);
Texture2DArray<float4> gLayerNormal : register(t7);
SamplerState gLayerSampler : register(s5);

#include "PBRCommon.hlsli"
#include "Shadow.hlsli"
#include "Fog.hlsli"

static const float kLayerGrass = 0.0f;
static const float kLayerDirt = 1.0f;
static const float kLayerRock = 2.0f;
// 2 つ目のスケール（大きい模様）。整数比にすると模様の継ぎ目が揃うので半端な値にする
static const float kFarScale = 0.23f;
static const float kFarWeight = 0.35f;
// 色ムラ（大きな明暗の斑）の 1 周期 [m]
static const float kMacroMeters = 170.0f;
// 凹みの陰（キャビティ AO）：層の高さが低い所（石の隙間・土の凹み）を暗くし、環境光の映り込みも抑える
static const float kCavity = 0.45f;
// マイクロシャドウ（Chan 2018）：凹み（cavity）の中では光が浅い角度ほど遮られる。法線マップだけでは出ない影の黒さを出す
static const float kMicroShadow = 1.0f;
// 色合いのマップ：近くは層の模様が主役なので弱め、遠く（層の模様がミップで平均される距離）ほど強く効かせる
static const float kTintNear = 0.6f;
static const float kTintFarMeters = 250.0f;
// 草の枯れ色：高い所と、大きな斑に応じて黄色っぽくする
static const float3 kDryTint = float3(1.28f, 1.12f, 0.62f);
static const float kDryHeightMin = 25.0f;
static const float kDryHeightMax = 110.0f;
static const float kDryStrength = 0.55f;

struct LayerSample
{
    float3 albedo;
    float height;
    float3 normal;   // ワールド空間
};

float3 UnpackNormal(float4 n)
{
    return n.xyz * 2.0f - 1.0f;
}

// 上向き（XZ）投影の層。大小 2 スケールを混ぜて繰り返しを目立たなくする
LayerSample SampleTopLayer(float3 worldPos, float3 geomN, float slice, float tile)
{
    const float2 uv = worldPos.xz / tile;
    const float2 uvFar = uv * kFarScale;
    const float4 c0 = gLayerColor.Sample(gLayerSampler, float3(uv, slice));
    const float4 c1 = gLayerColor.Sample(gLayerSampler, float3(uvFar, slice));
    const float3 n0 = UnpackNormal(gLayerNormal.Sample(gLayerSampler, float3(uv, slice)));
    const float3 n1 = UnpackNormal(gLayerNormal.Sample(gLayerSampler, float3(uvFar, slice)));

    LayerSample s;
    s.albedo = lerp(c0.rgb, c1.rgb, kFarWeight);
    s.height = lerp(c0.a, c1.a, kFarWeight);
    // 2 スケールの接空間法線を whiteout で重ねてから、幾何法線とも whiteout で合成する
    const float3 tn = float3(n0.xy + n1.xy * kFarWeight, n0.z * n1.z);
    s.normal = normalize(float3(tn.x + geomN.x, abs(tn.z) * geomN.y, tn.y + geomN.z));
    return s;
}

// 三平面投影の 1 面ぶん。大小 2 スケールを混ぜる（岩の塊が格子状に並んで見えないように）
void SamplePlane(float2 uv, float slice, out float4 color, out float3 normalTS)
{
    const float2 uvFar = uv * kFarScale;
    const float4 c0 = gLayerColor.Sample(gLayerSampler, float3(uv, slice));
    const float4 c1 = gLayerColor.Sample(gLayerSampler, float3(uvFar, slice));
    const float3 n0 = UnpackNormal(gLayerNormal.Sample(gLayerSampler, float3(uv, slice)));
    const float3 n1 = UnpackNormal(gLayerNormal.Sample(gLayerSampler, float3(uvFar, slice)));
    color = lerp(c0, c1, kFarWeight);
    normalTS = float3(n0.xy + n1.xy * kFarWeight, n0.z * n1.z);
}

// 三平面投影の層（岩）。Ben Golus の whiteout 三平面投影
LayerSample SampleTriplanarLayer(float3 worldPos, float3 geomN, float slice, float tile, float sharpness)
{
    float3 blend = pow(abs(geomN), sharpness);
    blend /= max(blend.x + blend.y + blend.z, 1e-5f);

    const float3 axisSign = sign(geomN);
    float2 uvX = worldPos.zy / tile;
    float2 uvY = worldPos.xz / tile;
    float2 uvZ = worldPos.xy / tile;
    // 裏側の面の投影は左右が反転するので、UV と法線の x を戻す
    uvX.x *= axisSign.x;
    uvY.x *= axisSign.y;
    uvZ.x *= -axisSign.z;

    float4 cX, cY, cZ;
    float3 tX, tY, tZ;
    SamplePlane(uvX, slice, cX, tX);
    SamplePlane(uvY, slice, cY, tY);
    SamplePlane(uvZ, slice, cZ, tZ);
    tX.x *= axisSign.x;
    tY.x *= axisSign.y;
    tZ.x *= -axisSign.z;

    tX = float3(tX.xy + geomN.zy, abs(tX.z) * geomN.x);
    tY = float3(tY.xy + geomN.xz, abs(tY.z) * geomN.y);
    tZ = float3(tZ.xy + geomN.xy, abs(tZ.z) * geomN.z);

    LayerSample s;
    s.albedo = cX.rgb * blend.x + cY.rgb * blend.y + cZ.rgb * blend.z;
    s.height = cX.a * blend.x + cY.a * blend.y + cZ.a * blend.z;
    s.normal = normalize(tX.zyx * blend.x + tY.xzy * blend.y + tZ.xyz * blend.z);
    return s;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    const float3 P = input.worldPosition;
    const float3 geomN = normalize(input.normal);
    const float3 V = normalize(gCamera.worldPosition - P);

    const float4 splat = gSplat.Sample(gSampler, input.texcoord);
    float3 w = splat.rgb / max(splat.r + splat.g + splat.b, 1e-4f);

    const float3 tile = gMaterial.terrainTile.xyz;
    LayerSample layers[3];
    layers[0] = SampleTopLayer(P, geomN, kLayerGrass, tile.x);
    layers[1] = SampleTopLayer(P, geomN, kLayerDirt, tile.y);
    layers[2] = SampleTriplanarLayer(P, geomN, kLayerRock, tile.z, gMaterial.terrainRoughness.w);

    // 大きな斑（色ムラ・枯れ色に共用）
    const float macro = gLayerColor.Sample(gLayerSampler, float3(P.xz / kMacroMeters, kLayerDirt)).a;

    // 草の枯れ色（高い所ほど、斑の明るい所ほど）
    const float dry = saturate((P.y - kDryHeightMin) / (kDryHeightMax - kDryHeightMin)) * 0.6f + macro * 0.4f;
    layers[0].albedo *= lerp(float3(1.0f, 1.0f, 1.0f), kDryTint, dry * kDryStrength);

    // 高さブレンド：重み＋層の高さが一番高い層から blendWidth の範囲だけを混ぜる。
    // 線形に混ぜると境界がぼやけるので、石の間から草が生えるような噛み合わせにする
    const float3 h = float3(layers[0].height, layers[1].height, layers[2].height);
    const float3 hw = h + w;
    const float blendWidth = max(gMaterial.terrainTile.w, 1e-3f);
    const float top = max(max(hw.x, hw.y), hw.z) - blendWidth;
    float3 b = max(hw - top, 0.0f) * saturate(w * 8.0f);   // スプラットが 0 の層は出さない
    b /= max(b.x + b.y + b.z, 1e-5f);

    float3 albedo = layers[0].albedo * b.x + layers[1].albedo * b.y + layers[2].albedo * b.z;
    float3 N = normalize(layers[0].normal * b.x + layers[1].normal * b.y + layers[2].normal * b.z);
    float roughness = dot(b, gMaterial.terrainRoughness.xyz);

    // 地形全体の色合い（尾根・日なたは黄色、谷・水辺は濃い緑、岩は赤茶と灰。gen_terrain.py の build_tint）
    const float3 tint = gTint.Sample(gSampler, input.texcoord).rgb * 2.0f;
    const float tintAmount = lerp(kTintNear, 1.0f, saturate(length(gCamera.worldPosition - P) / kTintFarMeters));
    albedo *= lerp(float3(1.0f, 1.0f, 1.0f), tint, tintAmount);

    // 低い周波数の色ムラ（土の層の高さを大きく引き伸ばして明暗の斑にする）
    albedo *= lerp(1.0f - gMaterial.terrainParams.x, 1.0f + gMaterial.terrainParams.x, macro);

    // 凹みの陰：混ぜた後の高さが低い所ほど暗く（拡散は弱め、環境光の映り込みは強めに抑える）
    const float cavity = lerp(1.0f - kCavity, 1.0f, saturate(dot(b, h)));
    albedo *= sqrt(cavity);

    // 水際は濡れて暗く、少し艶が出る
    const float wet = splat.a;
    albedo *= 1.0f - gMaterial.terrainParams.y * wet;
    roughness = lerp(roughness, roughness * 0.55f, wet);
    roughness = clamp(roughness, 0.04f, 1.0f);

    albedo *= gMaterial.color.rgb;

    // 影は幾何法線で引く（層の細かい法線だと影の境目がざらつく）
    const float shadow = CalcShadowFactor(P, geomN, input.position.xy);
    if (gShadowDebug > 0.5f)
    {
        output.color = float4(shadow, shadow, shadow, 1.0f);
        return output;
    }

    // 平行光源だけに掛ける（cavity = 1 なら影響なし）
    const float NdotL = abs(dot(N, normalize(-gDirectionalLight.direction)));
    const float microShadow = lerp(1.0f, saturate(NdotL + 2.0f * cavity * cavity - 1.0f), kMicroShadow);

    const float metallic = 0.0f;
    float3 Lo = PBRDirectLighting(N, V, P, albedo, metallic, roughness, shadow * microShadow);
    float3 R, Fr;
    float3 ambient = PBRAmbient(N, V, albedo, metallic, roughness, R, Fr) * gMaterial.environmentCoefficient * cavity;

    output.color.rgb = Lo + ambient;
    output.color.a = 1.0f;
    output.color.rgb = ApplyFog(output.color.rgb, P, gCamera.worldPosition);
    return output;
}
