// OutlineNormal.PS.hlsl
// Depth + Normal-from-depth 併用のアウトラインエフェクト
// シルエットエッジは深度のPrewitt、折れ目エッジは深度から再構築した法線のPrewittで検出

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

cbuffer OutlineNormalParams : register(b0)
{
    float4x4 projectionInverse;
    float depthWeight;
    float normalWeight;
    float depthThreshold;
    float normalThreshold;
};

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

// NDCのz・スクリーンuvからview空間位置を復元する
float3 ReconstructViewPos(float2 uv, float ndcDepth)
{
    // uv (0..1, y下向き) -> NDC xy (-1..1, y上向き)
    float2 ndcXY = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewSpace = mul(float4(ndcXY, ndcDepth, 1.0f), projectionInverse);
    return viewSpace.xyz * rcp(viewSpace.w);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    int width, height;
    gDepthTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float) width), rcp((float) height));

    // 5x5の深度を読み、view空間位置を復元する
    // 中央3x3セルの法線をそれぞれ中心差分で計算するため、外側1ピクセル分余分にサンプルする
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

    // 内側3x3セルそれぞれで、view空間法線を中心差分で計算する
    float3 normals[3][3];
    [unroll]
    for (int x = 0; x < 3; ++x)
    {
        [unroll]
        for (int y = 0; y < 3; ++y)
        {
            int sx = x + 1; // viewPos上のインデックス（中央が[2][2]）
            int sy = y + 1;
            float3 dX = viewPos[sx + 1][sy] - viewPos[sx - 1][sy];
            float3 dY = viewPos[sx][sy + 1] - viewPos[sx][sy - 1];
            normals[x][y] = normalize(cross(dX, dY));
        }
    }

    // depth差分（シルエット検出用）
    float2 depthDiff = float2(0.0f, 0.0f);
    // normal差分（折れ目検出用、各成分ごとに畳み込み）
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

    // 深度エッジ
    float depthEdge = length(depthDiff);
    depthEdge = saturate((depthEdge - depthThreshold) * depthWeight);

    // 法線エッジ（成分ごとに畳み込んだ結果のベクトル長を採用）
    float normalEdge = length(normalDiffH) + length(normalDiffV);
    normalEdge = saturate((normalEdge - normalThreshold) * normalWeight);

    // 両者をmaxで合成（どちらかが強ければエッジ）
    float weight = max(depthEdge, normalEdge);

    PixelShaderOutput output;
    output.color.rgb = (1.0f - weight) * gTexture.Sample(gSamplerLinear, input.texcoord).rgb;
    output.color.a = 1.0f;
    return output;
}
