// SkinningObject3d.VS.hlslのSkinning関数をComputeShader化したもの
// 結果はRWStructuredBufferに書き込み、後段のObject3d.VSで通常頂点として描画する

// MatrixPaletteのWell構造体（C++のWellForGPUと対応）
struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

// SkinningObject3d.VSの入力頂点と同じレイアウト（VertexDataに対応）
struct Vertex
{
    float4 position;
    float2 texcoord;
    float3 normal;
    float4 tangent;  // 法線マップ用。当面はスキニングせずパススルー（アニメ法線マップは将来）
};

// VertexInfluence（C++のVertexInfluenceに対応）
// .mesh のスキンセクションと同じレイアウト: int4 index → float4 weight
struct VertexInfluence
{
    int4 index;
    float4 weight;
};

// 頂点数などのちょっとした情報
struct SkinningInformation
{
    uint numVertices;
};

StructuredBuffer<Well> gMatrixPalette : register(t0);
StructuredBuffer<Vertex> gInputVertices : register(t1);
StructuredBuffer<VertexInfluence> gInfluences : register(t2);

RWStructuredBuffer<Vertex> gOutputVertices : register(u0);

ConstantBuffer<SkinningInformation> gSkinningInformation : register(b0);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint vertexIndex = DTid.x;

    // 余分に起動したThreadは何もしない
    if (vertexIndex >= gSkinningInformation.numVertices)
    {
        return;
    }

    // 入力データを取り出す
    Vertex input = gInputVertices[vertexIndex];
    VertexInfluence influence = gInfluences[vertexIndex];

    // Skinning計算
    Vertex skinned;
    skinned.texcoord = input.texcoord;
    skinned.tangent = input.tangent;  // 当面パススルー（位置/法線のみスキニング）

    float totalWeight = influence.weight.x + influence.weight.y + influence.weight.z + influence.weight.w;

    if (totalWeight < 0.001f)
    {
        // Skinning対象外の頂点はそのまま出力
        skinned.position = input.position;
        skinned.normal = input.normal;
    }
    else
    {
        // 位置の変換
        skinned.position = mul(input.position, gMatrixPalette[influence.index.x].skeletonSpaceMatrix) * influence.weight.x;
        skinned.position += mul(input.position, gMatrixPalette[influence.index.y].skeletonSpaceMatrix) * influence.weight.y;
        skinned.position += mul(input.position, gMatrixPalette[influence.index.z].skeletonSpaceMatrix) * influence.weight.z;
        skinned.position += mul(input.position, gMatrixPalette[influence.index.w].skeletonSpaceMatrix) * influence.weight.w;
        skinned.position.w = 1.0f;

        // 法線の変換
        skinned.normal = mul(input.normal, (float3x3) gMatrixPalette[influence.index.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x;
        skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[influence.index.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y;
        skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[influence.index.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z;
        skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[influence.index.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;
        skinned.normal = normalize(skinned.normal);
    }

    // 結果を書き込み
    gOutputVertices[vertexIndex] = skinned;
}