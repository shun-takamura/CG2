// DisruptorShard.VS.hlsl
// 崩壊の破片（反転世界の殻のかけら）。シーンキャプチャの一部を貼った 3D キューブを描く。
// per-shard cbuffer: WVP（world×VP）＋サンプリングする画面UV矩形（uvMin/uvSize）＋α。

struct ShardCB
{
    float4x4 wvp;
    float2   uvMin;   // 破片が剥がれた画面位置（UV 左上）
    float2   uvSize;  // UV のサイズ（破片の画面フットプリント）
    float    alpha;
    float    distortAmount;
    float    distortFreq;
    float    _pad;
};
ConstantBuffer<ShardCB> gShard : register(b0);

struct VSInput
{
    float3 position : POSITION0;
    float2 uv       : TEXCOORD0;
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
    // キューブのローカル UV(0..1) を、剥がれた画面領域 [uvMin, uvMin+uvSize] にマップ。
    // これで破片は「剥がれた場所の世界の絵」を持ったまま動く。
    output.uv = gShard.uvMin + input.uv * gShard.uvSize;
    return output;
}
