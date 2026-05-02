// OutlineDepth.PS.hlsl
// Depthベースのアウトラインエフェクト（資料の方法）
// 深度バッファをPrewittフィルタで畳み込んでシルエットエッジを抽出する

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

cbuffer OutlineDepthParams : register(b0)
{
    float4x4 projectionInverse;
    float edgeStrength;
    float3 _padding;
};

// Prewittの横方向カーネル（3x3、合計0）
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

// 3x3カーネル分のオフセット（テクセル単位）
static const float2 kIndex3x3[3][3] =
{
    { float2(-1.0f, -1.0f), float2(0.0f, -1.0f), float2(1.0f, -1.0f) },
    { float2(-1.0f, 0.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f) },
    { float2(-1.0f, 1.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f) },
};

PixelShaderOutput main(VertexShaderOutput input)
{
    int width, height;
    gDepthTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float) width), rcp((float) height));

    // 縦横それぞれの畳み込み結果
    float2 difference = float2(0.0f, 0.0f);

    [unroll]
    for (int x = 0; x < 3; ++x)
    {
        [unroll]
        for (int y = 0; y < 3; ++y)
        {
            float2 texcoord = input.texcoord + kIndex3x3[x][y] * uvStepSize;

            // NDCのdepthを取得（pointサンプラーで補間させない）
            float ndcDepth = gDepthTexture.Sample(gSamplerPoint, texcoord);

            // NDC -> View空間のdepth(z)に変換（線形化）
            // 本当はviewSpace.z = mul(float4(0, 0, ndcDepth, 1.0f), projectionInverse).z / w
            float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), projectionInverse);
            float viewZ = viewSpace.z * rcp(viewSpace.w);

            difference.x += viewZ * kPrewittHorizontalKernel[x][y];
            difference.y += viewZ * kPrewittVerticalKernel[x][y];
        }
    }

    // 差の長さがそのままエッジの強さ
    float weight = length(difference);
    weight = saturate(weight * edgeStrength);

    // 元画像と合成（エッジ部分が暗くなる）
    PixelShaderOutput output;
    output.color.rgb = (1.0f - weight) * gTexture.Sample(gSamplerLinear, input.texcoord).rgb;
    output.color.a = 1.0f;
    return output;
}
