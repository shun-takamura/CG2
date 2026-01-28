// Grayscale.PS.hlsl
// グレースケール専用のピクセルシェーダー（BT.709方式）

#include "PostProcess.hlsli"


Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 元の色をサンプリング
    output.color = gTexture.Sample(gSampler, input.texcoord);
    
    // BT.709方式でグレースケール変換
    // 人間の目は緑成分に対して明るさを感じやすいため、
    // RGBが同じ強さで発光している場合、緑成分に対して明るさを感じるように計算
    float value = dot(output.color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    
    // グレースケール値をRGBに代入
    output.color.rgb = float3(value, value, value);
    
    return output;
}
