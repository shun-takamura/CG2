#include "Skybox.hlsli"

struct TransformationMatrix{
    float4x4 WVP;
    float4x4 World;
};

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix transformationMatrix;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
};

VertexShaderOutput main( VertexShaderInput input )
{
	VertexShaderOutput output;
    output.position = mul(input.position, transformationMatrix.WVP).xyww;
    output.texcoord = input.position.xyz;
	return output;
}