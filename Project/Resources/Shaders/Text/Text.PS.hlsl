// SDF (Signed Distance Field) 方式のテキスト描画。
// アトラスは onedge_value=128 / pixel_dist_scale=32 で焼かれている（[0..1] 正規化後 0.5 が縁）。
// fwidth でスクリーン空間の SDF 勾配を取り、smoothstep で AA される鋭利な輪郭を生成する。
// 拡大しても鋭利、縮小してもエッジが綺麗。アウトラインは 1 サンプルで同心円的に描ける。

Texture2D<float>   gAtlas   : register(t0);
SamplerState       gSampler : register(s0);

struct PSInput
{
    float4 position     : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float4 color        : COLOR0;
    float4 outlineColor : COLOR1;
    float  outlineWidth : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET0
{
    // [0,1] サンプルから縁(0.5)基準の signed distance に
    float sd = gAtlas.Sample(gSampler, input.uv) - 0.5;

    // スクリーン 1px 分の SDF 変化量。AA 幅と outline の換算に使う
    float pxDist = max(fwidth(sd), 1e-6);
    float aa = pxDist * 0.5;

    // 本体マスク：sd > 0 (内側) で 1
    float bodyAlpha = smoothstep(-aa, aa, sd);

    // アウトライン無効パス
    if (input.outlineWidth <= 0.0001 || input.outlineColor.a <= 0.0001)
    {
        float a = bodyAlpha * input.color.a;
        if (a <= 0.001) discard;
        return float4(input.color.rgb, a);
    }

    // アウトライン込みパス：outlineWidth 画素ぶん外側まで「外側のエッジ」を移動
    float outlineEdge = -input.outlineWidth * pxDist;
    float outlineAlpha = smoothstep(outlineEdge - aa, outlineEdge + aa, sd);

    float3 col = lerp(input.outlineColor.rgb, input.color.rgb, bodyAlpha);
    float a = outlineAlpha * lerp(input.outlineColor.a, input.color.a, bodyAlpha);
    if (a <= 0.001) discard;
    return float4(col, a);
}
