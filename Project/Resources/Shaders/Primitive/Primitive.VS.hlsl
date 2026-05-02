#include "Primitive.hlsli"

ConstantBuffer<TransformationMatrix> gTransform : register(b0);

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), gTransform.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gTransform.World));
    output.color = input.color;
    return output;
}