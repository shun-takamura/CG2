// 羽根の色：テクスチャ（白い羽根）× 両面の陰影 ＋ 裏から透ける光。ディゾルブはノイズのしきい値で欠けさせ、縁を光らせる。

#include "Feather.hlsli"

// 羽根のテクスチャ 5 種類（ヒープ上で並んでいないので 1 枚ずつ貼る）
Texture2D<float4> gFeather0 : register(t1);
Texture2D<float4> gFeather1 : register(t2);
Texture2D<float4> gFeather2 : register(t3);
Texture2D<float4> gFeather3 : register(t4);
Texture2D<float4> gFeather4 : register(t5);
Texture2D<float2> gNoise : register(t6);   // cloud_noise.dds（R = 形の fBm）
SamplerState gLinearClampSampler : register(s0);
SamplerState gWrapSampler : register(s1);

float4 SampleFeather(uint index, float2 uv)
{
    switch (index)
    {
    case 0: return gFeather0.Sample(gLinearClampSampler, uv);
    case 1: return gFeather1.Sample(gLinearClampSampler, uv);
    case 2: return gFeather2.Sample(gLinearClampSampler, uv);
    case 3: return gFeather3.Sample(gLinearClampSampler, uv);
    default: return gFeather4.Sample(gLinearClampSampler, uv);
    }
}

float4 main(FeatherVSOutput input) : SV_TARGET
{
    const FeatherInstance feather = gInstances[input.instance];
    const float4 tex = SampleFeather((uint)feather.extra.x, input.uv);
    const float alpha = tex.a * feather.shape.z;
    clip(alpha - 0.01f);

    // ディゾルブ：ノイズがしきい値より小さい所から欠ける。しきい値の手前 edgeWidth を縁として光らせる
    float glow = 0.0f;
    const float dissolve = feather.shape.w;
    if (dissolve > 0.0f)
    {
        const float n = gNoise.SampleLevel(gWrapSampler, input.uv * gNoiseScale + feather.extra.z, 0).r;
        const float threshold = dissolve * (1.0f + gEdgeWidth);
        clip(n - (threshold - gEdgeWidth));
        glow = 1.0f - smoothstep(threshold - gEdgeWidth, threshold, n);
    }

    // 両面：カメラを向いている側の法線で照らす
    float3 normal = normalize(input.normal);
    const float3 toCamera = normalize(gCameraPosition - input.worldPosition);
    if (dot(normal, toCamera) < 0.0f)
    {
        normal = -normal;
    }
    const float lit = dot(normal, -gLightDirection);
    const float diffuse = gAmbient + (1.0f - gAmbient) * saturate(lit);
    // 羽根は薄いので、裏から光を受けると透けて明るく見える
    const float translucent = gTranslucency * saturate(-lit);

    float3 color = tex.rgb * (diffuse + translucent) * feather.extra.y;
    color = lerp(color, gEdgeColor, glow);
    return float4(color, alpha);
}
