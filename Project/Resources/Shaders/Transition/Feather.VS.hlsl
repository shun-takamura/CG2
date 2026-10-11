// 羽根の板を曲げて置く。UV の格子 1 枚を全インスタンスで使い回す。
// 板のローカル座標：x = 横（-0.5〜0.5）、y = 羽軸（下が根元 -0.5、上が先 0.5）、z = 反りで膨らむ向き。
//   z = bend · y² + cup · x²   → 法線は (-2·cup·x, -2·bend·y, 1) の向き

#include "Feather.hlsli"

struct VSInput
{
    float2 uv : TEXCOORD0;
};

FeatherVSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    const FeatherInstance feather = gInstances[instanceId];
    const float bend = feather.shape.x;
    const float cup = feather.shape.y;

    // テクスチャの v は下向きなので、上（先）を +y にする
    const float x = input.uv.x - 0.5f;
    const float y = 0.5f - input.uv.y;
    const float3 local = float3(x, y, bend * y * y + cup * x * x);
    const float3 localNormal = normalize(float3(-2.0f * cup * x, -2.0f * bend * y, 1.0f));

    const float4 world = mul(float4(local, 1.0f), feather.world);

    FeatherVSOutput output;
    output.position = mul(world, gViewProjection);
    output.uv = input.uv;
    // 大きさは縦横同じなので、回転だけを掛ければ法線の向きが保たれる
    output.normal = normalize(mul(localNormal, (float3x3)feather.world));
    output.worldPosition = world.xyz;
    output.instance = instanceId;
    return output;
}
