// Monochrome.PS.hlsl
// 墨絵のような白黒の演出（ボスの必殺技など）。
//   1. 輝度を知覚上の明るさ（0〜1）にして、コントラストで明暗を強調する（強めのグレースケール）
//   2. ノイズで揺らしたしきい値で白と黒に分ける（柔らかさ 0 でくっきり 2 値、1 でグレーのまま）
//   3. 深度から取った輪郭を黒い線として重ねる（物の縁＝深度の差、面の折れ目＝1/深度 の 2 階差分）
//   4. 反転の量で白黒を入れ替え、白黒の量で元の色と混ぜる
// RT は sRGB なので、入力は線形の色。明るさの計算は知覚上の値で行い、書き出す前に線形へ戻す。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

cbuffer MonochromeParams : register(b0)
{
    float monoAmount;    // 0 = 元の色, 1 = 白黒
    float contrast;      // 明暗の強調（1 = そのまま）
    float midpoint;      // コントラストの中心の明るさ（知覚上 0〜1）
    float threshold;     // 白と黒を分けるしきい値（知覚上 0〜1。下げるほど白が多い）
    float softness;      // 0 = くっきり 2 値, 1 = グレーのまま
    float noiseAmount;   // しきい値をノイズで揺らす量
    float noiseScale;    // ノイズの細かさ（画面の高さあたりの数）
    float noiseSpeed;    // ノイズが動く速さ [1/s]
    float edgeStrength;  // 輪郭の黒い線の濃さ（0〜1）
    float edgeThreshold; // 輪郭とみなす深度の差（相対値）
    float edgeWidth;     // 輪郭の線の太さ [px]
    float invert;        // 反転の量（0〜1）
    float time;          // 経過時間 [s]（ノイズを動かす）
    float depthA;        // 射影行列の m[2][2]（深度から視空間の距離を戻す）
    float depthB;        // 射影行列の m[3][2]
    float padding;
};

// BT.709 の輝度の重み
static const float3 kLuminanceWeight = float3(0.2125f, 0.7154f, 0.0721f);
// 線形 ⇔ 知覚上の明るさの変換に使うガンマ
static const float kGamma = 2.2f;
// 2 値の境目のぼかし幅（ジャギーを出さない最小限）
static const float kBinaryEdge = 0.01f;
// これ以上の深度は空（何も描かれていない）とみなす
static const float kSkyDepth = 0.99999f;
// 空と物の境目を輪郭にするときの、物の側の深度の差（相対値）
static const float kSkyEdgeDifference = 1.0f;
// 面の折れ目（2 階差分）を、物の縁と同じ尺度にそろえる倍率
static const float kCreaseWeight = 4.0f;
// 輪郭のしきい値から、線が濃くなりきるまでの幅（しきい値に対する倍率）
static const float kEdgeRampScale = 1.0f;
// ノイズの 2 段目の細かさ・重み
static const float kNoiseOctaveScale = 2.03f;
static const float kNoiseOctaveWeight = 0.5f;
// 2 段のノイズの重みの合計（-1〜1 に戻す）
static const float kNoiseTotalWeight = 1.5f;
// 2 段目のノイズを 1 段目とずらすための位置
static const float2 kNoiseOctaveOffset = float2(17.3f, 41.7f);

// 2 次元の位置から 0〜1 の疑似乱数を作る
float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// 値ノイズ（0〜1）。マスの角の乱数を滑らかに補間する
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 2 段重ねたノイズ（-1〜1）。墨のにじみのような境目を作る
float InkNoise(float2 p)
{
    float n = ValueNoise(p) + ValueNoise(p * kNoiseOctaveScale + kNoiseOctaveOffset) * kNoiseOctaveWeight;
    return n / kNoiseTotalWeight * 2.0f - 1.0f;
}

// NDC の深度から視空間の距離を戻す
float LinearDepth(float ndcDepth)
{
    return depthB / (ndcDepth - depthA);
}

// 深度から輪郭の濃さ（0〜1）を出す。中心と上下左右・斜めの 9 点を読む
float DepthEdge(float2 uv, float2 texel)
{
    float centerNdc = gDepthTexture.SampleLevel(gSamplerPoint, uv, 0.0f);
    // 中心が空なら線は引かない（空の側には縁を作らない）
    if (centerNdc >= kSkyDepth) {
        return 0.0f;
    }
    float centerZ = LinearDepth(centerNdc);

    float silhouette = 0.0f;
    float invZ[3][3];
    [unroll]
    for (int x = 0; x < 3; ++x) {
        [unroll]
        for (int y = 0; y < 3; ++y) {
            float2 offset = float2((float) (x - 1), (float) (y - 1)) * texel * edgeWidth;
            float ndc = gDepthTexture.SampleLevel(gSamplerPoint, uv + offset, 0.0f);
            if (ndc >= kSkyDepth) {
                // 隣が空なら物の縁
                silhouette = max(silhouette, kSkyEdgeDifference);
                invZ[x][y] = 1.0f / centerZ;
                continue;
            }
            float z = LinearDepth(ndc);
            // 物の縁：手前の物の側だけに線が出るよう、隣が奥にあるときだけ数える
            silhouette = max(silhouette, (z - centerZ) / centerZ);
            invZ[x][y] = 1.0f / z;
        }
    }

    // 面の折れ目：平面の上では 1/深度 が画面上でまっすぐ変わるので、2 階差分は 0 になる
    float centerInvZ = invZ[1][1];
    float laplacian = abs(invZ[0][1] + invZ[2][1] - 2.0f * centerInvZ)
                    + abs(invZ[1][0] + invZ[1][2] - 2.0f * centerInvZ);
    float crease = laplacian / centerInvZ * kCreaseWeight;

    float metric = max(silhouette, crease);
    float rampWidth = max(edgeThreshold * kEdgeRampScale, kBinaryEdge);
    return smoothstep(edgeThreshold, edgeThreshold + rampWidth, metric);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 color = gTexture.Sample(gSamplerLinear, input.texcoord);

    // 1. 知覚上の明るさにして、コントラストで明暗を強調する
    float luminance = dot(color.rgb, kLuminanceWeight);
    float perceptual = pow(saturate(luminance), 1.0f / kGamma);
    float gray = saturate((perceptual - midpoint) * contrast + midpoint);

    // 2. ノイズで揺らしたしきい値で白と黒に分ける
    int width, height;
    gTexture.GetDimensions(width, height);
    float aspect = (float) width / (float) height;
    float2 noiseUv = float2(input.texcoord.x * aspect, input.texcoord.y) * noiseScale;
    float noise = InkNoise(noiseUv + float2(time * noiseSpeed, -time * noiseSpeed * 0.5f));
    float jitteredThreshold = threshold + noise * noiseAmount;
    float binary = smoothstep(jitteredThreshold - kBinaryEdge, jitteredThreshold + kBinaryEdge, gray);
    float value = lerp(binary, gray, saturate(softness));

    // 3. 輪郭を黒い線として重ねる（線を引かないときは深度を読まない）
    if (edgeStrength > 0.0f) {
        float2 texel = float2(1.0f / (float) width, 1.0f / (float) height);
        float edge = DepthEdge(input.texcoord, texel);
        value *= 1.0f - edge * saturate(edgeStrength);
    }

    // 4. 反転して、元の色と混ぜる
    value = lerp(value, 1.0f - value, saturate(invert));
    float linearValue = pow(value, kGamma);
    output.color.rgb = lerp(color.rgb, linearValue.xxx, saturate(monoAmount));
    output.color.a = color.a;
    return output;
}
