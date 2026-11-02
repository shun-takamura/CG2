// BeamBlade.VS.hlsl
// ビームの刃（BeamBladeRenderer.cpp）。頂点は形の係数だけ（w＝幅の向き, t＝厚みの向き, station＝0 根元 / 1 肩 / 2 先）で、
// 寸法はここで作る。刃は根元（原点）から +Z へ伸び、幅は Y、厚みは X。extend で根元から先へ伸び縮みする。
// 稜線（t≠0 の頂点）の高さは ridgeOffset、細くなるときの寄せ方は backAnchor（1＝上の縁を真っすぐに保つ＝片刃の峰）。

cbuffer BeamBladeTransform : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
    float gLength;
    float gHalfWidth;
    float gHalfThickness;
    float gBaseWidth;
    float gTipLength;
    float gTipOffset;
    float gExtend;
    float gRidgeOffset;
    float gBackAnchor;
    float3 gPadding0;
};

// 幅の係数 w（稜線は ridgeOffset）と幅の倍率 f から Y。f＜1 で細くなるときは backAnchor で上の縁へ寄せる
float SectionY(float w, float t, float f)
{
    float k = (abs(t) > 0.5f) ? gRidgeOffset : w;
    float center = gBackAnchor * gHalfWidth * (1.0f - f);
    return center + k * gHalfWidth * f;
}

struct VertexShaderInput
{
    float3 shape : POSITION0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float edge : TEXCOORD1;  // 1＝幅の縁
    float ridge : TEXCOORD2; // 1＝稜線
};

VertexShaderOutput main(VertexShaderInput input)
{
    float w = input.shape.x;
    float t = input.shape.y;
    float station = input.shape.z;

    float len = gLength * gExtend;
    float3 local;
    if (station < 0.5f)
    {
        local = float3(t * gHalfThickness, SectionY(w, t, gBaseWidth), 0.0f);
    }
    else if (station < 1.5f)
    {
        local = float3(t * gHalfThickness, SectionY(w, t, 1.0f), len * (1.0f - gTipLength));
    }
    else
    {
        local = float3(0.0f, gTipOffset * gHalfWidth, len);
    }

    VertexShaderOutput output;
    output.position = mul(float4(local, 1.0f), gWVP);
    output.worldPos = mul(float4(local, 1.0f), gWorld).xyz;
    output.edge = abs(w);
    output.ridge = abs(t);
    return output;
}
