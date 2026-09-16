// MaskedOutlineNormal.PS.hlsl
// OutlineNormal のマスク版。idMask（R8_UINT）が非0のオブジェクトにだけ、
// 深度＋深度から再構築した法線ベースのアウトラインを乗せる。
//   idMask == 1 -> colorFire（炎状態）
//   idMask == 2 -> colorIce （氷状態）
// blinkHz / minIntensity で点滅。
// 専用 root signature：color t0 + scene depth t1 + idMask t2 + cbuffer b0 + linear s0 + point s1

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture      : register(t0);
Texture2D<float>  gDepthTexture : register(t1);
Texture2D<uint>   gIdMask       : register(t2);
SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint  : register(s1);

cbuffer MaskedOutlineParams : register(b0)
{
    float4x4 projectionInverse;
    float4 colorFire;   // id == 1
    float4 colorIce;    // id == 2
    float depthWeight;
    float normalWeight;
    float depthThreshold;
    float normalThreshold;
    float edgeStrength;
    float gTime;
    float blinkHz;
    float minIntensity;
};

static const float kPI = 3.14159265f;

static const float kPrewittHorizontalKernel[3][3] =
{
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
};

static const float kPrewittVerticalKernel[3][3] =
{
    { -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
    { 0.0f, 0.0f, 0.0f },
    { 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f },
};

float3 ReconstructViewPos(float2 uv, float ndcDepth)
{
    float2 ndcXY = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewSpace = mul(float4(ndcXY, ndcDepth, 1.0f), projectionInverse);
    return viewSpace.xyz * rcp(viewSpace.w);
}

uint LoadId(int2 idx, int2 maxIdx)
{
    idx = clamp(idx, int2(0, 0), maxIdx);
    return gIdMask.Load(int3(idx, 0));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    int width, height;
    gDepthTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float) width), rcp((float) height));

    float3 sceneColor = gTexture.Sample(gSamplerLinear, input.texcoord).rgb;

    PixelShaderOutput output;
    output.color.a = 1.0f;

    // ---- IDマスク：中心と3x3近傍を読む ----
    uint2 maskSize;
    gIdMask.GetDimensions(maskSize.x, maskSize.y);
    int2 maxIdx = int2(maskSize) - int2(1, 1);
    int2 centerIdx = clamp(int2(input.texcoord * float2(maskSize)), int2(0, 0), maxIdx);

    uint idHere = LoadId(centerIdx, maxIdx);
    uint idRing = idHere;
    bool anyZero = (idHere == 0u);
    bool anyNonZero = (idHere != 0u);

    [unroll]
    for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ++ox)
        {
            uint s = LoadId(centerIdx + int2(ox, oy), maxIdx);
            idRing = max(idRing, s);
            anyZero = anyZero || (s == 0u);
            anyNonZero = anyNonZero || (s != 0u);
        }
    }

    // マスク領域が近傍に一切無ければ素の色
    if (idRing == 0u)
    {
        output.color.rgb = sceneColor;
        return output;
    }

    // ---- 深度/法線エッジ（OutlineNormal と同じ計算） ----
    float3 viewPos[5][5];
    [unroll]
    for (int xx = 0; xx < 5; ++xx)
    {
        [unroll]
        for (int yy = 0; yy < 5; ++yy)
        {
            float2 offset = float2((float) (xx - 2), (float) (yy - 2));
            float2 uv = input.texcoord + offset * uvStepSize;
            float ndcDepth = gDepthTexture.Sample(gSamplerPoint, uv);
            viewPos[xx][yy] = ReconstructViewPos(uv, ndcDepth);
        }
    }

    float3 normals[3][3];
    [unroll]
    for (int nx = 0; nx < 3; ++nx)
    {
        [unroll]
        for (int ny = 0; ny < 3; ++ny)
        {
            int sx = nx + 1;
            int sy = ny + 1;
            float3 dX = viewPos[sx + 1][sy] - viewPos[sx - 1][sy];
            float3 dY = viewPos[sx][sy + 1] - viewPos[sx][sy - 1];
            normals[nx][ny] = normalize(cross(dX, dY));
        }
    }

    float2 depthDiff = float2(0.0f, 0.0f);
    float3 normalDiffH = float3(0.0f, 0.0f, 0.0f);
    float3 normalDiffV = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int kx = 0; kx < 3; ++kx)
    {
        [unroll]
        for (int ky = 0; ky < 3; ++ky)
        {
            float kH = kPrewittHorizontalKernel[kx][ky];
            float kV = kPrewittVerticalKernel[kx][ky];
            float viewZ = viewPos[kx + 1][ky + 1].z;
            depthDiff.x += viewZ * kH;
            depthDiff.y += viewZ * kV;
            float3 n = normals[kx][ky];
            normalDiffH += n * kH;
            normalDiffV += n * kV;
        }
    }

    float depthEdge = saturate((length(depthDiff) - depthThreshold) * depthWeight);
    float normalEdge = saturate((length(normalDiffH) + length(normalDiffV) - normalThreshold) * normalWeight);
    float creaseEdge = max(depthEdge, normalEdge);

    // ---- アウトライン合成 ----
    // シルエット：マスク境界（内側1px と外側1px）
    float silhouette = 0.0f;
    if (idHere != 0u && anyZero) silhouette = 1.0f;        // 内側リム
    if (idHere == 0u && anyNonZero) silhouette = 1.0f;      // 外側リム

    // 折れ目：マスク上のピクセルだけ
    float inside = (idHere != 0u) ? saturate(creaseEdge * edgeStrength) : 0.0f;

    float edgeAmt = saturate(max(silhouette, inside));

    // 点滅
    float wave = 0.5f + 0.5f * sin(gTime * blinkHz * 2.0f * kPI);
    float blink = lerp(minIntensity, 1.0f, wave);

    float4 outlineColor = (idRing == 1u) ? colorFire : colorIce;
    float a = edgeAmt * blink * outlineColor.a;

    output.color.rgb = lerp(sceneColor, outlineColor.rgb, saturate(a));
    return output;
}
