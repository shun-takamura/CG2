// DisruptorReveal.PS.hlsl
// ディスラプター「崩壊リビール＋色反転」（2層リビール）。
// 断裂線（スクリーン UV の2端点）への垂直距離でマスクし、
//   距離 < revealHalfWidth(t)  → 通常色（殻が剥がれて下の世界が覗いた領域）
//   それ以外                   → 反転色（まだ残る殻）
// revealT=0 で全画面反転（仕様「全画面反転＝reveal t=0」）、revealT=1 で全画面通常。
// Collapse 中は World 停止なのでライブ＝キャプチャ同一フレーム＝この1パスで2層が成立する。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer DisruptorRevealParams : register(b0)
{
    float2 lineP0;    // 断裂線端点0（UV, 0..1）
    float2 lineP1;    // 断裂線端点1（UV, 0..1）
    float  revealT;   // リビール進捗 0..1（0=全画面反転 / 1=全画面通常）
    float  intensity; // 反転の強さ（1=完全反転）
    float  aspect;    // width/height（UV の縦横比補正）
    float  _pad;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 color = gTexture.Sample(gSampler, input.texcoord);

    // アスペクト補正空間（x を aspect 倍）でスクリーン上の実距離を測る
    float2 p = float2(input.texcoord.x * aspect, input.texcoord.y);
    float2 a = float2(lineP0.x * aspect, lineP0.y);
    float2 b = float2(lineP1.x * aspect, lineP1.y);

    float2 ab = b - a;
    float  abLen = length(ab);
    // 線が縮退していたら横一閃（水平線）にフォールバック
    float2 n = (abLen > 1e-5f) ? normalize(float2(-ab.y, ab.x)) : float2(0.0f, 1.0f);

    // 断裂線への垂直距離（上下対称に伝播するので絶対値）
    float dist = abs(dot(p - a, n));

    // 画面全体を覆い切れる最大距離（対角を少し超える）
    float maxDist = sqrt(aspect * aspect + 1.0f) * 1.1f;
    float halfW = revealT * maxDist;

    // shell=0: 通常色（剥がれた） / shell=1: 反転色（殻が残る）。境界はパリッと 0/1。
    // 破片は反転世界側を反転色で別途描くので、ここに中間（ソフト）帯は設けない。
    float shell = step(halfW, dist);

    float3 inverted = 1.0f - color.rgb;
    float3 shellColor = lerp(color.rgb, inverted, intensity);

    output.color.rgb = lerp(color.rgb, shellColor, shell);
    output.color.a = color.a;

    return output;
}
