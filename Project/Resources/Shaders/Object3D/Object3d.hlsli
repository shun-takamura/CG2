struct VertexShaderOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION;
    float3 tangent : TANGENT0;     // ワールド空間の接線（法線マップ用）
    float3 bitangent : BINORMAL0;  // ワールド空間の従法線
};