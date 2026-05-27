// DistortionMesh.PS.hlsl
// プリミティブを「歪み源」として DistortionRT に描画する PS
// ノーマルマップ風RGテクスチャを読み、RGを歪み方向、Aを強度として出力する
// 頂点シェーダーは既存の Primitive.VS.hlsl を共用

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

    // UV変換を適用（distortion 専用 uvTransform — メインテクスチャとは独立）
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // ノーマルマップサンプリング
    float4 texColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 強度：テクスチャα × 頂点カラーα × Material.color.a（per-instance distortionStrength）
    // ※ ここで使う Material は distortion 専用 CB なので、通常描画の Material.color.a とは独立。
    float alpha = texColor.a * input.color.a * gMaterial.color.a;

    // 強度が極小ならピクセル破棄（無駄なブレンドを避ける）
    if (alpha <= gMaterial.alphaReference)
    {
        discard;
    }

    // RG: ノーマルマップのRGをそのまま（0.5 中心、±0.5 で方向）
    // B : この歪み源ピクセルの NDC 深度 (0..1)。合成時に手前オブジェクトとの depth 比較に使う。
    //     ※ MAX 合成なので、奥のものほど大きい値になる → 奥同士が重なった時は「より手前」が勝つよう、
    //        1.0 - z を書く（手前ほど大きい）。合成側ではこれを比較に使うので符号を統一する。
    // A : 強度
    output.color = float4(texColor.rg, 1.0f - input.position.z, alpha);

    return output;
}
