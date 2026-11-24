// ボロノイで割った岩（FracturedMeshRenderer）の VS / PS で共有する定義。
// レジスタ: b0 フレーム / b1 平行光源（エンジンと同じ並び）/ b3 描画ごとのルート定数 / b6 フォグ（Fog.hlsli）
//          t0 岩・破片ごとの行列 / t1〜t4 外側と断面のテクスチャ / s0 異方性・繰り返し

struct InstanceData
{
    float4x4 world;   // 割る前の岩の空間 → ワールド（破片は回転の中心と移動を含む）
    float4 params;    // x = 見えている量（ディゾルブ。1 で全部）/ y = 模様の密度の倍率
};

cbuffer FrameBuffer : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraPosition;
    float gTextureTiling;      // 岩の空間 1 あたりの模様の繰り返し数
    float4 gExteriorTint;
    float4 gInteriorTint;
    float3 gDissolveEdgeColor;
    float gNormalStrength;
    float gAmbient;
    float gDissolveEdgeWidth;
    float gDissolveNoiseScale;
    float gInteriorEmissive;   // 断面をうっすら光らせる量（割れた直後の熱など）
    float gBackfaceShade;      // ディゾルブの穴から見える裏面（岩の内側）の明るさ
    float3 gFramePadding;
};

cbuffer DrawConstants : register(b3)
{
    uint gInstanceBase;        // この描画の 1 個目が InstanceData の何番目か
    uint gFragmentMode;        // 0 = 割る前の岩 / 1 = 破片
    uint gFragmentCount;       // 1 個の岩の破片の数
};

StructuredBuffer<InstanceData> gInstances : register(t0);

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 rockPosition : POSITION1;   // 割る前の岩の空間（模様とディゾルブはこれに貼り付く）
    float3 rockNormal : NORMAL0;       // 割る前の岩の空間の法線
    // 岩の空間 → ワールドの回転（行列の 3 行。凹凸を付けた法線をワールドへ移す）
    nointerpolation float3 axis0 : NORMAL1;
    nointerpolation float3 axis1 : NORMAL2;
    nointerpolation float3 axis2 : NORMAL3;
    float interior : TEXCOORD0;
    float visibility : TEXCOORD1;
    float textureScale : TEXCOORD2;
};
