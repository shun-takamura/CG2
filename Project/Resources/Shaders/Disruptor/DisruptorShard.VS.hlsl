// DisruptorShard.VS.hlsl
// 崩壊の破片（反転世界の殻のかけら）。事前分割セルの三角形を、重心ローカル座標で受け取り、
// per-cell の WVP（重心回転＋飛散移動×VP）でワールドへ飛ばす。UV は baked スクリーンUV をそのまま流す。

struct ShardCB
{
    float4x4 wvp;
    float    alpha;
    float    satBoost;
    float2   pad;
};
ConstantBuffer<ShardCB> gShard : register(b0);

struct VSInput
{
    float3 position : POSITION0; // 重心ローカル座標（world 単位）
    float2 uv       : TEXCOORD0; // baked スクリーンUV（剥がれた画面位置）
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), gShard.wvp);
    output.uv = input.uv;
    return output;
}
