// 1/4 解像度の雲（乗算済みの色＋被覆率）を拡大してシーンに重ねる。
// ブレンドは ONE / INV_SRC_ALPHA（CloudRaymarcher の合成 PSO）

#include "../PostEffect/Common/PostProcess.hlsli"

Texture2D<float4> gCloud : register(t0);
SamplerState gLinearClampSampler : register(s2);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    return gCloud.SampleLevel(gLinearClampSampler, input.texcoord, 0);
}
