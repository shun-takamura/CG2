// DisruptorReveal.PS.hlsl
// ディスラプター「崩壊リビール＋色反転」（2層リビール）。
// 断裂線への垂直距離でマスクし、内＝通常色（剥がれて下の世界が覗く）／外＝反転色（殻）。
// 境界はノイズでギザギザにし、近傍を不規則ブロックに分けてセルごとにバラバラのタイミングで崩す＝
// 「画面の絵（反転殻）がブロック状に砕けながら剥がれる」。revealT=0 で全画面反転、1 で全画面通常。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer DisruptorRevealParams : register(b0)
{
    float2 lineP0;        // 断裂線端点0（UV, 0..1）
    float2 lineP1;        // 断裂線端点1（UV, 0..1）
    float  revealT;       // リビール進捗 0..1
    float  intensity;     // 反転の強さ
    float  aspect;        // width/height
    float  cellSize;      // 崩壊ブロックの大きさ（aspect補正UV）
    float  chunkJitter;   // セルごとの崩壊タイミングのばらつき（大=飛び散る）
    float  edgeNoiseAmp;  // 境界のギザギザ振幅（細かい滑らかな揺れ）
    float  edgeNoiseFreq; // ギザギザの細かさ
    float  edgeDepth;     // 境界の歯の深さ（ブロック状に上下する量＝食い込み）
};

// ---- ハッシュ／ノイズ ----
float hash11(float n) { return frac(sin(n) * 43758.5453123f); }
float hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 34.56f);
    return frac(p.x * p.y);
}
float vnoise1(float x)
{
    float i = floor(x);
    float f = frac(x);
    float u = f * f * (3.0f - 2.0f * f);
    return lerp(hash11(i), hash11(i + 1.0f), u);
}
float vnoise2(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = hash21(i);
    float b = hash21(i + float2(1.0f, 0.0f));
    float c = hash21(i + float2(0.0f, 1.0f));
    float d = hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 color = gTexture.Sample(gSampler, input.texcoord);

    // アスペクト補正空間で線への距離を測る
    float2 p = float2(input.texcoord.x * aspect, input.texcoord.y);
    float2 a = float2(lineP0.x * aspect, lineP0.y);
    float2 b = float2(lineP1.x * aspect, lineP1.y);
    float2 ab = b - a;
    float  abLen = length(ab);
    float2 dir = (abLen > 1e-5f) ? (ab / abLen) : float2(1.0f, 0.0f);
    float2 nrm = float2(-dir.y, dir.x);

    // 断裂線から画面四隅までの最大垂直距離＝この線で全画面を覆い切るのに必要な距離。
    // これを基準にすると revealT=1 でちょうど全画面復帰、revealT が「戻った割合」に正しく対応する。
    float2 c0 = float2(0.0f, 0.0f);
    float2 c1 = float2(aspect, 0.0f);
    float2 c2 = float2(0.0f, 1.0f);
    float2 c3 = float2(aspect, 1.0f);
    float maxDist = max(max(abs(dot(c0 - a, nrm)), abs(dot(c1 - a, nrm))),
                        max(abs(dot(c2 - a, nrm)), abs(dot(c3 - a, nrm)))) * 1.02f;
    float front = revealT * maxDist;          // 現在の境界（線から）

    // 不規則ブロック：ドメインワープした格子の「セル単位」で殻/通常を決める。
    // セル中心の距離で一括判定するので、1セル=1つの塊（縁のはっきりした破片）になり、ザラつかない。
    float cs = max(cellSize, 1e-4f);
    // ドメインワープを2オクターブで強めにかけ、格子の四角さを崩して不規則な多角形チャンクにする。
    float2 warp = float2(vnoise2(p / cs * 0.8f + 11.3f), vnoise2(p / cs * 0.8f + 47.1f)) - 0.5f;
    warp += (float2(vnoise2(p / cs * 1.9f + 5.1f), vnoise2(p / cs * 1.9f + 9.7f)) - 0.5f) * 0.6f;
    float2 cell = floor((p + warp * cs * 1.15f) / cs);
    float2 cellCenter = (cell + 0.5f) * cs;

    float alongC = dot(cellCenter - a, dir);          // セル中心：線に沿った座標
    float distC  = abs(dot(cellCenter - a, nrm));      // セル中心：線への垂直距離

    // 境界の歯（深さ）：線に沿ってセル単位でランダムに上下＝ブロック状の食い込み。edgeDepth が深さ。
    float alongCellId = floor(alongC / cs);
    float teeth = (hash11(alongCellId * 1.7f + 3.3f) - 0.5f) * 2.0f * edgeDepth;
    // 細かい滑らかな揺れ
    float jag = (vnoise1(alongC * edgeNoiseFreq) - 0.5f) * 2.0f * edgeNoiseAmp;
    // セルごとの崩壊タイミングのばらつき。負の大きいセルは front を越えても殻のまま＝通常色領域に残る破片の島。
    float rnd = hash21(cell);
    float cellBias = (rnd - 0.5f) * 2.0f * chunkJitter;

    // revealT≈0（全画面反転で「ちゃんと見せる」段階）では境界を崩さない＝クリーンな反転を保つ。
    // peel が始まって revealT が増えるにつれて崩壊（ギザギザ＋ブロック）をランプインさせる。
    float crumbleGate = smoothstep(0.0f, 0.06f, revealT);
    float effFront = front + (jag + teeth + cellBias) * crumbleGate;
    // セル中心 distC < effFront → 通常色（剥がれた）／それ以外 → 反転色（殻）。セル全体が1つの塊。
    float shell = (distC < effFront) ? 0.0f : 1.0f;

    float3 inverted = 1.0f - color.rgb;
    float3 shellColor = lerp(color.rgb, inverted, intensity);
    output.color.rgb = lerp(color.rgb, shellColor, shell);
    output.color.a = color.a;
    return output;
}
