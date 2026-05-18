// WriteID.PS.hlsl
// ID Pass 用：ハイライト対象オブジェクトの可視ピクセルに objectId を書き込む。
// 出力先 RT は R8_UINT 単体。
// 入力は SV_Position だけに絞ることで、Object3D / Animated / Primitive など
// 任意の VS と互換性を持たせる（PS 入力は VS 出力の部分集合で OK）。

struct PSInput
{
    float4 position : SV_Position;
};

struct PSOutput
{
    uint id : SV_Target0;
};

// ルート定数（4 byte）。SetGraphicsRoot32BitConstant で uint を渡す。
cbuffer IdCB : register(b0)
{
    uint gObjectId;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    output.id = gObjectId;
    return output;
}
