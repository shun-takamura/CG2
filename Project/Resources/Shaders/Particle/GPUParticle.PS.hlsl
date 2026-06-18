#include "Particle.hlsli"

// GPUParticle 専用 PS。旧 ParticleManager と共有の Particle.PS.hlsl は温存し、
// こちらにだけ「粒子ごとの寿命でディゾルブ」を実装する。

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
}

// ディゾルブ（粒子ごとの寿命比率ベース）。GPUParticleManager の DissolveParticle と一致させること。
struct DissolveParam
{
    int   enable;     // 0=無効 / 1=有効
    int   inEnable;   // 出現（材質化）0/1
    int   outEnable;  // 消滅（燃え尽き）0/1
    int   edgeEnable; // アウトライン 0/1
    float inEnd;      // 寿命比率: ここまでに出現完了（threshold 1→0）
    float outStart;   // 寿命比率: ここから消え始める（threshold 0→1）
    float edgeWidth;  // エッジの太さ（マスク値での幅）
    float pad0;
    float4 edgeColor; // エッジ発光色
};

cbuffer DissolveBuffer : register(b2)
{
    DissolveParam gDissolve;
}

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gDissolveMask : register(t1); // 無効時も white1x1 が必ずバインドされる
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    output.color = gMaterial.color * textureColor * input.color;

    if (gDissolve.enable != 0)
    {
        // 粒子の寿命比率(0..1)から、In(出現:1→0) と Out(消滅:0→1) を max 合成して閾値を作る。
        float lifeR = saturate(input.dissolveLife);
        float th = 0.0f;
        if (gDissolve.inEnable != 0 && gDissolve.inEnd > 0.0001f)
        {
            float inTh = (lifeR >= gDissolve.inEnd) ? 0.0f : (1.0f - lifeR / gDissolve.inEnd);
            th = max(th, inTh);
        }
        if (gDissolve.outEnable != 0 && gDissolve.outStart < 0.9999f)
        {
            float outTh = (lifeR <= gDissolve.outStart) ? 0.0f
                        : (lifeR - gDissolve.outStart) / (1.0f - gDissolve.outStart);
            th = max(th, outTh);
        }

        // マスクUVは粒子板の生UV（uvTransformは掛けない）
        float mask = gDissolveMask.Sample(gSampler, input.texcoord).r;
        if (mask < th)
        {
            discard;
        }
        // アウトライン：閾値のすぐ外側を発光色で塗る（進行中＝0<th<1のときのみ）
        if (gDissolve.edgeEnable != 0 && th > 0.0001f && th < 1.0f &&
            mask < th + gDissolve.edgeWidth)
        {
            output.color = gDissolve.edgeColor;
        }
    }

    if (output.color.a == 0.0)
    {
        discard;
    }

    return output;
}
