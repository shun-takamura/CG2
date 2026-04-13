// Smoothing.PS.hlsl
// BoxFilter（平均ぼかし）ピクセルシェーダー
// cbufferでカーネルサイズを動的に指定可能

#include "PostProcess.hlsli"

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// Smoothing専用の定数バッファ
cbuffer SmoothingParams : register(b0)
{
    int kernelSize; // カーネルサイズ（3, 5, 7, 9 など奇数を想定）
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 1. uvStepSizeを算出
    // GetDimensionsでテクスチャの幅と高さを取得し、
    // rcp（逆数）で1ピクセル分のUV移動量を求める
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float)width), rcp((float)height));

    // カーネルの半径を算出（例: kernelSize=3 → radius=1, kernelSize=5 → radius=2）
    int radius = kernelSize / 2;

    // カーネル内の総ピクセル数（= kernelSize * kernelSize）
    float totalSamples = (float)(kernelSize * kernelSize);

    // 出力色の初期化
    float3 resultColor = float3(0.0f, 0.0f, 0.0f);

    // 2. カーネルサイズ分のループを回す
    for (int x = -radius; x <= radius; ++x)
    {
        for (int y = -radius; y <= radius; ++y)
        {
            // 3. 現在のtexcoordを算出
            // 中心ピクセルからのオフセットをUV空間に変換して加算
            float2 texcoord = input.texcoord + float2((float)x, (float)y) * uvStepSize;

            // 4. サンプリングして均等に加算
            // サンプラーがClampモードなので、端のピクセルは自動的に
            // 端の値がそのまま続くものとして扱われる（資料の方法2）
            float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            resultColor += fetchColor;
        }
    }

    // 全ピクセルの平均を取る（BoxFilter: 全て均等な重み = 1/totalSamples）
    resultColor *= rcp(totalSamples);

    output.color = float4(resultColor, 1.0f);
    return output;
}
