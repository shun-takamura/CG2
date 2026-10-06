// WriteIDDissolve.PS.hlsl
// ID Pass のディゾルブ対応版：ディゾルブで消えている部分には ID を書かない。
// （WriteID.PS は形全体に ID を書くので、実体化途中の物にアウトラインを掛けると
//   まだ出ていない部分の背景の深度まで線として拾ってしまう）
// ルート定数は WriteID と同じ b0。先頭の objectId の後ろにディゾルブの値が並ぶ（Object3DInstance::DrawIdPass）。

#include "Object3d.hlsli"
#define DISSOLVE_NO_MATERIAL
#include "Dissolve.hlsli"

cbuffer IdDissolveCB : register(b0)
{
    uint gObjectId;
    float gDissolveProgress;
    float gDissolveEdgeWidth;
    float gDissolveNoiseScale;
    float gDissolveHeightMin;
    float gDissolveHeightMax;
    float gDissolveNoiseWeight;
    float gPadding;
};

struct PSOutput
{
    uint id : SV_Target0;
};

PSOutput main(VertexShaderOutput input)
{
    DissolveClip(input.worldPosition, gDissolveProgress, gDissolveEdgeWidth,
        gDissolveHeightMin, gDissolveHeightMax, gDissolveNoiseScale, gDissolveNoiseWeight);

    PSOutput output;
    output.id = gObjectId;
    return output;
}
