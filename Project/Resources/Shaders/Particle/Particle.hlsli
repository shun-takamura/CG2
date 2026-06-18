struct VertexShaderOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
    float dissolveLife : TEXCOORD1; // 粒子の寿命比率(0..1)。GPUParticle専用PSのディゾルブで使用
};