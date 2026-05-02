// Smoothing.PS.hlsl
// BoxFilter（平均ぼかし）ピクセルシェーダー
// cbufferでカーネルサイズを動的に指定可能

#include "../Common/PostProcess.hlsli"

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

// ループ上限の半径（最大15x15まで対応）
// これ以上大きいカーネルが必要なら値を増やす
static const int kMaxRadius = 7;

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 1. uvStepSizeを算出
    int width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float) width), rcp((float) height));

    // カーネルの半径を算出（例: kernelSize=3 → radius=1, kernelSize=5 → radius=2）
    int radius = kernelSize / 2;

    // カーネル内の総ピクセル数（= kernelSize * kernelSize）
    float totalSamples = (float) (kernelSize * kernelSize);

    // 出力色の初期化
    float3 resultColor = float3(0.0f, 0.0f, 0.0f);

    // 2. カーネルサイズ分のループを回す
    // ループ回数はコンパイル時に確定（kMaxRadius）
    // 実際のカーネル範囲外はif文でスキップ
    for (int x = -kMaxRadius; x <= kMaxRadius; ++x)
    {
        for (int y = -kMaxRadius; y <= kMaxRadius; ++y)
        {
            // cbufferで指定されたradius範囲外はスキップ
            if (abs(x) > radius || abs(y) > radius)
            {
                continue;
            }

            // 3. 現在のtexcoordを算出
            float2 texcoord = input.texcoord + float2((float) x, (float) y) * uvStepSize;

            // 4. サンプリングして加算
            // サンプラーがClampモードなので端は自動的に処理される
            float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            resultColor += fetchColor;
        }
    }

    // 全ピクセルの平均を取る（BoxFilter: 均等な重み = 1/totalSamples）
    resultColor *= rcp(totalSamples);

    output.color = float4(resultColor, 1.0f);
    return output;
}
