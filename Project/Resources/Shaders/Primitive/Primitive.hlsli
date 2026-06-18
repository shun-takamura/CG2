// 頂点入力（InputLayoutと対応）
struct VertexInput
{
    float3 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

// 頂点出力（VS→PS）
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
    float3 worldPos : TEXCOORD1;
};

// 変換行列（VSのb0）
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

// マテリアル（PSのb0）
struct Material
{
    float4 color;
    int enableLighting;
    float alphaReference;
    int samplerMode;       // 0=WrapAll / 1=WrapU+ClampV / 2=ClampAll
    float padding;
    float4x4 uvTransform;
    float3 cameraPos;
    float viewAngleFadePower; // 0=無効
    // ディゾルブ（オブジェクト単位）。mask.r < threshold のピクセルを discard する。
    int    dissolveEnable;     // 0=無効 / 1=有効
    float  dissolveThreshold;  // 0..1（時間で 0→1 に上げると暗い部分から消えていく）
    int    dissolveEdgeEnable; // アウトライン（燃えるエッジ）0=無効 / 1=有効
    float  dissolveEdgeWidth;  // エッジの太さ（マスク値での幅）
    float4 dissolveEdgeColor;  // エッジの発光色
};
