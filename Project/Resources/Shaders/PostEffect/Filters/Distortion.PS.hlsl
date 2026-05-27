// Distortion.PS.hlsl
// ディストーションRTに描画された歪みマップを読み取り、シーンのUVをオフセットする
// 深度バッファを参照して、サンプル先が「歪み源より手前にある不透明物体」の場合はオフセットを適用しない
// （手前オブジェクトが歪んで見えるのを防ぐ）

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture       : register(t0); // シーンテクスチャ
Texture2D<float4> gDistortionMap : register(t1); // ディストーションRT
Texture2D<float>  gDepthMap      : register(t2); // シーン深度バッファ
SamplerState      gSamplerLinear : register(s0);
SamplerState      gSamplerPoint  : register(s1);

cbuffer DistortionParams : register(b0)
{
    float strength;     // 歪みの強さ倍率
    float3 _padding;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float2 uv = input.texcoord;

    // ディストーションRTからオフセット情報を読み取る
    // R,G: UVオフセット方向（0.5が基準＝オフセットなし）
    // B  : (1 - NDC depth) of distortion source（手前ほど大きい）
    // A  : 強度（0で影響なし、1で最大）
    float4 distortionSample = gDistortionMap.Sample(gSamplerLinear, uv);

    float intensity = distortionSample.a;
    float2 offset = (distortionSample.rg - 0.5f) * 2.0f;
    offset *= intensity * strength;

    float2 distortedUV = clamp(uv + offset, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

    // ===== 深度比較：サンプル先が歪み源より手前なら、手前物体保護のため uv を元に戻す =====
    //   sourceNdcZ : 歪み源の NDC 深度
    //   sceneDepth : サンプル先（distortedUV）のシーン深度
    //   sceneDepth < sourceNdcZ なら、distortedUV のピクセルは歪み源より手前 → オフセット適用しない
    float sourceNdcZ  = 1.0f - distortionSample.b;
    float sceneDepth  = gDepthMap.SampleLevel(gSamplerPoint, distortedUV, 0.0f);
    // depth 差にゆとり（数値誤差や薄皮を許容）
    const float kEpsilon = 0.0005f;
    if (intensity > 0.0f && sceneDepth + kEpsilon < sourceNdcZ) {
        distortedUV = uv;
    }

    output.color = gTexture.Sample(gSamplerLinear, distortedUV);

    return output;
}
