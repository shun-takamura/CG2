// MaskedGrayscale.PS.hlsl
// IDマスク（R8_UINT）が 0 のピクセルだけグレースケール化、非0 のピクセルは色を維持する。
// outline 用 root signature（color t0 + aux t1 + cbuffer b0 + linear s0 + point s1）を共用。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture  : register(t0);
Texture2D<uint>   gIdMask   : register(t1);
SamplerState      gSampler  : register(s0);
SamplerState      gPoint    : register(s1);

cbuffer MaskedGrayscaleParams : register(b0)
{
    float intensity; // 0 = 効果なし、1 = マスク外を完全グレースケール
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 color = gTexture.Sample(gSampler, input.texcoord);

    // IDマスクは Texture2D<uint> なので Load で読む（ピクセル単位）
    uint2 size;
    gIdMask.GetDimensions(size.x, size.y);
    int2 idx = int2(input.texcoord * float2(size));
    idx = clamp(idx, int2(0, 0), int2(size) - int2(1, 1));
    uint maskId = gIdMask.Load(int3(idx, 0));

    // mask == 0 → グレースケール、非0 → 元色を維持
    float gray = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    float3 grayscale = float3(gray, gray, gray);

    // mask が 0 のとき intensity 分グレースケール、非0 のときは色そのまま
    float w = (maskId == 0u) ? intensity : 0.0f;
    output.color.rgb = lerp(color.rgb, grayscale, w);
    output.color.a = color.a;

    return output;
}
