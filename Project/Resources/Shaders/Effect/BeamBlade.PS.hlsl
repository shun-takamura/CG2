// BeamBlade.PS.hlsl
// ビームの刃（BeamBladeRenderer.cpp）。実体に近い安定した光の刃：揺らぎ・ノイズは入れない。
//   面   ：ddx/ddy の法線で角ばらせ、斜めから見た面ほど明るく（輪郭が立つ）
//   縁   ：幅の縁と稜線を明るく、不透明に近く
// 出力は乗算済み α（本体）／発光 RT へは光る分だけ（bloomPass）。

cbuffer BeamBladeParams : register(b0)
{
    float3 gCoreColor;
    float gIntensity;

    float3 gEdgeColor;
    float gBodyAlpha;

    float3 gCameraPosition;
    float gEdgeAlpha;

    float gEdgeSharpness;
    float gFresnelPower;
    float gFresnelStrength;
    float gBloomScale;

    int gBloomPass;
    float3 gPadding0;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float edge : TEXCOORD1;
    float ridge : TEXCOORD2;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    float3 n = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    float3 v = normalize(gCameraPosition - input.worldPos);
    float ndv = abs(dot(n, v));
    float fresnel = pow(saturate(1.0f - ndv), gFresnelPower) * gFresnelStrength;

    float glow = saturate(max(pow(input.edge, gEdgeSharpness), pow(input.ridge, gEdgeSharpness) * 0.6f));
    float3 color = lerp(gCoreColor, gEdgeColor, glow) * gIntensity * (0.75f + 0.25f * ndv + fresnel);
    float alpha = saturate(lerp(gBodyAlpha, gEdgeAlpha, glow) + fresnel * 0.5f);

    if (gBloomPass != 0)
    {
        return float4(color * alpha * gBloomScale, 0.0f);
    }
    return float4(color * alpha, alpha);
}
