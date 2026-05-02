// DistortionMesh.PS.hlsl
// ディストーションRTに描画するための専用ピクセルシェーダー
// テクスチャのRGを歪み方向、Aを強度として出力する
// 頂点シェーダーは既存のPrimitive.VS.hlslを共用

#include "Primitive.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelOutput
{
    float4 color : SV_TARGET0;
};

PixelOutput main(VertexOutput input)
{
    PixelOutput output;

    // UV変換を適用
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // テクスチャサンプリング
    float4 texColor = gTexture.Sample(gSampler, transformedUV.xy);

    // RG: 歪み方向（テクスチャのRGをそのまま使用）
    // A: 強度（テクスチャのA × 頂点カラーのA × マテリアルのA）
    float alpha = texColor.a * input.color.a * gMaterial.color.a;

    // alphaReferenceを使ったdiscard
    if (alpha <= gMaterial.alphaReference)
    {
        discard;
    }

    output.color = float4(texColor.rg, 0.0f, alpha);

    return output;
}
