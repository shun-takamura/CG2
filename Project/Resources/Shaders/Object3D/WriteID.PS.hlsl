// WriteID.PS.hlsl
// ID Pass 用：ハイライト対象オブジェクトの可視ピクセルに objectId を書き込む。
// 出力先 RT は R8_UINT 単体。

#include "Object3d.hlsli"

struct PSOutput
{
    uint id : SV_Target0;
};

// ルート定数（4 byte）。SetGraphicsRoot32BitConstant で uint を渡す。
cbuffer IdCB : register(b0)
{
    uint gObjectId;
};

PSOutput main(VertexShaderOutput input)
{
    PSOutput output;
    output.id = gObjectId;
    return output;
}
