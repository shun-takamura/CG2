#include "Primitive.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
// ディゾルブ用マスク（グレースケール）。無効時も white1x1 が必ずバインドされる。
Texture2D<float4> gDissolveMask : register(t1);

// 3つのサンプラー（material.samplerMode で選択）
//   s0: WRAP U  / WRAP V
//   s1: WRAP U  / CLAMP V  （Ring/Cylinderの中心アーティファクト回避）
//   s2: CLAMP U / CLAMP V
SamplerState gSamplerWrap        : register(s0);
SamplerState gSamplerWrapU_ClampV: register(s1);
SamplerState gSamplerClamp       : register(s2);

struct PixelOutput
{
    float4 color : SV_TARGET0;
};

PixelOutput main(VertexOutput input)
{
    PixelOutput output;

    // UV変換を適用
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // サンプラー選択（uniform 分岐なのでGPUコストは小さい）
    float4 texColor;
    if (gMaterial.samplerMode == 1)
    {
        texColor = gTexture.Sample(gSamplerWrapU_ClampV, transformedUV.xy);
    }
    else if (gMaterial.samplerMode == 2)
    {
        texColor = gTexture.Sample(gSamplerClamp, transformedUV.xy);
    }
    else
    {
        texColor = gTexture.Sample(gSamplerWrap, transformedUV.xy);
    }

    // 最終色 = テクスチャ × 頂点カラー × マテリアル色
    output.color = texColor * input.color * gMaterial.color;

    // ビュー角度フェード（power>0 のときのみ有効）
    // 面法線がカメラ方向と平行=正面に近い=明るく、垂直=エッジオン=暗く
    if (gMaterial.viewAngleFadePower > 0.0001f)
    {
        float3 viewDir = normalize(gMaterial.cameraPos - input.worldPos);
        float ndotv = saturate(abs(dot(viewDir, normalize(input.normal))));
        float fade = pow(ndotv, gMaterial.viewAngleFadePower);
        output.color.a *= fade;
    }

    // alphaReferenceを使ったdiscard
    if (output.color.a <= gMaterial.alphaReference)
    {
        discard;
    }

    // ディゾルブ：マスク値が閾値未満のピクセルを破棄（オブジェクト単位で時間経過とともに消える）
    // マスクUVは本体UVと独立させたいので uvTransform を掛けず生の texcoord を使う。
    if (gMaterial.dissolveEnable != 0)
    {
        float mask = gDissolveMask.Sample(gSamplerWrap, input.texcoord).r;
        if (mask < gMaterial.dissolveThreshold)
        {
            discard;
        }
        // アウトライン：閾値のすぐ外側の帯を発光色で塗る（ディゾルブ進行中のみ＝0<threshold<1）。
        if (gMaterial.dissolveEdgeEnable != 0 &&
            gMaterial.dissolveThreshold > 0.0001f &&
            gMaterial.dissolveThreshold < 1.0f &&
            mask < gMaterial.dissolveThreshold + gMaterial.dissolveEdgeWidth)
        {
            output.color = gMaterial.dissolveEdgeColor;
        }
    }

    return output;
}
