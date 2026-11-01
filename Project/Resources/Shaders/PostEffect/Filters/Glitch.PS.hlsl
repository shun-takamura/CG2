// Glitch.PS.hlsl
// 空間がずれたようなノイズ。斜めの帯ごとに帯の向きへずらし、R と B を逆へずらして紫・緑の色ずれを出す。
// 強いときは四角いブロック単位でもずらす。ずれ方は seed（jitterInterval 秒ごとに変わる）で決まる。
// どこに掛けるかは IDマスク（R8_UINT）で選ぶ：0=画面全体 / 1=マスクの中 / 2=マスクの外。
// outline 用 root signature（color t0 + aux t1 + cbuffer b0 + linear s0 + point s1）を共用。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<uint>   gIdMask  : register(t1);
SamplerState      gSampler : register(s0);
SamplerState      gPoint   : register(s1);

cbuffer GlitchParams : register(b0)
{
    float intensity;     // 0..1
    float seed;          // ずれ方の切り替え用の整数
    int   maskMode;      // 0=画面全体 / 1=マスクの中 / 2=マスクの外
    float aspect;        // 幅 / 高さ
    float bandSlope;     // 帯の傾き（縦横比を直した後の dy/dx）
    float bandCount;     // 対角あたりの帯の数
    float bandChance;    // 強さ 1 のときずれる帯の割合
    float maxShift;      // 帯のずれの最大（画面の幅に対する割合）
    float rgbSplit;      // 色ずれの量（画面の幅に対する割合）
    float blockSize;     // ブロックの一辺 [px]
    float blockChance;   // 強さ 1 のときずれるブロックの割合
    float tintStrength;  // ずれた所を紫・緑に染める強さ
};

float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

uint MaskAt(float2 uv, uint2 size)
{
    int2 idx = int2(saturate(uv) * float2(size));
    idx = clamp(idx, int2(0, 0), int2(size) - int2(1, 1));
    return gIdMask.Load(int3(idx, 0));
}

// 縦横比を直した空間（横が aspect）での長さ d を、方向 dir の uv のずれに直す
float2 ToUV(float2 dir, float d)
{
    return float2(dir.x * d, dir.y * d * aspect);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float2 uv = input.texcoord;
    float4 original = gTexture.Sample(gSampler, uv);
    output.color = original;
    if (intensity <= 0.0f) return output;

    uint2 size;
    gIdMask.GetDimensions(size.x, size.y);

    // 帯：縦横比を直した座標を帯に垂直な向きへ測って番号を付ける。細い帯と太い帯を混ぜて幅をまちまちにする
    float2 p = float2(uv.x * aspect, uv.y);
    float2 dir = normalize(float2(1.0f, bandSlope));
    float2 nrm = float2(-dir.y, dir.x);
    float across = dot(p, nrm) / sqrt(aspect * aspect + 1.0f);
    float bandFine = floor(across * bandCount * 2.3f);
    float bandWide = floor(across * bandCount * 0.7f + Hash21(float2(seed, 3.1f)));
    float chance = bandChance * intensity;
    float rFine = Hash21(float2(bandFine, seed));
    float rWide = Hash21(float2(bandWide + 91.7f, seed * 1.37f));
    bool fineActive = rFine < chance * 0.6f;
    bool wideActive = rWide < chance * 0.5f;
    bool bandActive = fineActive || wideActive;
    float rShift = fineActive ? Hash21(float2(bandFine + 17.3f, seed)) : Hash21(float2(bandWide + 5.9f, seed));
    float shift = bandActive ? (rShift * 2.0f - 1.0f) * maxShift * intensity : 0.0f;
    float2 offset = ToUV(dir, shift * aspect);

    // ブロック：四角いマスごとに横へずらす
    float2 cell = floor(uv * float2(size) / blockSize);
    bool blockActive = Hash21(cell + seed * 0.731f) < blockChance * intensity;
    if (blockActive) {
        offset.x += (Hash21(cell * 1.91f + seed) * 2.0f - 1.0f) * maxShift * intensity;
    }

    float2 src = uv + offset;

    // 掛ける所：マスクの中なら「中の画素」か「中から持ってくる画素」。外なら機体を引きずらないよう両方とも外のときだけ
    uint mDst = MaskAt(uv, size);
    uint mSrc = MaskAt(src, size);
    bool apply = true;
    if (maskMode == 1) apply = (mDst != 0u) || (mSrc != 0u);
    else if (maskMode == 2) apply = (mDst == 0u) && (mSrc == 0u);
    if (!apply) return output;

    // 色ずれ：帯の向きに R と B を逆へ。ずれた所は強く、ほかは弱く
    bool moved = bandActive || blockActive;
    float split = rgbSplit * intensity * (moved ? 1.0f : 0.35f);
    float2 splitUV = ToUV(dir, split * aspect);
    float3 color;
    color.r = gTexture.Sample(gSampler, src + splitUV).r;
    color.g = gTexture.Sample(gSampler, src).g;
    color.b = gTexture.Sample(gSampler, src - splitUV).b;

    // ずれた所を紫か緑に染める
    if (moved) {
        float3 tint = (rShift > 0.5f) ? float3(0.85f, 0.3f, 1.0f) : float3(0.3f, 1.0f, 0.5f);
        color = lerp(color, color * tint * 1.6f + tint * 0.06f, tintStrength * intensity);
    }

    output.color = float4(color, original.a);
    return output;
}
